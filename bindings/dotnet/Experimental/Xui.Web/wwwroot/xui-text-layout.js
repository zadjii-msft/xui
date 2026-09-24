export function validateTextLayout(kind, value, text = "") {
    if (value === null) return;
    if (kind !== "Label" || !value || Object.keys(value).sort().join(",") !== "maximumLines,overflow,wrapping" ||
        typeof value.wrapping !== "boolean" || !Number.isInteger(value.maximumLines) ||
        value.maximumLines < 0 || value.maximumLines > 32 ||
        !["Clip", "CharacterEllipsis"].includes(value.overflow) ||
        (value.wrapping && value.overflow !== "Clip") || (!value.wrapping && value.maximumLines !== 1))
        throw new TypeError("Unsupported native label layout.");
    if (!value.wrapping && /[\r\n\u0085\u2028\u2029]/.test(text))
        throw new TypeError("Explicit single-line labels cannot contain paragraph breaks.");
}
export function applyTextLayout(peer) {
    const value = peer.state.textLayout;
    const body = peer.labelBody;
    body.style.whiteSpace = value === null ? "" : value.wrapping ? "pre-wrap" : "pre";
    body.style.overflowWrap = value === null ? "" : value.wrapping ? "anywhere" : "normal";
    body.style.textOverflow = value?.overflow === "CharacterEllipsis" ? "ellipsis" : "clip";
    body.style.maxHeight = "";
    if (value?.wrapping && value.maximumLines > 0 && peer.node.isConnected) {
        const style = getComputedStyle(body);
        let height = parseFloat(style.lineHeight);
        if (!Number.isFinite(height)) {
            const probe = document.createElement("div");
            probe.inert = true;
            probe.setAttribute("aria-hidden", "true");
            Object.assign(probe.style, {
                position: "fixed", left: "-100000px", top: "0", visibility: "hidden",
                whiteSpace: "pre", font: style.font, lineHeight: style.lineHeight,
                fontFeatureSettings: style.fontFeatureSettings, fontVariationSettings: style.fontVariationSettings
            });
            probe.textContent = "M\nM";
            peer.node.ownerDocument.body.append(probe);
            try { height = probe.getBoundingClientRect().height / 2; }
            finally { probe.remove(); }
        }
        if (!Number.isFinite(height) || height <= 0) throw new Error("Could not measure a native label line height.");
        body.style.maxHeight = `${height * value.maximumLines}px`;
    }
}
