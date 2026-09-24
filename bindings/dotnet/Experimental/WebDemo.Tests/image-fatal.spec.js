import { test, expect, byId } from "./fixtures.js";

test("injected post-Ready native image error causes real Host detachment and resource retirement", async ({ page, diagnostics }) => {
    await page.goto("./?app=image");
    await byId(page, "image-load").click();
    await expect(byId(page, "image-status")).toHaveText("Ready: 64x64");
    const image = byId(page, "packaged-image").locator("img");
    await image.evaluate(node => { window.retiredNativeImage = node; });
    diagnostics.expected.push(/native image presentation failed/);
    await image.evaluate(node => node.dispatchEvent(new Event("error")));
    await expect(page.locator("#app .xui-node")).toHaveCount(0);
    const state = await page.evaluate(() => DotNet.invokeMethodAsync("WebGalleryDemo", "ImageState"));
    expect(state).toEqual({ attached: false, resources: { decodes: 0, sourceBytes: 0, outputBytes: 0, encodedBytes: 0 } });
    expect(await page.evaluate(() => window.retiredNativeImage.hasAttribute("src"))).toBe(false);
    await page.evaluate(() => window.retiredNativeImage.dispatchEvent(new Event("error")));
    expect(await page.evaluate(() => DotNet.invokeMethodAsync("WebGalleryDemo", "ImageState"))).toEqual(state);
});
