using System;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace PortableDemo;

public sealed record ProfileSnapshot
{
    public string DisplayName { get; }
    public string Role { get; }
    public string Location { get; }
    public string Focus { get; }

    public ProfileSnapshot() : this("", "", "", "") { }

    [JsonConstructor]
    public ProfileSnapshot(string displayName, string role, string location, string focus)
    {
        DisplayName = CheckText(displayName);
        Role = CheckText(role);
        Location = CheckText(location);
        Focus = CheckText(focus);
    }

    [JsonIgnore] public bool CanSave => !string.IsNullOrWhiteSpace(DisplayName);
    [JsonIgnore] public string Validation => CanSave
        ? "Preview and edits do not save. Choose Save draft to store this profile."
        : "Enter a display name before previewing or saving.";

    public ProfileSnapshot WithDisplayName(string value) => new(value, Role, Location, Focus);
    public ProfileSnapshot WithRole(string value) => new(DisplayName, value, Location, Focus);
    public ProfileSnapshot WithLocation(string value) => new(DisplayName, Role, value, Focus);
    public ProfileSnapshot WithFocus(string value) => new(DisplayName, Role, Location, value);

    private static string CheckText(string value)
    {
        ArgumentNullException.ThrowIfNull(value);
        if (value.IndexOfAny(['\0', '\r', '\n']) >= 0)
            throw new ArgumentException("Profile fields must be single-line text without NUL.", nameof(value));
        return value;
    }
}

public sealed record ProfileDocument
{
    public int Version { get; }
    public ProfileSnapshot Profile { get; }

    [JsonConstructor]
    public ProfileDocument(int version, ProfileSnapshot profile)
    {
        if (version != 1) throw new JsonException("Unsupported profile draft version.");
        Version = version;
        Profile = profile ?? throw new JsonException("Profile draft cannot be null.");
        if (!Profile.CanSave) throw new JsonException("Stored profile draft requires a display name.");
    }
}

public static class ProfileWorkspaceCodec
{
    public static string Serialize(ProfileSnapshot profile) =>
        JsonSerializer.Serialize(new ProfileDocument(1, profile), ProfileJsonContext.Default.ProfileDocument);

    public static ProfileSnapshot Restore(string json) =>
        (JsonSerializer.Deserialize(json, ProfileJsonContext.Default.ProfileDocument) ??
            throw new JsonException("Profile document cannot be null.")).Profile;
}

[JsonSourceGenerationOptions(RespectRequiredConstructorParameters = true, UnmappedMemberHandling = JsonUnmappedMemberHandling.Disallow)]
[JsonSerializable(typeof(ProfileDocument))]
internal partial class ProfileJsonContext : JsonSerializerContext;
