import { defineConfig } from "@playwright/test";
import { fileURLToPath } from "node:url";

const project = fileURLToPath(new URL("../WebDemo/WebDemo.csproj", import.meta.url));
export default defineConfig({
    testDir: ".",
    testMatch: "*.spec.js",
    workers: 1,
    timeout: 60000,
    expect: { timeout: 15000 },
    reporter: "list",
    use: {
        baseURL: "http://127.0.0.1:5187",
        channel: process.env.XUI_BROWSER_CHANNEL,
        launchOptions: { args: ["--disable-gpu"], timeout: 30000 },
        headless: true
    },
    webServer: {
        command: `dotnet run --no-build --project "${project}" -c Debug --urls http://127.0.0.1:5187`,
        url: "http://127.0.0.1:5187",
        reuseExistingServer: false,
        timeout: 120000,
        env: { ASPNETCORE_ENVIRONMENT: "Development" }
    }
});
