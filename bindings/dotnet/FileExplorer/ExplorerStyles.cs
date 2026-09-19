namespace Xui.FileExplorer;

internal static class ExplorerStyles
{
    // Match the WinUI window background until styles support transparent colors.
    private static readonly ThemeColor Background = new(0xF3F3F3, 0x202020);

    internal static readonly ButtonStyle BreadcrumbButton = new(new()
    {
        Background = Background,
        BorderThickness = new(0),
        CornerRadius = 4,
        Padding = new(2, 0, 2, 0)
    },
    [
        new(ButtonStyleState.Hovered, new() { Background = new(0xEAEAEA, 0x2D2D2D) }),
        new(ButtonStyleState.Pressed, new() { Background = new(0xE1E1E1, 0x292929) })
    ]);

    // Keep a named, keyboard-accessible button without a visible glyph in the trailing space.
    internal static readonly ControlStyle AddressSpace = new(StyleTarget.Button,
    [
        new(StylePart.Icon, new() { Size = 0 })
    ]);

    internal static readonly ControlStyle BreadcrumbLayout = new(StyleTarget.AdaptiveLayout,
    [
        new(StylePart.Root, new() { Padding = new(0), Spacing = 0 })
    ]);

    internal static readonly ControlStyle PreviewText = new(StyleTarget.MultilineText,
    [
        new(StylePart.Root, new() { Background = Background, BorderThickness = new(0), Padding = new(12), CornerRadius = 0 }),
        new(StylePart.Text, new() { FontFamily = "Cascadia Mono", FontSize = 14 })
    ]);

    internal static readonly ControlStyle PreviewImage = new(StyleTarget.Image,
    [
        new(StylePart.Root, new() { Background = Background, BorderThickness = new(0), CornerRadius = 0 })
    ]);

    internal static readonly ControlStyle NavigationFilter = new(StyleTarget.TextInput,
    [
        new(StylePart.Root, new() { Background = Background, BorderThickness = new(0,0,0,1), CornerRadius = 0 })
    ]);

    internal static readonly ControlStyle NavigationItems = new(StyleTarget.NavigationList,
    [
        new(StylePart.Root, new() { RowHeight = 28, FontSize = 12, Indentation = 12 }),
        new(StylePart.Row, new() { Padding = new(0) }),
        new(StylePart.GroupHeader, new() { Padding = new(0), Background = Background }),
        new(StylePart.Icon, new() { Size = 16 })
    ]);

    internal static readonly ButtonStyle IconButton = new(new()
    {
        Background = Background,
        BorderThickness = new(0),
        CornerRadius = 4,
        Padding = new(0)
    },
    [
        new(ButtonStyleState.Hovered, new() { Background = new(0xEAEAEA, 0x2D2D2D) }),
        new(ButtonStyleState.Pressed, new() { Background = new(0xE1E1E1, 0x292929) }),
        new(ButtonStyleState.Disabled, new() { Background = Background })
    ]);
}
