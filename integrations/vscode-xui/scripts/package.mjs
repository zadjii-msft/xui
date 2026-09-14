import { mkdir, readFile } from "node:fs/promises";
import { createVSIX } from "@vscode/vsce";

const { name, version } = JSON.parse(await readFile(new URL("../package.json", import.meta.url), "utf8"));
await mkdir(new URL("../dist/", import.meta.url), { recursive: true });
await createVSIX({ packagePath: `dist/${name}-${version}.vsix`, dependencies: false });
