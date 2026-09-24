import { defineConfig } from "@playwright/test";
import { publishedConfig } from "./browser-config.js";

export default defineConfig(publishedConfig("WebGalleryDemo", [
    "gallery.spec.js", "dynamic-tasks.spec.js", "dynamic-tasks.navigation.spec.js", "settings.spec.js", "layout.spec.js",
    "profile.spec.js", "profile.navigation.spec.js", "virtual-list.spec.js", "virtual-list.navigation.spec.js",
    "forms.spec.js", "presentation.spec.js", "pages.spec.js", "studio.spec.js", "studio.short-height.spec.js", "studio.navigation.spec.js", "label-layout.spec.js", "image.spec.js", "reveal.spec.js"
]));
