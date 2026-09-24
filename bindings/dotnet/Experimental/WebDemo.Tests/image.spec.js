import { test, expect, byId } from "./fixtures.js";

test("real packaged C# image loads offline and quality changes never resize or replace native controls", async ({ page }) => {
    await page.goto("./?app=image");
    await expect(byId(page, "image-status")).toHaveText("Empty");
    const box = byId(page, "packaged-image");
    const bounds = await box.boundingBox();
    expect(bounds.width).toBe(192);
    expect(bounds.height).toBe(144);
    const notes = byId(page, "image-notes").locator("input");
    await notes.fill("Retained native image draft");
    await notes.evaluate(node => { window.imageNotes = node; node.focus(); node.setSelectionRange(1, 7); });
    await box.locator("img").evaluate(node => { window.retainedImage = node; });
    await page.context().setOffline(true);
    await byId(page, "image-load").evaluate(node => node.click());
    await expect(byId(page, "image-status")).toHaveText("Ready: 64x64");
    await expect(box.locator("img")).toHaveAccessibleName("Zoey packaged image");
    expect(await box.locator("img").evaluate(node => node.complete && node.naturalWidth === 64 && node.src.startsWith("blob:"))).toBe(true);
    await byId(page, "image-smaller").evaluate(node => node.click());
    await expect(byId(page, "image-status")).toHaveText("Ready: 32x32");
    expect(await box.boundingBox()).toEqual(bounds);
    expect(await notes.evaluate(node => node === window.imageNotes && node === document.activeElement && node.selectionStart === 1 && node.selectionEnd === 7)).toBe(true);
    expect(await box.locator("img").evaluate(node => node === window.retainedImage)).toBe(true);
    await byId(page, "image-invalid").click();
    await expect(byId(page, "image-status")).toContainText("Error:");
    expect(await box.boundingBox()).toEqual(bounds);
    await byId(page, "image-clear").click();
    await expect(byId(page, "image-status")).toHaveText("Empty");
    await expect(box.locator("img")).not.toHaveAttribute("src");
    expect(await box.boundingBox()).toEqual(bounds);
    await expect(page.locator("#app canvas")).toHaveCount(0);
});

test("actual codec pixels match the original fixture and rounded output keeps source containment geometry", async ({ page }) => {
    await page.goto("./?app=image-pixels");
    const box = byId(page, "exact-image");
    const image = box.locator("img");
    await expect.poll(() => image.evaluate(node => node.naturalWidth)).toBe(3);
    expect(await image.evaluate(node => {
        const canvas = document.createElement("canvas");
        canvas.width = 3; canvas.height = 2;
        const context = canvas.getContext("2d");
        context.drawImage(node, 0, 0);
        const pixels = [...context.getImageData(0, 0, 3, 2).data];
        canvas.width = canvas.height = 0;
        return pixels;
    })).toEqual([255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255, 0, 0, 0, 255, 128, 64, 32, 255]);
    expect((await box.boundingBox()).width).toBe(192);
    expect((await box.boundingBox()).height).toBe(144);
    await page.goto("./?app=image-pixels&small=1");
    await expect.poll(() => image.evaluate(node => node.naturalWidth)).toBe(2);
    expect(await image.evaluate(node => node.naturalHeight)).toBe(1);
    const imageBounds = await image.boundingBox();
    expect(imageBounds.width / imageBounds.height).toBeCloseTo(1.5);
    expect(imageBounds.height).toBe(128);
    await page.setViewportSize({ width: 120, height: 500 });
    await expect.poll(async () => (await box.boundingBox()).width).toBeLessThanOrEqual(120);
    expect((await image.boundingBox()).width / (await image.boundingBox()).height).toBeCloseTo(1.5);
});

test("canceled native decode closes its late bitmap and never resurrects a cleared image", async ({ page }) => {
    await page.addInitScript(() => {
        const decode = window.createImageBitmap;
        window.bitmapCloses = 0;
        window.decodeStarted = false;
        window.createImageBitmap = async (...args) => {
            const bitmap = await decode(...args);
            const close = bitmap.close.bind(bitmap);
            bitmap.close = () => { window.bitmapCloses++; close(); };
            window.decodeStarted = true;
            await new Promise(resolve => { window.releaseBitmap = resolve; });
            return bitmap;
        };
    });
    await page.goto("./?app=image");
    await byId(page, "image-load").click();
    await expect.poll(() => page.evaluate(() => window.decodeStarted)).toBe(true);
    await byId(page, "image-clear").click();
    await expect(byId(page, "image-status")).toHaveText("Empty");
    await page.evaluate(() => window.releaseBitmap());
    await expect.poll(() => page.evaluate(() => window.bitmapCloses)).toBe(1);
    await expect(byId(page, "packaged-image").locator("img")).not.toHaveAttribute("src");
    await expect(byId(page, "image-status")).toHaveText("Empty");
});

test("an initially hidden packaged image reaches native Ready before its application shows it", async ({ page }) => {
    await page.goto("./?app=image-hidden");
    await expect(byId(page, "packaged-image")).toBeHidden();
    await byId(page, "image-load").click();
    await expect(byId(page, "image-status")).toHaveText("Ready: 64x64");
    await expect(byId(page, "packaged-image")).toBeVisible();
    expect(await byId(page, "packaged-image").locator("img").evaluate(node => node.complete && node.naturalWidth === 64)).toBe(true);
});

test("canceled-but-running raw decodes retain admission and repeated replacement cannot start a codec storm", async ({ page }) => {
    await page.addInitScript(() => {
        const decode = window.createImageBitmap;
        window.rawDecodeStarts = 0;
        window.rawDecodeCloses = 0;
        window.pendingRaw = [];
        window.holdRaw = true;
        window.createImageBitmap = async (...args) => {
            window.rawDecodeStarts++;
            const bitmap = await decode(...args);
            const close = bitmap.close.bind(bitmap);
            bitmap.close = () => { window.rawDecodeCloses++; close(); };
            if (window.holdRaw) await new Promise(resolve => window.pendingRaw.push(resolve));
            return bitmap;
        };
    });
    await page.goto("./?app=image");
    await byId(page, "image-load").click();
    await expect.poll(() => page.evaluate(() => window.pendingRaw.length)).toBe(1);
    await byId(page, "image-smaller").click();
    await expect.poll(() => page.evaluate(() => window.pendingRaw.length)).toBe(2);
    await byId(page, "image-clear").click();
    await byId(page, "image-load").click();
    await expect(byId(page, "image-status")).toContainText("concurrency limit");
    expect(await page.evaluate(() => window.rawDecodeStarts)).toBe(2);
    await page.evaluate(() => { window.holdRaw = false; for (const release of window.pendingRaw) release(); });
    await expect.poll(() => page.evaluate(() => window.rawDecodeCloses)).toBe(2);
    await byId(page, "image-clear").click();
    await byId(page, "image-load").click();
    await expect(byId(page, "image-status")).toHaveText("Ready: 32x32");
    expect(await page.evaluate(() => window.rawDecodeStarts)).toBe(3);
    expect(await page.evaluate(() => window.rawDecodeCloses)).toBe(3);
});

test("repeated actual image replacement releases every owned output URL", async ({ page }) => {
    await page.addInitScript(() => {
        window.imageUrls = new Set();
        const create = URL.createObjectURL.bind(URL), revoke = URL.revokeObjectURL.bind(URL);
        URL.createObjectURL = blob => { const url = create(blob); window.imageUrls.add(url); return url; };
        URL.revokeObjectURL = url => { window.imageUrls.delete(url); return revoke(url); };
    });
    await page.goto("./?app=image");
    for (let cycle = 0; cycle < 12; cycle++) {
        await byId(page, "image-load").click();
        await expect(byId(page, "image-status")).toHaveText("Ready: 64x64");
        expect(await page.evaluate(() => window.imageUrls.size)).toBe(1);
        await byId(page, "image-clear").click();
        await expect(byId(page, "image-status")).toHaveText("Empty");
        expect(await page.evaluate(() => window.imageUrls.size)).toBe(0);
    }
});
