using System;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace PortableDemo;

public sealed record ProfileWorkspaceSession
{
    public int Version { get; }
    public ProfileSnapshot Draft { get; }
    public ProfileSnapshot? SavedProfile { get; }
    public ProfilePage Page { get; }
    public bool Interrupted { get; }

    [JsonConstructor]
    public ProfileWorkspaceSession(int version, ProfileSnapshot draft, ProfileSnapshot? savedProfile,
        ProfilePage page, bool interrupted)
    {
        if (version != 1) throw new JsonException("Unsupported profile session version.");
        if (draft is null) throw new JsonException("Profile session draft cannot be null.");
        if (!Enum.IsDefined(page)) throw new JsonException("Unknown profile session page.");
        if (page == ProfilePage.Preview && !draft.CanSave)
            throw new JsonException("A preview session requires a valid display name.");
        if (savedProfile is not null && !savedProfile.CanSave)
            throw new JsonException("The last confirmed saved profile requires a valid display name.");
        Version = version;
        Draft = draft;
        SavedProfile = savedProfile;
        Page = page;
        Interrupted = interrupted;
    }
}

public static class ProfileWorkspaceSessionCodec
{
    public static string Serialize(ProfileWorkspaceSession session) =>
        JsonSerializer.Serialize(session ?? throw new ArgumentNullException(nameof(session)),
            ProfileSessionJsonContext.Default.ProfileWorkspaceSession);

    public static ProfileWorkspaceSession Restore(string json) =>
        JsonSerializer.Deserialize(json, ProfileSessionJsonContext.Default.ProfileWorkspaceSession) ??
        throw new JsonException("Profile session cannot be null.");
}

[JsonSourceGenerationOptions(RespectRequiredConstructorParameters = true,
    UnmappedMemberHandling = JsonUnmappedMemberHandling.Disallow, AllowDuplicateProperties = false)]
[JsonSerializable(typeof(ProfileWorkspaceSession))]
internal partial class ProfileSessionJsonContext : JsonSerializerContext;
