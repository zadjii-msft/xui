import { test, expect, byId, profile, runProfileSteps, readStored, holdNextOpen } from "./profile-fixtures.js";
import { observeNavigation } from "./navigation.js";

test("terminal Profile navigation cancels root-owned work before storage/module cleanup", async ({ page, database, baseURL }) => {
    await profile(page, database);
    await byId(page, "profile-name").locator("input").fill("Confirmed before navigation");
    await runProfileSteps(page, [{ action: "click", id: "profile-save" }]);
    await byId(page, "profile-name").locator("input").fill("Uncommitted navigation draft");
    await holdNextOpen(page, database);
    await byId(page, "profile-save").click();
    await expect(byId(page, "profile-busy")).toBeVisible();
    await page.waitForFunction(() => typeof window.releaseProfileOpen === "function");
    await observeNavigation(page);
    await page.evaluate(() => window.addEventListener("pagehide", () => window.releaseProfileOpen?.()));
    await page.goto(new URL("/__xui_acceptance__/away.html", baseURL).href);
    expect(await page.evaluate(() => JSON.parse(sessionStorage.getItem("xui-pagehide"))))
        .toEqual({ persisted: false, nodes: 0 });
    await page.goBack();
    await expect(byId(page, "profile-name").locator("input")).toHaveValue("");
    expect(await page.evaluate(() => window.profileIo)).toEqual({ opens: 0, reads: 0, writes: 0, deletes: 0 });
    expect((await readStored(page, database)).text).toContain("Confirmed before navigation");
    await runProfileSteps(page, [{ action: "click", id: "profile-load" }]);
    await expect(byId(page, "profile-name").locator("input")).toHaveValue("Confirmed before navigation");
});

test("actual bfcache retains incomplete Profile draft controller and selected native editor without storage I/O @bfcache", async ({ page, database, baseURL }, testInfo) => {
    await profile(page, database);
    const input = byId(page, "profile-role").locator("input");
    await input.fill("Incomplete role draft");
    await input.evaluate(node => { node.focus(); node.setSelectionRange(2, 7); });
    await observeNavigation(page);
    const count = await page.locator("#app .xui-node").count();
    await page.goto(new URL("/__xui_acceptance__/away.html", baseURL).href);
    const hidden = await page.evaluate(() => JSON.parse(sessionStorage.getItem("xui-pagehide")));
    await page.goBack({ waitUntil: "commit" });
    await expect(input).toHaveValue("Incomplete role draft");
    const restored = await page.evaluate(() => ({
        restored: window.restored === true,
        nodes: window.originalNodes?.every((node, index) => document.querySelectorAll("#app .xui-node")[index] === node),
        inputs: window.originalInputs?.every((node, index) => document.querySelectorAll("#app input")[index] === node),
        reasons: performance.getEntriesByType("navigation")[0]?.notRestoredReasons?.toJSON() ?? null
    }));
    await testInfo.attach("profile-bfcache", { body: JSON.stringify({ hidden, ...restored }), contentType: "application/json" });
    expect(hidden).toEqual({ persisted: true, nodes: count });
    expect(restored, JSON.stringify(restored.reasons)).toMatchObject({ restored: true, nodes: true, inputs: true });
    expect(await input.evaluate(node => [node.selectionStart, node.selectionEnd])).toEqual([2, 7]);
    await expect(byId(page, "profile-name").locator("input")).toHaveValue("");
    await expect(byId(page, "profile-save")).toBeDisabled();
    expect(await page.evaluate(() => window.profileIo)).toEqual({ opens: 0, reads: 0, writes: 0, deletes: 0 });
    await byId(page, "profile-name").locator("input").fill("Name after restore");
    await byId(page, "profile-preview").click();
    await expect(byId(page, "profile-preview-name")).toHaveText("Name after restore");
    await expect(byId(page, "profile-preview-role")).toHaveText("Incomplete role draft");
});
