import assert from "node:assert/strict";
import { createServer } from "node:http";
import { readFile, writeFile } from "node:fs/promises";
import { createRequire } from "node:module";
import { resolve, relative, isAbsolute, extname, sep } from "node:path";

const [directory, packageJson, resultFile] = process.argv.slice(2);
if (!directory || !packageJson) throw new Error("Supply published wwwroot and browser test package.json.");
const root = resolve(directory);
const { chromium } = createRequire(resolve(packageJson))("@playwright/test");
const prefix = "/nested/localization/";
const types = { ".html": "text/html", ".js": "text/javascript", ".css": "text/css", ".wasm": "application/wasm", ".json": "application/json", ".dat": "application/octet-stream" };
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
const timeout = setTimeout(() => { console.error("Packaged localization browser fixture timed out."); process.exit(1); }, 120000);
try {
    browser = await chromium.launch({ channel: process.env.XUI_BROWSER_CHANNEL, headless: true, timeout: 30000 });
    const page = await browser.newPage();
    page.setDefaultTimeout(30000);
    const errors = [];
    const satelliteRequests = [];
    page.on("pageerror", error => errors.push(String(error)));
    page.on("response", response => { if (response.status() >= 400) errors.push(`${response.status()} ${response.url()}`); });
    page.on("request", request => {
        if (/\/(de|ar)\/[^/]*\.resources\./.test(new URL(request.url()).pathname)) satelliteRequests.push(request.url());
    });
    await page.goto(`http://127.0.0.1:${server.address().port}${prefix}`);
    await page.waitForFunction(() => document.getElementById("result").textContent !== "Loading", null, { timeout: 60000 });
    assert.equal(await page.locator("#result").textContent(), "PASS: packaged localization cultures and fallback.");
    const input = page.locator('[data-xui-id="localization-draft"] input');
    const handle = await input.elementHandle();
    const draft = "Owned draft e\u0301 \u0645\u0631\u062d\u0628\u0627 \u4e2d\u6587";
    await input.fill(draft);
    await input.evaluate(node => { node.focus(); node.setSelectionRange(4, 9); });
    for (const [language, title] of [
        ["german", "Sprachwerkstatt"],
        ["arabic", "\u0648\u0631\u0634\u0629 \u0627\u0644\u0644\u063a\u0627\u062a"],
        ["english", "Language workbench"]
    ]) {
        await page.locator(`[data-xui-id="localization-${language}"]`).click();
        await page.waitForFunction(expected => document.querySelector('[data-xui-id="localization-title"]').textContent === expected, title);
        assert.equal(await input.inputValue(), draft);
        assert.ok(await handle.evaluate(node => node === document.querySelector('[data-xui-id="localization-draft"] input')));
        assert.deepEqual(await input.evaluate(node => [
            document.activeElement.tagName, document.activeElement.dataset.xuiId, node.selectionStart, node.selectionEnd
        ]), ["BUTTON", `localization-${language}`, 4, 9], "Pointer activation keeps normal button focus and retains the editor's selected range.");
    }
    await input.focus();
    await input.evaluate(node => node.setSelectionRange(4, 9));
    for (const language of ["german", "arabic", "english"]) {
        await page.locator(`[data-xui-id="localization-${language}"]`).evaluate(node => node.click());
        assert.equal(await input.inputValue(), draft);
        assert.ok(await handle.evaluate(node => node === document.querySelector('[data-xui-id="localization-draft"] input')));
        assert.deepEqual(await input.evaluate(node => [document.activeElement === node, node.selectionStart, node.selectionEnd]),
            [true, 4, 9], "A locale update without a focus-changing pointer gesture preserves the active editor and range.");
    }
    await page.locator('[data-xui-id="localization-german"]').click();
    await page.locator('[data-xui-id="localization-increment"]').click();
    assert.equal(await page.locator('[data-xui-id="localization-count"]').textContent(), "Vorschauanzahl: 1.235");
    await page.locator('[data-xui-id="localization-english"]').click();
    assert.equal(await page.locator('[data-xui-id="localization-count"]').textContent(), "Preview count: 1,235");
    assert.equal(await page.locator("#errors").isVisible(), false);
    assert.deepEqual(satelliteRequests, [], "Language bundles are embedded in the shared assembly, not lazy satellite payloads.");
    assert.deepEqual(errors, []);
    await handle.dispose();
    if (resultFile) await writeFile(resultFile, JSON.stringify({
        status: "passed",
        browserVersion: browser.version(),
        basePath: prefix,
        languages: ["en", "de", "ar"],
        livePointerButtons: true,
        pointerFocus: "native button",
        focusedEditorDuringProgrammaticLocaleUpdate: true,
        draftAndSelectionPreserved: true,
        fullReloadsDuringSwitching: 0,
        satelliteRequests,
        errors
    }, null, 2));
    console.log("Published Wasm embedded language bundles, live pointer buttons with native focus, focused-editor locale updates, retained selection and fallback passed without reload or internal runtime hooks.");
} finally {
    clearTimeout(timeout);
    try { await browser?.close(); }
    finally { server.closeAllConnections(); await new Promise(done => server.close(done)); }
}
