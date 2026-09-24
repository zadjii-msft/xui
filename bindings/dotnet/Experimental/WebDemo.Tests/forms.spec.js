import { readFileSync } from "node:fs";
import { test, expect, byId } from "./fixtures.js";

const corpus = JSON.parse(readFileSync(new URL("../SharedDemo/FormsScenarios.json", import.meta.url), "utf8"));
test.beforeEach(async ({ page }) => {
    await page.goto("./?app=forms");
    await expect(byId(page, "forms-title")).toBeVisible();
});
for (const scenario of corpus.scenarios) {
    test(`shared forms: ${scenario.name}`, async ({ page }) => {
        await page.context().setOffline(true);
        for (const step of scenario.steps) {
            const wrapper = byId(page, step.id);
            const editor = wrapper.locator("input,textarea");
            if (step.action === "click") await wrapper.click();
            else if (step.action === "change") await editor.fill(step.value);
            else if (step.action === "seed-password") await editor.fill("x".repeat(step.codeUnits));
            else if (step.action === "expect") {
                if (Object.hasOwn(step, "text")) {
                    if (await editor.count()) await expect(editor).toHaveValue(step.text);
                    else await expect(wrapper).toHaveText(step.text);
                }
                if (Object.hasOwn(step, "readOnly")) expect(await editor.evaluate(node => node.readOnly)).toBe(step.readOnly);
                if (Object.hasOwn(step, "passwordLength")) expect(await editor.evaluate(node => node.value.length)).toBe(step.passwordLength);
                if (Object.hasOwn(step, "purpose")) {
                    await expect(editor).toHaveAttribute("type", "text");
                    await expect(editor).toHaveAttribute("inputmode", { Email: "email", Number: "decimal" }[step.purpose]);
                }
            } else throw new Error("Unknown shared form action.");
        }
    });
}

test("textarea keeps native Enter selection and read-only behavior without replacing the node", async ({ page }) => {
    const notes = byId(page, "forms-notes").locator("textarea");
    await notes.fill("first");
    await notes.press("End");
    await notes.press("Enter");
    await notes.pressSequentially("second");
    await expect(notes).toHaveValue("first\nsecond");
    await notes.evaluate(node => { window.originalNotes = node; node.setSelectionRange(1, 4); });
    await byId(page, "forms-lock").evaluate(node => node.click());
    await expect(notes).toHaveAttribute("readonly", "");
    expect(await notes.evaluate(node => node === window.originalNotes && node.selectionStart === 1 && node.selectionEnd === 4)).toBe(true);
    await notes.press("X");
    await expect(notes).toHaveValue("first\nsecond");
    await byId(page, "forms-lock").click();
    await expect(notes).toHaveAttribute("maxlength", "64");
    await expect(notes).toHaveAccessibleName("Notes");
});

test("password is native masked state with no value attribute or copy/cut/context-menu default action", async ({ page }) => {
    const input = byId(page, "forms-password").locator("input");
    await expect(input).toHaveAttribute("type", "password");
    await expect(input).toHaveAttribute("maxlength", "32");
    await input.fill("x".repeat(8));
    expect(await input.evaluate(node => node.getAttribute("value"))).toBeNull();
    expect(await input.evaluate(node => ["copy", "cut", "contextmenu"].map(type =>
        node.dispatchEvent(new Event(type, { bubbles: true, cancelable: true }))))).toEqual([false, false, false]);
    await input.evaluate(node => { node.value = "z".repeat(11); });
    await byId(page, "forms-inspect").click();
    await expect(byId(page, "forms-status")).toHaveText("Inspected 11 UTF-16 units; no value displayed.");
    expect(await page.locator("#app").textContent()).not.toContain("z".repeat(11));
});
