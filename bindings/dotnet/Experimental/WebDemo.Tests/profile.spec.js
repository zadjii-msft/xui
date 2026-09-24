import { test, expect, byId, profile, profileApplication, runProfileSteps, readStored, holdNextOpen } from "./profile-fixtures.js";

for (const scenario of profileApplication.scenarios) {
    test(`shared Profile scenario: ${scenario.name}`, async ({ page, database }) => {
        await profile(page, database);
        await page.context().setOffline(true);
        await runProfileSteps(page, scenario.steps);
    });
}

test("Profile never loads on startup and explicit Load after reload restores the real saved document", async ({ page, database }) => {
    await profile(page, database);
    await byId(page, "profile-name").locator("input").fill("Saved Profile");
    await byId(page, "profile-role").locator("input").fill("Developer");
    await runProfileSteps(page, [{ action: "click", id: "profile-save" }]);
    expect((await readStored(page, database)).text).toContain("Saved Profile");
    await page.reload();
    await expect(byId(page, "profile-name").locator("input")).toHaveValue("");
    expect(await page.evaluate(() => window.profileIo)).toEqual({ opens: 0, reads: 0, writes: 0, deletes: 0 });
    await runProfileSteps(page, [{ action: "click", id: "profile-load" }]);
    await expect(byId(page, "profile-name").locator("input")).toHaveValue("Saved Profile");
    await expect(byId(page, "profile-role").locator("input")).toHaveValue("Developer");
});

test("malformed stored Profile fails visibly and retains incomplete native draft fields", async ({ page, database }) => {
    await profile(page, database);
    await byId(page, "profile-role").locator("input").fill("Incomplete draft");
    await page.evaluate(async name => {
        const module = await import("./_content/Xui.Web/xui-storage.js");
        const store = module.createStorage(name);
        try {
            const result = await store.run(1, "write", "profile-draft-v1", new TextEncoder().encode("{malformed"));
            if (result.status !== "Completed") throw new Error(result.error ?? result.status);
        } finally { store.close(); }
    }, database);
    await runProfileSteps(page, [{ action: "click", id: "profile-load" }]);
    await expect(byId(page, "profile-status")).toHaveText("Load failed.");
    await expect(byId(page, "profile-error")).toBeVisible();
    await expect(byId(page, "profile-role").locator("input")).toHaveValue("Incomplete draft");
    await expect(byId(page, "profile-name").locator("input")).toHaveValue("");
    await expect(byId(page, "profile-save")).toBeDisabled();
    await expect(byId(page, "profile-busy")).toBeHidden();
});

test("Cancel settles a pending native open before another save and late delivery cannot overwrite storage", async ({ page, database }) => {
    await profile(page, database);
    await byId(page, "profile-name").locator("input").fill("Confirmed document");
    await runProfileSteps(page, [{ action: "click", id: "profile-save" }]);
    await byId(page, "profile-name").locator("input").fill("Canceled draft");
    await holdNextOpen(page, database);
    try {
        await byId(page, "profile-save").click();
        await expect(byId(page, "profile-busy")).toBeVisible();
        await expect(byId(page, "profile-name").locator("input")).toBeDisabled();
        await expect(byId(page, "profile-reset")).toBeDisabled();
        await page.waitForFunction(() => typeof window.releaseProfileOpen === "function");
        await byId(page, "profile-cancel").click();
        await expect(byId(page, "profile-status")).toHaveText("Save canceled. Storage may have changed; Load draft to check.");
        await expect(byId(page, "profile-busy")).toBeHidden();
        await expect(byId(page, "profile-name").locator("input")).toHaveValue("Canceled draft");
    } finally { await page.evaluate(() => window.releaseProfileOpen?.()); }
    expect((await readStored(page, database)).text).toContain("Confirmed document");
    await runProfileSteps(page, [{ action: "click", id: "profile-save" }]);
    expect((await readStored(page, database)).text).toContain("Canceled draft");
});

test("Profile native editors retain selection while editing other fields and remain accessible at a narrow text scale", async ({ page, database }) => {
    await page.setViewportSize({ width: 320, height: 500 });
    await profile(page, database);
    const name = byId(page, "profile-name").locator("input");
    await name.fill("Ada Lovelace");
    await name.evaluate(node => {
        window.profileNameInput = node;
        node.setSelectionRange(1, 5);
        window.profileValueWrites = 0;
        const descriptor = Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, "value");
        Object.defineProperty(node, "value", {
            get() { return descriptor.get.call(this); },
            set(value) { window.profileValueWrites++; descriptor.set.call(this, value); }
        });
    });
    await byId(page, "profile-role").locator("input").fill("Developer");
    expect(await name.evaluate(node => ({
        same: node === window.profileNameInput, start: node.selectionStart, end: node.selectionEnd, writes: window.profileValueWrites
    }))).toEqual({ same: true, start: 1, end: 5, writes: 0 });
    await name.focus();
    for (const id of ["profile-role", "profile-location", "profile-focus"]) {
        await page.keyboard.press("Tab");
        await expect(byId(page, id).locator("input")).toBeFocused();
    }
    await page.evaluate(() => { document.body.style.fontSize = "24px"; });
    for (const [id, label] of [["profile-name", "Display name"], ["profile-role", "Role"], ["profile-location", "Location"], ["profile-focus", "Focus"]]) {
        const input = byId(page, id).locator("input");
        await expect(input).toHaveAccessibleName(label);
        await input.scrollIntoViewIfNeeded();
        expect(await input.evaluate(node => {
            const field = node.parentElement.getBoundingClientRect();
            const caption = node.labels[0].getBoundingClientRect();
            const edit = node.getBoundingClientRect();
            return caption.bottom <= edit.top && edit.bottom <= field.bottom && field.width <= innerWidth;
        })).toBe(true);
    }
    await byId(page, "profile-preview").click();
    expect(await page.evaluate(() => window.profileNameInput.isConnected)).toBe(false);
    await byId(page, "profile-back").click();
    await expect(name).toHaveValue("Ada Lovelace");
    expect(await name.evaluate(node => node !== window.profileNameInput)).toBe(true);
    expect(await page.evaluate(() => window.profileIo)).toEqual({ opens: 0, reads: 0, writes: 0, deletes: 0 });
});
