export function requested() { return new URLSearchParams(location.search).has("test"); }
export function install(reference) {
    window.xuiTest = (command, value = null) => reference.invokeMethodAsync("Command", command, value);
}
export function uninstall() { delete window.xuiTest; }
