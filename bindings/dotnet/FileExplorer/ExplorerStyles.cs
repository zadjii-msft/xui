namespace Xui.FileExplorer;

internal static class ExplorerStyles
{
    // Match the WinUI window background until styles support transparent colors.
    private static readonly ThemeColor Background = new(0xF3F3F3, 0x202020);

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
