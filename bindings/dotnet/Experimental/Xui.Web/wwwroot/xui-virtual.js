const maximumInt64 = 9223372036854775807n;
function version(value, allowZero = false) {
    if (typeof value !== "string" || value.length > 19 || !/^(0|[1-9][0-9]*)$/.test(value))
        throw new TypeError("Viewport identities require exact decimal Int64 strings.");
    const parsed = BigInt(value);
    if (parsed > maximumInt64 || parsed < (allowZero ? 0n : 1n)) throw new RangeError("Viewport identity is outside Int64 range.");
    return parsed;
}
function length(value) {
    if (typeof value !== "number" || !Number.isFinite(value) || value < 0) throw new TypeError("Viewport geometry must be finite and nonnegative.");
    const result = Math.fround(value);
    if (!Number.isFinite(result)) throw new RangeError("Viewport geometry exceeds finite float coordinates.");
    return result;
}
function extent(count, pitch) {
    if (!Number.isInteger(count) || count < 0 || count > 2147483647) throw new TypeError("Invalid virtual item count.");
    const value = Math.fround(count * pitch);
    if (!Number.isFinite(value)) throw new RangeError("Virtual extent exceeds finite geometry.");
    return value;
}
function rectangle(offset, width, height, total) {
    return Object.freeze({ offset: length(offset), width: length(width), height: length(height), extent: length(total) });
}

export function createVirtualViewport(viewport, content, itemCount, rowHeight, sourceVersion, callback, hooks) {
    rowHeight = length(rowHeight);
    if (rowHeight === 0) throw new RangeError("Virtual row height must be positive.");
    let requestedVersion = version(sourceVersion);
    let requestedCount = itemCount;
    let requestedExtent = extent(itemCount, rowHeight);
    if (!callback || typeof callback.invokeMethodAsync !== "function") throw new TypeError("Missing viewport callback.");
    if (content.parentElement !== viewport || content.contains(document.activeElement) || hooks.composingWithin(content))
        throw new Error("Begin a viewport before focusing or composing in its content.");
    const savedOverflow = viewport.style.overflow;
    const savedPosition = viewport.style.position;
    const savedTabIndex = viewport.getAttribute("tabindex");
    const savedTransform = content.style.transform;
    const savedPointerEvents = content.style.pointerEvents;
    const savedMaxHeight = content.style.maxHeight;
    const intent = document.createElement("div");
    intent.className = "xui-virtual-intent";
    intent.tabIndex = 0;
    intent.setAttribute("aria-label", viewport.getAttribute("aria-label") ?? "List scrolling");
    const spacer = document.createElement("div");
    spacer.className = "xui-virtual-extent";
    spacer.setAttribute("aria-hidden", "true");
    const clip = document.createElement("div");
    clip.className = "xui-virtual-clip";
    clip.setAttribute("role", "list");
    clip.setAttribute("aria-label", viewport.getAttribute("aria-label") ?? "Virtual items");
    intent.append(spacer);
    let closed = false, faulted = false, pendingFrame = 0, inCallback = false, dirty = true;
    let epoch = 0n, committedVersion = 0n;
    let committedEpoch = 0n, committedCount = 0;
    let committed = rectangle(0, 0, 0, 0);
    let committedLayout = committed;
    let current = null, reservation = null;
    let wishedOffset = 0, nativeOffset = 0, explicitOffset = false;
    const composing = new Set();
    const listeners = [];
    const resize = new ResizeObserver(() => signal());
    const compositionRemoval = new MutationObserver(() => {
        let removed = false;
        for (const node of composing) if (!node.isConnected) { composing.delete(node); removed = true; }
        if (composing.size === 0) compositionRemoval.disconnect();
        if (removed) signal();
    });
    let reservationChanged = false;

    function alive() {
        if (closed || faulted) throw new Error("The virtual viewport lease is closed or faulted.");
    }
    function validateFlush(value) {
        alive();
        const expected = version(value);
        if (committedEpoch === 0n || expected !== committedEpoch || reservation)
            throw new Error("Flush requires the current committed viewport epoch without a reserved update.");
    }
    function listen(target, name, handler, options) {
        target.addEventListener(name, handler, options);
        listeners.push(() => target.removeEventListener(name, handler, options));
    }
    function blocked(source = requestedVersion) {
        for (const node of composing) if (!node.isConnected) composing.delete(node);
        return hooks.composing() || composing.size !== 0 ||
            (source !== committedVersion && content.contains(document.activeElement));
    }
    function snap(offset, total, height) {
        const probe = document.createElement("div");
        probe.inert = true;
        probe.setAttribute("aria-hidden", "true");
        probe.style.cssText = `position:fixed;left:-100000px;top:0;width:30px;height:${height}px;overflow-y:scroll;overflow-x:hidden;visibility:hidden;pointer-events:none;`;
        const fill = document.createElement("div");
        fill.style.height = `${total}px`;
        probe.append(fill);
        viewport.ownerDocument.body.append(probe);
        try {
            if (Math.abs(fill.getBoundingClientRect().height - total) > 1)
                throw new RangeError("The browser cannot represent this virtual extent.");
            probe.scrollTop = Math.max(0, Math.min(offset, total - height));
            return Math.fround(probe.scrollTop);
        } finally { probe.remove(); }
    }
    function sample() {
        const top = Math.fround(intent.scrollTop);
        if (top !== nativeOffset) {
            wishedOffset = top;
            nativeOffset = top;
            explicitOffset = false;
        }
        const width = intent.clientWidth, height = intent.clientHeight;
        if (width < committed.width || height < committed.height) {
            committed = rectangle(committed.offset, Math.min(width, committed.width), Math.min(height, committed.height), committed.extent);
            paint();
        }
        return rectangle(snap(wishedOffset, requestedExtent, height), width, height, requestedExtent);
    }
    function paint() {
        clip.style.width = `${committed.width}px`;
        clip.style.height = `${committed.height}px`;
        content.style.transform = `translateY(${-committed.offset}px)`;
        viewport.dataset.xuiCommittedOffset = String(committed.offset);
        viewport.dataset.xuiCommittedHeight = String(committed.height);
        viewport.dataset.xuiCommittedWidth = String(committed.width);
        viewport.dataset.xuiCommittedSource = committedVersion.toString();
    }
    function schedule() {
        if (closed || faulted || pendingFrame || inCallback || reservation || !dirty) return;
        pendingFrame = requestAnimationFrame(deliver);
    }
    function signal() {
        if (closed || faulted) return;
        dirty = true;
        if (!reservation) current = null;
        schedule();
    }
    function stop() {
        resize.disconnect();
        compositionRemoval.disconnect();
        for (const remove of listeners) remove();
        listeners.length = 0;
        if (pendingFrame) cancelAnimationFrame(pendingFrame);
        pendingFrame = 0;
    }
    function fail(error) {
        faulted = true;
        stop();
        intent.inert = true;
        hooks.report(error);
    }
    function deliver() {
        pendingFrame = 0;
        if (closed || faulted || reservation || inCallback || !dirty) return;
        try {
            const requested = sample();
            if (++epoch > maximumInt64) throw new RangeError("Viewport epoch exhausted Int64.");
            current = Object.freeze({
                epoch, sourceVersion: requestedVersion, count: requestedCount, requested,
                committed, committedVersion, blocked: blocked(), explicitOffset
            });
            const snapshot = current;
            dirty = false;
            inCallback = true;
            const invoke = () => {
                if (closed || faulted) return;
                return callback.invokeMethodAsync("Request", snapshot.epoch.toString(),
                    snapshot.committedVersion.toString(), snapshot.sourceVersion.toString(),
                    snapshot.committed, snapshot.requested, snapshot.blocked);
            };
            hooks.deliver(invoke, fail).finally(() => { inCallback = false; schedule(); });
        } catch (error) { fail(error); }
    }
    function requestOffset(offset) {
        alive();
        wishedOffset = length(offset);
        explicitOffset = true;
        const target = snap(wishedOffset, requestedExtent, intent.clientHeight);
        wishedOffset = target;
        if (requestedVersion === committedVersion) {
            intent.scrollTop = target;
            nativeOffset = Math.fround(intent.scrollTop);
        }
        signal();
    }
    function restoreLayout() {
        viewport.append(content);
        intent.remove();
        clip.remove();
        viewport.classList.remove("xui-virtual-viewport");
        viewport.style.overflow = savedOverflow;
        viewport.style.position = savedPosition;
        if (savedTabIndex === null) viewport.removeAttribute("tabindex");
        else viewport.setAttribute("tabindex", savedTabIndex);
        content.style.transform = savedTransform;
        content.style.pointerEvents = savedPointerEvents;
        content.style.maxHeight = savedMaxHeight;
    }
    const wheel = event => {
        if (closed || faulted || event.ctrlKey || event.deltaY === 0) return;
        const unit = event.deltaMode === 2 ? intent.clientHeight :
            event.deltaMode === 1 ? parseFloat(getComputedStyle(viewport).fontSize) * 1.2 : 1;
        event.preventDefault();
        const top = Math.fround(intent.scrollTop);
        if (top !== nativeOffset) wishedOffset = nativeOffset = top;
        requestOffset(Math.max(0, wishedOffset + event.deltaY * unit));
    };
    const focus = event => {
        if (!closed && !faulted && content.contains(event.target) && committed.height > 0) {
            const top = event.target.getBoundingClientRect().top - content.getBoundingClientRect().top;
            const rowTop = Math.floor(Math.max(0, top) / rowHeight) * rowHeight;
            if (rowTop < committed.offset) requestOffset(rowTop);
            else if (rowTop + rowHeight > committed.offset + committed.height)
                requestOffset(rowTop + rowHeight - committed.height);
        }
        signal();
    };
    const lease = {
        focusTarget: intent,
        get closed() { return closed; },
        changed: signal,
        contentSize() {
            const value = reservation?.requested ?? committedLayout;
            return { width: value.width, height: Math.max(value.height, value.extent) };
        },
        markMutation(node) {
            if (reservation && (node === content || content.contains(node))) reservationChanged = true;
        },
        validateItem(info) {
            alive();
            if (!reservation || version(info.sourceVersion) !== reservation.sourceVersion || info.count !== reservation.count)
                throw new Error("Virtual row metadata requires the reserved source snapshot.");
        },
        setExtent(count, source) {
            alive();
            const next = version(source);
            const total = extent(count, rowHeight);
            if (next <= requestedVersion)
                throw new Error("Virtual source versions must increase when the source changes.");
            requestedCount = count;
            requestedExtent = total;
            requestedVersion = next;
            signal();
        },
        requestOffset,
        tryBeginUpdate(value) {
            alive();
            const expected = version(value);
            if (!current || current.epoch !== expected) return "Superseded";
            if (current.blocked || blocked(current.sourceVersion)) return "Blocked";
            if (reservation) return reservation.epoch === expected ? "Ready" : "Superseded";
            reservation = current;
            reservationChanged = false;
            return "Ready";
        },
        tryCommit(value) {
            alive();
            const expected = version(value);
            if (!current || current.epoch !== expected) return "Superseded";
            if (!reservation) {
                if (current.blocked || blocked(current.sourceVersion)) return "Blocked";
                throw new Error("Reserve the viewport before committing it.");
            }
            if (blocked(reservation.sourceVersion)) throw new Error("Native input blocked an already reserved viewport; detach the attachment.");
            hooks.validate(reservation.requested, reservation.count, rowHeight, reservation.sourceVersion.toString());
            committed = reservation.requested;
            committedLayout = committed;
            committedVersion = reservation.sourceVersion;
            committedEpoch = reservation.epoch;
            committedCount = reservation.count;
            spacer.style.height = `${committed.extent}px`;
            if (reservation.explicitOffset && !dirty) {
                intent.scrollTop = committed.offset;
                nativeOffset = Math.fround(intent.scrollTop);
            } else if (explicitOffset && requestedVersion === committedVersion) {
                intent.scrollTop = snap(wishedOffset, requestedExtent, intent.clientHeight);
                nativeOffset = Math.fround(intent.scrollTop);
            }
            paint();
            reservation = null;
            current = null;
            schedule();
            return "Committed";
        },
        cancel(value) {
            alive();
            const expected = version(value);
            if (!current || current.epoch !== expected) return;
            if (reservation && reservationChanged)
                throw new Error("Native row staging already changed; detach instead of pretending to roll it back.");
            reservation = null;
            current = null;
            schedule();
        },
        validateFlush,
        flushCommitted(value) {
            validateFlush(value);
            hooks.validate(committed, committedCount, rowHeight, committedVersion.toString());
        },
        dispose() {
            if (closed) return;
            closed = true;
            stop();
            callback = null;
            intent.inert = true;
            clip.inert = true;
            clip.hidden = true;
            hooks.closed();
        },
        restore() {
            if (!closed) throw new Error("Close the old viewport before beginning a replacement.");
            if (content.contains(document.activeElement) || hooks.composingWithin(content))
                throw new Error("Release focused/composing content before replacing its viewport lease.");
            restoreLayout();
        }
    };
    try {
        viewport.append(intent, clip);
        clip.append(content);
        content.style.pointerEvents = "auto";
        content.style.maxHeight = "none";
        viewport.style.overflow = "clip";
        if (getComputedStyle(viewport).position === "static") viewport.style.position = "relative";
        viewport.removeAttribute("tabindex");
        viewport.classList.add("xui-virtual-viewport");
        snap(0, requestedExtent, intent.clientHeight);
        listen(intent, "scroll", () => {
            const next = Math.fround(intent.scrollTop);
            if (next === nativeOffset) return;
            wishedOffset = nativeOffset = next;
            explicitOffset = false;
            signal();
        });
        listen(viewport, "wheel", wheel, { passive: false });
        listen(document, "compositionstart", event => {
            composing.add(event.target);
            compositionRemoval.observe(document.documentElement, { childList: true, subtree: true });
            signal();
        }, true);
        listen(document, "compositionend", event => {
            composing.delete(event.target);
            if (composing.size === 0) compositionRemoval.disconnect();
            signal();
        }, true);
        listen(document, "focusin", focus, true);
        listen(document, "focusout", signal, true);
        resize.observe(intent);
        paint();
        signal();
    } catch (error) {
        closed = true;
        stop();
        callback = null;
        try { restoreLayout(); }
        catch (cleanup) { throw new AggregateError([error, cleanup], "Viewport construction and cleanup failed."); }
        throw error;
    }
    return lease;
}
