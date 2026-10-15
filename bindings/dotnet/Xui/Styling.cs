using System.Collections.ObjectModel;

namespace Xui;

/// <summary>Opaque 0xRRGGBB colors for the light and dark themes.</summary>
public readonly record struct ThemeColor(uint Light, uint Dark)
{
    public ThemeColor(uint color) : this(color, color) { }
    internal void Validate()
    {
        if (Light > 0xffffff || Dark > 0xffffff)
            throw new ArgumentOutOfRangeException(nameof(ThemeColor), "Colors must be 0xRRGGBB values.");
    }
}

public readonly record struct Insets(float Left, float Top, float Right, float Bottom)
{
    public Insets(float uniform) : this(uniform, uniform, uniform, uniform) { }
}

internal static class StyleValidation
{
    internal static void Dimension(float value)
    {
        if (!float.IsFinite(value) || value < 0 || value > 32768)
            throw new ArgumentOutOfRangeException(nameof(value), "Style dimensions must be finite and between 0 and 32768 DIPs.");
    }
    internal static void Edges(Insets? edges)
    {
        if (edges is { } value)
        {
            Dimension(value.Left); Dimension(value.Top); Dimension(value.Right); Dimension(value.Bottom);
        }
    }
}

/// <summary>A bounded immutable color scope. String values name aliases in this scope or its parent.</summary>
public sealed class ResourceScope
{
    private readonly IReadOnlyDictionary<string, ThemeColor> colors;
    private readonly ResourceScope? parent;
    private readonly int depth;

    public ResourceScope(IEnumerable<KeyValuePair<string, object>> entries, ResourceScope? parent = null)
    {
        ArgumentNullException.ThrowIfNull(entries);
        this.parent = parent;
        depth = (parent?.depth ?? 0) + 1;
        if (depth > 16) throw new ArgumentException("Resource scopes exceed 16 layers.", nameof(parent));
        var source = new Dictionary<string, object>(StringComparer.Ordinal);
        foreach (var entry in entries)
        {
            Name(entry.Key);
            if (source.Count == 256) throw new ArgumentException("A resource scope supports at most 256 entries.", nameof(entries));
            if (entry.Value is ThemeColor color) color.Validate();
            else if (entry.Value is string alias) Name(alias);
            else throw new ArgumentException("A color resource must be a ThemeColor or a string alias.", nameof(entries));
            if (!source.TryAdd(entry.Key, entry.Value)) throw new ArgumentException("Duplicate resource name.", nameof(entries));
        }
        var resolved = new Dictionary<string, ThemeColor>(StringComparer.Ordinal);
        var visiting = new HashSet<string>(StringComparer.Ordinal);
        ThemeColor ResolveEntry(string name)
        {
            if (resolved.TryGetValue(name, out var found)) return found;
            if (!source.TryGetValue(name, out var entry))
                return parent?.Resolve(name) ?? throw new KeyNotFoundException($"Color resource '{name}' was not found.");
            if (!visiting.Add(name)) throw new ArgumentException("Color resource aliases contain a cycle.", nameof(entries));
            var color = entry is ThemeColor direct ? direct : ResolveEntry((string)entry);
            visiting.Remove(name); resolved.Add(name, color); return color;
        }
        foreach (var name in source.Keys) ResolveEntry(name);
        colors = new ReadOnlyDictionary<string, ThemeColor>(resolved);
    }
    private static void Name(string name)
    {
        ArgumentNullException.ThrowIfNull(name);
        if (name.Length == 0 || name.Length > 1024 || name.Contains('\0'))
            throw new ArgumentException("Resource names must contain 1 to 1024 characters without NUL.", nameof(name));
    }
    public ThemeColor Resolve(string name)
    {
        Name(name);
        return colors.TryGetValue(name, out var color) ? color :
            parent?.Resolve(name) ?? throw new KeyNotFoundException($"Color resource '{name}' was not found.");
    }
}
