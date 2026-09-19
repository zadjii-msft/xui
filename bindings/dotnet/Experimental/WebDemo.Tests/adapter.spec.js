import { test, expect } from "./fixtures.js";

test.beforeEach(async ({ page }) => {
    await page.route("**/adapter-fixture", route => route.fulfill({
        contentType: "text/html",
        body: '<link rel="stylesheet" href="/_content/Xui.Web/xui-dom.css"><div id="mount" style="width:400px;height:300px"></div><div id="errors" hidden></div>'
    }));
    await page.goto("/adapter-fixture");
    await page.evaluate(async () => {
        window.adapter = await import("/_content/Xui.Web/xui-dom.js");
        window.surface = window.adapter.createSurface("mount", "errors");
        window.calls = [];
        window.callback = { invokeMethodAsync: async (...args) => { window.calls.push(args); return true; } };
        window.state = (kind, extra = {}) => ({
            kind, axis: kind === "Stack" ? "Vertical" : null, flex: 0, fixedSize: null, preferredSize: null,
            spacing: 0, padding: 0, name: kind, automationId: "", help: "",
            enabled: true, visible: true, text: "", placeholder: "", captionVisible: true, ...extra
        });
    });
});

test("serialized state and ownership fail explicitly at the boundary", async ({ page }) => {
    const results = await page.evaluate(() => {
        const failures = [];
        function rejects(action) {
            try { action(); failures.push(false); } catch (error) { failures.push(error instanceof Error); }
        }
        rejects(() => window.adapter.createSurface("mount", "errors"));
        rejects(() => surface.create(1, {}, callback));
        rejects(() => surface.create(1, state("Unknown"), callback));
        rejects(() => surface.create(1, state("Stack", { spacing: -1 }), callback));
        rejects(() => surface.create(1, state("Stack", { padding: NaN }), callback));
        rejects(() => surface.create(1, state("TextInput", { text: "a\0b" }), callback));
        rejects(() => surface.create(1, state("Stack", { fixedSize: { width: 2, height: 3, extra: 4 } }), callback));
        rejects(() => surface.create(1, state("Stack", { extra: 1 }), callback));
        surface.create(1, state("Stack"), callback);
        surface.create(2, state("Button"), callback);
        rejects(() => surface.create(2, state("Button"), callback));
        rejects(() => surface.mount(1));
        surface.addChild(1, 2);
        rejects(() => surface.addChild(1, 2));
        rejects(() => surface.addChild(2, 1));
        rejects(() => surface.update(2, "__proto__", {}, 0));
        rejects(() => surface.update(2, "Text", "bad", 1));
        rejects(() => surface.update(2, "Enabled", "false", 0));
        surface.mount(1);
        rejects(() => surface.addChild(1, 2));
        surface.unmount();
        rejects(() => surface.update(2, "Name", "late", 0));
        surface.destroy(2);
        surface.destroy(1);
        return failures;
    });
    expect(results).toHaveLength(17);
    expect(results.every(Boolean)).toBe(true);
});

test("queued input events retain order and programmatic text invalidates late echoes", async ({ page }) => {
    await page.evaluate(() => {
        surface.create(1, state("Stack"), callback);
        surface.create(2, state("TextInput"), callback);
        surface.addChild(1, 2);
        surface.mount(1);
        const input = document.querySelector("input");
        input.value = "old";
        input.dispatchEvent(new Event("input"));
        surface.update(2, "Text", "programmatic", 1);
        input.dispatchEvent(new Event("input"));
    });
    await expect.poll(() => page.evaluate(() => window.calls.length)).toBe(0);
    await page.evaluate(() => {
        const input = document.querySelector("input");
        input.value = "first";
        input.dispatchEvent(new Event("input"));
        input.value = "second";
        input.dispatchEvent(new Event("input"));
        input.dispatchEvent(new KeyboardEvent("keydown", { key: "Enter" }));
    });
    await expect.poll(() => page.evaluate(() => window.calls)).toEqual([
        ["Deliver", "change", "first", 1], ["Deliver", "change", "second", 1], ["Deliver", "submit", null, 1]
    ]);
});

test("unmount precedes peer cleanup, queued events stop, and duplicate IDs stay distinct", async ({ page }) => {
    const result = await page.evaluate(async () => {
        surface.create(1, state("Stack"), callback);
        surface.create(2, state("Button", { automationId: "same" }), callback);
        surface.create(3, state("Button", { automationId: "same" }), callback);
        surface.addChild(1, 2);
        surface.addChild(1, 3);
        surface.mount(1);
        const buttons = [...document.querySelectorAll("button")];
        buttons[0].click();
        surface.unmount();
        const unmounted = !document.querySelector("#mount").childNodes.length;
        surface.destroy(3);
        surface.destroy(2);
        surface.destroy(1);
        for (const button of buttons) button.click();
        await new Promise(resolve => setTimeout(resolve, 0));
        const next = window.adapter.createSurface("mount", "errors");
        next.create(1, state("Stack"), callback);
        next.create(2, state("Button", { automationId: "same" }), callback);
        next.addChild(1, 2);
        next.mount(1);
        const newId = document.querySelector("button").id;
        next.unmount();
        next.destroy(2);
        next.destroy(1);
        return { unmounted, calls: calls.length, ids: [buttons[0].id, buttons[1].id, newId] };
    });
    expect(result.unmounted).toBe(true);
    expect(result.calls).toBe(0);
    expect(new Set(result.ids).size).toBe(3);
});

test("async interop rejection reports an alert without an unhandled rejection", async ({ page, diagnostics }) => {
    diagnostics.expected.push(/XUI browser error: rejected interop/);
    await page.evaluate(() => {
        surface.create(1, state("Stack"), callback);
        surface.create(2, state("Button"), { invokeMethodAsync: () => Promise.reject(new Error("rejected interop")) });
        surface.addChild(1, 2);
        surface.mount(1);
        document.querySelector("button").click();
    });
    await expect(page.getByRole("alert")).toHaveText("XUI browser error: rejected interop");
});

test("CSS sizing, flex weights, hidden gaps, and unbounded scroll content use real layout", async ({ page }) => {
    const geometry = await page.evaluate(() => {
        surface.create(1, state("Stack", { spacing: 10, padding: 10 }), callback);
        surface.create(2, state("Label", { preferredSize: { width: 100, height: 30 }, fixedSize: { width: 80, height: 20 } }), callback);
        surface.create(3, state("Label", { visible: false, fixedSize: { width: 80, height: 80 } }), callback);
        surface.create(4, state("ScrollView", { flex: 1 }), callback);
        surface.create(5, state("Label", { flex: 2 }), callback);
        surface.create(6, state("Stack"), callback);
        surface.create(7, state("Label", { name: "Unbounded intrinsic text", flex: 1 }), callback);
        for (const id of [2, 3, 4, 5]) surface.addChild(1, id);
        surface.addChild(6, 7);
        surface.addChild(4, 6);
        surface.mount(1);
        const nodes = Object.fromEntries([...document.querySelectorAll(".xui-node")].map(node =>
            [node.id.split("-").at(-1), node.getBoundingClientRect().toJSON()]));
        return nodes;
    });
    expect(geometry[2].width).toBe(80);
    expect(geometry[2].height).toBe(20);
    expect(geometry[3].height).toBe(0);
    expect(geometry[4].y - geometry[2].bottom).toBe(10);
    expect(geometry[5].height / geometry[4].height).toBeCloseTo(2, 1);
    expect(geometry[7].height).toBeGreaterThan(0);
});
