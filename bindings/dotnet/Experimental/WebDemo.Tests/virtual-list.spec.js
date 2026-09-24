import { test, expect, byId } from "./fixtures.js";
import { virtualEvidence } from "./virtual-evidence.js";

async function ready(page) {
    await expect(byId(page, "virtual-last")).toBeEnabled();
    await expect.poll(() => page.locator(".xui-virtual-clip").evaluate(node => node.getBoundingClientRect().height)).toBeGreaterThan(0);
}
async function coverage(page, count = 10000, pins = 0) {
    const actual = await page.evaluate(() => {
        const clip = document.querySelector(".xui-virtual-clip");
        const bounds = clip.getBoundingClientRect();
        const content = clip.firstElementChild;
        const offset = -new DOMMatrixReadOnly(getComputedStyle(content).transform).m42;
        const rows = [...clip.querySelectorAll('[role="listitem"]')].map(node => {
            const rect = node.getBoundingClientRect();
            return { index: Number(node.getAttribute("aria-posinset")) - 1, count: Number(node.getAttribute("aria-setsize")),
                y: rect.top, height: rect.height, bottom: rect.bottom };
        });
        return { offset, height: bounds.height, rows, visible: rows.filter(row => row.bottom > bounds.top && row.y < bounds.bottom).map(row => row.index),
            inputs: clip.querySelectorAll("input").length, source: document.querySelector(".xui-virtual-viewport").dataset.xuiCommittedSource };
    });
    const first = Math.floor(actual.offset / 128);
    const end = Math.min(count, Math.ceil((actual.offset + actual.height) / 128));
    expect(actual.visible).toEqual(Array.from({ length: end - first }, (_, index) => first + index));
    expect(actual.rows.every(row => row.count === count && row.height === 128)).toBe(true);
    expect(actual.rows.map(row => row.index)).toEqual(actual.rows.map(row => row.index).sort((a, b) => a - b));
    expect(new Set(actual.rows.map(row => row.index)).size).toBe(actual.rows.length);
    expect(actual.inputs).toBe(actual.rows.length);
    expect(actual.inputs).toBeLessThanOrEqual(Math.ceil(actual.height / 128) + 5 + pins);
    return actual;
}

test.beforeEach(async ({ page }) => {
    await page.setViewportSize({ width: 600, height: 700 });
    await page.goto("./?app=virtual-list");
    await ready(page);
    expect(await page.evaluate(() => typeof window.xuiTest)).toBe("undefined");
});

test("ten thousand real C# rows stay sparse with native wheel scrolling and logical accessible positions", async ({ page }, testInfo) => {
    await page.evaluate(() => {
        window.transientRows = [];
        const insert = Node.prototype.insertBefore;
        Node.prototype.insertBefore = function (node, before) {
            const result = insert.call(this, node, before);
            const clip = document.querySelector(".xui-virtual-clip");
            const viewport = document.querySelector(".xui-virtual-viewport");
            if (clip?.contains(node) && viewport) {
                const committedHeight = Number(viewport.dataset.xuiCommittedHeight);
                const requestedHeight = viewport.querySelector(".xui-virtual-intent").clientHeight;
                window.transientRows.push({ count: clip.querySelectorAll("input").length, committedHeight, requestedHeight,
                    bound: Math.ceil(committedHeight / 128) + Math.ceil(requestedHeight / 128) + 11 });
            }
            return result;
        };
    });
    await coverage(page);
    await expect(page.getByRole("textbox", { name: "Task 1", exact: true })).toHaveValue("Task 1");
    await page.locator(".xui-virtual-clip").hover();
    await page.mouse.wheel(0, 350000);
    await expect.poll(() => page.locator(".xui-virtual-viewport").getAttribute("data-xui-committed-offset")).not.toBe("0");
    const far = await coverage(page);
    expect(far.offset).toBeGreaterThan(300000);
    await byId(page, "virtual-last").click();
    const last = page.getByRole("textbox", { name: "Task 10,000", exact: true });
    await expect(last).toBeFocused();
    await coverage(page, 10000, 1);
    await page.context().setOffline(true);
    await last.fill("Offline retained task");
    await last.evaluate(node => node.setSelectionRange(2, 8));
    await byId(page, "virtual-first").click();
    await expect(page.getByRole("textbox", { name: "Task 1", exact: true })).toBeFocused();
    await byId(page, "virtual-last").click();
    await expect(last).toHaveValue("Offline retained task");
    expect(await last.evaluate(node => [node.selectionStart, node.selectionEnd])).toEqual([2, 8]);
    await coverage(page, 10000, 1);
    const transients = await page.evaluate(() => window.transientRows);
    expect(transients.length).toBeGreaterThan(0);
    expect(transients.every(sample => sample.count <= sample.bound)).toBe(true);
    await virtualEvidence("native-realization-peak", { peak: Math.max(...transients.map(sample => sample.count)), transients }, testInfo);
});

test("focused row and gap updates require no row moves or value writes during far scroll and return", async ({ page }) => {
    const input = page.getByRole("textbox", { name: "Task 1", exact: true });
    await input.fill("Pinned native draft");
    await input.evaluate(node => {
        window.pinnedEditor = node;
        window.pinnedRoot = node.closest('[role="listitem"]');
        node.setSelectionRange(2, 8, "backward");
        window.pinnedMoves = 0;
        window.pinnedWrites = 0;
        const value = Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, "value");
        Object.defineProperty(node, "value", {
            get() { return value.get.call(this); },
            set(next) { window.pinnedWrites++; value.set.call(this, next); }
        });
        for (const method of ["moveBefore", "insertBefore"]) {
            const original = Element.prototype[method];
            if (typeof original !== "function") continue;
            Element.prototype[method] = function (moving, ...args) {
                if (moving === window.pinnedRoot || moving.contains(window.pinnedEditor)) window.pinnedMoves++;
                return original.call(this, moving, ...args);
            };
        }
        Element.prototype.moveBefore = undefined;
    });
    await page.locator(".xui-virtual-intent").evaluate(node => { node.scrollTop = 800000; });
    await expect(page.locator(".xui-virtual-viewport")).toHaveAttribute("data-xui-committed-offset", "800000");
    await coverage(page, 10000, 1);
    await page.locator(".xui-virtual-intent").evaluate(node => { node.scrollTop = 0; });
    await expect(page.locator(".xui-virtual-viewport")).toHaveAttribute("data-xui-committed-offset", "0");
    expect(await input.evaluate(node => ({
        same: node === window.pinnedEditor, focused: node === document.activeElement,
        start: node.selectionStart, end: node.selectionEnd, direction: node.selectionDirection,
        moves: window.pinnedMoves, writes: window.pinnedWrites
    }))).toEqual({ same: true, focused: true, start: 2, end: 8, direction: "backward", moves: 0, writes: 0 });
    await expect(input).toHaveValue("Pinned native draft");
    await coverage(page, 10000, 1);
});

test("composition holds scroll exposure viewport growth and source publication until an unblock", async ({ page }) => {
    const input = page.getByRole("textbox", { name: "Task 1", exact: true });
    await input.focus();
    const original = await coverage(page, 10000, 1);
    await input.evaluate(node => { node.setSelectionRange(1, 4); node.dispatchEvent(new CompositionEvent("compositionstart", { bubbles: true })); });
    await page.locator(".xui-virtual-intent").evaluate(node => { node.scrollTop = 500000; });
    await page.setViewportSize({ width: 750, height: 850 });
    await byId(page, "virtual-filter").evaluate(node => node.click());
    await expect(byId(page, "virtual-status")).toContainText("deferred");
    await expect(page.locator(".xui-virtual-viewport")).toHaveAttribute("data-xui-committed-offset", String(original.offset));
    await expect(page.locator(".xui-virtual-viewport")).toHaveAttribute("data-xui-committed-source", original.source);
    expect(await page.locator(".xui-virtual-clip").evaluate(node => node.getBoundingClientRect().height)).toBeLessThanOrEqual(original.height);
    await expect(input).toBeFocused();
    await input.evaluate(node => node.dispatchEvent(new CompositionEvent("compositionend", { bubbles: true })));
    // Source replacement remains deferred until the actual focused row leaves.
    await expect(page.locator(".xui-virtual-viewport")).toHaveAttribute("data-xui-committed-source", original.source);
    await byId(page, "virtual-title").evaluate(node => { node.tabIndex = -1; node.focus(); });
    await expect.poll(() => page.locator(".xui-virtual-viewport").getAttribute("data-xui-committed-source")).not.toBe(original.source);
    await expect(byId(page, "virtual-status")).toContainText("5,000 items");
    await coverage(page, 5000);
});

test("native Tab follows realized source order and Enter plus toolbar navigate logical rows", async ({ page }) => {
    const first = page.getByRole("textbox", { name: "Task 1", exact: true });
    await first.focus();
    await first.press("Tab");
    const second = page.getByRole("textbox", { name: "Task 2", exact: true });
    await expect(second).toBeFocused();
    await second.press("Enter");
    await expect(page.getByRole("textbox", { name: "Task 3", exact: true })).toBeFocused();
    await byId(page, "virtual-reverse").click();
    await byId(page, "virtual-first").click();
    await expect(page.getByRole("textbox", { name: "Task 10,000", exact: true })).toBeFocused();
    await coverage(page, 10000, 1);
    await expect(page.getByRole("listitem").first()).toHaveAttribute("aria-posinset", "1");
    await byId(page, "virtual-filter").click();
    await expect(byId(page, "virtual-status")).toContainText("5,000 items");
    const top = page.getByRole("textbox", { name: "Task 9,999", exact: true });
    await top.focus();
    await byId(page, "virtual-remove").click();
    await expect(byId(page, "virtual-status")).toContainText("4,999 items");
    await coverage(page, 4999);
});

test("animation-frame observations never expose a committed sparse gap during rapid scroll and resize", async ({ page }) => {
    await page.evaluate(() => {
        window.viewportAudit = { active: true, frames: 0, failures: [] };
        const inspect = () => {
            const audit = window.viewportAudit;
            if (!audit.active) return;
            const clip = document.querySelector(".xui-virtual-clip");
            const viewport = document.querySelector(".xui-virtual-viewport");
            if (clip && !clip.hidden && clip.clientHeight > 0) {
                const content = clip.firstElementChild;
                const bounds = content.getBoundingClientRect();
                const offset = -new DOMMatrixReadOnly(getComputedStyle(content).transform).m42;
                const count = Math.round(Number(viewport.querySelector(".xui-virtual-extent").style.height.slice(0, -2)) / 128);
                const rows = [...clip.querySelectorAll('[role="listitem"]')];
                const first = Math.floor(offset / 128);
                const end = Math.min(count, Math.ceil((offset + clip.getBoundingClientRect().height) / 128));
                for (let index = first; index < end; index++) {
                    const matching = rows.filter(row => Number(row.getAttribute("aria-posinset")) === index + 1 &&
                        row.getAttribute("aria-setsize") === String(count) &&
                        row.dataset.xuiVirtualSource === viewport.dataset.xuiCommittedSource);
                    if (matching.length !== 1 ||
                        Math.abs(matching[0].getBoundingClientRect().top - bounds.top - index * 128) > 0.75)
                        audit.failures.push({ index, offset, count, matches: matching.length });
                }
                audit.frames++;
            }
            requestAnimationFrame(inspect);
        };
        requestAnimationFrame(inspect);
    });
    for (let index = 0; index < 12; index++) {
        await page.locator(".xui-virtual-intent").evaluate((node, value) => { node.scrollTop = value; }, index % 2 ? 1000 : 900000);
        await page.setViewportSize({ width: index % 2 ? 600 : 640, height: index % 2 ? 650 : 800 });
    }
    await expect.poll(() => page.locator(".xui-virtual-viewport").getAttribute("data-xui-committed-offset")).toBe("1000");
    const audit = await page.evaluate(() => { window.viewportAudit.active = false; return window.viewportAudit; });
    expect(audit.frames).toBeGreaterThan(5);
    expect(audit.failures).toEqual([]);
    await coverage(page);
});

test("virtual row native editors preserve pitch and readable geometry under a larger root font", async ({ page }) => {
    const input = page.getByRole("textbox", { name: "Task 1", exact: true });
    await input.fill("Scaled virtual draft");
    await input.evaluate(node => { window.scaledVirtualInput = node; });
    await page.setViewportSize({ width: 400, height: 700 });
    await page.evaluate(() => { document.body.style.fontSize = "24px"; });
    await expect.poll(() => input.evaluate(node => getComputedStyle(node).fontSize)).toBe("24px");
    await expect.poll(() => input.evaluate(node => {
        const editor = node.getBoundingClientRect();
        const row = node.closest('[role="listitem"]').getBoundingClientRect();
        return editor.height >= 24 && row.height === 128 && editor.bottom <= row.bottom && editor.right <= row.right;
    })).toBe(true);
    expect(await input.evaluate(node => node === window.scaledVirtualInput)).toBe(true);
    await expect(input).toHaveValue("Scaled virtual draft");
    await coverage(page, 10000, 1);
});
