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
    private static Window CreateWindow()
    {
        var window = new Window("XUI Declarative", 480, 360);
        try
        {
            window.IconErrorHandler = error => throw new InvalidOperationException($"Cannot load the application icon: {error}");
            window.SetIconSource(Path.Combine(AppContext.BaseDirectory, "zoey.ico"));
            return window;
        }
        catch
        {
            window.Dispose();
            throw;
        }
    }
}
