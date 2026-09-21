using System.Globalization;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace Xui.FileExplorer.Models;

public sealed class ExplorerCustomization
{
    public int Version { get; set; } = 1;
    public Dictionary<string, string[]> Keybindings { get; set; } = new(StringComparer.Ordinal);
    public List<string> ToolbarCommands { get; set; } = ["back", "forward", "up-to-parent-folder", "refresh-folder", "commands"];
    public List<string> SidebarCommands { get; set; } = [];
    public List<string> SidebarSections { get; set; } = ["recents", "bookmarks", "storage", "places", "tree"];
    public List<string> PinnedContextActions { get; set; } = [];
    public List<string> HiddenContextActions { get; set; } = [];
    public bool ToolbarLabels { get; set; }
    public bool ShowToolbar { get; set; } = true;
    public bool ShowSidebar { get; set; } = true;
    public bool ShowStatus { get; set; } = true;
    public bool HomeRecentFiles { get; set; } = true;
    public bool HomePinnedLocations { get; set; } = true;
    public bool HomeDriveCapacity { get; set; } = true;
    public float RowHeight { get; set; } = 32;
    public string FontFamily { get; set; } = "Segoe UI";
    public float FontSize { get; set; } = 14;
    public string DateFormat { get; set; } = "yyyy-MM-dd HH:mm";
    public bool ThumbnailFill { get; set; }
    public bool SmoothScrolling { get; set; } = true;
    public bool Animations { get; set; } = true;
    public bool SingleClick { get; set; }
    public string Theme { get; set; } = "system";

    public void Validate()
    {
        if (Version != 1) throw new InvalidDataException("Unsupported customization version.");
        if (Theme is not ("system" or "light" or "dark"))
            throw new InvalidDataException("Theme must be system, light, or dark.");
        if (!float.IsFinite(RowHeight) || RowHeight is < 20 or > 80 ||
            !float.IsFinite(FontSize) || FontSize is < 9 or > 32)
            throw new InvalidDataException("Row height must be 20-80 and font size must be 9-32.");
        Text(FontFamily, 128);
        Text(DateFormat, 80);
        try { _ = DateTime.Now.ToString(DateFormat, CultureInfo.CurrentCulture); }
        catch (FormatException error) { throw new InvalidDataException("Invalid date format.", error); }
        foreach (var list in new[] { ToolbarCommands, SidebarCommands, SidebarSections, PinnedContextActions, HiddenContextActions })
        {
            if (list is null || list.Count > 256 || list.Distinct(StringComparer.Ordinal).Count() != list.Count)
                throw new InvalidDataException("Customization lists must contain at most 256 unique identities.");
            foreach (string value in list) Text(value, 256);
        }
        if (SidebarSections.Any(id => id is not ("recents" or "bookmarks" or "storage" or "places" or "tree")))
            throw new InvalidDataException("Unknown navigation section.");
        if (Keybindings is null || Keybindings.Count > 512)
            throw new InvalidDataException("Too many keybindings.");
        foreach (var (id, bindings) in Keybindings)
        {
            Text(id, 256);
            if (bindings is null || bindings.Length > 8) throw new InvalidDataException("A command supports at most eight aliases.");
            foreach (string binding in bindings) _ = KeyGesture.ParseSequence(binding);
        }
    }

    private static void Text(string? value, int maximum)
    {
        if (string.IsNullOrWhiteSpace(value) || value.Length > maximum || value.Any(char.IsControl))
            throw new InvalidDataException("Customization contains invalid text.");
    }

    public ExplorerCustomization Clone() => Parse(ToJson());
    public string ToJson()
    {
        Validate();
        return JsonSerializer.Serialize(this, CustomizationJsonContext.Default.ExplorerCustomization);
    }
    public static ExplorerCustomization Parse(string json)
    {
        if (json.Length > 1024 * 1024) throw new InvalidDataException("Customization import is too large.");
        try
        {
            var result = JsonSerializer.Deserialize(json, CustomizationJsonContext.Default.ExplorerCustomization)
                ?? throw new InvalidDataException("Customization cannot be null.");
            result.Validate();
            return result;
        }
        catch (JsonException error) { throw new InvalidDataException($"Invalid customization JSON: {error.Message}", error); }
    }
}

[JsonSourceGenerationOptions(WriteIndented = true, UnmappedMemberHandling = JsonUnmappedMemberHandling.Disallow)]
[JsonSerializable(typeof(ExplorerCustomization))]
internal partial class CustomizationJsonContext : JsonSerializerContext;
