import { test, expect, command, demo, byId } from "./fixtures.js";

test.beforeEach(async ({ page }) => {
    await demo(page);
    await command(page, "mutation-start");
});

test("C# keyed changes preserve a surviving editor across insert reorder remove and type replacement", async ({ page }) => {
    const a = byId(page, "a").locator("input");
    await a.fill("Native retained edit");
    await a.evaluate(node => {
        window.keptInput = node;
        node.focus();
        node.setSelectionRange(2, 8);
        node.dispatchEvent(new CompositionEvent("compositionstart", { bubbles: true }));
    });
    for (let i = 0; i < 12; i++) {
        await command(page, "mutation-reconcile", i % 2 === 0 ? "new,c,a,b" : "a,b,c");
        expect(await a.evaluate(node => node === window.keptInput && document.activeElement === node &&
            node.selectionStart === 2 && node.selectionEnd === 8)).toBe(true);
        await expect(a).toHaveValue("Native retained edit");
    }
    await a.evaluate(node => node.dispatchEvent(new CompositionEvent("compositionend", { bubbles: true })));
    await page.evaluate(() => { window.removedInput = document.querySelector('[data-xui-id="b"] input'); });
    await command(page, "mutation-reconcile", "a,c");
    await page.evaluate(() => {
        window.removedInput.value = "stale";
        window.removedInput.dispatchEvent(new Event("input"));
        window.removedInput.dispatchEvent(new KeyboardEvent("keydown", { key: "Enter" }));
    });
    expect((await command(page, "mutation-state")).changes).toBe(1);
    expect((await command(page, "mutation-state")).submits).toBe(0);
    await command(page, "mutation-reconcile", "!a,c");
    expect(await a.evaluate(node => node !== window.keptInput)).toBe(true);
    await expect(a).toHaveValue("Value a");
    await command(page, "mutation-reconcile", "");
    await expect(page.locator("#app input")).toHaveCount(0);
    await command(page, "mutation-reconcile", "a");
    await expect(a).toHaveValue("Value a");
    await a.press("Enter");
    expect((await command(page, "mutation-state")).submits).toBe(1);
    await command(page, "mutation-dispose");
    await expect(page.locator("#app .xui-node")).toHaveCount(0);
});

test("C# precommit rejection retains the model while a native operation failure detaches", async ({ page }) => {
    const initial = await command(page, "mutation-state");
    await expect(command(page, "mutation-reconcile", "a,a")).rejects.toThrow(/Duplicate key/);
    await expect(command(page, "mutation-reconcile", "a,factory-error")).rejects.toThrow(/Intentional keyed factory failure/);
    expect(await command(page, "mutation-state")).toEqual(initial);
    await byId(page, "c").locator("input").evaluate(node => {
        window.mutatingParent = node.parentElement.parentElement.parentElement;
        window.originalMove = window.mutatingParent.moveBefore;
        window.mutatingParent.moveBefore = undefined;
        node.focus();
        node.setSelectionRange(1, 3);
        node.dispatchEvent(new CompositionEvent("compositionstart", { bubbles: true }));
    });
    await expect(command(page, "mutation-reconcile", "c,a,b")).rejects.toThrow(/composition/);
    expect(await command(page, "mutation-state")).toEqual(initial);
    await byId(page, "c").locator("input").evaluate(node => node.dispatchEvent(new CompositionEvent("compositionend", { bubbles: true })));
    await page.evaluate(() => {
        window.mutatingParent.moveBefore = () => { throw new Error("Intentional native move failure"); };
    });
    await expect(command(page, "mutation-reconcile", "c,a,b")).rejects.toThrow(/Intentional native move failure/);
    await expect(page.locator("#app .xui-node")).toHaveCount(0);
    const failed = await command(page, "mutation-state");
    expect(failed.attached).toBe(false);
    expect(failed.rows.map(row => row.key)).toEqual(["c", "a", "b"]);
    await command(page, "mutation-attach");
    await expect(page.locator("#app input")).toHaveCount(3);
    await byId(page, "a").locator("input").fill("Recovered");
    expect((await command(page, "mutation-state")).changes).toBe(1);
});
