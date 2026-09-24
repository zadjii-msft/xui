using System;
using System.Collections.Generic;
using System.Text.Json;

namespace PortableDemo;

public readonly record struct SettingsProgressSnapshot(double Minimum, double Maximum, double? Value, bool Indeterminate);

public interface ISettingsScenarioDriver
{
    void Click(string id);
    void Change(string id, string value);
    string Text(string id);
    bool Enabled(string id);
    bool Visible(string id);
    bool Checked(string id);
    string CheckState(string id);
    SettingsProgressSnapshot Progress(string id);
}

public static class SettingsScenarioRunner
{
    public static int Run(string json, ISettingsScenarioDriver driver)
    {
        ArgumentNullException.ThrowIfNull(driver);
        using var document = JsonDocument.Parse(json);
        var root = document.RootElement;
        Properties(root, "version", "scenarios");
        if (root.GetProperty("version").GetInt32() != 1) throw new InvalidOperationException("Unsupported settings corpus version.");
        var scenarios = root.GetProperty("scenarios");
        if (scenarios.GetArrayLength() == 0) throw new InvalidOperationException("The settings corpus requires scenarios.");
        var names = new HashSet<string>(StringComparer.Ordinal);
        int checks = 0;
        foreach (var scenario in scenarios.EnumerateArray())
        {
            Properties(scenario, "name", "steps");
            string name = Text(scenario, "name");
            if (!names.Add(name)) throw new InvalidOperationException($"Duplicate settings scenario '{name}'.");
            var steps = scenario.GetProperty("steps");
            if (steps.GetArrayLength() == 0) throw new InvalidOperationException($"Settings scenario '{name}' requires steps.");
            int index = 0;
            foreach (var step in steps.EnumerateArray())
            {
                index++;
                try
                {
                    string action = Text(step, "action");
                    string id = Text(step, "id");
                    if (index == 1 && (action != "click" || id != "reset-settings"))
                        throw new InvalidOperationException("Each settings scenario must begin with reset-settings.");
                    if (action == "click")
                    {
                        Properties(step, "action", "id");
                        driver.Click(id);
                    }
                    else if (action == "change")
                    {
                        Properties(step, "action", "id", "value");
                        driver.Change(id, Text(step, "value", allowEmpty: true));
                    }
                    else if (action == "expect")
                    {
                        Properties(step, "action", "id", "text", "enabled", "visible", "checked", "checkState", "minimum", "maximum", "value", "indeterminate");
                        int before = checks;
                        void Check<T>(string property, T expected, T actual)
                        {
                            if (!EqualityComparer<T>.Default.Equals(expected, actual))
                                throw new InvalidOperationException($"'{id}' {property}: expected '{expected}', got '{actual}'.");
                            checks++;
                        }
                        if (step.TryGetProperty("text", out _)) Check("text", Text(step, "text", allowEmpty: true), driver.Text(id));
                        if (step.TryGetProperty("enabled", out var enabled)) Check("enabled", enabled.GetBoolean(), driver.Enabled(id));
                        if (step.TryGetProperty("visible", out var visible)) Check("visible", visible.GetBoolean(), driver.Visible(id));
                        if (step.TryGetProperty("checked", out var isChecked)) Check("checked", isChecked.GetBoolean(), driver.Checked(id));
                        if (step.TryGetProperty("checkState", out _)) Check("checkState", Text(step, "checkState"), driver.CheckState(id));
                        if (step.TryGetProperty("minimum", out _) || step.TryGetProperty("maximum", out _) ||
                            step.TryGetProperty("value", out _) || step.TryGetProperty("indeterminate", out _))
                        {
                            var progress = driver.Progress(id);
                            if (step.TryGetProperty("minimum", out var minimum)) Check("minimum", minimum.GetDouble(), progress.Minimum);
                            if (step.TryGetProperty("maximum", out var maximum)) Check("maximum", maximum.GetDouble(), progress.Maximum);
                            if (step.TryGetProperty("value", out var value))
                                Check("value", value.ValueKind == JsonValueKind.Null ? (double?)null : value.GetDouble(), progress.Value);
                            if (step.TryGetProperty("indeterminate", out var indeterminate))
                                Check("indeterminate", indeterminate.GetBoolean(), progress.Indeterminate);
                        }
                        if (checks == before) throw new InvalidOperationException("Expect requires at least one property.");
                    }
                    else throw new InvalidOperationException($"Unknown settings action '{action}'.");
                }
                catch (Exception error)
                {
                    throw new InvalidOperationException($"Settings scenario '{name}', step {index}: {error.Message}", error);
                }
            }
        }
        return checks;
    }

    private static string Text(JsonElement element, string property, bool allowEmpty = false)
    {
        string? text = element.GetProperty(property).GetString();
        return text is not null && (allowEmpty || !string.IsNullOrWhiteSpace(text))
            ? text : throw new InvalidOperationException($"'{property}' must be a string{(allowEmpty ? "" : " with a nonempty value")}.");
    }

    private static void Properties(JsonElement element, params string[] allowed)
    {
        var seen = new HashSet<string>(StringComparer.Ordinal);
        foreach (var property in element.EnumerateObject())
            if (Array.IndexOf(allowed, property.Name) < 0 || !seen.Add(property.Name))
                throw new InvalidOperationException($"Unexpected or duplicate settings property '{property.Name}'.");
    }
}
