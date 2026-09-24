import { test, expect } from "./fixtures.js";
import { adapterFixture } from "./adapter-fixtures.js";

test.beforeEach(async ({ page }) => { await adapterFixture(page); });

test("theme inheritance never overwrites unowned mount styling and each acquisition captures a fresh baseline", async ({ page }) => {
    const result = await page.evaluate(() => {
        const mount = document.querySelector("#mount");
        mount.style.setProperty("color-scheme", "light", "important");
        mount.style.setProperty("--xui-theme-accent", "#123456", "important");
        surface.applyTheme(null);
        surface.unmount();
        const unowned = [mount.style.colorScheme, mount.style.getPropertyPriority("color-scheme"), mount.style.getPropertyValue("--xui-theme-accent")];
        const next = adapter.createSurface("mount", "errors");
        next.applyTheme({ mode: "Dark", foreground: null, background: null, accent: null });
        next.applyTheme(null);
        const restored = [mount.style.colorScheme, mount.style.getPropertyPriority("color-scheme"), mount.style.getPropertyValue("--xui-theme-accent")];
        mount.style.setProperty("color-scheme", "dark", "important");
        mount.style.setProperty("--xui-theme-accent", "#654321");
        mount.classList.add("xui-theme-surface");
        next.applyTheme({ mode: "Light", foreground: null, background: null, accent: null });
        next.unmount();
        return { unowned, restored, originalClass: mount.classList.contains("xui-theme-surface"),
            reacquired: [mount.style.colorScheme, mount.style.getPropertyPriority("color-scheme"), mount.style.getPropertyValue("--xui-theme-accent")] };
    });
    expect(result).toEqual({
        unowned: ["light", "important", "#123456"], restored: ["light", "important", "#123456"],
        reacquired: ["dark", "important", "#654321"], originalClass: true
    });
});

test("custom and forced palettes retain actual native disabled button and editor colors", async ({ page }) => {
    await page.evaluate(() => {
        surface.create(1, state("Stack"), callback);
        surface.create(2, state("Button", { enabled: false }), callback);
        surface.create(3, state("TextInput", { enabled: false }), callback);
        surface.addChild(1, 2);
        surface.addChild(1, 3);
        surface.mount(1);
        const reference = document.createElement("div");
        reference.id = "native-reference";
        reference.style.cssText = "width:400px;height:150px";
        document.body.append(reference);
        window.referenceSurface = adapter.createSurface("native-reference", "errors");
        referenceSurface.create(1, state("Stack"), callback);
        referenceSurface.create(2, state("Button", { enabled: false }), callback);
        referenceSurface.create(3, state("TextInput", { enabled: false }), callback);
        referenceSurface.addChild(1, 2);
        referenceSurface.addChild(1, 3);
        referenceSurface.mount(1);
    });
    const colors = () => page.evaluate(() => {
        const reference = document.querySelector("#native-reference");
        reference.style.colorScheme = getComputedStyle(document.querySelector("#mount")).colorScheme;
        return {
            themed: [".xui-button", ".xui-textinput input"].map(selector => getComputedStyle(document.querySelector("#mount").querySelector(selector)).color),
            native: ["button", "input"].map(selector => getComputedStyle(reference.querySelector(selector)).color)
        };
    });
    let values = await colors();
    expect(values.themed).toEqual(values.native);
    await page.evaluate(() => surface.applyTheme({ mode: "System", foreground: null, background: null, accent: null }));
    values = await colors();
    expect(values.themed).toEqual(values.native);
    await page.evaluate(() => surface.applyTheme({
        mode: "Dark", foreground: { light: 0xff0000, dark: 0x00ff00 },
        background: null, accent: { light: 0xff0000, dark: 0x00ff00 }
    }));
    values = await colors();
    expect(values.themed).toEqual(values.native);
    await page.evaluate(() => surface.update(2, "Enabled", true, 0));
    expect(await page.locator("#mount .xui-button").evaluate(node => getComputedStyle(node).color)).toBe("rgb(0, 255, 0)");
    await page.evaluate(() => surface.update(2, "Enabled", false, 0));
    await page.emulateMedia({ forcedColors: "active" });
    await expect.poll(() => page.locator("#mount").evaluate(node => node.style.getPropertyValue("--xui-theme-foreground"))).toBe("");
    values = await colors();
    expect(values.themed).toEqual(values.native);
});
