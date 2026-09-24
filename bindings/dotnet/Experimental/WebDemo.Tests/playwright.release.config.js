import { defineConfig } from "@playwright/test";
import { publishedConfig } from "./browser-config.js";

export default defineConfig(publishedConfig("WebDemo", "release.spec.js"));
