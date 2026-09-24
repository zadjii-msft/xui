import { test, expect, demo, command } from "./fixtures.js";
import { virtualEvidence } from "./virtual-evidence.js";

test("warm native viewport commits meet the initial bounded performance budget", async ({ page, browser }, testInfo) => {
    test.setTimeout(180000);
    await page.setViewportSize({ width: 600, height: 700 });
    await demo(page);
    await command(page, "virtual-start");
    await expect(page.locator('[data-xui-id="virtual-last"]')).toBeEnabled();
    try {
        const result = await command(page, "virtual-performance");
        await virtualEvidence("native-viewport-performance",
            { ...result, browser: browser.version(), viewport: page.viewportSize(), deviceScaleFactor: 1, warmups: 10 }, testInfo);
        expect(result.samplesMilliseconds).toHaveLength(100);
        console.log(`Native viewport ms p50=${result.p50Milliseconds} p95=${result.p95Milliseconds} max=${result.maximumMilliseconds}`);
        expect(result.p95Milliseconds).toBeLessThanOrEqual(50);
        expect(result.meetsInitialBudget).toBe(true);
    } finally { await command(page, "virtual-dispose"); }
});
