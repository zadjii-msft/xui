import { test, expect, byId } from "./fixtures.js";
import { observeNavigation } from "./navigation.js";

async function published(page) {
    await page.goto("./?test");
    await expect(page.getByRole("button", { name: "Increment", exact: true })).toBeVisible();
    expect(await page.evaluate(() => typeof window.xuiTest)).toBe("undefined");
}

test("published assets use the deployment path and Wasm MIME; C# callbacks work offline", async ({ page, baseURL }) => {
    const requests = [];
    const wasm = [];
    page.on("request", request => requests.push(request.url()));
    page.on("response", response => { if (/\.wasm(?:$|\?)/.test(response.url())) wasm.push(response); });
    await published(page);
    expect(wasm.length).toBeGreaterThan(0);
    expect(wasm.some(response => /dotnet.*\.wasm/.test(response.url()))).toBe(true);
    for (const response of wasm) {
        expect(response.status()).toBe(200);
        expect(await response.headerValue("content-type")).toBe("application/wasm");
    }
    expect(requests.every(url => url.startsWith(baseURL))).toBe(true);
    expect(requests.some(url => url.includes("test-driver.js"))).toBe(false);
    expect(await page.evaluate(() => document.baseURI)).toBe(baseURL);
    await expect(page.locator("#app .xui-node")).toHaveCount(11);
    await expect(page.locator("canvas")).toHaveCount(0);
    await expect(page.getByRole("region", { name: "Greeting and counter" })).toBeVisible();
    const loadedRequests = requests.length;
    await page.context().setOffline(true);
    await page.getByRole("button", { name: "Increment", exact: true }).click();
    await expect(byId(page, "count")).toHaveText("Count: 1");
    await page.getByRole("textbox", { name: "Your name" }).fill("Ada");
    await page.getByRole("textbox").press("Enter");
    await expect(byId(page, "greeting")).toHaveText("Hello, Ada!");
    await page.getByRole("button", { name: "Reset", exact: true }).click();
    await expect(byId(page, "count")).toHaveText("Count: 0");
    await expect(page.getByRole("textbox")).toHaveValue("");
    await expect(byId(page, "greeting")).toHaveText("Type a name, then submit.");
    expect(requests).toHaveLength(loadedRequests);
});

test("published updates retain native input, selection, and composition without the test bridge", async ({ page }) => {
    await published(page);
    const input = page.getByRole("textbox", { name: "Your name" });
    await input.fill("Grace");
    await input.evaluate(node => {
        window.originalInput = node;
        window.originalNodes = [...document.querySelectorAll("#app .xui-node")];
        node.focus();
        node.setSelectionRange(1, 4);
        window.valueWrites = 0;
        const descriptor = Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, "value");
        Object.defineProperty(node, "value", {
            get() { return descriptor.get.call(this); },
            set(value) { window.valueWrites++; descriptor.set.call(this, value); }
        });
        node.dispatchEvent(new CompositionEvent("compositionstart", { bubbles: true }));
        node.dispatchEvent(new KeyboardEvent("keydown", { key: "Enter", isComposing: true, bubbles: true }));
        document.querySelector('[data-xui-id="increment"]').click();
    });
    await expect(byId(page, "count")).toHaveText("Count: 1");
    await expect(byId(page, "greeting")).toHaveText("Type a name, then submit.");
    expect(await input.evaluate(node => ({
        same: node === window.originalInput,
        tree: window.originalNodes.every((n, i) => document.querySelectorAll("#app .xui-node")[i] === n),
        focused: node === document.activeElement,
        start: node.selectionStart, end: node.selectionEnd, writes: window.valueWrites
    }))).toEqual({ same: true, tree: true, focused: true, start: 1, end: 4, writes: 0 });
    await input.evaluate(node => node.dispatchEvent(new CompositionEvent("compositionend", { bubbles: true })));
    await input.press("Z");
    await input.press("Enter");
    await expect(byId(page, "greeting")).toHaveText("Hello, GZe!");
    expect(await input.evaluate(node => [node.selectionStart, node.selectionEnd, window.valueWrites])).toEqual([2, 2, 0]);
});

test("published interop errors remain explicit and subsequent C# events recover", async ({ page, diagnostics }) => {
    await published(page);
    diagnostics.expected.push(/XUI browser error:.*Text cannot contain NUL/s);
    await page.getByRole("textbox").evaluate(node => {
        node.value = "invalid\0text";
        node.dispatchEvent(new Event("input", { bubbles: true }));
    });
    await expect(page.getByRole("alert")).toContainText("Text cannot contain NUL");
    await page.getByRole("textbox").fill("<b>Recovered</b>");
    await page.getByRole("textbox").press("Enter");
    await expect(byId(page, "greeting")).toHaveText("Hello, <b>Recovered</b>!");
    await expect(byId(page, "greeting").locator("b")).toHaveCount(0);
});

test("published native keyboard and accessible input work at a narrow viewport", async ({ page }) => {
    await page.setViewportSize({ width: 240, height: 200 });
    await published(page);
    const input = page.getByRole("textbox", { name: "Your name", exact: true });
    await expect(input).toHaveAttribute("placeholder", "Ada");
    expect(await input.evaluate(node => [...node.labels].map(label => label.textContent))).toEqual(["Your name"]);
    await input.fill("Keyboard");
    await input.press("Tab");
    const increment = page.getByRole("button", { name: "Increment", exact: true });
    await expect(increment).toBeFocused();
    await increment.press("Space");
    await expect(byId(page, "count")).toHaveText("Count: 1");
    await increment.press("Tab");
    const submit = page.getByRole("button", { name: "Submit", exact: true });
    await expect(submit).toBeFocused();
    await submit.press("Enter");
    await expect(byId(page, "greeting")).toHaveText("Hello, Keyboard!");
    expect(await page.evaluate(() => {
        const root = document.querySelector(".xui-root").getBoundingClientRect();
        const scroll = document.querySelector(".xui-scrollview");
        return { width: root.width, height: root.height, scrolls: scroll.scrollHeight > scroll.clientHeight,
            inputFits: document.querySelector(".xui-textinput").getBoundingClientRect().width <= scroll.clientWidth };
    })).toEqual({ width: 240, height: 200, scrolls: true, inputFits: true });
});

test("real terminal navigation disposes the published tree before leaving", async ({ page, baseURL }) => {
    await published(page);
    await observeNavigation(page);
    await page.getByRole("button", { name: "Increment", exact: true }).click();
    await expect(byId(page, "count")).toHaveText("Count: 1");
    await page.goto(new URL("/__xui_acceptance__/away.html", baseURL).href);
    await expect(page.getByRole("heading", { name: "Navigation target" })).toBeVisible();
    expect(await page.evaluate(() => JSON.parse(sessionStorage.getItem("xui-pagehide"))))
        .toEqual({ persisted: false, nodes: 0 });
    await page.goBack();
    await expect(byId(page, "count")).toHaveText("Count: 0");
    await expect(page.getByRole("textbox")).toHaveValue("");
    expect(await page.evaluate(() => typeof window.xuiTest)).toBe("undefined");
});

test("real back-forward cache restores the same published C# app and DOM @bfcache", async ({ page, baseURL }, testInfo) => {
    await published(page);
    await page.getByRole("textbox").fill("Retained");
    await page.getByRole("button", { name: "Increment", exact: true }).click();
    await expect(byId(page, "count")).toHaveText("Count: 1");
    await page.getByRole("textbox").evaluate(node => { node.focus(); node.setSelectionRange(1, 4); });
    await observeNavigation(page);
    await page.goto(new URL("/__xui_acceptance__/away.html", baseURL).href);
    await expect(page.getByRole("heading", { name: "Navigation target" })).toBeVisible();
    const hidden = await page.evaluate(() => JSON.parse(sessionStorage.getItem("xui-pagehide")));
    await page.goBack({ waitUntil: "commit" });
    await expect(page.getByRole("textbox")).toBeVisible();
    const navigation = await page.evaluate(() => ({
        restored: window.restored === true,
        reasons: performance.getEntriesByType("navigation")[0]?.notRestoredReasons?.toJSON() ?? null
    }));
    await testInfo.attach("bfcache-navigation", { body: JSON.stringify({ hidden, ...navigation }), contentType: "application/json" });
    expect(hidden).toEqual({ persisted: true, nodes: 11 });
    expect(navigation.restored, JSON.stringify(navigation.reasons)).toBe(true);
    expect(await page.getByRole("textbox").evaluate(node => ({
        same: node === window.originalInput,
        tree: window.originalNodes.every((n, i) => document.querySelectorAll("#app .xui-node")[i] === n),
        value: node.value, start: node.selectionStart, end: node.selectionEnd
    }))).toEqual({ same: true, tree: true, value: "Retained", start: 1, end: 4 });
    await expect(byId(page, "count")).toHaveText("Count: 1");
    await page.getByRole("button", { name: "Increment", exact: true }).click();
    await expect(byId(page, "count")).toHaveText("Count: 2");
    await page.getByRole("textbox").press("Enter");
    await expect(byId(page, "greeting")).toHaveText("Hello, Retained!");
});
