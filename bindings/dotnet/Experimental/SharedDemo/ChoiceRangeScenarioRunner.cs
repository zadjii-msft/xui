using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Text.Json;

namespace PortableDemo;

public readonly record struct ChoiceSnapshotItem(ulong Id, string Text, bool Enabled);
public readonly record struct RangeSnapshot(double Minimum, double Maximum, double Value);

public interface IChoiceRangeScenarioDriver
{
    void Click(string id);
    void Change(string id, string value);
    void Select(string id, ulong selected);
    void PreviewRange(string id, double fraction);
    void CommitRange(string id);
    void CancelRange(string id);
    void RangeKey(string id, string key);
    string Text(string id);
    bool Enabled(string id);
    ulong? Selected(string id);
    IReadOnlyList<ChoiceSnapshotItem> Items(string id);
    RangeSnapshot Range(string id);
}

public static class ChoiceRangeScenarioRunner
{
    public static int Run(string json, IChoiceRangeScenarioDriver driver)
    {
        ArgumentNullException.ThrowIfNull(driver);
        using var document = JsonDocument.Parse(json);
        var root = document.RootElement;
        Properties(root, "version", "scenarios");
        if (root.GetProperty("version").GetInt32() != 1) throw new InvalidOperationException("Unsupported choice/range corpus version.");
        var scenarios = root.GetProperty("scenarios");
        if (scenarios.GetArrayLength() == 0) throw new InvalidOperationException("Choice/range scenarios cannot be empty.");
        var names = new HashSet<string>(StringComparer.Ordinal);
        int checks = 0;
        foreach (var scenario in scenarios.EnumerateArray())
        {
            Properties(scenario, "name", "steps");
            string name = Text(scenario, "name");
            if (!names.Add(name)) throw new InvalidOperationException("Duplicate choice/range scenario.");
            var steps = scenario.GetProperty("steps");
            if (steps.GetArrayLength() == 0) throw new InvalidOperationException("Choice/range steps cannot be empty.");
            int index = 0;
            foreach (var step in steps.EnumerateArray())
            {
                index++;
                try
                {
                    string action = Text(step, "action");
                    string id = Text(step, "id");
                    if (index == 1 && (action != "click" || id != "choice-range-reset"))
                        throw new InvalidOperationException("Each choice/range scenario must begin with choice-range-reset.");
                    switch (action)
                    {
                        case "click": Properties(step, "action", "id"); driver.Click(id); break;
                        case "change":
                            Properties(step, "action", "id", "value");
                            driver.Change(id, Text(step, "value", allowEmpty: true));
                            break;
                        case "select":
                            Properties(step, "action", "id", "selected");
                            driver.Select(id, Id(step.GetProperty("selected")));
                            break;
                        case "range-preview":
                            Properties(step, "action", "id", "fraction");
                            double fraction = step.GetProperty("fraction").GetDouble();
                            if (!double.IsFinite(fraction)) throw new InvalidOperationException("Range fraction must be finite.");
                            driver.PreviewRange(id, fraction);
                            break;
                        case "range-commit": Properties(step, "action", "id"); driver.CommitRange(id); break;
                        case "range-cancel": Properties(step, "action", "id"); driver.CancelRange(id); break;
                        case "range-key":
                            Properties(step, "action", "id", "key");
                            string key = Text(step, "key");
                            if (key is not ("Decrease" or "Increase" or "PageDecrease" or "PageIncrease" or "Minimum" or "Maximum"))
                                throw new InvalidOperationException("Unknown range key action.");
                            driver.RangeKey(id, key);
                            break;
                        case "expect":
                            Properties(step, "action", "id", "text", "enabled", "selected", "items", "itemEnabled", "minimum", "maximum", "value");
                            int before = checks;
                            void Check<T>(string property, T expected, T actual)
                            {
                                if (!EqualityComparer<T>.Default.Equals(expected, actual))
                                    throw new InvalidOperationException($"'{id}' {property}: expected '{expected}', got '{actual}'.");
                                checks++;
                            }
                            if (step.TryGetProperty("text", out _)) Check("text", Text(step, "text", true), driver.Text(id));
                            if (step.TryGetProperty("enabled", out var enabled)) Check("enabled", enabled.GetBoolean(), driver.Enabled(id));
                            if (step.TryGetProperty("selected", out var selected))
                                Check("selected", selected.ValueKind == JsonValueKind.Null ? (ulong?)null : Id(selected), driver.Selected(id));
                            if (step.TryGetProperty("items", out var items))
                            {
                                if (!items.EnumerateArray().Select(Id).SequenceEqual(driver.Items(id).Select(item => item.Id)))
                                    throw new InvalidOperationException($"'{id}' native item identity/order differs.");
                                checks++;
                            }
                            if (step.TryGetProperty("itemEnabled", out var itemEnabled))
                            {
                                if (!itemEnabled.EnumerateArray().Select(value => value.GetBoolean()).SequenceEqual(driver.Items(id).Select(item => item.Enabled)))
                                    throw new InvalidOperationException($"'{id}' native item availability differs.");
                                checks++;
                            }
                            if (step.TryGetProperty("minimum", out _) || step.TryGetProperty("maximum", out _) || step.TryGetProperty("value", out _))
                            {
                                var range = driver.Range(id);
                                if (step.TryGetProperty("minimum", out var min)) Check("minimum", min.GetDouble(), range.Minimum);
                                if (step.TryGetProperty("maximum", out var max)) Check("maximum", max.GetDouble(), range.Maximum);
                                if (step.TryGetProperty("value", out var value)) Check("value", value.GetDouble(), range.Value);
                            }
                            if (checks == before) throw new InvalidOperationException("Expect requires a property.");
                            break;
                        default: throw new InvalidOperationException("Unknown choice/range action.");
                    }
                }
                catch (Exception error) { throw new InvalidOperationException($"Choice/range scenario '{name}', step {index}: {error.Message}", error); }
            }
        }
        return checks;
    }

    private static ulong Id(JsonElement value)
    {
        string? text = value.GetString();
        if (!ulong.TryParse(text, NumberStyles.None, CultureInfo.InvariantCulture, out ulong id) ||
            id == 0 || id > (ulong)long.MaxValue - 100 || id.ToString(CultureInfo.InvariantCulture) != text)
            throw new InvalidOperationException("A choice ID must be a canonical supported decimal string.");
        return id;
    }
    private static string Text(JsonElement value, string property, bool allowEmpty = false)
    {
        string? text = value.GetProperty(property).GetString();
        return text is not null && (allowEmpty || !string.IsNullOrWhiteSpace(text))
            ? text : throw new InvalidOperationException($"Invalid string property '{property}'.");
    }
    private static void Properties(JsonElement value, params string[] allowed)
    {
        var seen = new HashSet<string>(StringComparer.Ordinal);
        foreach (var property in value.EnumerateObject())
            if (Array.IndexOf(allowed, property.Name) < 0 || !seen.Add(property.Name))
                throw new InvalidOperationException($"Unexpected or duplicate choice/range property '{property.Name}'.");
    }
}
