using Xui.FileExplorer.Models;

namespace Xui.FileExplorer;

internal sealed class PreviewController(Application application, bool smoke) : IDisposable
{
    private readonly List<PreviewSession> sessions = [];
    internal PreviewSession? Current { get; private set; }
    internal IReadOnlyList<PreviewSession> Sessions => sessions;
    internal int OpenCount { get; private set; }
    internal string? LastOpenedPath { get; private set; }
    public bool IsOpen => Current?.IsOpen == true;

    public bool CanPreview(FilePaneView target) => target.HasCurrentRows && target.SelectedEntries.Length == 1;
    public void ShowSelected(FilePaneView target)
    {
        if (CanPreview(target)) Show(target, target.SelectedEntries[0]);
        else target.Report("Select one item to preview.");
    }
    public void Show(FilePaneView target, FileEntry selected)
    {
        if (!CanPreview(target) || target.SelectedEntries[0].FullPath != selected.FullPath)
        {
            target.Report("The selected item changed. Select one item to preview.");
            return;
        }
        var session = new PreviewSession(application, selected, smoke, () =>
        {
            ++OpenCount;
            LastOpenedPath = selected.FullPath;
        }, target.PresentationSettings);
        sessions.Add(session);
        Current = session;
        void ApplyPresentation() => session.ApplyCustomization(target.PresentationSettings);
        target.PresentationChanged += ApplyPresentation;
        session.Window.Closed += _ =>
        {
            target.PresentationChanged -= ApplyPresentation;
            sessions.Remove(session);
        };
        session.Show();
    }
    public void Dismiss() => Current?.Dismiss();
    internal void ApplyCustomization(ExplorerCustomization settings)
    {
        foreach (var session in sessions) session.ApplyCustomization(settings);
    }
    internal void Open() => Current?.Open();
    internal void CloseAll()
    {
        foreach (var session in sessions.ToArray()) session.Dismiss();
    }
    public void Dispose()
    {
        if (sessions.Count != 0) throw new InvalidOperationException("Close previews before disposing their controller.");
        Current = null;
    }

    internal bool Pending => Current?.Pending == true;
    internal string Message => Current?.Message ?? "";
    internal MultilineText Text => Current!.Text;
    internal Image Image => Current!.Image;
    internal Button OpenButton => Current!.OpenButton;
    internal ElementBounds Bounds => Current!.Bounds;
    internal VectorCanvas MetadataIcon => Current!.MetadataIcon;
    internal string MetadataName => Current!.MetadataName;
    internal string MetadataKind => Current!.MetadataKind;
    internal string MetadataSize => Current!.MetadataSize;
    internal ElementBounds MetadataNameBounds => Current!.MetadataNameBounds;
    internal bool StatusVisible => Current?.StatusVisible == true;
}
