import { mkdir, writeFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import { resolve } from "node:path";

export async function virtualEvidence(name, data, testInfo) {
    const directory = fileURLToPath(new URL("../../../../build/browser-virtualization/", import.meta.url));
    await mkdir(directory, { recursive: true });
    const path = resolve(directory, `${name}.json`);
    await writeFile(path, JSON.stringify(data, null, 2) + "\n");
    await testInfo.attach(name, { path, contentType: "application/json" });
}
