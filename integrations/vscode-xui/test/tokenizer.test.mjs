import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { createRequire } from "node:module";
import test from "node:test";
import textmate from "vscode-textmate";
import oniguruma from "vscode-oniguruma";

const require = createRequire(import.meta.url);
const wasm = await readFile(require.resolve("vscode-oniguruma/release/onig.wasm"));
await oniguruma.loadWASM(wasm.buffer.slice(wasm.byteOffset, wasm.byteOffset + wasm.byteLength));
const manifest = JSON.parse(await readFile(new URL("../package.json", import.meta.url), "utf8"));
const registry = new textmate.Registry({
  onigLib: Promise.resolve({
    createOnigScanner: (patterns) => new oniguruma.OnigScanner(patterns),
    createOnigString: (text) => new oniguruma.OnigString(text)
  }),
  loadGrammar: async (scope) => {
    const path = scope === "source.xui"
      ? new URL("../syntaxes/xui.tmLanguage.json", import.meta.url)
      : scope === "source.cs"
        ? new URL("./cache/csharp.tmLanguage", import.meta.url)
        : null;
    assert.ok(path, `Unexpected external grammar: ${scope}`);
    return textmate.parseRawGrammar(await readFile(path, "utf8"), path.pathname);
  }
});
const grammar = await registry.loadGrammarWithEmbeddedLanguages("source.xui", 1,
  Object.fromEntries(Object.keys(manifest.contributes.grammars[0].embeddedLanguages).map((scope) => [scope, 2])));
test.after(() => registry.dispose());

function tokenize(source) {
  const lines = source.split(/\r?\n/);
  let stack = textmate.INITIAL;
  const tokens = lines.map((line) => {
    const result = grammar.tokenizeLine(line, stack);
    assert.equal(result.stoppedEarly, false);
    stack = result.ruleStack;
    return result.tokens;
  });
  return { lines, tokens, stack };
}

function scopesAt(document, needle, offset = 0, occurrence = 0) {
  let remaining = occurrence;
  for (let row = 0; row < document.lines.length; row++) {
    const line = document.lines[row];
    let from = 0;
    while (from <= line.length) {
      const index = line.indexOf(needle, from);
      if (index < 0) break;
      if (remaining-- === 0) {
        const column = index + offset;
        return document.tokens[row].find((token) => token.startIndex <= column && token.endIndex > column)?.scopes ?? [];
      }
      from = index + needle.length;
    }
  }
  assert.fail(`Text not found: ${needle} (occurrence ${occurrence})`);
}

function has(document, needle, expected, offset = 0, occurrence = 0) {
  const scopes = scopesAt(document, needle, offset, occurrence);
  assert.ok(scopes.some((scope) => scope === expected || scope.startsWith(`${expected}.`)),
    `${JSON.stringify(needle)} at +${offset}: expected ${expected}, got ${scopes.join(" ")}`);
}

function outsideCsharp(document, needle, offset = 0) {
  const scopes = scopesAt(document, needle, offset);
  assert.ok(!scopes.some((scope) => scope.startsWith("meta.embedded.")), `${needle} leaked C# scopes: ${scopes}`);
}

function closed(document) {
  assert.equal(document.stack.depth, 1, "All lexical contexts must close at EOF");
}

const counter = await readFile(new URL("./fixtures/counter.xui", import.meta.url), "utf8");
const nested = await readFile(new URL("./fixtures/nested.xui", import.meta.url), "utf8");

test("named style declarations retain XUI scopes and return to view and C# contexts", async () => {
  const source = await readFile(new URL("../../../bindings/dotnet/GeneratorTests/Fixtures/Styling.xui", import.meta.url), "utf8");
  const doc = tokenize(source);
  has(doc, "resources", "keyword.declaration.resources.xui");
  has(doc, "DangerFill:", "entity.name.constant.resource.xui");
  has(doc, "theme(", "support.function.color.xui");
  has(doc, "light:", "variable.parameter.named.xui");
  has(doc, "0xB42318", "constant.numeric");
  has(doc, "style DangerButton", "keyword.declaration.style.xui");
  has(doc, "DangerButton for", "entity.name.type.style.xui");
  has(doc, "basedOn", "keyword.other.style.xui");
  has(doc, "BaseButton {", "variable.other.style.xui");
  has(doc, "background:", "support.type.property-name.xui");
  has(doc, "resource(DangerFill)", "support.function.color.xui");
  has(doc, "resource(DangerFill)", "variable.other.resource.xui", "resource(".length);
  for (const state of ["focused", "checked", "hovered", "pressed", "disabled"])
    has(doc, `when ${state}`, "constant.language.style-state.xui", "when ".length);
  outsideCsharp(doc, "when disabled");
  has(doc, "Button(\"Delete\"", "support.class.node.xui");
  has(doc, "style:", "variable.parameter.named.xui");
  has(doc, "void SetEntry", "meta.embedded.block.csharp");
  closed(doc);
});

test("style headers, references, state rules and colors tolerate comments and line breaks", () => {
  const doc = tokenize(`component Styled {
  resources /* { */ {
    @Base: 0b1010_0011u;
    Alias: resource /* ) */ (
      // not a theme label
      @Base);
    Pair: @theme /* ( */ (
      light /* : */ : 0xAABBCCu,
      dark: 123UL);
  }
  style Button for /* target */ Button
    basedOn /* base */
      @BaseStyle {
    borderBrush: resource(Alias);
    padding: (1.5f, 2m, 3e1, 0);
    when /* } */
      hovered /* { */ {
      background: theme(light: 0, dark: 0xFFFFFF);
    }
    when disabled { foreground: 0; }
  }
  style @BaseStyle for Button { cornerRadius: 0; }
  view {
    Button("Go", style: /* reference */ @BaseStyle,
      background: resource /* ) */ (Alias), click: Run);
  }
  code csharp { void Run() {} }
}`);
  has(doc, "style Button", "entity.name.type.style.xui", "style ".length);
  has(doc, "/* target */ Button", "support.class.node.xui", "/* target */ ".length);
  has(doc, "@BaseStyle {", "variable.other.style.xui");
  has(doc, "/* target */", "comment.block");
  has(doc, "@Base:", "entity.name.constant.resource.xui");
  has(doc, "@Base);", "variable.other.resource.xui");
  has(doc, "@theme", "support.function.color.xui");
  has(doc, "light /*", "variable.parameter.named.xui");
  for (const literal of ["0b1010_0011u", "0xAABBCCu", "123UL", "1.5f", "2m", "3e1"])
    has(doc, literal, "constant.numeric");
  for (const state of ["hovered /*", "disabled {"])
    has(doc, state, "constant.language.style-state.xui");
  has(doc, "/* reference */ @BaseStyle", "variable.other.style.xui", "/* reference */ ".length);
  has(doc, "background: resource", "support.function.color.xui", "background: ".length);
  has(doc, "click:", "variable.parameter.named.xui");
  has(doc, "void Run", "meta.embedded.block.csharp");
  closed(doc);
});

test("style color syntax does not override ordinary C# calls, strings or comments", () => {
  const doc = tokenize(String.raw`component Calls {
  state object First = theme("light: )", resource("name"));
  state object Second = resource(@"dark: )", /* ) */ () => { return theme(1); });
  view {
    Text(theme("caption"), id: "theme(resource)");
    Button(resource("label"), click: Handle,
      enabled: theme(true), background: theme(light: 0, dark: 1));
  }
  code csharp {
    object Handle() => resource(theme("code"));
  }
}`);
  for (const needle of ['theme("light:', 'resource("name"', 'resource(@"dark:', 'theme("caption"',
    'resource("label"', "theme(true)", 'resource(theme("code"']) {
    has(doc, needle, "entity.name.function");
    assert.ok(!scopesAt(doc, needle).includes("support.function.color.xui"), needle);
  }
  has(doc, 'light: )', "string");
  has(doc, 'dark: )', "string");
  has(doc, "/* ) */", "comment.block");
  has(doc, "theme(light:", "support.function.color.xui");
  closed(doc);
});

test("invalid style strings and nested expressions retain C# lexical protection", () => {
  const doc = tokenize(String.raw`component Editing {
  resources {
    Broken: theme(light: "}; // dark: resource(Fake)", dark: 0);
    Alias: resource(/* ) } */ "NotAName)");
    Valid: 1;
  }
  style Editing for Button {
    background: theme(light: Pick(")", new[] { 1, 2 })[0], dark: 1);
    foreground: resource(@"quoted "" } ); text");
    padding: (() => { return (1, 2, 3, 4); })();
    when hovered { background: 1; }
  }
  view { Button("Recovered"); }
}`);
  for (const needle of ["}; // dark:", "NotAName)", 'quoted "" } ); text'])
    has(doc, needle, "string");
  has(doc, "/* ) } */", "comment.block");
  has(doc, "Pick(", "entity.name.function");
  has(doc, "Valid:", "entity.name.constant.resource.xui");
  has(doc, "when hovered", "keyword.control.when.xui");
  has(doc, 'Button("Recovered"', "support.class.node.xui");
  closed(doc);
});

test("missing style values and terminators recover at the next property or closing block", () => {
  const doc = tokenize(`component Incomplete {
  resources {
    Missing:
    Recovered: 1;
    Unclosed: theme(light: 0, dark: 1;
    Last: 2
  }
  style Editing for Button {
    background:
    foreground: 1;
    when hov { borderBrush: 2 }
    when /* incomplete state */ { padding: 1; }
    when hovered { cornerRadius: }
    when pressed { background: resource(Missing; }
    padding: 3
  }
  view { Button("Recovered", style: Editing); }
}
component After { view { Text("After"); } }`);
  has(doc, "Recovered:", "entity.name.constant.resource.xui");
  has(doc, "Last:", "entity.name.constant.resource.xui");
  has(doc, "foreground:", "support.type.property-name.xui");
  assert.ok(!scopesAt(doc, "hov {").includes("constant.language.style-state.xui"));
  has(doc, "cornerRadius:", "support.type.property-name.xui");
  has(doc, "padding: 3", "support.type.property-name.xui");
  has(doc, 'Button("Recovered"', "support.class.node.xui");
  has(doc, "After {", "entity.name.type.component.xui");
  closed(doc);
});

test("unfinished style documents keep stable incremental stacks without declaring unsupported tokens", () => {
  for (const tail of ["style ", "style Draft for ", "style Draft for Button basedOn ",
    "style Draft for Button { when ", "style Draft for Button { when hovered { background: theme(light:",
    "resources { Color: resource(/*", 'resources { Color: theme(light: "']) {
    const doc = tokenize(`component Draft {\n${tail}`);
    assert.ok(doc.stack.depth > 1);
    assert.deepEqual(doc.tokens, tokenize(`component Draft {\r\n${tail}`).tokens);
  }
  const doc = tokenize(`component Unsupported {
    style Unknown for NotAControl {
      unknownProperty: 1;
      when unknownState { background: 2; }
    }
    view { Button("Known"); }
  }`);
  assert.ok(!scopesAt(doc, "NotAControl").includes("support.class.node.xui"));
  assert.ok(!scopesAt(doc, "unknownProperty").includes("support.type.property-name.xui"));
  assert.ok(!scopesAt(doc, "unknownState").includes("constant.language.style-state.xui"));
  has(doc, 'Button("Known"', "support.class.node.xui");
  closed(doc);
});

test("constructor parameters and native composition nodes have XUI scopes", () => {
  const doc = tokenize(`component Composition {
    param string Title;
    param Xui.Element Body;
    view { Grid(Title, ref: Layout, columns: [new(Xui.TrackSizing.Star, 1)]) {
      NavigationView("Navigation", headerVisible: false);
      DataGrid("Details", columns: []);
      ItemsView("Files");
      SplitView("Panes") {
        ScrollView("Content") { Content(Body); }
        Popup("Details") { Text("Body"); }
      }
    } }
  }`);
  has(doc, "param", "keyword.declaration.param.xui");
  has(doc, "Title;", "variable.other.readonly.param.xui");
  has(doc, "Body;", "variable.other.readonly.param.xui");
  for (const node of ["Grid(", "NavigationView(", "DataGrid(", "ItemsView(", "SplitView(", "ScrollView(", "Content(", "Popup("])
    has(doc, node, "support.class.node.xui");
  has(doc, "ref:", "variable.parameter.named.xui");
  has(doc, "columns:", "variable.parameter.named.xui");
  closed(doc);
});

test("canonical component scopes keywords, names, state, arguments, and C# members", () => {
  const doc = tokenize(counter);
  has(doc, "namespace", "keyword.other.namespace.xui");
  has(doc, "Demo", "entity.name.namespace.xui");
  has(doc, "component", "keyword.declaration.component.xui");
  has(doc, "Counter", "entity.name.type.component.xui");
  has(doc, "state", "keyword.declaration.state.xui");
  has(doc, "int", "keyword.type.int.cs");
  has(doc, "Count = 0", "variable.other.readwrite.state.xui");
  has(doc, "0;", "constant.numeric");
  has(doc, "view", "keyword.other.view.xui");
  has(doc, "VStack", "support.class.node.xui");
  has(doc, "Text(", "support.class.node.xui");
  has(doc, "Button(", "support.class.node.xui");
  has(doc, "spacing:", "variable.parameter.named.xui");
  has(doc, "spacing:", "punctuation.separator.key-value.xui", "spacing".length);
  has(doc, "click:", "variable.parameter.named.xui");
  has(doc, "code", "keyword.other.code.xui");
  has(doc, "csharp", "storage.type.language.xui");
  has(doc, "void", "keyword.type.void.cs");
  has(doc, "void", "meta.embedded.block.csharp");
  has(doc, "Increment()", "entity.name.function");
  has(doc, "Count++", "variable.other");
  closed(doc);
});

test("nested C# blocks and multiline strings/comments do not close the embedding", () => {
  const doc = tokenize(nested);
  has(doc, "Demo.Nested", "entity.name.namespace.xui", 5);
  for (const node of ["VStack", "HStack", 'Text($"Count:', "Button(", "Toggle(", "TextInput("]) {
    has(doc, node, "support.class.node.xui");
  }
  has(doc, "if (Count", "keyword.control");
  has(doc, "} // not a comment", "string");
  has(doc, 'second ""quoted"" } line', "string");
  has(doc, '// } } Text("not XUI")', "comment.line", 10);
  has(doc, "} view {", "comment.block");
  has(doc, "Count += 2", "meta.embedded.block.csharp");
  has(doc, 'Text("Recovered"', "support.class.node.xui");
  outsideCsharp(doc, 'Text("Recovered"');
  has(doc, "Second {", "entity.name.type.component.xui");
  outsideCsharp(doc, "Second {");
  closed(doc);
});

test("interpolation recognizes C# identifiers, calls, nested strings, and escaped braces", () => {
  const doc = tokenize(String.raw`component Strings {
  state string Caption = $"{Math.Max(1, 2)} {{literal}}";
  view {
    Text($"Nested {string.Join("}", new[] { "a", "b" })}", id: "label");
    Text($@"Verbatim {Caption} {{literal}} ""quote""");
    Text(@$"Reversed {Caption} ""quote""");
    Text("escaped \" } /*");
    Text(@"} // ""quote""");
  }
}`);
  has(doc, "Math.Max", "meta.embedded.expression.csharp");
  has(doc, "Max(1", "entity.name.function");
  has(doc, "{{literal}}", "string");
  has(doc, "Join", "entity.name.function");
  has(doc, "Caption} {{literal}}", "variable.other");
  has(doc, 'Caption} ""quote""', "variable.other");
  has(doc, 'escaped \\" } /*', "string");
  has(doc, "id:", "variable.parameter.named.xui");
  closed(doc);
});

test("state initializers preserve nested lambda statements, collections, and call parentheses", () => {
  const doc = tokenize(String.raw`component Expressions {
  state int Value = Run(() => { var data = new[] { 1, 2 }; return data[0]; });
  state string Label = "semi; brace}";
  view {
    Text(Format((Value + 1), new[] { ")", ";" }[0]), id: "expr");
    Button("Go", click: Handle);
  }
}`);
  has(doc, "data[0]", "meta.embedded.expression.csharp");
  has(doc, "Label =", "variable.other.readwrite.state.xui");
  has(doc, "semi; brace}", "string");
  has(doc, "Format", "entity.name.function");
  has(doc, "id:", "variable.parameter.named.xui");
  has(doc, "Handle", "variable.other");
  outsideCsharp(doc, "Button");
  closed(doc);
});

test("code raw strings and characters protect braces without asserting compiler support", () => {
  const doc = tokenize(String.raw`component Raw {
  code csharp {
    void Run() {
      char close = '}';
      char quote = '\'';
      var raw = """
        } // /* text
        """;
      var interpolated = $$"""
        {literal} {{1 + 2}} }
        """;
      var after = 7;
    }
  }
  view { Text("after raw"); }
}`);
  has(doc, "'}'", "string");
  has(doc, "} // /* text", "string");
  has(doc, "{literal}", "string");
  has(doc, "1 + 2", "constant.numeric");
  has(doc, "after = 7", "meta.embedded.block.csharp");
  outsideCsharp(doc, 'Text("after raw")');
  closed(doc);
});

test("braces in XUI comments and comments between headers and bodies stay balanced", () => {
  const doc = tokenize(String.raw`// component Fake { }
namespace /* } */ Demo /* { */ . Nested;
component /* } */ Commented // }
/* } */
{
  state int Count /* } */ = 1 /* ; } */;
  view // }
  /* } */
  {
    // } Button(
    Text("real" /* ), } */, id: "safe");
  }
  code /* } */ csharp // }
  /* } */
  {
    void Run() { /* } */ }
  }
}
component After { view { Text("after"); } }`);
  has(doc, "Fake", "comment.line");
  has(doc, "Commented", "entity.name.type.component.xui");
  has(doc, "Count /*", "variable.other.readwrite.state.xui");
  has(doc, "id:", "variable.parameter.named.xui");
  has(doc, "Run()", "entity.name.function");
  has(doc, "After", "entity.name.type.component.xui");
  closed(doc);
});

test("namespace is optional and multiple components recover on the same line", () => {
  const doc = tokenize('component First { view { Text("1"); } } component Next { view { Text("2"); } }');
  has(doc, "First", "entity.name.type.component.xui");
  has(doc, "Next", "entity.name.type.component.xui");
  outsideCsharp(doc, "Next");
  closed(doc);
});

test("C# contexts do not apply XUI keywords or built-in node scopes", () => {
  const doc = tokenize(`component Names {
  state string ComponentName = "view Text namespace";
  code csharp {
    void Text() { var component = 1; var view = component; }
  }
  view { Text(ComponentName); TextInput(text: ComponentName); CustomNode(); }
}`);
  has(doc, "Text()", "entity.name.function");
  has(doc, "component = 1", "entity.name.variable.local.cs");
  for (const needle of ["Text()", "component = 1", "view = component"]) {
    assert.ok(!scopesAt(doc, needle).some((scope) => /^(keyword|support)\..*\.xui$/.test(scope)));
  }
  has(doc, "TextInput", "support.class.node.xui");
  has(doc, "CustomNode", "entity.name.tag.xui");
  closed(doc);
});

test("numbers, booleans, operators, member access and named method references use C# scopes", () => {
  const doc = tokenize(`component Values {
  state double Ratio = -1.5e+2;
  state int Mask = 0xFF;
  state int Bits = 0b1010;
  view {
    Text(Model.Caption.ToString(), enabled: true, id: "value");
    Button("go", click: Increment);
  }
}`);
  has(doc, "1.5e+2", "constant.numeric");
  has(doc, "0xFF", "constant.numeric");
  has(doc, "0b1010", "constant.numeric");
  has(doc, "Model.Caption", "variable.other");
  has(doc, "ToString", "entity.name.function");
  has(doc, "true", "constant.language");
  has(doc, "Increment", "variable.other");
  closed(doc);
});

test("LF and CRLF documents produce identical scopes", () => {
  assert.deepEqual(tokenize(nested.replace(/\r?\n/g, "\r\n")).tokens, tokenize(nested).tokens);
});

test("manifest embedded language mappings produce C# token metadata and return to XUI", () => {
  let stack = textmate.INITIAL;
  const lines = counter.split(/\r?\n/);
  const metadata = lines.map((line) => {
    const result = grammar.tokenizeLine2(line, stack);
    stack = result.ruleStack;
    return result.tokens;
  });
  function languageAt(needle, offset = 0) {
    const row = lines.findIndex((line) => line.includes(needle));
    assert.ok(row >= 0);
    const column = lines[row].indexOf(needle) + offset;
    const tokens = metadata[row];
    for (let index = 0; index < tokens.length; index += 2) {
      if (tokens[index] <= column && (index + 2 === tokens.length || tokens[index + 2] > column)) {
        return tokens[index + 1] & 0xFF;
      }
    }
    assert.fail(`No encoded token at ${needle}`);
  }
  assert.equal(languageAt("namespace"), 1);
  assert.equal(languageAt("VStack"), 1);
  assert.equal(languageAt('Text($"Count: {Count}"', 'Text($"Count: {'.length), 2);
  assert.equal(languageAt("Count = 0", "Count = ".length), 2);
  assert.equal(languageAt("void"), 2);
  assert.equal(languageAt("code csharp"), 1);
});

test("reserved keyword boundaries do not split longer names", () => {
  const doc = tokenize("component componentCounter { state int stateValue = 0; view { TextInput(text: stateValue); } }");
  has(doc, "componentCounter", "entity.name.type.component.xui");
  has(doc, "stateValue =", "variable.other.readwrite.state.xui");
  has(doc, "TextInput", "support.class.node.xui", 4);
  closed(doc);
});
