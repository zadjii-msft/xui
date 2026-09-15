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
            using var app = new ExplorerApplication(initialPath, args.Contains("--smoke"));
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
