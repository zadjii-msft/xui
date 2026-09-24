export function createFilePicker(callback, maximumBytes) {
    const limit = BigInt(maximumBytes);
    if (limit <= 0n) throw new RangeError("A positive file read limit is required.");
    let input;
    let file;
    let position = 0;
    let finished = false;
    let released = false;
    let reading = false;

    function removeInput() {
        if (!input) return;
        input.removeEventListener("change", changed);
        input.removeEventListener("cancel", canceled);
        input.value = "";
        input.remove();
        input = undefined;
    }

    function release() {
        if (released) return;
        released = true;
        finished = true;
        removeInput();
        file = undefined;
        callback = undefined;
    }

    function deliver(status, name = "", size = 0, error = "") {
        if (finished || released) return;
        finished = true;
        removeInput();
        const receiver = callback;
        callback = undefined;
        receiver.invokeMethodAsync("Receive", status, name, size, error).catch(failure => {
            console.error("XUI file selection callback failed.", failure);
            release();
        });
    }

    function changed() {
        if (released || finished) return;
        const files = input.files;
        if (!files || files.length === 0) { deliver("cancelled"); return; }
        if (files.length !== 1) { deliver("failed", "", 0, "The picker returned multiple files."); return; }
        const selected = files[0];
        if (!Number.isSafeInteger(selected.size) || selected.size < 0) {
            deliver("failed", "", 0, "The selected file has an invalid size.");
            return;
        }
        if (BigInt(selected.size) > limit) { deliver("oversized"); return; }
        file = selected;
        deliver("completed", selected.name, selected.size);
    }

    function canceled() { deliver("cancelled"); }

    return {
        show() {
            if (released || input || finished) throw new Error("The picker request is no longer available.");
            input = document.createElement("input");
            input.type = "file";
            input.multiple = false;
            input.hidden = true;
            if (typeof input.showPicker !== "function") { deliver("unsupported"); return; }
            if (!navigator.userActivation?.isActive) { deliver("denied"); return; }
            input.addEventListener("change", changed);
            input.addEventListener("cancel", canceled);
            document.body.append(input);
            try { input.showPicker(); }
            catch (error) {
                if (error.name === "NotAllowedError" || error.name === "SecurityError") deliver("denied");
                else if (error.name === "NotSupportedError") deliver("unsupported");
                else deliver("failed", "", 0, String(error));
            }
        },
        async read(count) {
            if (released || !file) throw new Error("The selected file has been released.");
            if (!Number.isInteger(count) || count < 0 || count > 65536) throw new RangeError("Invalid file read chunk size.");
            if (reading) throw new Error("A selected file read is already pending.");
            if (count === 0 || position === file.size) return new Uint8Array(0);
            reading = true;
            try {
                const end = Math.min(file.size, position + count);
                const bytes = new Uint8Array(await file.slice(position, end).arrayBuffer());
                if (released) throw new Error("The selected file was released during a read.");
                if (bytes.length !== end - position) throw new Error("The selected file returned an incomplete chunk.");
                position = end;
                return bytes;
            } finally { reading = false; }
        },
        release
    };
}
