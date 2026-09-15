import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";

const json = async (path) => JSON.parse(await readFile(new URL(path, import.meta.url), "utf8"));
const manifest = await json("../package.json");
const config = await json("../language-configuration.json");
const snippets = await json("../snippets/xui.json");

function expand(body) {
  const defaults = new Map();
  return (Array.isArray(body) ? body.join("\n") : body).replace(
    /\\([$}\\])|\$\{(\d+)(?::([^}]*))?\}|\$(\d+)/g,
    (_, escaped, index, value, simpleIndex) => {
      if (escaped) return escaped;
      const key = index ?? simpleIndex;
      if (value !== undefined) defaults.set(key, value);
      return defaults.get(key) ?? "";
    }
  );
}

test("manifest associates .xui files without activation code", () => {
  assert.equal(`${manifest.publisher}.${manifest.name}`, "zadjii-msft.xui");
  assert.equal(manifest.main, undefined);
  assert.equal(manifest.browser, undefined);
  assert.equal(manifest.activationEvents, undefined);
  assert.equal(manifest.contributes.languages[0].id, "xui");
  assert.deepEqual(manifest.contributes.languages[0].extensions, [".xui"]);
  assert.equal(manifest.contributes.grammars[0].scopeName, "source.xui");
});

test("language configuration supplies brackets, comments, closing pairs, indentation and folding", () => {
  assert.deepEqual(config.comments, { lineComment: "//", blockComment: ["/*", "*/"] });
  assert.deepEqual(config.brackets, [["{", "}"], ["[", "]"], ["(", ")"]]);
  for (const pair of config.autoClosingPairs) assert.deepEqual(pair.notIn, ["string", "comment"]);
  const increase = new RegExp(config.indentationRules.increaseIndentPattern);
  const decrease = new RegExp(config.indentationRules.decreaseIndentPattern);
  for (const line of ["component Counter {", "  VStack() { // children", "Text(", "  var values = ["]) {
    assert.ok(increase.test(line), line);
  }
  for (const line of ["// {", "  // (", 'Text("{");', 'state string Brace = "{";']) {
    assert.ok(!increase.test(line), line);
  }
  for (const line of ["  }", "  );", "  ];"]) assert.ok(decrease.test(line), line);
  for (const prefix of ["// ", "#"]) {
    assert.ok(new RegExp(config.folding.markers.start).test(`${prefix}region Details`));
    assert.ok(new RegExp(config.folding.markers.end).test(`${prefix}endregion`));
  }
});

test("component snippet expands to the canonical component with literal C# interpolation", async () => {
  const canonical = await readFile(new URL("./fixtures/counter.xui", import.meta.url), "utf8");
  const component = canonical.slice(canonical.indexOf("component")).replaceAll("\r\n", "\n").trimEnd();
  assert.equal(expand(snippets.Component.body).replaceAll("\t", "  ").trimEnd(), component);
  assert.ok(expand(snippets.Component.body).includes('$"Count: {Count}"'));
  assert.equal(expand(snippets.Namespace.body).trim(), "namespace Demo;");
});

test("control snippets use canonical property and handler names", () => {
  assert.equal(expand(snippets.Button.body), 'Button("Increment", click: Increment, id: "increment");');
  assert.equal(expand(snippets.Toggle.body), 'Toggle("Enabled", checked: IsEnabled, change: OnChanged, id: "enabled");');
  assert.equal(expand(snippets["Text input"].body), 'TextInput(text: Input, change: OnChanged, submit: OnSubmit, id: "input");');
  assert.deepEqual(Object.values(snippets).map((snippet) => snippet.prefix).sort(),
    ["component", "namespace", "state", "view", "code", "vstack", "hstack", "text", "button", "toggle", "textinput"].sort());
});
