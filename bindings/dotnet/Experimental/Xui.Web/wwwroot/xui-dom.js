let nextSurface = 0;
const mounts = new WeakMap();
const kinds = new Set(["Stack", "Label", "Button", "TextInput", "ScrollView"]);
const properties = {
    Name: "name", AutomationId: "automationId", Enabled: "enabled", Visible: "visible",
    Help: "help", Text: "text", Placeholder: "placeholder", CaptionVisible: "captionVisible",
    Spacing: "spacing", Padding: "padding", FixedSize: "fixedSize", PreferredSize: "preferredSize"
};
const strings = new Set(["name", "automationId", "help", "text", "placeholder"]);
const booleans = new Set(["enabled", "visible", "captionVisible"]);
const lengths = new Set(["flex", "spacing", "padding"]);
const fields = new Set(["kind", "axis", "fixedSize", "preferredSize", ...strings, ...booleans, ...lengths]);

function integer(value) {
    if (!Number.isInteger(value) || value < 0 || value > 2147483647) throw new TypeError("Expected a nonnegative Int32.");
}
function length(value) {
    if (typeof value !== "number" || !Number.isFinite(value) || value < 0)
        throw new TypeError("Expected a finite nonnegative length.");
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
    } else throw new TypeError(`Unknown state field: ${key}.`);
}

export function reportError(errorId, error) {
    const target = document.getElementById(errorId);
    if (!target) throw new Error(`Missing error surface: ${errorId}. ${String(error)}`);
    displayError(target, error);
}

function displayError(target, error) {
    const message = `XUI browser error: ${error instanceof Error ? error.message : String(error)}`;
    console.error(message);
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

export function createSurface(mountId, errorId) {
    validate("automationId", mountId);
    validate("automationId", errorId);
    const mount = document.getElementById(mountId);
    const errors = document.getElementById(errorId);
    if (!(mount instanceof HTMLElement) || !(errors instanceof HTMLElement) || mount === errors ||
        mount.contains(errors) || errors.contains(mount)) throw new Error("Distinct mount and error elements are required.");
    if (mounts.has(mount) || mount.childNodes.length) throw new Error("The DOM mount is already occupied.");
    const scope = `xui-${++nextSurface}`;
    mounts.set(mount, scope);
    const peers = new Map();
    let active = false;
    let disposed = false;
    let queue = Promise.resolve();

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
            if (!current.state.enabled || !current.state.visible) return false;
        return active && peer.alive;
    }
    function enqueue(peer, kind, text = null) {
        if (!accepts(peer)) return;
        const revision = peer.revision;
        queue = queue.then(async () => {
            if (!accepts(peer) || (kind === "change" && revision !== peer.revision)) return;
            await peer.callback.invokeMethodAsync("Deliver", kind, text, revision);
        }).catch(error => displayError(errors, error));
    }
    function listen(peer, node, name, handler) {
        node.addEventListener(name, handler);
        peer.listeners.push(() => node.removeEventListener(name, handler));
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
    function apply(peer, key) {
        const { state: s, node, input, caption } = peer;
        const target = input ?? node;
        switch (key) {
            case "name":
                if (s.kind === "Label" || s.kind === "Button") node.textContent = s.name;
                else if (input) caption.textContent = s.name;
                if (s.kind !== "Stack") target.setAttribute("aria-label", s.name);
                break;
            case "automationId":
                node.dataset.xuiId = s.automationId;
                break;
            case "enabled":
                node.inert = !s.enabled;
                target.setAttribute("aria-disabled", String(!s.enabled));
                if (input || s.kind === "Button") target.disabled = !s.enabled;
                break;
            case "visible": node.hidden = !s.visible; break;
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
            default: layout(peer); break;
        }
    }
    return {
        create(id, state, callback) {
            usable();
            if (active) throw new Error("The DOM tree is already mounted.");
            integer(id);
            if (peers.has(id)) throw new Error("Duplicate DOM peer.");
            if (!state || Object.keys(state).length !== fields.size) throw new TypeError("Invalid element state shape.");
            for (const [key, value] of Object.entries(state)) validate(key, value);
            if ((state.kind === "Stack") !== (state.axis !== null)) throw new TypeError("Stack axis does not match kind.");
            if (!callback || typeof callback.invokeMethodAsync !== "function") throw new TypeError("Missing event callback.");
            const node = document.createElement(state.kind === "Button" ? "button" : "div");
            node.className = `xui-node xui-${state.kind.toLowerCase()}`;
            node.id = `${scope}-${id}`;
            node.dataset.xuiKind = state.kind;
            const peer = { node, state: { ...state }, callback, revision: 0, alive: true,
                listeners: [], parent: null, children: [], input: null, caption: null, lastValue: state.text };
            try {
                if (state.kind === "Stack") node.style.flexDirection = state.axis === "Vertical" ? "column" : "row";
                if (state.kind === "Button") {
                    node.type = "button";
                    listen(peer, node, "click", () => enqueue(peer, "click"));
                }
                if (state.kind === "ScrollView") {
                    node.setAttribute("role", "region");
                    node.tabIndex = 0;
                }
                if (state.kind === "TextInput") {
                    peer.caption = document.createElement("label");
                    peer.input = document.createElement("input");
                    peer.input.type = "text";
                    peer.input.id = `${node.id}-input`;
                    peer.caption.htmlFor = peer.input.id;
                    node.append(peer.caption, peer.input);
                    const changed = () => {
                        const text = peer.input.value;
                        if (text === peer.lastValue) return;
                        peer.lastValue = text;
                        enqueue(peer, "change", text);
                    };
                    listen(peer, peer.input, "input", changed);
                    listen(peer, peer.input, "compositionend", changed);
                    listen(peer, peer.input, "keydown", event => {
                        if (event.key === "Enter" && !event.isComposing && event.keyCode !== 229) {
                            event.preventDefault();
                            enqueue(peer, "submit");
                        }
                    });
                }
                for (const key of ["name", "automationId", "enabled", "visible", "help"]) apply(peer, key);
                if (peer.input) for (const key of ["text", "placeholder", "captionVisible"]) apply(peer, key);
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
            if (active || child.parent || child === parent ||
                !["Stack", "ScrollView"].includes(parent.state.kind) ||
                (parent.state.kind === "ScrollView" && parent.children.length))
                throw new Error("Invalid DOM child ownership.");
            for (let ancestor = parent; ancestor; ancestor = ancestor.parent)
                if (ancestor === child) throw new Error("DOM child cycle.");
            child.parent = parent;
            parent.children.push(child);
            parent.node.append(child.node);
            layout(child);
        },
        update(id, property, value, revision) {
            usable();
            const peer = get(id);
            const key = Object.hasOwn(properties, property) ? properties[property] : null;
            if (!key) throw new TypeError("Unknown element property.");
            validate(key, value);
            integer(revision);
            if (key === "text" ? revision !== peer.revision + 1 : revision !== peer.revision)
                throw new Error("Invalid text revision.");
            if (["text", "captionVisible", "placeholder"].includes(key) && !peer.input)
                throw new TypeError("This property requires a text input.");
            if (["spacing", "padding"].includes(key) && peer.state.kind !== "Stack")
                throw new TypeError("This property requires a stack.");
            peer.state[key] = value;
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
            active = true;
        },
        unmount() {
            if (disposed) return;
            active = false;
            disposed = true;
            mount.replaceChildren();
            mounts.delete(mount);
        },
        destroy(id) {
            integer(id);
            const peer = peers.get(id);
            if (!peer) return; // Create can fail before it registers a peer.
            peer.alive = false;
            for (const remove of peer.listeners) remove();
            peer.listeners.length = 0;
            peer.callback = null;
            peer.node.remove();
            peers.delete(id);
        }
    };
}
