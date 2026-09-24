import { test, expect, demo, command, byId } from "./fixtures.js";

test("public C# Reveal presentation observes the native clock and retirement cancels all owned animation resources", async ({ page }) => {
    await page.addInitScript(() => {
        window.motionListeners = 0;
        const match = matchMedia.bind(window);
        window.matchMedia = query => {
            const media = match(query);
            if (query === "(prefers-reduced-motion: reduce)" || query === "(forced-colors: active)") {
                const add = media.addEventListener.bind(media), remove = media.removeEventListener.bind(media);
                media.addEventListener = (...args) => { window.motionListeners++; return add(...args); };
                media.removeEventListener = (...args) => { window.motionListeners--; return remove(...args); };
            }
            return media;
        };
        window.motionFrames = new Set();
        const request = requestAnimationFrame.bind(window), cancel = cancelAnimationFrame.bind(window);
        window.requestAnimationFrame = callback => {
            const id = request(time => { window.motionFrames.delete(id); callback(time); });
            window.motionFrames.add(id);
            return id;
        };
        window.cancelAnimationFrame = id => { window.motionFrames.delete(id); cancel(id); };
    });
    await demo(page);
    const baseline = await page.evaluate(() => window.motionListeners);
    await command(page, "feature-start", "reveal");
    expect(await command(page, "feature-reveal-state")).toEqual({ progress: 0, animating: false });
    await byId(page, "reveal-toggle").click();
    await expect.poll(() => command(page, "feature-reveal-state")).toEqual({ progress: 1, animating: false });
    await page.evaluate(() => new Promise(requestAnimationFrame));
    expect(await page.evaluate(() => window.motionFrames.size)).toBe(0);
    await page.evaluate(() => {
        document.querySelector('[data-xui-id="reveal-toggle"]').click();
        window.motionBeforeRetire = document.querySelector('[data-xui-kind="Reveal"]');
    });
    await command(page, "feature-dispose");
    expect(await page.evaluate(() => window.motionListeners)).toBe(baseline);
    expect(await page.evaluate(() => window.motionFrames.size)).toBe(0);
    await expect(page.locator("#app .xui-node")).toHaveCount(0);
});
