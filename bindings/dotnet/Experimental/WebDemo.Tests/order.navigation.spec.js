import { test, expect, byId, inputById, order, runSteps, fullOrder, fullOrderExpectations } from "./order-fixtures.js";
import { observeNavigation } from "./navigation.js";

async function snapshot(page) {
    return page.evaluate(() => ({
        controls: [...document.querySelectorAll("#app [data-xui-id]")].filter(node => node.dataset.xuiId).map(node => {
            const input = node.querySelector("input");
            const control = input ?? node;
            return {
                id: node.dataset.xuiId, text: input?.value ?? node.textContent,
                enabled: !control.disabled && control.getAttribute("aria-disabled") !== "true",
                visible: !node.hidden
            };
        }),
        selections: [...document.querySelectorAll("#app input")].map(node => [node.selectionStart, node.selectionEnd])
    }));
}

test("real terminal navigation disposes the full order and Back starts fresh", async ({ page, baseURL }) => {
    await order(page);
    const initial = await snapshot(page);
    await runSteps(page, fullOrder.steps);
    await observeNavigation(page);
    await page.goto(new URL("/__xui_acceptance__/away.html", baseURL).href);
    await expect(page.getByRole("heading", { name: "Navigation target" })).toBeVisible();
    expect(await page.evaluate(() => JSON.parse(sessionStorage.getItem("xui-pagehide"))))
        .toEqual({ persisted: false, nodes: 0 });
    await page.goBack();
    await expect(inputById(page, "customer-name")).toBeVisible();
    expect(await snapshot(page)).toEqual(initial);
    expect(await page.evaluate(() => typeof window.xuiTest)).toBe("undefined");
    await runSteps(page, fullOrder.steps);
});

test("real bfcache preserves all order inputs totals review state and native nodes @bfcache", async ({ page, baseURL }, testInfo) => {
    await order(page);
    await runSteps(page, fullOrder.steps);
    await page.evaluate(() => {
        for (const input of document.querySelectorAll("#app input")) input.setSelectionRange(1, 3);
        document.querySelector('[data-xui-id="email"] input').focus();
    });
    await observeNavigation(page);
    const before = await snapshot(page);
    const nodeCount = await page.locator("#app .xui-node").count();
    await page.goto(new URL("/__xui_acceptance__/away.html", baseURL).href);
    await expect(page.getByRole("heading", { name: "Navigation target" })).toBeVisible();
    const hidden = await page.evaluate(() => JSON.parse(sessionStorage.getItem("xui-pagehide")));
    await page.goBack({ waitUntil: "commit" });
    await expect(inputById(page, "customer-name")).toBeVisible();
    const navigation = await page.evaluate(() => ({
        restored: window.restored === true,
        reasons: performance.getEntriesByType("navigation")[0]?.notRestoredReasons?.toJSON() ?? null
    }));
    await testInfo.attach("order-bfcache-navigation", { body: JSON.stringify({ hidden, ...navigation }), contentType: "application/json" });
    expect(hidden).toEqual({ persisted: true, nodes: nodeCount });
    expect(navigation.restored, JSON.stringify(navigation.reasons)).toBe(true);
    expect(await snapshot(page)).toEqual(before);
    expect(await page.evaluate(() => ({
        inputs: window.originalInputs.every((node, i) => document.querySelectorAll("#app input")[i] === node),
        tree: window.originalNodes.every((node, i) => document.querySelectorAll("#app .xui-node")[i] === node)
    }))).toEqual({ inputs: true, tree: true });
    await byId(page, "edit").click();
    await expect(byId(page, "review-summary")).toBeHidden();
    await inputById(page, "email").press("Enter");
    await runSteps(page, fullOrderExpectations);
});
