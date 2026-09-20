namespace Xui.FileExplorer;

internal sealed class ExplorerWindows(Application application, PreviewController preview, bool smoke, bool prefetchShellMenus = false) : IDisposable
{
    private readonly List<ExplorerApplication> windows = [];
    internal IReadOnlyList<ExplorerApplication> Windows => windows;

    internal ExplorerApplication Create(string path)
    {
        var controller = new ExplorerApplication(this, application, preview, path, smoke, prefetchShellMenus);
        windows.Add(controller);
        return controller;
    }

    internal ExplorerApplication? Find(Window window)
        => windows.Find(controller => ReferenceEquals(controller.Window, window)
            && !controller.IsDisposed && !controller.CloseRequested
            && controller.Window.State == WindowState.Open);

    internal void Forget(ExplorerApplication controller) => windows.Remove(controller);

    public void Dispose()
    {
        foreach (var controller in windows.ToArray()) controller.Dispose();
    }
}
