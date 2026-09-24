import { test, expect, demo, command } from "./fixtures.js";

async function isolatedClipboard(page) {
    await page.addInitScript(() => {
        window.clipboardFixture = { text: "", calls: 0, error: null, pending: false };
        const action = () => {
            const state = window.clipboardFixture;
            state.calls++;
            if (state.error === "Error") throw new Error("Clipboard fixture failure");
            if (state.error) throw new DOMException("Clipboard fixture denial", state.error);
            return state;
        };
        Object.defineProperty(navigator, "clipboard", {
            configurable: true,
            value: {
                async readText() {
                    const state = action();
                    if (state.pending) return new Promise(() => {});
                    return state.text;
                },
                async writeText(text) { action().text = text; }
            }
        });
    });
}

test("managed browser services round-trip isolated clipboard data and cancel before any native call", async ({ page }) => {
    await isolatedClipboard(page);
    await demo(page);
    expect(await command(page, "services-availability")).toEqual({
        Clipboard: "RequiresUserGesture", OpenUri: "RequiresUserGesture",
        OpenFile: "Unsupported", SaveFile: "Unsupported", ApplicationStorage: "Available"
    });
    expect(await command(page, "services-read")).toEqual({ status: "Completed", value: "", error: null });
    const text = "Private test fixture\nZo\u00eb \u674e";
    expect(await command(page, "services-write", text)).toEqual({ status: "Completed", value: true, error: null });
    expect(await command(page, "services-read")).toEqual({ status: "Completed", value: text, error: null });
    const before = await page.evaluate(() => window.clipboardFixture.calls);
    expect(await command(page, "services-cancelled")).toEqual({ taskCancelled: true });
    await expect(command(page, "services-write", "invalid\0text")).rejects.toThrow(/NUL/);
    expect(await page.evaluate(() => window.clipboardFixture.calls)).toBe(before);
    await page.evaluate(() => { window.clipboardFixture.pending = true; });
    expect(await command(page, "services-cancel-pending")).toEqual({ taskCancelled: true });
    expect(await page.evaluate(() => window.clipboardFixture.calls)).toBe(before + 1);
});

test("managed browser service results distinguish native denial cancellation unsupported and failure", async ({ page }) => {
    await isolatedClipboard(page);
    await demo(page);
    for (const [error, status] of [
        ["NotAllowedError", "Denied"], ["SecurityError", "Denied"],
        ["AbortError", "Cancelled"], ["NotSupportedError", "Unsupported"], ["Error", "Failed"]
    ]) {
        await page.evaluate(value => { window.clipboardFixture.error = value; }, error);
        const read = await command(page, "services-read");
        expect(read.status).toBe(status);
        expect(read.value).toBeNull();
        if (status === "Failed") expect(read.error).toContain("Clipboard fixture failure");
        else expect(read.error).toBeNull();
        expect((await command(page, "services-write", "fixture")).status).toBe(status);
    }
    await page.evaluate(() => { Object.defineProperty(navigator, "clipboard", { value: undefined }); });
    expect((await command(page, "services-availability")).Clipboard).toBe("Unsupported");
    expect((await command(page, "services-read")).status).toBe("Unsupported");
});

test("native no-activation service calls deny before clipboard access or popup creation", async ({ page }) => {
    await page.route("**/services-no-gesture", route => route.fulfill({
        contentType: "text/html",
        body: `<!doctype html><title>Service activation boundary</title>
            <script type="module">
                import { readClipboard, writeClipboard, openUri } from "/_content/Xui.Web/xui-services.js";
                if (navigator.userActivation.isActive) throw new Error("Unexpected activation in fixture");
                window.noGesture = [await readClipboard(), await writeClipboard("must not be written"), openUri(location.origin)];
            </script>`
    }));
    await page.goto("/services-no-gesture");
    await expect.poll(() => page.evaluate(() => window.noGesture?.map(result => result.status))).toEqual(["Denied", "Denied", "Denied"]);
    expect(page.context().pages()).toHaveLength(1);
});

test("managed URI launch validates input opens only a controlled loopback popup and detaches its opener", async ({ page }) => {
    await isolatedClipboard(page);
    await demo(page);
    for (const uri of ["javascript:alert(1)", "data:text/html,test", "file:///C:/test", "/relative", "https://name:password@example.test/"]) {
        await expect(command(page, "services-uri", uri)).rejects.toThrow(/absolute|credentials/);
    }
    await page.context().route("**/services-target", route => route.fulfill({
        contentType: "text/html", body: "<!doctype html><title>Owned service target</title><p>Controlled loopback page</p>"
    }));
    const popupReady = page.waitForEvent("popup");
    const opened = await command(page, "services-uri", new URL("/services-target", page.url()).href);
    expect(opened).toEqual({ status: "Completed", value: true, error: null });
    const popup = await popupReady;
    try {
        await expect(popup).toHaveTitle("Owned service target");
        expect(await popup.evaluate(() => window.opener)).toBeNull();
    } finally { await popup.close(); }
    await page.evaluate(() => { window.open = () => null; });
    expect((await command(page, "services-uri", new URL("/services-target", page.url()).href)).status).toBe("Denied");
});

test("JavaScript service boundary independently rejects unsafe URIs and invalid clipboard text", async ({ page }) => {
    await isolatedClipboard(page);
    await demo(page);
    const rejected = await page.evaluate(async () => {
        const module = await import("/_content/Xui.Web/xui-services.js");
        const failures = [];
        for (const uri of ["javascript:alert(1)", "/relative", "https://u:p@example.test/", "https://example.test/\nnext"]) {
            try { module.openUri(uri); failures.push(false); }
            catch (error) { failures.push(error instanceof TypeError); }
        }
        try { await module.writeClipboard("nul\0"); failures.push(false); }
        catch (error) { failures.push(error instanceof TypeError); }
        return failures;
    });
    expect(rejected).toEqual([true, true, true, true, true]);
    expect(await page.evaluate(() => window.clipboardFixture.calls)).toBe(0);
    const accepted = await page.evaluate(async () => {
        const module = await import("/_content/Xui.Web/xui-services.js");
        const targets = [];
        window.open = () => ({ opener: window, location: { replace: uri => targets.push(uri) }, close() {} });
        const statuses = ["http://example.test/", "https://example.test/", "mailto:example@example.test", "tel:+15550100"]
            .map(uri => module.openUri(uri).status);
        return { statuses, count: targets.length };
    });
    expect(accepted).toEqual({ statuses: ["Completed", "Completed", "Completed", "Completed"], count: 4 });
});
