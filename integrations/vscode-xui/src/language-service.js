"use strict";

const meta = require("./metadata");
const identifier = /@?[\p{L}_][\p{L}\p{N}\p{M}_]*/uy;
const simpleName = /^@?[\p{L}\p{N}\p{M}_]*$/u;
const isName = (token) => token?.kind === "name";
const key = (name) => name?.replace(/^@/, "");

// Strings and comments are opaque: XUI providers must not take over embedded C#.
function stringEnd(text, start, depth = 0) {
  const prefix = text.slice(start).match(/^(?:\$+@?|@\$?)?("|')/);
  if (!prefix) return undefined;
  if (depth > 128) return { end: text.length, closed: false };
  const quote = prefix[1], verbatim = prefix[0].includes("@"), interpolated = prefix[0].includes("$");
  let i = start + prefix[0].length;
  const raw = quote === '"' && !verbatim ? text.slice(i - 1).match(/^"{3,}/)?.[0].length : 0;
  if (raw) {
    const end = text.indexOf('"'.repeat(raw), i - 1 + raw);
    return { end: end < 0 ? text.length : end + raw, closed: end >= 0 };
  }
  while (i < text.length) {
    if (!verbatim && text[i] === "\\") { i += 2; continue; }
    if (text[i] === quote) {
      if (verbatim && text[i + 1] === quote) { i += 2; continue; }
      return { end: i + 1, closed: true };
    }
    if (interpolated && text[i] === "{") {
      if (text[i + 1] === "{") { i += 2; continue; }
      let braces = 1;
      ++i;
      while (i < text.length && braces) {
        const comment = commentEnd(text, i);
        const nested = comment ?? stringEnd(text, i, depth + 1);
        if (nested) { i = nested.end; continue; }
        if (text[i] === "{") ++braces;
        if (text[i] === "}") --braces;
        ++i;
      }
      continue;
    }
    if (!verbatim && /[\r\n]/.test(text[i])) return { end: i, closed: false };
    ++i;
  }
  return { end: text.length, closed: false };
}

function commentEnd(text, start) {
  if (text.startsWith("//", start)) {
    const end = text.indexOf("\n", start);
    return { end: end < 0 ? text.length : end, closed: false };
  }
  if (text.startsWith("/*", start)) {
    const end = text.indexOf("*/", start + 2);
    return { end: end < 0 ? text.length : end + 2, closed: end >= 0 };
  }
}

function tokenize(text, cancelled) {
  const tokens = [], opaque = [], stack = [];
  for (let i = 0; i < text.length;) {
    if (cancelled()) return undefined;
    if (/\s/.test(text[i])) { ++i; continue; }
    const start = i;
    const comment = commentEnd(text, i);
    const literal = comment ?? stringEnd(text, i);
    if (literal) {
      i = literal.end;
      opaque.push({ start, ...literal, comment: !!comment });
      if (comment) continue;
      tokens.push({ text: text.slice(start, i), start, end: i, kind: "string" });
      continue;
    }
    identifier.lastIndex = i;
    const name = identifier.exec(text);
    const value = name?.[0] ?? (text.startsWith("::", i) || text.startsWith("=>", i) ? text.slice(i, i + 2) : text[i]);
    i += value.length;
    const token = { text: value, start, end: i, kind: name ? "name" : "punctuation" };
    const index = tokens.length;
    if (["(", "[", "{"].includes(value)) stack.push(index);
    const opening = { ")": "(", "]": "[", "}": "{" }[value];
    if (opening) {
      const match = stack.findLastIndex((j) => tokens[j].text === opening);
      if (match >= 0) {
        token.open = stack[match];
        tokens[token.open].close = index;
        stack.length = match;
      }
    }
    tokens.push(token);
  }
  // A generic method's type-argument commas are not XUI argument separators.
  const angles = [];
  for (let i = 0; i < tokens.length; ++i) {
    if (tokens[i].text === "<" && isName(tokens[i - 1])) angles.push(i);
    else if (tokens[i].text === ">" && angles.length) {
      const open = angles.pop();
      if ([">", "(", ".", "::"].includes(tokens[i + 1]?.text) &&
          tokens.slice(open + 1, i).every((t) => isName(t) || [".", "::", ",", "<", ">", "[", "]", "?"].includes(t.text)))
        tokens[open].close = i;
    } else if (!isName(tokens[i]) && ![".", "::", ",", "[", "]", "?"].includes(tokens[i].text)) angles.length = 0;
  }
  return { tokens, opaque };
}

function analyze(text, cancelled = () => false) {
  const lexical = tokenize(text, cancelled);
  if (!lexical) return undefined;
  const { tokens } = lexical;
  const model = { text, ...lexical, regions: [], nodes: [], declarations: [], styles: [], resources: [], members: [] };
  const close = (i) => tokens[i]?.close ?? tokens.length;
  const at = (i) => tokens[i]?.text;
  const start = (i) => tokens[i]?.start ?? text.length;
  const skip = (i) => tokens[i]?.close !== undefined || ["(", "[", "{"].includes(at(i)) ? close(i) + 1 : i + 1;
  const until = (i, stops, end) => {
    while (i < end && !stops.includes(at(i))) i = skip(i);
    return Math.min(i, end);
  };
  const declaration = (kind, token, end, detail, extra = {}) => {
    const item = { kind, name: token.text, start: token.start, end, nameEnd: token.end, detail, ...extra };
    model.declarations.push(item);
    return item;
  };
  const region = (kind, open, extra = {}) => {
    const item = { kind, start: tokens[open].end, end: start(close(open)), from: open + 1, to: close(open), ...extra };
    model.regions.push(item);
    return item;
  };
  const segments = (from, to, separator, boundary) => {
    const result = [];
    let position = boundary;
    for (let i = from; i <= to;) {
      const end = until(i, [separator], to);
      result.push({ from: i, to: end, start: position, end: start(end) });
      position = tokens[end]?.end ?? text.length;
      i = end + 1;
    }
    return result;
  };
  const property = (segment) => {
    const named = isName(tokens[segment.from]) && at(segment.from + 1) === ":";
    const valueFrom = segment.from + (named ? 2 : 0);
    return { ...segment, name: named ? at(segment.from) : undefined, valueFrom,
      valueStart: named ? tokens[segment.from + 1].end : segment.start,
      value: text.slice(start(valueFrom), start(segment.to)).trim() };
  };

  const componentIndex = tokens.findIndex((t, i) => t.text === "component" && isName(tokens[i + 1]) && at(i + 2) === "{");
  if (componentIndex < 0) return model;
  const component = declaration("component", tokens[componentIndex + 1], tokens[close(componentIndex + 2)]?.end ?? text.length, "XUI component");
  model.component = component;
  const body = region("component", componentIndex + 2);
  const views = [], styleBodies = [];
  for (let i = body.from; i < body.to;) {
    if (cancelled()) return undefined;
    const end = until(i, [";", "{"], body.to);
    if (at(i) === "state" || at(i) === "param") {
      const equals = until(i + 1, ["=", ";"], end);
      const nameIndex = equals - 1;
      if (isName(tokens[nameIndex]) && nameIndex > i + 1) {
        const type = text.slice(start(i + 1), start(nameIndex)).trim();
        model.members.push(declaration(at(i), tokens[nameIndex], tokens[end]?.end ?? text.length, type));
      }
      // Initializers may contain object/collection bodies; consume through the semicolon.
      i = until(i + 1, [";"], body.to) + 1;
    } else if (at(i) === "view" && at(i + 1) === "{") {
      views.push(region("children", i + 1));
      i = close(i + 1) + 1;
    } else if (at(i) === "resources" && at(i + 1) === "{") {
      const resources = region("resources", i + 1);
      resources.properties = segments(resources.from, resources.to, ";", resources.start).map(property);
      for (const prop of resources.properties)
        if (prop.name) model.resources.push(declaration("resource", tokens[prop.from], prop.end, "RGB24 color resource"));
      i = close(i + 1) + 1;
    } else if (at(i) === "style" && isName(tokens[i + 1])) {
      const target = at(i + 2) === "for" ? at(i + 3) : undefined;
      const base = at(i + 4) === "basedOn" ? at(i + 5) : undefined;
      const style = declaration("style", tokens[i + 1], tokens[close(end)]?.end ?? text.length, `style for ${target ?? "?"}`, { target, base, headerStart: start(i), headerEnd: start(end) });
      model.styles.push(style);
      if (at(end) === "{") styleBodies.push(region("style", end, { style, target, part: "root", state: undefined }));
      i = at(end) === "{" ? close(end) + 1 : end + 1;
    } else if (at(i) === "code" && at(i + 1) === "csharp" && at(i + 2) === "{") {
      const code = region("code", i + 2);
      for (let j = code.from; j < code.to;) {
        const boundary = until(j, ["(", ";", "{"], code.to);
        if (at(boundary) === "(" && isName(tokens[boundary - 1])) {
          const tail = until(close(boundary) + 1, ["{", ";"], code.to);
          model.members.push(declaration("method", tokens[boundary - 1], tokens[at(tail) === "{" ? close(tail) : tail]?.end ?? text.length,
            text.slice(start(j), tokens[close(boundary)]?.end ?? start(boundary)).trim()));
          j = at(tail) === "{" ? close(tail) + 1 : tail + 1;
        } else j = boundary < code.to ? skip(boundary) : code.to;
      }
      i = close(i + 2) + 1;
    } else i = end < body.to ? skip(end) : body.to;
  }

  for (const current of views) {
    for (let i = current.from; i < current.to;) {
      if (cancelled()) return undefined;
      if (isName(tokens[i]) && at(i + 1) === "(") {
        const callEnd = close(i + 1);
        const node = { kind: at(i), start: start(i), nameEnd: tokens[i].end, end: tokens[callEnd]?.end ?? text.length,
          argsStart: tokens[i + 1].end, argsEnd: start(callEnd), parent: current.node?.kind };
        node.args = segments(i + 2, callEnd, ",", node.argsStart).map(property);
        model.nodes.push(node);
        const ref = node.args.find((p) => p.name === "ref");
        if (ref && isName(tokens[ref.valueFrom]) && ref.to === ref.valueFrom + 1)
          model.members.push(declaration("ref", tokens[ref.valueFrom], tokens[ref.valueFrom].end, `Xui.${meta.targetName(node.kind)}`));
        if (at(callEnd + 1) === "{") {
          views.push(region("children", callEnd + 1, { node }));
          node.end = tokens[close(callEnd + 1)]?.end ?? text.length;
          i = close(callEnd + 1) + 1;
        } else i = callEnd + 1;
      } else i = skip(i);
    }
  }
  for (const current of styleBodies) {
    current.properties = [];
    current.parts = [];
    current.states = [];
    for (let i = current.from; i < current.to;) {
      if (cancelled()) return undefined;
      if (["part", "when"].includes(at(i)) && isName(tokens[i + 1]) && at(i + 2) === "{") {
        const isPart = at(i) === "part";
        (isPart ? current.parts : current.states).push(at(i + 1));
        styleBodies.push(region("style", i + 2, { style: current.style, target: current.target,
          part: isPart ? at(i + 1) : current.part, state: isPart ? undefined : at(i + 1) }));
        i = close(i + 2) + 1;
      } else {
        const end = until(i, [";"], current.to);
        current.properties.push(property({ from: i, to: end, start: start(i), end: start(end) }));
        i = end + 1;
      }
    }
  }
  return model;
}

function sourceBetween(model, start, end) {
  let result = "", position = start;
  for (const span of model.opaque) {
    if (!span.comment || span.end <= start || span.start >= end) continue;
    result += model.text.slice(position, Math.max(position, span.start)) + " ";
    position = Math.min(span.end, end);
  }
  return result + model.text.slice(position, end);
}

function context(model, offset) {
  const opaque = model.opaque.some((r) => offset > r.start && (offset < r.end || (!r.closed && offset === r.end)));
  const token = model.tokens.find((t) => t.start <= offset && offset <= t.end && isName(t));
  const start = token?.start ?? offset, end = token?.end ?? offset;
  const prefix = model.text.slice(start, offset);
  const region = model.regions.filter((r) => r.start <= offset && offset <= r.end).at(-1);
  const node = model.nodes.findLast((n) => n.argsStart <= offset && offset <= n.argsEnd);
  const arg = node?.args.find((p) => p.start <= offset && offset <= p.end);
  const styleTarget = node?.kind === "Content" ? model.styles.find((s) => key(s.name) === key(node.args.find((p) => p.name === "style")?.value))?.target : node?.kind;
  const schema = meta.schemaFor(node ? styleTarget : region?.target, node ? "root" : region?.part);
  return { opaque, token, start, end, prefix, region, node, arg, schema, styleTarget };
}

const item = (label, kind, detail, insertText = label, description = "") => ({ label, kind, detail, insertText, description });
const memberItem = (member) => item(member.name, member.kind === "method" ? "Method" : "Variable", member.detail);
const declarationItems = {
  namespace: "namespace ${1:Demo};", component: "component ${1:Example} {\n\tview {\n\t\tVStack() {\n\t\t\t$0\n\t\t}\n\t}\n}",
  param: "param ${1:string} ${2:Title};", state: "state ${1:int} ${2:Count} = ${3:0};",
  view: "view {\n\tVStack() {\n\t\t$0\n\t}\n}", code: "code csharp {\n\t$0\n}",
  resources: "resources {\n\t${1:Accent}: ${2:0x2468AD};\n\t$0\n}",
  style: "style ${1:Heading} for ${2:Label} {\n\t$0\n}"
};
const snippet = (label, kind, detail, text, description) => ({ ...item(label, kind, detail, text, description), snippet: true });

function genericStyle(model, style) {
  const visited = new Set();
  while (style && !visited.has(style)) {
    visited.add(style);
    if (meta.targetName(style.target) !== "Button" || model.regions.some((r) => r.style === style &&
        (r.part !== "root" || r.properties?.some((p) => p.name && !["background", "foreground", "borderBrush", "cornerRadius", "borderThickness", "padding"].includes(p.name))))) return true;
    style = model.styles.find((s) => key(s.name) === key(style.base));
  }
  return false;
}

function namedStyles(model, target, exclude, content = false) {
  const derivesFrom = (style) => {
    const visited = new Set();
    while (style && !visited.has(style)) {
      if (style === exclude) return true;
      visited.add(style);
      style = model.styles.find((s) => key(s.name) === key(style.base));
    }
    return false;
  };
  return model.styles.filter((s) => (!target || meta.targetName(s.target) === meta.targetName(target)) &&
    !derivesFrom(s) && (!content || genericStyle(model, s)))
    .map((s) => item(s.name, "Reference", s.detail));
}

function valueItems(model, info, ctx, offset, expression) {
  const valuePrefix = sourceBetween(model, expression.valueStart, offset).trim();
  const resource = valuePrefix.match(/^resource\s*\(\s*(@?[\p{L}\p{N}\p{M}_]*)$/u);
  const resources = model.resources.filter((s) => ctx.region?.kind !== "resources" || key(s.name) !== key(expression.name));
  if (info.color && resource) return resources.map((s) => item(s.name, "Reference", s.detail));
  if (info.color && !simpleName.test(valuePrefix)) return [];
  if (info.type === "style identifier") return simpleName.test(valuePrefix) ?
    namedStyles(model, ctx.node.kind === "Content" ? undefined : ctx.node.kind, undefined, ctx.node.kind === "Content") : [];
  if (info.handler) return simpleName.test(valuePrefix) ? model.members.filter((m) => m.kind === "method").map(memberItem) : [];
  if (info.type === "identifier") return [];
  const values = [];
  if (info.values) values.push(...info.values.map((v) => item(v, "EnumMember", info.type)));
  if (info.type === "bool" && !info.values) values.push(...["true", "false"].map((v) => item(v, "Value", "bool")));
  if (meta.hasEnum(info.type)) {
    const qualifier = valuePrefix.match(/^(?:(?:global::)?Xui\.)?(\w+)\.\w*$/);
    if (qualifier && qualifier[1] !== info.type) return [];
    if (!qualifier && !simpleName.test(valuePrefix)) return [];
    values.push(...meta.enums[info.type].map((v) => item(v, "EnumMember", `Xui.${info.type}`,
      qualifier ? v : `global::Xui.${info.type}.${v}`)));
    if (qualifier) return values;
  } else if (!info.color && !simpleName.test(valuePrefix)) return [];
  if (info.color) {
    values.push(snippet("theme", "Function", "Light and dark RGB24 colors", "theme(light: ${1:0xFFFFFF}, dark: ${2:0x202020})"));
    values.push(...resources.map((s) => item(`resource(${s.name})`, "Reference", s.detail)));
    values.push(item("0x000000", "Color", "RGB24 black"), item("0xFFFFFF", "Color", "RGB24 white"));
  }
  if (info.type === "insets") values.push(snippet("insets", "Value", "(left, top, right, bottom)", "(${1:0}, ${2:0}, ${3:0}, ${4:0})"));
  if (info.type === "(float Width, float Height)") values.push(snippet("size", "Value", info.type, "(${1:100}, ${2:100})"));
  if (info.type === "ulong?") values.push(item("null", "Value", "No selection"));
  const structural = ctx.node && !info.color && !info.values && !["insets", "string literal", "positive dimension", "integer literal", "dimension literal"].includes(info.type);
  if (structural) values.push(...model.members.filter((m) => ["state", "param"].includes(m.kind) &&
    (m.kind !== "state" || !["range", "row", "column", "rowSpan", "columnSpan", "flex"].includes(expression.name)) &&
    (m.kind !== "state" || expression.name || !["Content", "Grid"].includes(ctx.node.kind))).map(memberItem));
  return values;
}

function completions(model, offset) {
  const ctx = context(model, offset), { node, arg, region, schema } = ctx;
  if (ctx.opaque || region?.kind === "code") return [];
  let values = [];
  if (node && meta.hasControl(node.kind) && arg) {
    if (arg.name && offset >= arg.valueStart) {
      if (!meta.argumentsFor(node.kind, node.parent, ctx.styleTarget).includes(arg.name)) return [];
      const info = meta.propertyInfo(arg.name, node.kind, schema);
      values = valueItems(model, info, ctx, offset, arg);
    } else if (!arg.name && arg === node.args[0] && !meta.isStack(node.kind)) {
      values = valueItems(model, { type: node.kind === "Content" ? "Element" : "string" }, ctx, offset, arg);
    } else if (simpleName.test(sourceBetween(model, arg.start, offset).trim())) {
      const used = new Set(node.args.filter((p) => p !== arg).map((p) => p.name));
      if (node.kind === "InfoBadge") {
        if (used.has("icon")) used.add("count");
        if (used.has("count")) used.add("icon");
      }
      values = meta.argumentsFor(node.kind, node.parent, ctx.styleTarget).filter((name) => !used.has(name)).map((name) => {
        const info = meta.propertyInfo(name, node.kind, schema);
        return item(name, "Property", `${name}: ${info.type}`, name + (arg.name ? "" : ": "), info.description);
      });
    }
  } else {
    const header = model.styles.find((s) => s.headerStart <= offset && offset <= s.headerEnd);
    if (header) {
      const before = model.text.slice(header.headerStart, ctx.start).replace(/\/\*[\s\S]*?\*\/|\/\/[^\r\n]*/g, " ").trim();
      if (/\bfor$/.test(before)) values = [...new Set([...meta.schemas.map((s) => s.target), ...Object.keys(meta.aliases)])].map((t) => item(t, "Class", "Style target"));
      else if (/\bbasedOn$/.test(before)) values = namedStyles(model, header.target, header);
      else if (/\bfor\s+\w+$/.test(before)) values = [item("basedOn", "Keyword", "Derive from a matching named style", "basedOn ")];
    } else if (region?.kind === "style" || region?.kind === "resources") {
      const prop = region.properties?.find((p) => p.name && p.start <= offset && offset <= p.end);
      if (prop && offset >= prop.valueStart) {
        const info = region.kind === "resources" ? { type: "RGB24 color", color: true } : meta.propertyInfo(prop.name, undefined, schema, true);
        values = valueItems(model, info, ctx, offset, prop);
      } else if (region.kind === "style" && schema) {
        const previous = model.tokens.filter((t) => t.end <= ctx.start && t.start >= region.start).at(-1)?.text;
        const blockItem = (name, kind, detail) => model.text.slice(ctx.end).trimStart().startsWith("{") ?
          item(name, kind, detail) : snippet(name, kind, detail, `${name} {\n\t$0\n}`);
        if (previous === "part" && region.part === "root" && !region.state)
          values = meta.schemas.filter((s) => s.target === schema.target && s.part !== "root" &&
            (!region.parts.includes(s.part) || s.part === ctx.token?.text))
            .map((s) => blockItem(s.part, "Field", `${s.target} part`));
        else if (previous === "when" && !region.state)
          values = schema.states.filter((s) => !region.states.includes(s) || s === ctx.token?.text)
            .map((s) => blockItem(s, "EnumMember", "Style state"));
        else if (!previous || [";", "{", "}"].includes(previous)) {
          const used = new Set(region.properties.filter((p) => p !== prop).map((p) => p.name));
          values = (region.state ? schema.stateProperties : schema.properties).filter((name) => !used.has(name))
            .map((name) => {
              const info = meta.propertyInfo(name, undefined, schema, true);
              return item(name, "Property", `${name}: ${info.type}`, name + (prop ? "" : ": "), info.description);
            });
          if (!region.state && schema.states.length) values.push(item("when", "Keyword", "State-specific properties", "when "));
          if (!region.state && region.part === "root") values.push(item("part", "Keyword", "Named visual part", "part "));
        }
      }
    } else if (region?.kind === "children") {
      const previous = model.tokens.filter((t) => t.end <= ctx.start && t.start >= region.start).at(-1)?.text;
      if (!previous || [";", "}"].includes(previous)) {
        values = Object.keys(meta.controls).map((name) => {
          const positional = meta.isStack(name) ? "" : name === "Content" ? "${1:ExistingElement}" : '"${1:' + name + '}"';
          const suffix = meta.containers.has(name) ? " {\n\t$0\n}" : ";$0";
          const alreadyCall = model.text.slice(ctx.end).trimStart().startsWith("(");
          return snippet(name, "Constructor", name, alreadyCall ? name : `${name}(${positional})${suffix}`, meta.controlDescriptions[name]);
        });
      }
    } else if (region?.kind === "component" || !model.component) {
      const previous = model.tokens.filter((t) => t.end <= ctx.start).at(-1)?.text;
      if (!previous || [";", "{", "}"].includes(previous)) {
        const names = model.component ? ["param", "state", "view", "code", "resources", "style"] : ["namespace", "component"];
        values = names.filter((name) => !(name === "view" && model.nodes.length) && !(name === "code" && model.regions.some((r) => r.kind === "code")))
          .map((name) => snippet(name, "Keyword", "XUI declaration", declarationItems[name]));
      }
    }
  }
  return values.filter((v) => v.label.toLowerCase().startsWith(ctx.prefix.toLowerCase()))
    .map((v) => ({ ...v, start: ctx.start, end: ctx.end }));
}

function signature(model, offset) {
  const ctx = context(model, offset), { node, arg, schema } = ctx;
  if (ctx.opaque || !node || !meta.hasControl(node.kind)) return undefined;
  const parameters = [];
  if (!meta.isStack(node.kind)) parameters.push({ name: "value", label: node.kind === "Content" ? "element: Element" : "text: string", description: "Required positional argument; do not write value:." });
  for (const name of meta.argumentsFor(node.kind, node.parent, ctx.styleTarget)) {
    const info = meta.propertyInfo(name, node.kind, schema);
    parameters.push({ name, label: `${name}: ${info.type}`, description: info.description });
  }
  const active = parameters.findIndex((p) => p.name === (arg?.name ?? "value"));
  return { label: `${node.kind}(${parameters.map((p) => p.label).join(", ")})`, description: meta.controlDescriptions[node.kind],
    parameters, activeParameter: Math.max(0, active) };
}

function definition(model, offset) {
  const ctx = context(model, offset);
  if (ctx.opaque || !ctx.token || ctx.region?.kind === "code") return undefined;
  const index = model.tokens.indexOf(ctx.token), previous = model.tokens[index - 1]?.text;
  if (previous === "." || previous === "::") return undefined;
  if (ctx.node && ctx.arg && offset >= ctx.arg.valueStart) {
    const name = ctx.arg.name;
    const pool = name === "style" ? model.styles : previous === "(" && model.tokens[index - 2]?.text === "resource" ? model.resources :
      name === "ref" || meta.events.has(name) || !meta.colors.has(name) ? model.members : [];
    return pool.find((s) => key(s.name) === key(ctx.token.text));
  }
  if (previous === "basedOn") return model.styles.find((s) => key(s.name) === key(ctx.token.text));
  if (previous === "(" && model.tokens[index - 2]?.text === "resource")
    return model.resources.find((s) => key(s.name) === key(ctx.token.text));
}

function hover(model, offset) {
  const ctx = context(model, offset);
  if (ctx.opaque || !ctx.token || ctx.region?.kind === "code") return undefined;
  const node = model.nodes.find((n) => n.start === ctx.start);
  if (node && meta.hasControl(node.kind)) return { start: ctx.start, end: ctx.end, title: node.kind, description: meta.controlDescriptions[node.kind] };
  let info;
  if (ctx.node && ctx.arg?.name === ctx.token.text && offset < ctx.arg.valueStart &&
      meta.argumentsFor(ctx.node.kind, ctx.node.parent, ctx.styleTarget).includes(ctx.arg.name))
    info = meta.propertyInfo(ctx.arg.name, ctx.node.kind, ctx.schema);
  else if (ctx.region?.kind === "style" && ctx.region.properties.some((p) => p.name === ctx.token.text && p.start === ctx.start))
    info = meta.propertyInfo(ctx.token.text, undefined, ctx.schema, true);
  if (info) return { start: ctx.start, end: ctx.end, title: `${ctx.token.text}: ${info.type}`, description: info.description };
  const declared = definition(model, offset) ?? model.declarations.find((s) => s.start === ctx.start);
  if (declared) return { start: ctx.start, end: ctx.end, title: declared.name, description: declared.detail };
}

module.exports = { analyze, completions, signature, hover, definition };
