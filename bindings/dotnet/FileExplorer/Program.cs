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
            bool partitionSmoke = args.Contains("--partition-smoke");
            bool viewsSmoke = args.Contains("--views-smoke");
            bool addressSmoke = args.Contains("--address-smoke");
            bool previewSmoke = args.Contains("--preview-smoke");
            if ((viewSwitchSmoke ? 1 : 0) + (paneAnimationSmoke ? 1 : 0) + (hoverSmoke ? 1 : 0)
                + (viewsSmoke ? 1 : 0) + (addressSmoke ? 1 : 0) + (partitionSmoke ? 1 : 0) + (previewSmoke ? 1 : 0) > 1)
                throw new ArgumentException("Choose one focused smoke mode.");
            bool smoke = args.Contains("--smoke") || viewSwitchSmoke || paneAnimationSmoke || hoverSmoke || viewsSmoke || addressSmoke || partitionSmoke || previewSmoke;
            using var application = new Xui.Application();
            using var previews = new PreviewController(application, smoke);
            using var windows = new ExplorerWindows(application, previews, smoke, args.Contains("--prefetch-shell-menus"));
            var app = windows.Create(initialPath);
            app.Run(viewSwitchSmoke ? ExplorerSmokeMode.ViewSwitch :
                paneAnimationSmoke ? ExplorerSmokeMode.PaneAnimation :
                hoverSmoke ? ExplorerSmokeMode.Hover :
                partitionSmoke ? ExplorerSmokeMode.Partition :
                viewsSmoke ? ExplorerSmokeMode.Views :
                previewSmoke ? ExplorerSmokeMode.Preview :
                addressSmoke ? ExplorerSmokeMode.Address : ExplorerSmokeMode.Full);
            return 0;
        }
        catch (Exception error)
        {
            Console.Error.WriteLine(error);
            return 1;
        }
    }
}
