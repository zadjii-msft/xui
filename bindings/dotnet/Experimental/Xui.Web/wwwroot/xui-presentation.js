const textKinds = new Set(["Label", "Button", "TextInput", "Toggle", "CheckBox"]);
export function validateTypography(kind, value) {
    if (value === null) return;
    if (!textKinds.has(kind) || !value || Object.keys(value).sort().join(",") !== "size,weight" ||
        typeof value.size !== "number" || !Number.isFinite(value.size) || value.size < 8 || value.size > 32 ||
        ![400, 700].includes(value.weight)) throw new TypeError("Unsupported native typography.");
}
export function applyTypography(node, value) {
    if (value === null) {
        node.style.removeProperty("font-size");
        node.style.removeProperty("font-weight");
    } else {
        node.style.fontSize = `${value.size / 16}rem`;
        node.style.fontWeight = String(value.weight);
    }
}
export function validateTheme(value) {
    if (value === null) return;
    if (!value || Object.keys(value).sort().join(",") !== "accent,background,foreground,mode" ||
        !["System", "Light", "Dark"].includes(value.mode)) throw new TypeError("Invalid native theme.");
    for (const role of ["foreground", "background", "accent"]) {
        const color = value[role];
        if (color !== null && (!color || Object.keys(color).sort().join(",") !== "dark,light" ||
            [color.light, color.dark].some(rgb => !Number.isInteger(rgb) || rgb < 0 || rgb > 0xffffff)))
            throw new TypeError("Theme colors must be RGB24 values.");
    }
}
export function createPresentation(mount, invalidate, report) {
    let theme = null, dark = null, forced = null;
    const properties = ["--xui-theme-foreground", "--xui-theme-background", "--xui-theme-accent", "color-scheme"];
    let original = null, hadClass = false;
    function render() {
        if (!theme) return;
        const useDark = forced.matches || theme.mode === "System" ? dark.matches : theme.mode === "Dark";
        mount.style.colorScheme = useDark ? "dark" : "light";
        for (const role of ["foreground", "background", "accent"]) {
            const color = forced.matches ? null : theme[role];
            if (color === null) mount.style.removeProperty(`--xui-theme-${role}`);
            else mount.style.setProperty(`--xui-theme-${role}`, `#${color[useDark ? "dark" : "light"].toString(16).padStart(6, "0")}`);
        }
        mount.classList.add("xui-theme-surface");
        invalidate();
    }
    const changed = () => { try { render(); } catch (error) { report(error); } };
    function restore() {
        if (original === null) return;
        dark?.removeEventListener("change", changed);
        forced?.removeEventListener("change", changed);
        dark = forced = null;
        for (const [name, value, priority] of original) {
            if (value) mount.style.setProperty(name, value, priority);
            else mount.style.removeProperty(name);
        }
        if (!hadClass) mount.classList.remove("xui-theme-surface");
        original = null;
    }
    return {
        apply(value) {
            validateTheme(value);
            theme = value;
            if (value === null) { restore(); invalidate(); return; }
            if (original === null) {
                original = properties.map(name => [name, mount.style.getPropertyValue(name), mount.style.getPropertyPriority(name)]);
                hadClass = mount.classList.contains("xui-theme-surface");
            }
            if (!dark) {
                dark = matchMedia("(prefers-color-scheme: dark)");
                forced = matchMedia("(forced-colors: active)");
                dark.addEventListener("change", changed);
                forced.addEventListener("change", changed);
            }
            render();
        },
        dispose() { theme = null; restore(); }
    };
}
