import { readFileSync } from "node:fs";
import { test, expect, byId } from "./fixtures.js";

const corpus = JSON.parse(readFileSync(new URL("../SharedDemo/SettingsScenarios.json", import.meta.url), "utf8"));
if (corpus.version !== 1) throw new Error("Expected shared settings corpus version 1.");
test.beforeEach(async ({ page }) => {
    await page.goto("./?app=settings");
    await expect(byId(page, "settings-title")).toBeVisible();
});

for (const scenario of corpus.scenarios) {
    test(`shared settings: ${scenario.name}`, async ({ page }) => {
        await page.context().setOffline(true);
        for (const step of scenario.steps) {
            const node = byId(page, step.id);
            const input = node.locator("input");
            const progress = node.locator("progress");
            const control = await input.count() ? input : await progress.count() ? progress : node;
            if (step.action === "click") {
                if (await control.isDisabled()) await control.evaluate(element => element.click());
                else await control.click();
            } else if (step.action === "change") await input.fill(step.value);
            else if (step.action === "expect") {
                for (const [key, value] of Object.entries(step)) {
                    if (key === "id" || key === "action") continue;
                    switch (key) {
                        case "text":
                            if (await input.count()) await expect(input).toHaveValue(value);
                            else await expect.poll(() => node.textContent()).toBe(value);
                            break;
                        case "checked": await expect(input).toBeChecked({ checked: value }); break;
                        case "checkState":
                            await expect.poll(() => input.evaluate(element => element.indeterminate ? "Indeterminate" :
                                element.checked ? "Checked" : "Unchecked")).toBe(value);
                            break;
                        case "enabled":
                            if (await progress.count()) await expect(progress).toHaveAttribute("aria-disabled", String(!value));
                            else await expect(control).toBeEnabled({ enabled: value });
                            break;
                        case "visible": await expect(node).toBeVisible({ visible: value }); break;
                        case "minimum": await expect(progress).toHaveAttribute("aria-valuemin", String(value)); break;
                        case "maximum": await expect(progress).toHaveAttribute("aria-valuemax", String(value)); break;
                        case "value":
                            if (value === null) expect(await progress.getAttribute("aria-valuenow")).toBeNull();
                            else await expect(progress).toHaveAttribute("aria-valuenow", String(value));
                            break;
                        case "indeterminate": expect(await progress.evaluate(element => !element.hasAttribute("value"))).toBe(value); break;
                        default: throw new Error(`Unknown settings expectation: ${key}`);
                    }
                }
            } else throw new Error(`Unknown settings action: ${step.action}`);
        }
    });
}

test("native choice keyboard and progress ratios preserve retained editing and accessible state", async ({ page }) => {
    const draft = byId(page, "settings-draft").locator("input");
    await draft.fill("Native draft");
    await draft.evaluate(node => {
        window.settingsInput = node;
        node.focus();
        node.setSelectionRange(1, 5);
    });
    await byId(page, "notifications").locator("input").evaluate(node => node.click());
    expect(await draft.evaluate(node => node === window.settingsInput && node === document.activeElement &&
        node.selectionStart === 1 && node.selectionEnd === 5)).toBe(true);
    const mixed = page.getByRole("checkbox", { name: "Sync policy" });
    await expect(mixed).toBeChecked({ indeterminate: true });
    await mixed.focus();
    await mixed.press("Space");
    await expect(mixed).not.toBeChecked();
    await expect(page.getByRole("switch", { name: "Notifications" })).not.toBeChecked();
    const units = byId(page, "unit-progress").locator("progress");
    await expect(units).toHaveAccessibleName("Authored units preview");
    expect(await units.evaluate(node => node.position)).toBeCloseTo(0.3125, 8);
    await byId(page, "advance-units").click();
    expect(await units.evaluate(node => node.position)).toBeCloseTo(0.31875, 8);
    await byId(page, "indeterminate").locator("input").click();
    const work = byId(page, "work-progress").locator("progress");
    await expect(work).toHaveClass(/xui-progress-running/);
    expect(await work.getAttribute("aria-valuenow")).toBeNull();
    await byId(page, "options-enabled").locator("input").click();
    await expect(work).not.toHaveClass(/xui-progress-running/);
    await expect(work).toHaveAttribute("aria-disabled", "true");
    await byId(page, "options-enabled").locator("input").click();
    await expect(work).toHaveClass(/xui-progress-running/);
    await page.emulateMedia({ reducedMotion: "reduce" });
    expect(await work.evaluate(node => getComputedStyle(node).animationName)).toBe("none");
    await byId(page, "reset-settings").click();
    await expect(byId(page, "changes-summary")).toHaveText("Changes: 0");
    await expect(work).not.toHaveClass(/xui-progress-running/);
});

test("settings labels and native editors fit scaled narrow layout", async ({ page }) => {
    await page.setViewportSize({ width: 320, height: 360 });
    await page.evaluate(() => { document.body.style.fontSize = "24px"; });
    for (const id of ["notifications", "sync-policy", "indeterminate"]) {
        const node = byId(page, id);
        await node.scrollIntoViewIfNeeded();
        expect(await node.evaluate(wrapper => {
            const bounds = wrapper.getBoundingClientRect();
            const input = wrapper.querySelector("input").getBoundingClientRect();
            const label = wrapper.querySelector("label").getBoundingClientRect();
            return input.width > 0 && input.height > 0 && input.right <= label.left &&
                label.right <= bounds.right && label.bottom <= bounds.bottom;
        })).toBe(true);
    }
});
