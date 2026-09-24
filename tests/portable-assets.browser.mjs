import assert from "node:assert/strict";
import { createServer } from "node:http";
import { readFile } from "node:fs/promises";
import { createRequire } from "node:module";
import { resolve, relative, isAbsolute, extname, sep } from "node:path";

const [directory, packageJson] = process.argv.slice(2);
if (!directory || !packageJson) throw new Error("Supply published wwwroot and browser test package.json.");
const root = resolve(directory);
const { chromium } = createRequire(resolve(packageJson))("@playwright/test");
const prefix = "/nested/assets/";
const types = { ".html": "text/html", ".js": "text/javascript", ".wasm": "application/wasm", ".json": "application/json" };
const server = createServer(async (request, response) => {
    const pathname = new URL(request.url, "http://localhost").pathname;
    if (!pathname.startsWith(prefix)) { response.writeHead(404).end(); return; }
    const filename = resolve(root, pathname.slice(prefix.length) || "index.html");
    const path = relative(root, filename);
    if (isAbsolute(path) || path === ".." || path.startsWith(`..${sep}`)) { response.writeHead(404).end(); return; }
    try {
        let bytes = await readFile(filename);
        if (filename === resolve(root, "index.html"))
            bytes = bytes.toString().replace('<base href="/">', `<base href="${prefix}">`);
        response.writeHead(200, { "Content-Type": types[extname(filename)] ?? "application/octet-stream" }).end(bytes);
    } catch (error) {
        if (error.code !== "ENOENT" && error.code !== "EISDIR") console.error(error);
        response.writeHead(error.code === "ENOENT" || error.code === "EISDIR" ? 404 : 500).end();
    }
});
await new Promise((done, reject) => { server.once("error", reject); server.listen(0, "127.0.0.1", done); });
let browser;
const timeout = setTimeout(() => { console.error("Packaged asset browser fixture timed out."); process.exit(1); }, 120000);
try {
    browser = await chromium.launch({ channel: process.env.XUI_BROWSER_CHANNEL, headless: true, timeout: 30000 });
    const page = await browser.newPage();
    const errors = [];
    const assets = [];
    page.on("pageerror", error => errors.push(String(error)));
    page.on("request", request => {
        if (/\.(png|jpg|ttf|otf)(\?|$)/.test(request.url())) assets.push(request.url());
    });
    await page.goto(`http://127.0.0.1:${server.address().port}${prefix}`);
    await page.waitForFunction(() => document.getElementById("result").textContent !== "Loading", null, { timeout: 60000 });
    assert.equal(await page.locator("#result").textContent(), "PASS: shared packaged image/font fixture bytes.");
    assert.deepEqual(errors, []);
    assert.deepEqual(assets, [], "Embedded assets must not trigger filesystem or per-asset network resolution.");
    console.log("Published Wasm shared-assembly asset bytes passed at /nested/assets/ without asset URL requests.");
} finally {
    clearTimeout(timeout);
    try { await browser?.close(); }
    finally { server.closeAllConnections(); await new Promise(done => server.close(done)); }
}
