using Xui;

internal static class Program
{
    [STAThread]
    private static void Main()
    {
#if XUI_HOT_RELOAD
        Xui.Development.ReloadHost.Run(CreateWindow, CreateContent);
#else
        using var window = CreateWindow();
        CreateContent(window);
        window.Run();
#endif
    }

    private static Window CreateWindow() =>
        new("Task card", 560, 440, visualStyle: VisualStyle.WinUI);

    private static void CreateContent(Window window)
    {
        var progress = window.Progress("Task completion")
            .SetRange(new(0, 100))
            .SetValue(0)
            .FixedSize(360, 20);
        _ = new Tutorial.TaskCard(window, "Today", progress);
    }
}
