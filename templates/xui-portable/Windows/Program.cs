using PortableApp;
using Xui.Experimental.Portable;
using Xui.Experimental.Windows;

internal static class Program
{
    [STAThread]
    private static int Main()
    {
        try
        {
            using var window = new Xui.Window("PortableApp", 520, 420);
            var surface = window.CreateContentHost();
            window.SetContent(window.Stack().Add(surface, 1));
            using var dispatcher = new WindowsDispatcher(window);
            using var host = new Host(dispatcher);
            _ = new Counter(host);
            host.Attach(new WindowsBackend(surface, dispatcher));
            window.Run();
            return 0;
        }
        catch (Exception error)
        {
            Console.Error.WriteLine(error);
            return 1;
        }
    }
}
