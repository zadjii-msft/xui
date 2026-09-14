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
