import { readFileSync } from "node:fs";
import { test, expect, byId } from "./fixtures.js";

const corpus = JSON.parse(readFileSync(new URL("../Xui.Portable.Tests/Fixtures/PageScenarios.json", import.meta.url), "utf8"));
for (const scenario of corpus.scenarios) {
    test(`retained native pages: ${scenario.name}`, async ({ page }) => {
        await page.goto("./?app=pages");
        await expect(byId(page, "workspace-tabs")).toBeVisible();
        for (const step of scenario.steps) {
            const node = byId(page, step.id);
            if (step.action === "click") await node.click();
            else if (step.action === "change") await node.locator("input").fill(step.value);
            else if (["select-page", "activate-page", "request-close"].includes(step.action)) {
                const target = node.locator(`[id$="-tab-${step.page}"]`);
                const button = step.action === "request-close" ? target.locator("..").locator(".xui-tab-close") : target;
                if (await button.isDisabled()) await button.evaluate(element => element.click());
                else if (step.action === "activate-page") await button.press("Enter");
                else await button.click();
            } else if (step.action === "remember-editor") {
                await node.locator("input").evaluate((input, id) => { (window.rememberedEditors ??= {})[id] = input; }, step.id);
            } else if (step.action === "expect-same-editor") {
                expect(await node.locator("input").evaluate((input, id) => window.rememberedEditors[id] === input, step.id)).toBe(true);
            } else if (step.action === "expect-absent") await expect(node).toHaveCount(0);
            else if (step.action === "expect") {
                if (Object.hasOwn(step, "text")) {
                    if (await node.locator("input").count()) await expect(node.locator("input")).toHaveValue(step.text);
                    else await expect(node).toHaveText(step.text);
                }
                if (Object.hasOwn(step, "visible")) await expect(node).toBeVisible({ visible: step.visible });
                if (Object.hasOwn(step, "enabled")) await expect(node).toHaveAttribute("aria-disabled", String(!step.enabled));
                if (Object.hasOwn(step, "selected")) {
                    const selected = node.locator('[aria-selected="true"],[aria-current="page"]');
                    if (step.selected === null) await expect(selected).toHaveCount(0);
                    else await expect(selected).toHaveAttribute("id", new RegExp(`-tab-${step.selected}$`));
                }
                if (Object.hasOwn(step, "pages")) {
                    expect(await node.locator('[role="tab"]').evaluateAll(items => items.map(item => item.id.split("-tab-")[1]))).toEqual(step.pages);
                }
            } else throw new Error("Unknown shared retained-page action.");
        }
    });
}

test("native tab keyboard and typed aria relationships keep inactive editors out of accessibility", async ({ page }) => {
    await page.goto("./?app=pages");
    const tabs = page.getByRole("tablist", { name: "Open documents" });
    await tabs.getByRole("tab", { name: "First document" }).focus();
    await page.keyboard.press("ArrowRight");
    await expect(tabs.getByRole("tab", { name: "Second document" })).toBeFocused();
    await expect(tabs.getByRole("tab", { name: "Second document" })).toHaveAttribute("aria-selected", "true");
    await expect(page.getByRole("tabpanel", { name: "Second document" })).toBeVisible();
    await expect(page.getByRole("tabpanel", { name: "First document" })).toHaveCount(0);
    const selected = tabs.getByRole("tab", { name: "Second document" });
    expect(await selected.evaluate(node => {
        const panel = document.getElementById(node.getAttribute("aria-controls"));
        return panel?.getAttribute("aria-labelledby") === node.id;
    })).toBe(true);
    await page.keyboard.press("Home");
    await expect(tabs.getByRole("tab", { name: "First document" })).toBeFocused();
    await page.keyboard.press("Space");
    await expect(byId(page, "page-activation")).toHaveText("Activated: 1");
});
