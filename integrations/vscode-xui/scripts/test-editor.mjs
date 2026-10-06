import { fileURLToPath } from "node:url";
import { downloadAndUnzipVSCode, runTests } from "@vscode/test-electron";

const path = (relative) => fileURLToPath(new URL(relative, import.meta.url));
const vscodeExecutablePath = await downloadAndUnzipVSCode({
  version: "1.85.2",
  cachePath: path("../test/cache/vscode")
});
await runTests({
  vscodeExecutablePath,
  extensionDevelopmentPath: path(".."),
  extensionTestsPath: path("../test/extension-host.cjs"),
  launchArgs: [
    "--disable-extensions", "--skip-welcome", "--skip-release-notes", "--disable-workspace-trust",
    "--user-data-dir", path("../test/cache/editor-profile"),
    "--extensions-dir", path("../test/cache/editor-extensions")
  ]
});
