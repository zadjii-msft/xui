export const captureApps = {
    greeting: { project: "WebDemo", source: "Greeting.xui" },
    order: { project: "WebOrderDemo", source: "OrderBuilder.xui" },
    "task-board": { project: "WebGalleryDemo", source: "TaskBoard.xui" },
    "expense-ledger": { project: "WebGalleryDemo", source: "ExpenseLedger.xui" },
    "session-planner": { project: "WebGalleryDemo", source: "SessionPlanner.xui" },
    "dynamic-tasks": { project: "WebGalleryDemo", source: "DynamicTaskBoard.xui", scenarioId: "dynamic-task-board" },
    settings: { project: "WebGalleryDemo", source: "SettingsShowcase.xui" },
    "profile-workspace": { project: "WebGalleryDemo", source: "ProfileWorkspace.xui" },
    studio: { project: "WebGalleryDemo", source: "WorkspaceStudio.xui", viewport: { width: 1440, height: 1000 }, allowScroll: true },
    "studio-phone": { project: "WebGalleryDemo", source: "WorkspaceStudio.xui", route: "studio", viewport: { width: 390, height: 844 }, allowScroll: true }
};

export const captureName = process.env.XUI_CAPTURE_APP ?? "greeting";
export const captureApp = captureApps[captureName];
if (!captureApp) throw new Error(`Unknown XUI_CAPTURE_APP: ${captureName}. Available: ${Object.keys(captureApps).join(", ")}.`);
