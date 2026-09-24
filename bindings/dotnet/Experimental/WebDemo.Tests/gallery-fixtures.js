import { readFileSync } from "node:fs";
import { test, expect, byId } from "./fixtures.js";

export { test, expect, byId };
export { runScenarioSteps as runGallerySteps } from "./fixtures.js";
const corpus = JSON.parse(readFileSync(new URL("../SharedDemo/GalleryScenarios.json", import.meta.url), "utf8"));
if (corpus.version !== 1 || !Array.isArray(corpus.applications) || corpus.applications.length !== 3)
    throw new Error("Expected the version 1 three-application gallery corpus.");
const dynamic = JSON.parse(readFileSync(new URL("../SharedDemo/DynamicTaskScenarios.json", import.meta.url), "utf8"));
if (dynamic.version !== 1 || dynamic.applications?.length !== 1 || dynamic.applications[0].id !== "dynamic-task-board")
    throw new Error("Expected the shared dynamic task-board corpus.");
export const galleryApplications = [
    ...corpus.applications,
    { ...dynamic.applications[0], route: "dynamic-tasks" }
];
