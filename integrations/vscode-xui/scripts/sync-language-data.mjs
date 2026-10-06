import assert from "node:assert/strict";
import { readFile, readdir, writeFile } from "node:fs/promises";

const root = new URL("../../../", import.meta.url);
const read = (path) => readFile(new URL(path, root), "utf8");
const parser = await read("bindings/dotnet/Xui.Generator/Parser.cs");
const styling = await read("bindings/dotnet/Xui.Generator/Styling.cs");
const catalog = await read("bindings/dotnet/Xui.Generator/StyleCatalog.g.cs");
const strings = (text) => [...text.matchAll(/"(\w+)"/g)].map((match) => match[1]);
const controls = {};
const allowed = parser.match(/string\[\] allowed = kind switch\s*\{([\s\S]*?)\n        \};/);
assert.ok(allowed, "Cannot find the compiler's node arguments");
for (const [, names, values] of allowed[1].matchAll(/((?:"\w+"(?: or )?)+) => \[([^\]]*)\]/g))
  for (const name of strings(names)) controls[name] = strings(values);
assert.ok(Object.keys(controls).length > 0);

const aliases = {};
const targets = styling.match(/TargetName\(string target\) => target switch \{([\s\S]*?)\n    \};/);
assert.ok(targets, "Cannot find style target aliases");
for (const [, names, target] of targets[1].matchAll(/((?:"\w+"(?: or )?)+) => "(\w+)"/g))
  for (const name of strings(names)) aliases[name] = target;

const schemas = new Map();
const allParts = new Set();
for (const [section, key] of [["Properties", "properties"], ["States", "states"], ["StateProperties", "stateProperties"]]) {
  const match = catalog.match(new RegExp(`string\\[\\] ${section}\\([^\\n]+\\{([\\s\\S]*?)\\n    \\};`));
  assert.ok(match, `Cannot find ${section}`);
  for (const [, target, part, values] of match[1].matchAll(/\("(\w+)", "(\w+)"\) => \[([^\]]*)\]/g)) {
    allParts.add(`${target}.${part}`);
    if (target === "Tooltip") continue;
    const id = `${target}.${part}`;
    if (!schemas.has(id)) schemas.set(id, { target, part });
    schemas.get(id)[key] = strings(values);
  }
}
for (const [, target, part, fontSize, familyLength, fontStyles, horizontal, vertical] of catalog.matchAll(
  /\("(\w+)", "(\w+)"\) => \(([\d.]+)f, (\d+)u, (\d+)u, (\d+)u, (\d+)u\)/g
)) {
  const schema = schemas.get(`${target}.${part}`);
  if (schema) schema.limits = [fontSize, familyLength, fontStyles, horizontal, vertical].map(Number);
}
assert.ok([...schemas.values()].every((s) => s.properties && s.states && s.stateProperties && s.limits));
const native = JSON.parse(await read("bindings/control_style_catalog.json"));
assert.equal(allParts.size, native.schemas.length,
  "Update the exporter if the native catalog layout changes");

const enumNames = ["Axis", "ButtonIcon", "CheckState", "PopupPlacement", "ProgressState", "RevealLayout", "RevealDirection"];
const enums = {};
for (const file of await readdir(new URL("bindings/dotnet/Xui/", root))) {
  if (!file.endsWith(".cs")) continue;
  const source = await read(`bindings/dotnet/Xui/${file}`);
  for (const name of enumNames) {
    const match = source.match(new RegExp(`public enum ${name}\\s*(?::\\s*\\w+)?\\s*\\{([^}]+)\\}`));
    if (match) enums[name] = match[1].replace(/\/\/[^\r\n]*/g, "").split(",")
      .map((item) => item.trim().match(/^(\w+)/)?.[1]).filter(Boolean);
  }
}
assert.equal(Object.keys(enums).length, enumNames.length);
const data = JSON.stringify({ controls, aliases, enums }, null, 2).slice(0, -2) +
  ',\n  "schemas": [\n' + [...schemas.values()].map((schema) => `    ${JSON.stringify(schema)}`).join(",\n") + "\n  ]\n}\n";
const destination = new URL("../data/language.json", import.meta.url);
if (process.argv.includes("--check")) {
  assert.equal((await readFile(destination, "utf8")).replaceAll("\r\n", "\n"), data,
    "Language metadata is stale. Run npm run sync:language.");
  console.log("Language metadata matches the compiler and managed enums.");
} else {
  const { mkdir } = await import("node:fs/promises");
  await mkdir(new URL("../data/", import.meta.url), { recursive: true });
  await writeFile(destination, data);
  console.log(`Updated ${Object.keys(controls).length} controls and ${schemas.size} style parts.`);
}
