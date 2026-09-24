import { test, expect, byId } from "./fixtures.js";
import { observeNavigation } from "./navigation.js";

test("browser toolbar navigation preserves actual Studio retained editors through bfcache @bfcache", async ({ page, baseURL, browserName, browser }, testInfo) => {
    await page.setViewportSize({ width: 1400, height: 900 });
    await page.goto("./?app=studio");
    const input = byId(page, "doc-00001-body").locator("textarea");
    await expect(input).toBeVisible();
    await input.fill("Unsaved browser workspace draft");
    await input.evaluate(node => { window.cachedStudioEditor = node; node.setSelectionRange(2, 9); });
    await observeNavigation(page);
    await page.goto(new URL("/__xui_acceptance__/away.html", baseURL).href);
    const hidden = await page.evaluate(() => JSON.parse(sessionStorage.getItem("xui-pagehide")));
    await page.goBack({ waitUntil: "commit" });
    await expect(input).toBeVisible();
    const restored = await input.evaluate(node => ({
        same: node === window.cachedStudioEditor, restored: window.restored === true,
        selection: [node.selectionStart, node.selectionEnd], value: node.value,
        reasons: performance.getEntriesByType("navigation")[0]?.notRestoredReasons?.toJSON() ?? null
    }));
    await testInfo.attach("studio-bfcache", {
        body: JSON.stringify({ browserName, browserVersion: browser.version(), hidden, restored }),
        contentType: "application/json"
    });
    expect(hidden.persisted).toBe(true);
    expect(restored, JSON.stringify(restored.reasons)).toMatchObject({ same: true, restored: true, selection: [2, 9] });
    await expect(input).toHaveValue("Unsaved browser workspace draft");
    await byId(page, "studio-tabs").locator('[id$="-tab-2"]').click();
    await byId(page, "studio-tabs").locator('[id$="-tab-1"]').click();
    await expect(input).toHaveValue("Unsaved browser workspace draft");
    expect(await input.evaluate(node => node === window.cachedStudioEditor)).toBe(true);
});

test("Studio-owned Back protects composition and a full reload does not invent persistence", async ({ page }) => {
    await page.setViewportSize({ width: 390, height: 844 });
    await page.goto("./?app=studio");
    await expect(page.locator("#app")).toHaveAttribute("data-xui-studio-host-ready", "true");
    const editor = byId(page, "doc-00001-body").locator("textarea");
    await editor.fill("Local incomplete draft");
    await editor.evaluate(node => node.dispatchEvent(new CompositionEvent("compositionstart", { bubbles: true })));
    await page.keyboard.press("Alt+ArrowLeft");
    await expect(page.locator("#app")).toHaveAttribute("data-xui-back-blocked", "true");
    await expect(editor).toHaveValue("Local incomplete draft");
    await expect(editor).toBeFocused();
    await editor.evaluate(node => node.dispatchEvent(new CompositionEvent("compositionend", { bubbles: true })));
    await byId(page, "studio-theme").focus();
    await page.keyboard.press("Alt+ArrowLeft");
    await expect(page.locator("#app")).toHaveAttribute("data-xui-back-handled", "true");
    await expect(byId(page, "studio-layout")).toHaveText("Compact workspace / Catalog");
    await page.reload();
    await expect(byId(page, "doc-00001-title").locator("input")).toHaveValue("Keyboard navigation 00001");
    await expect(byId(page, "doc-00001-body").locator("textarea")).not.toHaveValue("Local incomplete draft");
});
