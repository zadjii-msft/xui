import { hasComposition, reportError } from "./_content/Xui.Web/xui-dom.js";

export function bindBack(callback, mountId, errorId) {
    const mount = document.getElementById(mountId);
    if (!mount) throw new Error("Missing workspace mount.");
    const keydown = event => {
        if (event.defaultPrevented || event.key !== "ArrowLeft" || !event.altKey || event.ctrlKey || event.metaKey ||
            !mount.contains(event.target) || event.target instanceof HTMLSelectElement) return;
        event.preventDefault();
        if (event.isComposing || hasComposition(mountId)) {
            mount.dataset.xuiBackHandled = "true";
            mount.dataset.xuiBackBlocked = "true";
            return;
        }
        try {
            const result = callback.invokeMethod("Back");
            mount.dataset.xuiBackHandled = String(result.handled);
            mount.dataset.xuiBackBlocked = String(result.blocked);
            if (!result.handled) history.back();
        } catch (error) {
            mount.dataset.xuiBackHandled = "true";
            mount.dataset.xuiBackBlocked = "true";
            reportError(errorId, error);
        }
    };
    document.addEventListener("keydown", keydown);
    mount.dataset.xuiStudioHostReady = "true";
    return { dispose() {
        document.removeEventListener("keydown", keydown);
        callback = null;
        delete mount.dataset.xuiStudioHostReady;
    } };
}
