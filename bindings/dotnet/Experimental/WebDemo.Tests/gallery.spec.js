import { test, expect, galleryApplications, runGallerySteps } from "./gallery-fixtures.js";

for (const application of galleryApplications) {
    for (const scenario of application.scenarios) {
        test(`${application.id}: ${scenario.name}`, async ({ page }) => {
            await page.goto(`./?app=${application.route ?? application.id}`);
            await expect(page.locator("#app input").first()).toBeVisible();
            expect(await page.evaluate(() => typeof window.xuiTest)).toBe("undefined");
            await page.context().setOffline(true);
            await runGallerySteps(page, scenario.steps);
        });
    }
}

test("unknown gallery selection reports a visible startup error", async ({ page, diagnostics }) => {
    diagnostics.expected.push(/XUI browser error:.*Unknown gallery application/s, /Unknown gallery application/);
    await page.goto("./?app=missing");
    await expect(page.getByRole("alert")).toContainText("Unknown gallery application: missing");
    await expect(page.locator("#app .xui-node")).toHaveCount(0);
});
