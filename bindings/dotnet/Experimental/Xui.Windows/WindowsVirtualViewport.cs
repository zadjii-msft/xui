using Xui.Experimental.Portable;
using P = Xui.Experimental.Portable;

namespace Xui.Experimental.Windows;

internal sealed class WindowsVirtualItemPeer(WindowsBackend backend, P.Element element, IControlEvents events)
    : WindowsConstrainedMutablePeer(backend, element, events), IVirtualItemPeer
{
    public void SetVirtualItemInfo(P.VirtualItemInfo info) => Backend.SetVirtualItemInfo(this, info);
}

internal sealed class WindowsViewportPeer(WindowsBackend backend, P.Element element, IControlEvents events)
    : WindowsConstrainedPeer(backend, element, events), IVirtualViewportPeer
{
    private WindowsViewportLease? viewport;

    public IVirtualViewportLease BeginVirtualViewport(int itemCount, float rowHeight, long sourceVersion,
        Action<P.VirtualViewportRequest> requested)
    {
        Backend.VerifyAccess();
        ObjectDisposedException.ThrowIf(Disposed, this);
        if (!Backend.Mounted) throw new InvalidOperationException("Virtual viewports require a mounted Windows attachment.");
        for (P.Element? ancestor = Element.Parent; ancestor is not null; ancestor = ancestor.Parent)
            if (ancestor is P.Reveal)
                throw new NotSupportedException("Leased virtual viewports cannot be hosted inside this native Reveal.");
        if (viewport is { IsDisposed: false }) throw new InvalidOperationException("This scroll peer already owns a viewport lease.");
        return viewport = WindowsViewportLease.Create(this, itemCount, rowHeight, sourceVersion, requested);
    }

    public override void Dispose()
    {
        Backend.VerifyAccess();
        if (Disposed) return;
        viewport?.Dispose();
        viewport = null;
        base.Dispose();
    }
}

internal sealed class WindowsViewportLease : ISettledVirtualViewportLease
{
    private readonly WindowsViewportPeer peer;
    private Xui.VirtualViewportLease? native;
    private Action<P.VirtualViewportRequest>? requested;
    private P.VirtualViewportRequest? pending;
    private long deliveredEpoch;
    private bool scheduled;
    internal bool IsDisposed { get; private set; }
    internal P.Element Owner { get; }

    private WindowsViewportLease(WindowsViewportPeer peer, Action<P.VirtualViewportRequest> requested)
    {
        this.peer = peer;
        Owner = peer.Element;
        this.requested = requested;
    }

    internal static WindowsViewportLease Create(WindowsViewportPeer peer, int itemCount, float rowHeight, long sourceVersion,
        Action<P.VirtualViewportRequest> requested)
    {
        ArgumentNullException.ThrowIfNull(requested);
        var lease = new WindowsViewportLease(peer, requested);
        try
        {
            lease.native = ((ScrollView)peer.Native).BeginVirtualViewport(itemCount, rowHeight, sourceVersion, lease.OnRequested);
            peer.Backend.TrackViewport(lease);
            return lease;
        }
        catch (Exception error)
        {
            try { lease.Dispose(); }
            catch (Exception cleanup) { throw new AggregateException(error, cleanup); }
            throw;
        }
    }

    private Xui.VirtualViewportLease Native
    {
        get
        {
            peer.Backend.VerifyAccess();
            ObjectDisposedException.ThrowIf(IsDisposed || peer.Disposed, this);
            if (!peer.Backend.Mounted) throw new InvalidOperationException("The Windows viewport attachment is no longer active.");
            return native ?? throw new InvalidOperationException("The native viewport lease has not been initialized.");
        }
    }

    public void SetExtent(int itemCount, long sourceVersion) => Native.SetExtent(itemCount, sourceVersion);
    public void RequestOffset(float offset) => Native.RequestOffset(offset);
    internal void SetItemInfo(WindowsPeer row, P.VirtualItemInfo info) =>
        Native.SetItemInfo((Stack)row.Native, info.Key, info.Index, info.Count, info.SourceVersion);
    public P.VirtualViewportUpdateResult TryBeginUpdate(long expectedEpoch) => Native.TryBeginUpdate(expectedEpoch) switch
    {
        Xui.VirtualViewportUpdateResult.Ready => P.VirtualViewportUpdateResult.Ready,
        Xui.VirtualViewportUpdateResult.Superseded => P.VirtualViewportUpdateResult.Superseded,
        Xui.VirtualViewportUpdateResult.Blocked => P.VirtualViewportUpdateResult.Blocked,
        _ => throw new InvalidOperationException("The Windows runtime returned an unknown viewport preparation result.")
    };
    public P.VirtualViewportCommitResult TryCommit(long expectedEpoch) => Native.TryCommit(expectedEpoch) switch
    {
        Xui.VirtualViewportCommitResult.Committed => P.VirtualViewportCommitResult.Committed,
        Xui.VirtualViewportCommitResult.Superseded => P.VirtualViewportCommitResult.Superseded,
        Xui.VirtualViewportCommitResult.Blocked => P.VirtualViewportCommitResult.Blocked,
        _ => throw new InvalidOperationException("The Windows runtime returned an unknown viewport publication result.")
    };
    public void Cancel(long expectedEpoch) => Native.Cancel(expectedEpoch);
    public void FlushCommitted(long expectedEpoch) => Native.FlushCommitted(expectedEpoch);

    private void OnRequested(Xui.VirtualViewportRequest request)
    {
        if (IsDisposed || peer.Disposed || !peer.Backend.Mounted || request.Epoch <= deliveredEpoch) return;
        static P.VirtualViewportRect Rect(Xui.VirtualViewportRect value) => new(value.Offset, value.Width, value.Height, value.Extent);
        var value = new P.VirtualViewportRequest(request.Epoch, request.CommittedSourceVersion, request.RequestedSourceVersion,
            Rect(request.Committed), Rect(request.Requested), request.IsBlocked);
        value.Validate();
        if (pending is { } previous && previous.Epoch > value.Epoch) return;
        pending = value;
        if (scheduled) return;
        scheduled = true;
        try
        {
            peer.Backend.PostNotification(peer, () =>
            {
                var latest = pending;
                pending = null;
                scheduled = false;
                if (IsDisposed || latest is not { } snapshot || snapshot.Epoch <= deliveredEpoch) return;
                deliveredEpoch = snapshot.Epoch;
                requested?.Invoke(snapshot);
            }, () => { pending = null; scheduled = false; });
        }
        catch
        {
            pending = null;
            scheduled = false;
            throw;
        }
    }

    public void Dispose()
    {
        peer.Backend.VerifyAccess();
        if (IsDisposed) return;
        requested = null;
        pending = null;
        native?.Dispose();
        native = null;
        IsDisposed = true;
        peer.Backend.ForgetViewport(this);
    }
}
