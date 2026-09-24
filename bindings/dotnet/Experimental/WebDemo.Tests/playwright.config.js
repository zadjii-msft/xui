import { defineConfig } from "@playwright/test";
import { developmentConfig } from "./browser-config.js";

export default defineConfig(developmentConfig("WebDemo", [
    "adapter.spec.js", "demo.spec.js", "mutation.spec.js", "generated-mutation.spec.js", "services.spec.js", "storage.spec.js",
    "native-input.spec.js", "layout-math.spec.js", "virtual-lease.spec.js", "viewport-probe.spec.js", "virtual-stress.spec.js",
    "feature-lifetime.spec.js", "presentation-boundary.spec.js", "reveal-lifetime.spec.js"
]));
