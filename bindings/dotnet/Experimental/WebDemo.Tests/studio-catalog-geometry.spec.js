import { test, expect, byId } from "./fixtures.js";
import { virtualEvidence } from "./virtual-evidence.js";

for (const width of [1400, 320]) for (const scale of [1, 2]) for (const dirty of [false, true]) {
    test(`Studio catalog content fits at ${width}px ${scale}x logical text ${dirty ? "long dirty" : "sample"} title`, async ({ page }, testInfo) => {
        await page.setViewportSize({ width, height: 1000 });
        await page.goto("./?app=studio");
        await expect(byId(page, "studio-navigation")).toBeVisible();
        await expect(page.locator("#app")).toHaveAttribute("data-xui-studio-host-ready", "true");
        if (dirty) await byId(page, "doc-00001-title").locator("input").fill("W".repeat(160));
        if (width < 720) await byId(page, "studio-navigation").locator('[id$="-tab-1"]').click();
        await expect(byId(page, "doc-00001-open")).toBeVisible();
        await page.evaluate(scale => {
            document.documentElement.style.fontSize = `${16 * scale}px`;
            document.body.style.fontSize = `${16 * scale}px`;
        }, scale);
        await expect.poll(() => byId(page, "doc-00001-open").evaluate(node => getComputedStyle(node).fontSize)).toBe(`${16 * scale}px`);
        const geometry = await byId(page, "doc-00001-open").evaluate(button => {
            const row = button.closest('[role="listitem"]');
            const caption = row.querySelector('[data-xui-id$="-category"]');
            const title = row.querySelector('[data-xui-id$="-catalog-title"]');
            const rowRect = row.getBoundingClientRect(), buttonRect = button.getBoundingClientRect(), captionRect = caption.getBoundingClientRect();
            const range = document.createRange();
            range.selectNodeContents(button);
            const text = range.getBoundingClientRect();
            const captionRange = document.createRange();
            captionRange.selectNodeContents(caption);
            const captionText = captionRange.getBoundingClientRect();
            const clone = row.cloneNode(true);
            for (const node of [clone, ...clone.querySelectorAll("*")]) {
                node.classList.remove("xui-arranged", "xui-managed-container");
                node.removeAttribute("id");
                node.removeAttribute("data-xui-id");
            }
            clone.inert = true;
            clone.setAttribute("aria-hidden", "true");
            Object.assign(clone.style, { position: "fixed", left: "-100000px", top: "0", width: `${rowRect.width}px`, height: "auto", maxHeight: "none", visibility: "hidden" });
            document.querySelector("#app").append(clone);
            let naturalHeight;
            try { naturalHeight = clone.getBoundingClientRect().height; }
            finally { clone.remove(); }
            return {
                naturalHeight,
                row: { y: rowRect.y, height: rowRect.height, bottom: rowRect.bottom, width: rowRect.width },
                button: { y: buttonRect.y, height: buttonRect.height, width: buttonRect.width, bottom: buttonRect.bottom, textBottom: text.bottom,
                    font: getComputedStyle(button).fontSize, lineHeight: getComputedStyle(button).lineHeight },
                caption: { y: captionRect.y, height: captionRect.height, bottom: captionRect.bottom, textBottom: captionText.bottom,
                    font: getComputedStyle(caption).fontSize },
                title: title ? { textLength: title.textContent.length, accessibleLength: title.getAttribute("aria-label").length, whiteSpace: getComputedStyle(title.firstElementChild).whiteSpace,
                    overflow: getComputedStyle(title.firstElementChild).textOverflow, height: title.getBoundingClientRect().height } : null,
                bodyFont: getComputedStyle(document.body).fontSize,
                rootFont: getComputedStyle(document.documentElement).fontSize
            };
        });
        await virtualEvidence(`studio-catalog-${width}-${scale}-${dirty ? "dirty" : "sample"}`, geometry, testInfo);
        expect(geometry.naturalHeight).toBeLessThanOrEqual(128);
        expect(geometry.row.height).toBe(128);
        expect(geometry.button.height).toBeGreaterThanOrEqual(16 * scale + 6);
        expect(geometry.button.height).toBeGreaterThanOrEqual(24);
        expect(geometry.button.width).toBeGreaterThanOrEqual(24);
        expect(geometry.button.textBottom).toBeLessThanOrEqual(geometry.button.bottom);
        expect(geometry.caption.height).toBeGreaterThanOrEqual(12 * scale);
        expect(geometry.caption.textBottom).toBeLessThanOrEqual(geometry.caption.bottom);
        expect(geometry.caption.bottom).toBeLessThanOrEqual(geometry.row.bottom - 8);
        expect(geometry.caption.y).toBeGreaterThanOrEqual(geometry.button.bottom + 4);
        if (dirty) expect(geometry.title).toMatchObject({ textLength: 160, accessibleLength: 160, whiteSpace: "pre", overflow: "ellipsis" });
    });
}
