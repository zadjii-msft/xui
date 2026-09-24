import { test, expect, byId, inputById, inputIds, order, runSteps, scenarios, fullOrder } from "./order-fixtures.js";

for (const scenario of scenarios) {
    test(`shared order scenario: ${scenario.name}`, async ({ page }) => {
        await order(page);
        await runSteps(page, scenario.steps);
    });
}

test("order assets and generated C# stay local with no application test bridge", async ({ page, baseURL }) => {
    const requests = [];
    const wasm = [];
    page.on("request", request => requests.push(request.url()));
    page.on("response", response => { if (/\.wasm(?:$|\?)/.test(response.url())) wasm.push(response); });
    await order(page);
    const base = new URL(baseURL);
    if (!base.pathname.endsWith("/")) base.pathname += "/";
    expect(await page.evaluate(() => document.baseURI)).toBe(base.href);
    expect(requests.every(url => url.startsWith(base.href))).toBe(true);
    expect(requests.some(url => url.includes("test-driver.js"))).toBe(false);
    expect(wasm.some(response => /dotnet.*\.wasm/.test(response.url()))).toBe(true);
    for (const response of wasm) {
        expect(response.status()).toBe(200);
        expect(await response.headerValue("content-type")).toBe("application/wasm");
    }
    await expect(page.locator("canvas")).toHaveCount(0);
    await expect(page.getByRole("main", { name: "XUI shared order builder" })).toBeVisible();
});

test("the literal full order scenario runs offline through compiled C# callbacks", async ({ page }) => {
    const requests = [];
    page.on("request", request => requests.push(request.url()));
    await order(page);
    const loadedRequests = requests.length;
    await page.context().setOffline(true);
    await runSteps(page, fullOrder.steps);
    expect(requests).toHaveLength(loadedRequests);
});

test("all three native inputs retain identity selection and composition through dependent updates", async ({ page }) => {
    await order(page);
    for (const [id, value] of [["customer-name", "Grace Hopper"], ["email", "grace@example.test"], ["discount-code", "SAVE10"]])
        await inputById(page, id).fill(value);
    await page.evaluate(() => {
        window.originalNodes = [...document.querySelectorAll("#app .xui-node")];
        window.originalInputs = [...document.querySelectorAll("#app input")];
        window.valueWrites = 0;
        const descriptor = Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, "value");
        for (const input of window.originalInputs) {
            Object.defineProperty(input, "value", {
                get() { return descriptor.get.call(this); },
                set(value) { window.valueWrites++; descriptor.set.call(this, value); }
            });
        }
    });
    for (const id of inputIds) {
        const input = inputById(page, id);
        const value = await input.inputValue();
        const quantity = await byId(page, "coffee-quantity").textContent();
        await input.evaluate(node => {
            node.focus();
            node.setSelectionRange(1, 4);
            node.dispatchEvent(new CompositionEvent("compositionstart", { bubbles: true }));
            node.dispatchEvent(new KeyboardEvent("keydown", { key: "Enter", isComposing: true, bubbles: true }));
            document.querySelector('[data-xui-id="coffee-more"]').click();
        });
        await expect(byId(page, "coffee-quantity")).not.toHaveText(quantity);
        await expect(byId(page, "review-summary")).toBeHidden();
        expect(await input.evaluate(node => ({
            same: window.originalInputs.includes(node),
            inputs: window.originalInputs.every((n, i) => document.querySelectorAll("#app input")[i] === n),
            tree: window.originalNodes.every((n, i) => document.querySelectorAll("#app .xui-node")[i] === n),
            focused: document.activeElement === node,
            start: node.selectionStart, end: node.selectionEnd, writes: window.valueWrites
        }))).toEqual({ same: true, inputs: true, tree: true, focused: true, start: 1, end: 4, writes: 0 });
        await expect(input).toHaveValue(value);
        await input.evaluate(node => node.dispatchEvent(new CompositionEvent("compositionend", { bubbles: true })));
    }
});

test("malformed native text reports explicitly and the order callback queue recovers", async ({ page, diagnostics }) => {
    await order(page);
    diagnostics.expected.push(/XUI browser error:.*Text cannot contain NUL/s);
    await inputById(page, "email").evaluate(node => {
        node.value = "invalid\0email";
        node.dispatchEvent(new Event("input", { bubbles: true }));
    });
    await expect(page.getByRole("alert")).toContainText("Text cannot contain NUL");
    expect(await page.evaluate(() => {
        const app = document.querySelector("#app").getBoundingClientRect();
        const errors = document.querySelector("#errors").getBoundingClientRect();
        return { separate: app.bottom <= errors.top, bounded: errors.height <= innerHeight * 0.4 };
    })).toEqual({ separate: true, bounded: true });
    await inputById(page, "email").fill("recovered@example.test");
    await runSteps(page, scenarios[0].steps);
    await inputById(page, "customer-name").fill("<b>Local C#</b>");
    await expect(inputById(page, "customer-name")).toHaveValue("<b>Local C#</b>");
    await expect(page.locator("#app b")).toHaveCount(0);
});

test("native labels keyboard order and controls remain usable in a narrow scroll viewport", async ({ page }) => {
    await page.setViewportSize({ width: 320, height: 240 });
    await order(page);
    for (const [id, label] of [["customer-name", "Customer name"], ["email", "Email"], ["discount-code", "Discount code"]]) {
        const input = inputById(page, id);
        await expect(input).toHaveAccessibleName(label);
        expect(await input.evaluate(node => [...node.labels].map(caption => caption.textContent))).toEqual([label]);
    }
    await inputById(page, "customer-name").focus();
    for (const id of ["email", "coffee-more", "tea-more", "cocoa-more", "discount-code", "reset"]) {
        await page.keyboard.press("Tab");
        await expect(inputIds.includes(id) ? inputById(page, id) : byId(page, id)).toBeFocused();
    }
    for (const item of ["coffee", "tea", "cocoa"]) {
        await expect(byId(page, `${item}-more`)).toHaveAccessibleName(`More ${item}`);
        await expect(byId(page, `${item}-less`)).toHaveAccessibleName(`Less ${item}`);
    }
    const quantity = await byId(page, "coffee-quantity").textContent();
    await byId(page, "coffee-more").focus();
    await byId(page, "coffee-more").press("Space");
    await expect(byId(page, "coffee-quantity")).not.toHaveText(quantity);
    await byId(page, "reset").focus();
    await byId(page, "reset").press("Enter");
    await expect(byId(page, "coffee-quantity")).toHaveText(quantity);
    expect(await page.evaluate(() => {
        const root = document.querySelector(".xui-root").getBoundingClientRect();
        const scroll = document.querySelector('[data-xui-id="order-content"]');
        return {
            width: root.width, height: root.height, scrolls: scroll.scrollHeight > scroll.clientHeight,
            inputsFit: [...document.querySelectorAll(".xui-textinput")].every(node =>
                node.getBoundingClientRect().width <= scroll.clientWidth)
        };
    })).toEqual({ width: 320, height: 240, scrolls: true, inputsFit: true });
});

test("natural caption and editor geometry stays usable at 100 150 and 200 percent text size", async ({ page }) => {
    await page.setViewportSize({ width: 320, height: 360 });
    await order(page);
    for (const fontSize of [16, 24, 32]) {
        await page.evaluate(size => { document.body.style.fontSize = `${size}px`; }, fontSize);
        for (const id of inputIds) {
            const input = inputById(page, id);
            await input.scrollIntoViewIfNeeded();
            const geometry = await input.evaluate(node => {
                const wrapper = node.parentElement.getBoundingClientRect();
                const label = node.labels[0].getBoundingClientRect();
                const editor = node.getBoundingClientRect();
                const style = getComputedStyle(node);
                const range = document.createRange();
                range.selectNodeContents(node.labels[0]);
                const text = range.getBoundingClientRect();
                return {
                    font: parseFloat(style.fontSize), editorHeight: editor.height,
                    separated: label.bottom <= editor.top,
                    containsLabel: label.top >= wrapper.top && label.bottom <= wrapper.bottom,
                    containsEditor: editor.bottom <= wrapper.bottom && editor.left >= wrapper.left && editor.right <= wrapper.right,
                    labelFits: text.top >= label.top && text.bottom <= label.bottom,
                    visibleEditor: editor.top >= 0 && editor.bottom <= innerHeight
                };
            });
            expect(geometry).toMatchObject({
                font: fontSize, separated: true, containsLabel: true, containsEditor: true, labelFits: true, visibleEditor: true
            });
            expect(geometry.editorHeight).toBeGreaterThanOrEqual(fontSize);
        }
    }
    await runSteps(page, fullOrder.steps);
});
