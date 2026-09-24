using System;
using System.Collections.Generic;
using System.Text.Json;

namespace PortableDemo;

public interface IWorkspaceStudioScenarioDriver
{
    void CreateFreshWorkspace();
    void SelectSection(ulong id);
    void SelectTab(ulong id);
    void CloseTab(ulong id);
    void SelectOperationsScope(ulong id);
    void OpenDocument(string key);
    void Change(string id, string value);
    void Click(string id);
    void AwaitAnalysis();
    bool NavigateBack();
    bool BackBlocked();
    string Text(string id);
    bool Enabled(string id);
    int OpenTabCount();
    ulong? SelectedTab();
}

public static class WorkspaceStudioScenarioRunner
{
    public static int Run(string json, IWorkspaceStudioScenarioDriver driver)
    {
        ArgumentNullException.ThrowIfNull(driver);
        using var document = JsonDocument.Parse(json);
        var root = document.RootElement;
        Properties(root, "version", "scenarios");
        if (root.GetProperty("version").GetInt32() != 1) throw new InvalidOperationException("Unsupported studio scenario version.");
        var scenarios = root.GetProperty("scenarios");
        if (scenarios.ValueKind != JsonValueKind.Array || scenarios.GetArrayLength() == 0)
            throw new InvalidOperationException("Studio scenarios must be nonempty.");
        var names = new HashSet<string>(StringComparer.Ordinal);
        int checks = 0;
        foreach (var scenario in scenarios.EnumerateArray())
        {
            Properties(scenario, "name", "steps");
            string name = String(scenario, "name");
            if (!names.Add(name)) throw new InvalidOperationException("Duplicate studio scenario.");
            var steps = scenario.GetProperty("steps");
            if (steps.ValueKind != JsonValueKind.Array || steps.GetArrayLength() == 0)
                throw new InvalidOperationException("Studio scenario steps must be nonempty.");
            driver.CreateFreshWorkspace();
            int index = 0;
            int beforeScenario = checks;
            foreach (var step in steps.EnumerateArray())
            {
                index++;
                try
                {
                    string action = String(step, "action");
                    switch (action)
                    {
                        case "selectSection":
                            Properties(step, "action", "id");
                            driver.SelectSection(step.GetProperty("id").GetUInt64());
                            break;
                        case "selectTab":
                            Properties(step, "action", "id");
                            driver.SelectTab(step.GetProperty("id").GetUInt64());
                            break;
                        case "closeTab":
                            Properties(step, "action", "id");
                            driver.CloseTab(step.GetProperty("id").GetUInt64());
                            break;
                        case "selectOperationsScope":
                            Properties(step, "action", "id");
                            driver.SelectOperationsScope(step.GetProperty("id").GetUInt64());
                            break;
                        case "openDocument":
                            Properties(step, "action", "key");
                            driver.OpenDocument(String(step, "key"));
                            break;
                        case "change":
                            Properties(step, "action", "id", "value");
                            driver.Change(String(step, "id"), step.GetProperty("value").GetString() ??
                                throw new InvalidOperationException("Change value must be a string."));
                            break;
                        case "click":
                            Properties(step, "action", "id");
                            driver.Click(String(step, "id"));
                            break;
                        case "awaitAnalysis":
                        case "awaitOperations":
                            Properties(step, "action");
                            driver.AwaitAnalysis();
                            break;
                        case "back":
                            Properties(step, "action", "handled", "blocked");
                            if (driver.NavigateBack() != step.GetProperty("handled").GetBoolean())
                                throw new InvalidOperationException("Shared Back handling did not match.");
                            if (driver.BackBlocked() != step.GetProperty("blocked").GetBoolean())
                                throw new InvalidOperationException("Shared Back blocked state did not match.");
                            checks += 2;
                            break;
                        case "expectTabs":
                            Properties(step, "action", "count", "selected");
                            if (driver.OpenTabCount() != step.GetProperty("count").GetInt32())
                                throw new InvalidOperationException("Native open tab count did not match.");
                            var selected = step.GetProperty("selected");
                            ulong? expected = selected.ValueKind == JsonValueKind.Null ? null : selected.GetUInt64();
                            if (driver.SelectedTab() != expected) throw new InvalidOperationException("Native selected tab did not match.");
                            checks += 2;
                            break;
                        case "expect":
                            Properties(step, "action", "id", "text", "enabled");
                            string id = String(step, "id");
                            int before = checks;
                            if (step.TryGetProperty("text", out var text))
                            {
                                string expectedText = text.GetString() ?? throw new InvalidOperationException("Expected text must be a string.");
                                if (driver.Text(id) != expectedText) throw new InvalidOperationException($"Text did not match for '{id}'.");
                                checks++;
                            }
                            if (step.TryGetProperty("enabled", out var enabled))
                            {
                                if (driver.Enabled(id) != enabled.GetBoolean()) throw new InvalidOperationException($"Enabled did not match for '{id}'.");
                                checks++;
                            }
                            if (checks == before) throw new InvalidOperationException("Expect requires a property.");
                            break;
                        default: throw new InvalidOperationException("Unknown studio scenario action.");
                    }
                }
                catch (Exception error)
                {
                    throw new InvalidOperationException($"Studio scenario '{name}', step {index}: {error.Message}", error);
                }
            }
            if (checks == beforeScenario) throw new InvalidOperationException("Studio scenario requires expectations.");
        }
        return checks;
    }
    private static string String(JsonElement element, string property)
    {
        string? value = element.GetProperty(property).GetString();
        return !string.IsNullOrWhiteSpace(value) ? value : throw new InvalidOperationException($"'{property}' must be a nonempty string.");
    }
    private static void Properties(JsonElement element, params string[] allowed)
    {
        var seen = new HashSet<string>(StringComparer.Ordinal);
        foreach (var property in element.EnumerateObject())
            if (Array.IndexOf(allowed, property.Name) < 0 || !seen.Add(property.Name))
                throw new InvalidOperationException("Unexpected or duplicate studio scenario property.");
    }
}
