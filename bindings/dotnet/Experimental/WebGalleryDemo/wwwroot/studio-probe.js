import { waitForEvents } from "./_content/Xui.Web/xui-dom.js";

export function install(callback) {
    window.xuiStudioIdle = async () => { await waitForEvents("app"); await callback.invokeMethodAsync("WaitForIdle"); };
}
export function uninstall() { delete window.xuiStudioIdle; }
