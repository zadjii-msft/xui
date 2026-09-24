import { defineConfig } from "@playwright/test";
import { publishedConfig } from "./browser-config.js";
import { captureApp } from "./capture-apps.js";

const config = publishedConfig(captureApp.project, "capture.spec.js");
export default defineConfig({
    ...config,
    projects: [{ name: "capture" }],
    use: { ...config.use, viewport: { width: 1100, height: 1300 }, deviceScaleFactor: 1, colorScheme: "light", reducedMotion: "reduce" }
});
