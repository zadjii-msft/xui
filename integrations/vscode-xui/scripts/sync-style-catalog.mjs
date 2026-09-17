import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { readFile, writeFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";

const ref = process.argv[2];
assert.ok(ref && !ref.startsWith("-"), "Usage: node scripts/sync-style-catalog.mjs <validated-git-ref>");
const cwd = fileURLToPath(new URL("../../../", import.meta.url));
const git = (...args) => execFileSync("git", args, { cwd, encoding: "utf8", maxBuffer: 4 * 1024 * 1024 });
const commit = git("rev-parse", "--verify", `${ref}^{commit}`).trim();
const source = git("show", `${commit}:bindings/dotnet/Xui.Generator/StyleCatalog.g.cs`);
const section = (name) => {
  const match = source.match(new RegExp(`internal static string\\[\\] ${name}\\([^\\n]+\\{([\\s\\S]*?)\\n    \\};`));
  assert.ok(match, `Missing generated catalog section: ${name}`);
  return [...match[1].matchAll(/\("(\w+)", "(\w+)"\) => \[([^\]]*)\]/g)]
    .map(([, target, part, values]) => ({ target, part, values: [...values.matchAll(/"(\w+)"/g)].map((m) => m[1]) }));
};
const properties = section("Properties");
const states = section("States");
const stateProperties = section("StateProperties");
const native = JSON.parse(git("show", `${commit}:bindings/control_style_catalog.json`));
assert.ok(properties.length > 0, "The generated catalog must not be empty");
assert.equal(properties.length, native.schemas.length, "Generated and native catalog sizes must agree");
assert.equal(properties.length, states.length);
assert.equal(properties.length, stateProperties.length);
const schemas = properties.filter(({ target }) => target !== "Tooltip").map(({ target, part, values }) => {
  const lookup = (rows) => {
    const row = rows.find((entry) => entry.target === target && entry.part === part);
    assert.ok(row, `Missing ${target}.${part} schema`);
    return row.values;
  };
  return { target, part, properties: values, states: lookup(states), stateProperties: lookup(stateProperties) };
});
const aliases = { Text: "Label", VStack: "Stack", HStack: "Stack",
  ToggleSwitch: "Toggle", ToggleButton: "Button", ProgressRing: "Progress",
  CheckBox: "Toggle", HyperlinkButton: "Button", SelectorBar: "ChoiceList", InfoBadge: "InlineStatus", MenuBar: "CommandBar" };
const catalog = { sourceCommit: commit, aliases, schemas };
const words = (values) => [...new Set(values)].sort();
const vocabulary = {
  "style-targets": words([...schemas.map((s) => s.target), ...Object.keys(aliases)]),
  "style-parts": words(schemas.map((s) => s.part).filter((part) => part !== "root")),
  "style-property-names": words(schemas.flatMap((s) => s.properties)),
  "style-states": words(schemas.flatMap((s) => s.states))
};
const grammarPath = new URL("../syntaxes/xui.tmLanguage.json", import.meta.url);
let text = await readFile(grammarPath, "utf8");
const grammar = JSON.parse(text);
for (const [name, values] of Object.entries(vocabulary)) {
  assert.ok(values.every((word) => /^[A-Za-z][A-Za-z0-9]*$/.test(word)));
  const previous = grammar.repository[name]?.match;
  assert.ok(previous, `Missing vocabulary rule: ${name}`);
  const replacement = `\\b(?:${values.join("|")})\\b`;
  const ruleStart = text.indexOf(`"${name}": {`);
  const matchStart = text.indexOf(JSON.stringify(previous), ruleStart);
  assert.ok(ruleStart >= 0 && matchStart > ruleStart, `Cannot update rule: ${name}`);
  text = text.slice(0, matchStart) + JSON.stringify(replacement) + text.slice(matchStart + JSON.stringify(previous).length);
}
await writeFile(grammarPath, text);
await writeFile(new URL("../test/fixtures/style-catalog.json", import.meta.url),
  `{\n  "sourceCommit": ${JSON.stringify(catalog.sourceCommit)},\n  "aliases": ${JSON.stringify(aliases)},\n  "schemas": [\n` +
  schemas.map((schema) => `    ${JSON.stringify(schema)}`).join(",\n") + "\n  ]\n}\n");
console.log(`Updated ${schemas.length} part schemas for ${new Set(schemas.map((s) => s.target)).size} .xui targets from ${commit}.`);
