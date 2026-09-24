import { test, expect, byId } from "./fixtures.js";

test.beforeEach(async ({ page }) => {
    await page.goto("./?app=reveal");
    await expect(byId(page, "reveal-title")).toBeVisible();
});

test("actual native Reveal expands retained geometry and preserves closed spacing and editor identity", async ({ page }) => {
    const drawer = page.locator('[data-xui-kind="Reveal"]');
    const input = byId(page, "reveal-note").locator("input");
    await expect(drawer).toHaveAttribute("data-xui-reveal-progress", "0");
    const closedAfter = await byId(page, "reveal-instant").boundingBox();
    await input.evaluate(node => { window.retainedRevealInput = node; });
    await page.evaluate(() => {
        window.revealFrames = [];
        const drawer = document.querySelector('[data-xui-kind="Reveal"]');
        const record = () => {
            const progress = Number(drawer.dataset.xuiRevealProgress);
            window.revealFrames.push({ progress, clip: drawer.getBoundingClientRect().height,
                child: drawer.firstElementChild.getBoundingClientRect().height });
            if (drawer.dataset.xuiRevealAnimating === "true") requestAnimationFrame(record);
        };
        document.querySelector('[data-xui-id="reveal-toggle"]').click();
        requestAnimationFrame(record);
    });
    await expect(drawer).toHaveAttribute("data-xui-reveal-animating", "false");
    await expect(drawer).toHaveAttribute("data-xui-reveal-progress", "1");
    const frames = await page.evaluate(() => window.revealFrames);
    expect(frames.some(frame => frame.progress > 0 && frame.progress < 1)).toBe(true);
    expect(frames.filter(frame => frame.progress > 0).every(frame => Math.abs(frame.clip - frame.child * frame.progress) < 1)).toBe(true);
    await expect(input).toBeVisible();
    await input.fill("Retained native drawer draft");
    await byId(page, "reveal-toggle").click();
    await expect(drawer).toHaveAttribute("aria-hidden", "true");
    await expect(drawer).toHaveAttribute("data-xui-reveal-progress", "0");
    expect((await byId(page, "reveal-instant").boundingBox()).y).toBe(closedAfter.y);
    await byId(page, "reveal-toggle").click();
    await expect(drawer).toHaveAttribute("data-xui-reveal-progress", "1");
    expect(await input.evaluate(node => node === window.retainedRevealInput)).toBe(true);
    await expect(input).toHaveValue("Retained native drawer draft");
});

test("native Reveal vetoes unsafe close and honors actual reduced-motion and forced-color policy", async ({ page }) => {
    const drawer = page.locator('[data-xui-kind="Reveal"]');
    await byId(page, "reveal-toggle").click();
    await expect(drawer).toHaveAttribute("data-xui-reveal-progress", "1");
    const input = byId(page, "reveal-note").locator("input");
    await input.fill("Composing retained note");
    await input.evaluate(node => { node.setSelectionRange(2, 8); node.dispatchEvent(new CompositionEvent("compositionstart", { bubbles: true })); });
    await byId(page, "reveal-toggle").evaluate(node => node.click());
    await expect(byId(page, "reveal-status")).toContainText("Close deferred");
    await expect(input).toBeFocused();
    await expect(drawer).toHaveAttribute("data-xui-reveal-progress", "1");
    await input.evaluate(node => node.dispatchEvent(new CompositionEvent("compositionend", { bubbles: true })));
    await byId(page, "reveal-toggle").click();
    await page.emulateMedia({ reducedMotion: "reduce" });
    await expect(drawer).toHaveAttribute("data-xui-reveal-progress", "0");
    await expect(drawer).toHaveAttribute("data-xui-reveal-animating", "false");
    await byId(page, "reveal-toggle").click();
    await expect(drawer).toHaveAttribute("data-xui-reveal-progress", "1");
    await expect(drawer).toHaveAttribute("data-xui-reveal-animating", "false");
    await page.emulateMedia({ reducedMotion: "no-preference", forcedColors: "active" });
    await byId(page, "reveal-toggle").click();
    await expect(drawer).toHaveAttribute("data-xui-reveal-progress", "0");
    await expect(drawer).toHaveAttribute("data-xui-reveal-animating", "false");
});

test("native Reveal motion changes settle the old endpoint before the new direction and never shrink retained content", async ({ page }) => {
    const drawer = page.locator('[data-xui-kind="Reveal"]');
    await byId(page, "reveal-toggle").click();
    await expect(drawer).toHaveAttribute("data-xui-reveal-progress", "1");
    const full = await drawer.locator(":scope > .xui-node").boundingBox();
    await page.evaluate(() => {
        document.querySelector('[data-xui-id="reveal-toggle"]').click();
        requestAnimationFrame(() => document.querySelector('[data-xui-id="reveal-right"]').click());
    });

    await expect(drawer).toHaveAttribute("data-xui-reveal-progress", "0");
    await expect(drawer).toHaveAttribute("data-xui-reveal-animating", "false");
    await byId(page, "reveal-toggle").click();
    await expect(drawer).toHaveAttribute("data-xui-reveal-progress", "1");
    expect((await drawer.locator(":scope > .xui-node").boundingBox()).width).toBe(full.width);
});

test("real shared Operations drawer is opt-in and keeps controls alive through native motion and tab activation", async ({ page }) => {
        await page.setViewportSize({ width: 1400, height: 1000 });
        await page.goto("./?app=studio&motion=1");
        await byId(page, "studio-open-operations").click();
        const drawer = page.getByRole("group", { name: "Operations controls", exact: true });
        await expect(drawer).toHaveAttribute("data-xui-reveal-progress", "1");
        const selector = byId(page, "operations-scope").locator("select");
        await selector.evaluate(node => { window.retainedScope = node; });
        await byId(page, "operations-controls-toggle").click();
        await expect(page.locator('[data-xui-kind="Reveal"]')).toHaveAttribute("data-xui-reveal-progress", "0");
        await expect(selector).toBeHidden();
        await byId(page, "studio-tabs").locator('[id$="-tab-1"]').click();
        await byId(page, "studio-tabs").locator('[id$="-tab-10001"]').press("Enter");
        await expect(byId(page, "operations-controls-toggle")).toBeFocused();
        await expect(selector).toBeHidden();
        await byId(page, "operations-controls-toggle").click();
        await expect(drawer).toHaveAttribute("data-xui-reveal-progress", "1");
        expect(await selector.evaluate(node => node === window.retainedScope)).toBe(true);
        await selector.selectOption("3");
        await byId(page, "operations-scan").click();
        await expect(byId(page, "operations-status")).toHaveText("Local operations snapshot complete. No cloud metrics or user files were accessed.");
    });
