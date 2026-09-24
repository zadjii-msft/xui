using Xui.Experimental.Portable;

namespace PortableDemo;

public sealed partial class VirtualList
{
    private VirtualListState? data;
    private VirtualizationController<VirtualListRow>? controller;
    private Action<float>? requestOffset;
    private string? selectedKey;
    private bool reverse;
    private bool filtered;
    private Host? owner;
    private bool leased;
    private ISettledVirtualViewportLease? viewportLease;
    private Action<Element, VirtualItemInfo>? setItemInfo;
    private bool focusAfterCommit;
    private bool viewportWaiting;
    private long navigationSourceVersion;
    private float navigationViewportHeight;
    private float? restoreOffset;
    private Action<VirtualViewportRequest>? viewportCommitted;
    private bool retired;
    private Host Owner => owner ?? throw new InvalidOperationException("Call VirtualList.Create.");

    public VirtualListState Data => data ?? throw new InvalidOperationException("Call VirtualList.Create.");
    public VirtualizationController<VirtualListRow> Controller =>
        controller ?? throw new InvalidOperationException("Call VirtualList.Create.");
    /// <summary>
    /// Observe completed pruning and its synchronous native geometry flush. Initial native
    /// interaction snapshots may still be queued; queue subsequent UI work instead of reentering.
    /// </summary>
    public event Action<VirtualViewportRequest> ViewportCommitted
    {
        add
        {
            Owner.VerifyComponentMutation(Root);
            viewportCommitted += value;
        }
        remove
        {
            var lifetime = Owner.GetComponentLifetime(Root);
            if (!retired && !lifetime.Token.IsCancellationRequested) Owner.VerifyComponentMutation(Root);
            viewportCommitted -= value;
        }
    }

    public static VirtualList Create(Host host, VirtualListState? state = null)
    {
        var view = new VirtualList(host);
        view.Initialize(host, state ?? new VirtualListState(), leased: false);
        return view;
    }

    public static VirtualList CreateForViewport(Host host, VirtualListState? state = null)
    {
        var view = new VirtualList(host);
        view.Initialize(host, state ?? new VirtualListState(), leased: true);
        return view;
    }

    private void Initialize(Host host, VirtualListState state, bool leased)
    {
        owner = host;
        this.leased = leased;
        data = state;
        var lifetime = host.GetComponentLifetime(Root);
        lifetime.Own(new Retirement(this));
        controller = lifetime.Own(new VirtualizationController<VirtualListRow>(
            host, RowsView, 128, 2, (owner, key) => new VirtualListRow(owner, Data.Items[key],
                interaction => AcceptInteraction(key, interaction.HasFocus, interaction.IsComposing), () => NextFrom(key))));
        controller.Retiring += SaveSelection;
        selectedKey = Data.Items.Keys.FirstOrDefault();
        Project();
        Ready = !leased;
    }

    public void ConnectScrollRequest(Action<float> scroll)
    {
        Owner.VerifyComponentMutation(Root);
        ArgumentNullException.ThrowIfNull(scroll);
        requestOffset = scroll;
    }

    public void AttachViewport(IVirtualViewportLease lease, Action<Element, VirtualItemInfo> setItemInfo)
    {
        Owner.VerifyComponentMutation(Root);
        ArgumentNullException.ThrowIfNull(lease);
        ArgumentNullException.ThrowIfNull(setItemInfo);
        if (lease is not ISettledVirtualViewportLease settled)
            throw new NotSupportedException("This sample requires explicit native post-prune geometry completion.");
        if (!leased) throw new InvalidOperationException("Use CreateForViewport for native viewport attachments.");
        if (Controller.Count != 0 || Controller.Mounted.Count != 0 || RowsView.Children.Count != 0)
            throw new InvalidOperationException("Call PrepareForViewportAttachment while detached before attaching a new backend.");
        viewportLease?.Dispose();
        viewportLease = settled;
        this.setItemInfo = setItemInfo;
        Ready = false;
        requestOffset = lease.RequestOffset;
        if (restoreOffset is float target)
        {
            restoreOffset = null;
            lease.RequestOffset(target);
        }
    }

    public void PrepareForViewportAttachment()
    {
        Owner.VerifyComponentMutation(Root);
        if (!leased) throw new InvalidOperationException("Use CreateForViewport for native viewport attachments.");
        if (Owner.IsAttached) throw new InvalidOperationException("Capture editing state and detach before preparing another attachment.");
        restoreOffset ??= Controller.Offset;
        Controller.PrepareForViewportAttachment();
        viewportLease = null;
        requestOffset = null;
        setItemInfo = null;
        Ready = false;
        viewportWaiting = false;
        navigationSourceVersion = 0;
        navigationViewportHeight = 0;
        ShowStatus();
    }

    public void OnViewportRequested(VirtualViewportRequest request)
    {
        Owner.VerifyComponentMutation(Root);
        var lease = viewportLease ?? throw new InvalidOperationException("A viewport callback ran before its attachment was connected.");
        try
        {
            if (focusAfterCommit && selectedKey is not null &&
                (navigationSourceVersion != request.RequestedSourceVersion || navigationViewportHeight != request.Requested.Height))
            {
                navigationSourceVersion = request.RequestedSourceVersion;
                navigationViewportHeight = request.Requested.Height;
                if (!Controller.TryGetRevealOffset(selectedKey, request.RequestedSourceVersion,
                    request.Requested.Offset, request.Requested.Height, out float target))
                    focusAfterCommit = false;
                else if (target != request.Requested.Offset)
                {
                    lease.RequestOffset(target);
                    return;
                }
            }
            var begin = lease.TryBeginUpdate(request.Epoch);
            if (!Enum.IsDefined(begin)) throw new InvalidOperationException("The native viewport returned an invalid update result.");
            if (begin != VirtualViewportUpdateResult.Ready)
            {
                viewportWaiting = begin == VirtualViewportUpdateResult.Blocked;
                ShowStatus();
                return;
            }
            var focus = Controller.Mounted.Select(row => (row.Key, Focused: Owner.HasFocus(row.Value.Input))).ToArray();
            foreach (var row in focus) Controller.ObserveInteraction(row.Key, new(row.Focused, false));
            var prepared = Controller.PrepareViewport(request);
            if (prepared != VirtualizationUpdate.Applied)
            {
                Controller.CancelPreparedViewport(request.Epoch);
                lease.Cancel(request.Epoch);
                ShowStatus();
                return;
            }
            foreach (var row in Controller.Mounted)
                setItemInfo!(row.Value.Root, Controller.GetPreparedItemInfo(row.Key));
            if (lease.TryCommit(request.Epoch) != VirtualViewportCommitResult.Committed)
                throw new InvalidOperationException("A reserved viewport changed after row staging; the native attachment must be detached.");
            if (!Controller.CommitPreparedViewport(request.Epoch))
                throw new InvalidOperationException("The controller rejected an already committed native viewport.");
            viewportWaiting = false;
            Ready = true;
            ShowStatus();
            if (focusAfterCommit)
            {
                focusAfterCommit = false;
                FocusSelected();
            }
            lease.FlushCommitted(request.Epoch);
            viewportCommitted?.Invoke(request);
        }
        catch (Exception error)
        {
            try { Owner.Detach(); }
            catch (Exception cleanup) { throw new AggregateException(error, cleanup); }
            Ready = false;
            throw;
        }
    }

    public VirtualizationUpdate AcceptViewport(float offset, float height)
    {
        var result = Controller.SetViewport(offset, height);
        ShowStatus();
        return result;
    }

    public VirtualizationUpdate PrepareViewport(long epoch, float offset, float height)
    {
        var result = Controller.PrepareViewport(epoch, offset, height);
        ShowStatus();
        return result;
    }

    public VirtualizationUpdate PrepareViewport(VirtualViewportRequest request)
    {
        var result = Controller.PrepareViewport(request);
        ShowStatus();
        return result;
    }

    public bool CommitViewport(long epoch)
    {
        bool result = Controller.CommitPreparedViewport(epoch);
        ShowStatus();
        return result;
    }

    public VirtualizationUpdate AcceptInteraction(string key, bool focused, bool composing)
    {
        if (Controller.Mounted.TryGetValue(key, out var row))
        {
            if (!row.NativeSelectionReady && !focused && !composing && !Owner.HasFocus(row.Input))
            {
                Owner.SetSelection(row.Input, Data.Items[key].Selection);
                row.NativeSelectionReady = true;
            }
            if (focused || composing) row.NativeSelectionReady = true;
            SaveSelection(key, row);
        }
        if (focused) selectedKey = key;
        VirtualizationUpdate result;
        if (leased)
        {
            Controller.ObserveInteraction(key, new(focused, composing));
            result = Controller.IsDeferred ? VirtualizationUpdate.Deferred : VirtualizationUpdate.Applied;
        }
        else result = Controller.SetInteraction(key, focused, composing);
        ShowStatus();
        return result;
    }

    private void SaveSelection(string key, VirtualListRow row)
    {
        if (Owner.IsAttached && (row.NativeSelectionReady || Owner.HasFocus(row.Input)))
        {
            Data.Items[key].Selection = Owner.GetSelection(row.Input);
            row.NativeSelectionReady = true;
        }
    }

    public void CaptureEditingState()
    {
        Owner.VerifyComponentMutation(Root);
        foreach (var row in Controller.Mounted) SaveSelection(row.Key, row.Value);
    }

    private string[] Projection()
    {
        var keys = Data.Items.Values.Where(item => !item.Removed && (!filtered || item.Number % 2 == 0))
            .Select(item => item.Key);
        return (reverse ? keys.Reverse() : keys).ToArray();
    }

    private void Project()
    {
        if (leased)
        {
            long version = Controller.QueueKeys(Projection());
            viewportLease?.SetExtent(Controller.RequestedCount, version);
        }
        else Controller.SetKeys(Projection());
        ShowStatus();
    }

    private void ShowStatus()
    {
        Status = $"{Controller.Count:N0} items; {Controller.Mounted.Count} mounted; offset {Controller.Offset:0}" +
            (viewportWaiting ? "; native viewport deferred." :
                Controller.IsDeferred ? $"; pending {Controller.RequestedCount:N0} items." : ".");
    }

    public void Reveal(string key)
    {
        Owner.VerifyComponentMutation(Root);
        if (requestOffset is null) throw new InvalidOperationException("The native scroll request is not connected.");
        float target;
        if (leased)
        {
            if (!Controller.TryGetRevealOffset(key, Controller.RequestedSourceVersion, Controller.Offset,
                Controller.ViewportHeight, out target))
                throw new ArgumentException("The selected task is not in the requested source.", nameof(key));
        }
        else target = Controller.OffsetFor(key);
        selectedKey = key;
        focusAfterCommit = leased;
        navigationSourceVersion = Controller.RequestedSourceVersion;
        navigationViewportHeight = Controller.ViewportHeight;
        requestOffset(target);
    }

    public bool FocusSelected()
    {
        if (selectedKey is null || !Controller.Mounted.TryGetValue(selectedKey, out var row)) return false;
        if (!Owner.TryFocus(row.Input)) return false;
        Owner.SetSelection(row.Input, Data.Items[selectedKey].Selection);
        row.NativeSelectionReady = true;
        return true;
    }

    private void Move(int direction, bool endpoint)
    {
        var keys = Projection();
        if (keys.Length == 0) return;
        int index = selectedKey is null ? -1 : Array.IndexOf(keys, selectedKey);
        int target = endpoint ? (direction < 0 ? 0 : keys.Length - 1) :
            Math.Clamp(index + direction, 0, keys.Length - 1);
        Reveal(keys[target]);
    }

    private void NextFrom(string key)
    {
        if (!Controller.Mounted.TryGetValue(key, out var row)) return;
        if (!Owner.HasFocus(row.Input)) return;
        if (row.Input.Interaction is { IsComposing: true } || (leased && row.Input.Interaction is null)) return;
        SaveSelection(key, row);
        if (Controller.RequestedSourceVersion != Controller.SourceVersion && !Owner.TryFocus(NextButton))
            throw new InvalidOperationException("The next-task navigation control could not receive native focus.");
        var keys = Projection();
        if (keys.Length == 0) return;
        int index = Array.IndexOf(keys, key);
        Reveal(keys[Math.Clamp(index + 1, 0, keys.Length - 1)]);
    }

    private void First() => Move(-1, true);
    private void Previous() => Move(-1, false);
    private void Next() => Move(1, false);
    private void Last() => Move(1, true);
    private void Reverse() { reverse = !reverse; Project(); }
    private void Filter() { filtered = !filtered; Project(); }
    private void RemoveSelected()
    {
        if (selectedKey is not null) { Data.Items[selectedKey].Removed = true; Project(); }
    }

    private sealed class Retirement(VirtualList view) : IDisposable
    {
        public void Dispose()
        {
            view.retired = true;
            view.viewportCommitted = null;
            view.requestOffset = null;
            view.setItemInfo = null;
            view.viewportLease = null;
        }
    }
}
