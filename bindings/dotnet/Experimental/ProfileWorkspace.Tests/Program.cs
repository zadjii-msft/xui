using System.Text.Json;
using PortableDemo;

internal static partial class Program
{
    private static int assertions;
    private static void Main()
    {
        ModelChecks();
        PageChecks();
        StorageChecks();
        ScenarioChecks();
        SessionChecks();
        Console.WriteLine($"Profile workspace: {assertions} assertions passed.");
    }

    private static void Assert(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
        assertions++;
    }

    private static T Throws<T>(Action action) where T : Exception
    {
        try { action(); }
        catch (T error) { assertions++; return error; }
        throw new InvalidOperationException($"Expected {typeof(T).Name}.");
    }

    private static void ModelChecks()
    {
        var empty = new ProfileSnapshot();
        Assert(!empty.CanSave && empty.DisplayName == "" && empty.Role == "" && empty.Location == "" && empty.Focus == "",
            "New workspace contains no pretend saved profile.");
        Assert(empty.Validation == "Enter a display name before previewing or saving.", "Empty name has explicit validation.");
        var profile = empty.WithDisplayName("  Zo\u00eb \u674e  ").WithRole("Developer").WithLocation("London").WithFocus("Accessible tools");
        Assert(profile.CanSave && profile.DisplayName == "  Zo\u00eb \u674e  " && empty.DisplayName == "",
            "Editing preserves exact native text without changing earlier snapshots.");
        Assert(profile.Role == "Developer" && profile.Location == "London" && profile.Focus == "Accessible tools", "Independent profile fields.");
        Assert(profile.WithDisplayName(" ").CanSave == false && profile.WithRole("").CanSave, "Only display name is required.");
        foreach (string invalid in new[] { "a\0b", "a\nb", "a\rb" })
        {
            Throws<ArgumentException>(() => profile.WithDisplayName(invalid));
            Throws<ArgumentException>(() => profile.WithRole(invalid));
            Throws<ArgumentException>(() => profile.WithLocation(invalid));
            Throws<ArgumentException>(() => profile.WithFocus(invalid));
        }
        Throws<ArgumentNullException>(() => profile.WithDisplayName(null!));
        Assert(!JsonSerializer.IsReflectionEnabledByDefault, "JSON metadata is generated at compile time.");
        string json = ProfileWorkspaceCodec.Serialize(profile);
        Assert(ProfileWorkspaceCodec.Restore(json) == profile, "Profile JSON roundtrip preserves all values.");
        Assert(!json.Contains("CanSave", StringComparison.Ordinal) && !json.Contains("Validation", StringComparison.Ordinal), "Storage contains data only, not presentation.");
        const string literal = """{"Version":1,"Profile":{"DisplayName":"Ada","Role":"Engineer","Location":"London","Focus":"Native input"}}""";
        Assert(ProfileWorkspaceCodec.Restore(literal) == new ProfileSnapshot("Ada", "Engineer", "London", "Native input"), "Literal document contract.");
        Assert(ProfileWorkspaceCodec.Serialize(new("Ada", "Engineer", "London", "Native input")) == literal, "Literal persisted output shape.");
        Throws<JsonException>(() => ProfileWorkspaceCodec.Serialize(empty));
        foreach (string invalid in new[]
        {
            "null", "{}", "{", """{"Version":2,"Profile":{"DisplayName":"Ada","Role":"","Location":"","Focus":""}}""",
            """{"Version":1,"Profile":null}""", """{"Version":1,"Profile":{"DisplayName":"Ada"}}""",
            """{"Version":1,"Profile":{"DisplayName":"","Role":"","Location":"","Focus":""}}""",
            """{"Version":1,"Profile":{"DisplayName":"Ada","Role":"","Location":"","Focus":""},"Unexpected":true}"""
        })
            Throws<JsonException>(() => ProfileWorkspaceCodec.Restore(invalid));
    }
}
