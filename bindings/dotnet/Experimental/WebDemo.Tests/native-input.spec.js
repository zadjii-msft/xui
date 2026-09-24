import { test, expect, demo, command, byId } from "./fixtures.js";

test.beforeEach(async ({ page }) => { await demo(page); });

test("public C# focus uses native input button and scroll targets without stealing focus for labels", async ({ page }) => {
    expect(await command(page, "input-focus", "input")).toEqual({ focused: true, actual: true });
    await expect(page.getByRole("textbox")).toBeFocused();
    expect(await command(page, "input-focus", "label")).toEqual({ focused: false, actual: false });
    await expect(page.getByRole("textbox")).toBeFocused();
    expect(await command(page, "input-focus", "button")).toEqual({ focused: true, actual: true });
    await expect(byId(page, "increment")).toBeFocused();
    expect(await command(page, "input-has-focus")).toEqual({ focused: false });
    expect(await command(page, "input-focus", "scroll")).toEqual({ focused: true, actual: true });
    await expect(byId(page, "content")).toBeFocused();
    await command(page, "count", "10");
    expect(await command(page, "input-focus", "button")).toEqual({ focused: false, actual: false });
    await command(page, "visible", "false");
    expect(await command(page, "input-focus", "input")).toEqual({ focused: false, actual: false });
    await command(page, "visible", "true");
    await command(page, "enabled", "false");
    expect(await command(page, "input-focus", "input")).toEqual({ focused: false, actual: false });
    await command(page, "enabled", "true");
    await page.locator("#app").evaluate(node => { node.inert = true; });
    expect(await command(page, "input-focus", "input")).toEqual({ focused: false, actual: false });
});

test("public C# selection reads actual native text and clamps UTF16 surrogate boundaries without value writes", async ({ page }) => {
    const input = page.getByRole("textbox");
    await input.fill("Model text");
    await expect.poll(async () => (await command(page, "state")).entry).toBe("Model text");
    await input.evaluate(node => {
        node.value = "A\uD83D\uDE00B";
        node.focus();
        window.nativeValueWrites = 0;
        window.nativeInputEvents = 0;
        node.addEventListener("input", () => { window.nativeInputEvents++; });
        const descriptor = Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, "value");
        Object.defineProperty(node, "value", {
            get() { return descriptor.get.call(this); },
            set(value) { window.nativeValueWrites++; descriptor.set.call(this, value); }
        });
    });
    for (const [range, expected] of [
        ["2,2", { start: 1, end: 1 }],
        ["2,3", { start: 1, end: 3 }],
        ["0,2", { start: 0, end: 3 }],
        ["3,1", { start: 1, end: 3 }],
        ["99,88", { start: 4, end: 4 }],
        ["0,2147483647", { start: 0, end: 4 }]
    ]) {
        expect(await command(page, "input-set-selection", range)).toEqual(expected);
        expect(await input.evaluate(node => ({ start: node.selectionStart, end: node.selectionEnd }))).toEqual(expected);
    }
    await input.evaluate(node => node.setSelectionRange(1, 3, "backward"));
    expect(await command(page, "input-get-selection")).toEqual({ start: 1, end: 3 });
    await expect(command(page, "input-set-selection", "-1,2")).rejects.toThrow(/nonnegative/);
    expect(await command(page, "input-get-selection")).toEqual({ start: 1, end: 3 });
    expect(await command(page, "input-focus", "button")).toEqual({ focused: true, actual: true });
    expect(await command(page, "input-set-selection", "0,1")).toEqual({ start: 0, end: 1 });
    await expect(byId(page, "increment")).toBeFocused();
    expect(await page.evaluate(() => [window.nativeValueWrites, window.nativeInputEvents])).toEqual([0, 0]);
    expect((await command(page, "state")).entry).toBe("Model text");
});

test("native input APIs reject detached hosts and use new peers after reattachment", async ({ page }) => {
    await command(page, "entry", "Retained");
    await command(page, "input-focus", "input");
    await command(page, "input-set-selection", "1,4");
    await page.getByRole("textbox").evaluate(node => { window.oldFocusInput = node; });
    await command(page, "detach");
    for (const [name, value] of [["input-focus", "input"], ["input-get-selection", null], ["input-set-selection", "0,1"]])
        await expect(command(page, name, value)).rejects.toThrow(/attached host/);
    await command(page, "attach");
    expect(await command(page, "input-focus", "input")).toEqual({ focused: true, actual: true });
    expect(await page.getByRole("textbox").evaluate(node => node !== window.oldFocusInput)).toBe(true);
    expect(await command(page, "input-set-selection", "1,4")).toEqual({ start: 1, end: 4 });
    await command(page, "dispose");
    await expect(command(page, "input-has-focus")).rejects.toThrow(/disposed/i);
});

test("public C# focus refuses progress and targets native choice inputs rather than wrappers", async ({ page }) => {
    await page.evaluate(() => {
        const mount = document.createElement("div");
        mount.id = "input-focus-probe";
        mount.style.height = "80px";
        window.probeFocusTargets = [];
        mount.addEventListener("focusin", event => window.probeFocusTargets.push([event.target.tagName, event.target.type]));
        document.body.append(mount);
    });
    expect(await command(page, "input-focus", "input")).toEqual({ focused: true, actual: true });
    expect(await command(page, "input-probe-focus", "progress")).toEqual({ focused: false, actual: false });
    await expect(page.getByRole("textbox")).toBeFocused();
    expect(await command(page, "input-probe-focus", "toggle")).toEqual({ focused: true, actual: true });
    expect(await command(page, "input-probe-focus", "checkbox")).toEqual({ focused: true, actual: true });
    expect(await page.evaluate(() => window.probeFocusTargets)).toEqual([["INPUT", "checkbox"], ["INPUT", "checkbox"]]);
    await expect(page.locator("#input-focus-probe .xui-node")).toHaveCount(0);
});
