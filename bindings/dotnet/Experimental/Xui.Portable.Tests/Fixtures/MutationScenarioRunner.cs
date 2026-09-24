using System;
using System.Collections.Generic;
using System.Linq;
using System.Text.Json;

namespace PortableMutation;

public interface IMutationScenarioDriver
{
    void Click(string id);
    void Change(string id, string value);
    string Text(string id);
    IReadOnlyList<string> Order();
    bool Exists(string id);
}

public static class MutationScenarioRunner
{
    public static int Run(string json, IMutationScenarioDriver driver)
    {
        ArgumentNullException.ThrowIfNull(driver);
        using var document = JsonDocument.Parse(json);
        var root = document.RootElement;
        Properties(root, "version", "scenarios");
        if (root.GetProperty("version").GetInt32() != 1) throw new InvalidOperationException("Unsupported mutation corpus version.");
        var scenarios = root.GetProperty("scenarios");
        if (scenarios.GetArrayLength() == 0) throw new InvalidOperationException("The mutation corpus requires scenarios.");
        var names = new HashSet<string>(StringComparer.Ordinal);
        int checks = 0;
        foreach (var scenario in scenarios.EnumerateArray())
        {
            Properties(scenario, "name", "steps");
            string name = Text(scenario, "name");
            if (!names.Add(name)) throw new InvalidOperationException($"Duplicate mutation scenario '{name}'.");
            var steps = scenario.GetProperty("steps");
            if (steps.GetArrayLength() == 0) throw new InvalidOperationException($"Mutation scenario '{name}' requires steps.");
            int index = 0;
            foreach (var step in steps.EnumerateArray())
            {
                index++;
                try
                {
                    string action = Text(step, "action");
                    if (index == 1 && (action != "click" || Text(step, "id") != "reset-rows"))
                        throw new InvalidOperationException("Each mutation scenario must begin with reset-rows.");
                    switch (action)
                    {
                        case "click":
                            Properties(step, "action", "id");
                            driver.Click(Text(step, "id"));
                            break;
                        case "change":
                            Properties(step, "action", "id", "value");
                            driver.Change(Text(step, "id"), Text(step, "value", allowEmpty: true));
                            break;
                        case "expect":
                            Properties(step, "action", "id", "text");
                            string id = Text(step, "id");
                            string expected = Text(step, "text", allowEmpty: true);
                            string actual = driver.Text(id);
                            if (!string.Equals(expected, actual, StringComparison.Ordinal))
                                throw new InvalidOperationException($"'{id}' text: expected '{expected}', got '{actual}'.");
                            checks++;
                            break;
                        case "expect-order":
                            Properties(step, "action", "keys");
                            var keys = step.GetProperty("keys").EnumerateArray().Select(value =>
                                value.GetString() ?? throw new InvalidOperationException("Order keys must be strings.")).ToArray();
                            var order = driver.Order();
                            if (!keys.SequenceEqual(order, StringComparer.Ordinal))
                                throw new InvalidOperationException($"Row order: expected [{string.Join(", ", keys)}], got [{string.Join(", ", order)}].");
                            checks++;
                            break;
                        case "expect-absent":
                            Properties(step, "action", "id");
                            string absent = Text(step, "id");
                            if (driver.Exists(absent)) throw new InvalidOperationException($"'{absent}' still exists.");
                            checks++;
                            break;
                        default:
                            throw new InvalidOperationException($"Unknown mutation action '{action}'.");
                    }
                }
                catch (Exception error)
                {
                    throw new InvalidOperationException($"Mutation scenario '{name}', step {index}: {error.Message}", error);
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
                throw new InvalidOperationException($"Unexpected or duplicate mutation property '{property.Name}'.");
    }
}
