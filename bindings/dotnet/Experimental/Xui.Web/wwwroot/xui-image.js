export function createImagePeer(node, alt, invalidate, fatal) {
    const image = document.createElement("img");
    image.alt = alt;
    image.draggable = false;
    image.style.cssText = "position:absolute;object-fit:fill";
    node.append(image);
    let current = null;
    function arrange() {
        if (!current?.sourceWidth) return;
        const scale = Math.min(node.clientWidth / current.sourceWidth, node.clientHeight / current.sourceHeight);
        const width = current.sourceWidth * scale, height = current.sourceHeight * scale;
        image.style.width = `${width}px`;
        image.style.height = `${height}px`;
        image.style.left = `${(node.clientWidth - width) / 2}px`;
        image.style.top = `${(node.clientHeight - height) / 2}px`;
    }
    const resize = new ResizeObserver(arrange);
    resize.observe(node);
    const failed = () => {
        if (current?.ready && !current.canceled) fatal(current.generation);
    };
    image.addEventListener("error", failed);
    function cancel() {
        if (current) current.canceled = true;
        image.removeAttribute("src");
        current?.release();
        current = null;
        invalidate();
    }
    return {
        image,
        cancel,
        async decode(generation, bytes, plan) {
            if (typeof generation !== "string" || !/^[1-9][0-9]{0,18}$/.test(generation) ||
                BigInt(generation) > 9223372036854775807n) throw new TypeError("Invalid exact image generation.");
            if (!(bytes instanceof Uint8Array) || bytes.byteLength === 0 || bytes.byteLength > 33554432 ||
                !["image/png", "image/jpeg"].includes(plan.contentType) ||
                ![plan.sourceWidth, plan.sourceHeight, plan.width, plan.height].every(Number.isInteger) ||
                plan.sourceWidth < 1 || plan.sourceHeight < 1 || plan.sourceWidth > 16384 || plan.sourceHeight > 16384 ||
                plan.sourceWidth * plan.sourceHeight > 16777216 || plan.width < 1 || plan.height < 1 || plan.width > 1024 || plan.height > 1024)
                throw new TypeError("Invalid bounded image decode request.");
            cancel();
            let url = null, bitmap = null, canvas = null;
            let disposed = false;
            const result = { canceled: false, error: null, sourceWidth: 0, sourceHeight: 0, width: 0, height: 0 };
            const request = {
                canceled: false,
                ready: false,
                generation,
                sourceWidth: plan.sourceWidth,
                sourceHeight: plan.sourceHeight,
                release() {
                    if (url !== null) { URL.revokeObjectURL(url); url = null; }
                }
            };
            current = request;
            try {
                if (typeof createImageBitmap !== "function") throw new Error("This browser does not support bounded native image decoding.");
                bitmap = await createImageBitmap(new Blob([bytes], { type: plan.contentType }));
                result.sourceWidth = bitmap.width;
                result.sourceHeight = bitmap.height;
                if (bitmap.width !== plan.sourceWidth || bitmap.height !== plan.sourceHeight)
                    throw new Error("Native codec dimensions disagree with image preflight.");
                if (request.canceled) { result.canceled = true; }
                else {
                    canvas = document.createElement("canvas");
                    canvas.width = plan.width;
                    canvas.height = plan.height;
                    const context = canvas.getContext("2d");
                    if (!context) throw new Error("Native image resampling is unavailable.");
                    context.drawImage(bitmap, 0, 0, plan.width, plan.height);
                    bitmap.close();
                    bitmap = null;
                    const blob = await new Promise((resolve, reject) => canvas.toBlob(value =>
                        value ? resolve(value) : reject(new Error("Native image encoding failed.")), "image/png"));
                    canvas.width = canvas.height = 0;
                    canvas = null;
                    if (blob.size > plan.width * plan.height * 4 + 65536) throw new Error("Resampled image encoding exceeded its bound.");
                    if (request.canceled) result.canceled = true;
                    else {
                        url = URL.createObjectURL(blob);
                        image.src = url;
                        await image.decode();
                        if (request.canceled || current !== request) result.canceled = true;
                        else {
                            result.width = image.naturalWidth;
                            result.height = image.naturalHeight;
                            if (result.width !== plan.width || result.height !== plan.height)
                                throw new Error("The displayed image dimensions do not match the decode plan.");
                            arrange();
                            request.ready = true;
                            invalidate();
                        }
                    }
                }
            } catch (error) {
                if (request.canceled) result.canceled = true;
                else result.error = error instanceof Error ? error.message : "Native image decoding failed.";
            } finally {
                bitmap?.close();
                if (canvas) canvas.width = canvas.height = 0;
                if (result.canceled || result.error) {
                    if (current === request) image.removeAttribute("src");
                    request.release();
                }
            }
            return {
                result() { return result; },
                dispose() {
                    if (disposed) return;
                    disposed = true;
                    request.canceled = true;
                    if (current === request) { image.removeAttribute("src"); current = null; invalidate(); }
                    request.release();
                }
            };
        },
        dispose() { image.removeEventListener("error", failed); resize.disconnect(); cancel(); }
    };
}
