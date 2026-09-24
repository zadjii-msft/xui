import { test, expect, byId } from "./fixtures.js";

test("height-only Studio adaptation keeps usable native editors without replacing or refocusing them", async ({ page }, testInfo) => {
    await page.setViewportSize({ width: 914, height: 800 });
    await page.goto("./?app=studio");
    const title = byId(page, "doc-00001-title").locator("input");
    const body = byId(page, "doc-00001-body").locator("textarea");
    await expect(body).toBeVisible();
    await title.fill("Retained short-height title");
    await body.fill("Retained landscape draft\nSecond native line");
    await body.evaluate(node => {
        window.shortHeightBody = node;
        window.shortHeightTitle = document.querySelector('[data-xui-id="doc-00001-title"] input');
        node.setSelectionRange(2, 8);
    });
    await page.setViewportSize({ width: 914, height: 314 });
    await expect(byId(page, "studio-tab-left")).toBeHidden();
    await expect(title).toBeVisible();
    await expect(body).toBeVisible();
    await expect(body).toBeFocused();
    const geometry = await page.evaluate(() => {
        const bounds = node => {
            const r = node.getBoundingClientRect();
            return { x: r.x, y: r.y, width: r.width, height: r.height, bottom: r.bottom };
        };
        const title = document.querySelector('[data-xui-id="doc-00001-title"] input');
        const body = document.querySelector('[data-xui-id="doc-00001-body"] textarea');
        return {
            window: { width: innerWidth, height: innerHeight },
            host: bounds(document.querySelector("#app")),
            title: bounds(title), body: bounds(body),
            sameTitle: title === window.shortHeightTitle,
            sameBody: body === window.shortHeightBody,
            selection: [body.selectionStart, body.selectionEnd]
        };
    });
    await testInfo.attach("short-height-native-geometry", {
        body: JSON.stringify(geometry), contentType: "application/json"
    });
    expect(geometry).toMatchObject({ sameTitle: true, sameBody: true, selection: [2, 8] });
    expect(geometry.title.height).toBeGreaterThanOrEqual(24);
    expect(geometry.body.height).toBeGreaterThanOrEqual(96);
    expect(geometry.body.y).toBeGreaterThanOrEqual(geometry.title.bottom);
    expect(geometry.body.bottom).toBeLessThanOrEqual(314);
    await body.fill("Edited through the short native viewport");
    await page.setViewportSize({ width: 914, height: 800 });
    await expect(byId(page, "studio-tab-left")).toBeVisible();
    await expect(title).toHaveValue("Retained short-height title");
    await expect(body).toHaveValue("Edited through the short native viewport");
    expect(await body.evaluate(node => node === window.shortHeightBody)).toBe(true);
});
