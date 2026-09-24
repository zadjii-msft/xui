import { test, expect, byId } from "./fixtures.js";
import { observeNavigation } from "./navigation.js";

test.beforeEach(async ({ page }) => {
    await page.goto("./?app=dynamic-tasks");
    await expect(byId(page, "dynamic-summary")).toHaveText("3 tasks / 1 done");
    await byId(page, "dynamic-task-2-title").locator("input").fill("Persisted browser task");
    await byId(page, "dynamic-reverse").click();
    await byId(page, "dynamic-task-2-title").locator("input").evaluate(node => { node.focus(); node.setSelectionRange(1, 7); });
    await observeNavigation(page);
});

test("terminal navigation disposes dynamic rows and a new document restores the authored seed", async ({ page, baseURL }) => {
    await page.goto(new URL("/__xui_acceptance__/away.html", baseURL).href);
    expect(await page.evaluate(() => JSON.parse(sessionStorage.getItem("xui-pagehide")))).toEqual({ persisted: false, nodes: 0 });
    await page.goBack();
    await expect(byId(page, "dynamic-task-2-title").locator("input")).toHaveValue("Build shared UI");
    await expect(page.locator('[data-xui-id^="dynamic-task-"][data-xui-id$="-title"]').first())
        .toHaveAttribute("data-xui-id", "dynamic-task-1-title");
});

test("real bfcache preserves dynamic row instances edited model and callable handlers @bfcache", async ({ page, baseURL }, testInfo) => {
    const count = await page.locator("#app .xui-node").count();
    await page.goto(new URL("/__xui_acceptance__/away.html", baseURL).href);
    const hidden = await page.evaluate(() => JSON.parse(sessionStorage.getItem("xui-pagehide")));
    await page.goBack({ waitUntil: "commit" });
    const input = byId(page, "dynamic-task-2-title").locator("input");
    await expect(input).toHaveValue("Persisted browser task");
    const restored = await page.evaluate(() => ({
        restored: window.restored === true,
        inputs: window.originalInputs?.every((node, index) => document.querySelectorAll("#app input")[index] === node),
        tree: window.originalNodes?.every((node, index) => document.querySelectorAll("#app .xui-node")[index] === node),
        reasons: performance.getEntriesByType("navigation")[0]?.notRestoredReasons?.toJSON() ?? null
    }));
    await testInfo.attach("dynamic-bfcache", { body: JSON.stringify({ hidden, ...restored }), contentType: "application/json" });
    expect(hidden).toEqual({ persisted: true, nodes: count });
    expect(restored, JSON.stringify(restored.reasons)).toMatchObject({ restored: true, inputs: true, tree: true });
    expect(await input.evaluate(node => [node.selectionStart, node.selectionEnd])).toEqual([1, 7]);
    await byId(page, "dynamic-task-2-toggle").click();
    await expect(byId(page, "dynamic-summary")).toHaveText("3 tasks / 2 done");
    await byId(page, "dynamic-reverse").click();
    await expect(input).toHaveValue("Persisted browser task");
});
