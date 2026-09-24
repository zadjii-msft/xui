import { defineConfig } from "@playwright/test";
import { publishedConfig } from "./browser-config.js";

export default defineConfig(publishedConfig("WebOrderDemo", ["order.spec.js", "order.navigation.spec.js"]));
