export function validateReveal(value) {
    if (!value || Object.keys(value).sort().join(",") !== "direction,duration,open" ||
        typeof value.open !== "boolean" || !Number.isInteger(value.duration) || value.duration < 0 || value.duration > 400 ||
        !["Bottom", "Right"].includes(value.direction)) throw new TypeError("Invalid retained Reveal state.");
}
export function createReveal(peer, composing, eligible, layout, progressChanged, report) {
    let state = peer.state.reveal;
    let progress = state.open ? 1 : 0;
    let animating = false, frame = 0, start = 0, from = progress, target = progress, disposed = false;
    const reduced = matchMedia("(prefers-reduced-motion: reduce)");
    const forced = matchMedia("(forced-colors: active)");
    function paint() {
        peer.node.inert = !state.open;
        peer.node.setAttribute("aria-hidden", String(!state.open));
        peer.node.style.visibility = progress === 0 && !animating ? "hidden" : "";
        peer.node.dataset.xuiRevealProgress = String(progress);
        peer.node.dataset.xuiRevealAnimating = String(animating);
    }
    function stop() { if (frame) cancelAnimationFrame(frame); frame = 0; animating = false; }
    function settle() {
        stop();
        progress = state.open ? 1 : 0;
        paint();
        layout();
        progressChanged();
    }
    function tick(now) {
        frame = 0;
        if (disposed) return;
        try {
            if (!eligible() || reduced.matches || forced.matches) { settle(); return; }
            const elapsed = Math.min(1, Math.max(0, (now - start) / state.duration));
            progress = from + (target - from) * (1 - (1 - elapsed) ** 3);
            animating = elapsed < 1;
            paint();
            layout();
            progressChanged();
            if (animating) frame = requestAnimationFrame(tick);
        } catch (error) { stop(); report(error); }
    }
    const policy = () => { if (animating && (reduced.matches || forced.matches || !eligible())) settle(); };
    reduced.addEventListener("change", policy);
    forced.addEventListener("change", policy);
    paint();
    return {
        canSet(open) { return open || (!peer.node.contains(document.activeElement) && !composing(peer.node)); },
        presentation() { return { progress, animating }; },
        apply(next) {
            validateReveal(next);
            const sameMotion = state.duration === next.duration && state.direction === next.direction;
            if (state.open === next.open && sameMotion) return;
            stop();
            if (!sameMotion) progress = state.open ? 1 : 0;
            const changed = state.open !== next.open;
            state = next;
            target = next.open ? 1 : 0;
            if (!changed || next.duration === 0 || reduced.matches || forced.matches || !eligible()) { settle(); return; }
            from = progress;
            start = performance.now();
            animating = from !== target;
            paint();
            progressChanged();
            if (animating) frame = requestAnimationFrame(tick);
            else layout();
        },
        refresh() { policy(); },
        dispose() {
            if (disposed) return;
            disposed = true;
            stop();
            reduced.removeEventListener("change", policy);
            forced.removeEventListener("change", policy);
        }
    };
}
