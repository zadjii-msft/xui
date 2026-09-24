namespace Xui.Experimental.Portable;

public enum TextRole { Body, Caption, Title }
public enum ThemeMode { System, Light, Dark }

/// <summary>Explicit typography in logical units; native accessibility font scaling still applies.</summary>
public sealed record Typography
{
    public TextRole Role { get; }
    public float? FontSize { get; }
    public uint? FontWeight { get; }
    public float ResolvedFontSize => FontSize ?? Role switch
    {
        TextRole.Body => 14,
        TextRole.Caption => 12,
        TextRole.Title => 24,
        _ => throw new InvalidOperationException("Unknown text role.")
    };
    public uint ResolvedFontWeight => FontWeight ?? (Role == TextRole.Title ? 700u : 400u);

    public Typography(TextRole role = TextRole.Body, float? fontSize = null, uint? fontWeight = null)
    {
        if (!Enum.IsDefined(role)) throw new ArgumentOutOfRangeException(nameof(role));
        if (fontSize is { } size && (!float.IsFinite(size) || size is < 8 or > 32))
            throw new ArgumentOutOfRangeException(nameof(fontSize), "Font size must be finite and between 8 and 32 logical units.");
        if (fontWeight is not (null or 400 or 700))
            throw new ArgumentOutOfRangeException(nameof(fontWeight), "Portable font weight must be 400 (normal) or 700 (bold).");
        Role = role;
        FontSize = fontSize;
        FontWeight = fontWeight;
    }
}

/// <summary>Opaque RGB24 colors selected by the effective light or dark theme.</summary>
public readonly record struct ThemeColor
{
    public uint Light { get; }
    public uint Dark { get; }

    public ThemeColor(uint color) : this(color, color) { }

    public ThemeColor(uint light, uint dark)
    {
        if (light > 0xffffff) throw new ArgumentOutOfRangeException(nameof(light), "Colors must be opaque 0xRRGGBB values.");
        if (dark > 0xffffff) throw new ArgumentOutOfRangeException(nameof(dark), "Colors must be opaque 0xRRGGBB values.");
        Light = light;
        Dark = dark;
    }
}

/// <summary>Immutable semantic overrides; null preserves the platform's resource for that role.</summary>
public sealed record ThemeResources
{
    public static ThemeResources Default { get; } = new();
    public ThemeColor? Foreground { get; }
    public ThemeColor? Background { get; }
    public ThemeColor? Accent { get; }
    public bool IsDefault => Foreground is null && Background is null && Accent is null;

    public ThemeResources(ThemeColor? foreground = null, ThemeColor? background = null, ThemeColor? accent = null)
    {
        Foreground = foreground;
        Background = background;
        Accent = accent;
    }
}

/// <summary>Presentation scoped to one host, subordinate to native high-contrast and reduced-motion policy.</summary>
public sealed record ThemeSettings
{
    public static ThemeSettings System { get; } = new();
    public ThemeMode Mode { get; }
    public ThemeResources Resources { get; }
    public ThemeSettings(ThemeMode mode = ThemeMode.System, ThemeResources? resources = null)
    {
        if (!Enum.IsDefined(mode)) throw new ArgumentOutOfRangeException(nameof(mode));
        Mode = mode;
        Resources = resources ?? ThemeResources.Default;
    }
}

/// <summary>Validates typography without mutation before a host commits an authored update.</summary>
public interface IPresentationPeer : IElementPeer
{
    void ValidateTypography(Typography? typography);
}

/// <summary>Applies theme resources only to this backend's owned host surface.</summary>
public interface IThemeBackend : IBackend
{
    void ValidateTheme(ThemeSettings? theme);
    void ApplyTheme(ThemeSettings? theme);
}
