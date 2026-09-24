using P = Xui.Experimental.Portable;

namespace Xui.Experimental.Windows;

public enum WindowsThemeAuthority { BorrowedSurface, ExclusiveWindow }

public sealed partial class WindowsBackend
{
    private static readonly System.Runtime.CompilerServices.ConditionalWeakTable<Window, ThemeOwner> themeOwners = new();
    private sealed class ThemeOwner { internal WindowsBackend? Backend; }
    private readonly WindowsThemeAuthority themeAuthority;
    private WindowThemeSettings? themeBaseline;

    public void ValidateTheme(P.ThemeSettings? theme)
    {
        VerifyAccess();
        ObjectDisposedException.ThrowIf(disposed, this);
        if (theme is null) return;
        if (themeAuthority != WindowsThemeAuthority.ExclusiveWindow)
            throw new NotSupportedException("This borrowed content surface has no window-wide theme authority.");
        if (!Window.SupportsSemanticTheme)
            throw new NotSupportedException("The Windows runtime does not provide the complete semantic theme contract.");
        if (themeOwners.TryGetValue(Window, out var owner) && owner.Backend is not null && owner.Backend != this)
            throw new InvalidOperationException("Another backend currently owns the window theme.");
        if (Window.State is WindowState.Closed or WindowState.Closing)
            throw new InvalidOperationException("A closing window cannot acquire theme authority.");
    }

    public void ApplyTheme(P.ThemeSettings? theme)
    {
        ValidateTheme(theme);
        if (theme is null) { RestoreTheme(); return; }
        if (themeBaseline is null)
        {
            themeBaseline = Window.GetThemeSettings();
            themeOwners.GetValue(Window, static _ => new ThemeOwner()).Backend = this;
        }
        static ThemeColor? Color(P.ThemeColor? color) => color is { } value ? new(value.Light, value.Dark) : null;
        Window.SetThemeSettings(new(theme.Mode switch
        {
            P.ThemeMode.System => Theme.System,
            P.ThemeMode.Light => Theme.Light,
            P.ThemeMode.Dark => Theme.Dark,
            _ => throw new ArgumentOutOfRangeException(nameof(theme))
        }, Color(theme.Resources.Foreground), Color(theme.Resources.Background), Color(theme.Resources.Accent)));
    }

    private void RestoreTheme()
    {
        if (themeBaseline is null) return;
        if (Window.State is WindowState.Created or WindowState.Open) Window.SetThemeSettings(themeBaseline);
        themeBaseline = null;
        if (themeOwners.TryGetValue(Window, out var owner) && owner.Backend == this) owner.Backend = null;
    }
}
