import { readFileSync } from "node:fs";
import { test, expect, byId } from "./fixtures.js";

export { test, expect, byId };
export { runScenarioSteps as runSteps } from "./fixtures.js";
export const inputIds = ["customer-name", "email", "discount-code"];
const corpus = JSON.parse(readFileSync(new URL("../SharedDemo/OrderScenarios.json", import.meta.url), "utf8"));
if (corpus.version !== 1 || !Array.isArray(corpus.scenarios) || !corpus.scenarios.length)
    throw new Error("Expected the version 1 shared order scenario corpus.");
export const scenarios = corpus.scenarios;
const names = new Set();
for (const scenario of scenarios) {
    if (typeof scenario.name !== "string" || !scenario.name || names.has(scenario.name) ||
        !Array.isArray(scenario.steps) || !scenario.steps.length ||
        scenario.steps[0].action !== "click" || scenario.steps[0].id !== "reset")
        throw new Error("Every shared scenario needs a unique name and an initial reset click.");
    names.add(scenario.name);
    for (const step of scenario.steps) {
        const fields = {
            change: ["action", "id", "value"], click: ["action", "id"],
            submit: ["action", "id"], expect: ["action", "id", "text", "enabled", "visible"]
        }[step.action];
        if (!fields || Object.keys(step).some(key => !fields.includes(key)) ||
            typeof step.id !== "string" || !step.id ||
            (step.action === "change" && typeof step.value !== "string") ||
            (step.action === "expect" && !["text", "enabled", "visible"].some(key => Object.hasOwn(step, key))) ||
            (Object.hasOwn(step, "text") && typeof step.text !== "string") ||
            ["enabled", "visible"].some(key => Object.hasOwn(step, key) && typeof step[key] !== "boolean"))
            throw new Error(`Invalid shared scenario step in ${scenario.name}: ${JSON.stringify(step)}`);
    }
}
export const fullOrder = scenarios.find(scenario => scenario.name === "full-order-review");
if (!fullOrder) throw new Error("The shared corpus must include its full-order-review checkpoint.");
export const fullOrderExpectations = fullOrder.steps.slice(fullOrder.steps.findLastIndex(step => step.action !== "expect") + 1);
if (!fullOrderExpectations.some(step => step.id === "review-summary" && step.visible === true))
    throw new Error("The full-order-review scenario must finish with a visible review checkpoint.");

export const inputById = (page, id) => byId(page, id).locator("input");

export async function order(page) {
    await page.goto("./?test");
    await expect(inputById(page, "customer-name")).toBeVisible();
    await expect(page.locator("#app input")).toHaveCount(inputIds.length);
    expect(await page.evaluate(() => typeof window.xuiTest)).toBe("undefined");
}
