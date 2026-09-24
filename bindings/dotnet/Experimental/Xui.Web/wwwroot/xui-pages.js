const maximumId = 9223372036854775707n;
export function pageId(value) {
    if (typeof value !== "string" || value.length > 19 || !/^[1-9][0-9]*$/.test(value) || BigInt(value) > maximumId)
        throw new TypeError("Invalid exact page identity.");
}
export function validatePageSnapshot(snapshot) {
    if (!snapshot || Object.keys(snapshot).sort().join(",") !== "items,selected" ||
        !Array.isArray(snapshot.items) || snapshot.items.length > 4096) throw new TypeError("Invalid page snapshot.");
    const ids = new Set();
    for (const entry of snapshot.items) {
        pageId(entry.id);
        if (ids.has(entry.id) || typeof entry.title !== "string" || !entry.title.trim() ||
            entry.title.length > 1024 || entry.title.includes("\0") || typeof entry.enabled !== "boolean")
            throw new TypeError("Invalid page entry.");
        ids.add(entry.id);
    }
    if (snapshot.selected !== null && !snapshot.items.some(entry => entry.id === snapshot.selected && entry.enabled))
        throw new TypeError("The selected page must be present and enabled.");
}

export function createPageView(peer, composing) {
    const links = new Set();
    let repair = false;
    function focusLink(excluded = null) {
        return [...links].find(link => link.state.enabled && link.state.visible && link.node.isConnected &&
            !link.node.closest("[hidden],[inert]") && !excluded?.contains(link.node));
    }
    function rejectsHide(child, canRepair = true) {
        const focused = child.node.contains(document.activeElement);
        const editable = document.activeElement?.matches("input,textarea,select,[contenteditable='true']");
        if (composing(child.node) || (focused && (!canRepair || editable || !focusLink(child.node))))
            throw new Error("Finish editing or explicitly move focus before hiding or removing a retained page.");
    }
    return {
        links,
        validate(snapshot) {
            validatePageSnapshot(snapshot);
            const old = peer.state.pages;
            for (let index = 0; index < peer.children.length; index++) {
                const id = old.items[index]?.id;
                const retained = snapshot.items.find(entry => entry.id === id);
                if (!retained || !retained.enabled || snapshot.selected !== id) rejectsHide(peer.children[index]);
            }
        },
        validateVisibility(visible) {
            if (typeof visible !== "boolean") throw new TypeError("Page visibility must be boolean.");
            if (!visible) for (const child of peer.children) rejectsHide(child, false);
        },
        beforeRemove(child) { if (child.node.contains(document.activeElement)) repair = true; },
        apply() {
            const snapshot = peer.state.pages;
            peer.node.hidden = !peer.state.visible;
            peer.node.inert = !peer.state.visible;
            const tabs = [...links].find(link => link.state.kind === "TabStrip");
            for (let index = 0; index < peer.children.length; index++) {
                const child = peer.children[index], entry = snapshot.items[index];
                const active = !!entry && entry.enabled && entry.id === snapshot.selected && peer.state.visible;
                if (!active && child.node.contains(document.activeElement)) repair = true;
                child.presented = active;
                child.node.hidden = !active;
                child.node.inert = !active;
                child.node.setAttribute("role", tabs ? "tabpanel" : "region");
                if (entry) {
                    child.node.id = `${peer.node.id}-page-${entry.id}`;
                    child.node.setAttribute("aria-label", entry.title);
                    if (tabs) child.node.setAttribute("aria-labelledby", `${tabs.node.id}-tab-${entry.id}`);
                    else child.node.removeAttribute("aria-labelledby");
                }
            }
            if (repair) {
                const link = focusLink();
                if (link) {
                    link.pageSelector.apply();
                    link.pageSelector.focusTarget().focus();
                }
                repair = false;
            }
        },
        dispose() {
            for (const link of links) link.pageSelector.disconnect();
            links.clear();
        }
    };
}

export function createPageSelector(peer, send) {
    let pages = null;
    const entries = new Map();
    const list = document.createElement(peer.state.kind === "NavigationView" ? "ul" : "div");
    list.className = "xui-page-headers";
    if (peer.state.kind === "TabStrip") peer.node.setAttribute("role", "tablist");
    peer.node.append(list);
    const isTabs = peer.state.kind === "TabStrip";
    function enabled() {
        return (pages?.state.pages.items ?? []).map(entry => entries.get(entry.id)).filter(entry => entry && !entry.button.disabled);
    }
    function create(entry) {
        const wrapper = document.createElement(isTabs ? "div" : "li");
        wrapper.className = "xui-page-header";
        if (isTabs) wrapper.setAttribute("role", "presentation");
        const button = document.createElement("button");
        button.type = "button";
        button.id = `${peer.node.id}-tab-${entry.id}`;
        if (isTabs) button.setAttribute("role", "tab");
        const label = document.createElement("span");
        button.append(label);
        const click = () => send(!isTabs && pages?.state.pages.selected === entry.id ? "page-activate" : "page-select", entry.id);
        const keydown = event => {
            if (event.key === "Enter" || event.key === " ") {
                event.preventDefault();
                send("page-activate", entry.id);
                return;
            }
            const keys = isTabs ? ["ArrowLeft", "ArrowRight"] : ["ArrowUp", "ArrowDown"];
            if (![...keys, "Home", "End"].includes(event.key)) return;
            event.preventDefault();
            const items = enabled();
            const index = items.findIndex(item => item.button === button);
            const next = event.key === "Home" ? 0 : event.key === "End" ? items.length - 1 :
                (index + (event.key === keys[0] ? -1 : 1) + items.length) % items.length;
            items[next]?.button.focus();
            if (items[next]) send("page-select", items[next].id);
        };
        button.addEventListener("click", click);
        button.addEventListener("keydown", keydown);
        wrapper.append(button);
        let close = null;
        const closing = () => send("page-close", entry.id);
        if (isTabs) {
            close = document.createElement("button");
            close.type = "button";
            close.className = "xui-tab-close";
            close.textContent = "\u00d7";
            close.addEventListener("click", closing);
            wrapper.append(close);
        }
        return { id: entry.id, wrapper, button, label, close, dispose() {
            button.removeEventListener("click", click);
            button.removeEventListener("keydown", keydown);
            close?.removeEventListener("click", closing);
            wrapper.remove();
        } };
    }
    const selector = {
        validate(snapshot) { validatePageSnapshot(snapshot); },
        connect(target) {
            if (pages) throw new Error("The selector is already linked.");
            pages = target;
            pages.pageView.links.add(peer);
            selector.apply();
            pages.pageView.apply();
        },
        disconnect() { pages = null; },
        focusTarget() { return entries.get(pages?.state.pages.selected)?.button ?? enabled()[0]?.button ?? peer.node; },
        apply() {
            if (!pages) return;
            const snapshot = pages.state.pages;
            const focused = peer.node.contains(document.activeElement);
            for (const [id, item] of entries) {
                if (!snapshot.items.some(entry => entry.id === id)) { item.dispose(); entries.delete(id); }
            }
            for (let index = 0; index < snapshot.items.length; index++) {
                const entry = snapshot.items[index];
                let item = entries.get(entry.id);
                if (!item) { item = create(entry); entries.set(entry.id, item); }
                item.label.textContent = entry.title;
                item.button.title = entry.title;
                item.button.setAttribute("aria-label", entry.title);
                item.button.disabled = !entry.enabled || !peer.state.enabled;
                item.button.tabIndex = entry.id === snapshot.selected ? 0 : -1;
                item.button.setAttribute("aria-controls", `${pages.node.id}-page-${entry.id}`);
                if (isTabs) item.button.setAttribute("aria-selected", String(entry.id === snapshot.selected));
                else {
                    if (entry.id === snapshot.selected) item.button.setAttribute("aria-current", "page");
                    else item.button.removeAttribute("aria-current");
                }
                if (item.close) {
                    item.close.hidden = !peer.state.closable;
                    item.close.disabled = item.button.disabled;
                    item.close.tabIndex = entry.id === snapshot.selected ? 0 : -1;
                    item.close.setAttribute("aria-label", `Close ${entry.title}`);
                }
                if (list.children[index] !== item.wrapper) {
                    const before = list.children[index] ?? null;
                    if (item.wrapper.parentNode === list && typeof list.moveBefore === "function") list.moveBefore(item.wrapper, before);
                    else list.insertBefore(item.wrapper, before);
                }
            }
            peer.node.tabIndex = enabled().length === 0 ? 0 : -1;
            peer.node.classList.toggle("xui-navigation-collapsed", !peer.state.expanded);
            if (focused && !peer.node.contains(document.activeElement)) selector.focusTarget().focus();
        },
        dispose() {
            pages?.pageView.links.delete(peer);
            pages = null;
            for (const item of entries.values()) item.dispose();
            entries.clear();
        }
    };
    return selector;
}
