import { test, expect, byId } from "./fixtures.js";

test.beforeEach(async ({ page }) => {
    await page.goto("./?app=dynamic-tasks");
    await expect(byId(page, "dynamic-summary")).toHaveText("3 tasks / 1 done");
});

test("real dynamic app retains editor identity caret composition and draft through add remove and reverse", async ({ page }) => {
    const input = byId(page, "dynamic-task-2-title").locator("input");
    await input.fill("Retained native draft");
    await byId(page, "dynamic-draft").locator("input").fill("New sibling");
    await input.evaluate(node => {
        window.retainedTaskInput = node;
        node.focus();
        node.setSelectionRange(2, 8, "backward");
        window.nativeWrites = 0;
        const descriptor = Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, "value");
        Object.defineProperty(node, "value", {
            get() { return descriptor.get.call(this); },
            set(value) { window.nativeWrites++; descriptor.set.call(this, value); }
        });
        node.dispatchEvent(new CompositionEvent("compositionstart", { bubbles: true }));
    });
    await byId(page, "dynamic-add").evaluate(node => node.click());
    await expect(byId(page, "dynamic-summary")).toHaveText("4 tasks / 1 done");
    for (let index = 0; index < 12; index++) {
        await byId(page, "dynamic-reverse").evaluate(node => node.click());
        await expect.poll(() => page.locator('[data-xui-id^="dynamic-task-"][data-xui-id$="-title"]').first()
            .getAttribute("data-xui-id")).toBe(index % 2 === 0 ? "dynamic-task-4-title" : "dynamic-task-1-title");
        expect(await input.evaluate(node => ({
            same: node === window.retainedTaskInput, focused: node === document.activeElement,
            start: node.selectionStart, end: node.selectionEnd, direction: node.selectionDirection, writes: window.nativeWrites
        }))).toEqual({ same: true, focused: true, start: 2, end: 8, direction: "backward", writes: 0 });
    }
    await byId(page, "dynamic-task-4-remove").evaluate(node => node.click());
    await expect(byId(page, "dynamic-task-4-title")).toHaveCount(0);
    await expect(input).toHaveValue("Retained native draft");
    await input.evaluate(node => node.dispatchEvent(new CompositionEvent("compositionend", { bubbles: true })));
    await input.press("Z");
    await expect(input).toHaveValue("ReZ native draft");
    await byId(page, "dynamic-filter-done").click();
    await expect(byId(page, "dynamic-task-2-title")).toHaveCount(0);
    await byId(page, "dynamic-filter-all").click();
    await expect(input).toHaveValue("ReZ native draft");
    expect(await input.evaluate(node => node !== window.retainedTaskInput)).toBe(true);
});

test("dynamic rows keep natural caption editor geometry and scroll access at a narrow scaled viewport", async ({ page }) => {
    await page.setViewportSize({ width: 320, height: 640 });
    for (const fontSize of [16, 24]) {
        await page.evaluate(size => { document.body.style.fontSize = `${size}px`; }, fontSize);
        const input = byId(page, "dynamic-task-3-title").locator("input");
        await input.scrollIntoViewIfNeeded();
        expect(await input.evaluate(node => {
            const editor = node.getBoundingClientRect();
            const caption = node.labels[0].getBoundingClientRect();
            const wrapper = node.parentElement.getBoundingClientRect();
            const scroll = document.querySelector('[data-xui-id="dynamic-content"]').getBoundingClientRect();
            return { separated: caption.bottom <= editor.top, contains: editor.bottom <= wrapper.bottom,
                fits: wrapper.width <= scroll.width, scrollUsable: scroll.height >= editor.height,
                onScreen: editor.top >= 0 && editor.bottom <= innerHeight };
        })).toEqual({ separated: true, contains: true, fits: true, scrollUsable: true, onScreen: true });
    }
});
