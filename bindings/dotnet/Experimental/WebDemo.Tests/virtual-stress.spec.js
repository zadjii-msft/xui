import { test, expect, demo, command } from "./fixtures.js";
import { virtualEvidence } from "./virtual-evidence.js";

test("one hundred actual C# attachments retire native listeners observers and viewport callbacks", async ({ page, browser }, testInfo) => {
    test.setTimeout(180000);
    await page.addInitScript(() => {
        const registrations = new WeakMap();
        const add = EventTarget.prototype.addEventListener, remove = EventTarget.prototype.removeEventListener;
        const capture = options => typeof options === "boolean" ? options : !!options?.capture;
        EventTarget.prototype.addEventListener = function (type, callback, options) {
            const list = registrations.get(this) ?? [];
            if (callback && !list.some(item => item.type === type && item.callback === callback && item.capture === capture(options)))
                list.push({ type, callback, capture: capture(options) });
            registrations.set(this, list);
            return add.call(this, type, callback, options);
        };
        EventTarget.prototype.removeEventListener = function (type, callback, options) {
            const list = registrations.get(this);
            const index = list?.findIndex(item => item.type === type && item.callback === callback && item.capture === capture(options)) ?? -1;
            if (index >= 0) list.splice(index, 1);
            return remove.call(this, type, callback, options);
        };
        window.listenerCount = target => (registrations.get(target) ?? []).length;
        window.mediaQueries = [];
        const match = window.matchMedia.bind(window);
        window.matchMedia = query => { const media = match(query); window.mediaQueries.push(media); return media; };
        window.observers = [];
        for (const type of ["ResizeObserver", "MutationObserver"]) {
            const Native = window[type];
            window[type] = class extends Native {
                constructor(callback) { super(callback); this.targets = new Set(); window.observers.push(this); }
                observe(target, options) { this.targets.add(target); return super.observe(target, options); }
                disconnect() { this.targets.clear(); return super.disconnect(); }
            };
        }
    });
    await demo(page);
    const baseline = await page.evaluate(() => [listenerCount(document), listenerCount(window), listenerCount(document.fonts)]);
    const baselineObservers = await page.evaluate(() => observers.reduce((total, observer) => total + observer.targets.size, 0));
    const baselineMedia = await page.evaluate(() => mediaQueries.reduce((total, media) => total + listenerCount(media), 0));
    await command(page, "virtual-start");
    await expect(page.locator('[data-xui-id="virtual-last"]')).toBeEnabled();
    await page.getByRole("textbox", { name: "Task 1", exact: true }).fill("Retained for 100 attachments");
    const active = await page.evaluate(() => [listenerCount(document), listenerCount(window), listenerCount(document.fonts)]);
    const start = performance.now();
    let peakRetiredInputListeners = 0;
    for (let cycle = 0; cycle < 100; cycle++) {
        await page.evaluate(() => { window.retiredInputs = [...document.querySelectorAll("#app input")]; });
        await command(page, "virtual-cycle");
        await expect(page.locator('[data-xui-id="virtual-last"]')).toBeEnabled();
        await expect.poll(() => page.locator(".xui-virtual-clip").evaluate(node => node.getBoundingClientRect().height)).toBeGreaterThan(0);
        expect(await page.evaluate(() => retiredInputs.every(node => !node.isConnected && listenerCount(node) === 0))).toBe(true);
        peakRetiredInputListeners = Math.max(peakRetiredInputListeners,
            await page.evaluate(() => Math.max(0, ...retiredInputs.map(listenerCount))));
        expect(await page.evaluate(() => [listenerCount(document), listenerCount(window), listenerCount(document.fonts)])).toEqual(active);
        await expect(page.getByRole("textbox", { name: "Task 1", exact: true })).toHaveValue("Retained for 100 attachments");
        expect((await command(page, "virtual-state")).error).toBeNull();
        expect((await command(page, "virtual-state")).emptyAttachments).toBe(cycle + 2);
        if ((cycle + 1) % 25 === 0) console.log(`Completed ${cycle + 1} empty-before-native-attach cycles`);
    }
    const emptyBeforeNativeAttachments = (await command(page, "virtual-state")).emptyAttachments;
    await command(page, "virtual-dispose");
    await expect(page.locator("#app .xui-node")).toHaveCount(0);
    const finalListeners = await page.evaluate(() => [listenerCount(document), listenerCount(window), listenerCount(document.fonts)]);
    const finalObservers = await page.evaluate(() => observers.reduce((total, observer) => total + observer.targets.size, 0));
    const finalMedia = await page.evaluate(() => mediaQueries.reduce((total, media) => total + listenerCount(media), 0));
    expect(finalListeners).toEqual(baseline);
    expect(finalObservers).toBe(baselineObservers);
    expect(finalMedia).toBe(baselineMedia);
    await virtualEvidence("virtual-attachment-resources",
        { cycles: 100, seconds: (performance.now() - start) / 1000, browser: browser.version(),
            emptyBeforeNativeAttachments, peakRetiredInputListeners, finalListeners,
            baselineListeners: baseline, activeListeners: active, baselineObservers, finalObservers,
            baselineMedia, finalMedia }, testInfo);
});
