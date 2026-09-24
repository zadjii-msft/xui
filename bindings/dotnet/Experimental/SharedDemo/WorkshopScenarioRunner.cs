using System;
using System.Collections.Generic;
using System.Text.Json;

namespace PortableDemo;

public interface IWorkshopScenarioDriver
{
    void Click(string id);
    void Change(string id, string value);
    string Text(string id);
    bool Enabled(string id);
    bool Visible(string id);
    bool Checked(string id);
    bool ReadOnly(string id);
    int MaximumLength(string id);
    string Purpose(string id);
}

public static class WorkshopScenarioRunner
{
    public static int Run(string json, IWorkshopScenarioDriver driver)
    {
        ArgumentNullException.ThrowIfNull(driver);
        using var document = JsonDocument.Parse(json);
        var root = document.RootElement;
        Properties(root, "version", "scenarios");
        if (root.GetProperty("version").GetInt32() != 1) throw new InvalidOperationException("Unknown workshop scenario version.");
        var scenarios = Array(root, "scenarios");
        var names = new HashSet<string>(StringComparer.Ordinal);
        int checks = 0;
        foreach (var scenario in scenarios.EnumerateArray())
        {
            Properties(scenario, "name", "steps");
            string name = Required(scenario, "name");
            if (!names.Add(name)) throw new InvalidOperationException("Duplicate workshop scenario.");
            int index = 0;
            int beforeScenario = checks;
            foreach (var step in Array(scenario, "steps").EnumerateArray())
            {
                index++;
                try
                {
                    string action = Required(step, "action");
                    string id = Required(step, "id");
                    if (index == 1 && (action != "click" || id != "workshop-reset"))
                        throw new InvalidOperationException("Scenario must begin with workshop-reset.");
                    if (action == "click") { Properties(step, "action", "id"); driver.Click(id); }
                    else if (action == "change")
                    {
                        Properties(step, "action", "id", "value");
                        driver.Change(id, step.GetProperty("value").GetString() ?? throw new InvalidOperationException("Change requires string value."));
                    }
                    else if (action == "expect")
                    {
                        Properties(step, "action", "id", "text", "enabled", "visible", "checked", "readOnly", "maximumLength", "purpose");
                        int before = checks;
                        void Check<T>(string property, T expected, T actual)
                        {
                            if (!EqualityComparer<T>.Default.Equals(expected, actual))
                                throw new InvalidOperationException($"'{id}' {property}: expected '{expected}', got '{actual}'.");
                            checks++;
                        }
                        if (step.TryGetProperty("text", out var text))
                            Check("text", text.GetString() ?? throw new InvalidOperationException("Expected text must be a string."), driver.Text(id));
                        if (step.TryGetProperty("enabled", out var enabled)) Check("enabled", enabled.GetBoolean(), driver.Enabled(id));
                        if (step.TryGetProperty("visible", out var visible)) Check("visible", visible.GetBoolean(), driver.Visible(id));
                        if (step.TryGetProperty("checked", out var check)) Check("checked", check.GetBoolean(), driver.Checked(id));
                        if (step.TryGetProperty("readOnly", out var readOnly)) Check("readOnly", readOnly.GetBoolean(), driver.ReadOnly(id));
                        if (step.TryGetProperty("maximumLength", out var max)) Check("maximumLength", max.GetInt32(), driver.MaximumLength(id));
                        if (step.TryGetProperty("purpose", out _)) Check("purpose", Required(step, "purpose"), driver.Purpose(id));
                        if (checks == before) throw new InvalidOperationException("Expect requires a property.");
                    }
                    else throw new InvalidOperationException("Unknown workshop action.");
                }
                catch (Exception error)
                {
                    throw new InvalidOperationException($"Workshop '{name}', step {index}: {error.Message}", error);
                }
            }
            if (checks == beforeScenario) throw new InvalidOperationException("Scenario requires expectations.");
        }
        return checks;
    }
    private static string Required(JsonElement element, string name)
    {
        string? value = element.GetProperty(name).GetString();
        return !string.IsNullOrWhiteSpace(value) ? value : throw new InvalidOperationException($"'{name}' must be a nonempty string.");
    }
    private static JsonElement Array(JsonElement element, string name)
    {
        var value = element.GetProperty(name);
        return value.ValueKind == JsonValueKind.Array && value.GetArrayLength() > 0 ? value :
            throw new InvalidOperationException($"'{name}' must be a nonempty array.");
    }
    private static void Properties(JsonElement element, params string[] allowed)
    {
        var seen = new HashSet<string>(StringComparer.Ordinal);
        foreach (var property in element.EnumerateObject())
            if (System.Array.IndexOf(allowed, property.Name) < 0 || !seen.Add(property.Name))
                throw new InvalidOperationException($"Unexpected or duplicate '{property.Name}'.");
    }
}
