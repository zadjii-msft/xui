import { createHash, randomUUID } from "node:crypto";
import { readFile, readdir, mkdir, rename, unlink, writeFile } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { Resvg } from "@resvg/resvg-js";
import { DOMParser, XMLSerializer, onWarningStopParsing } from "@xmldom/xmldom";

export const ROOT = fileURLToPath(new URL("../../", import.meta.url));
export const SOURCE = "assets/branding/zoey.svg";
export const OUTPUT = "assets/branding/generated";
export const PNG_SIZES = [16, 20, 24, 32, 40, 48, 64, 96, 128, 180, 192, 256, 512, 1024];
export const ICO_SIZES = [16, 20, 24, 32, 40, 48, 64, 96, 128, 256];
export const EXTENSION_ICON = "integrations/vscode-xui/images/zoey.png";
const NS = "http://www.w3.org/2000/svg";
const HEX = /^#[0-9a-f]{6}$/i;
const ALLOWED = new Set(["svg", "title", "desc", "defs", "g", "path", "rect", "circle", "ellipse",
  "line", "polyline", "polygon", "clipPath", "mask", "linearGradient", "radialGradient", "stop", "use"]);
const FACE_ROLES = ["ear", "innerEar", "face", "ink", "muzzle", "blush"];
const sha256 = data => createHash("sha256").update(data).digest("hex");
const local = (root, relative) => path.join(root, ...relative.split("/"));
const elements = node => Array.from(node.getElementsByTagName("*"));
const children = node => Array.from(node.childNodes).filter(child => child.nodeType === 1);
const serialize = doc => new XMLSerializer().serializeToString(doc.documentElement).replaceAll("\r\n", "\n") + "\n";

function required(doc, id) {
  const result = doc.getElementById(id);
  if (!result) throw new Error(`Zoey SVG is missing #${id}. Keep the named layers from the canonical SVG.`);
  return result;
}

export function parseSource(text) {
  if (/<!DOCTYPE|<!ENTITY/i.test(text)) throw new Error("Zoey SVG must not contain a DTD or entity declarations.");
  const doc = new DOMParser({ onError: onWarningStopParsing }).parseFromString(text, "image/svg+xml");
  const svg = doc.documentElement;
  if (svg.localName !== "svg" || svg.namespaceURI !== NS) throw new Error("The source must be an SVG document.");
  const box = (svg.getAttribute("viewBox") || "").trim().split(/[\s,]+/).map(Number);
  if (box.length !== 4 || !box.every(Number.isFinite) || box[2] <= 0 || box[2] !== box[3]) {
    throw new Error("Zoey needs a finite, square viewBox with positive dimensions.");
  }
  const ids = new Set();
  for (const node of elements(doc)) {
    if (node.namespaceURI !== NS || !ALLOWED.has(node.localName)) {
      throw new Error(`Unsupported Zoey element: ${node.nodeName}. Use self-contained vector shapes, not fonts or images.`);
    }
    const id = node.getAttribute("id");
    if (id && ids.has(id)) throw new Error(`Duplicate SVG id: ${id}`);
    if (id) ids.add(id);
    for (const attribute of Array.from(node.attributes)) {
      if (/^on/i.test(attribute.name) || attribute.name === "style" || attribute.name === "xml:base") {
        throw new Error(`Unsupported SVG attribute: ${attribute.name}. Use SVG presentation attributes.`);
      }
      if (attribute.localName === "href" && !attribute.value.startsWith("#")) {
        throw new Error("SVG references must be local fragments, not external files or URLs.");
      }
    }
  }
  for (const node of elements(doc)) {
    for (const attribute of Array.from(node.attributes)) {
      const references = [...attribute.value.matchAll(/url\(\s*['"]?([^)'"\s]+)['"]?\s*\)/g)].map(match => match[1]);
      if (attribute.localName === "href") references.push(attribute.value);
      for (const reference of references) {
        if (!reference.startsWith("#") || !ids.has(reference.slice(1))) throw new Error(`Invalid SVG reference: ${reference}`);
      }
    }
  }
  for (const id of ["mane", "mane-backing", "outer-mane-border", "border-backing", "face", "face-border", "title", "desc"]) required(doc, id);
  for (const [group, prefix] of [["segments", "segment"], ["border-colors", "border-segment"]]) {
    const parts = children(required(doc, group));
    if (parts.length !== 6 || parts.some((node, index) => node.localName !== "path" || node.getAttribute("id") !== `${prefix}-${index + 1}` || !HEX.test(node.getAttribute("fill")))) {
      throw new Error(`#${group} must contain six named, solid-color paths.`);
    }
  }
  svg.removeAttribute("width");
  svg.removeAttribute("height");
  svg.setAttribute("role", "img");
  svg.setAttribute("aria-labelledby", "title desc");
  return doc;
}

export function validatePalettes(config) {
  if (!config || !config.sourceFace || !Array.isArray(config.variants) || !config.variants.length) {
    throw new Error("palettes.json must define sourceFace and a nonempty variants list.");
  }
  for (const role of FACE_ROLES) if (!HEX.test(config.sourceFace[role])) throw new Error(`Invalid sourceFace color: ${role}`);
  if (new Set(Object.values(config.sourceFace).map(color => color.toUpperCase())).size !== FACE_ROLES.length) {
    throw new Error("The source face roles must use distinct colors.");
  }
  const ids = new Set();
  for (const variant of config.variants) {
    if (!variant || !/^[a-z][a-z0-9-]*$/.test(variant.id) || ids.has(variant.id)) throw new Error("Palette IDs must be unique lowercase names.");
    ids.add(variant.id);
    if (typeof variant.name !== "string" || !variant.name.trim() || typeof variant.status !== "string" || !variant.status.trim()) {
      throw new Error(`Palette ${variant.id} needs a name and status label.`);
    }
    if (variant.id === "rainbow") continue;
    if (!HEX.test(variant.mane)) throw new Error(`Palette ${variant.id} needs a six-digit mane color.`);
    for (const role of FACE_ROLES) if (!HEX.test(variant.face?.[role])) throw new Error(`Invalid ${variant.id} face color: ${role}`);
  }
  if (config.variants[0].id !== "rainbow") throw new Error("The first palette must be the canonical rainbow.");
}

export function darker(color) {
  return "#" + color.slice(1).match(/../g).map(channel => Math.round(parseInt(channel, 16) * .68).toString(16).padStart(2, "0")).join("").toUpperCase();
}

export function makeVariant(source, config, variant) {
  const doc = parseSource(source);
  const title = variant.id === "rainbow" ? "Zoey the XUI Lion" : `Zoey the XUI Lion - ${variant.name} / ${variant.status}`;
  required(doc, "title").textContent = title;
  required(doc, "desc").textContent = "A front-facing lion with happy closed eyes, a cloud-shaped mane, and seamless color-matched outer borders. " +
    (variant.id === "rainbow" ? "The canonical XUI mascot uses the six-color Enamel rainbow." : `The ${variant.name.toLowerCase()} palette represents ${variant.status.toLowerCase()}.`);
  if (variant.id !== "rainbow") {
    for (const segment of children(required(doc, "segments"))) {
      segment.setAttribute("fill", variant.mane);
      segment.setAttribute("stroke", variant.mane);
    }
    required(doc, "mane-backing").setAttribute("fill", variant.mane);
    for (const segment of children(required(doc, "border-colors"))) {
      segment.setAttribute("fill", darker(variant.mane));
      segment.setAttribute("stroke", darker(variant.mane));
    }
    required(doc, "border-backing").setAttribute("fill", darker(variant.mane));
  }
  const roles = new Map(Object.entries(config.sourceFace).map(([role, color]) => [color.toUpperCase(), role]));
  for (const id of ["face", "face-border"]) {
    const group = required(doc, id);
    for (const node of [group, ...elements(group)]) for (const attribute of ["fill", "stroke"]) {
      const value = node.getAttribute(attribute);
      if (!value || value === "none") continue;
      const role = roles.get(value.toUpperCase());
      if (!role) throw new Error(`Unknown face paint ${value} in #${id}. Update sourceFace in palettes.json.`);
      if (variant.id !== "rainbow") node.setAttribute(attribute, variant.face[role]);
    }
  }
  return serialize(doc);
}

export function renderPng(svg, size) {
  const image = new Resvg(svg, { fitTo: { mode: "width", value: size }, font: { loadSystemFonts: false } }).render();
  if (image.width !== size || image.height !== size) throw new Error(`Unexpected ${image.width}x${image.height} output for ${size}px.`);
  return image.asPng();
}

export function encodeIco(frames) {
  if (!frames.length || frames.length > 65535) throw new Error("An ICO needs one or more frames.");
  const header = Buffer.alloc(6 + frames.length * 16);
  header.writeUInt16LE(1, 2);
  header.writeUInt16LE(frames.length, 4);
  let offset = header.length;
  const sizes = new Set();
  for (const [index, { size, png }] of frames.entries()) {
    if (!Number.isInteger(size) || size < 1 || size > 256 || sizes.has(size) ||
        !Buffer.isBuffer(png) || png.length < 24 || png.subarray(0, 8).toString("hex") !== "89504e470d0a1a0a" ||
        png.readUInt32BE(16) !== size || png.readUInt32BE(20) !== size) throw new Error("Invalid or duplicate ICO PNG frame.");
    sizes.add(size);
    const entry = 6 + index * 16;
    header[entry] = size === 256 ? 0 : size;
    header[entry + 1] = header[entry];
    header.writeUInt16LE(1, entry + 4);
    header.writeUInt16LE(32, entry + 6);
    header.writeUInt32LE(png.length, entry + 8);
    header.writeUInt32LE(offset, entry + 12);
    offset += png.length;
  }
  return Buffer.concat([header, ...frames.map(frame => frame.png)]);
}

const escape = value => value.replaceAll("&", "&amp;").replaceAll("<", "&lt;").replaceAll('"', "&quot;");
function gallery(variants) {
  return `<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Zoey the XUI Lion / Brand assets</title><link rel="icon" href="zoey.ico">
<style>body{margin:0;padding:32px;font:16px system-ui,sans-serif;color:#28343b;background:#f5f3ec}main{max-width:1200px;margin:auto}h1{margin-bottom:8px}p{line-height:1.5}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(240px,1fr));gap:20px}.card{background:#fff;border:1px solid #d4d5cf;border-radius:16px;padding:20px}img{display:block;width:100%;height:220px;object-fit:contain}.links{display:flex;flex-wrap:wrap;gap:12px}a{color:#235c8d}.sizes{display:flex;align-items:center;gap:12px;margin:12px 0}.sizes img{width:auto;height:auto}.dark .art{background:#24313a;border-radius:12px}button{font:inherit;padding:9px 14px;border:1px solid #789;border-radius:7px;background:#fff;margin:12px 0 24px;cursor:pointer}#error{color:#9e252d}.small{font-size:13px}</style></head>
<body><main><h1>Zoey the XUI Lion</h1><p>The canonical rainbow mascot and six single-hue status palettes. All keep the happy face and seamless outer border.</p>
<p><a href="../../../docs/specs/branding/zoey.md">Usage and regeneration guide</a></p><button id="background" aria-pressed="false" hidden>Dark artboards</button><p id="error" role="alert" hidden></p><div class="grid">
${variants.map(v => `<article class="card"><div class="art"><img src="${v.stem}.svg" alt="${escape(v.name)} Zoey"></div><h2>${escape(v.name)}</h2><p>${escape(v.status)}</p><div class="sizes art">${[16,32,48].map(size => `<img src="${v.stem}-${size}.png" width="${size}" height="${size}" alt="${size}px">`).join("")}</div><div class="links"><a href="${v.stem}.svg" download>SVG</a><a href="${v.stem}.ico" download>ICO</a><a href="${v.stem}-256.png" download>PNG 256</a><a href="${v.stem}-1024.png" download>PNG 1024</a></div></article>`).join("\n")}
</div><p class="small">Generated assets. Edit assets/branding/zoey.svg, then run npm run branding:generate. Do not use color alone to communicate status.</p></main>
<script>const b=document.getElementById("background");b.hidden=false;b.onclick=()=>b.setAttribute("aria-pressed",String(document.body.classList.toggle("dark")));for(const img of document.images){const report=()=>{const e=document.getElementById("error");e.hidden=false;e.textContent="Cannot load "+img.getAttribute("src")+". Regenerate the branding assets, then reload.";};img.addEventListener("error",report,{once:true});if(img.complete&&!img.naturalWidth)report();}</script></body></html>\n`;
}

export function generateAssets(source, config) {
  validatePalettes(config);
  const files = new Map();
  const canonical = makeVariant(source, config, config.variants[0]);
  files.set(SOURCE, Buffer.from(canonical));
  const variants = [];
  for (const variant of config.variants) {
    const stem = variant.id === "rainbow" ? "zoey" : `zoey-${variant.id}`;
    const svg = makeVariant(canonical, config, variant);
    files.set(`${OUTPUT}/${stem}.svg`, Buffer.from(svg));
    const pngs = new Map(PNG_SIZES.map(size => [size, renderPng(svg, size)]));
    for (const [size, png] of pngs) files.set(`${OUTPUT}/${stem}-${size}.png`, png);
    files.set(`${OUTPUT}/${stem}.ico`, encodeIco(ICO_SIZES.map(size => ({ size, png: pngs.get(size) }))));
    variants.push({ id: variant.id, name: variant.name, status: variant.status, stem });
    if (variant.id === "rainbow") files.set(EXTENSION_ICON, pngs.get(256));
  }
  files.set(`${OUTPUT}/site.webmanifest`, Buffer.from(JSON.stringify({
    name: "XUI", short_name: "XUI",
    icons: [192, 512].map(size => ({ src: `zoey-${size}.png`, sizes: `${size}x${size}`, type: "image/png", purpose: "any" })),
    display: "browser",
  }, null, 2) + "\n"));
  files.set(`${OUTPUT}/index.html`, Buffer.from(gallery(variants)));
  files.set(`${OUTPUT}/manifest.json`, Buffer.from(JSON.stringify({
    source: SOURCE, sourceSha256: sha256(canonical), renderer: "@resvg/resvg-js@2.6.2",
    pngSizes: PNG_SIZES, icoSizes: ICO_SIZES, variants,
    files: Object.fromEntries([...files].map(([name, data]) => [name, sha256(data)])),
  }, null, 2) + "\n"));
  return files;
}

async function existing(file) {
  try { return await readFile(file); }
  catch (error) { if (error.code === "ENOENT") return null; throw error; }
}

export async function synchronize(root, files, check) {
  const stale = [];
  for (const [name, content] of files) {
    const previous = await existing(local(root, name));
    if (!previous || !content.equals(previous)) stale.push(name);
  }
  let entries = [];
  try { entries = await readdir(local(root, OUTPUT)); }
  catch (error) { if (error.code !== "ENOENT") throw error; }
  const unexpected = entries.filter(name => !files.has(`${OUTPUT}/${name}`));
  if (unexpected.length) throw new Error(`Unexpected generated assets: ${unexpected.join(", ")}. Remove only obsolete generated files before regeneration.`);
  if (check) {
    if (stale.length) throw new Error(`Missing or stale branding assets:\n${stale.join("\n")}\nRun npm run branding:generate.`);
    return 0;
  }
  for (const name of stale) {
    const target = local(root, name);
    await mkdir(path.dirname(target), { recursive: true });
    const temporary = `${target}.${randomUUID()}.tmp`;
    try {
      await writeFile(temporary, files.get(name));
      await rename(temporary, target);
    } finally {
      try { await unlink(temporary); }
      catch (error) { if (error.code !== "ENOENT") throw error; }
    }
  }
  return stale.length;
}

async function main() {
  let source = local(ROOT, SOURCE), check = false;
  const args = process.argv.slice(2);
  for (let index = 0; index < args.length; index++) {
    if (args[index] === "--check") check = true;
    else if (args[index] === "--source" && args[index + 1] && !args[index + 1].startsWith("--")) source = path.resolve(args[++index]);
    else if (args[index] === "--help") {
      console.log("Generate Zoey SVG/PNG/ICO assets: npm run branding:generate [-- --source path\\to\\zoey.svg]\nCheck without writing: npm run branding:check");
      return;
    } else throw new Error(`Unknown or incomplete argument: ${args[index]}`);
  }
  if (check && source !== local(ROOT, SOURCE)) throw new Error("--source cannot be combined with --check.");
  const config = JSON.parse(await readFile(local(ROOT, "assets/branding/palettes.json"), "utf8"));
  const files = generateAssets(await readFile(source, "utf8"), config);
  const count = await synchronize(ROOT, files, check);
  console.log(check ? `Zoey assets are current: ${config.variants.length} palettes, ${PNG_SIZES.length} PNG sizes, ${ICO_SIZES.length} ICO frames.`
    : `Generated Zoey assets: ${count} files updated. Preview ${local(ROOT, `${OUTPUT}/index.html`)}`);
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  main().catch(error => { console.error(error.message); process.exitCode = 1; });
}
