using Xui.FileExplorer;

internal static class Program
{
    [STAThread]
    private static int Main(string[] args)
    {
        try
        {
            string initialPath = args.FirstOrDefault(a => !a.StartsWith("--", StringComparison.Ordinal))
                ?? Environment.CurrentDirectory;
            bool smoke = args.Contains("--smoke");
            using var application = new Xui.Application();
            using var previews = new PreviewController(application, smoke);
            using var windows = new ExplorerWindows(application, previews, smoke);
            var app = windows.Create(initialPath);
            app.Run();
            return 0;
        }
        catch (Exception error)
        {
            Console.Error.WriteLine(error);
            return 1;
        }
    }
}
