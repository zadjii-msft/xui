import { defineConfig } from "@playwright/test";
import { developmentConfig } from "./browser-config.js";

export default defineConfig(developmentConfig("WebDemo", "virtual-performance.spec.js"));
