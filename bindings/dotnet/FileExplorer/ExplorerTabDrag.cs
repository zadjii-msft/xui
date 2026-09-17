using Xui.FileExplorer.Models;

namespace Xui.FileExplorer;

internal sealed partial class ExplorerApplication
{
    private sealed record DragState(ulong TabId, ExplorerDragSnapshot Snapshot, WindowPlacement Placement,
        bool SplitOpen, bool RightInitialized, bool ActiveRight, double Ratio)
    {
        internal ExplorerApplication? Remainder { get; set; }
        internal bool TornOut { get; set; }
    }

    private DragState? drag;
    internal bool HasTabDrag => drag is not null;
    internal ExplorerApplication? DragRemainder => drag?.Remainder;
    internal IReadOnlyList<ExplorerApplication> ExplorerWindows => windows.Windows;

    private FilePaneView? Pane(uint strip) => strip switch { 0 => Left, 1 => Right, _ => null };

    private DragState CaptureDrag(ulong id)
    {
        Left.CaptureViewport();
        Right.CaptureViewport();
        return new(id, new(Left.Model, Right.Model), Window.Placement,
            splitOpen, rightInitialized, ReferenceEquals(Active, Right), split.Ratio);
    }

    private bool RejectDrag(string message)
    {
        Report($"Cannot move tab: {message}");
        return false;
    }

    private sealed record TabDestination(FilePaneView Source, ExplorerApplication Owner, FilePaneView Target);

    private TabDestination? ValidateTabDestination(TabDragEvent e, out string error)
    {
        error = "The source tab is no longer in this strip.";
        var source = Pane(e.SourceStrip);
        if (Window.State != WindowState.Open || source is null
            || source.Model.Tabs.All(tab => tab.Id != e.TabId)) return null;
        error = "Another tab gesture is still active.";
        if (drag is not null && drag.TabId != e.TabId) return null;
        error = "Reorder must stay in the source strip.";
        if (e.Kind == TabDragKind.Reorder && e.TargetStrip != e.SourceStrip) return null;
        var owner = e.Kind == TabDragKind.Reorder ? this
            : e.Target is null ? null : windows.Find(e.Target);
        error = "The destination window is no longer available.";
        if (owner is null || (e.Kind == TabDragKind.Reorder && e.Target is not null
            && !ReferenceEquals(e.Target, Window))) return null;
        var target = owner.Pane(e.TargetStrip);
        error = "The destination pane is not available.";
        if (target is null || (e.TargetStrip == 1
                && (!owner.SecondPaneVisible || !owner.Window.TitlebarSecondaryTabs.Visible))
            || (e.TargetStrip == 0 && !owner.Window.TitlebarTabs.Visible)
            || (!ReferenceEquals(owner, this) && owner.drag is not null)) return null;
        error = $"The tab or insertion slot changed, or the destination reached its {ExplorerPane.TabLimit}-tab limit.";
        if (!source.Model.CanTransferTab(e.TabId, target.Model, e.Index)) return null;
        error = "";
        return new(source, owner, target);
    }

    private bool HandleTabDrag(TabDragEvent e)
    {
        if (disposed || CloseRequested) return false;
        // Preview must not capture viewports, cancel work, report errors, or start a transaction.
        if (e.Kind == TabDragKind.QueryDrop) return ValidateTabDestination(e, out _) is not null;
        if (drag is not null && drag.TabId != e.TabId)
            return RejectDrag("Another tab gesture is still active.");
        try
        {
            switch (e.Kind)
            {
                case TabDragKind.Cancel:
                    return CancelTabDrag();
                case TabDragKind.Completed:
                    drag = null;
                    NormalizeTransferredPanes();
                    return true;
                case TabDragKind.Reorder:
                case TabDragKind.Drop:
                {
                    var destination = ValidateTabDestination(e, out string error);
                    if (destination is null) return RejectDrag(error);
                    var (source, owner, target) = destination;
                    source.CaptureViewport();
                    target.CaptureViewport();
                    var previousDrag = drag;
                    var sourceModel = new ExplorerPane(source.Model.Tabs, source.Model.Active.Id);
                    var targetModel = ReferenceEquals(source, target) ? sourceModel
                        : new ExplorerPane(target.Model.Tabs,
                            target.Model.Tabs.Count == 0 ? 0 : target.Model.Active.Id);
                    var sourceActive = active;
                    var targetActive = owner.active;
                    drag ??= CaptureDrag(e.TabId);
                    try
                    {
                        Palettes.Dismiss();
                        if (!ReferenceEquals(owner, this)) owner.Palettes.Dismiss();
                        source.CancelForTransfer();
                        target.CancelForTransfer();
                        source.Model.TransferTab(e.TabId, target.Model, e.Index);
                        active = source.Model.Tabs.Count != 0 ? source
                            : Left.Model.Tabs.Count != 0 ? Left : Right;
                        owner.active = target;
                        RenderTransferredPanes();
                        if (!ReferenceEquals(owner, this)) owner.RenderTransferredPanes();
                    }
                    catch
                    {
                        source.SetTransferredModel(sourceModel);
                        if (!ReferenceEquals(source, target)) target.SetTransferredModel(targetModel);
                        active = sourceActive;
                        owner.active = targetActive;
                        drag = previousDrag;
                        RenderTransferredPanes();
                        if (!ReferenceEquals(owner, this)) owner.RenderTransferredPanes();
                        throw;
                    }
                    if (e.Kind == TabDragKind.Drop)
                    {
                        drag = null;
                        NormalizeTransferredPanes();
                    }
                    return true;
                }
                case TabDragKind.TearOut:
                    return TearOutTab(e.SourceStrip, e.TabId);
                default:
                    return RejectDrag("The tab gesture is not supported.");
            }
        }
        catch (Exception error) when (error is XuiException or InvalidOperationException or ArgumentException
            || UiWork.IsExpected(error))
        {
            return RejectDrag(error.Message);
        }
    }

    private bool TearOutTab(uint strip, ulong id)
    {
        var pane = Pane(strip);
        if (pane is null || pane.Model.Tabs.SingleOrDefault(tab => tab.Id == id) is not { } tab)
            return RejectDrag("The source tab is no longer in this strip.");
        if (drag?.TornOut == true) return true;
        drag ??= CaptureDrag(id);
        var state = drag;
        // A lone visible tab already has the required HWND. Do not manufacture an empty document.
        if (Left.Model.Tabs.Count + (rightInitialized ? Right.Model.Tabs.Count : 0) == 1)
        {
            state.TornOut = true;
            return true;
        }

        var remainingLeft = strip == 0 ? ExplorerDragSnapshot.Without(Left.Model, id) : Left.Model;
        var remainingRight = strip == 1 ? ExplorerDragSnapshot.Without(Right.Model, id) : Right.Model;
        var moving = new ExplorerPane([tab], id);
        var empty = ExplorerDragSnapshot.Empty();
        ExplorerApplication? remainder = null;
        var originalLeft = Left.Model;
        var originalRight = Right.Model;
        try
        {
            remainder = windows.Create(tab.Path);
            remainder.Window.Placement = state.Placement;
            remainder.Window.SetShowActivated(false);
            remainder.light = light;
            remainder.Window.SetTheme(light ? Theme.Light : Theme.Dark);
            if (remainder.Sidebar.IsOpen != Sidebar.IsOpen) remainder.Sidebar.Toggle();
            Palettes.Dismiss();
            Left.CancelForTransfer();
            Right.CancelForTransfer();
            remainder.Left.SetTransferredModel(remainingLeft);
            remainder.Right.SetTransferredModel(remainingRight);
            remainder.splitOpen = splitOpen;
            remainder.rightInitialized = rightInitialized;
            remainder.split.Ratio = state.Ratio;
            remainder.active = state.ActiveRight ? remainder.Right : remainder.Left;
            remainder.NormalizeTransferredPanes();
            Left.SetTransferredModel(strip == 0 ? moving : empty);
            Right.SetTransferredModel(strip == 1 ? moving : empty);
            active = pane;
            rightInitialized = strip == 1;
            splitOpen = strip == 1;
            RenderTransferredPanes();
            Application.Show(remainder.Window);
            state.Remainder = remainder;
            state.TornOut = true;
            return true;
        }
        catch
        {
            Left.SetTransferredModel(originalLeft);
            Right.SetTransferredModel(originalRight);
            splitOpen = state.SplitOpen;
            rightInitialized = state.RightInitialized;
            active = state.ActiveRight ? Right : Left;
            RenderTransferredPanes();
            if (remainder is not null) RetireWindow(remainder);
            throw;
        }
    }

    private bool CancelTabDrag()
    {
        if (drag is not { } state) return true;
        var remainder = state.Remainder;
        if (remainder is not null && (remainder.disposed || remainder.CloseRequested
            || remainder.Window.State is not WindowState.Open))
            return RejectDrag("The original window closed. Keep the detached tab in this window.");
        var panes = remainder is null ? new[] { Left.Model, Right.Model }
            : new[] { Left.Model, Right.Model, remainder.Left.Model, remainder.Right.Model };
        if (!state.Snapshot.CanRestore(panes))
            return RejectDrag("The tabs changed during the gesture. Keep the current windows to avoid losing tabs.");
        Left.CaptureViewport();
        Right.CaptureViewport();
        remainder?.Left.CaptureViewport();
        remainder?.Right.CaptureViewport();
        remainder?.Left.CancelForTransfer();
        remainder?.Right.CancelForTransfer();
        var restored = state.Snapshot.Restore();
        Left.SetTransferredModel(restored.Left);
        Right.SetTransferredModel(restored.Right);
        splitOpen = state.SplitOpen;
        rightInitialized = state.RightInitialized;
        active = state.ActiveRight ? Right : Left;
        split.Ratio = state.Ratio;
        Window.Placement = state.Placement;
        RenderTransferredPanes();
        drag = null;
        if (remainder is not null)
        {
            remainder.Left.SetTransferredModel(ExplorerDragSnapshot.Empty());
            remainder.Right.SetTransferredModel(ExplorerDragSnapshot.Empty());
            RetireWindow(remainder);
        }
        return true;
    }

    private void RenderTransferredPanes()
    {
        bool hasLeft = Left.Model.Tabs.Count != 0;
        bool hasRight = Right.Model.Tabs.Count != 0;
        Window.TitlebarTabs.Visible = hasLeft;
        split.FirstVisible = hasLeft;
        split.SecondVisible = splitOpen && hasRight;
        Window.TitlebarSecondaryTabs.Visible = splitOpen && hasRight;
        Left.RenderTransferredModel();
        Right.RenderTransferredModel();
        if (!hasLeft && !hasRight) return;
        if (Active.Model.Tabs.Count == 0) active = hasLeft ? Left : Right;
        Sidebar.Refresh();
        UpdateTitle();
    }

    private void NormalizeTransferredPanes()
    {
        if (Left.Model.Tabs.Count == 0 && !rightInitialized)
            Right.SetTransferredModel(ExplorerDragSnapshot.Empty());
        if (Left.Model.Tabs.Count == 0 && Right.Model.Tabs.Count == 0)
        {
            RetireWindow(this);
            return;
        }
        if (Left.Model.Tabs.Count == 0)
        {
            Left.SetTransferredModel(Right.Model);
            Right.SetTransferredModel(ExplorerDragSnapshot.Empty());
            active = Left;
            splitOpen = rightInitialized = false;
        }
        else if (Right.Model.Tabs.Count == 0) splitOpen = rightInitialized = false;
        RenderTransferredPanes();
    }

    private void RetireWindow(ExplorerApplication controller)
    {
        if (controller.CloseRequested || controller.disposed) return;
        controller.CloseRequested = true;
        controller.Left.Cancel();
        controller.Right.Cancel();
        if (!Application.Post(() =>
        {
            if (controller.disposed) return;
            if (controller.Window.State == WindowState.Created) controller.Dispose();
            else if (controller.Window.State == WindowState.Open) controller.Window.Close();
        }))
            throw new InvalidOperationException("The application rejected Explorer window retirement.");
    }
}
