import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import { open } from "yauzl";

const manifest = JSON.parse(await readFile(new URL("../package.json", import.meta.url), "utf8"));
const path = new URL(`../dist/${manifest.name}-${manifest.version}.vsix`, import.meta.url);
const zip = await new Promise((resolve, reject) => {
  open(fileURLToPath(path), { lazyEntries: true }, (error, archive) => error ? reject(error) : resolve(archive));
});
const entries = [];
const contents = new Map();
await new Promise((resolve, reject) => {
  zip.on("error", reject);
  zip.on("end", resolve);
  zip.on("entry", (entry) => {
    entries.push(entry.fileName);
    zip.openReadStream(entry, (error, stream) => {
      if (error) return reject(error);
      const chunks = [];
      stream.on("error", reject);
      stream.on("data", (chunk) => chunks.push(chunk));
      stream.on("end", () => {
        contents.set(entry.fileName, Buffer.concat(chunks));
        zip.readEntry();
      });
    });
  });
  zip.readEntry();
});

const required = [
  "[Content_Types].xml",
  "extension.vsixmanifest",
  "extension/package.json",
  "extension/readme.md",
  "extension/LICENSE.txt",
  "extension/images/zoey.png",
  "extension/language-configuration.json",
  "extension/syntaxes/xui.tmLanguage.json",
  "extension/snippets/xui.json"
];
assert.deepEqual(entries.sort(), required.sort(), "VSIX must contain only the runtime assets and package metadata");
const packaged = JSON.parse(contents.get("extension/package.json"));
assert.equal(packaged.publisher, "zadjii-msft");
assert.equal(packaged.name, "xui");
assert.equal(packaged.version, manifest.version);
assert.equal(packaged.main, undefined);
assert.equal(packaged.icon, "images/zoey.png");
assert.ok(contents.get("extension/images/zoey.png").equals(
  await readFile(new URL("../images/zoey.png", import.meta.url))
), "The VSIX must include the generated Zoey icon unchanged");
for (const [name, packagedName] of [
  ["language-configuration.json", "language-configuration.json"],
  ["syntaxes/xui.tmLanguage.json", "syntaxes/xui.tmLanguage.json"],
  ["snippets/xui.json", "snippets/xui.json"],
  ["README.md", "readme.md"],
  ["LICENSE", "LICENSE.txt"]
]) {
  assert.equal(
    contents.get(`extension/${packagedName}`).toString("utf8"),
    await readFile(new URL(`../${name}`, import.meta.url), "utf8"),
    `Packaged ${name} must match the source`
  );
}
console.log(`Checked ${fileURLToPath(path)}: ${entries.length} expected files; no tests, cache, dependencies, or scripts.`);
