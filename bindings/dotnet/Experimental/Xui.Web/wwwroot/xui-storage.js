const prefix = "xui-app-";
const storeName = "documents";
function key(value) {
    if (typeof value !== "string" || !/^[a-z0-9_-]{1,128}$/.test(value))
        throw new TypeError("Storage keys require 1-128 lowercase ASCII letters, digits, hyphens, or underscores.");
}
function failure(error) {
    const denied = error instanceof DOMException && ["NotAllowedError", "SecurityError"].includes(error.name);
    const unsupported = error instanceof DOMException && error.name === "NotSupportedError";
    return { status: denied ? "Denied" : unsupported ? "Unsupported" : "Failed",
        error: error instanceof Error ? `${error.name}: ${error.message}` : String(error) };
}

export function createStorage(databaseName, maximumBytes = 1024 * 1024) {
    key(databaseName);
    if (!Number.isInteger(maximumBytes) || maximumBytes <= 0 || maximumBytes > 2147483647)
        throw new TypeError("Storage size limit must be a positive Int32.");
    const operations = new Map();
    let closed = false;

    return {
        run(id, operation, name, data) {
            if (closed) throw new Error("IndexedDB storage is closed.");
            if (!Number.isSafeInteger(id) || id < 1 || operations.has(id)) throw new TypeError("Invalid storage operation identity.");
            key(name);
            if (!["read", "write", "delete"].includes(operation)) throw new TypeError("Unknown storage operation.");
            if (operation === "write" && (!(data instanceof Uint8Array) || data.byteLength > maximumBytes))
                throw new TypeError("Storage requires bytes within its configured limit.");
            const snapshot = operation === "write" ? data.slice() : null;
            let factory;
            try { factory = globalThis.indexedDB; }
            catch (error) { return Promise.resolve(failure(error)); }
            if (!factory) return Promise.resolve({ status: "Unsupported" });
            return new Promise(resolve => {
                const state = { db: null, transaction: null, finished: false, cancelled: false, abortReason: null };
                function finish(result) {
                    if (state.finished) return;
                    state.finished = true;
                    operations.delete(id);
                    state.db?.close();
                    resolve(result);
                }
                state.stop = reason => {
                    if (state.finished) return;
                    const previousCancelled = state.cancelled;
                    const previousReason = state.abortReason;
                    state.cancelled = reason === "cancel";
                    state.abortReason = state.cancelled ? null : new Error("IndexedDB storage closed before commit.");
                    if (state.transaction) {
                        try { state.transaction.abort(); }
                        catch (error) {
                            // A completed transaction can still have its completion event queued.
                            if (error instanceof DOMException && error.name === "InvalidStateError") {
                                state.cancelled = previousCancelled;
                                state.abortReason = previousReason;
                            } else finish(failure(error));
                        }
                    } else finish(state.cancelled ? { status: "Cancelled" } : failure(state.abortReason));
                };
                operations.set(id, state);
                let request;
                try { request = factory.open(prefix + databaseName, 1); }
                catch (error) { finish(failure(error)); return; }
                request.onblocked = () => finish(failure(new Error("IndexedDB open was blocked by another database version.")));
                request.onerror = () => finish(failure(state.abortReason ?? request.error ?? new Error("IndexedDB open failed.")));
                request.onupgradeneeded = () => {
                    if (state.finished) { request.transaction.abort(); return; }
                    try {
                        if (!request.result.objectStoreNames.contains(storeName)) request.result.createObjectStore(storeName);
                    } catch (error) {
                        state.abortReason = error;
                        request.transaction.abort();
                    }
                };
                request.onsuccess = () => {
                    state.db = request.result;
                    if (state.finished) { state.db.close(); return; }
                    state.db.onversionchange = () => state.stop("close");
                    try {
                        const transaction = state.transaction = state.db.transaction(storeName, operation === "read" ? "readonly" : "readwrite");
                        let output = { status: "Completed" };
                        transaction.oncomplete = () => finish(output);
                        transaction.onabort = () => finish(state.cancelled ? { status: "Cancelled" } :
                            failure(state.abortReason ?? transaction.error ?? new Error("IndexedDB transaction aborted.")));
                        const store = transaction.objectStore(storeName);
                        const action = operation === "read" ? store.get(name) : operation === "write" ? store.put(snapshot, name) : store.delete(name);
                        if (operation === "read") action.onsuccess = () => {
                            const value = action.result;
                            if (value !== undefined && (!(value instanceof Uint8Array) || value.byteLength > maximumBytes)) {
                                state.abortReason = new Error("Stored data is malformed or exceeds the configured size limit.");
                                transaction.abort();
                            } else output = { status: "Completed", exists: value !== undefined, data: value?.slice() ?? new Uint8Array() };
                        };
                    } catch (error) {
                        if (state.transaction) {
                            state.abortReason = error;
                            state.transaction.abort();
                        } else finish(failure(error));
                    }
                };
            });
        },
        cancel(id) { operations.get(id)?.stop("cancel"); },
        close() {
            if (closed) return;
            closed = true;
            for (const state of [...operations.values()]) state.stop("close");
        }
    };
}
