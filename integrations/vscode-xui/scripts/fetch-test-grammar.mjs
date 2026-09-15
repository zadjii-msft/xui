import { createHash } from "node:crypto";
import { mkdir, readFile, writeFile } from "node:fs/promises";

const revision = "6317c3c9ba89d02b08fc69b52c345782d6a42ade";
const baseUrl = `https://raw.githubusercontent.com/dotnet/csharp-tmLanguage/${revision}`;
const cache = new URL("../test/cache/", import.meta.url);
const files = [
  ["grammars/csharp.tmLanguage", "csharp.tmLanguage", "bdfc82323c3d081c5282f6db1bff23386083877462d5456737d987350d013ed8"],
  ["LICENSE", "CSharp-LICENSE", "22844d95411661d8827cdbb724b55c40e779327be9892beb1c684fd393cd290e"]
];

await mkdir(cache, { recursive: true });
for (const [source, name, expected] of files) {
  const path = new URL(name, cache);
  let bytes;
  let downloaded = false;
  try {
    bytes = await readFile(path);
  } catch (error) {
    if (error.code !== "ENOENT") throw error;
    console.log(`Downloading test fixture ${source} at ${revision}`);
    const response = await fetch(`${baseUrl}/${source}`, { signal: AbortSignal.timeout(30_000) });
    if (!response.ok) throw new Error(`Cannot download ${source}: HTTP ${response.status}`);
    bytes = Buffer.from(await response.arrayBuffer());
    downloaded = true;
  }
  const actual = createHash("sha256").update(bytes).digest("hex");
  if (actual !== expected) {
    throw new Error(`SHA-256 mismatch for ${name}: expected ${expected}, got ${actual}. Delete the test/cache file and retry.`);
  }
  if (downloaded) await writeFile(path, bytes);
}
