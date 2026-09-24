export function createLayout(mount, peers, callback, report) {
    let enabled = false, disposed = false, faulted = false, pending = 0;
    const measurements = new Map();
    const schedule = () => {
        if (!enabled || disposed || faulted || pending) return;
        pending = requestAnimationFrame(() => {
            pending = 0;
            if (!enabled || disposed) return;
            try { callback.invokeMethod("ReflowObserved"); }
            catch (error) { faulted = true; stop(); report(error); }
        });
    };
    const fontsChanged = () => { measurements.clear(); schedule(); };
    const stylesheetLoaded = event => {
        if (event.target instanceof HTMLLinkElement && event.target.relList.contains("stylesheet")) fontsChanged();
    };
    const resize = new ResizeObserver(fontsChanged);
    const ancestors = new MutationObserver(fontsChanged);
    const styles = new MutationObserver(fontsChanged);
    let resolution = null;
    const resolutionChanged = () => {
        resolution?.removeEventListener("change", resolutionChanged);
        resolution = matchMedia(`(resolution: ${devicePixelRatio}dppx)`);
        resolution.addEventListener("change", resolutionChanged);
        fontsChanged();
    };
    function stop() {
        resize.disconnect();
        ancestors.disconnect();
        styles.disconnect();
        resolution?.removeEventListener("change", resolutionChanged);
        resolution = null;
        document.fonts.removeEventListener("loadingdone", fontsChanged);
        document.removeEventListener("load", stylesheetLoaded, true);
        window.removeEventListener("resize", fontsChanged);
        if (pending) cancelAnimationFrame(pending);
        pending = 0;
    }
    function measureBox() {
        const box = document.createElement("div");
        box.inert = true;
        box.setAttribute("aria-hidden", "true");
        box.style.cssText = "position:fixed;left:-100000px;top:0;visibility:hidden;pointer-events:none;width:max-content;height:auto;";
        mount.append(box);
        return box;
    }
    function measurementDescription(source) {
        if (source.nodeType === Node.TEXT_NODE) return { text: source.textContent };
        if (!(source instanceof HTMLElement) || !["DIV", "SPAN", "LABEL", "BUTTON", "INPUT", "TEXTAREA", "PROGRESS", "NAV", "UL", "LI", "SELECT", "OPTION"].includes(source.tagName))
            throw new TypeError("Unsupported native measurement content.");
        const computed = getComputedStyle(source);
        const metrics = [
            "display", "box-sizing", "font-family", "font-size", "font-weight", "font-style", "font-stretch",
            "font-variant", "font-feature-settings", "font-variation-settings", "font-kerning", "font-optical-sizing",
            "line-height", "letter-spacing", "word-spacing", "text-transform", "text-indent", "white-space",
            "word-break", "overflow-wrap", "direction", "writing-mode", "text-orientation", "tab-size",
            "padding-top", "padding-right", "padding-bottom", "padding-left",
            "border-top-width", "border-right-width", "border-bottom-width", "border-left-width",
            "border-top-style", "border-right-style", "border-bottom-style", "border-left-style",
            "margin-top", "margin-right", "margin-bottom", "margin-left",
            "flex-direction", "flex-grow", "flex-shrink", "flex-basis", "gap", "align-items", "justify-content", "appearance", "text-overflow", "overflow", "list-style"
        ];
        return {
            tag: source.tagName,
            language: source.closest("[lang]")?.getAttribute("lang") ?? "",
            type: source instanceof HTMLInputElement ? source.type : null,
            maxHeight: source.style.maxHeight,
            styles: metrics.map(name => [name, computed.getPropertyValue(name)]),
            children: computed.display === "none" || source instanceof HTMLTextAreaElement ? [] : [...source.childNodes].map(measurementDescription)
        };
    }
    function measurementClone(description) {
        if (Object.hasOwn(description, "text")) return document.createTextNode(description.text);
        const clone = document.createElement(description.tag);
        if (description.language) clone.lang = description.language;
        for (const [name, value] of description.styles) clone.style.setProperty(name, value);
        clone.style.minWidth = "0";
        clone.style.minHeight = "0";
        if (description.maxHeight) clone.style.maxHeight = description.maxHeight;
        if (description.type !== null) {
            clone.type = description.type;
            clone.autocomplete = "off";
            clone.style.width = description.type === "text" ? "100%" : "auto";
        }
        if (description.tag === "PROGRESS") clone.style.width = "100%";
        if (description.tag === "TEXTAREA") { clone.rows = 4; clone.style.width = "100%"; }
        for (const child of description.children) clone.append(measurementClone(child));
        return clone;
    }
    return {
        invalidate() { measurements.clear(); schedule(); },
        enable(value) {
            if (enabled === value) return;
            if (value && !callback) throw new Error("Managed layout requires a .NET layout callback.");
            enabled = value;
            if (enabled) {
                faulted = false;
                resize.observe(mount);
                for (let node = mount; node; node = node.parentElement)
                    ancestors.observe(node, { attributes: true, attributeFilter: ["style", "class", "lang", "dir"] });
                styles.observe(document.head, { subtree: true, childList: true, characterData: true, attributes: true });
                document.fonts.addEventListener("loadingdone", fontsChanged);
                document.addEventListener("load", stylesheetLoaded, true);
                window.addEventListener("resize", fontsChanged);
                resolution = matchMedia(`(resolution: ${devicePixelRatio}dppx)`);
                resolution.addEventListener("change", resolutionChanged);
            } else {
                stop();
                measurements.clear();
                for (const peer of peers.values()) {
                    peer.node.classList.remove("xui-arranged", "xui-managed-container");
                    peer.layoutFrame = null;
                }
            }
        },
        viewport() { return { width: mount.clientWidth, height: mount.clientHeight }; },
        measure(id, mode, width) {
            const peer = peers.get(id);
            if (!peer || !Number.isFinite(width) || width < 0 || !["Unspecified", "AtMost", "Exactly"].includes(mode))
                throw new TypeError("Invalid native measurement request.");
            const description = measurementDescription(peer.node);
            const key = JSON.stringify([mode, width, description]);
            if (measurements.has(key)) return measurements.get(key);
            const clone = measurementClone(description);
            Object.assign(clone.style, {
                position: "relative", left: "0", top: "0", flex: "none", alignSelf: "start",
                width: mode === "Exactly" ? `${width}px` : "max-content", height: "auto",
                minWidth: "0", minHeight: "0", maxWidth: mode === "Unspecified" ? "none" : `${width}px`, maxHeight: "none"
            });
            const box = measureBox();
            try {
                box.append(clone);
                const bounds = clone.getBoundingClientRect();
                const result = { width: bounds.width, height: bounds.height };
                if (measurements.size >= 256) measurements.delete(measurements.keys().next().value);
                measurements.set(key, result);
                return result;
            } finally { box.remove(); }
        },
        scrollbarWidth() {
            const box = measureBox();
            try {
                Object.assign(box.style, { width: "100px", height: "100px", overflowY: "scroll" });
                return box.offsetWidth - box.clientWidth;
            } finally { box.remove(); }
        },
        apply(frames) {
            const seen = new Set();
            for (const frame of frames) {
                const peer = peers.get(frame.id);
                if (!peer || seen.has(frame.id) || typeof frame.container !== "boolean" || ["x", "y", "width", "height"].some(key =>
                    typeof frame[key] !== "number" || !Number.isFinite(frame[key]) || frame[key] < 0))
                    throw new TypeError("Invalid managed layout frame.");
                seen.add(frame.id);
            }
            for (const frame of frames) {
                const peer = peers.get(frame.id);
                const previous = peer.layoutFrame;
                if (previous && ["x", "y", "width", "height", "container"].every(key => previous[key] === frame[key])) continue;
                const node = peer.node;
                node.classList.add("xui-arranged");
                node.classList.toggle("xui-managed-container", frame.container);
                for (const key of ["x", "y", "width", "height"])
                    node.style.setProperty(`--xui-${key}`, `${frame[key]}px`);
                peer.layoutFrame = frame;
            }
        },
        dispose() { disposed = true; measurements.clear(); stop(); }
    };
}
