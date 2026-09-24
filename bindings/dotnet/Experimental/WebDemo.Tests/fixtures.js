import { test as base, expect } from "@playwright/test";

export const test = base.extend({
    diagnostics: [async ({ page }, use) => {
        const errors = [];
        const expected = [];
        page.on("pageerror", error => errors.push(error.message));
        page.on("console", message => { if (message.type() === "error") errors.push(message.text()); });
        await use({ expected });
        expect(errors).toHaveLength(expected.length);
        for (let i = 0; i < expected.length; i++) expect(errors[i]).toMatch(expected[i]);
    }, { auto: true }]
});
export { expect };
export async function command(page, name, value = null) {
    return page.evaluate(([name, value]) => window.xuiTest(name, value), [name, value]);
}
export async function demo(page) {
    await page.goto("/?test");
    await expect(page.getByRole("button", { name: "Increment", exact: true })).toBeVisible();
    await page.waitForFunction(() => typeof window.xuiTest === "function");
}
export const byId = (page, id) => page.locator(`[data-xui-id="${id}"]`);

export async function runScenarioSteps(page, steps) {
    for (const step of steps) {
        await test.step(`${step.action} ${step.id}`, async () => {
            const node = byId(page, step.id);
            if (step.action === "expect" && step.visible === false &&
                !Object.hasOwn(step, "text") && !Object.hasOwn(step, "enabled")) {
                await expect(node).toBeHidden();
                return;
            }
            await expect(node).toHaveCount(1);
            const input = node.locator("input");
            const isInput = await input.count() === 1;
            const control = isInput ? input : node;
            switch (step.action) {
                case "change": await control.fill(step.value); break;
                case "submit": await control.press("Enter"); break;
                case "click":
                    if (await control.isDisabled()) await control.evaluate(element => element.click());
                    else await control.click();
                    break;
                case "expect":
                    if (Object.hasOwn(step, "text")) {
                        if (isInput) await expect(input).toHaveValue(step.text);
                        else await expect.poll(() => node.textContent()).toBe(step.text);
                    }
                    if (Object.hasOwn(step, "enabled")) await expect(control).toBeEnabled({ enabled: step.enabled });
                    if (Object.hasOwn(step, "visible")) await expect(node).toBeVisible({ visible: step.visible });
                    break;
                default: throw new Error(`Unsupported scenario action: ${step.action}`);
            }
        });
    }
}
