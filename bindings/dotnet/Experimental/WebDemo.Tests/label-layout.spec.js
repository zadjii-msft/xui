import { test, expect, byId } from "./fixtures.js";

test("native label wrapping and ellipsis preserve source text and unrelated editing", async ({ page }) => {
    await page.setViewportSize({ width: 320, height: 800 });
    await page.goto("./?app=label-layout");
    const paragraph = byId(page, "label-layout-paragraph");
    const headline = byId(page, "label-layout-headline");
    await expect(paragraph).toBeVisible();
    const original = await paragraph.textContent();
    expect(original).toContain("Second paragraph:");
    await expect(paragraph).toHaveAccessibleName(original);
    const capped = await paragraph.boundingBox();
    const input = byId(page, "label-layout-draft").locator("input");
    await input.fill("Retained draft");
    await input.evaluate(node => { window.labelDraft = node; node.focus(); node.setSelectionRange(1, 6); });
    await byId(page, "label-layout-uncap").evaluate(node => node.click());
    await expect.poll(async () => (await paragraph.boundingBox()).height).toBeGreaterThan(capped.height);
    await expect(paragraph).toHaveText(original);
    expect(await input.evaluate(node => node === window.labelDraft && node === document.activeElement && node.selectionStart === 1 && node.selectionEnd === 6)).toBe(true);
    await byId(page, "label-layout-cap").evaluate(node => node.click());
    await expect.poll(async () => (await paragraph.boundingBox()).height).toBe(capped.height);
    expect(await headline.locator(".xui-label-body").evaluate(node => [getComputedStyle(node).whiteSpace, getComputedStyle(node).textOverflow])).toEqual(["pre", "ellipsis"]);
    await byId(page, "label-layout-clip").click();
    expect(await headline.locator(".xui-label-body").evaluate(node => getComputedStyle(node).textOverflow)).toBe("clip");
    await byId(page, "label-layout-inherit").click();
    expect(await headline.locator(".xui-label-body").evaluate(node => getComputedStyle(node).whiteSpace)).toBe("pre-wrap");
    await expect(paragraph).toHaveText(original);
});
