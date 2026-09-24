import { test, expect, demo, command } from "./fixtures.js";

test("public C# Host lease transports Int64 versions exactly and delivers only after Begin returns", async ({ page }) => {
    await demo(page);
    expect(await command(page, "viewport-probe-start")).toEqual({ requests: [] });
    await expect.poll(async () => (await command(page, "viewport-probe-state")).requests.length).toBeGreaterThan(0);
    let snapshot = await command(page, "viewport-probe-state");
    expect(snapshot.requests.at(-1)).toMatchObject({ committedSourceVersion: "0", requestedSourceVersion: "9007199254740993" });
    expect(await command(page, "viewport-probe-commit")).toEqual({ result: "Committed" });
    await command(page, "viewport-probe-max");
    await expect.poll(async () => (await command(page, "viewport-probe-state")).requests.at(-1).requestedSourceVersion).toBe("9223372036854775807");
    expect(await command(page, "viewport-probe-commit")).toEqual({ result: "Committed" });
    snapshot = await command(page, "viewport-probe-close");
    await expect(page.getByRole("list")).toBeHidden();
    await page.setViewportSize({ width: 640, height: 360 });
    await page.evaluate(() => new Promise(requestAnimationFrame));
    expect(await command(page, "viewport-probe-state")).toEqual(snapshot);
});
