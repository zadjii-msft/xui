import { readFileSync } from "node:fs";
import { test, expect, byId } from "./fixtures.js";

const corpus = JSON.parse(readFileSync(new URL("../SharedDemo/WorkspaceStudioScenarios.json", import.meta.url), "utf8"));
for (const scenario of corpus.scenarios) {
    test(`actual Studio: ${scenario.name}`, async ({ page }) => {
        await page.setViewportSize({ width: 1400, height: 1000 });
        await page.goto("./?app=studio&studio-test=1");
        await expect(byId(page, "doc-00001-title").locator("input")).toBeVisible();
        await expect(page.locator("#app")).toHaveAttribute("data-xui-studio-host-ready", "true");
        await page.evaluate(() => { window.outerBackRequests = 0; history.back = () => { window.outerBackRequests++; }; });
        for (const step of scenario.steps) {
            if (step.action === "change") await byId(page, step.id).locator("input,textarea").fill(step.value);
            else if (step.action === "click") await byId(page, step.id).click();
            else if (step.action === "selectTab") await byId(page, "studio-tabs").locator(`[id$="-tab-${step.id}"]`).click();
            else if (step.action === "selectSection") await byId(page, "studio-navigation").locator(`[id$="-tab-${step.id}"]`).click();
            else if (step.action === "closeTab")
                await byId(page, "studio-tabs").locator(`[id$="-tab-${step.id}"]`).locator("..").locator(".xui-tab-close").click();
            else if (step.action === "openDocument") await byId(page, step.key + "-open").click();
            else if (step.action === "awaitAnalysis")
            {
                if (await page.evaluate(() => typeof window.xuiStudioIdle === "function"))
                    await page.evaluate(() => window.xuiStudioIdle());
                await expect(byId(page, "studio-status")).toHaveText("Analysis complete. No content left this workspace.");
            }
            else if (step.action === "awaitOperations")
            {
                if (await page.evaluate(() => typeof window.xuiStudioIdle === "function"))
                    await page.evaluate(() => window.xuiStudioIdle());
                await expect(byId(page, "operations-status")).toHaveText("Local operations snapshot complete. No cloud metrics or user files were accessed.");
            }
            else if (step.action === "selectOperationsScope")
                await byId(page, "operations-scope").locator("select").selectOption(String(step.id));
            else if (step.action === "back")
            {
                await page.keyboard.press("Alt+ArrowLeft");
                await expect(page.locator("#app")).toHaveAttribute("data-xui-back-handled", String(step.handled));
                await expect(page.locator("#app")).toHaveAttribute("data-xui-back-blocked", String(step.blocked));
                if (!step.handled) expect(await page.evaluate(() => window.outerBackRequests)).toBeGreaterThan(0);
            }
            else if (step.action === "expectTabs") {
                const tabs = byId(page, "studio-tabs").getByRole("tab");
                await expect(tabs).toHaveCount(step.count);
                if (step.selected !== null)
                    await expect(byId(page, "studio-tabs").locator(`[id$="-tab-${step.selected}"]`)).toHaveAttribute("aria-selected", "true");
            } else if (step.action === "expect") {
                const node = byId(page, step.id);
                if (Object.hasOwn(step, "text")) {
                    if (await node.locator("input,textarea").count()) await expect(node.locator("input,textarea")).toHaveValue(step.text);
                    else await expect(node).toHaveText(step.text);
                }

                if (Object.hasOwn(step, "enabled")) await expect(node).toBeEnabled({ enabled: step.enabled });
            } else throw new Error("Unknown Studio corpus action.");
        }
    });
}

test("one real Studio tree changes panes at 720 and 1120 while retaining inactive document editors", async ({ page }) => {
                    await page.setViewportSize({ width: 1400, height: 900 });
                    await page.goto("./?app=studio");
                    const body = byId(page, "doc-00001-body").locator("textarea");
                    await expect(body).toBeVisible();
                    await body.fill("Retained responsive draft\nSecond line");
                    await body.evaluate(node => { window.studioEditor = node; node.setSelectionRange(2, 9); });
                    await byId(page, "studio-theme").focus();
                    await page.setViewportSize({ width: 1119, height: 900 });
                    await expect(byId(page, "studio-layout")).toHaveText("Medium workspace / retained editor panes");
                    await expect(byId(page, "studio-details")).toBeHidden();
                    await page.setViewportSize({ width: 720, height: 900 });
                    await expect(byId(page, "studio-layout")).toHaveText("Medium workspace / retained editor panes");
                    await page.setViewportSize({ width: 719, height: 900 });
                    await expect(byId(page, "studio-layout")).toHaveText("Compact workspace / Document");
                    await expect(byId(page, "studio-catalog")).toBeHidden();
                    await page.setViewportSize({ width: 320, height: 700 });
                    await byId(page, "studio-navigation").locator('[id$="-tab-1"]').click();
                    await expect(byId(page, "studio-layout")).toHaveText("Compact workspace / Catalog");
                    await expect(body).toBeHidden();
                    await expect(byId(page, "studio-search")).toBeVisible();
                    await byId(page, "doc-00001-open").click();
                    await expect(body).toBeVisible();
                    await expect(byId(page, "studio-layout")).toHaveText("Compact workspace / Document");
                    await expect(body).toHaveValue("Retained responsive draft\nSecond line");
                    expect(await body.evaluate(node => node === window.studioEditor && node.selectionStart === 2 && node.selectionEnd === 9)).toBe(true);
                    await byId(page, "studio-navigation").locator('[id$="-tab-3"]').click();
                    await expect(byId(page, "studio-layout")).toHaveText("Compact workspace / Details");
                    await expect(byId(page, "studio-details")).toBeVisible();
                    await page.setViewportSize({ width: 1120, height: 900 });
                    await expect(byId(page, "studio-layout")).toHaveText("Expanded workspace / retained editor panes");
                    await expect(body).toBeVisible();
                    expect(await body.evaluate(node => node === window.studioEditor)).toBe(true);
                });

                test("page hide and selection preflight reject active composition without changing the native editor", async ({ page, diagnostics }) => {
                    await page.setViewportSize({ width: 1400, height: 900 });
                    await page.goto("./?app=studio");
                    const editor = byId(page, "doc-00001-body").locator("textarea");
                    await expect(editor).toBeVisible();
                    await editor.fill("Native composition stays owned");
                    await editor.evaluate(node => {
                        window.composingStudioEditor = node;
                        node.focus();
                        node.setSelectionRange(1, 8);
                        node.dispatchEvent(new CompositionEvent("compositionstart", { bubbles: true }));
                    });
                    diagnostics.expected.push(/Finish editing or explicitly move focus/);
                    await byId(page, "studio-tabs").locator('[id$="-tab-2"]').evaluate(node => node.click());
                    await expect(byId(page, "studio-tabs").locator('[id$="-tab-1"]')).toHaveAttribute("aria-selected", "true");
                    expect(await editor.evaluate(node => node === window.composingStudioEditor && node === document.activeElement && node.selectionStart === 1 && node.selectionEnd === 8)).toBe(true);
                    await editor.evaluate(node => node.dispatchEvent(new CompositionEvent("compositionend", { bubbles: true })));
                    await byId(page, "studio-tabs").locator('[id$="-tab-2"]').click();
                    await expect(byId(page, "doc-00002-body").locator("textarea")).toBeVisible();
                });
