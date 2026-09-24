import { fileURLToPath } from "node:url";

export function browserOptions(environment = process.env) {
    const browserName = environment.XUI_BROWSER_ENGINE ?? "chromium";
    if (!["chromium", "firefox", "webkit"].includes(browserName))
        throw new Error("XUI_BROWSER_ENGINE must be chromium, firefox, or webkit.");
    const channel = environment.XUI_BROWSER_CHANNEL;
    if (channel && browserName !== "chromium")
        throw new Error("XUI_BROWSER_CHANNEL requires XUI_BROWSER_ENGINE=chromium.");
    return { browserName, ...(channel ? { channel } : {}) };
}

const browser = browserOptions();
const defaults = {
    testDir: ".",
    workers: 1,
    timeout: 60000,
    globalTimeout: 300000,
    expect: { timeout: 15000 },
    reporter: "list",
    use: {
        ...browser,
        launchOptions: { timeout: 30000 },
        headless: true
    }
};

export function developmentConfig(projectName, testMatch) {
    const project = fileURLToPath(new URL(`../${projectName}/${projectName}.csproj`, import.meta.url));
    const baseURL = "http://127.0.0.1:5187";
    return {
        ...defaults,
        testMatch,
        use: { ...defaults.use, baseURL },
        webServer: {
            command: `dotnet run --no-build --project "${project}" -c Debug --urls ${baseURL}`,
            url: baseURL,
            reuseExistingServer: false,
            timeout: 120000,
            env: { ASPNETCORE_ENVIRONMENT: "Development" }
        }
    };
}

export function publishedConfig(projectName, testMatch) {
    const basePath = process.env.XUI_WEB_BASE_PATH ?? "/";
    const baseURL = `http://127.0.0.1:5187${basePath}`;
    return {
        ...defaults,
        testMatch,
        use: { ...defaults.use, baseURL },
        projects: [
            { name: "release", grepInvert: /@bfcache/ },
            {
                name: "bfcache",
                grep: /@bfcache/,
                use: {
                    launchOptions: browser.browserName === "chromium"
                        ? { ignoreDefaultArgs: ["--disable-back-forward-cache"], timeout: 30000 }
                        : { timeout: 30000 }
                }
            }
        ],
        webServer: {
            command: "node static-server.js",
            url: baseURL,
            reuseExistingServer: false,
            timeout: 30000,
            env: { XUI_WEB_PROJECT: projectName }
        }
    };
}
