import { test, expect, byId } from "./fixtures.js";
import { observeNavigation } from "./navigation.js";

test("actual bfcache retains the native lease committed viewport and sparse edited row @bfcache", async ({ page, baseURL }, testInfo) => {
    await page.setViewportSize({ width: 600, height: 700 });
    await page.goto("./?app=virtual-list");
    await expect(byId(page, "virtual-last")).toBeEnabled();
    await byId(page, "virtual-last").click();
    const last = page.getByRole("textbox", { name: "Task 10,000", exact: true });
    await expect(last).toBeFocused();
    await last.fill("Cached virtual task");
    await last.evaluate(node => { node.setSelectionRange(2, 7); window.cachedVirtualInput = node; });
    const offset = await page.locator(".xui-virtual-viewport").getAttribute("data-xui-committed-offset");
    await observeNavigation(page);
    await page.goto(new URL("/__xui_acceptance__/away.html", baseURL).href);
    const hidden = await page.evaluate(() => JSON.parse(sessionStorage.getItem("xui-pagehide")));
    await page.goBack({ waitUntil: "commit" });
    await expect(last).toHaveValue("Cached virtual task");
    const restored = await page.evaluate(() => ({
        persisted: window.restored === true,
        sameInput: document.querySelector('input[aria-label="Task 10,000"]') === window.cachedVirtualInput,
        reasons: performance.getEntriesByType("navigation")[0]?.notRestoredReasons?.toJSON() ?? null
    }));
    await testInfo.attach("virtual-bfcache", { body: JSON.stringify({ hidden, restored }), contentType: "application/json" });
    expect(hidden.persisted).toBe(true);
    expect(restored, JSON.stringify(restored.reasons)).toMatchObject({ persisted: true, sameInput: true });
    expect(await last.evaluate(node => [node.selectionStart, node.selectionEnd])).toEqual([2, 7]);
    await expect(page.locator(".xui-virtual-viewport")).toHaveAttribute("data-xui-committed-offset", offset);
    await byId(page, "virtual-first").click();
    await expect(page.getByRole("textbox", { name: "Task 1", exact: true })).toBeFocused();
});
