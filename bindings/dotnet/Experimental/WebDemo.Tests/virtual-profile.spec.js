import { test, expect, demo, command } from "./fixtures.js";
import { virtualEvidence } from "./virtual-evidence.js";

test("diagnose native viewport phases without treating instrumentation as a performance pass", async ({ page }, testInfo) => {
    test.setTimeout(180000);
    await page.setViewportSize({ width: 600, height: 700 });
    await demo(page);
    await command(page, "virtual-start");
    await expect(page.locator('[data-xui-id="virtual-last"]')).toBeEnabled();
    try {
        const result = await command(page, "virtual-profile");
        await virtualEvidence("native-viewport-phases", result, testInfo);
        expect(result.phases).toHaveLength(110);
    } finally { await command(page, "virtual-dispose"); }
});
