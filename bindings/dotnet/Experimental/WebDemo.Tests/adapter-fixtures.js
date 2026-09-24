export async function adapterFixture(page) {
    await page.route("**/adapter-fixture", route => route.fulfill({
        contentType: "text/html",
        body: '<link rel="stylesheet" href="/_content/Xui.Web/xui-dom.css"><div id="mount" style="width:400px;height:300px"></div><div id="errors" hidden></div>'
    }));
    await page.goto("/adapter-fixture");
    await page.evaluate(async () => {
        window.adapter = await import("/_content/Xui.Web/xui-dom.js");
        window.surface = window.adapter.createSurface("mount", "errors");
        window.calls = [];
        window.callback = { invokeMethodAsync: async (...args) => { window.calls.push(args); return true; } };
        window.state = (kind, extra = {}) => ({
            kind, axis: kind === "Stack" ? "Vertical" : null, flex: 0, fixedSize: null, preferredSize: null,
            spacing: 0, padding: 0, name: kind, automationId: "", help: "",
            enabled: true, visible: true, text: "", placeholder: "", captionVisible: true, trackInteraction: false,
            isChecked: false, checkState: "Unchecked", threeState: false,
            range: { minimum: 0, maximum: 100, smallStep: 1, largeStep: 10 }, value: 0, progressState: "Determinate",
            purpose: "Normal", maximumLength: 65536, readOnly: false, typography: null,
            pages: { items: [], selected: null }, choices: { items: [], selected: null },
            expanded: true, closable: false, textLayout: null, reveal: { open: false, duration: 0, direction: "Bottom" }, ...extra
        });
    });
}
