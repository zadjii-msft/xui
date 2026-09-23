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
            bool customizationSmoke = args.Contains("--customization-smoke");
            bool settingsScrollSmoke = args.Contains("--settings-scroll-smoke");
            bool settingsOpenSmoke = args.Contains("--settings-open-smoke");
            bool contextActionsSmoke = args.Contains("--context-actions-smoke");
            bool directoryChangesSmoke = args.Contains("--directory-changes-smoke");
            if ((viewSwitchSmoke ? 1 : 0) + (paneAnimationSmoke ? 1 : 0) + (hoverSmoke ? 1 : 0)
                + (viewsSmoke ? 1 : 0) + (addressSmoke ? 1 : 0) + (partitionSmoke ? 1 : 0) + (previewSmoke ? 1 : 0) + (customizationSmoke ? 1 : 0) + (settingsScrollSmoke ? 1 : 0) + (settingsOpenSmoke ? 1 : 0) + (contextActionsSmoke ? 1 : 0) + (directoryChangesSmoke ? 1 : 0) > 1)
                throw new ArgumentException("Choose one focused smoke mode.");
            bool smoke = args.Contains("--smoke") || viewSwitchSmoke || paneAnimationSmoke || hoverSmoke || viewsSmoke || addressSmoke || partitionSmoke || previewSmoke || customizationSmoke || settingsScrollSmoke || settingsOpenSmoke || contextActionsSmoke || directoryChangesSmoke;
            using var application = new Xui.Application();
            using var previews = new PreviewController(application, smoke);
            using var windows = new ExplorerWindows(application, previews, smoke, args.Contains("--prefetch-shell-menus"));
            var app = windows.Create(initialPath);
            app.Run(directoryChangesSmoke ? ExplorerSmokeMode.DirectoryChanges : settingsOpenSmoke ? ExplorerSmokeMode.SettingsOpen : settingsScrollSmoke ? ExplorerSmokeMode.SettingsScroll : contextActionsSmoke ? ExplorerSmokeMode.ContextActions : customizationSmoke ? ExplorerSmokeMode.Customization : viewSwitchSmoke ? ExplorerSmokeMode.ViewSwitch :
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
