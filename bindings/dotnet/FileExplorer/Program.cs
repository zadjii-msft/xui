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
            bool viewSwitchSmoke = args.Contains("--view-switch-smoke") || args.Contains("--view-entry-smoke");
            bool paneAnimationSmoke = args.Contains("--pane-animation-smoke");
            bool hoverSmoke = args.Contains("--smoke-hover");
            bool viewsSmoke = args.Contains("--views-smoke");
            if ((viewSwitchSmoke ? 1 : 0) + (paneAnimationSmoke ? 1 : 0) + (hoverSmoke ? 1 : 0) + (viewsSmoke ? 1 : 0) > 1)
                throw new ArgumentException("Choose one focused smoke mode.");
            bool smoke = args.Contains("--smoke") || viewSwitchSmoke || paneAnimationSmoke || hoverSmoke || viewsSmoke;
            using var application = new Xui.Application();
            using var previews = new PreviewController(application, smoke);
            using var windows = new ExplorerWindows(application, previews, smoke);
            var app = windows.Create(initialPath);
            app.Run(viewSwitchSmoke ? ExplorerSmokeMode.ViewSwitch :
                paneAnimationSmoke ? ExplorerSmokeMode.PaneAnimation :
                hoverSmoke ? ExplorerSmokeMode.Hover :
                viewsSmoke ? ExplorerSmokeMode.Views : ExplorerSmokeMode.Full);
            return 0;
        }
        catch (Exception error)
        {
            Console.Error.WriteLine(error);
            return 1;
        }
    }
}
