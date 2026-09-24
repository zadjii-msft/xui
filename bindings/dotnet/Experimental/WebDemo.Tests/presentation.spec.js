import { test, expect, byId } from "./fixtures.js";

test.beforeEach(async ({ page }) => {
    await page.goto("./?app=presentation");
    await expect(byId(page, "presentation-title")).toBeVisible();
});
test("typed rem typography honors root font and keeps native input focus selection and composition", async ({ page }) => {
    const input = byId(page, "presentation-draft").locator("input");
    await input.fill("Retained appearance draft");
    await input.evaluate(node => {
        window.presentationInput = node;
        node.focus();
        node.setSelectionRange(1, 8);
        node.dispatchEvent(new CompositionEvent("compositionstart", { bubbles: true }));
        window.appearanceWrites = 0;
        const property = Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, "value");
        Object.defineProperty(node, "value", {
            get() { return property.get.call(this); },
            set(value) { window.appearanceWrites++; property.set.call(this, value); }
        });
    });
    expect(await input.evaluate(node => getComputedStyle(node).fontSize)).toBe("14px");
    await byId(page, "presentation-larger").evaluate(node => node.click());
    await byId(page, "presentation-bold").evaluate(node => node.click());
    expect(await input.evaluate(node => [getComputedStyle(node).fontSize, getComputedStyle(node).fontWeight])).toEqual(["28px", "700"]);
    await page.evaluate(() => { document.documentElement.style.fontSize = "20px"; });
    expect(await input.evaluate(node => getComputedStyle(node).fontSize)).toBe("35px");
    expect(await input.evaluate(node => ({
        same: node === window.presentationInput, focused: document.activeElement === node,
        range: [node.selectionStart, node.selectionEnd], writes: window.appearanceWrites
    }))).toEqual({ same: true, focused: true, range: [1, 8], writes: 0 });
    await input.evaluate(node => node.dispatchEvent(new CompositionEvent("compositionend", { bubbles: true })));
    for (const id of ["presentation-toggle", "presentation-check"]) {
        expect(await byId(page, id).locator("label").evaluate(node => [getComputedStyle(node).fontSize, getComputedStyle(node).fontWeight]))
            .toEqual(["35px", "700"]);
    }
    await page.setViewportSize({ width: 320, height: 500 });
    await input.scrollIntoViewIfNeeded();
    expect(await input.evaluate(node => {
        const editor = node.getBoundingClientRect(), label = node.labels[0].getBoundingClientRect(), wrapper = node.parentElement.getBoundingClientRect();
        return label.bottom <= editor.top && editor.bottom <= wrapper.bottom;
    })).toBe(true);
});

test("scoped custom palettes follow system appearance and yield to forced colors without editor replacement", async ({ page }) => {
    const input = byId(page, "presentation-draft").locator("input");
    await input.fill("Palette draft");
    await input.evaluate(node => { window.paletteInput = node; });
    const body = await page.locator("body").evaluate(node => ({ color: getComputedStyle(node).color, background: getComputedStyle(node).backgroundColor }));
    await byId(page, "presentation-dark").click();
    await byId(page, "presentation-colors").click();
    expect(await page.locator("#app").evaluate(node => [getComputedStyle(node).color, getComputedStyle(node).backgroundColor, getComputedStyle(node).colorScheme]))
        .toEqual(["rgb(255, 255, 255)", "rgb(32, 32, 32)", "dark"]);
    expect(await page.locator("#app").evaluate(node => {
        const luminance = color => color.match(/\d+/g).slice(0, 3).map(Number).map(value => {
            const channel = value / 255;
            return channel <= 0.04045 ? channel / 12.92 : ((channel + 0.055) / 1.055) ** 2.4;
        }).reduce((sum, value, index) => sum + value * [0.2126, 0.7152, 0.0722][index], 0);
        const style = getComputedStyle(node);
        const values = [luminance(style.color), luminance(style.backgroundColor)].sort((a, b) => a - b);
        return (values[1] + 0.05) / (values[0] + 0.05);
    })).toBeGreaterThanOrEqual(7);
    expect(await byId(page, "presentation-toggle").locator("input").evaluate(node => getComputedStyle(node).accentColor)).toBe("rgb(96, 205, 255)");
    await byId(page, "presentation-system").click();
    await page.emulateMedia({ colorScheme: "light" });
    await expect.poll(() => page.locator("#app").evaluate(node => getComputedStyle(node).colorScheme)).toBe("light");
    await page.emulateMedia({ forcedColors: "active" });
    await expect.poll(() => page.locator("#app").evaluate(node => node.style.getPropertyValue("--xui-theme-accent"))).toBe("");
    expect(await byId(page, "presentation-toggle").locator("input").evaluate(node => getComputedStyle(node).accentColor)).toBe("auto");
    await page.emulateMedia({ forcedColors: "none" });
    await byId(page, "presentation-inherit").click();
    await expect(page.locator("#app")).not.toHaveClass(/xui-theme-surface/);
    expect(await page.locator("body").evaluate(node => ({ color: getComputedStyle(node).color, background: getComputedStyle(node).backgroundColor }))).toEqual(body);
    expect(await input.evaluate(node => node === window.paletteInput)).toBe(true);
    await expect(input).toHaveValue("Palette draft");
});
