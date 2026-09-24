export async function observeNavigation(page) {
    await page.evaluate(() => {
        window.originalInput = document.querySelector("input");
        window.originalInputs = [...document.querySelectorAll("#app input")];
        window.originalNodes = [...document.querySelectorAll("#app .xui-node")];
        window.restored = false;
        window.addEventListener("pageshow", event => { window.restored = event.persisted; });
        window.addEventListener("pagehide", event => sessionStorage.setItem("xui-pagehide", JSON.stringify({
            persisted: event.persisted, nodes: document.querySelectorAll("#app .xui-node").length
        })));
    });
}
