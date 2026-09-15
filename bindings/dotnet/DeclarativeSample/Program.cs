using Demo;
using Xui;

internal static class Program
{
    [STAThread]
    private static void Main()
    {
#if XUI_HOT_RELOAD
        Xui.Development.ReloadHost.Run(CreateWindow, window => _ = new Counter(window));
#else
        using var window = CreateWindow();
        _ = new Counter(window);
        window.Run();
#endif
    }
    private static Window CreateWindow() => new("XUI Declarative", 480, 360);
}
