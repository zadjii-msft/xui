import { readFileSync } from "node:fs";
import { test, expect, demo, command } from "./fixtures.js";

test("actual C# Wasm solvers match every shared literal axis stack track and arrangement case", async ({ page }) => {
    const axes = JSON.parse(readFileSync(new URL("../Xui.Portable.Tests/Fixtures/AxisLayoutScenarios.json", import.meta.url), "utf8"));
    const grid = JSON.parse(readFileSync(new URL("../Xui.Portable.Tests/Fixtures/GridLayoutScenarios.json", import.meta.url), "utf8"));
    expect(axes.version).toBe(1);
    expect(grid.version).toBe(1);
    await demo(page);
    const result = await command(page, "layout-math", JSON.stringify({ ...axes, ...grid }));
    expect(result.axes.map(({ offerMode, offerSize, value }) => ({ offerMode, offerSize, value })))
        .toEqual(axes.axes.map(item => ({ offerMode: item.offerMode, offerSize: item.offerSize, value: item.expected })));
    axes.axes.forEach((item, index) => {
        if (Object.hasOwn(item, "expectedUnbounded")) expect(result.axes[index].unbounded).toBe(item.expectedUnbounded);
    });
    expect(result.stacks).toEqual(axes.stacks.map(item => ({ extent: item.extent, slots: item.slots })));
    expect(result.tracks).toEqual(grid.tracks.map(item => item.expected));
    expect(result.arrangements).toEqual(grid.arrangements.map(item => ({
        rows: item.expectedRows, cells: item.expectedCells, contexts: item.expectedContexts, nested: item.expectedNestedSlots ?? null
    })));
});

test("live layout opt-in and removal preserve editor identity and disconnect all layout observers", async ({ page }) => {
    await page.addInitScript(() => {
        window.layoutObservers = [];
        window.resolutionListeners = 0;
        const match = window.matchMedia.bind(window);
        window.matchMedia = query => {
            const media = match(query);
            if (query.startsWith("(resolution:")) {
                const add = media.addEventListener.bind(media), remove = media.removeEventListener.bind(media);
                media.addEventListener = (...args) => { window.resolutionListeners++; return add(...args); };
                media.removeEventListener = (...args) => { window.resolutionListeners--; return remove(...args); };
            }
            return media;
        };
        for (const name of ["ResizeObserver", "MutationObserver"]) {
            const Native = window[name];
            window[name] = class extends Native {
                constructor(callback) {
                    super(callback);
                    this.targets = new Set();
                    window.layoutObservers.push(this);
                }
                observe(target, options) { this.targets.add(target); return super.observe(target, options); }
                disconnect() { this.targets.clear(); return super.disconnect(); }
            };
        }
    });
    await demo(page);
    const input = page.getByRole("textbox");
    await input.fill("Same editor");
    await input.evaluate(node => { window.retained = node; node.focus(); node.setSelectionRange(1, 4); });
    const baseline = await page.evaluate(() => window.layoutObservers.reduce((n, observer) => n + observer.targets.size, 0));
    await command(page, "layout-on");
    await expect(page.locator("#app .xui-arranged").first()).toBeVisible();
    expect(await input.evaluate(node => node === window.retained && node.selectionStart === 1 && node.selectionEnd === 4)).toBe(true);
    expect(await page.evaluate(() => window.layoutObservers.reduce((n, observer) => n + observer.targets.size, 0))).toBeGreaterThan(baseline);
    expect(await page.evaluate(() => window.resolutionListeners)).toBe(1);
    await command(page, "layout-off");
    await expect(page.locator("#app .xui-arranged")).toHaveCount(0);
    expect(await page.evaluate(() => window.layoutObservers.reduce((n, observer) => n + observer.targets.size, 0))).toBe(baseline);
    expect(await page.evaluate(() => window.resolutionListeners)).toBe(0);
    await command(page, "layout-on");
    await command(page, "dispose");
    await expect(page.locator("#app .xui-node")).toHaveCount(0);
    expect(await page.evaluate(() => window.layoutObservers.reduce((n, observer) => n + observer.targets.size, 0))).toBe(baseline);
    expect(await page.evaluate(() => window.resolutionListeners)).toBe(0);
});
