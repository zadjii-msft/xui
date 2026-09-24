using System;
using System.Collections.Generic;
using System.Text.Json;

namespace PortableDemo;

public interface IApplicationScenarioDriver
{
    void Change(string id, string value);
    void Click(string id);
    void Submit(string id);
    string Text(string id);
    bool Enabled(string id);
    bool Visible(string id);
}

public static class ApplicationScenarioRunner
{
    public static int Run(string json, string applicationId, IApplicationScenarioDriver driver)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(applicationId);
        ArgumentNullException.ThrowIfNull(driver);
        using var document = JsonDocument.Parse(json);
        var root = document.RootElement;
        CheckProperties(root, "version", "applications");
        if (root.GetProperty("version").GetInt32() != 1) throw new InvalidOperationException("Unsupported application scenario version.");
        var applications = NonemptyArray(root, "applications");
        var ids = new HashSet<string>(StringComparer.Ordinal);
        JsonElement? selected = null;
        foreach (var application in applications.EnumerateArray())
        {
            CheckProperties(application, "id", "reset", "scenarios");
            string id = RequiredString(application, "id");
            if (!ids.Add(id)) throw new InvalidOperationException($"Duplicate application '{id}'.");
            RequiredString(application, "reset");
            NonemptyArray(application, "scenarios");
            if (id == applicationId) selected = application;
        }
        var app = selected ?? throw new InvalidOperationException($"Application '{applicationId}' not found.");
        string reset = RequiredString(app, "reset");
        int expectations = 0;
        var names = new HashSet<string>(StringComparer.Ordinal);
        foreach (var scenario in app.GetProperty("scenarios").EnumerateArray())
        {
            CheckProperties(scenario, "name", "steps");
            string name = RequiredString(scenario, "name");
            if (!names.Add(name)) throw new InvalidOperationException($"Duplicate scenario '{name}'.");
            var steps = NonemptyArray(scenario, "steps");
            int index = 0;
            int beforeScenario = expectations;
            foreach (var step in steps.EnumerateArray())
            {
                index++;
                try
                {
                    string action = RequiredString(step, "action");
                    string id = RequiredString(step, "id");
                    if (index == 1 && (action != "click" || id != reset))
                        throw new InvalidOperationException($"Each scenario must start by clicking '{reset}'.");
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
                                if (expected != actual) throw new InvalidOperationException($"'{id}' text: expected '{expected}', got '{actual}'.");
                                expectations++;
                            }
                            if (step.TryGetProperty("enabled", out var enabled))
                            {
                                bool expected = enabled.GetBoolean();
                                if (driver.Enabled(id) != expected) throw new InvalidOperationException($"'{id}' enabled: expected {expected}.");
                                expectations++;
                            }
                            if (step.TryGetProperty("visible", out var visible))
                            {
                                bool expected = visible.GetBoolean();
                                if (driver.Visible(id) != expected) throw new InvalidOperationException($"'{id}' visible: expected {expected}.");
                                expectations++;
                            }
                            if (before == expectations) throw new InvalidOperationException("Expect must specify text, enabled, or visible.");
                            break;
                        default:
                            throw new InvalidOperationException($"Unknown scenario action '{action}'.");
                    }
                }
                catch (Exception error)
                {
                    throw new InvalidOperationException($"Application '{applicationId}', scenario '{name}', step {index}: {error.Message}", error);
                }
            }
            if (expectations == beforeScenario) throw new InvalidOperationException($"Scenario '{name}' must contain expectations.");
        }
        return expectations;
    }

    private static JsonElement NonemptyArray(JsonElement element, string property)
    {
        var value = element.GetProperty(property);
        return value.ValueKind == JsonValueKind.Array && value.GetArrayLength() > 0 ? value
            : throw new InvalidOperationException($"'{property}' must be a nonempty array.");
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
            if (Array.IndexOf(allowed, property.Name) < 0 || !seen.Add(property.Name))
                throw new InvalidOperationException($"Unexpected or duplicate scenario property '{property.Name}'.");
    }
}
