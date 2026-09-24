import { createLayout } from "./xui-layout.js";
import { createVirtualViewport } from "./xui-virtual.js";
import { validateTypography, applyTypography, validateTheme, createPresentation } from "./xui-presentation.js";
import { createPageView, createPageSelector, validatePageSnapshot, pageId } from "./xui-pages.js";
import { validateTextLayout, applyTextLayout } from "./xui-text-layout.js";
import { createImagePeer } from "./xui-image.js";
import { createReveal, validateReveal } from "./xui-reveal.js";

let nextSurface = 0;
const mounts = new WeakMap();
const composingInputs = new Set();
const virtualViewports = new Set();
const eventQueues = new WeakMap();
const kinds = new Set(["Stack", "Label", "Button", "TextInput", "ScrollView", "Toggle", "CheckBox", "Progress", "Grid", "MultilineText", "PasswordInput", "PageView", "TabStrip", "NavigationView", "SingleChoice", "Image", "Reveal"]);
const properties = {
    Name: "name", AutomationId: "automationId", Enabled: "enabled", Visible: "visible",
    Help: "help", Text: "text", Placeholder: "placeholder", CaptionVisible: "captionVisible",
    Spacing: "spacing", Padding: "padding", FixedSize: "fixedSize", PreferredSize: "preferredSize",
    Checked: "isChecked", CheckState: "checkState", ThreeState: "threeState",
    Range: "range", Value: "value", ProgressState: "progressState", ReadOnly: "readOnly", Typography: "typography",
    Pages: "pages", Expanded: "expanded", Closable: "closable", TextLayout: "textLayout", Choices: "choices", RevealState: "reveal"
};
const strings = new Set(["name", "automationId", "help", "text", "placeholder"]);
const booleans = new Set(["enabled", "visible", "captionVisible", "isChecked", "threeState", "trackInteraction", "readOnly", "expanded", "closable"]);
const lengths = new Set(["flex", "spacing", "padding"]);
const fields = new Set(["kind", "axis", "fixedSize", "preferredSize", "checkState", "range", "value", "progressState", "purpose", "maximumLength", "typography", "pages", "textLayout", "choices", "reveal",
    ...strings, ...booleans, ...lengths]);

function integer(value) {
    if (!Number.isInteger(value) || value < 0 || value > 2147483647) throw new TypeError("Expected a nonnegative Int32.");
}
function length(value) {
    if (typeof value !== "number" || !Number.isFinite(value) || value < 0)
        throw new TypeError("Expected a finite nonnegative length.");
}
function progressValue(value, range) {
    if (typeof value !== "number" || !Number.isFinite(value) || value < range.minimum || value > range.maximum)
        throw new TypeError("Progress value must be finite and within its range.");
}
function validate(key, value) {
    if (strings.has(key)) {
        if (typeof value !== "string" || value.includes("\0")) throw new TypeError(`Invalid ${key} text.`);
    } else if (booleans.has(key)) {
        if (typeof value !== "boolean") throw new TypeError(`Invalid ${key} flag.`);
    } else if (lengths.has(key)) length(value);
    else if (key === "fixedSize" || key === "preferredSize") {
        if (value === null) return;
        if (!value || Object.keys(value).sort().join(",") !== "height,width") throw new TypeError("Invalid size shape.");
        length(value.width);
        length(value.height);
    } else if (key === "kind") {
        if (!kinds.has(value)) throw new TypeError("Unsupported element kind.");
    } else if (key === "axis") {
        if (value !== null && value !== "Vertical" && value !== "Horizontal") throw new TypeError("Invalid stack axis.");
    } else if (key === "checkState") {
        if (!["Unchecked", "Checked", "Indeterminate"].includes(value)) throw new TypeError("Invalid check state.");
    } else if (key === "progressState") {
        if (!["Determinate", "Indeterminate"].includes(value)) throw new TypeError("Invalid progress state.");
    } else if (key === "value") {
        if (typeof value !== "number" || !Number.isFinite(value)) throw new TypeError("Invalid progress value.");
    } else if (key === "range") {
        if (!value || Object.keys(value).sort().join(",") !== "largeStep,maximum,minimum,smallStep" ||
            Object.values(value).some(entry => typeof entry !== "number" || !Number.isFinite(entry)) ||
            value.minimum >= value.maximum || !Number.isFinite(value.maximum - value.minimum) ||
            value.smallStep <= 0 || value.largeStep <= 0) throw new TypeError("Invalid numeric range.");
    } else if (key === "purpose") {
        if (!["Normal", "Email", "Url", "Telephone", "Number"].includes(value)) throw new TypeError("Invalid input purpose.");
    } else if (key === "maximumLength") {
        if (!Number.isInteger(value) || value < 1 || value > 1048576) throw new TypeError("Invalid editor length limit.");
    } else if (key === "typography") {
        validateTypography("TextInput", value);
    } else if (key === "pages") {
        validatePageSnapshot(value);
    } else if (key === "textLayout") {
        validateTextLayout("Label", value);
    } else if (key === "choices") {
        validateChoices(value);
    } else if (key === "reveal") {
        validateReveal(value);
    } else throw new TypeError(`Unknown state field: ${key}.`);
}
function validateChoices(value) {
    if (!value || !Array.isArray(value.items) || value.items.length > 4096) throw new TypeError("Invalid native choices.");
    const ids = new Set();
    for (const item of value.items) {
        pageId(item.id);
        if (ids.has(item.id) || typeof item.text !== "string" || item.text.includes("\0") || typeof item.enabled !== "boolean")
            throw new TypeError("Invalid choice entry.");
        ids.add(item.id);
    }
    if (value.selected !== null && !value.items.some(item => item.id === value.selected && item.enabled))
        throw new TypeError("The selected choice must exist and be enabled.");
}

function editorText(value, maximum, password) {
    if (typeof value !== "string" || value.length > maximum || value.includes("\0") ||
        (password && /[\r\n]/.test(value))) throw new TypeError("Native editor text violates its boundary.");
    for (let index = 0; index < value.length; index++) {
        const code = value.charCodeAt(index);
        if (code >= 0xd800 && code <= 0xdbff) {
            const next = value.charCodeAt(++index);
            if (!(next >= 0xdc00 && next <= 0xdfff)) throw new TypeError("Native editor text contains an incomplete UTF-16 sequence.");
        } else if (code >= 0xdc00 && code <= 0xdfff) throw new TypeError("Native editor text contains an incomplete UTF-16 sequence.");
    }
}

export function reportError(errorId, error, log = true) {
    const target = document.getElementById(errorId);
    if (!target) throw new Error(`Missing error surface: ${errorId}. ${String(error)}`);
    displayError(target, error, log);
}
export function hasComposition(mountId) {
    const mount = document.getElementById(mountId);
    if (!mount) throw new Error("Missing host mount.");
    return [...composingInputs].some(input => input.isConnected && mount.contains(input));
}
export async function waitForEvents(mountId) {
    const mount = document.getElementById(mountId);
    const queue = mount && eventQueues.get(mount);
    if (!queue) throw new Error("The event surface is not attached.");
    await queue();
}

function displayError(target, error, log = true) {
    const message = `XUI browser error: ${error instanceof Error ? error.message : String(error)}`;
    if (log) console.error(message);
    target.hidden = false;
    target.textContent = message;
    target.setAttribute("role", "alert");
}

export function bindLifetime(callback, errorId) {
    if (!callback || typeof callback.invokeMethod !== "function") throw new TypeError("Missing synchronous lifetime callback.");
    const errors = document.getElementById(errorId);
    if (!(errors instanceof HTMLElement)) throw new Error("Missing lifetime error surface.");
    const leave = event => {
        // The back-forward cache retains the live application, including its Wasm heap.
        if (event.persisted) return;
        try { callback.invokeMethod("PageHide"); }
        catch (error) { displayError(errors, error); }
    };
    window.addEventListener("pagehide", leave);
    return { dispose() { window.removeEventListener("pagehide", leave); } };
}

export function createSurface(mountId, errorId, layoutCallback = null) {
    validate("automationId", mountId);
    validate("automationId", errorId);
    const mount = document.getElementById(mountId);
    const errors = document.getElementById(errorId);
    if (!(mount instanceof HTMLElement) || !(errors instanceof HTMLElement) || mount === errors ||
        mount.contains(errors) || errors.contains(mount)) throw new Error("Distinct mount and error elements are required.");
    if (mounts.has(mount) || mount.childNodes.length) throw new Error("The DOM mount is already occupied.");
    const scope = `xui-${++nextSurface}`;
    const peers = new Map();
    const managed = createLayout(mount, peers, layoutCallback, error => displayError(errors, error));
    const presentation = createPresentation(mount, () => managed.invalidate(), error => displayError(errors, error));
    const viewportObservers = new Set();
    mounts.set(mount, scope);
    let active = false;
    let rootPeer = null;
    let disposed = false;
    let queue = Promise.resolve();
    eventQueues.set(mount, () => queue);

    function get(id) {
        integer(id);
        const peer = peers.get(id);
        if (!peer) throw new Error(`Unknown DOM peer: ${id}.`);
        return peer;
    }
    function usable() {
        if (disposed) throw new Error("The DOM backend is disposed.");
    }
    function accepts(peer) {
        for (let current = peer; current; current = current.parent)
            if (!current.state.enabled || !current.state.visible || current.presented === false ||
                (current.revealPeer && !current.state.reveal.open)) return false;
        return peer.alive && attached(peer);
    }
    function attached(peer) {
        let root = peer;
        while (root.parent) root = root.parent;
        return active && root === rootPeer;
    }
    function validateChild(parent, child) {
        if (child.parent || attached(child) || child === parent ||
            !["Stack", "ScrollView", "Grid", "PageView", "Reveal"].includes(parent.state.kind) ||
            (["ScrollView", "Reveal"].includes(parent.state.kind) && parent.children.length))
            throw new Error("Invalid DOM child ownership.");
        for (let ancestor = parent; ancestor; ancestor = ancestor.parent)
            if (ancestor === child) throw new Error("DOM child cycle.");
    }
    function validateMove(parent, child, index) {
        integer(index);
        if (child.parent !== parent || !parent.children.includes(child))
            throw new Error("The moved DOM child must belong to its parent.");
        if (typeof parent.node.moveBefore !== "function") {
            const composing = peer => peer.composing || peer.children.some(composing);
            if (composing(child)) throw new Error("This browser cannot move an input during composition. Retry after composition ends.");
        }
    }
    function enqueue(peer, kind, text = null) {
        if (!accepts(peer)) return;
        const revision = peer.revision;
        queue = queue.then(async () => {
            if (!accepts(peer) || (["change", "toggle", "check", "password", "choice"].includes(kind) && revision !== peer.revision)) return;
            await peer.callback.invokeMethodAsync("Deliver", kind, text, revision);
        }).catch(error => displayError(errors, error));
    }
    function enqueueInteraction(peer) {
        if (!peer.state.trackInteraction || !peer.alive || !attached(peer)) return;
        const focused = document.activeElement === peer.input;
        const composing = peer.composing;
        queue = queue.then(async () => {
            if (peer.alive && attached(peer)) await peer.callback.invokeMethodAsync("Interaction", focused, composing);
        }).catch(error => displayError(errors, error));
    }
    function signalViewports() {
        for (const viewport of virtualViewports) viewport.changed();
    }
    function markVirtualMutation(node) {
        for (const viewport of virtualViewports) viewport.markMutation(node);
    }
    function virtualOwner(peer) {
        for (let parent = peer.parent; parent; parent = parent.parent)
            if (parent.virtualViewport && !parent.virtualViewport.closed) return parent;
        throw new Error("The row has no live virtual viewport.");
    }
    function validateVirtualRows(owner, requested, count, pitch, sourceVersion) {
        if (requested.width === 0 || requested.height === 0 || count === 0) return;
        const content = owner.children[0].node;
        const origin = content.getBoundingClientRect();
        const first = Math.min(count, Math.floor(requested.offset / pitch));
        const end = Math.min(count, Math.ceil((requested.offset + requested.height) / pitch));
        const realized = new Map();
        const keys = new Set();
        for (const peer of peers.values()) {
            const info = peer.virtualItem;
            if (!info || !content.contains(peer.node) || info.sourceVersion !== sourceVersion) continue;
            if (info.count !== count || realized.has(info.index) || keys.has(info.key))
                throw new Error("Virtual row metadata does not identify one row per source index.");
            realized.set(info.index, peer);
            keys.add(info.key);
        }
        for (let index = first; index < end; index++) {
            const row = realized.get(index);
            if (!row || !row.node.isConnected) throw new Error(`Virtual viewport index ${index} is not realized.`);
            const bounds = row.node.getBoundingClientRect();
            if (Math.abs(bounds.top - origin.top - index * pitch) > 0.75 || Math.abs(bounds.height - pitch) > 0.75 ||
                bounds.width <= 0 || bounds.left < origin.left - 0.75 || bounds.right > origin.left + requested.width + 0.75)
                throw new Error(`Virtual viewport index ${index} has invalid native geometry.`);
        }
    }
    function listen(peer, node, name, handler) {
        node.addEventListener(name, handler);
        peer.listeners.push(() => node.removeEventListener(name, handler));
    }
    function focusTarget(peer) {
        if (peer.virtualViewport) return peer.virtualViewport.focusTarget;
        if (peer.pageSelector) return peer.pageSelector.focusTarget();
        if (peer.input) return peer.input;
        return ["Button", "ScrollView"].includes(peer.state.kind) ? peer.node : null;
    }
    function selectionInput(id) {
        const peer = get(id);
        if (peer.state.kind !== "TextInput" || !attached(peer))
            throw new Error("Native selection requires an attached text input.");
        return peer.input;
    }
    function passwordPeer(id, requireAttached = true) {
        const peer = get(id);
        if (peer.state.kind !== "PasswordInput" || (requireAttached && !attached(peer)))
            throw new Error("The operation requires a native password input.");
        return peer;
    }
    function writePassword(peer, value, revision) {
        integer(revision);
        if (revision !== peer.revision + 1) throw new Error("Invalid password revision.");
        editorText(value, peer.state.maximumLength, true);
        peer.revision = revision;
        peer.writingPassword = true;
        try { peer.input.value = value; }
        finally { peer.writingPassword = false; }
    }
    function clampSelection(input, start, end) {
        integer(start);
        integer(end);
        const text = input.value;
        [start, end] = [Math.min(start, end, text.length), Math.min(Math.max(start, end), text.length)];
        const splitsSurrogate = offset => offset > 0 && offset < text.length &&
            text.charCodeAt(offset - 1) >= 0xd800 && text.charCodeAt(offset - 1) <= 0xdbff &&
            text.charCodeAt(offset) >= 0xdc00 && text.charCodeAt(offset) <= 0xdfff;
        if (start === end && splitsSurrogate(start)) { start--; end--; }
        else {
            if (splitsSurrogate(start)) start--;
            if (splitsSurrogate(end)) end++;
        }
        return [start, end];
    }
    function layout(peer) {
        const { state: s, node } = peer;
        const size = s.fixedSize ?? s.preferredSize;
        node.style.width = size ? `${size.width}px` : "";
        node.style.height = size ? `${size.height}px` : "";
        node.style.alignSelf = s.fixedSize ? "start" : "";
        node.style.flexGrow = String(s.flex);
        node.style.flexShrink = "1";
        // Auto basis retains intrinsic size when the main axis is unbounded.
        node.style.flexBasis = s.flex > 0 ? "0%" : "auto";
        if (peer.parent?.state.kind === "ScrollView") node.style.flex = "0 0 auto";
        if (s.kind === "Stack") {
            node.style.gap = `${s.spacing}px`;
            node.style.padding = `${s.padding}px`;
        }
    }
    function updateProgress(peer) {
        const { state: s, progress } = peer;
        if (!progress) return;
        progress.max = 1;
        progress.setAttribute("aria-valuemin", String(s.range.minimum));
        progress.setAttribute("aria-valuemax", String(s.range.maximum));
        if (s.progressState === "Indeterminate") {
            progress.removeAttribute("value");
            progress.removeAttribute("aria-valuenow");
        } else {
            progress.value = (s.value - s.range.minimum) / (s.range.maximum - s.range.minimum);
            progress.setAttribute("aria-valuenow", String(s.value));
        }
        progress.classList.toggle("xui-progress-running", s.progressState === "Indeterminate" && accepts(peer));
    }
    function refreshProgress(peer) {
        updateProgress(peer);
        peer.revealPeer?.refresh();
        for (const child of peer.children) refreshProgress(child);
    }
    function apply(peer, key) {
        const { state: s, node, input, caption, progress } = peer;
        const target = input ?? progress ?? node;
        switch (key) {
            case "name":
                if (s.kind === "Label") {
                    peer.labelBody.textContent = s.name;
                    applyTextLayout(peer);
                } else if (s.kind === "Button") node.textContent = s.name;
                else if (peer.imagePeer) peer.imagePeer.image.alt = s.name;
                else if (caption) caption.textContent = s.name;
                if (s.kind !== "Stack") target.setAttribute("aria-label", s.name);
                break;
            case "automationId":
                node.dataset.xuiId = s.automationId;
                break;
            case "enabled":
                node.inert = !s.enabled;
                target.setAttribute("aria-disabled", String(!s.enabled));
                if (input || s.kind === "Button") target.disabled = !s.enabled;
                peer.pageSelector?.apply();
                refreshProgress(peer);
                break;
            case "visible":
                node.hidden = !s.visible;
                peer.pageView?.apply();
                refreshProgress(peer);
                break;
            case "help":
                target.title = s.help;
                if (s.help) target.setAttribute("aria-description", s.help);
                else target.removeAttribute("aria-description");
                break;
            case "text":
                if (input.value !== s.text) input.value = s.text;
                peer.lastValue = s.text;
                break;
            case "placeholder": input.placeholder = s.placeholder; break;
            case "captionVisible": caption.hidden = !s.captionVisible; break;
            case "isChecked": input.checked = s.isChecked; peer.lastValue = s.isChecked; break;
            case "checkState":
                input.checked = s.checkState === "Checked";
                input.indeterminate = s.checkState === "Indeterminate";
                peer.lastValue = s.checkState;
                break;
            case "threeState": break;
            case "readOnly": input.readOnly = s.readOnly; break;
            case "typography": applyTypography(node, s.typography); break;
            case "textLayout": applyTextLayout(peer); break;
            case "pages":
                peer.pageView?.apply();
                peer.pageSelector?.apply();
                refreshProgress(peer);
                break;
            case "expanded":
            case "closable": peer.pageSelector?.apply(); break;
            case "reveal":
                peer.revealPeer.apply(s.reveal);
                refreshProgress(peer);
                break;
            case "choices":
                input.replaceChildren(...s.choices.items.map(item => {
                    const option = document.createElement("option");
                    option.value = item.id;
                    option.textContent = item.text;
                    option.disabled = !item.enabled;
                    return option;
                }));
                if (s.choices.selected === null) input.selectedIndex = -1;
                else input.value = s.choices.selected;
                break;
            case "range":
            case "value":
            case "progressState": updateProgress(peer); break;
            default: layout(peer); break;
        }
    }
    return {
        validateImage() { usable(); if (typeof createImageBitmap !== "function") throw new Error("Native image decoding is unsupported."); },
        decodeImage(id, generation, bytes, plan) {
            usable();
            const peer = get(id);
            if (!peer.imagePeer) throw new Error("This peer is not a native image.");
            return peer.imagePeer.decode(generation, bytes, plan);
        },
        cancelImage(id) { usable(); get(id).imagePeer?.cancel(); },
        validateReveal(value) { usable(); validateReveal(value); },
        canRevealOpen(id, open) { usable(); return get(id).revealPeer.canSet(open); },
        revealPresentation(id) { usable(); return get(id).revealPeer.presentation(); },
        validateChoices(value) { usable(); validateChoices(value); },
        observeViewport(callback) {
            usable();
            let stopped = false;
            let previous = `${mount.clientWidth},${mount.clientHeight}`;
            const notify = () => {
                if (stopped || disposed) return;
                const width = mount.clientWidth, height = mount.clientHeight;
                const next = `${width},${height}`;
                if (next === previous) return;
                previous = next;
                try { callback.invokeMethod("Changed", width, height); }
                catch (error) { displayError(errors, error); }
            };
            const resize = new ResizeObserver(notify);
            const attributes = new MutationObserver(notify);
            resize.observe(mount, { box: "border-box" });
            for (let node = mount; node; node = node.parentElement)
                attributes.observe(node, { attributes: true, attributeFilter: ["style", "class"] });
            const observer = { dispose() {
                if (stopped) return;
                stopped = true;
                callback = null;
                resize.disconnect();
                attributes.disconnect();
                viewportObservers.delete(observer);
            } };
            viewportObservers.add(observer);
            return observer;
        },
        validatePages(id, snapshot) {
            usable();
            const peer = get(id);
            if (peer.pageView) peer.pageView.validate(snapshot);
            else if (peer.pageSelector) peer.pageSelector.validate(snapshot);
            else throw new Error("This peer is not a retained page host or selector.");
        },
        validatePageVisibility(id, visible) {
            usable();
            const peer = get(id);
            if (!peer.pageView) throw new Error("This peer is not a retained page host.");
            peer.pageView.validateVisibility(visible);
        },
        connectPages(id, pageId) {
            usable();
            const selector = get(id), pages = get(pageId);
            if (!selector.pageSelector || !pages.pageView) throw new Error("Invalid native page linkage.");
            selector.pageSelector.connect(pages);
        },
        validateTextLayout(id, value) { usable(); const peer = get(id); validateTextLayout(peer.state.kind, value, peer.state.name); },
        validateTheme(value) { usable(); validateTheme(value); },
        applyTheme(value) { usable(); presentation.apply(value); },
        validateTypography(kind, value) { usable(); validateTypography(kind, value); },
        passwordLength(id) {
            usable();
            const peer = passwordPeer(id);
            editorText(peer.input.value, peer.state.maximumLength, true);
            return peer.input.value.length;
        },
        readPassword(id) {
            usable();
            const peer = passwordPeer(id);
            editorText(peer.input.value, peer.state.maximumLength, true);
            return peer.input.value;
        },
        setPassword(id, value, revision) { usable(); writePassword(passwordPeer(id), value, revision); },
        clearPassword(id, revision) { usable(); writePassword(passwordPeer(id, false), "", revision); },
        managedLayout(enabled) { usable(); managed.enable(enabled); },
        viewport() { usable(); return managed.viewport(); },
        measureNative(id, mode, width) { usable(); const peer = get(id); if (peer.labelBody) applyTextLayout(peer); return managed.measure(id, mode, width); },
        measureNativeBatch(ids, modes, widths) {
            usable();
            if (!Array.isArray(ids) || !Array.isArray(modes) || !Array.isArray(widths) ||
                ids.length !== modes.length || ids.length !== widths.length)
                throw new TypeError("Invalid native measurement batch.");
            return ids.map((id, index) => { const peer = get(id); if (peer.labelBody) applyTextLayout(peer); return managed.measure(id, modes[index], widths[index]); });
        },
        scrollbarWidth() { usable(); return managed.scrollbarWidth(); },
        applyFrames(frames) { usable(); managed.apply(frames); },
        applyPackedFrames(ids, bounds, containers) {
            usable();
            if (!Array.isArray(ids) || !Array.isArray(bounds) || !Array.isArray(containers) ||
                bounds.length !== ids.length * 4 || containers.length !== ids.length)
                throw new TypeError("Invalid packed layout frame shape.");
            managed.apply(ids.map((id, index) => ({
                id, x: bounds[index * 4], y: bounds[index * 4 + 1],
                width: bounds[index * 4 + 2], height: bounds[index * 4 + 3], container: containers[index]
            })));
        },
        beginVirtualViewport(id, count, pitch, sourceVersion, callback) {
            usable();
            const peer = get(id);
            if (peer.state.kind !== "ScrollView" || !attached(peer) || peer.children.length !== 1)
                throw new Error("A virtual viewport requires an attached scroll view with one content root.");
            if (peer.virtualViewport) {
                if (!peer.virtualViewport.closed) throw new Error("The scroll view is already leased.");
                peer.virtualViewport.restore();
            }
            let viewport;
            viewport = createVirtualViewport(peer.node, peer.children[0].node, count, pitch, sourceVersion, callback, {
                composing: () => [...composingInputs].some(input => input.isConnected),
                composingWithin: node => [...composingInputs].some(input => input.isConnected && node.contains(input)),
                validate: (rect, itemCount, rowHeight, version) => validateVirtualRows(peer, rect, itemCount, rowHeight, version),
                deliver: (invoke, failed) => { queue = queue.then(invoke).catch(failed); return queue; },
                report: error => displayError(errors, error),
                closed: () => virtualViewports.delete(viewport)
            });
            peer.virtualViewport = viewport;
            virtualViewports.add(viewport);
            return viewport;
        },
        virtualContentSize(id) {
            usable();
            const viewport = get(id).virtualViewport;
            if (!viewport) throw new Error("The scroll view has no virtual lease.");
            return viewport.contentSize();
        },
        setVirtualItemInfo(id, info) {
            usable();
            const peer = get(id);
            if (!info || Object.keys(info).sort().join(",") !== "count,index,key,sourceVersion" ||
                typeof info.key !== "string" || !info.key || info.key.length > 4096 || info.key.includes("\0"))
                throw new TypeError("Invalid virtual item metadata.");
            for (let index = 0; index < info.key.length; index++) {
                const code = info.key.charCodeAt(index);
                if (code >= 0xd800 && code <= 0xdbff) {
                    const next = info.key.charCodeAt(++index);
                    if (!(next >= 0xdc00 && next <= 0xdfff)) throw new TypeError("A virtual key cannot contain an unpaired surrogate.");
                } else if (code >= 0xdc00 && code <= 0xdfff) throw new TypeError("A virtual key cannot contain an unpaired surrogate.");
            }
            integer(info.index);
            integer(info.count);
            if (info.index >= info.count) throw new RangeError("A virtual item index must be within its count.");
            const owner = virtualOwner(peer);
            owner.virtualViewport.validateItem(info);
            markVirtualMutation(peer.node);
            peer.virtualItem = Object.freeze({ ...info });
            peer.node.setAttribute("role", "listitem");
            peer.node.setAttribute("aria-posinset", String(info.index + 1));
            peer.node.setAttribute("aria-setsize", String(info.count));
            peer.node.dataset.xuiVirtualKey = info.key;
            peer.node.dataset.xuiVirtualSource = info.sourceVersion;
        },
        create(id, state, callback) {
            usable();
            integer(id);
            if (peers.has(id)) throw new Error("Duplicate DOM peer.");
            if (!state || Object.keys(state).length !== fields.size) throw new TypeError("Invalid element state shape.");
            for (const [key, value] of Object.entries(state)) validate(key, value);
            validateTypography(state.kind, state.typography);
            validateTextLayout(state.kind, state.textLayout, state.name);
            if (state.kind === "PasswordInput" && (state.maximumLength > 4096 || state.text !== ""))
                throw new TypeError("Password state cannot contain text or an invalid length limit.");
            if (state.kind === "MultilineText") {
                editorText(state.text, state.maximumLength, false);
                if (state.text.includes("\r")) throw new TypeError("Multiline state requires canonical LF text.");
            }
            if (["Grid", "Reveal"].includes(state.kind) && !layoutCallback) throw new Error("This control requires managed layout support.");
            progressValue(state.value, state.range);
            if (["Stack", "PageView"].includes(state.kind) !== (state.axis !== null)) throw new TypeError("Stack axis does not match kind.");
            if (!callback || typeof callback.invokeMethodAsync !== "function") throw new TypeError("Missing event callback.");
            const node = document.createElement(state.kind === "Button" ? "button" : state.kind === "NavigationView" ? "nav" : "div");
            node.className = `xui-node xui-${state.kind.toLowerCase()}`;
            node.id = `${scope}-${id}`;
            node.dataset.xuiKind = state.kind;
            const peer = { node, state: { ...state }, callback, revision: 0, alive: true,
                listeners: [], parent: null, children: [], input: null, caption: null, progress: null,
                virtualViewport: null, virtualItem: null, composing: false, writingPassword: false, lastValue: state.text,
                presented: true, pageView: null, pageSelector: null, labelBody: null, imagePeer: null, revealPeer: null };
            try {
                if (state.kind === "Reveal") {
                    node.setAttribute("role", "group");
                    peer.revealPeer = createReveal(peer, node => [...composingInputs].some(input => node.contains(input)),
                        () => {
                            if (!active || !attached(peer)) return false;
                            for (let item = peer; item; item = item.parent)
                                if (!item.state.enabled || !item.state.visible || item.presented === false ||
                                    (item !== peer && item.revealPeer && !item.state.reveal.open)) return false;
                            return true;
                        }, () => layoutCallback.invokeMethod("ReflowObserved"),
                        () => { for (const child of peer.children) refreshProgress(child); },
                        error => displayError(errors, error));
                }
                if (state.kind === "Image") peer.imagePeer = createImagePeer(node, state.name, () => managed.invalidate(), generation => {
                    queue = queue.then(async () => {
                        if (peer.alive && attached(peer)) await peer.callback.invokeMethodAsync("ImagePresentationFailed", generation);
                    }).catch(error => displayError(errors, error));
                });
                if (state.kind === "Label") {
                    peer.labelBody = document.createElement("span");
                    peer.labelBody.className = "xui-label-body";
                    node.append(peer.labelBody);
                }
                if (state.kind === "PageView") {
                    peer.pageView = createPageView(peer, node => [...composingInputs].some(input => node.contains(input)));
                    node.setAttribute("role", "group");
                }
                if (["TabStrip", "NavigationView"].includes(state.kind))
                    peer.pageSelector = createPageSelector(peer, (kind, id) => enqueue(peer, kind, id));
                if (state.kind === "Stack") node.style.flexDirection = state.axis === "Vertical" ? "column" : "row";
                if (state.kind === "Button") {
                    node.type = "button";
                    listen(peer, node, "click", () => enqueue(peer, "click"));
                }
                if (state.kind === "ScrollView") {
                    node.setAttribute("role", "region");
                    node.tabIndex = 0;
                }
                if (["TextInput", "MultilineText", "PasswordInput"].includes(state.kind)) {
                    peer.input = document.createElement(state.kind === "MultilineText" ? "textarea" : "input");
                    if (state.kind !== "MultilineText") peer.input.type = state.kind === "PasswordInput" ? "password" : "text";
                    peer.input.id = `${node.id}-input`;
                    if (state.kind === "TextInput") {
                        peer.caption = document.createElement("label");
                        peer.caption.htmlFor = peer.input.id;
                        node.append(peer.caption);
                        peer.input.inputMode = { Normal: "text", Email: "email", Url: "url", Telephone: "tel", Number: "decimal" }[state.purpose];
                        peer.input.autocomplete = { Normal: "off", Email: "email", Url: "url", Telephone: "tel", Number: "off" }[state.purpose];
                    } else peer.input.maxLength = state.maximumLength;
                    if (state.kind === "MultilineText") { peer.input.rows = 4; peer.input.readOnly = state.readOnly; }
                    if (state.kind === "PasswordInput") {
                        peer.input.autocomplete = "current-password";
                        for (const type of ["copy", "cut", "contextmenu"]) listen(peer, peer.input, type, event => event.preventDefault());
                    }
                    node.append(peer.input);
                    const changed = () => {
                        if (state.kind === "PasswordInput") {
                            if (!peer.writingPassword) enqueue(peer, "password");
                            return;
                        }
                        if (state.kind === "MultilineText" && peer.state.readOnly) return;
                        const text = peer.input.value;
                        if (state.kind === "MultilineText") {
                            try { editorText(text, peer.state.maximumLength, false); }
                            catch (error) { displayError(errors, error); return; }
                        }
                        if (text === peer.lastValue) return;
                        peer.lastValue = text;
                        enqueue(peer, "change", text);
                    };
                    listen(peer, peer.input, "input", changed);
                    listen(peer, peer.input, "focus", () => enqueueInteraction(peer));
                    listen(peer, peer.input, "blur", () => {
                        if (peer.composing) changed();
                        enqueueInteraction(peer);
                    });
                    listen(peer, peer.input, "compositionstart", () => {
                        peer.composing = true;
                        composingInputs.add(peer.input);
                        enqueueInteraction(peer);
                        signalViewports();
                    });
                    listen(peer, peer.input, "compositionend", () => {
                        peer.composing = false;
                        composingInputs.delete(peer.input);
                        changed();
                        enqueueInteraction(peer);
                        signalViewports();
                    });
                    listen(peer, peer.input, "keydown", event => {
                        if (state.kind === "TextInput" && event.key === "Enter" && !event.isComposing && event.keyCode !== 229) {
                            event.preventDefault();
                            enqueue(peer, "submit");
                        }
                    });
                }
                if (state.kind === "Toggle" || state.kind === "CheckBox") {
                    peer.input = document.createElement("input");
                    peer.input.type = "checkbox";
                    peer.input.id = `${node.id}-input`;
                    if (state.kind === "Toggle") peer.input.setAttribute("role", "switch");
                    peer.caption = document.createElement("label");
                    peer.caption.htmlFor = peer.input.id;
                    node.append(peer.input, peer.caption);
                    if (state.kind === "Toggle") {
                        listen(peer, peer.input, "change", () => {
                            if (!accepts(peer) || peer.input.checked === peer.lastValue) return;
                            peer.lastValue = peer.state.isChecked = peer.input.checked;
                            enqueue(peer, "toggle", String(peer.input.checked));
                        });
                    } else {
                        listen(peer, peer.input, "click", () => {
                            if (!accepts(peer)) return;
                            const next = peer.state.checkState === "Unchecked" ? "Checked" :
                                peer.state.checkState === "Checked" && peer.state.threeState ? "Indeterminate" : "Unchecked";
                            peer.state.checkState = next;
                            apply(peer, "checkState");
                            enqueue(peer, "check", next);
                        });
                    }
                }
                if (state.kind === "Progress") {
                    peer.caption = document.createElement("label");
                    peer.progress = document.createElement("progress");
                    peer.progress.id = `${node.id}-progress`;
                    peer.caption.htmlFor = peer.progress.id;
                    node.append(peer.caption, peer.progress);
                    updateProgress(peer);
                }
                if (state.kind === "SingleChoice") {
                    peer.input = document.createElement("select");
                    peer.input.id = `${node.id}-input`;
                    node.append(peer.input);
                    apply(peer, "choices");
                    listen(peer, peer.input, "change", () => enqueue(peer, "choice", peer.input.value));
                }
                for (const key of ["name", "automationId", "enabled", "visible", "help"]) apply(peer, key);
                apply(peer, "typography");
                if (state.kind === "TextInput") for (const key of ["text", "placeholder", "captionVisible"]) apply(peer, key);
                if (state.kind === "MultilineText") apply(peer, "text");
                if (state.kind === "Toggle") apply(peer, "isChecked");
                if (state.kind === "CheckBox") apply(peer, "checkState");
                layout(peer);
                peers.set(id, peer);
            } catch (error) {
                for (const remove of peer.listeners) remove();
                node.remove();
                throw error;
            }
        },
        addChild(parentId, childId) {
            usable();
            const parent = get(parentId), child = get(childId);
            if (attached(parent)) throw new Error("Use InsertChild to mutate a mounted parent.");
            validateChild(parent, child);
            child.parent = parent;
            parent.children.push(child);
            parent.node.append(child.node);
            if (parent.pageView) parent.pageView.apply();
            layout(child);
        },
        insertChild(parentId, index, childId) {
            usable();
            integer(index);
            const parent = get(parentId), child = get(childId);
            validateChild(parent, child);
            if (index > parent.children.length) throw new RangeError("DOM insertion index is out of range.");
            markVirtualMutation(parent.node);
            parent.node.insertBefore(child.node, parent.children[index]?.node ?? null);
            child.parent = parent;
            parent.children.splice(index, 0, child);
            if (parent.pageView) { child.presented = false; child.node.hidden = true; child.node.inert = true; }
            layout(child);
            refreshProgress(child);
        },
        removeChild(parentId, childId) {
            usable();
            const parent = get(parentId), child = get(childId);
            const index = parent.children.indexOf(child);
            if (child.parent !== parent || index < 0) throw new Error("The removed DOM child must belong to its parent.");
            parent.pageView?.beforeRemove(child);
            markVirtualMutation(parent.node);
            parent.children.splice(index, 1);
            child.parent = null;
            child.node.remove();
            refreshProgress(child);
        },
        validateMove(parentId, childId, index) {
            usable();
            validateMove(get(parentId), get(childId), index);
        },
        moveChild(parentId, childId, index) {
            usable();
            const parent = get(parentId), child = get(childId);
            validateMove(parent, child, index);
            if (index >= parent.children.length) throw new RangeError("DOM move index is out of range.");
            const previous = parent.children.indexOf(child);
            if (previous === index) return;
            markVirtualMutation(parent.node);
            const remaining = parent.children.filter(peer => peer !== child);
            const before = remaining[index]?.node ?? null;
            if (typeof parent.node.moveBefore === "function") parent.node.moveBefore(child.node, before);
            else {
                const focused = document.activeElement;
                const retainFocus = child.node.contains(focused);
                const selection = retainFocus && (focused instanceof HTMLInputElement || focused instanceof HTMLTextAreaElement)
                    ? [focused.selectionStart, focused.selectionEnd, focused.selectionDirection] : null;
                parent.node.insertBefore(child.node, before);
                if (retainFocus) {
                    focused.focus({ preventScroll: true });
                    if (selection) focused.setSelectionRange(...selection);
                }
            }
            parent.children.splice(previous, 1);
            parent.children.splice(index, 0, child);
        },
        tryFocus(id) {
            usable();
            const peer = get(id);
            const target = focusTarget(peer);
            if (!target || !accepts(peer) || !target.isConnected) return false;
            target.focus();
            return document.activeElement === target;
        },
        hasFocus(id) {
            usable();
            const target = focusTarget(get(id));
            return target !== null && document.activeElement === target;
        },
        getSelection(id) {
            usable();
            const input = selectionInput(id);
            return { start: input.selectionStart, end: input.selectionEnd };
        },
        setSelection(id, start, end) {
            usable();
            const input = selectionInput(id);
            input.setSelectionRange(...clampSelection(input, start, end));
        },
        update(id, property, value, revision) {
            usable();
            const peer = get(id);
            const key = Object.hasOwn(properties, property) ? properties[property] : null;
            if (!key) throw new TypeError("Unknown element property.");
            if (key === "range") {
                if (!value || Object.keys(value).sort().join(",") !== "range,value") throw new TypeError("Invalid range update.");
                validate("range", value.range);
                progressValue(value.value, value.range);
            } else validate(key, value);
            integer(revision);
            if (["text", "isChecked", "checkState", "threeState", "choices"].includes(key) ? revision !== peer.revision + 1 : revision !== peer.revision)
                throw new Error("Invalid value revision.");
            if (key === "text" && !["TextInput", "MultilineText"].includes(peer.state.kind))
                throw new TypeError("This property requires a text editor.");
            if (["captionVisible", "placeholder"].includes(key) && peer.state.kind !== "TextInput")
                throw new TypeError("This property requires a text input.");
            if (key === "readOnly" && peer.state.kind !== "MultilineText") throw new TypeError("ReadOnly requires multiline text.");
            if (key === "choices" && peer.state.kind !== "SingleChoice") throw new TypeError("Choices require a native select.");
            if (key === "typography") validateTypography(peer.state.kind, value);
            if (key === "textLayout") validateTextLayout(peer.state.kind, value, peer.state.name);
            if (key === "name" && peer.state.kind === "Label") validateTextLayout("Label", peer.state.textLayout, value);
            if (key === "text" && peer.state.kind === "MultilineText") {
                editorText(value, peer.state.maximumLength, false);
                if (value.includes("\r")) throw new TypeError("Multiline state requires canonical LF text.");
            }
            if (key === "isChecked" && peer.state.kind !== "Toggle") throw new TypeError("This property requires a toggle.");
            if (["checkState", "threeState"].includes(key) && peer.state.kind !== "CheckBox") throw new TypeError("This property requires a checkbox.");
            if (["range", "value", "progressState"].includes(key) && !peer.progress) throw new TypeError("This property requires progress.");
            if (key === "value") progressValue(value, peer.state.range);
            if (["spacing", "padding"].includes(key) && peer.state.kind !== "Stack")
                throw new TypeError("This property requires a stack.");
            markVirtualMutation(peer.node);
            if (key === "range") {
                peer.state.range = value.range;
                peer.state.value = value.value;
            } else peer.state[key] = value;
            peer.revision = revision;
            apply(peer, key);
        },
        mount(id) {
            usable();
            const root = get(id);
            if (active || root.parent || root.state.kind !== "Stack") throw new Error("Invalid DOM root.");
            const visited = new Set();
            function visit(peer) { visited.add(peer); for (const child of peer.children) visit(child); }
            visit(root);
            if (visited.size !== peers.size) throw new Error("The DOM tree has orphan peers.");
            root.node.classList.add("xui-root");
            mount.append(root.node);
            rootPeer = root;
            active = true;
            refreshProgress(root);
        },
        unmount() {
            if (disposed) return;
            active = false;
            let endedComposition = false;
            for (const peer of peers.values()) {
                peer.virtualViewport?.dispose();
                if (peer.input) endedComposition = composingInputs.delete(peer.input) || endedComposition;
            }
            if (endedComposition) signalViewports();
            if (rootPeer) refreshProgress(rootPeer);
            rootPeer = null;
            disposed = true;
            managed.dispose();
            presentation.dispose();
            for (const observer of [...viewportObservers]) observer.dispose();
            mount.replaceChildren();
            mounts.delete(mount);
            eventQueues.delete(mount);
        },
        destroy(id) {
            integer(id);
            const peer = peers.get(id);
            if (!peer) return; // Create can fail before it registers a peer.
            peer.alive = false;
            peer.revealPeer?.dispose();
            peer.imagePeer?.dispose();
            peer.pageSelector?.dispose();
            peer.pageView?.dispose();
            if (peer.state.kind === "PasswordInput") peer.input.value = "";
            peer.virtualViewport?.dispose();
            if (peer.input && composingInputs.delete(peer.input)) signalViewports();
            updateProgress(peer);
            for (const remove of peer.listeners) remove();
            peer.listeners.length = 0;
            peer.callback = null;
            peer.node.remove();
            if (peer.parent) {
                const index = peer.parent.children.indexOf(peer);
                if (index >= 0) peer.parent.children.splice(index, 1);
                peer.parent = null;
            }
            peers.delete(id);
        }
    };
}
