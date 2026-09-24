import assert from "node:assert/strict";
import { createServer } from "node:http";
import { readFile } from "node:fs/promises";
import { extname, resolve, relative, isAbsolute, sep } from "node:path";
import { createRequire } from "node:module";

const [rootPath, packageJson] = process.argv.slice(2);
if (!rootPath || !packageJson) throw new Error("Supply published wwwroot and the browser test package.json.");
const root = resolve(rootPath);
const require = createRequire(resolve(packageJson));
const { chromium } = require("@playwright/test");
const prefix = "/nested/portable/";
const types = { ".html": "text/html", ".js": "text/javascript", ".css": "text/css", ".wasm": "application/wasm", ".json": "application/json" };
const server = createServer(async (request, response) => {
    const pathname = new URL(request.url, "http://localhost").pathname;
    if (!pathname.startsWith(prefix)) { response.writeHead(404).end(); return; }
    const filename = resolve(root, pathname.slice(prefix.length) || "index.html");
    const path = relative(root, filename);
    if (isAbsolute(path) || path === ".." || path.startsWith(`..${sep}`)) { response.writeHead(404).end(); return; }
    try {
        let body = await readFile(filename);
        if (filename === resolve(root, "index.html"))
            body = body.toString().replace('<base href="/">', `<base href="${prefix}">`);
        response.writeHead(200, { "Content-Type": types[extname(filename)] ?? "application/octet-stream" }).end(body);
    } catch (error) {
        if (error.code !== "ENOENT" && error.code !== "EISDIR") console.error(error);
        response.writeHead(error.code === "ENOENT" || error.code === "EISDIR" ? 404 : 500).end();
    }
});
await new Promise((done, reject) => { server.once("error", reject); server.listen(0, "127.0.0.1", done); });
let browser;
const timeout = setTimeout(() => { console.error("Packaged browser acceptance timed out."); process.exit(1); }, 120000);
try {
    browser = await chromium.launch({ channel: process.env.XUI_BROWSER_CHANNEL, headless: true, timeout: 30000 });
    const page = await browser.newPage();
    page.setDefaultTimeout(30000);
    const failures = [];
    page.on("pageerror", error => failures.push(String(error)));
    page.on("response", response => { if (response.status() >= 400) failures.push(`${response.status()} ${response.url()}`); });
    const url = `http://127.0.0.1:${server.address().port}${prefix}`;
    for (const asset of ["_content/Xui.Web/xui-dom.js", "_content/Xui.Web/xui-dom.css"]) {
        const response = await fetch(`${url}${asset}`);
        assert.equal(response.status, 200, asset);
        assert.ok((await response.text()).length > 0, asset);
    }
    await page.goto(url);
    await page.getByRole("button", { name: "Increment", exact: true }).click();
    await page.getByText("Count: 1", { exact: true }).waitFor();
    const input = page.getByRole("textbox", { name: "Your name", exact: true });
    await input.fill("Ada");
    await page.getByText("Hello, Ada!", { exact: true }).waitFor();
    assert.equal(await page.evaluate(() => getComputedStyle(document.querySelector(".xui-root")).boxSizing), "border-box");
    assert.equal(await page.evaluate(() => typeof window.xuiTest), "undefined");
    assert.equal(await page.locator("#errors").isVisible(), false);
    assert.deepEqual(failures, []);
    console.log("Packaged Release browser interaction, nested JS/CSS resolution, and absence of test bridge passed.");
} finally {
    clearTimeout(timeout);
    try { await browser?.close(); }
    finally { await new Promise(done => server.close(done)); server.closeAllConnections(); }
}
