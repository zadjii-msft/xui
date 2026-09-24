import assert from "node:assert/strict";
import test from "node:test";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import { browserOptions, developmentConfig, publishedConfig } from "./browser-config.js";

test("browser engine selection is explicit and never silently falls back", () => {
    assert.deepEqual(browserOptions({}), { browserName: "chromium" });
    for (const browserName of ["chromium", "firefox", "webkit"])
        assert.deepEqual(browserOptions({ XUI_BROWSER_ENGINE: browserName }), { browserName });
    assert.deepEqual(browserOptions({ XUI_BROWSER_CHANNEL: "msedge" }),
        { browserName: "chromium", channel: "msedge" });
    for (const value of ["", "edge", "Chrome", "firefox --headless"])
        assert.throws(() => browserOptions({ XUI_BROWSER_ENGINE: value }), /XUI_BROWSER_ENGINE/);
    for (const browserName of ["firefox", "webkit"])
        assert.throws(() => browserOptions({ XUI_BROWSER_ENGINE: browserName, XUI_BROWSER_CHANNEL: "msedge" }),
            /XUI_BROWSER_CHANNEL/);
});

test("all engines retain the same common and real navigation test projects", () => {
    const development = developmentConfig("WebDemo", ["demo.spec.js"]);
    const published = publishedConfig("WebGalleryDemo", ["virtual-list.navigation.spec.js"]);
    assert.deepEqual(development.testMatch, ["demo.spec.js"]);
    assert.deepEqual(published.testMatch, ["virtual-list.navigation.spec.js"]);
    assert.equal(development.use.browserName, published.use.browserName);
    assert.deepEqual(published.projects.map(project => project.name), ["release", "bfcache"]);
    assert.equal(published.projects[0].grepInvert.test("@bfcache"), true);
    assert.equal(published.projects[1].grep.test("@bfcache"), true);
    assert.equal(development.webServer.reuseExistingServer, false);
    assert.equal(published.webServer.reuseExistingServer, false);
    assert.equal(development.workers, 1);
});

test("published server rejects invalid isolated ports before reading build output", () => {
    const server = fileURLToPath(new URL("./static-server.js", import.meta.url));
    for (const value of ["", "0", "65536", "-1", "5187.5", "5187 --inspect"]) {
        const result = spawnSync(process.execPath, [server], {
            env: { ...process.env, XUI_WEB_PROJECT: "WebGalleryDemo", XUI_WEB_PORT: value },
            encoding: "utf8",
            timeout: 5000
        });
        assert.equal(result.error, undefined);
        assert.notEqual(result.status, 0);
        assert.match(result.stderr, /XUI_WEB_PORT must be an integer/);
    }
});
