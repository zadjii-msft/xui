import { mkdir, readFile, writeFile } from "node:fs/promises";
import { createHash, randomUUID } from "node:crypto";
import { resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { test, expect, byId } from "./fixtures.js";
import { captureApp, captureName } from "./capture-apps.js";

test(`capture actual published ${captureName} shared application`, async ({ page, browser }, testInfo) => {
    if (captureApp.viewport) await page.setViewportSize(captureApp.viewport);
    const profileStore = captureName === "profile-workspace" ? `profile-capture-${randomUUID()}` : null;
    if (profileStore) await page.addInitScript(() => {
        window.captureStorageOpens = 0;
        const original = indexedDB.open;
        indexedDB.open = function (...args) { window.captureStorageOpens++; return original.apply(this, args); };
    });
    await page.goto(captureApp.project === "WebGalleryDemo"
        ? `./?app=${captureApp.route ?? captureName}${profileStore ? `&profile-store=${profileStore}` : ""}` : "./");
    if (captureName.startsWith("studio")) await expect(byId(page, "doc-00001-title").locator("input")).toBeVisible();
    else await expect(page.locator("#app input").first()).toBeVisible();
    expect(await page.evaluate(() => typeof window.xuiTest)).toBe("undefined");
    await page.context().setOffline(true);
    if (captureName === "greeting") {
        await page.getByRole("textbox", { name: "Your name" }).fill("Ada Lovelace");
        await page.getByRole("textbox").press("Enter");
        await expect(byId(page, "greeting")).toHaveText("Hello, Ada Lovelace!");
        for (let i = 0; i < 3; i++) await byId(page, "increment").click();
        await expect(byId(page, "count")).toHaveText("Count: 3");
    } else if (captureName === "order") {
        const { fullOrder, runSteps } = await import("./order-fixtures.js");
        await runSteps(page, fullOrder.steps);
    } else if (captureName === "settings") {
        await byId(page, "reset-settings").click();
        await expect(byId(page, "changes-summary")).toHaveText("Changes: 0");
        await expect(page.getByRole("checkbox", { name: "Sync policy" })).toBeChecked({ indeterminate: true });
        await expect(page.locator("#app progress")).toHaveCount(2);
    } else if (captureName === "profile-workspace") {
        const { profileApplication, runProfileSteps } = await import("./profile-fixtures.js");
        const seed = profileApplication.scenarios.find(scenario => scenario.name === "screenshot-seed-unsaved-preview");
        if (!seed) throw new Error("Missing shared unsaved Profile screenshot seed.");
        await runProfileSteps(page, seed.steps);
        expect(await page.evaluate(() => window.captureStorageOpens)).toBe(0);
    } else if (captureName.startsWith("studio")) {
        const { studioScenarios, runStudioSteps } = await import("./studio-fixtures.js");
        const seed = studioScenarios.find(scenario => scenario.name === "workspace-screenshot-state");
        if (!seed) throw new Error("Missing shared Studio screenshot seed.");
        await runStudioSteps(page, seed.steps);
    } else {
        const { galleryApplications, runGallerySteps } = await import("./gallery-fixtures.js");
        const application = galleryApplications.find(entry => entry.id === (captureApp.scenarioId ?? captureName));
        const seed = application?.scenarios.find(scenario => scenario.name === "screenshot-seed");
        if (!seed) throw new Error(`No shared screenshot seed for ${captureName}.`);
        await runGallerySteps(page, seed.steps);
    }
    await expect(page.getByRole("alert")).toBeHidden();
    await expect(page.locator("canvas")).toHaveCount(0);
    await page.evaluate(async () => {
        await document.fonts.ready;
        for (const node of document.querySelectorAll(".xui-scrollview")) node.scrollTop = 0;
        document.activeElement?.blur();
    });
    const geometry = await page.evaluate(() => ({
        nodes: document.querySelectorAll("#app .xui-node").length,
        nativeInputs: document.querySelectorAll("#app input").length,
        overflow: [...document.querySelectorAll(".xui-scrollview")].some(node => node.scrollHeight > node.clientHeight)
    }));
    expect(geometry.nodes).toBeGreaterThan(0);
    if (!captureApp.allowScroll)
        expect(geometry.overflow, "Capture viewport must display the actual full sample, not clipped scroll content.").toBe(false);
    const output = resolve(process.env.XUI_CAPTURE_DIR ??
        fileURLToPath(new URL("../../../../build/browser-captures/", import.meta.url)));
    await mkdir(output, { recursive: true });
    const filename = resolve(output, `${captureName}-web.png`);
    const png = await page.screenshot({ path: filename, fullPage: true, animations: "disabled" });
    expect(png.subarray(0, 8).toString("hex")).toBe("89504e470d0a1a0a");
    const source = await readFile(new URL(`../SharedDemo/${captureApp.source}`, import.meta.url));
    const metadata = {
        app: captureName, project: captureApp.project, source: captureApp.source,
        sourceSha256: createHash("sha256").update(source).digest("hex"),
        pngSha256: createHash("sha256").update(png).digest("hex"),
        browser: browser.version(), channel: process.env.XUI_BROWSER_CHANNEL ?? "chromium",
        viewport: page.viewportSize(), url: page.url(), offlineCallbacks: true, ...geometry
    };
    await writeFile(resolve(output, `${captureName}-web.json`), JSON.stringify(metadata, null, 2) + "\n");
    await testInfo.attach(`${captureName}-web`, { path: filename, contentType: "image/png" });
});
