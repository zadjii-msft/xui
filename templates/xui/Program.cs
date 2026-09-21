//-:cnd:noEmit
using Xui;
using XuiApp;

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
    //+:cnd:noEmit

    private static Window CreateWindow() => new Window("XuiApp", 480, 240);
}
