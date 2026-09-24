import { createServer } from "node:http";
import { access, readFile } from "node:fs/promises";
import { extname, isAbsolute, relative, resolve, sep } from "node:path";
import { fileURLToPath } from "node:url";

const project = process.env.XUI_WEB_PROJECT ?? "WebDemo";
if (!["WebDemo", "WebOrderDemo", "WebGalleryDemo"].includes(project))
    throw new Error("XUI_WEB_PROJECT must be WebDemo, WebOrderDemo, or WebGalleryDemo.");
const portText = process.env.XUI_WEB_PORT ?? "5187";
if (!/^[1-9][0-9]{0,4}$/.test(portText) || Number(portText) > 65535)
    throw new Error("XUI_WEB_PORT must be an integer from 1 through 65535.");
const port = Number(portText);
const origin = `http://127.0.0.1:${port}`;
const root = fileURLToPath(new URL(`../${project}/bin/Release/net10.0/publish/wwwroot/`, import.meta.url));
const basePath = process.env.XUI_WEB_BASE_PATH ?? "/";
if (!/^\/(?:[a-zA-Z0-9_-]+\/)*$/.test(basePath))
    throw new Error("XUI_WEB_BASE_PATH must be / or a slash-terminated path such as /nested/xui/.");
await access(resolve(root, "index.html"));

const types = {
    ".html": "text/html; charset=utf-8",
    ".js": "text/javascript; charset=utf-8",
    ".css": "text/css; charset=utf-8",
    ".json": "application/json",
    ".wasm": "application/wasm",
    ".woff": "font/woff",
    ".woff2": "font/woff2"
};
const awayPath = "/__xui_acceptance__/away.html";
const server = createServer(async (request, response) => {
    function send(status, type, body) {
        response.writeHead(status, { "Content-Type": type, "Cache-Control": "no-cache" });
        response.end(request.method === "HEAD" ? undefined : body);
    }
    if (!["GET", "HEAD"].includes(request.method)) {
        send(405, "text/plain", "Only GET and HEAD are supported.");
        return;
    }
    let pathname;
    try { pathname = decodeURIComponent(new URL(request.url, origin).pathname); }
    catch (error) {
        if (!(error instanceof URIError || error instanceof TypeError)) throw error;
        send(400, "text/plain", "Invalid URL.");
        return;
    }
    if (pathname === awayPath) {
        send(200, "text/html; charset=utf-8",
            '<!doctype html><html lang="en"><title>Navigation target</title><link rel="icon" href="data:,"><h1>Navigation target</h1></html>');
        return;
    }
    if (!pathname.startsWith(basePath) || pathname.includes("\\") || pathname.includes("\0")) {
        send(404, "text/plain", "Outside the published application.");
        return;
    }
    const filename = resolve(root, pathname.slice(basePath.length) || "index.html");
    const path = relative(root, filename);
    if (isAbsolute(path) || path === ".." || path.startsWith(`..${sep}`)) {
        send(404, "text/plain", "Outside the published application.");
        return;
    }
    try {
        const body = await readFile(filename);
        send(200, types[extname(filename)] ?? "application/octet-stream", body);
    } catch (error) {
        if (error.code === "ENOENT" || error.code === "EISDIR") {
            send(404, "text/plain", "Published file not found.");
            return;
        }
        console.error(error);
        send(500, "text/plain", "Failed to read published file.");
    }
});
server.listen(port, "127.0.0.1", () => console.log(`Published XUI: ${origin}${basePath}`));
for (const signal of ["SIGINT", "SIGTERM"]) {
    process.once(signal, () => {
        server.close(error => { if (error) throw error; });
        server.closeIdleConnections();
    });
}
