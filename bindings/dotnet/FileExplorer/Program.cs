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
            bool viewEntrySmoke = args.Contains("--view-entry-smoke");
            bool paneAnimationSmoke = args.Contains("--pane-animation-smoke");
            if (viewEntrySmoke && paneAnimationSmoke)
                throw new ArgumentException("Choose one focused smoke mode.");
            bool smoke = args.Contains("--smoke") || viewEntrySmoke || paneAnimationSmoke;
            using var application = new Xui.Application();
            using var previews = new PreviewController(application, smoke);
            using var app = new ExplorerApplication(application, previews, initialPath, smoke);
            app.Run(viewEntrySmoke ? ExplorerSmokeMode.ViewEntry :
                paneAnimationSmoke ? ExplorerSmokeMode.PaneAnimation : ExplorerSmokeMode.Full);
            return 0;
        }
        catch (Exception error)
        {
            Console.Error.WriteLine(error);
            return 1;
        }
    }
}
