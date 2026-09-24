import { test, expect } from "./fixtures.js";
import { adapterFixture } from "./adapter-fixtures.js";

test.beforeEach(async ({ page }) => {
    await adapterFixture(page);
    await page.evaluate(() => {
        surface.create(1, state("Stack"), callback);
        surface.create(2, state("ScrollView", { flex: 1 }), callback);
        surface.create(3, state("Stack"), callback);
        surface.addChild(2, 3);
        surface.addChild(1, 2);
        surface.mount(1);
        window.requests = [];
        window.viewportCallback = { invokeMethodAsync: async (method, epoch, committedSourceVersion, requestedSourceVersion, committed, requested, isBlocked) => {
            if (method !== "Request") throw new Error("Unexpected viewport method.");
            const request = { epoch, committedSourceVersion, requestedSourceVersion, committed, requested, isBlocked };
            requests.push(request);
            window.requestAction?.(request);
        } };
        window.unitRows = new Map();
        window.atNextRequest = (offset, action) => new Promise((resolve, reject) => {
            window.requestAction = request => {
                window.requestAction = null;
                try { resolve(action(request)); } catch (error) { reject(error); }
            };
            lease.requestOffset(offset);
        });
        window.stage = request => {
            const first = Math.floor(request.requested.offset / 64);
            const end = Math.min(100, Math.ceil((request.requested.offset + request.requested.height) / 64));
            for (let index = first; index < end; index++) {
                if (unitRows.has(index)) continue;
                const row = 10 + index, input = 1000 + index;
                surface.create(row, state("Stack"), callback);
                surface.create(input, state("TextInput", { text: `Row ${index}`, captionVisible: false }), callback);
                surface.addChild(row, input);
                const before = [...unitRows.keys()].filter(key => key < index).length;
                surface.insertChild(3, before, row);
                unitRows.set(index, { row, input });
            }
            const frames = [{ id: 3, x: 0, y: 0, width: request.requested.width, height: request.requested.extent, container: true }];
            for (const [index, { row, input }] of unitRows) {
                surface.setVirtualItemInfo(row, { key: `opaque-key:${index}`, index, count: 100, sourceVersion: request.requestedSourceVersion });
                frames.push({ id: row, x: 0, y: index * 64, width: request.requested.width, height: 64, container: true });
                frames.push({ id: input, x: 0, y: 0, width: request.requested.width, height: 32, container: false });
            }
            surface.applyFrames(frames);
        };
    });
});

test("native viewport callbacks are asynchronous exact Int64 snapshots and invalid commits never expose missing rows", async ({ page }) => {
    expect(await page.evaluate(() => {
        window.lease = surface.beginVirtualViewport(2, 100, 64, "9007199254740993", viewportCallback);
        return requests.length;
    })).toBe(0);
    await expect.poll(() => page.evaluate(() => requests.length)).toBeGreaterThan(0);
    const initial = await page.evaluate(() => requests.at(-1));
    expect(initial.requestedSourceVersion).toBe("9007199254740993");
    expect(initial.committedSourceVersion).toBe("0");
    expect(initial.committed.height).toBe(0);
    expect(await page.locator(".xui-virtual-clip").evaluate(node => node.getBoundingClientRect().height)).toBe(0);
    const outcome = await page.evaluate(() => atNextRequest(0, request => {
        const begin = lease.tryBeginUpdate(request.epoch);
        let error;
        try { lease.tryCommit(request.epoch); } catch (failure) { error = failure.message; }
        lease.cancel(request.epoch);
        return { begin, error, committed: document.querySelector(".xui-scrollview").dataset.xuiCommittedHeight };
    }));
    expect(outcome.begin).toBe("Ready");
    expect(outcome.error).toContain("not realized");
    expect(outcome.committed).toBe("0");
    await page.evaluate(() => lease.requestOffset(0));
    await expect.poll(() => page.evaluate(() => requests.at(-1).epoch)).not.toBe(initial.epoch);
    expect(await page.evaluate(() => atNextRequest(0, request => {
        lease.tryBeginUpdate(request.epoch);
        stage(request);
        return lease.tryCommit(request.epoch);
    }))).toBe("Committed");
    await expect(page.locator(".xui-scrollview")).toHaveAttribute("data-xui-committed-source", "9007199254740993");
    await expect(page.getByRole("listitem").first()).toHaveAttribute("aria-posinset", "1");
    await expect(page.getByRole("listitem").first()).toHaveAttribute("aria-setsize", "100");
    const calls = await page.evaluate(() => { lease.dispose(); return requests.length; });
    await expect(page.getByRole("list")).toBeHidden();
    await page.evaluate(() => { document.querySelector(".xui-virtual-intent").scrollTop = 500; });
    await page.evaluate(() => new Promise(requestAnimationFrame));
    expect(await page.evaluate(() => requests.length)).toBe(calls);
});

test("scroll intent and growth stay outside the committed native layer during composition", async ({ page }) => {
    await page.evaluate(() => {
        window.commitOnDelivery = request => {
            if (lease.tryBeginUpdate(request.epoch) !== "Ready") throw new Error("A newly delivered unblocked request was not ready.");
            stage(request);
            if (lease.tryCommit(request.epoch) !== "Committed") throw new Error("The reserved viewport did not commit.");
            window.requestAction = null;
        };
        window.requestAction = window.commitOnDelivery;
        window.lease = surface.beginVirtualViewport(2, 100, 64, "1", viewportCallback);
    });
    await expect(page.locator(".xui-scrollview")).toHaveAttribute("data-xui-committed-height", "300");
    await page.evaluate(() => {
        window.requestAction = request => {
            if (request.isBlocked) window.blockedObservation = {
                requested: request.requested,
                offset: document.querySelector(".xui-scrollview").dataset.xuiCommittedOffset,
                height: document.querySelector(".xui-virtual-clip").getBoundingClientRect().height,
                begin: lease.tryBeginUpdate(request.epoch),
                focused: document.activeElement === pinned,
                selection: [pinned.selectionStart, pinned.selectionEnd]
            };
        };
        window.pinned = document.querySelector(".xui-textinput input");
        pinned.focus();
        pinned.setSelectionRange(1, 3);
        pinned.dispatchEvent(new CompositionEvent("compositionstart", { bubbles: true }));
        document.querySelector(".xui-virtual-intent").scrollTop = 3200;
        document.querySelector("#mount").style.height = "450px";
    });
    await expect.poll(() => page.evaluate(() => window.blockedObservation?.requested)).toMatchObject({ offset: 3200, height: 450 });
    const blocked = await page.evaluate(() => window.blockedObservation);
    expect(blocked.requested.offset).toBe(3200);
    expect(blocked.requested.height).toBe(450);
    expect(blocked).toMatchObject({ offset: "0", height: 300, begin: "Blocked", focused: true, selection: [1, 3] });
    await page.evaluate(() => {
        window.requestAction = window.commitOnDelivery;
        pinned.dispatchEvent(new CompositionEvent("compositionend", { bubbles: true }));
    });
    await expect(page.locator(".xui-scrollview")).toHaveAttribute("data-xui-committed-offset", "3200");
    expect(await page.evaluate(() => document.activeElement === pinned)).toBe(true);
    await page.evaluate(() => lease.dispose());
});

test("new native intent supersedes old epochs while reservations queue newer intent and versions stay exact", async ({ page }) => {
    await page.evaluate(() => { window.lease = surface.beginVirtualViewport(2, 100, 64, "9007199254740993", viewportCallback); });
    await expect.poll(() => page.evaluate(() => requests.length)).toBeGreaterThan(0);
    const first = await page.evaluate(() => requests.at(-1));
    await page.evaluate(() => lease.requestOffset(64));
    expect(await page.evaluate(epoch => lease.tryBeginUpdate(epoch), first.epoch)).toBe("Superseded");
    await expect.poll(() => page.evaluate(() => requests.at(-1).epoch)).not.toBe(first.epoch);
    const reserved = await page.evaluate(() => atNextRequest(64, request => {
        lease.tryBeginUpdate(request.epoch);
        stage(request);
        lease.requestOffset(128);
        const committed = lease.tryCommit(request.epoch);
        return { epoch: request.epoch, committed, visible: document.querySelector(".xui-scrollview").dataset.xuiCommittedOffset };
    }));
    expect(reserved).toMatchObject({ committed: "Committed", visible: "64" });
    await expect.poll(() => page.evaluate(() => requests.at(-1).epoch)).not.toBe(reserved.epoch);
    expect(await page.evaluate(() => requests.at(-1).requested.offset)).toBe(128);
    await page.evaluate(() => atNextRequest(128, request => {
        lease.tryBeginUpdate(request.epoch);
        stage(request);
        lease.tryCommit(request.epoch);
        lease.setExtent(100, "9223372036854775807");
    }));
    await expect.poll(() => page.evaluate(() => requests.at(-1).requestedSourceVersion)).toBe("9223372036854775807");
    expect(await page.evaluate(() => atNextRequest(128, request => {
        lease.tryBeginUpdate(request.epoch);
        stage(request);
        lease.tryCommit(request.epoch);
        let rejected = false;
        try { lease.setExtent(100, "9223372036854775808"); } catch (error) { rejected = error instanceof RangeError; }
        return { rejected, source: document.querySelector(".xui-scrollview").dataset.xuiCommittedSource,
            initialHeight: requests[0].committed.height, initialSource: requests[0].committedSourceVersion };
    }))).toEqual({ rejected: true, source: "9223372036854775807", initialHeight: 0, initialSource: "0" });
    await page.evaluate(() => lease.dispose());
});

test("begin rejects focused content before moving it or registering a viewport", async ({ page }) => {
    const result = await page.evaluate(() => {
        surface.create(10, state("TextInput", { text: "Kept" }), callback);
        surface.insertChild(3, 0, 10);
        const input = document.querySelector("input");
        input.focus();
        input.setSelectionRange(1, 3);
        const parent = input.parentElement.parentElement;
        let rejected = false;
        try { surface.beginVirtualViewport(2, 100, 64, "1", viewportCallback); }
        catch (error) { rejected = error.message.includes("before focusing"); }
        return { rejected, sameParent: input.parentElement.parentElement === parent,
            focused: document.activeElement === input, selection: [input.selectionStart, input.selectionEnd],
            wrappers: document.querySelectorAll(".xui-virtual-intent, .xui-virtual-clip").length };
    });
    expect(result).toEqual({ rejected: true, sameParent: true, focused: true, selection: [1, 3], wrappers: 0 });
});

test("unrepresentable browser extents fail explicitly and unwind the temporary viewport structure", async ({ page }) => {
    const result = await page.evaluate(() => {
        const scroll = document.querySelector(".xui-scrollview");
        const content = scroll.firstElementChild;
        let error;
        try { surface.beginVirtualViewport(2, 2147483647, 128, "1", viewportCallback); }
        catch (failure) { error = failure.message; }
        return { error, sameContent: scroll.firstElementChild === content,
            wrappers: document.querySelectorAll(".xui-virtual-intent,.xui-virtual-clip").length, requests: requests.length };
    });
    expect(result.error).toContain("cannot represent");
    expect(result).toMatchObject({ sameContent: true, wrappers: 0, requests: 0 });
});

test("explicit settlement validates current native geometry without consuming queued future intent", async ({ page }) => {
    await page.evaluate(() => { window.lease = surface.beginVirtualViewport(2, 100, 64, "1", viewportCallback); });
    await expect.poll(() => page.evaluate(() => requests.length)).toBeGreaterThan(0);
    const first = await page.evaluate(() => atNextRequest(0, request => {
        lease.tryBeginUpdate(request.epoch);
        stage(request);
        lease.tryCommit(request.epoch);
        const callbacks = requests.length;
        lease.flushCommitted(request.epoch);
        lease.requestOffset(64);
        lease.flushCommitted(request.epoch);
        return { epoch: request.epoch, callbacks, after: requests.length,
            offset: document.querySelector(".xui-scrollview").dataset.xuiCommittedOffset };
    }));
    expect(first.after).toBe(first.callbacks);
    expect(first.offset).toBe("0");
    await expect.poll(() => page.evaluate(() => requests.at(-1).requested.offset)).toBe(64);
    const result = await page.evaluate(previousEpoch => atNextRequest(64, request => {
        lease.tryBeginUpdate(request.epoch);
        let reservedRejected = false;
        try { lease.flushCommitted(previousEpoch); } catch (error) { reservedRejected = error.message.includes("without a reserved update"); }
        lease.cancel(request.epoch);
        const row = document.querySelector('[role="listitem"]');
        row.style.setProperty("--xui-height", "1px");
        let geometryRejected = false;
        try { lease.flushCommitted(previousEpoch); } catch (error) { geometryRejected = error.message.includes("native geometry"); }
        lease.dispose();
        return { reservedRejected, geometryRejected };
    }), first.epoch);
    expect(result).toEqual({ reservedRejected: true, geometryRejected: true });
});
