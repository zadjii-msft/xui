import { test, expect, byId } from "./fixtures.js";

const rect = locator => locator.evaluate(node => {
    const value = node.getBoundingClientRect();
    return { x: value.x, y: value.y, width: value.width, height: value.height, right: value.right, bottom: value.bottom };
});
test("axis constraints override only the selected legacy dimension and allocate pressure with shared math", async ({ page }) => {
    await page.setViewportSize({ width: 500, height: 700 });
    await page.goto("./?app=axes");
    const field = byId(page, "axes-input");
    await expect(field).toBeVisible();
    await expect.poll(async () => (await rect(field)).width).toBe(280);
    expect((await rect(field)).height).toBeLessThan(90);
    const input = field.locator("input");
    await input.fill("Retained axis input");
    await input.evaluate(node => { window.axesInput = node; node.focus(); node.setSelectionRange(1, 6); });
    await byId(page, "axes-narrow").evaluate(node => node.click());
    await expect.poll(async () => (await rect(field)).width).toBe(220);
    expect(await input.evaluate(node => node === window.axesInput && node === document.activeElement &&
        node.selectionStart === 1 && node.selectionEnd === 6)).toBe(true);
    await byId(page, "axes-inherit").click();
    expect(await rect(field)).toMatchObject({ width: 180, height: 90 });
    await byId(page, "axes-preserve-height").click();
    expect(await rect(byId(page, "width-only-input"))).toMatchObject({ width: 240, height: 96 });
    expect(await rect(byId(page, "first-minimum"))).toMatchObject({ width: 120 });
    expect(await rect(byId(page, "second-minimum"))).toMatchObject({ width: 60 });
    expect(await rect(byId(page, "fixed-child"))).toMatchObject({ width: 60 });
    expect(await rect(byId(page, "capped-child"))).toMatchObject({ width: 40 });
    expect(await rect(byId(page, "flex-child"))).toMatchObject({ width: 60 });
    const first = await rect(byId(page, "unbounded-first"));
    const second = await rect(byId(page, "unbounded-second"));
    expect(first.height).toBe(32);
    expect(second.height).toBe(48);
    expect(second.y - first.y).toBe(40);
    await page.setViewportSize({ width: 100, height: 300 });
    await expect.poll(async () => (await rect(field)).width).toBeLessThanOrEqual(68);
});

test("Grid uses actual clipped column width and unbounded star rows retain their natural heights", async ({ page }) => {
    await page.setViewportSize({ width: 500, height: 700 });
    await page.goto("./?app=grid");
    const field = byId(page, "grid-input");
    await expect(field).toBeVisible();
    const first = await rect(byId(page, "grid-natural-first"));
    const second = await rect(byId(page, "grid-natural-second"));
    expect(first.height).toBe(40);
    expect(second.height).toBe(80);
    expect(second.y - first.y).toBe(40);
    const fixedFirst = await rect(byId(page, "fixed-scope-first"));
    const fixedSecond = await rect(byId(page, "fixed-scope-second"));
    expect(fixedFirst.height).toBe(48);
    expect(fixedSecond.height).toBe(144);
    expect(fixedSecond.y - fixedFirst.y).toBe(56);
    const mixedFirst = await rect(byId(page, "mixed-scope-first"));
    const mixedSecond = await rect(byId(page, "mixed-scope-second"));
    expect(mixedFirst.height).toBe(32);
    expect(mixedSecond.height).toBe(48);
    expect(mixedSecond.y - mixedFirst.y).toBe(40);
    const input = field.locator("input");
    await input.fill("Retained grid text");
    await input.evaluate(node => { window.gridInput = node; node.focus(); node.setSelectionRange(2, 8); });
    await byId(page, "grid-compact").evaluate(node => node.click());
    expect(await input.evaluate(node => node === window.gridInput && document.activeElement === node &&
        node.selectionStart === 2 && node.selectionEnd === 8)).toBe(true);
    await page.setViewportSize({ width: 180, height: 500 });
    await expect.poll(async () => (await rect(field)).right).toBeLessThanOrEqual(164);
    const geometry = await input.evaluate(node => ({
        labelBottom: node.labels[0].getBoundingClientRect().bottom,
        inputTop: node.getBoundingClientRect().top,
        bottom: node.getBoundingClientRect().bottom,
        parentBottom: node.parentElement.getBoundingClientRect().bottom
    }));
    expect(geometry.inputTop).toBeGreaterThanOrEqual(geometry.labelBottom);
    expect(geometry.bottom).toBeLessThanOrEqual(geometry.parentBottom);
});

test("Grid remeasures intrinsic text keyed descendants and nested theme fonts without replacing input", async ({ page }) => {
    await page.setViewportSize({ width: 360, height: 650 });
    await page.goto("./?app=grid");
    const field = byId(page, "grid-input");
    await expect(field).toBeVisible();
    const input = field.locator("input");
    await input.evaluate(node => { window.gridInput = node; });
    const before = await rect(byId(page, "grid-compact"));
    await input.fill("Long wrapped summary ".repeat(16));
    await expect.poll(async () => (await rect(byId(page, "grid-compact"))).y).toBeGreaterThan(before.y);
    const changed = await rect(byId(page, "grid-compact"));
    await byId(page, "grid-toggle-note").evaluate(node => node.click());
    await expect(byId(page, "grid-note")).toHaveCount(1);
    await expect.poll(async () => (await rect(byId(page, "grid-compact"))).y).toBeGreaterThan(changed.y);
    const oldHeight = (await rect(field)).height;
    await page.addStyleTag({ content: ".xui-grid { font: 28px/1.5 monospace; }" });
    await expect.poll(async () => (await rect(field)).height).toBeGreaterThan(oldHeight);
    expect(await input.evaluate(node => node === window.gridInput)).toBe(true);
    const geometry = await input.evaluate(node => ({
        font: getComputedStyle(node).fontSize,
        labelHeight: node.labels[0].getBoundingClientRect().height,
        inputHeight: node.getBoundingClientRect().height,
        parentHeight: node.parentElement.getBoundingClientRect().height
    }));
    expect(geometry.font).toBe("28px");
    expect(geometry.parentHeight).toBeGreaterThanOrEqual(geometry.labelHeight + geometry.inputHeight);
});

test("measurement content is inert sanitized ephemeral and never copies live input values", async ({ page }) => {
    await page.goto("./?app=grid");
    await expect(byId(page, "grid-input")).toBeVisible();
    await page.evaluate(() => {
        window.measurementRecords = [];
        window.measurementObserver = new MutationObserver(records => {
            for (const record of records) for (const node of record.addedNodes) {
                if (!(node instanceof HTMLElement) || !node.inert || node.getAttribute("aria-hidden") !== "true") continue;
                window.measurementRecords.push({
                    connected: node.isConnected,
                    unsafe: !!node.querySelector("[id],[name],[form],[for],[list],[autofocus],[src],[srcset],[href]"),
                    values: [...node.querySelectorAll("input")].map(input => input.value)
                });
            }
        });
        window.measurementObserver.observe(document.querySelector("#app"), { childList: true });
    });
    const requests = [];
    page.on("request", request => requests.push(request.url()));
    await byId(page, "grid-input").locator("input").fill("Do not clone this field value");
    await byId(page, "grid-compact").click();
    const records = await page.evaluate(() => { window.measurementObserver.disconnect(); return window.measurementRecords; });
    expect(records.length).toBeGreaterThan(0);
    expect(records.every(record => !record.connected && !record.unsafe && record.values.every(value => value === ""))).toBe(true);
    expect(requests).toEqual([]);
});

test("unstable viewport allocation reports a bounded error instead of silently abandoning layout", async ({ page, diagnostics }) => {
    await page.goto("./?app=axes");
    await expect(byId(page, "axes-input")).toBeVisible();
    diagnostics.expected.push(/XUI browser error:.*did not stabilize within four viewport passes/s);
    await page.evaluate(() => {
        const mount = document.querySelector("#app");
        let reads = 0;
        Object.defineProperty(mount, "clientHeight", { get() { return ++reads % 2 ? 600 : 601; } });
    });
    await byId(page, "axes-narrow").click();
    await expect(page.getByRole("alert")).toContainText("did not stabilize within four viewport passes");
    await expect(page.locator("#app .xui-node")).toHaveCount(0);
});

test("a stylesheet finishing after insertion invalidates cached native typography measurements", async ({ page }) => {
    await page.goto("./?app=grid");
    const field = byId(page, "grid-input");
    await expect(field).toBeVisible();
    const before = (await rect(field)).height;
    let stylesheet;
    await page.route("**/delayed-layout.css", route => { stylesheet = route; });
    await page.evaluate(() => {
        const link = document.createElement("link");
        link.rel = "stylesheet";
        link.href = "./delayed-layout.css";
        document.head.append(link);
    });
    await expect.poll(() => !!stylesheet).toBe(true);
    await page.evaluate(() => new Promise(requestAnimationFrame));
    await stylesheet.fulfill({ contentType: "text/css", body: ".xui-grid { font: 30px/1.6 monospace; }" });
    await expect.poll(async () => (await rect(field)).height).toBeGreaterThan(before);
    expect(await field.locator("input").evaluate(node => getComputedStyle(node).fontSize)).toBe("30px");
});
