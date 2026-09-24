import { randomUUID } from "node:crypto";
import { test as base, expect, demo, command } from "./fixtures.js";

const test = base.extend({
    database: async ({ page }, use) => {
        const name = `test-${randomUUID()}`;
        await demo(page);
        try { await use(name); }
        finally {
            await page.evaluate(async name => {
                window.storageFixture?.close();
                await new Promise((resolve, reject) => {
                    const request = indexedDB.deleteDatabase(`xui-app-${name}`);
                    request.onsuccess = resolve;
                    request.onerror = () => reject(request.error);
                    request.onblocked = () => reject(new Error("Owned test database cleanup is blocked."));
                });
            }, name);
        }
    }
});

test("real C# IndexedDB preserves missing empty bytes persisted updates deletion and token cancellation", async ({ page, database }) => {
    await command(page, "storage-open", database);
    expect(await command(page, "storage-read", "document")).toEqual({ status: "Completed", exists: false, text: "", error: null });
    await command(page, "storage-write", "");
    expect(await command(page, "storage-read", "document")).toEqual({ status: "Completed", exists: true, text: "", error: null });
    const value = "Stored locally\nZo\u00eb \u674e";
    expect((await command(page, "storage-write", value)).status).toBe("Completed");
    expect(await command(page, "storage-cancel")).toEqual({ taskCancelled: true });
    expect((await command(page, "storage-read", "document")).text).toBe(value);
    await expect(command(page, "storage-write", "x".repeat(1025))).rejects.toThrow(/size limit/);
    await expect(command(page, "storage-read", "../bad")).rejects.toThrow(/Storage keys/);
    await command(page, "storage-close");
    await page.reload();
    await page.waitForFunction(() => typeof window.xuiTest === "function");
    await command(page, "storage-open", database);
    expect((await command(page, "storage-read", "document")).text).toBe(value);
    await command(page, "storage-write", "x".repeat(1024));
    expect((await command(page, "storage-read", "document")).text).toHaveLength(1024);
    expect((await command(page, "storage-delete", "document")).status).toBe("Completed");
    expect((await command(page, "storage-read", "document")).exists).toBe(false);
    expect((await command(page, "storage-delete", "document")).status).toBe("Completed");
    await command(page, "storage-close");
});

test("native transactions snapshot bytes abort before commit and close without partial replacement", async ({ page, database }) => {
    const result = await page.evaluate(async name => {
        const module = await import("/_content/Xui.Web/xui-storage.js");
        const store = window.storageFixture = module.createStorage(name, 8);
        const bytes = new Uint8Array([0, 128, 255]);
        const write = store.run(1, "write", "bytes", bytes);
        bytes.fill(1);
        await write;
        const read = await store.run(2, "read", "bytes");
        const originalPut = IDBObjectStore.prototype.put;
        let cancelled, closed;
        try {
            IDBObjectStore.prototype.put = function (...args) {
                const request = originalPut.apply(this, args);
                store.cancel(3);
                return request;
            };
            cancelled = await store.run(3, "write", "bytes", new Uint8Array([2]));
            IDBObjectStore.prototype.put = function (...args) {
                const request = originalPut.apply(this, args);
                store.close();
                return request;
            };
            closed = await store.run(4, "write", "bytes", new Uint8Array([3]));
        } finally { IDBObjectStore.prototype.put = originalPut; }
        let afterClose = false;
        try { store.run(5, "read", "bytes"); } catch (error) { afterClose = error.message.includes("closed"); }
        const reopened = window.storageFixture = module.createStorage(name, 8);
        const kept = await reopened.run(1, "read", "bytes");
        reopened.close();
        return { data: [...read.data], cancelled: cancelled.status, closed: closed.status, afterClose, kept: [...kept.data] };
    }, database);
    expect(result).toEqual({ data: [0, 128, 255], cancelled: "Cancelled", closed: "Failed", afterClose: true, kept: [0, 128, 255] });
});

test("IndexedDB quota failures malformed records and denied API never become empty success", async ({ page, database }) => {
    const result = await page.evaluate(async name => {
        const module = await import("/_content/Xui.Web/xui-storage.js");
        const store = window.storageFixture = module.createStorage(name, 8);
        await store.run(1, "write", "bytes", new Uint8Array([42]));
        const originalPut = IDBObjectStore.prototype.put;
        let quota;
        try {
            IDBObjectStore.prototype.put = function () { throw new DOMException("Controlled quota failure", "QuotaExceededError"); };
            quota = await store.run(2, "write", "bytes", new Uint8Array([9]));
        } finally { IDBObjectStore.prototype.put = originalPut; }
        const kept = await store.run(3, "read", "bytes");
        const small = module.createStorage(name, 1);
        await store.run(4, "write", "large", new Uint8Array([1, 2]));
        const oversized = await small.run(1, "read", "large");
        small.close();
        await new Promise((resolve, reject) => {
            const request = indexedDB.open(`xui-app-${name}`, 1);
            request.onerror = () => reject(request.error);
            request.onsuccess = () => {
                const db = request.result;
                const transaction = db.transaction("documents", "readwrite");
                transaction.objectStore("documents").put("not byte data", "malformed");
                transaction.oncomplete = () => { db.close(); resolve(); };
                transaction.onabort = () => { db.close(); reject(transaction.error); };
            };
        });
        const malformed = await store.run(6, "read", "malformed");
        const originalOpen = indexedDB.open;
        let denied;
        try {
            indexedDB.open = () => { throw new DOMException("Controlled denied database", "SecurityError"); };
            denied = await store.run(5, "read", "bytes");
        } finally { indexedDB.open = originalOpen; }
        store.close();
        return { quota: quota.status, quotaError: quota.error, kept: [...kept.data], oversized: oversized.status,
            malformed: malformed.status, denied: denied.status };
    }, database);
    expect(result).toMatchObject({ quota: "Failed", kept: [42], oversized: "Failed", malformed: "Failed", denied: "Denied" });
    expect(result.quotaError).toContain("QuotaExceededError");
});

test("default storage accepts exactly one MiB and rejects larger data without replacing it", async ({ page, database }) => {
    const result = await page.evaluate(async name => {
        const module = await import("/_content/Xui.Web/xui-storage.js");
        const store = window.storageFixture = module.createStorage(name);
        const data = new Uint8Array(1024 * 1024).fill(37);
        const written = await store.run(1, "write", "limit", data);
        let rejected = false;
        try { await store.run(2, "write", "limit", new Uint8Array(data.length + 1)); }
        catch (error) { rejected = error instanceof TypeError; }
        const read = await store.run(3, "read", "limit");
        store.close();
        return { status: written.status, rejected, length: read.data.length,
            first: read.data[0], last: read.data.at(-1) };
    }, database);
    expect(result).toEqual({ status: "Completed", rejected: true, length: 1048576, first: 37, last: 37 });
});

test("missing IndexedDB and denied access are explicit and do not create replacement stores", async ({ page, database }) => {
    const statuses = await page.evaluate(async name => {
        const module = await import("/_content/Xui.Web/xui-storage.js");
        const store = window.storageFixture = module.createStorage(name);
        const descriptor = Object.getOwnPropertyDescriptor(window, "indexedDB");
        try {
            Object.defineProperty(window, "indexedDB", { configurable: true, value: undefined });
            const unsupported = await store.run(1, "read", "document");
            Object.defineProperty(window, "indexedDB", { configurable: true,
                get() { throw new DOMException("Controlled API denial", "SecurityError"); } });
            const denied = await store.run(2, "read", "document");
            return [unsupported.status, denied.status];
        } finally {
            if (descriptor) Object.defineProperty(window, "indexedDB", descriptor);
            else delete window.indexedDB;
            store.close();
        }
    }, database);
    expect(statuses).toEqual(["Unsupported", "Denied"]);
});

test("schema creation failures abort the new database and allow an explicit later operation", async ({ page, database }) => {
    const result = await page.evaluate(async name => {
        const module = await import("/_content/Xui.Web/xui-storage.js");
        const store = window.storageFixture = module.createStorage(name);
        const create = IDBDatabase.prototype.createObjectStore;
        let failed;
        try {
            IDBDatabase.prototype.createObjectStore = () => { throw new DOMException("Schema quota fixture", "QuotaExceededError"); };
            failed = await store.run(1, "write", "document", new Uint8Array([1]));
        } finally { IDBDatabase.prototype.createObjectStore = create; }
        const written = await store.run(2, "write", "document", new Uint8Array([2]));
        const read = await store.run(3, "read", "document");
        store.close();
        return { failed: failed.status, error: failed.error, written: written.status, bytes: [...read.data] };
    }, database);
    expect(result).toMatchObject({ failed: "Failed", written: "Completed", bytes: [2] });
    expect(result.error).toContain("Schema quota fixture");
});
