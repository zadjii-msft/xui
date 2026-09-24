import { defineConfig } from "@playwright/test";
import { developmentConfig } from "./browser-config.js";

export default defineConfig(developmentConfig("WebGalleryDemo", [
    "gallery.spec.js", "dynamic-tasks.spec.js", "settings.spec.js", "layout.spec.js", "profile.spec.js", "virtual-list.spec.js",
    "forms.spec.js", "presentation.spec.js", "pages.spec.js", "studio.spec.js", "studio.short-height.spec.js", "label-layout.spec.js", "image.spec.js", "image-fatal.spec.js", "reveal.spec.js"
]));
