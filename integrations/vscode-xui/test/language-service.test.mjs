import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { createRequire } from "node:module";
import { fileURLToPath } from "node:url";
import test from "node:test";

const require = createRequire(import.meta.url);
const service = require("../src/language-service");
const meta = require("../src/metadata");
const marked = (source) => {
  const offset = source.indexOf("|");
  assert.ok(offset >= 0, "A cursor marker is required");
  const text = source.slice(0, offset) + source.slice(offset + 1);
  return { model: service.analyze(text), offset, text };
};
const complete = (source) => {
  const { model, offset } = marked(source);
  return service.completions(model, offset);
};
const labels = (source) => complete(source).map((entry) => entry.label);
const view = (text) => `component Example { view { ${text} } }`;
const stack = (text) => view(`VStack() { ${text} }`);
const style = (text, target = "Button") => `component Example { style Sample for ${target} { ${text} } }`;
const has = (source, ...expected) => {
  const actual = labels(source);
  for (const name of expected) assert.ok(actual.includes(name), `${name} missing: ${actual}`);
};
const lacks = (source, ...unexpected) => {
  const actual = labels(source);
  for (const name of unexpected) assert.ok(!actual.includes(name), `${name} unexpectedly present: ${actual}`);
};

test("metadata is regenerated from the current compiler and binding enums", () => {
  execFileSync(process.execPath, [fileURLToPath(new URL("../scripts/sync-language-data.mjs", import.meta.url)), "--check"]);
  assert.equal(Object.keys(meta.controls).length, 26);
  assert.equal(meta.schemas.length, 221);
});

test("control suggestions use the actual node set and insert callable snippets", () => {
  assert.deepEqual(labels(stack("|")).sort(), Object.keys(meta.controls).sort());
  has(stack("Bu|"), "Button");
  lacks(stack("|"), "Slider", "TreeView", "Tooltip", "Label");
  const [button] = complete(stack("But|ton"));
  assert.equal(button.insertText, 'Button("${1:Button}");$0');
  assert.equal(button.end - button.start, 6);
  assert.equal(complete(stack("But|ton(\"old\");"))[0].insertText, "Button");
  assert.equal(complete(stack("VSt|"))[0].insertText, "VStack() {\n\t$0\n}");
  assert.equal(complete(stack("Con|"))[0].insertText, "Content(${1:ExistingElement});$0");
  has("component Example { view { VStack() {\n Bu|", "Button");
  has(stack('Text("done"); |'), "Button", "TextInput");
  has(stack('HStack() { Text("nested"); } |'), "Button");
});

test("properties are control-specific, parent-aware, and exclude existing arguments", () => {
  has(stack('Button("ok", |);'), "click", "id", "size", "style", "flex");
  lacks(stack('Button("ok", |);'), "value", "name", "checked", "row", "change");
  lacks(view('Button("ok", |);'), "flex", "row");
  has(view('Grid("grid") { Text("cell", |); }'), "row", "column", "rowSpan", "columnSpan");
  lacks(view('Grid("grid") { Text("cell", |); }'), "flex");
  has(view("VStack(|) {}"), "spacing", "padding");
  lacks(view("VStack(|) {}"), "id", "enabled", "help", "value");
  lacks(stack('ToggleButton("toggle", |);'), "click", "name");
  has(stack('ToggleButton("toggle", |);'), "checked", "change", "icon");
  has(stack('RangeInput("slider", |);'), "currentValue", "range", "orientation");
  lacks(stack('Progress("progress", |);'), "change", "reversed");
  has(stack('TextInput("Name", |);'), "name", "text", "submit", "placeholder");
  lacks(stack('Button("ok", click: Save, |, id: "later");'), "click", "id");
  assert.equal(complete(stack('Button("ok", cli|ck: Save);'))[0].insertText, "click");
  lacks(stack('InfoBadge("new", count: 1, |);'), "icon", "count");
  lacks(stack('InfoBadge("new", icon: Icon, |);'), "count");
  assert.deepEqual(labels(stack('Button("x", invalidAlignment: |);')), []);
  assert.deepEqual(labels(stack('constructor("x", |);')), []);
});

test("manual completion at an empty trailing Button argument offers unused properties", () => {
  for (const gap of ["", " ", "\r\n        "]) {
    const source = stack(`Button("Increment", click: Increment, id: "increment",${gap}|);`);
    has(source, "enabled", "visible", "help", "size", "icon", "style");
    lacks(source, "click", "id", "value", "name");
    const { offset } = marked(source);
    for (const entry of complete(source)) {
      assert.equal(entry.kind, "Property");
      assert.equal(entry.start, offset);
      assert.equal(entry.end, offset);
      assert.equal(entry.insertText, `${entry.label}: `);
    }
  }
});

test("nested expressions do not confuse argument completion or signature help", () => {
  for (const expression of [
    '"a,b:(x)"', '$"Value: {Format("a,b", (1, 2))}"', '@"a,""b"",c"',
    '"""a, { Button("not a node") }"""', 'new [] { Make(1, 2), Make(3, 4) }',
    '(Name: "x", Value: new[] { 1, 2 })', "Make<int, string>(1, 2)", "Make<List<int>, string>(1, 2)"
  ]) has(stack(`Button(${expression}, |);`), "click");
  assert.deepEqual(labels(stack('Button("x", size: (10, |));')), []);
  has(stack('Button("x", /* comment, ) */ |);'), "click");
  has(stack('Button("x", enabled: /* value */ |);'), "true", "false");
  const { model, offset } = marked(stack('Button("x", size: (10, |20));'));
  const help = service.signature(model, offset);
  assert.equal(help.parameters[help.activeParameter].name, "size");
});

test("comments, strings, state expressions and C# blocks suppress XUI providers", () => {
  for (const text of [
    stack('// But|\nText("x");'), stack('/* But| */'), stack('Text("But|");'),
    stack('Text($@"{Format("Bu|", 1)}");'), stack('Text("""multiline\n Bu| \n""");'),
    stack('Text("unterminated |'), "component C { state string Text = Foo(|); }",
    "component C { code csharp { void M() { Button(|); } } }",
    "component C { code csharp { void M() => Button(|); } }"
  ]) assert.deepEqual(labels(text), [], text);
  has(stack('Text("/* not a comment */"); |'), "Button");
  has(stack('/* comment */ |'), "Button");
});

test("values include booleans, enums, state, params and handler names", () => {
  has(stack('Toggle("x", checked: |);'), "true", "false");
  has(stack('CheckBox("x", checkState: |);'), "Unchecked", "Checked", "Indeterminate");
  const member = complete(stack('Button("x", icon: |);')).find((i) => i.label === "Refresh");
  assert.equal(member.insertText, "global::Xui.ButtonIcon.Refresh");
  for (const qualifier of ["global::Xui.ButtonIcon.", "Xui.ButtonIcon.", "ButtonIcon."])
    assert.equal(complete(stack(`Button("x", icon: ${qualifier}Refr|);`))[0].insertText, "Refresh");
  assert.deepEqual(labels(stack('Button("x", icon: Other.|);')), []);
  const members = "state bool Enabled = true; param string Title; code csharp { void Save() {} void Changed(bool value) {} }";
  has(`component C { ${members} view { Button(Title, enabled: |); } }`, "Enabled", "Title", "true", "false");
  has(`component C { ${members} view { Button(Title, click: |); } }`, "Save", "Changed");
  lacks(`component C { ${members} view { Button(Title, click: |); } }`, "Enabled", "Title");
  lacks(`component C { ${members} view { RangeInput("x", range: |); } }`, "Enabled");
  lacks(`component C { ${members} view { Content(|); } }`, "Enabled");
  has(`component C { ${members} view { Content(|); } }`, "Title");
  lacks(`component C { ${members} view { Button("x", ref: |); } }`, "Enabled", "Save");
});

test("styles, aliases, parts, states and state properties use the catalog", () => {
  has("component C { style A for | }", "Button", "Label", "Text", "ToggleSwitch");
  lacks("component C { style A for | }", "Tooltip", "ContentDialog", "Reveal");
  has(style("|"), "background", "fontSize", "part", "when");
  has(style("part |", "Toggle"), "indicator", "label", "mark");
  lacks(style("part |", "Toggle"), "root", "arrow");
  has(style("part indicator { | }", "Toggle"), "size", "background", "when");
  lacks(style("part indicator { | }", "Toggle"), "part", "fontSize");
  has(style("when |", "Toggle"), "checked", "hovered", "disabled");
  lacks(style("when hovered { | }"), "when", "part");
  has(style("part tile { | }", "ItemsView"), "width");
  lacks(style("part tile { when selected { | } }", "ItemsView"), "width");
  lacks(style("background: 0; |"), "background");
  lacks(style("part label {} part |"), "label");
  lacks(style("when hovered {} when |"), "hovered");
  assert.equal(complete(style("part lab|el { foreground: 0; }"))[0].insertText, "label");
  assert.equal(complete(style("when hov|ered { background: 0; }"))[0].insertText, "hovered");
  assert.deepEqual(labels(style("part text { fontStyle: |; }", "TextInput")), ["normal", "italic"]);
  assert.deepEqual(labels(style("fontStyle: |; }", "Label")), ["normal", "italic", "oblique"]);
  lacks(style("verticalAlignment: |;", "Label"), "stretch");
  has(style("verticalAlignment: |;", "Stack"), "stretch");
});

test("named styles and resources include forward declarations and filter targets", () => {
  const definitions = "style Primary for Button {} style Heading for Label {} resources { Accent: 0xFF0000; }";
  has(`component C { view { Button("ok", style: |); } ${definitions} }`, "Primary");
  lacks(`component C { view { Button("ok", style: |); } ${definitions} }`, "Heading", "Accent");
  has(`component C { view { Text("ok", style: |); } ${definitions} }`, "Heading");
  has(`component C { view { Content(Element, style: |); } ${definitions} }`, "Heading");
  lacks(`component C { view { Content(Element, style: |); } ${definitions} }`, "Primary");
  has(`component C { style New for Button basedOn | {} ${definitions} }`, "Primary");
  lacks(`component C { style New for Button basedOn | {} ${definitions} }`, "New", "Heading");
  has(`component C { style New for Button { background: resource(|); } ${definitions} }`, "Accent");
  has(`component C { view { Button("ok", foreground: |); } ${definitions} }`, "theme", "resource(Accent)");
  has(`component C { resources { First: resource(|); } ${definitions} }`, "Accent");
  lacks(`component C { resources { First: resource(|); } ${definitions} }`, "First");
  lacks(stack("Content(Element, |);"), "foreground", "fontSize");
  has(`component C { view { Content(Element, style: Heading, |); } ${definitions} }`, "fontSize", "foreground");
  lacks("component C { style A for Button basedOn | {} style B for Button basedOn A {} }", "A", "B");
  has("component C { style A for Button { part label {} } view { Content(Element, style: |); } }", "A");
});

test("hover, signatures and definitions navigate local XUI declarations", () => {
  const source = `component C { state string Title = "hello"; resources { Accent: 0; }
    style Heading for Label { foreground: resource(Accent); }
    view { VStack() { Text(Title, style: Heading, ref: Label); Button("ok", click: Save); } }
    code csharp { void Save() {} } }`;
  const model = service.analyze(source);
  for (const [search, name] of [["Text(Title", "Title"], ["style: Heading", "Heading"], ["resource(Accent", "Accent"], ["click: Save", "Save"]]) {
    const offset = source.indexOf(search) + search.length - name.length + 1;
    assert.equal(service.definition(model, offset)?.name, name, search);
  }
  assert.equal(service.hover(model, source.indexOf("Text(Title") + 1).title, "Text");
  assert.equal(service.hover(model, source.indexOf("click:") + 1).title, "click: void handler()");
  assert.ok(model.declarations.some((d) => d.kind === "ref" && d.name === "Label"));
  const { model: partial, offset } = marked(stack('TextInput("x", submit: |);'));
  const help = service.signature(partial, offset);
  assert.equal(help.parameters[help.activeParameter].label, "submit: void handler()");
});

test("UTF-16 ranges, CRLF, escaped identifiers and cancellation", () => {
  const source = 'component C {\r\n state string @Title = "😀";\r\n view { VStack() { Text(@Ti|tle); } }\r\n}';
  const { text, offset, model } = marked(source);
  const entry = service.completions(model, offset)[0];
  assert.equal(entry.label, "@Title");
  assert.equal(text.slice(entry.start, entry.end), "@Title");
  assert.equal(service.definition(model, offset)?.name, "@Title");
  assert.equal(service.analyze(text, () => true), undefined);
  has('component C { state string 标题 = ""; view { Text(标|); } }', "标题");
});

test("partially typed documents never throw and keep completion replacement ranges local", () => {
  const source = `component C {
    state string Title = "😀";
    resources { Accent: theme(light: 0xFFFFFF, dark: 0); }
    style Heading for Label { foreground: resource(Accent); when disabled { foreground: 0; } }
    view { VStack() { Text(Title, style: Heading); Button("ok", click: Save); } }
    code csharp { void Save() { var text = $"Value: {Format("x", 1)}"; } }
  }`;
  for (let offset = 0; offset <= source.length; ++offset) {
    const model = service.analyze(source.slice(0, offset));
    for (const entry of service.completions(model, offset)) {
      assert.ok(entry.start <= offset && entry.end >= offset && entry.end <= model.text.length);
    }
    service.signature(model, offset);
    service.hover(model, offset);
    service.definition(model, offset);
  }
});

test("every control has documented properties and every catalog part completes precisely", () => {
  for (const control of Object.keys(meta.controls)) {
    assert.ok(meta.controlDescriptions[control], control);
    const args = labels(stack(`${control}(${meta.isStack(control) ? "" : '"x", '}|)${meta.containers.has(control) ? "{}" : ";"}`));
    const expected = meta.argumentsFor(control, "VStack");
    assert.deepEqual(args.sort(), expected.sort(), control);
  }
  for (const schema of meta.schemas) {
    const wrap = (body) => style(schema.part === "root" ? body : `part ${schema.part} { ${body} }`, schema.target);
    const actual = labels(wrap("|")).filter((name) => !["part", "when"].includes(name));
    assert.deepEqual(actual.sort(), [...schema.properties].sort(), `${schema.target}.${schema.part}`);
    for (const state of schema.states) {
      assert.deepEqual(labels(wrap(`when ${state} { | }`)).sort(), [...schema.stateProperties].sort(), `${schema.target}.${schema.part}:${state}`);
    }
  }
});
