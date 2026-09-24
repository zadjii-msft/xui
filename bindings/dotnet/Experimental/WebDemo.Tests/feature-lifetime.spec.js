import { test, expect, demo, command, byId } from "./fixtures.js";

test("native password clears before unmount, never persists in the model, and setters stay silent", async ({ page }) => {
    await demo(page);
    await command(page, "feature-start", "forms");
    expect(await command(page, "feature-secret-seed", "8")).toEqual({ length: 8, notices: 0 });
    const input = byId(page, "forms-password").locator("input");
    await input.evaluate(node => {
        window.retiredSecret = node;
        const mount = document.querySelector("#app");
        const replace = mount.replaceChildren;
        mount.replaceChildren = function (...children) {
            window.secretLengthAtUnmount = node.value.length;
            return replace.apply(this, children);
        };
    });
    await command(page, "feature-detach");
    expect(await page.evaluate(() => [window.secretLengthAtUnmount, window.retiredSecret.value.length])).toEqual([0, 0]);
    await expect(command(page, "feature-secret-length")).rejects.toThrow(/attached host/);
    await command(page, "feature-attach");
    expect(await command(page, "feature-secret-length")).toEqual({ length: 0 });
    expect(await input.evaluate(node => node !== window.retiredSecret)).toBe(true);
    await input.evaluate(node => { node.value = "q".repeat(9); });
    expect(await command(page, "feature-secret-read")).toEqual({ length: 9 });
    await expect(command(page, "feature-secret-seed", "33")).rejects.toThrow(/length limit/);
    expect(await command(page, "feature-secret-length")).toEqual({ length: 9 });
    await input.evaluate(node => { node.value = "\ud800"; });
    await expect(command(page, "feature-secret-read")).rejects.toThrow(/UTF-16/);
    await command(page, "feature-secret-seed", "32");
    expect(await command(page, "feature-secret-length")).toEqual({ length: 32 });
    expect(await command(page, "feature-notes-set", "first\r\nsecond\rthird"))
        .toEqual({ text: "first\nsecond\nthird", authored: "" });
    await expect(byId(page, "forms-notes").locator("textarea")).toHaveValue("first\nsecond\nthird");
    await expect(command(page, "feature-notes-set", "x".repeat(65))).rejects.toThrow(/length limit/);
    await expect(byId(page, "forms-notes").locator("textarea")).toHaveValue("first\nsecond\nthird");
    await command(page, "feature-dispose");
});

test("clearing typography on all five targets restores inherited fonts and theme listeners retire", async ({ page }) => {
    await page.addInitScript(() => {
        window.presentationListeners = 0;
        const match = window.matchMedia.bind(window);
        window.matchMedia = query => {
            const media = match(query);
            if (query === "(prefers-color-scheme: dark)" || query === "(forced-colors: active)") {
                const add = media.addEventListener.bind(media), remove = media.removeEventListener.bind(media);
                media.addEventListener = (...args) => { window.presentationListeners++; return add(...args); };
                media.removeEventListener = (...args) => { window.presentationListeners--; return remove(...args); };
            }
            return media;
        };
    });
    await demo(page);
    await command(page, "feature-start", "presentation");
    const inherited = await page.locator("body").evaluate(node => [getComputedStyle(node).fontFamily, getComputedStyle(node).fontSize, getComputedStyle(node).fontWeight]);
    expect(await command(page, "feature-clear-typography")).toEqual({ cleared: 5 });
    for (const id of ["presentation-title", "presentation-draft", "presentation-toggle", "presentation-check", "presentation-bold"]) {
        expect(await byId(page, id).evaluate(node => [getComputedStyle(node).fontFamily, getComputedStyle(node).fontSize, getComputedStyle(node).fontWeight])).toEqual(inherited);
    }
    await byId(page, "presentation-system").click();
    expect(await page.evaluate(() => window.presentationListeners)).toBe(2);
    await byId(page, "presentation-inherit").click();
    expect(await page.evaluate(() => window.presentationListeners)).toBe(0);
    await byId(page, "presentation-dark").click();
    await command(page, "feature-dispose");
    expect(await page.evaluate(() => window.presentationListeners)).toBe(0);
});

test("retiring a keyed password subtree clears its native value before removal and never resurrects it", async ({ page }) => {
    await demo(page);
    await command(page, "feature-start", "secret-rows");
    await command(page, "feature-secret-seed", "8");
    await page.locator('input[type="password"]').evaluate(node => {
        window.retiredPassword = node;
        window.passwordRemovalLengths = [];
        const remove = Element.prototype.remove;
        Element.prototype.remove = function () {
            if (this === node || this.contains(node)) window.passwordRemovalLengths.push(node.value.length);
            return remove.call(this);
        };
    });
    await command(page, "feature-secret-remove");
    expect(await page.evaluate(() => window.passwordRemovalLengths.length)).toBeGreaterThan(0);
    expect(await page.evaluate(() => window.passwordRemovalLengths.every(length => length === 0))).toBe(true);
    expect(await page.evaluate(() => window.retiredPassword.value.length)).toBe(0);
    await command(page, "feature-secret-add");
    expect(await command(page, "feature-secret-length")).toEqual({ length: 0 });
    expect(await page.locator('input[type="password"]').evaluate(node => node !== window.retiredPassword)).toBe(true);
    await command(page, "feature-dispose");
});
