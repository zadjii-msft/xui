import { readFileSync } from "node:fs";
import { test, expect, byId } from "./fixtures.js";

const corpus = JSON.parse(readFileSync(new URL("../Xui.Portable.Tests/Fixtures/MutationScenarios.json", import.meta.url), "utf8"));
if (corpus.version !== 1 || !Array.isArray(corpus.scenarios)) throw new Error("Expected shared mutation scenarios version 1.");

test.beforeEach(async ({ page }) => {
    await page.goto("./?mutation=true");
    await expect(byId(page, "row-count")).toHaveText("Rows: 0");
    expect(await page.evaluate(() => typeof window.xuiTest)).toBe("undefined");
});

for (const scenario of corpus.scenarios) {
    test(`generated shared mutation scenario: ${scenario.name}`, async ({ page }) => {
        for (const step of scenario.steps) {
            switch (step.action) {
                case "click": await byId(page, step.id).click(); break;
                case "change": await byId(page, step.id).locator("input").fill(step.value); break;
                case "expect":
                    if (step.id.endsWith("-input")) await expect(byId(page, step.id).locator("input")).toHaveValue(step.text);
                    else await expect.poll(() => byId(page, step.id).textContent()).toBe(step.text);
                    break;
                case "expect-absent": await expect(byId(page, step.id)).toHaveCount(0); break;
                case "expect-order":
                    await expect.poll(() => page.locator('[data-xui-id^="row-"][data-xui-id$="-input"]')
                        .evaluateAll(nodes => nodes.map(node => node.dataset.xuiId.slice(0, -6)))).toEqual(step.keys);
                    break;
                default: throw new Error(`Unknown mutation scenario action: ${step.action}`);
            }
        }
    });
}

test("authored keyed rows preserve focused composing native editor through repeated reversals", async ({ page }) => {
    for (let i = 0; i < 3; i++) await byId(page, "add-row").click();
    const input = byId(page, "row-1-input").locator("input");
    await input.fill("Authored C# draft");
    await input.evaluate(node => {
        window.authoredInput = node;
        node.focus();
        node.setSelectionRange(1, 8);
        node.dispatchEvent(new CompositionEvent("compositionstart", { bubbles: true }));
    });
    for (let i = 0; i < 12; i++) {
        await byId(page, "reverse-rows").evaluate(node => node.click());
        await expect.poll(() => page.locator('[data-xui-id^="row-"][data-xui-id$="-input"]').first().getAttribute("data-xui-id"))
            .toBe(i % 2 === 0 ? "row-3-input" : "row-1-input");
        expect(await input.evaluate(node => node === window.authoredInput && node === document.activeElement &&
            node.selectionStart === 1 && node.selectionEnd === 8)).toBe(true);
    }
    await input.evaluate(node => node.dispatchEvent(new CompositionEvent("compositionend", { bubbles: true })));
    await input.press("Z");
    await expect(input).toHaveValue("AZ C# draft");
    await byId(page, "row-1-increment").click();
    await expect(byId(page, "row-1-count")).toHaveText("Edits: 1");
});
