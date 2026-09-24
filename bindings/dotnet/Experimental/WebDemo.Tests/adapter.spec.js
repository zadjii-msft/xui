import { test, expect } from "./fixtures.js";
import { adapterFixture } from "./adapter-fixtures.js";

test.beforeEach(async ({ page }) => { await adapterFixture(page); });

test("native select retains exact wide identities disabled items and silent empty selection updates", async ({ page }) => {
    await page.evaluate(() => {
        surface.create(1, state("Stack"), callback);
        surface.create(2, state("SingleChoice", { choices: {
            items: [{ id: "1", text: "First", enabled: true }, { id: "9007199254740993", text: "Wide identity", enabled: true },
                { id: "3", text: "Disabled", enabled: false }], selected: "1"
        } }), callback);
        surface.addChild(1, 2);
        surface.mount(1);
    });
    await expect(page.locator("select option[value='3']")).toBeDisabled();
    await page.locator("select").selectOption("9007199254740993");
    await expect.poll(() => page.evaluate(() => calls)).toEqual([["Deliver", "choice", "9007199254740993", 0]]);
    await page.evaluate(() => {
        window.retainedSelect = document.querySelector("select");
        surface.update(2, "Choices", { items: [], selected: null }, 1);
    });
    expect(await page.locator("select").evaluate(node => ({ same: node === window.retainedSelect, selected: node.selectedIndex })))
        .toEqual({ same: true, selected: -1 });
    expect(await page.evaluate(() => calls.length)).toBe(1);
});

test("password events contain no value payload and serialized password text is rejected", async ({ page }) => {
    const result = await page.evaluate(async () => {
        let rejected = false;
        try { surface.create(9, state("PasswordInput", { maximumLength: 32, text: "not allowed" }), callback); }
        catch (error) { rejected = error instanceof TypeError; }
        surface.create(1, state("Stack"), callback);
        surface.create(2, state("PasswordInput", { maximumLength: 32 }), callback);
        surface.addChild(1, 2);
        surface.mount(1);
        const input = document.querySelector('input[type="password"]');
        input.value = "x".repeat(8);
        input.dispatchEvent(new Event("input"));
        await new Promise(resolve => setTimeout(resolve, 0));
        surface.clearPassword(2, 1);
        const length = input.value.length;
        surface.unmount();
        surface.destroy(2);
        surface.destroy(1);
        return { rejected, calls, length };
    });
    expect(result).toEqual({ rejected: true, calls: [["Deliver", "password", null, 0]], length: 0 });
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

    test("native value controls suppress stale revisions and stop indeterminate animation on detach", async ({ page }) => {
        const result = await page.evaluate(async () => {
            surface.create(1, state("Stack"), callback);
            surface.create(2, state("Toggle"), callback);
            surface.create(3, state("CheckBox", { checkState: "Indeterminate", threeState: true }), callback);
            surface.create(4, state("Progress", { range: { minimum: -10, maximum: 30, smallStep: 0.25, largeStep: 5 }, value: 20 }), callback);
            for (const id of [2, 3, 4]) surface.addChild(1, id);
            surface.mount(1);
            const toggle = document.querySelector(".xui-toggle input");
            const checkbox = document.querySelector(".xui-checkbox input");
            const progress = document.querySelector("progress");
            toggle.click();
            surface.update(2, "Checked", false, 1);
            toggle.dispatchEvent(new Event("change"));
            checkbox.click();
            surface.update(3, "CheckState", "Checked", 1);
            checkbox.dispatchEvent(new Event("change"));
            await new Promise(resolve => setTimeout(resolve, 0));
            const silent = calls.length === 0;
            surface.update(4, "Range", { range: { minimum: -2, maximum: 5, smallStep: 1, largeStep: 2 }, value: 5 }, 0);
            const clamped = [progress.getAttribute("aria-valuemin"), progress.getAttribute("aria-valuemax"),
                progress.getAttribute("aria-valuenow"), progress.position];
            surface.update(4, "ProgressState", "Indeterminate", 0);
            const running = progress.classList.contains("xui-progress-running");
            surface.update(4, "Visible", false, 0);
            const hiddenStopped = !progress.classList.contains("xui-progress-running");
            surface.update(4, "Visible", true, 0);
            toggle.click();
            checkbox.click();
            await new Promise(resolve => setTimeout(resolve, 0));
            const delivered = [...calls];
            surface.unmount();
            const stopped = !progress.classList.contains("xui-progress-running");
            for (const id of [4, 3, 2, 1]) surface.destroy(id);
            toggle.click();
            checkbox.click();
            await new Promise(resolve => setTimeout(resolve, 0));
            return { silent, clamped, running, hiddenStopped, stopped, delivered, calls };
        });
        expect(result).toEqual({
            silent: true, clamped: ["-2", "5", "5", 1], running: true, hiddenStopped: true, stopped: true,
            delivered: [["Deliver", "toggle", "true", 1], ["Deliver", "check", "Indeterminate", 1]],
            calls: [["Deliver", "toggle", "true", 1], ["Deliver", "check", "Indeterminate", 1]]
        });
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
    test("mutable insertion removal and future-index preflight preserve native ownership and reject stale events", async ({ page }) => {
        const result = await page.evaluate(async () => {
            surface.create(1, state("Stack"), callback);
            surface.create(2, state("TextInput", { text: "retained" }), callback);
            surface.addChild(1, 2);
            surface.mount(1);
            const input = document.querySelector("input");
            surface.create(3, state("Stack"), callback);
            surface.create(4, state("Button"), callback);
            surface.addChild(3, 4);
            surface.insertChild(1, 0, 3);
            const button = document.querySelector("button");
            surface.validateMove(1, 2, 10);
            let invalidRange = false;
            try { surface.moveChild(1, 2, 10); } catch (error) { invalidRange = error instanceof RangeError; }
            surface.moveChild(1, 2, 0);
            const order = [...input.parentElement.parentElement.children].map(node => node.id.split("-").at(-1));
            button.click();
            surface.removeChild(1, 3);
            button.click();
            await new Promise(resolve => setTimeout(resolve, 0));
            const inactive = calls.length === 0 && !button.isConnected;
            surface.destroy(4);
            surface.destroy(3);
            input.value = "current";
            input.dispatchEvent(new Event("input"));
            await new Promise(resolve => setTimeout(resolve, 0));
            surface.unmount();
            surface.destroy(2);
            surface.destroy(1);
            return { order, invalidRange, inactive, calls };
        });
        expect(result).toEqual({ order: ["2", "3"], invalidRange: true, inactive: true, calls: [["Deliver", "change", "current", 0]] });
    });

    test("30 native moves preserve focused input selection composition and DOM keyboard order", async ({ page }) => {
        const result = await page.evaluate(() => {
            surface.create(1, state("Stack"), callback);
            for (const id of [2, 3, 4]) {
                surface.create(id, state("TextInput", { name: `Input ${id}`, text: `value ${id}` }), callback);
                surface.addChild(1, id);
            }
            surface.mount(1);
            const inputs = [...document.querySelectorAll("input")];
            const input = inputs[0];
            const parent = input.parentElement.parentElement;
            if (typeof parent.moveBefore !== "function") throw new Error("This acceptance case requires state-preserving DOM moveBefore.");
            input.focus();
            input.setSelectionRange(1, 4, "backward");
            input.dispatchEvent(new CompositionEvent("compositionstart", { bubbles: true }));
            let retained = true;
            for (let iteration = 0; iteration < 30; iteration++) {
                const index = iteration % 2 === 0 ? 2 : 0;
                surface.validateMove(1, 2, index);
                surface.moveChild(1, 2, index);
                retained &&= document.activeElement === input && input.selectionStart === 1 && input.selectionEnd === 4 &&
                    input.selectionDirection === "backward" && inputs.every(node => node.isConnected);
            }
            input.dispatchEvent(new CompositionEvent("compositionend", { bubbles: true }));
            return { retained, value: input.value, order: [...parent.children].map(node => node.querySelector("input").getAttribute("aria-label")) };
        });
        expect(result).toEqual({ retained: true, value: "value 2", order: ["Input 2", "Input 3", "Input 4"] });
        await page.keyboard.press("Tab");
        await expect(page.getByRole("textbox", { name: "Input 3", exact: true })).toBeFocused();
    });

    test("fallback move rejects composition before mutation and restores native focus outside composition", async ({ page }) => {
        const result = await page.evaluate(() => {
            surface.create(1, state("Stack"), callback);
            for (const id of [2, 3]) {
                surface.create(id, state("TextInput", { text: "editing" }), callback);
                surface.addChild(1, id);
            }
            surface.mount(1);
            const input = document.querySelector("input");
            const parent = input.parentElement.parentElement;
            parent.moveBefore = undefined;
            input.focus();
            input.setSelectionRange(1, 3);
            input.dispatchEvent(new CompositionEvent("compositionstart", { bubbles: true }));
            let rejected = false;
            try { surface.validateMove(1, 2, 1); }
            catch (error) { rejected = error.message.includes("composition"); }
            const unchanged = parent.firstElementChild.contains(input) && document.activeElement === input;
            input.dispatchEvent(new CompositionEvent("compositionend", { bubbles: true }));
            surface.validateMove(1, 2, 1);
            surface.moveChild(1, 2, 1);
            return { rejected, unchanged, moved: parent.lastElementChild.contains(input),
                focused: document.activeElement === input, selection: [input.selectionStart, input.selectionEnd], value: input.value };
        });
        expect(result).toEqual({ rejected: true, unchanged: true, moved: true, focused: true, selection: [1, 3], value: "editing" });
    });
