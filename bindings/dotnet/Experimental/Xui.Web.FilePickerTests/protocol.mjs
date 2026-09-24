import assert from "node:assert/strict";
import { createServer } from "node:http";
import { readFile } from "node:fs/promises";
import { createRequire } from "node:module";

const require = createRequire(new URL("../WebDemo.Tests/package.json", import.meta.url));
const { chromium } = require("@playwright/test");
const source = await readFile(new URL("../Xui.Web/wwwroot/xui-filepicker.js", import.meta.url));
const server = createServer((request, response) => {
    if (request.url === "/xui-filepicker.js")
        response.writeHead(200, { "Content-Type": "text/javascript" }).end(source);
    else if (request.url === "/")
        response.writeHead(200, { "Content-Type": "text/html" }).end('<!doctype html><title>Owned picker fixture</title><button id="gesture">Fixture gesture</button>');
    else response.writeHead(404).end();
});
await new Promise((done, reject) => { server.once("error", reject); server.listen(0, "127.0.0.1", done); });
let browser;
const timer = setTimeout(() => { console.error("File picker protocol fixture timed out."); process.exit(1); }, 90000);
try {
    browser = await chromium.launch({ channel: process.env.XUI_BROWSER_CHANNEL, headless: true, timeout: 30000 });
    const page = await browser.newPage();
    const errors = [];
    page.on("pageerror", error => errors.push(String(error)));
    await page.goto(`http://127.0.0.1:${server.address().port}/`);
    await page.evaluate(async () => {
        const { createFilePicker } = await import("/xui-filepicker.js");
        const assertions = [];
        function check(value, message) {
            if (!value) throw new Error(message);
            assertions.push(message);
        }
        let input;
        let calls = 0;
        let action;
        // Replacing only the native API seam guarantees these fixtures never open OS UI.
        Object.defineProperty(HTMLInputElement.prototype, "showPicker", {
            configurable: true,
            value() { calls++; input = this; action?.(this); }
        });
        const activation = Object.getOwnPropertyDescriptor(navigator, "userActivation");
        Object.defineProperty(navigator, "userActivation", { configurable: true, value: { isActive: false } });
        const deniedResults = [];
        const denied = createFilePicker({ invokeMethodAsync: async (...args) => { deniedResults.push(args); } }, "3");
        denied.show();
        check(deniedResults[0][1] === "denied" && calls === 0, "Expired user activation never reaches the native picker.");
        denied.release();
        if (activation) Object.defineProperty(navigator, "userActivation", activation);
        else delete navigator.userActivation;
        window.runFilePickerFixtures = async () => {
            const callback = () => {
                const values = [];
                return { values, invokeMethodAsync: async (...args) => { values.push(args); } };
            };
            let receiver = callback();
            let picker = createFilePicker(receiver, "3");
            picker.show();
            check(calls === 1 && input.isConnected && input.multiple === false, "Native picker invoked synchronously with single selection.");
            const file = new File([new Uint8Array([1, 2, 3])], "../opaque.txt");
            const slices = [];
            const nativeSlice = file.slice.bind(file);
            file.slice = (start, end) => { slices.push([start, end]); return nativeSlice(start, end); };
            file.arrayBuffer = () => { throw new Error("Whole-file loading is forbidden."); };
            function select(target) {
                const transfer = new DataTransfer();
                transfer.items.add(file);
                target.files = transfer.files;
            }
            select(input);
            input.dispatchEvent(new Event("change"));
            check(receiver.values[0][1] === "completed" && receiver.values[0][2] === "../opaque.txt",
                "Selected File metadata delivered without path conversion.");
            check(!input.isConnected && document.querySelectorAll("input[type=file]").length === 0, "Selection removes input and listeners.");
            check(Array.from(await picker.read(2)).join() === "1,2", "First bounded chunk.");
            check(Array.from(await picker.read(2)).join() === "3", "Final short chunk.");
            check((await picker.read(1)).length === 0, "Genuine EOF.");
            check(JSON.stringify(slices) === "[[0,2],[2,3]]", "Only requested File.slice ranges are read.");
            picker.release();
            picker.release();
            await picker.read(1).then(() => { throw new Error("Released file was readable."); }, () => assertions.push("Released handle rejects reads."));

            receiver = callback();
            picker = createFilePicker(receiver, "3");
            picker.show();
            const blocked = new File([new Uint8Array([9])], "pending.bin");
            let releaseRead;
            blocked.slice = () => ({ arrayBuffer: () => new Promise(done => { releaseRead = done; }) });
            const blockedTransfer = new DataTransfer();
            blockedTransfer.items.add(blocked);
            input.files = blockedTransfer.files;
            input.dispatchEvent(new Event("change"));
            const pendingRead = picker.read(1);
            await picker.read(1).then(() => { throw new Error("Concurrent read succeeded."); }, () => assertions.push("Concurrent read rejected."));
            picker.release();
            releaseRead(new Uint8Array([9]).buffer);
            await pendingRead.then(() => { throw new Error("Canceled read delivered bytes."); }, () => assertions.push("Canceled in-flight read cannot return late bytes."));

            receiver = callback();
            picker = createFilePicker(receiver, "2");
            picker.show();
            select(input);
            input.dispatchEvent(new Event("change"));
            check(receiver.values[0][1] === "oversized", `Oversize metadata rejected before any file read: ${JSON.stringify(receiver.values)}.`);
            picker.release();

            receiver = callback();
            picker = createFilePicker(receiver, "3");
            picker.show();
            input.dispatchEvent(new Event("cancel"));
            check(receiver.values[0][1] === "cancelled", "Native cancel event has its own outcome.");
            picker.release();

            receiver = callback();
            picker = createFilePicker(receiver, "3");
            picker.show();
            const stale = input;
            picker.release();
            select(stale);
            stale.dispatchEvent(new Event("change"));
            check(receiver.values.length === 0 && !stale.isConnected, "External cancellation drops late change without retaining files.");

            for (const [name, expected] of [["NotAllowedError", "denied"], ["SecurityError", "denied"], ["NotSupportedError", "unsupported"], ["AbortError", "failed"]]) {
                receiver = callback();
                action = () => { throw new DOMException("fixture", name); };
                picker = createFilePicker(receiver, "3");
                picker.show();
                check(receiver.values[0][1] === expected, `Explicit ${name} result.`);
                picker.release();
            }
            action = undefined;
            receiver = callback();
            picker = createFilePicker(receiver, "3");
            picker.show();
            select(input);
            input.dispatchEvent(new Event("change"));
            await picker.read(65537).then(() => { throw new Error("Oversized read request accepted."); },
                error => check(error instanceof RangeError, "Native read requests are bounded."));
            picker.release();
            receiver = callback();
            Object.defineProperty(HTMLInputElement.prototype, "showPicker", { configurable: true, value: undefined });
            picker = createFilePicker(receiver, "3");
            picker.show();
            check(receiver.values[0][1] === "unsupported", "Missing native picker is not a synthetic success.");
            picker.release();
            window.fixtureResult = assertions;
        };
        document.getElementById("gesture").onclick = () => {
            window.fixtureTask = window.runFilePickerFixtures();
        };
    });
    await page.locator("#gesture").click();
    const assertions = await page.evaluate(async () => { await window.fixtureTask; return window.fixtureResult; });
    assert.ok(assertions.length >= 15);
    assert.deepEqual(errors, []);
    console.log(`Browser native-file protocol: ${assertions.length} assertions passed using synthetic File objects; no OS dialog or user file accessed.`);
} finally {
    clearTimeout(timer);
    try { await browser?.close(); }
    finally { server.closeAllConnections(); await new Promise(done => server.close(done)); }
}
