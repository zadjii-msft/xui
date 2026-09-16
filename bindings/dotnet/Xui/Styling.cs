using System.Collections.ObjectModel;
using System.Runtime.CompilerServices;

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

public sealed record ButtonStyleValues
{
    public ThemeColor? Background { get; init; }
    public ThemeColor? Foreground { get; init; }
    public ThemeColor? BorderBrush { get; init; }
    public Insets? BorderThickness { get; init; }
    public Insets? Padding { get; init; }
    public float? CornerRadius { get; init; }

    internal static readonly ButtonStyleValues Empty = new();
    internal static void Dimension(float value)
    {
        if (!float.IsFinite(value) || value < 0 || value > 32768)
            throw new ArgumentOutOfRangeException(nameof(value), "Style dimensions must be finite and between 0 and 32768 DIPs.");
    }
    internal void Validate()
    {
        Background?.Validate(); Foreground?.Validate(); BorderBrush?.Validate();
        static void Edges(Insets? edges)
        {
            if (edges is { } v) { Dimension(v.Left); Dimension(v.Top); Dimension(v.Right); Dimension(v.Bottom); }
        }
        Edges(BorderThickness); Edges(Padding);
        if (CornerRadius is { } radius) Dimension(radius);
    }
    internal unsafe Native.ButtonStyleValues ToNative()
    {
        Validate();
        var result = new Native.ButtonStyleValues
        {
            Size = (uint)sizeof(Native.ButtonStyleValues), Version = 0x10000
        };
        if (Background is { } background)
        { result.Mask |= 1; result.Background = new() { Light = background.Light, Dark = background.Dark }; }
        if (Foreground is { } foreground)
        { result.Mask |= 2; result.Foreground = new() { Light = foreground.Light, Dark = foreground.Dark }; }
        if (BorderBrush is { } border)
        { result.Mask |= 4; result.BorderBrush = new() { Light = border.Light, Dark = border.Dark }; }
        if (BorderThickness is { } thickness)
        { result.Mask |= 8; result.BorderThickness = new() { Left = thickness.Left, Top = thickness.Top, Right = thickness.Right, Bottom = thickness.Bottom }; }
        if (Padding is { } padding)
        { result.Mask |= 16; result.Padding = new() { Left = padding.Left, Top = padding.Top, Right = padding.Right, Bottom = padding.Bottom }; }
        if (CornerRadius is { } radius) { result.Mask |= 32; result.CornerRadius = radius; }
        return result;
    }
    internal static ButtonStyleValues FromNative(Native.ButtonStyleValues v) => new()
    {
        Background = (v.Mask & 1) != 0 ? new ThemeColor(v.Background.Light, v.Background.Dark) : null,
        Foreground = (v.Mask & 2) != 0 ? new ThemeColor(v.Foreground.Light, v.Foreground.Dark) : null,
        BorderBrush = (v.Mask & 4) != 0 ? new ThemeColor(v.BorderBrush.Light, v.BorderBrush.Dark) : null,
        BorderThickness = (v.Mask & 8) != 0 ? new Insets(v.BorderThickness.Left, v.BorderThickness.Top, v.BorderThickness.Right, v.BorderThickness.Bottom) : null,
        Padding = (v.Mask & 16) != 0 ? new Insets(v.Padding.Left, v.Padding.Top, v.Padding.Right, v.Padding.Bottom) : null,
        CornerRadius = (v.Mask & 32) != 0 ? v.CornerRadius : null
    };
}

public enum ButtonStyleState { Focused, Checked, Hovered, Pressed, Disabled }
public sealed record ButtonStyleRule(ButtonStyleState State, ButtonStyleValues Values);

/// <summary>An immutable definition. Native handles exist only during application to a window.</summary>
public sealed class ButtonStyle
{
    private sealed class NativeIdentity { internal ulong Value; }
    private readonly ConditionalWeakTable<Window, NativeIdentity> identities = new();
    private readonly int depth;
    public ButtonStyleValues Values { get; }
    public IReadOnlyList<ButtonStyleRule> Rules { get; }
    public ButtonStyle? BasedOn { get; }

    public ButtonStyle(ButtonStyleValues values, IReadOnlyList<ButtonStyleRule>? rules = null, ButtonStyle? basedOn = null)
    {
        ArgumentNullException.ThrowIfNull(values);
        values.Validate();
        depth = (basedOn?.depth ?? 0) + 1;
        if (depth > 16) throw new ArgumentException("Button style inheritance exceeds 16 layers.", nameof(basedOn));
        if (rules?.Count > 256) throw new ArgumentException("A Button style supports at most 256 rules.", nameof(rules));
        var copy = rules?.ToArray() ?? [];
        foreach (var rule in copy)
        {
            ArgumentNullException.ThrowIfNull(rule);
            ArgumentNullException.ThrowIfNull(rule.Values);
            if (!Enum.IsDefined(rule.State)) throw new ArgumentOutOfRangeException(nameof(rules), "Invalid Button style state.");
            rule.Values.Validate();
        }
        Values = values; Rules = Array.AsReadOnly(copy); BasedOn = basedOn;
    }

    internal unsafe void WithNativeHandle(Window window, Action<ulong> apply)
    {
        window.Guard();
        var identity = identities.GetOrCreateValue(window);
        if (identity.Value != 0)
        {
            ulong retained = 0;
            window.Check(Native.ButtonStyleReacquire(window.Handle, identity.Value, &retained));
            if (retained != 0)
            {
                try { apply(retained); }
                finally { window.Check(Native.ButtonStyleRelease(retained)); }
                return;
            }
        }
        var rules = new Native.ButtonStyleRule[Rules.Count];
        for (int i = 0; i < rules.Length; ++i)
            rules[i] = new() { Size = (uint)sizeof(Native.ButtonStyleRule), State = (uint)Rules[i].State, Values = Rules[i].Values.ToNative() };
        void Create(ulong baseHandle)
        {
            ulong handle = 0;
            fixed (Native.ButtonStyleRule* p = rules)
            {
                var options = new Native.ButtonStyleOptions
                {
                    Size = (uint)sizeof(Native.ButtonStyleOptions), Version = 0x10000,
                    Values = Values.ToNative(), Rules = p, RuleCount = (uint)rules.Length,
                    BasedOn = baseHandle
                };
                window.Check(Native.ButtonStyleCreate(window.Handle, &options, &handle));
            }
            identity.Value = handle;
            try { apply(handle); }
            finally { window.Check(Native.ButtonStyleRelease(handle)); }
        }
        if (BasedOn is { } parent) parent.WithNativeHandle(window, Create);
        else Create(0);
    }

    internal unsafe bool TryApply(Window window, ulong handle)
    {
        if (!identities.TryGetValue(window, out var identity)) return false;
        uint applied = 0;
        window.Check(Native.ButtonTrySetStyle(handle, identity.Value, &applied));
        return applied != 0;
    }
}

public sealed partial class Button
{
    private ButtonStyle? style;
    public ButtonStyle? Style
    {
        get { Window.Guard(); return style; }
        set => SetStyle(value);
    }
    public Button SetStyle(ButtonStyle? value)
    {
        Window.Guard();
        if (value is null) Window.Check(Native.ButtonSetStyle(Handle, 0));
        else if (!value.TryApply(Window, Handle))
            value.WithNativeHandle(Window, handle => Window.Check(Native.ButtonSetStyle(Handle, handle)));
        style = value;
        return this;
    }
    public ButtonStyleValues StyleValues
    {
        get => ReadStyleValues(false);
        set => SetStyleValues(value);
    }
    public ButtonStyleValues EffectiveStyleValues => ReadStyleValues(true);
    public unsafe Button SetStyleValues(ButtonStyleValues value)
    {
        Window.Guard(); ArgumentNullException.ThrowIfNull(value);
        var native = value.ToNative();
        Window.Check(Native.ButtonSetStyleValues(Handle, &native));
        return this;
    }
    private unsafe ButtonStyleValues ReadStyleValues(bool effective)
    {
        Window.Guard();
        var value = ButtonStyleValues.Empty.ToNative();
        Window.Check(Native.ButtonGetStyleValues(Handle, effective ? 1u : 0u, &value));
        return ButtonStyleValues.FromNative(value);
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
