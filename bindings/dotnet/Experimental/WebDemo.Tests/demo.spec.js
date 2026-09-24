import { test, expect, command, demo, byId } from "./fixtures.js";

test("the shared generated C# runs locally through Wasm with a real DOM tree", async ({ page }) => {
    const requests = [];
    page.on("request", request => requests.push(request.url()));
    await demo(page);
    expect(requests.some(url => /dotnet.*\.wasm/.test(url))).toBe(true);
    expect(requests.some(url => /\/xui\.dll(?:$|\?)/i.test(url))).toBe(false);
    await expect(page.locator("#app .xui-node")).toHaveCount(11);
    await expect(page.locator("canvas")).toHaveCount(0);
    await expect(page.getByRole("region", { name: "Greeting and counter" })).toBeVisible();
    await expect(page.getByLabel("Your name", { exact: true })).toHaveAttribute("placeholder", "Ada");
    await page.getByRole("button", { name: "Increment", exact: true }).click();
    await expect(byId(page, "count")).toHaveText("Count: 1");
    expect((await command(page, "state")).count).toBe(1);
    // After startup, authored handlers need no server connection.
    await page.context().setOffline(true);
    await page.getByRole("textbox", { name: "Your name" }).fill("Ada");
    await page.getByRole("textbox", { name: "Your name" }).press("Enter");
    await expect(byId(page, "greeting")).toHaveText("Hello, Ada!");
    expect((await command(page, "state")).entry).toBe("Ada");
    await page.getByRole("button", { name: "Reset", exact: true }).click();
    await expect(byId(page, "count")).toHaveText("Count: 0");
    await expect(page.getByRole("textbox")).toHaveValue("");
    await expect(byId(page, "greeting")).toHaveText("Type a name, then submit.");
});

test("updates retain input identity, caret, selection, composition, and authored order", async ({ page }) => {
    await demo(page);
    const input = page.getByRole("textbox");
    await input.fill("Grace");
    await expect.poll(async () => (await command(page, "state")).entry).toBe("Grace");
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
    });
    await command(page, "count", "4");
    expect(await input.evaluate(node => ({
        same: node === window.originalInput,
        tree: window.originalNodes.every((n, i) => document.querySelectorAll("#app .xui-node")[i] === n),
        start: node.selectionStart, end: node.selectionEnd, focused: node === document.activeElement,
        writes: window.valueWrites
    }))).toEqual({ same: true, tree: true, start: 1, end: 4, focused: true, writes: 0 });
    await input.press("Z");
    await expect.poll(async () => (await command(page, "state")).entry).toBe("GZe");
    expect(await input.evaluate(node => [node.selectionStart, window.valueWrites])).toEqual([2, 0]);
    await input.evaluate(node => {
        node.dispatchEvent(new CompositionEvent("compositionstart", { bubbles: true }));
        node.dispatchEvent(new KeyboardEvent("keydown", { key: "Enter", isComposing: true, bubbles: true }));
        node.dispatchEvent(new CompositionEvent("compositionend", { bubbles: true }));
    });
    await command(page, "count", "5");
    await expect(byId(page, "greeting")).toHaveText("Type a name, then submit.");
    expect(await byId(page, "increment").evaluate(node =>
        [...node.parentElement.children].map(child => child.dataset.xuiId))).toEqual(["increment", "submit", "reset"]);
});

test("native keyboard, accessibility, enabled ancestors, visibility, and silent setters", async ({ page }) => {
    await demo(page);
    const input = page.getByRole("textbox", { name: "Your name" });
    const increment = page.getByRole("button", { name: "Increment", exact: true });
    await increment.focus();
    await increment.press("Space");
    await expect(byId(page, "count")).toHaveText("Count: 1");
    await increment.press("Enter");
    await expect(byId(page, "count")).toHaveText("Count: 2");
    await command(page, "count", "10");
    await expect(increment).toBeDisabled();
    await increment.evaluate(node => node.click());
    expect((await command(page, "state")).count).toBe(10);
    await command(page, "entry", "<b>Local C#</b>");
    await expect(input).toHaveValue("<b>Local C#</b>");
    await expect(byId(page, "greeting")).toHaveText("Type a name, then submit.");
    await page.getByRole("button", { name: "Submit", exact: true }).click();
    await expect(byId(page, "greeting")).toHaveText("Hello, <b>Local C#</b>!");
    await expect(byId(page, "greeting").locator("b")).toHaveCount(0);
    await command(page, "help", "Enter your name");
    await expect(input).toHaveAttribute("aria-description", "Enter your name");
    expect(await input.evaluate(node => [...node.labels].map(label => label.textContent))).toEqual(["Your name"]);
    await command(page, "caption", "false");
    await expect(input).toHaveAccessibleName("Your name");
    await expect(page.locator("label")).toBeHidden();
    await command(page, "visible", "false");
    await expect(input).toBeHidden();
    expect(await byId(page, "name").evaluate(node => node.getBoundingClientRect().height)).toBe(0);
    await command(page, "visible", "true");
    await command(page, "count", "0");
    await command(page, "enabled", "false");
    expect(await byId(page, "content").evaluate(node => node.inert)).toBe(true);
    await increment.evaluate(node => node.click());
    expect((await command(page, "state")).count).toBe(0);
    await command(page, "enabled", "true");
    await command(page, "dispatch");
    await expect(byId(page, "count")).toHaveText("Count: 1");
});

test("detach, remount, stale callbacks, and final disposal retain no active old listeners", async ({ page }) => {
    await demo(page);
    await command(page, "entry", "Retained");
    await command(page, "count", "3");
    await page.evaluate(() => {
        window.oldInput = document.querySelector("input");
        window.oldIncrement = document.querySelector('[data-xui-id="increment"]');
    });
    await command(page, "detach");
    await expect(page.locator("#app .xui-node")).toHaveCount(0);
    await page.evaluate(() => {
        window.oldIncrement.click();
        window.oldInput.value = "stale";
        window.oldInput.dispatchEvent(new Event("input", { bubbles: true }));
    });
    await command(page, "attach");
    await expect(page.getByRole("textbox")).toHaveValue("Retained");
    expect(await page.getByRole("textbox").evaluate(node => node !== window.oldInput)).toBe(true);
    await page.evaluate(() => window.oldIncrement.click());
    expect((await command(page, "state")).count).toBe(3);
    await page.getByRole("button", { name: "Increment", exact: true }).click();
    await expect(byId(page, "count")).toHaveText("Count: 4");
    await command(page, "dispose");
    await expect(page.locator("#app .xui-node")).toHaveCount(0);
    await page.evaluate(() => window.oldIncrement.click());
});

test("authored callback failures are visible and the event queue recovers", async ({ page, diagnostics }) => {
    await demo(page);
    diagnostics.expected.push(/XUI browser error:.*Intentional browser callback failure/s);
    await command(page, "throw");
    await page.getByRole("textbox").press("Enter");
    await expect(page.getByRole("alert")).toContainText("Intentional browser callback failure");
    await command(page, "unthrow");
    await page.getByRole("textbox").fill("Recovered");
    await page.getByRole("textbox").press("Enter");
    await expect(byId(page, "greeting")).toHaveText("Hello, Recovered!");
});

test("25 attachment cycles remove every old native listener and preserve only current callbacks", async ({ page }) => {
    await page.addInitScript(() => {
        const listeners = new WeakMap();
        const add = EventTarget.prototype.addEventListener;
        const remove = EventTarget.prototype.removeEventListener;
        EventTarget.prototype.addEventListener = function (type, callback, options) {
            if (this instanceof HTMLInputElement || this instanceof HTMLButtonElement) {
                const current = listeners.get(this) ?? [];
                current.push({ type, callback });
                listeners.set(this, current);
            }
            return add.call(this, type, callback, options);
        };
        EventTarget.prototype.removeEventListener = function (type, callback, options) {
            const current = listeners.get(this);
            if (current) {
                const index = current.findIndex(entry => entry.type === type && entry.callback === callback);
                if (index >= 0) current.splice(index, 1);
            }
            return remove.call(this, type, callback, options);
        };
        window.nativeListenerCount = node => (listeners.get(node) ?? []).length;
    });
    await demo(page);
    await command(page, "entry", "Retained through 25 attachments");
    for (let cycle = 0; cycle < 25; cycle++) {
        await command(page, "count", "0");
        await page.evaluate(() => { window.retiredControls = [...document.querySelectorAll("#app input, #app button")]; });
        await command(page, "detach");
        expect(await page.evaluate(() => window.retiredControls.map(node => window.nativeListenerCount(node)))).toEqual([0, 0, 0, 0]);
        await expect(page.locator("#app .xui-node")).toHaveCount(0);
        await command(page, "attach");
        await page.evaluate(() => {
            for (const node of window.retiredControls) {
                if (node instanceof HTMLInputElement) {
                    node.value = "stale";
                    node.dispatchEvent(new Event("input"));
                    node.dispatchEvent(new KeyboardEvent("keydown", { key: "Enter" }));
                } else node.click();
            }
        });
        await expect(page.getByRole("textbox")).toHaveValue("Retained through 25 attachments");
        expect((await command(page, "state")).count).toBe(0);
        await byId(page, "increment").click();
        await expect(byId(page, "count")).toHaveText("Count: 1");
        expect(await page.evaluate(() => window.retiredControls.every(node => !node.isConnected))).toBe(true);
    }
    await page.evaluate(() => { window.retiredControls = [...document.querySelectorAll("#app input, #app button")]; });
    await command(page, "dispose");
    expect(await page.evaluate(() => window.retiredControls.map(node => window.nativeListenerCount(node)))).toEqual([0, 0, 0, 0]);
    await expect(page.locator("#app .xui-node")).toHaveCount(0);
});

test("pagehide disposes references, while BFCache entry retains the live application", async ({ page }) => {
    await demo(page);
    await page.evaluate(() => window.dispatchEvent(new PageTransitionEvent("pagehide", { persisted: true })));
    await page.getByRole("button", { name: "Increment", exact: true }).click();
    await expect(byId(page, "count")).toHaveText("Count: 1");
    await page.evaluate(() => window.dispatchEvent(new PageTransitionEvent("pagehide", { persisted: false })));
    await expect(page.locator("#app .xui-node")).toHaveCount(0);
    expect(await page.evaluate(() => typeof window.xuiTest)).toBe("undefined");
    await page.evaluate(() => window.dispatchEvent(new PageTransitionEvent("pagehide", { persisted: false })));
});

test("browser layout bounds the root and scroll viewport on a small screen", async ({ page }) => {
    await page.setViewportSize({ width: 240, height: 200 });
    await demo(page);
    const geometry = await page.evaluate(() => {
        const root = document.querySelector(".xui-root");
        const scroll = document.querySelector(".xui-scrollview");
        const input = document.querySelector(".xui-textinput");
        return {
            rootWidth: root.getBoundingClientRect().width,
            rootHeight: root.getBoundingClientRect().height,
            scrollWidth: scroll.clientWidth, scrollHeight: scroll.clientHeight,
            contentHeight: scroll.scrollHeight,
            inputWidth: input.getBoundingClientRect().width,
            gap: getComputedStyle(root).gap,
            padding: getComputedStyle(root).padding,
            overflow: getComputedStyle(scroll).overflowY
        };
    });
    expect(geometry.rootWidth).toBe(240);
    expect(geometry.rootHeight).toBe(200);
    expect(geometry.inputWidth).toBeLessThanOrEqual(geometry.scrollWidth);
    expect(geometry.contentHeight).toBeGreaterThan(geometry.scrollHeight);
    expect(geometry).toMatchObject({ gap: "12px", padding: "16px", overflow: "auto" });
});
