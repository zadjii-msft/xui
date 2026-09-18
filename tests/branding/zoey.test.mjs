import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { mkdtemp, readFile, rm, stat, writeFile } from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import test from "node:test";
import { Resvg } from "@resvg/resvg-js";
import {
  ROOT, SOURCE, OUTPUT, PNG_SIZES, ICO_SIZES, EXTENSION_ICON,
  parseSource, validatePalettes, makeVariant, renderPng, encodeIco, generateAssets, synchronize, darker,
} from "../../tools/branding/generate_zoey.mjs";

const source = await readFile(path.join(ROOT, SOURCE), "utf8");
const config = JSON.parse(await readFile(path.join(ROOT, "assets/branding/palettes.json"), "utf8"));
const generated = generateAssets(source, config);
const hash = value => createHash("sha256").update(value).digest("hex");
const stem = variant => variant.id === "rainbow" ? "zoey" : `zoey-${variant.id}`;
const elements = doc => Array.from(doc.getElementsByTagName("*"));

function geometry(svg) {
  return elements(parseSource(svg)).filter(node => !["title", "desc"].includes(node.localName)).map(node => [
    node.nodeName,
    Array.from(node.attributes).filter(a => !["fill", "stroke"].includes(a.name)).map(a => [a.name, a.value]),
  ]);
}

test("seven named palettes preserve canonical geometry, happy expression, and continuous border mask", () => {
  assert.deepEqual(config.variants.map(v => v.id), ["rainbow", "idle", "active", "success", "warning", "error", "paused"]);
  const original = parseSource(source);
  for (const variant of config.variants) {
    const svg = generated.get(`${OUTPUT}/${stem(variant)}.svg`).toString();
    assert.deepEqual(geometry(svg), geometry(source), variant.id);
    const doc = parseSource(svg);
    assert.equal(doc.getElementById("outer-mane-border").getAttribute("mask"), original.getElementById("outer-mane-border").getAttribute("mask"));
    assert.equal(doc.getElementById("title").textContent.startsWith("Zoey the XUI Lion"), true);
    for (let i = 1; i <= 6; i++) {
      const fill = doc.getElementById(`segment-${i}`).getAttribute("fill");
      assert.equal(fill, variant.id === "rainbow" ? original.getElementById(`segment-${i}`).getAttribute("fill") : variant.mane);
      assert.equal(doc.getElementById(`border-segment-${i}`).getAttribute("fill"), darker(fill));
    }
    const face = doc.getElementById("face");
    const allowed = new Set(Object.values(variant.id === "rainbow" ? config.sourceFace : variant.face));
    for (const node of [face, ...elements(face)]) for (const attr of ["fill", "stroke"]) {
      const value = node.getAttribute(attr);
      if (value && value !== "none") assert.ok(allowed.has(value), `${variant.id}: unexpected face paint ${value}`);
    }
  }
});

test("every palette has correctly sized PNGs, transparent corners, and visible artwork", () => {
  for (const variant of config.variants) {
    const svg = generated.get(`${OUTPUT}/${stem(variant)}.svg`);
    for (const size of PNG_SIZES) {
      const png = generated.get(`${OUTPUT}/${stem(variant)}-${size}.png`);
      assert.equal(png.subarray(0, 8).toString("hex"), "89504e470d0a1a0a");
      assert.equal(png.readUInt32BE(16), size);
      assert.equal(png.readUInt32BE(20), size);
      assert.equal(png[24], 8, "8-bit channels");
      assert.equal(png[25], 6, "RGBA PNG");
    }
    for (const size of [16, 32, 256]) {
      const image = new Resvg(svg, { fitTo: { mode: "width", value: size }, font: { loadSystemFonts: false } }).render();
      const pixels = image.pixels;
      for (const index of [0, size - 1, size * (size - 1), size * size - 1]) assert.equal(pixels[index * 4 + 3], 0);
      let visible = 0;
      for (let index = 3; index < pixels.length; index += 4) if (pixels[index] > 128) visible++;
      assert.ok(visible > size * size / 4, `${variant.id}: recognizable artwork at ${size}px`);
      assert.ok(image.asPng().equals(generated.get(`${OUTPUT}/${stem(variant)}-${size}.png`)));
    }
  }
});

test("each ICO contains all ten complete PNG frames with correct directory entries", () => {
  for (const variant of config.variants) {
    const ico = generated.get(`${OUTPUT}/${stem(variant)}.ico`);
    assert.equal(ico.readUInt16LE(0), 0);
    assert.equal(ico.readUInt16LE(2), 1);
    assert.equal(ico.readUInt16LE(4), ICO_SIZES.length);
    let offset = 6 + ICO_SIZES.length * 16;
    for (const [index, size] of ICO_SIZES.entries()) {
      const entry = 6 + index * 16;
      assert.equal(ico[entry] || 256, size);
      assert.equal(ico[entry + 1] || 256, size);
      assert.equal(ico.readUInt16LE(entry + 4), 1);
      assert.equal(ico.readUInt16LE(entry + 6), 32);
      const length = ico.readUInt32LE(entry + 8);
      assert.equal(ico.readUInt32LE(entry + 12), offset);
      assert.ok(ico.subarray(offset, offset + length).equals(generated.get(`${OUTPUT}/${stem(variant)}-${size}.png`)));
      offset += length;
    }
    assert.equal(offset, ico.length);
  }
  assert.throws(() => encodeIco([]), /one or more/);
  assert.throws(() => encodeIco([{ size: 512, png: Buffer.alloc(25) }]), /Invalid/);
  const png = renderPng(source, 16);
  assert.throws(() => encodeIco([{ size: 16, png }, { size: 16, png }]), /duplicate/);
});

test("manifest hashes, local web icons, and the packaged extension image match", () => {
  const manifest = JSON.parse(generated.get(`${OUTPUT}/manifest.json`));
  assert.equal(manifest.source, SOURCE);
  assert.equal(manifest.sourceSha256, hash(generated.get(SOURCE)));
  for (const [filename, expected] of Object.entries(manifest.files)) assert.equal(hash(generated.get(filename)), expected, filename);
  const web = JSON.parse(generated.get(`${OUTPUT}/site.webmanifest`));
  for (const icon of web.icons) assert.ok(generated.has(`${OUTPUT}/${icon.src}`));
  assert.ok(generated.get(EXTENSION_ICON).equals(generated.get(`${OUTPUT}/zoey-256.png`)));
});

test("checked-in outputs are current and generation is byte-stable", async () => {
  assert.equal(await synchronize(ROOT, generated, true), 0);
  const second = generateAssets(source, config);
  assert.deepEqual([...second.keys()], [...generated.keys()]);
  for (const [name, data] of generated) assert.ok(data.equals(second.get(name)), name);
});

test("updates preserve unchanged timestamps and check mode never repairs stale files", async () => {
  const directory = await mkdtemp(path.join(os.tmpdir(), "xui-zoey-test-"));
  try {
    assert.equal(await synchronize(directory, generated, false), generated.size);
    const icon = path.join(directory, OUTPUT, "zoey.ico");
    const timestamp = (await stat(icon)).mtimeMs;
    assert.equal(await synchronize(directory, generated, false), 0);
    assert.equal((await stat(icon)).mtimeMs, timestamp);
    await writeFile(icon, "stale");
    await assert.rejects(synchronize(directory, generated, true), /Missing or stale/);
    assert.equal(await readFile(icon, "utf8"), "stale");
    assert.equal(await synchronize(directory, generated, false), 1);
    await writeFile(path.join(directory, OUTPUT, "do-not-delete.txt"), "unrelated");
    await assert.rejects(synchronize(directory, generated, false), /Unexpected generated/);
    assert.equal(await readFile(path.join(directory, OUTPUT, "do-not-delete.txt"), "utf8"), "unrelated");
  } finally {
    await rm(directory, { recursive: true });
  }
});

test("bad sources and incomplete palettes fail explicitly before rendering", () => {
  assert.throws(() => parseSource("<svg>"), /./);
  assert.throws(() => parseSource(source.replace('viewBox="0 0 256 256"', 'viewBox="0 0 256 128"')), /square/);
  assert.throws(() => parseSource(source.replace("</svg>", '<script>alert(1)</script></svg>')), /Unsupported Zoey element/);
  assert.throws(() => parseSource(source.replace("</svg>", '<image href="https://example.com/image.png"/></svg>')), /Unsupported Zoey element/);
  assert.throws(() => parseSource(source.replace('fill="#ED6B79"', 'fill="url(https://example.com/paint)"')), /Invalid SVG reference/);
  assert.throws(() => parseSource(source.replace('id="face"', 'id="mane"')), /Duplicate SVG id/);
  assert.throws(() => parseSource("<!DOCTYPE svg>" + source), /DTD/);
  const duplicate = structuredClone(config);
  duplicate.variants[1].id = "rainbow";
  assert.throws(() => validatePalettes(duplicate), /unique/);
  const missing = structuredClone(config);
  delete missing.variants[1].face.ink;
  assert.throws(() => validatePalettes(missing), /Invalid idle face color/);
  assert.throws(() => makeVariant(source.replaceAll("#73503C", "#000001"), config, config.variants[0]), /Unknown face paint/);
});
