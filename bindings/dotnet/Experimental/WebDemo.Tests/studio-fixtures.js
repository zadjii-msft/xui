import { readFileSync } from "node:fs";
import { expect, byId } from "./fixtures.js";

export const studioScenarios = JSON.parse(readFileSync(new URL("../SharedDemo/WorkspaceStudioScenarios.json", import.meta.url), "utf8")).scenarios;
export async function runStudioSteps(page, steps) {
    for (const step of steps) {
        if (step.action === "change") await byId(page, step.id).locator("input,textarea").fill(step.value);
        else if (step.action === "click") await byId(page, step.id).click();
        else if (step.action === "selectTab") await byId(page, "studio-tabs").locator(`[id$="-tab-${step.id}"]`).click();
        else if (step.action === "selectSection") await byId(page, "studio-navigation").locator(`[id$="-tab-${step.id}"]`).click();
        else if (step.action === "closeTab")
            await byId(page, "studio-tabs").locator(`[id$="-tab-${step.id}"]`).locator("..").locator(".xui-tab-close").click();
        else if (step.action === "openDocument") await byId(page, step.key + "-open").click();
        else if (step.action === "awaitAnalysis" || step.action === "awaitOperations") {
            if (await page.evaluate(() => typeof window.xuiStudioIdle === "function"))
                await page.evaluate(() => window.xuiStudioIdle());
            await expect(byId(page, step.action === "awaitAnalysis" ? "studio-status" : "operations-status")).toHaveText(
                step.action === "awaitAnalysis" ? "Analysis complete. No content left this workspace." :
                    "Local operations snapshot complete. No cloud metrics or user files were accessed.");
        } else if (step.action === "selectOperationsScope")
            await byId(page, "operations-scope").locator("select").selectOption(String(step.id));
        else if (step.action === "back") {
            const before = await page.evaluate(() => window.outerBackRequests);
            await page.keyboard.press("Alt+ArrowLeft");
            await expect(page.locator("#app")).toHaveAttribute("data-xui-back-handled", String(step.handled));
            await expect(page.locator("#app")).toHaveAttribute("data-xui-back-blocked", String(step.blocked));
            expect(await page.evaluate(() => window.outerBackRequests)).toBe(before + (step.handled ? 0 : 1));
        } else if (step.action === "expectTabs") {
            await expect(byId(page, "studio-tabs").getByRole("tab")).toHaveCount(step.count);
            if (step.selected !== null)
                await expect(byId(page, "studio-tabs").locator(`[id$="-tab-${step.selected}"]`)).toHaveAttribute("aria-selected", "true");
        } else if (step.action === "expect") {
            const node = byId(page, step.id);
            if (Object.hasOwn(step, "text")) {
                if (await node.locator("input,textarea").count()) await expect(node.locator("input,textarea")).toHaveValue(step.text);
                else await expect(node).toHaveText(step.text);
            }
            if (Object.hasOwn(step, "enabled")) await expect(node).toBeEnabled({ enabled: step.enabled });
            if (Object.hasOwn(step, "visible")) await expect(node).toBeVisible({ visible: step.visible });
        } else throw new Error("Unknown Studio corpus action.");
    }
}
