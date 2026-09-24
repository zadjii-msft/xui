using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Text.Json;

namespace PortableNavigation;

public interface IPageScenarioDriver
{
    void Click(string id);
    void Change(string id, string value);
    void SelectPage(string id, ulong page);
    void ActivatePage(string id, ulong page);
    void RequestClose(string id, ulong page);
    string Text(string id);
    bool Enabled(string id);
    bool Visible(string id);
    bool Exists(string id);
    ulong? Selected(string id);
    IReadOnlyList<ulong> Pages(string id);
    void RememberEditor(string id);
    bool SameEditor(string id);
}

public static class PageScenarioRunner
{
    public static int Run(string json, IPageScenarioDriver driver)
    {
        ArgumentNullException.ThrowIfNull(driver);
        using var document = JsonDocument.Parse(json);
        var root = document.RootElement;
        Properties(root, "version", "scenarios");
        if (root.GetProperty("version").GetInt32() != 1) throw new InvalidOperationException("Unsupported page corpus version.");
        var scenarios = root.GetProperty("scenarios");
        if (scenarios.GetArrayLength() == 0) throw new InvalidOperationException("Page scenarios cannot be empty.");
        int checks = 0;
        var names = new HashSet<string>(StringComparer.Ordinal);
        foreach (var scenario in scenarios.EnumerateArray())
        {
            Properties(scenario, "name", "steps");
            string name = Text(scenario, "name");
            if (!names.Add(name)) throw new InvalidOperationException("Duplicate page scenario.");
            var steps = scenario.GetProperty("steps");
            if (steps.GetArrayLength() == 0) throw new InvalidOperationException("Page steps cannot be empty.");
            int index = 0;
            foreach (var step in steps.EnumerateArray())
            {
                index++;
                try
                {
                    string action = Text(step, "action");
                    string id = Text(step, "id");
                    if (index == 1 && (action != "click" || id != "pages-reset"))
                        throw new InvalidOperationException("Each page scenario must begin with pages-reset.");
                    switch (action)
                    {
                        case "click": Properties(step, "action", "id"); driver.Click(id); break;
                        case "change": Properties(step, "action", "id", "value"); driver.Change(id, Text(step, "value", true)); break;
                        case "select-page": Properties(step, "action", "id", "page"); driver.SelectPage(id, Id(step.GetProperty("page"))); break;
                        case "activate-page": Properties(step, "action", "id", "page"); driver.ActivatePage(id, Id(step.GetProperty("page"))); break;
                        case "request-close": Properties(step, "action", "id", "page"); driver.RequestClose(id, Id(step.GetProperty("page"))); break;
                        case "remember-editor": Properties(step, "action", "id"); driver.RememberEditor(id); break;
                        case "expect-same-editor":
                            Properties(step, "action", "id");
                            if (!driver.SameEditor(id)) throw new InvalidOperationException($"'{id}' editor was replaced.");
                            checks++;
                            break;
                        case "expect-absent":
                            Properties(step, "action", "id");
                            if (driver.Exists(id)) throw new InvalidOperationException($"'{id}' native control still exists.");
                            checks++;
                            break;
                        case "expect":
                            Properties(step, "action", "id", "text", "selected", "pages", "enabled", "visible");
                            int before = checks;
                            void Check<T>(string property, T expected, T actual)
                            {
                                if (!EqualityComparer<T>.Default.Equals(expected, actual))
                                    throw new InvalidOperationException($"'{id}' {property}: expected '{expected}', got '{actual}'.");
                                checks++;
                            }
                            if (step.TryGetProperty("text", out _)) Check("text", Text(step, "text", true), driver.Text(id));
                            if (step.TryGetProperty("enabled", out var enabled)) Check("enabled", enabled.GetBoolean(), driver.Enabled(id));
                            if (step.TryGetProperty("visible", out var visible)) Check("visible", visible.GetBoolean(), driver.Visible(id));
                            if (step.TryGetProperty("selected", out var selected))
                                Check("selected", selected.ValueKind == JsonValueKind.Null ? (ulong?)null : Id(selected), driver.Selected(id));
                            if (step.TryGetProperty("pages", out var pages))
                            {
                                if (!pages.EnumerateArray().Select(Id).SequenceEqual(driver.Pages(id)))
                                    throw new InvalidOperationException($"'{id}' native page order differs.");
                                checks++;
                            }
                            if (checks == before) throw new InvalidOperationException("Expect requires a property.");
                            break;
                        default: throw new InvalidOperationException("Unknown page action.");
                    }
                }
                catch (Exception error) { throw new InvalidOperationException($"Page scenario '{name}', step {index}: {error.Message}", error); }
            }
        }
        return checks;
    }

    private static ulong Id(JsonElement value)
    {
        string? text = value.GetString();
        if (!ulong.TryParse(text, NumberStyles.None, CultureInfo.InvariantCulture, out ulong id) ||
            id == 0 || id > (ulong)long.MaxValue - 100 || id.ToString(CultureInfo.InvariantCulture) != text)
            throw new InvalidOperationException("A page ID must be a canonical supported decimal string.");
        return id;
    }
    private static string Text(JsonElement value, string property, bool empty = false)
    {
        string? text = value.GetProperty(property).GetString();
        return text is not null && (empty || !string.IsNullOrWhiteSpace(text))
            ? text : throw new InvalidOperationException($"Invalid string property '{property}'.");
    }
    private static void Properties(JsonElement value, params string[] allowed)
    {
        var seen = new HashSet<string>(StringComparer.Ordinal);
        foreach (var property in value.EnumerateObject())
            if (!allowed.Contains(property.Name, StringComparer.Ordinal) || !seen.Add(property.Name))
                throw new InvalidOperationException($"Unexpected or duplicate page property '{property.Name}'.");
    }
}
