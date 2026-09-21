using Xui.FileExplorer.Models;

namespace Xui.FileExplorer;

internal static class ExplorerPresentation
{
    public static void Apply(Window window, ExplorerCustomization settings)
    {
        window.SetTheme(settings.Theme switch
        {
            "light" => Theme.Light,
            "dark" => Theme.Dark,
            _ => Theme.System
        });
        window.SetPresentation(settings.FontFamily, settings.FontSize, settings.SmoothScrolling, settings.Animations);
    }

    public static ControlStyle NavigationItems(ExplorerCustomization settings) => new(StyleTarget.NavigationList,
    [
        new(StylePart.Root, new() { RowHeight = Math.Max(20, settings.RowHeight - 4), FontFamily = settings.FontFamily,
            FontSize = CompactFontSize(settings), Indentation = 12 }),
        new(StylePart.Row, new() { Padding = new(0) }),
        new(StylePart.Icon, new() { Size = 16 })
    ]);

    public static void ApplyNavigationItems(RetainedElement items, ExplorerCustomization settings)
    {
        items.SetPresentationFontSize(CompactFontSize(settings));
        items.SetControlStyle(NavigationItems(settings));
    }

    internal static float CompactFontSize(ExplorerCustomization settings) => Math.Max(9, settings.FontSize - 2);

    public static ControlStyle NavigationFilter(ExplorerCustomization settings) => new(StyleTarget.TextInput,
    [
        new(StylePart.Root, new() { BorderThickness = new(0, 0, 0, 1), CornerRadius = 0 }),
        new(StylePart.Text, new() { FontFamily = settings.FontFamily, FontSize = settings.FontSize })
    ]);

    internal static string FormatDate(DateTime value, string format)
    {
        try { return value.ToLocalTime().ToString(format, System.Globalization.CultureInfo.CurrentCulture); }
        catch (FormatException) { return value.ToLocalTime().ToString("yyyy-MM-dd HH:mm"); }
    }
}
