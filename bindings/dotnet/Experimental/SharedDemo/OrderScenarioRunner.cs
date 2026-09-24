using System;
using System.Collections.Generic;
using System.Text.Json;

namespace PortableDemo;

public interface IOrderScenarioDriver
{
    void Change(string id, string value);
    void Click(string id);
    void Submit(string id);
    string Text(string id);
    bool Enabled(string id);
    bool Visible(string id);
}

public static class OrderScenarioRunner
{
    public static int Run(string json, IOrderScenarioDriver driver)
    {
        ArgumentNullException.ThrowIfNull(driver);
        using var document = JsonDocument.Parse(json);
        var root = document.RootElement;
        CheckProperties(root, "version", "scenarios");
        if (root.GetProperty("version").GetInt32() != 1) throw new InvalidOperationException("Unsupported order scenario version.");
        var scenarios = root.GetProperty("scenarios");
        if (scenarios.ValueKind != JsonValueKind.Array || scenarios.GetArrayLength() == 0)
            throw new InvalidOperationException("Order scenarios must be a nonempty array.");
        int expectations = 0;
        var names = new HashSet<string>(StringComparer.Ordinal);
        foreach (var scenario in scenarios.EnumerateArray())
        {
            CheckProperties(scenario, "name", "steps");
            string name = RequiredString(scenario, "name");
            if (!names.Add(name)) throw new InvalidOperationException($"Duplicate order scenario '{name}'.");
            var steps = scenario.GetProperty("steps");
            if (steps.ValueKind != JsonValueKind.Array || steps.GetArrayLength() == 0)
                throw new InvalidOperationException($"Order scenario '{name}' must have steps.");
            int index = 0;
            foreach (var step in steps.EnumerateArray())
            {
                index++;
                try
                {
                    string action = RequiredString(step, "action");
                    string id = RequiredString(step, "id");
                    if (index == 1 && (action != "click" || id != "reset"))
                        throw new InvalidOperationException("Each scenario must start by clicking reset.");
                    switch (action)
                    {
                        case "change":
                            CheckProperties(step, "action", "id", "value");
                            driver.Change(id, step.GetProperty("value").GetString() ?? throw new InvalidOperationException("Change value must be a string."));
                            break;
                        case "click":
                            CheckProperties(step, "action", "id");
                            driver.Click(id);
                            break;
                        case "submit":
                            CheckProperties(step, "action", "id");
                            driver.Submit(id);
                            break;
                        case "expect":
                            CheckProperties(step, "action", "id", "text", "enabled", "visible");
                            int before = expectations;
                            if (step.TryGetProperty("text", out var text))
                            {
                                string expected = text.GetString() ?? throw new InvalidOperationException("Expected text must be a string.");
                                string actual = driver.Text(id);
                                if (!string.Equals(expected, actual, StringComparison.Ordinal))
                                    throw new InvalidOperationException($"'{id}' text: expected '{expected}', got '{actual}'.");
                                expectations++;
                            }
                            if (step.TryGetProperty("enabled", out var enabled))
                            {
                                bool expected = enabled.GetBoolean();
                                bool actual = driver.Enabled(id);
                                if (expected != actual) throw new InvalidOperationException($"'{id}' enabled: expected {expected}, got {actual}.");
                                expectations++;
                            }
                            if (step.TryGetProperty("visible", out var visible))
                            {
                                bool expected = visible.GetBoolean();
                                bool actual = driver.Visible(id);
                                if (expected != actual) throw new InvalidOperationException($"'{id}' visible: expected {expected}, got {actual}.");
                                expectations++;
                            }
                            if (before == expectations) throw new InvalidOperationException("Expect must specify text, enabled, or visible.");
                            break;
                        default:
                            throw new InvalidOperationException($"Unknown order scenario action '{action}'.");
                    }
                }
                catch (Exception error)
                {
                    throw new InvalidOperationException($"Order scenario '{name}', step {index}: {error.Message}", error);
                }
            }
        }
        return expectations;
    }

    private static string RequiredString(JsonElement element, string property)
    {
        string? value = element.GetProperty(property).GetString();
        return !string.IsNullOrWhiteSpace(value) ? value : throw new InvalidOperationException($"'{property}' must be a nonempty string.");
    }

    private static void CheckProperties(JsonElement element, params string[] allowed)
    {
        var seen = new HashSet<string>(StringComparer.Ordinal);
        foreach (var property in element.EnumerateObject())
        {
            if (Array.IndexOf(allowed, property.Name) < 0 || !seen.Add(property.Name))
                throw new InvalidOperationException($"Unexpected or duplicate order scenario property '{property.Name}'.");
        }
    }
}
