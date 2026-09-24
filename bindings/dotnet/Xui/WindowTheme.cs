using System.Runtime.InteropServices;

namespace Xui;

public sealed record WindowThemeSettings(Theme Mode, ThemeColor? Foreground = null,
    ThemeColor? Background = null, ThemeColor? Accent = null);

public sealed partial class Window
{
    public static bool SupportsSemanticTheme => Native.WindowThemeAvailable.Value;

    /// <summary>Reads requested theme mode and semantic overrides, preserving System and unset colors.</summary>
    public WindowThemeSettings GetThemeSettings()
    {
        Guard();
        RequireSemanticTheme();
        var value = new Native.WindowThemeValue { Size = 40, Version = 0x10000 };
        Check(Native.WindowThemeGet(Handle, ref value));
        if (value.Size != 40 || value.Version != 0x10000 || value.Mode > 3 || (value.Mask & ~7u) != 0)
            throw new InvalidOperationException("The native window returned an invalid theme snapshot.");
        static ThemeColor? Color(uint mask, uint bit, Native.ThemeColor color)
        {
            if ((mask & bit) == 0)
            {
                if (color.Light != 0 || color.Dark != 0) throw new InvalidOperationException("An unset native theme color contains a value.");
                return null;
            }
            var result = new ThemeColor(color.Light, color.Dark);
            result.Validate();
            return result;
        }
        return new((Theme)value.Mode, Color(value.Mask, 1, value.Foreground),
            Color(value.Mask, 2, value.Background), Color(value.Mask, 4, value.Accent));
    }

    /// <summary>Atomically sets window-wide semantic colors below native state and high-contrast policy.</summary>
    public void SetThemeSettings(WindowThemeSettings settings)
    {
        ArgumentNullException.ThrowIfNull(settings);
        Guard();
        RequireSemanticTheme();
        if (!Enum.IsDefined(settings.Mode)) throw new ArgumentOutOfRangeException(nameof(settings));
        settings.Foreground?.Validate(); settings.Background?.Validate(); settings.Accent?.Validate();
        static Native.ThemeColor Color(ThemeColor? value) => value is { } color
            ? new() { Light = color.Light, Dark = color.Dark } : default;
        var value = new Native.WindowThemeValue
        {
            Size = 40, Version = 0x10000, Mode = (uint)settings.Mode,
            Mask = (settings.Foreground.HasValue ? 1u : 0u) | (settings.Background.HasValue ? 2u : 0u) |
                (settings.Accent.HasValue ? 4u : 0u),
            Foreground = Color(settings.Foreground), Background = Color(settings.Background), Accent = Color(settings.Accent)
        };
        Check(Native.WindowThemeSet(Handle, in value));
    }

    private static void RequireSemanticTheme()
    {
        if (!SupportsSemanticTheme) throw new NotSupportedException("This native XUI runtime does not provide semantic window themes.");
    }
}

internal static partial class Native
{
    internal static readonly Lazy<bool> WindowThemeAvailable = new(() =>
        HasExports("xui_window_theme_version", "xui_window_theme_get", "xui_window_theme_set") &&
        WindowThemeVersion() == 0x10000);
    [StructLayout(LayoutKind.Sequential)]
    internal struct WindowThemeValue
    {
        internal uint Size, Version, Mode, Mask;
        internal ThemeColor Foreground, Background, Accent;
    }
    [LibraryImport("xui", EntryPoint = "xui_window_theme_version")]
    internal static partial uint WindowThemeVersion();
    [LibraryImport("xui", EntryPoint = "xui_window_theme_get")]
    internal static partial int WindowThemeGet(ulong window, ref WindowThemeValue value);
    [LibraryImport("xui", EntryPoint = "xui_window_theme_set")]
    internal static partial int WindowThemeSet(ulong window, in WindowThemeValue value);
}
