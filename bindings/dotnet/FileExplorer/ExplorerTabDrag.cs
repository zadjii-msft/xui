using Xui.FileExplorer.Models;

namespace Xui.FileExplorer;

internal sealed partial class ExplorerApplication
{
    private sealed record DragState(ulong TabId, ExplorerDragSnapshot Snapshot, WindowPlacement Placement,
        bool SplitOpen, bool RightInitialized, bool ActiveRight, double Ratio)
    {
        internal ExplorerApplication? Remainder { get; set; }
        internal bool TornOut { get; set; }
        internal HostedTab? Hosted { get; set; }
    }

    private sealed record HostedTab(ExplorerApplication Owner, FilePaneView Source, FilePaneView Target,
        uint SourceStrip, uint TargetStrip, ExplorerTabJoin Lease, FilePaneView PreviousActive,
        Action<WindowClosedEventArgs> SourceClosed, Action<WindowClosedEventArgs> TargetClosed);

    private DragState? drag;
    private ExplorerApplication? hostedBy;
    private bool dropCommitted;
    private readonly List<ExplorerApplication> deferredDragRetirements = [];
    internal bool HasTabDrag => drag is not null || hostedBy is not null || deferredDragRetirements.Count != 0;
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
        var hosted = drag?.Hosted;
        var source = hosted is null ? Pane(e.SourceStrip)
            : hosted.SourceStrip == e.SourceStrip && hosted.Lease.ContainsTab
                && ReferenceEquals(hosted.Target.Model, hosted.Lease.Target) ? hosted.Target : null;
        if (Window.State != WindowState.Open || source is null
            || source.Model.Tabs.All(tab => tab.Id != e.TabId)) return null;
        error = "Another tab gesture is still active.";
        if (hostedBy is not null || (drag is not null && drag.TabId != e.TabId)) return null;
        error = "Reorder must stay in the source strip.";
        if (e.Kind == TabDragKind.Reorder && (e.TargetStrip != e.SourceStrip || hosted is not null)) return null;
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
            || (!ReferenceEquals(owner, this) && (owner.drag is not null || owner.deferredDragRetirements.Count != 0))
            || (owner.hostedBy is not null && !ReferenceEquals(owner.hostedBy, this))) return null;
        error = $"The tab or insertion slot changed, or the destination reached its {ExplorerPane.TabLimit}-tab limit.";
        if (!source.Model.CanTransferTab(e.TabId, target.Model, e.Index)) return null;
        error = "";
        return new(source, owner, target);
    }

    private bool HandleTabDrag(TabDragEvent e)
    {
        if (disposed || CloseRequested) return false;
        if (e.Kind != TabDragKind.Completed && (dropCommitted || deferredDragRetirements.Count != 0))
            return e.Kind == TabDragKind.QueryDrop ? false : RejectDrag("Wait for the current gesture to complete.");
        // Preview must not capture viewports, cancel work, report errors, or start a transaction.
        if (e.Kind == TabDragKind.QueryDrop) return ValidateTabDestination(e, out _) is not null;
        if (hostedBy is not null) return RejectDrag("This window is hosting another tab gesture.");
        if (drag is not null && drag.TabId != e.TabId)
            return RejectDrag("Another tab gesture is still active.");
        try
        {
            switch (e.Kind)
            {
                case TabDragKind.Cancel:
                    return CancelTabDrag();
                case TabDragKind.Completed:
                    if (drag?.Hosted is not null && !LeaveJoinedTab()) return false;
                    drag = null;
                    dropCommitted = false;
                    RetireDeferredDragWindows();
                    NormalizeTransferredPanes();
                    return true;
                case TabDragKind.Join:
                    return JoinTab(e);
                case TabDragKind.Leave:
                    if (drag?.Hosted is null)
                        return Pane(e.SourceStrip)?.Model.Tabs.Any(tab => tab.Id == e.TabId) == true
                            || RejectDrag("The dragged tab is not in its original source strip.");
                    if (drag?.Hosted is { } leaving && ((e.Target is not null && e.Target != leaving.Owner.Window)
                        || e.TargetStrip != leaving.TargetStrip || e.SourceStrip != leaving.SourceStrip))
                        return RejectDrag("The hosted destination changed before leave.");
                    return LeaveJoinedTab();
                case TabDragKind.Reorder:
                case TabDragKind.Drop:
                {
                    if (e.Kind == TabDragKind.Drop && drag?.Hosted is not null) return CommitJoinedTab(e);
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
                        dropCommitted = true;
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

    private bool JoinTab(TabDragEvent e)
    {
        if (drag is not { TornOut: true } state)
            return RejectDrag("Tear out the tab before joining another window.");
        var destination = ValidateTabDestination(e, out string error);
        if (destination is null) return RejectDrag(error);
        var (source, owner, target) = destination;
        if (ReferenceEquals(owner, this)) return RejectDrag("Hover join requires another window.");
        if (state.Hosted is { } hosted)
        {
            if (!ReferenceEquals(hosted.Owner, owner) || !ReferenceEquals(hosted.Target, target))
                return RejectDrag("Leave the previous destination before joining another pane.");
            return ReorderJoinedTab(hosted, e.Index);
        }
        source.CaptureViewport();
        target.CaptureViewport();
        source.CancelForTransfer();
        target.CancelForTransfer();
        Palettes.Dismiss();
        owner.Palettes.Dismiss();
        var lease = ExplorerTabJoin.Join(source.Model, target.Model, e.TabId, e.Index);
        if (lease is null) return RejectDrag("The destination changed before joining.");
        hosted = new(owner, source, target, e.SourceStrip, e.TargetStrip, lease, owner.Active,
            _ => JoinedSourceClosed(state), _ => JoinedTargetClosed(state));
        state.Hosted = hosted;
        owner.hostedBy = this;
        Window.Closed += hosted.SourceClosed;
        owner.Window.Closed += hosted.TargetClosed;
        try
        {
            owner.active = target;
            RenderTransferredPanes();
            owner.RenderTransferredPanes();
            return true;
        }
        catch
        {
            if (!lease.Leave()) throw new InvalidOperationException("The failed hover join could not be rolled back.");
            ReleaseHost(state);
            active = source;
            owner.active = hosted.PreviousActive;
            RenderTransferredPanes();
            owner.RenderTransferredPanes();
            throw;
        }
    }

    private bool ReorderJoinedTab(HostedTab hosted, int index)
    {
        if (!ReferenceEquals(hosted.Target.Model, hosted.Lease.Target) || !hosted.Lease.ContainsTab
            || !hosted.Lease.Target.CanTransferTab(hosted.Lease.Tab.Id, hosted.Lease.Target, index))
            return RejectDrag("The hosted tab or insertion slot changed.");
        if (hosted.Lease.IsUnchangedSlot(index)) return true;
        hosted.Target.CaptureViewport();
        hosted.Target.CancelForTransfer();
        var model = hosted.Lease.Target;
        int oldIndex = model.Tabs.IndexOf(hosted.Lease.Tab);
        ulong selected = model.Active.Id;
        hosted.Lease.Reorder(index);
        try { hosted.Owner.RenderTransferredPanes(); }
        catch
        {
            int current = model.Tabs.IndexOf(hosted.Lease.Tab);
            hosted.Lease.Reorder(current < oldIndex ? oldIndex + 1 : oldIndex);
            model.SelectTab(selected);
            hosted.Owner.RenderTransferredPanes();
            throw;
        }
        return true;
    }

    private bool LeaveJoinedTab()
    {
        if (drag is not { Hosted: { } hosted } state) return true;
        bool targetLive = !hosted.Owner.disposed && !hosted.Owner.CloseRequested
            && hosted.Owner.Window.State == WindowState.Open;
        if (targetLive)
        {
            hosted.Target.CaptureViewport();
            hosted.Target.CancelForTransfer();
        }
        hosted.Source.CancelForTransfer();
        bool restoreActive = targetLive && ReferenceEquals(hosted.Owner.Active, hosted.Target)
            && hosted.Target.Model.Tabs.Count != 0 && ReferenceEquals(hosted.Target.Model.Active, hosted.Lease.Tab);
        if (!hosted.Lease.RecoverTo(hosted.Source.Model))
        {
            PreserveHostedTab(state);
            return RejectDrag("The original pane cannot receive its tab. The tab was preserved in a recovery window.");
        }
        ReleaseHost(state);
        active = hosted.Source;
        if (restoreActive && hosted.PreviousActive.Model.Tabs.Count != 0)
            hosted.Owner.active = hosted.PreviousActive;
        RenderTransferredPanes();
        if (targetLive) hosted.Owner.RenderTransferredPanes();
        return true;
    }

    private bool CommitJoinedTab(TabDragEvent e)
    {
        var state = drag!;
        var hosted = state.Hosted!;
        if (e.SourceStrip != hosted.SourceStrip || e.Target != hosted.Owner.Window
            || e.TargetStrip != hosted.TargetStrip || ValidateTabDestination(e, out _) is null)
            return RejectDrag("Release must commit the current hosted destination.");
        if (!ReorderJoinedTab(hosted, e.Index)) return false;
        if (!hosted.Lease.Commit()) return RejectDrag("The hosted tab changed before commit.");
        ReleaseHost(state);
        dropCommitted = true;
        return true;
    }

    private void ReleaseHost(DragState state)
    {
        if (state.Hosted is not { } hosted) return;
        Window.Closed -= hosted.SourceClosed;
        hosted.Owner.Window.Closed -= hosted.TargetClosed;
        if (ReferenceEquals(hosted.Owner.hostedBy, this)) hosted.Owner.hostedBy = null;
        state.Hosted = null;
    }

    private void JoinedSourceClosed(DragState state)
    {
        if (state.Hosted is not { } hosted) return;
        if (!hosted.Owner.disposed && !hosted.Owner.CloseRequested
            && ReferenceEquals(hosted.Target.Model, hosted.Lease.Target) && hosted.Lease.Commit())
            ReleaseHost(state);
        else PreserveHostedTab(state);
    }

    private void JoinedTargetClosed(DragState state)
    {
        if (state.Hosted is not { } hosted) return;
        if (disposed || CloseRequested || !hosted.Lease.RecoverTo(hosted.Source.Model))
        {
            PreserveHostedTab(state);
            return;
        }
        ReleaseHost(state);
        active = hosted.Source;
        // A closed target cannot service view calls. Restore only the live source after dispatch.
        if (!Application.Post(() =>
        {
            if (!disposed && !CloseRequested) RenderTransferredPanes();
        }))
            throw new InvalidOperationException("The application rejected hosted-tab recovery.");
    }

    private void PreserveHostedTab(DragState state)
    {
        var hosted = state.Hosted!;
        var recovery = windows.Create(hosted.Lease.Tab.Path);
        recovery.Window.Placement = state.Placement;
        var model = ExplorerDragSnapshot.Empty();
        if (!hosted.Lease.RecoverTo(model))
            throw new InvalidOperationException("The interrupted hover gesture could not preserve its retained tab.");
        recovery.Left.SetTransferredModel(model);
        recovery.Right.SetTransferredModel(ExplorerDragSnapshot.Empty());
        recovery.NormalizeTransferredPanes();
        recovery.Report("The hover gesture could not restore its original pane. Your tab was preserved in this window.");
        Application.Show(recovery.Window);
        ReleaseHost(state);
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
        if (drag?.Hosted is not null && !LeaveJoinedTab()) return false;
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
            DeferDragRetirement(remainder);
        }
        return true;
    }

    private void DeferDragRetirement(ExplorerApplication controller)
    {
        if (deferredDragRetirements.Count == 0) Window.Closed += FinishDeferredDragRetirement;
        deferredDragRetirements.Add(controller);
    }

    private void FinishDeferredDragRetirement(WindowClosedEventArgs _) => RetireDeferredDragWindows();

    private void RetireDeferredDragWindows()
    {
        Window.Closed -= FinishDeferredDragRetirement;
        foreach (var controller in deferredDragRetirements) RetireWindow(controller);
        deferredDragRetirements.Clear();
    }

    private void RenderTransferredPanes()
    {
        bool hasLeft = Left.Model.Tabs.Count != 0;
        bool hasRight = Right.Model.Tabs.Count != 0;
        bool retainLeft = drag?.Hosted?.SourceStrip == 0;
        bool retainRight = drag?.Hosted?.SourceStrip == 1;
        Window.TitlebarTabs.Visible = hasLeft || retainLeft;
        split.FirstVisible = hasLeft || retainLeft;
        split.SecondVisible = splitOpen && (hasRight || retainRight);
        Window.TitlebarSecondaryTabs.Visible = splitOpen && (hasRight || retainRight);
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
