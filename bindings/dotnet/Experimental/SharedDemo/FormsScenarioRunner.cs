using System;
using System.Collections.Generic;
using System.Text.Json;

namespace PortableDemo;

public interface IFormsScenarioDriver
{
    void Click(string id);
    void Change(string id, string value);
    void SeedPassword(string id, int codeUnits);
    string Text(string id);
    string Purpose(string id);
    bool ReadOnly(string id);
    int PasswordLength(string id);
}

public static class FormsScenarioRunner
{
    public static int Run(string json, IFormsScenarioDriver driver)
    {
        ArgumentNullException.ThrowIfNull(driver);
        using var document = JsonDocument.Parse(json);
        var root = document.RootElement;
        Properties(root, "version", "scenarios");
        if (root.GetProperty("version").GetInt32() != 1) throw new InvalidOperationException("Unsupported forms corpus version.");
        var scenarios = root.GetProperty("scenarios");
        if (scenarios.GetArrayLength() == 0) throw new InvalidOperationException("Forms scenarios cannot be empty.");
        var names = new HashSet<string>(StringComparer.Ordinal);
        int checks = 0;
        foreach (var scenario in scenarios.EnumerateArray())
        {
            Properties(scenario, "name", "steps");
            string name = Text(scenario, "name");
            if (!names.Add(name)) throw new InvalidOperationException("Duplicate forms scenario.");
            var steps = scenario.GetProperty("steps");
            if (steps.GetArrayLength() == 0) throw new InvalidOperationException("Forms steps cannot be empty.");
            int index = 0;
            foreach (var step in steps.EnumerateArray())
            {
                index++;
                try
                {
                    string action = Text(step, "action");
                    string id = Text(step, "id");
                    if (index == 1 && (action != "click" || id != "forms-reset"))
                        throw new InvalidOperationException("Each forms scenario must begin with forms-reset.");
                    switch (action)
                    {
                        case "click":
                            Properties(step, "action", "id");
                            driver.Click(id);
                            break;
                        case "change":
                            Properties(step, "action", "id", "value");
                            driver.Change(id, Text(step, "value", allowEmpty: true));
                            break;
                        case "seed-password":
                            Properties(step, "action", "id", "codeUnits");
                            int length = step.GetProperty("codeUnits").GetInt32();
                            if (length is < 0 or > 32) throw new InvalidOperationException("The synthetic password seed must fit the showcase limit.");
                            driver.SeedPassword(id, length);
                            break;
                        case "expect":
                            Properties(step, "action", "id", "text", "purpose", "readOnly", "passwordLength");
                            int before = checks;
                            void Check<T>(string property, T expected, T actual)
                            {
                                if (!EqualityComparer<T>.Default.Equals(expected, actual))
                                    throw new InvalidOperationException($"'{id}' {property} did not match its literal expectation.");
                                checks++;
                            }
                            if (step.TryGetProperty("text", out _)) Check("text", Text(step, "text", true), driver.Text(id));
                            if (step.TryGetProperty("purpose", out _)) Check("purpose", Text(step, "purpose"), driver.Purpose(id));
                            if (step.TryGetProperty("readOnly", out var readOnly)) Check("readOnly", readOnly.GetBoolean(), driver.ReadOnly(id));
                            if (step.TryGetProperty("passwordLength", out var passwordLength)) Check("passwordLength", passwordLength.GetInt32(), driver.PasswordLength(id));
                            if (checks == before) throw new InvalidOperationException("Expect requires a property.");
                            break;
                        default: throw new InvalidOperationException("Unknown forms action.");
                    }
                }
                catch (Exception error) { throw new InvalidOperationException($"Forms scenario '{name}', step {index}: {error.Message}", error); }
            }
        }
        return checks;
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
                throw new InvalidOperationException($"Unexpected or duplicate forms property '{property.Name}'.");
    }
}
