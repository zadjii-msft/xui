const allowedSchemes = new Set(["http:", "https:", "mailto:", "tel:"]);

function result(status, value, error) {
    return { status, value, hasValue: status === "Completed", error };
}

function failure(error) {
    if (error instanceof DOMException) {
        if (error.name === "NotAllowedError" || error.name === "SecurityError") return result("Denied");
        if (error.name === "AbortError") return result("Cancelled");
        if (error.name === "NotSupportedError") return result("Unsupported");
    }
    return result("Failed", undefined, error instanceof Error ? `${error.name}: ${error.message}` : String(error));
}

export function getAvailability(capability) {
    switch (capability) {
        case "Clipboard":
            return isSecureContext && typeof navigator.clipboard?.readText === "function" &&
                typeof navigator.clipboard?.writeText === "function" ? "RequiresUserGesture" : "Unsupported";
        case "OpenUri": return typeof window.open === "function" ? "RequiresUserGesture" : "Unsupported";
        case "OpenFile":
        case "SaveFile": return "Unsupported";
        case "ApplicationStorage":
            try { return globalThis.indexedDB ? "Available" : "Unsupported"; }
            catch (error) {
                if (error instanceof DOMException && error.name === "SecurityError") return "Unsupported";
                throw error;
            }
        default: throw new TypeError(`Unknown browser capability: ${capability}.`);
    }
}

export async function readClipboard() {
    if (getAvailability("Clipboard") === "Unsupported") return result("Unsupported");
    if (!navigator.userActivation?.isActive) return result("Denied");
    try { return result("Completed", await navigator.clipboard.readText()); }
    catch (error) { return failure(error); }
}

export async function writeClipboard(text) {
    if (typeof text !== "string" || text.includes("\0")) throw new TypeError("Clipboard text must be a string without NUL.");
    if (getAvailability("Clipboard") === "Unsupported") return result("Unsupported");
    if (!navigator.userActivation?.isActive) return result("Denied");
    try {
        await navigator.clipboard.writeText(text);
        return result("Completed", true);
    } catch (error) { return failure(error); }
}

export function openUri(value) {
    if (typeof value !== "string" || /[\u0000-\u001f\u007f-\u009f]/.test(value))
        throw new TypeError("URI must be a string without control characters.");
    let uri;
    try { uri = new URL(value); }
    catch (error) {
        if (error instanceof TypeError) throw new TypeError("URI must be absolute.");
        throw error;
    }
    if (!allowedSchemes.has(uri.protocol) || (["http:", "https:"].includes(uri.protocol) && (uri.username || uri.password)))
        throw new TypeError("URI must use an allowed scheme without HTTP credentials.");
    if (getAvailability("OpenUri") === "Unsupported") return result("Unsupported");
    if (!navigator.userActivation?.isActive) return result("Denied");
    let popup;
    try {
        // Retain the blank handle to distinguish popup blocking; detach its opener before navigating.
        popup = window.open("about:blank", "_blank");
        if (!popup) return result("Denied");
        popup.opener = null;
        popup.location.replace(uri.href);
        return result("Completed", true);
    } catch (error) {
        if (popup) {
            try { popup.close(); }
            catch (cleanup) { return result("Failed", undefined, `${String(error)}; popup cleanup: ${String(cleanup)}`); }
        }
        return failure(error);
    }
}
