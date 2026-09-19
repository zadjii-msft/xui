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
