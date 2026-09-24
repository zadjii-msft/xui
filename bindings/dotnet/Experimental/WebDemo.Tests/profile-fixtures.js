import { readFileSync } from "node:fs";
import { randomUUID } from "node:crypto";
import { test as base, expect, byId, runScenarioSteps } from "./fixtures.js";

export { expect, byId };
const corpus = JSON.parse(readFileSync(new URL("../SharedDemo/ProfileWorkspaceScenarios.json", import.meta.url), "utf8"));
if (corpus.version !== 1 || corpus.applications?.[0]?.id !== "profile-workspace")
    throw new Error("Expected the shared profile workspace corpus.");
export const profileApplication = corpus.applications[0];
export const test = base.extend({
    database: async ({ page, baseURL }, use) => {
        const name = `profile-test-${randomUUID()}`;
        await page.addInitScript(name => {
            window.profileIo = { opens: 0, reads: 0, writes: 0, deletes: 0 };
            const open = indexedDB.open;
            indexedDB.open = function (database, ...args) {
                if (database === `xui-app-${name}`) window.profileIo.opens++;
                return open.call(this, database, ...args);
            };
            for (const [method, count] of [["get", "reads"], ["put", "writes"], ["delete", "deletes"]]) {
                const original = IDBObjectStore.prototype[method];
                IDBObjectStore.prototype[method] = function (...args) {
                    if (this.transaction.db.name === `xui-app-${name}`) window.profileIo[count]++;
                    return original.apply(this, args);
                };
            }
        }, name);
        try { await use(name); }
        finally {
            await page.context().setOffline(false);
            await page.route("**/__profile-cleanup", route => route.fulfill({ contentType: "text/html", body: "<!doctype html><title>Owned cleanup</title>" }));
            await page.goto(new URL("/__profile-cleanup", baseURL).href);
            await page.evaluate(async name => {
                await new Promise((resolve, reject) => {
                    const request = indexedDB.deleteDatabase(`xui-app-${name}`);
                    request.onsuccess = resolve;
                    request.onerror = () => reject(request.error);
                    request.onblocked = () => reject(new Error("Owned Profile database cleanup is blocked."));
                });
            }, name);
        }
    }
});

export async function profile(page, database) {
    await page.goto(`./?app=profile-workspace&profile-store=${database}`);
    await expect(byId(page, "profile-title")).toBeVisible();
    await expect(byId(page, "profile-name").locator("input")).toBeVisible();
    expect(await page.evaluate(() => typeof window.xuiTest)).toBe("undefined");
    expect(await page.evaluate(() => window.profileIo)).toEqual({ opens: 0, reads: 0, writes: 0, deletes: 0 });
}

export async function runProfileSteps(page, steps) {
    for (const step of steps) {
        if (step.action === "click" && ["profile-save", "profile-load", "profile-delete"].includes(step.id)) {
            await page.evaluate(() => {
                window.profileOperationSettled = false;
                const status = document.querySelector('[data-xui-id="profile-status"]');
                const busy = document.querySelector('[data-xui-id="profile-busy"]');
                const observer = new MutationObserver(() => {
                    if (busy.hidden && !status.textContent.endsWith("in progress...")) {
                        window.profileOperationSettled = true;
                        observer.disconnect();
                    }
                });
                observer.observe(status, { childList: true, subtree: true });
                observer.observe(busy, { attributes: true, attributeFilter: ["hidden"] });
            });
            await runScenarioSteps(page, [step]);
            await page.waitForFunction(() => window.profileOperationSettled === true);
        } else await runScenarioSteps(page, [step]);
    }
}

export async function readStored(page, database) {
    return page.evaluate(async name => {
        const module = await import("./_content/Xui.Web/xui-storage.js");
        const store = module.createStorage(name);
        try {
            const result = await store.run(1, "read", "profile-draft-v1");
            if (result.status !== "Completed") throw new Error(result.error ?? result.status);
            return { exists: result.exists, text: new TextDecoder().decode(result.data) };
        } finally { store.close(); }
    }, database);
}

export async function holdNextOpen(page, database) {
    await page.evaluate(name => {
        const open = indexedDB.open;
        window.releaseProfileOpen = null;
        indexedDB.open = function (database, ...args) {
            const request = open.call(this, database, ...args);
            if (database !== `xui-app-${name}`) return request;
            indexedDB.open = open;
            Object.defineProperty(request, "onsuccess", {
                set(callback) { request.addEventListener("success", event => {
                    window.releaseProfileOpen = () => { callback.call(request, event); window.releaseProfileOpen = null; };
                }, { once: true }); }
            });
            return request;
        };
    }, database);
}
