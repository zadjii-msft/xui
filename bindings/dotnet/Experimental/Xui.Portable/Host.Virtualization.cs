namespace Xui.Experimental.Portable;

public sealed partial class Host
{
    public IVirtualViewportLease BeginVirtualViewport(ScrollView scroll, int itemCount, float rowHeight,
        long sourceVersion, Action<VirtualViewportRequest> requested) =>
        DeferInteractionDelivery(() => BeginVirtualViewportCore(scroll, itemCount, rowHeight, sourceVersion, requested));

    private IVirtualViewportLease BeginVirtualViewportCore(ScrollView scroll, int itemCount, float rowHeight,
        long sourceVersion, Action<VirtualViewportRequest> requested)
    {
        ArgumentNullException.ThrowIfNull(scroll);
        ArgumentNullException.ThrowIfNull(requested);
        VerifyElementMutation(scroll);
        ValidateVirtualExtent(itemCount, rowHeight);
        ArgumentOutOfRangeException.ThrowIfNegativeOrZero(sourceVersion);
        var peer = InputPeer<IVirtualViewportPeer>(scroll);
        var current = attachment!;
        if (current.Resources.Any(resource => ReferenceEquals(resource.Owner, scroll) && resource is ViewportLease))
            throw new InvalidOperationException("The scroll view already owns a virtual viewport lease.");
        var lease = new ViewportLease(this, current, scroll, rowHeight, sourceVersion, requested);
        try
        {
            lease.Open(peer, itemCount);
            current.Resources.Add(lease);
            return lease.ExposeCapabilities();
        }
        catch (Exception error)
        {
            try { lease.Retire(); }
            catch (Exception cleanup) { throw new AggregateException(error, cleanup); }
            throw;
        }
    }

    private static void ValidateVirtualExtent(int itemCount, float rowHeight)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(itemCount);
        if (!float.IsFinite(rowHeight) || rowHeight <= 0 || (double)itemCount * rowHeight > float.MaxValue)
            throw new ArgumentOutOfRangeException(nameof(rowHeight), "The virtual extent must have a positive pitch and finite coordinates.");
    }

    public void SetVirtualItemInfo(Element rowRoot, VirtualItemInfo info)
    {
        VerifyComponent(rowRoot);
        VerifyElementMutation(rowRoot);
        info.Validate();
        var current = attachment ?? throw new InvalidOperationException("Virtual row metadata requires an attached host.");
        if (!componentRoots.Contains(rowRoot) || rowRoot.Parent is not global::Xui.Experimental.Portable.KeyedStack || !current.Peers.TryGetValue(rowRoot, out var peer))
            throw new InvalidOperationException("Virtual metadata requires a mounted keyed component root.");
        ViewportLease? lease = null;
        for (Element? ancestor = rowRoot.Parent; ancestor is not null && lease is null; ancestor = ancestor.Parent)
            lease = current.Resources.OfType<ViewportLease>().FirstOrDefault(resource => ReferenceEquals(resource.Owner, ancestor));
        if (lease is null) throw new InvalidOperationException("The row is not owned by an active virtual viewport.");
        lease.ValidateItem(info);
        if (peer is not IVirtualItemPeer itemPeer)
            throw new NotSupportedException("This backend does not provide native virtual-row metadata.");
        DeferInteractionDelivery(() =>
        {
            UpdateAttachment(current, () => itemPeer.SetVirtualItemInfo(info));
            return true;
        });
    }

    private sealed class SettledViewportLease(ViewportLease lease) : ISettledVirtualViewportLease
    {
        public void SetExtent(int itemCount, long sourceVersion) => lease.SetExtent(itemCount, sourceVersion);
        public void RequestOffset(float offset) => lease.RequestOffset(offset);
        public VirtualViewportUpdateResult TryBeginUpdate(long expectedEpoch) => lease.TryBeginUpdate(expectedEpoch);
        public VirtualViewportCommitResult TryCommit(long expectedEpoch) => lease.TryCommit(expectedEpoch);
        public void Cancel(long expectedEpoch) => lease.Cancel(expectedEpoch);
        public void FlushCommitted(long expectedEpoch) => lease.FlushCommitted(expectedEpoch);
        public void Dispose() => lease.Dispose();
    }

    private sealed class ViewportLease(Host host, Attachment current, ScrollView scroll, float rowHeight,
        long sourceVersion, Action<VirtualViewportRequest> requested) : AttachmentResource(scroll), IVirtualViewportLease
    {
        private IVirtualViewportLease? native;
        private Action<VirtualViewportRequest>? callback = requested;
        private long requestedSourceVersion = sourceVersion;
        private int requestedItemCount;
        private VirtualViewportRequest? latest;
        private long preparedEpoch;
        private long committedEpoch;
        private long preparedRevision;
        private long preparedSourceVersion;
        private int preparedItemCount;
        private bool operating;
        private Exception? protocolFailure;

        internal void Open(IVirtualViewportPeer peer, int itemCount)
        {
            requestedItemCount = itemCount;
            operating = true;
            try { native = host.InputOperation(() => peer.BeginVirtualViewport(itemCount, rowHeight, requestedSourceVersion, Receive)); }
            finally { operating = false; }
            if (native is null) throw new InvalidOperationException("The backend returned a null viewport lease.");
            if (protocolFailure is not null) throw protocolFailure;
        }

        internal IVirtualViewportLease ExposeCapabilities() =>
            native is ISettledVirtualViewportLease ? new SettledViewportLease(this) : this;

        private void VerifyLive()
        {
            host.VerifyThread();
            ObjectDisposedException.ThrowIf(IsRetired, this);
            host.VerifyElementMutation(scroll);
            if (!ReferenceEquals(host.attachment, current) || !current.Peers.ContainsKey(scroll))
                throw new InvalidOperationException("The viewport lease no longer belongs to the active attachment.");
            if (operating) throw new InvalidOperationException("A viewport lease operation cannot reenter itself.");
        }

        private T Invoke<T>(Func<T> operation)
        {
            operating = true;
            try
            {
                var result = host.InputOperation(operation);
                if (protocolFailure is not null) throw protocolFailure;
                return result;
            }
            catch (Exception error) { throw host.FailAttachment(current, error); }
            finally { operating = false; }
        }

        public void SetExtent(int itemCount, long sourceVersion) =>
            host.DeferInteractionDelivery(() => { SetExtentCore(itemCount, sourceVersion); return true; });
        private void SetExtentCore(int itemCount, long sourceVersion)
        {
            VerifyLive();
            ValidateVirtualExtent(itemCount, rowHeight);
            if (sourceVersion <= requestedSourceVersion)
                throw new ArgumentOutOfRangeException(nameof(sourceVersion), "A requested source version must strictly advance.");
            Invoke(() => { native!.SetExtent(itemCount, sourceVersion); return true; });
            requestedSourceVersion = sourceVersion;
            requestedItemCount = itemCount;
        }

        public void RequestOffset(float offset) =>
            host.DeferInteractionDelivery(() => { RequestOffsetCore(offset); return true; });
        private void RequestOffsetCore(float offset)
        {
            VerifyLive();
            Values.Length(offset);
            Invoke(() => { native!.RequestOffset(offset); return true; });
        }

        public VirtualViewportUpdateResult TryBeginUpdate(long expectedEpoch) =>
            host.DeferInteractionDelivery(() => BeginUpdateCore(expectedEpoch));
        private VirtualViewportUpdateResult BeginUpdateCore(long expectedEpoch)
        {
            VerifyLive();
            ArgumentOutOfRangeException.ThrowIfNegativeOrZero(expectedEpoch);
            if (host.viewportUpdates != 0) throw new InvalidOperationException("Complete the current viewport update before starting another.");
            if (latest is not { } request || expectedEpoch > request.Epoch)
                throw new InvalidOperationException("Prepare a viewport epoch only after its request is delivered.");
            if (expectedEpoch < request.Epoch) return VirtualViewportUpdateResult.Superseded;
            if (request.RequestedSourceVersion != requestedSourceVersion) return VirtualViewportUpdateResult.Superseded;
            var result = Invoke(() => native!.TryBeginUpdate(expectedEpoch));
            if (!Enum.IsDefined(result)) throw host.FailAttachment(current, new InvalidOperationException("The backend returned an unknown viewport update result."));
            if (result == VirtualViewportUpdateResult.Ready)
            {
                if (request.IsBlocked) throw host.FailAttachment(current, new InvalidOperationException("A blocked request cannot become ready without a new epoch."));
                preparedEpoch = expectedEpoch;
                preparedRevision = host.mutationRevision;
                preparedSourceVersion = request.RequestedSourceVersion;
                preparedItemCount = requestedItemCount;
                host.viewportUpdates++;
            }
            return result;
        }

        public VirtualViewportCommitResult TryCommit(long expectedEpoch) =>
            host.DeferInteractionDelivery(() => CommitCore(expectedEpoch));
        private VirtualViewportCommitResult CommitCore(long expectedEpoch)
        {
            VerifyLive();
            ArgumentOutOfRangeException.ThrowIfNegativeOrZero(expectedEpoch);
            if (preparedEpoch != expectedEpoch) throw new InvalidOperationException("Commit requires the reserved viewport epoch.");
            var result = Invoke(() => native!.TryCommit(expectedEpoch));
            if (result != VirtualViewportCommitResult.Committed)
                throw host.FailAttachment(current, new InvalidOperationException("A reserved viewport update must commit or fail terminally; it cannot become stale or blocked after staging."));
            committedEpoch = expectedEpoch;
            EndUpdate();
            return result;
        }

        internal void FlushCommitted(long expectedEpoch) =>
            host.DeferInteractionDelivery(() => { FlushCommittedCore(expectedEpoch); return true; });
        private void FlushCommittedCore(long expectedEpoch)
        {
            VerifyLive();
            ArgumentOutOfRangeException.ThrowIfNegativeOrZero(expectedEpoch);
            if (host.viewportUpdates != 0)
                throw new InvalidOperationException("Finish viewport staging before flushing committed geometry.");
            if (expectedEpoch != committedEpoch)
                throw new InvalidOperationException("Flush must identify the current committed viewport epoch.");
            if (native is not ISettledVirtualViewportLease settled)
                throw new NotSupportedException("This native viewport lease does not support committed geometry settling.");
            Invoke(() => { settled.FlushCommitted(expectedEpoch); return true; });
        }

        public void Cancel(long expectedEpoch) =>
            host.DeferInteractionDelivery(() => { CancelCore(expectedEpoch); return true; });
        private void CancelCore(long expectedEpoch)
        {
            VerifyLive();
            ArgumentOutOfRangeException.ThrowIfNegativeOrZero(expectedEpoch);
            if (preparedEpoch != 0 && preparedEpoch != expectedEpoch)
                throw new InvalidOperationException("Cancel must identify the reserved viewport epoch.");
            if (preparedEpoch != 0 && preparedRevision != host.mutationRevision)
                throw host.FailAttachment(current, new InvalidOperationException("A viewport update cannot cancel after retained model mutation; detach instead of claiming rollback."));
            Invoke(() => { native!.Cancel(expectedEpoch); return true; });
            EndUpdate();
        }

        private void Receive(VirtualViewportRequest request)
        {
            host.VerifyThread();
            if (IsRetired || host.disposed || !ReferenceEquals(host.attachment, current) || scroll.Disposed || !current.Peers.ContainsKey(scroll))
                return;
            if (operating || host.transitioning || host.updating != 0 || host.viewportUpdates != 0)
            {
                protocolFailure ??= new InvalidOperationException("Native viewport requests must be posted outside backend operations and synchronous staging.");
                if (!operating) throw protocolFailure;
                return;
            }
            try
            {
                request.Validate();
                if (request.RequestedSourceVersion > requestedSourceVersion)
                    throw new InvalidOperationException("The backend requested an undeclared source version.");
            }
            catch (Exception error) { throw host.FailAttachment(current, error); }
            if (latest is { } previous && request.Epoch <= previous.Epoch) return;
            latest = request;
            host.DeferInteractionDelivery(() => { DeliverRequest(request); return true; });
        }

        private void DeliverRequest(VirtualViewportRequest request)
        {
            try
            {
                callback!(request);
                if (preparedEpoch != 0)
                    throw new InvalidOperationException("Finish synchronous viewport staging before returning from its request callback.");
            }
            catch (Exception error)
            {
                if (preparedEpoch != 0)
                {
                    if (preparedRevision != host.mutationRevision) throw host.FailAttachment(current, error);
                    try { Cancel(preparedEpoch); }
                    catch (Exception cleanup) { throw new AggregateException(error, cleanup); }
                }
                throw;
            }
        }

        private void EndUpdate()
        {
            if (preparedEpoch == 0) return;
            preparedEpoch = 0;
            preparedSourceVersion = 0;
            preparedItemCount = 0;
            host.viewportUpdates--;
        }

        internal void ValidateItem(VirtualItemInfo info)
        {
            VerifyLive();
            if (preparedEpoch == 0 || info.SourceVersion != preparedSourceVersion || info.Count != preparedItemCount)
                throw new InvalidOperationException("Virtual row metadata must match the reserved source version and authored count.");
        }

        public void Dispose() =>
            host.DeferInteractionDelivery(() => { DisposeLease(); return true; });
        private void DisposeLease()
        {
            host.VerifyThread();
            if (IsRetired) return;
            if (operating) throw new InvalidOperationException("A viewport operation cannot dispose its own lease.");
            if (!host.transitioning && preparedEpoch != 0 && preparedRevision != host.mutationRevision)
                throw host.FailAttachment(current, new InvalidOperationException("Disposing a staged viewport requires terminal detachment."));
            current.Resources.Remove(this);
            try { Retire(); }
            catch (Exception error) { throw host.FailAttachment(current, error); }
        }

        protected override void DisposeCore()
        {
            EndUpdate();
            callback = null;
            var resource = native;
            native = null;
            if (resource is not null) host.InputOperation(() => { resource.Dispose(); return true; });
        }
    }
}
