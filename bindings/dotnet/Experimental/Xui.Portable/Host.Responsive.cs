namespace Xui.Experimental.Portable;

public sealed partial class Host
{
    /// <summary>Delivers the latest owned viewport allocation once per queued UI callback.</summary>
    /// <remarks>The returned subscription belongs to the current attachment and is retired before unmount.</remarks>
    public IDisposable ObserveViewport(Action<Size> changed) =>
        DeferInteractionDelivery(() => ObserveHostViewport(changed));

    private IDisposable ObserveHostViewport(Action<Size> changed)
    {
        VerifyMutation();
        ArgumentNullException.ThrowIfNull(changed);
        if (building is not null || reconciling != 0)
            throw new InvalidOperationException("Complete construction and keyed updates before observing the host viewport.");
        var current = attachment ?? throw new InvalidOperationException("Viewport observation requires an attached host.");
        if (current.Backend is not IHostViewportBackend backend)
            throw new NotSupportedException("Host viewport observation requires an IHostViewportBackend.");
        var observation = new HostViewportObservation(this, current, root!, changed);
        try
        {
            observation.Open(backend);
            current.Resources.Add(observation);
            observation.Queue();
            return observation;
        }
        catch (Exception error)
        {
            current.Resources.Remove(observation);
            Exception failure = error;
            try { observation.Retire(); }
            catch (Exception cleanup) { failure = new AggregateException(error, cleanup); }
            if (observation.ProtocolFailed) throw FailAttachment(current, failure);
            if (!ReferenceEquals(failure, error)) throw failure;
            throw;
        }
    }

    private sealed class HostViewportObservation(Host host, Attachment current, Element root, Action<Size> changed)
        : AttachmentResource(root), IDisposable
    {
        private IDisposable? native;
        private Action<Size>? callback = changed;
        private Size? latest;
        private Size? delivered;
        private bool opening;
        private bool queued;
        private bool posting;
        private long ticket;
        private Exception? protocolFailure;
        internal bool ProtocolFailed => protocolFailure is not null;
        private bool IsCurrent => !IsRetired && ReferenceEquals(host.attachment, current);

        internal void Open(IHostViewportBackend backend)
        {
            opening = true;
            try { native = host.InputOperation(() => backend.ObserveViewport(Receive)); }
            finally { opening = false; }
            if (native is null)
                protocolFailure ??= new InvalidOperationException("The backend returned a null viewport subscription.");
            if (latest is null)
                protocolFailure ??= new InvalidOperationException("The backend did not supply an initial viewport allocation.");
            if (protocolFailure is not null) throw protocolFailure;
        }

        private void Receive(Size size)
        {
            host.VerifyThread();
            if (!IsCurrent) return;
            try { Values.Size(size.Width, size.Height); }
            catch (ArgumentOutOfRangeException error)
            {
                protocolFailure = new InvalidOperationException("The backend reported an invalid viewport allocation.", error);
                if (!opening) throw host.FailAttachment(current, protocolFailure);
                return;
            }
            if (latest == size) return;
            latest = size;
            if (!opening)
            {
                try { Queue(); }
                catch (Exception error) { throw host.FailAttachment(current, error); }
            }
        }

        internal void Queue()
        {
            if (!IsCurrent || queued || latest == delivered) return;
            queued = true;
            long request = ++ticket;
            posting = true;
            try
            {
                if (host.dispatcher is ICancellableUiDispatcher cancellable)
                    cancellable.Post(() => Deliver(request), error => Canceled(request, error));
                else host.dispatcher.Post(() => Deliver(request));
            }
            catch (Exception error)
            {
                queued = false;
                protocolFailure ??= error;
                throw;
            }
            finally { posting = false; }
            if (protocolFailure is not null) throw protocolFailure;
        }

        private void Deliver(long request)
        {
            host.VerifyThread();
            if (!IsCurrent || !queued || request != ticket) return;
            if (posting)
            {
                protocolFailure = new InvalidOperationException("Viewport delivery requires an asynchronously queued UI dispatcher.");
                return;
            }
            queued = false;
            if (host.updating != 0 || host.transitioning || host.building is not null ||
                host.reconciling != 0 || host.viewportUpdates != 0)
                throw host.FailAttachment(current, new InvalidOperationException("The dispatcher delivered viewport work during a native or structural operation."));
            if (latest is not { } size || delivered == size) return;
            delivered = size;
            callback!(size);
        }

        private void Canceled(long request, Exception error)
        {
            host.VerifyThread();
            if (!IsCurrent || !queued || request != ticket) return;
            queued = false;
            protocolFailure = error;
            if (!posting) throw host.FailAttachment(current, error);
        }

        public void Dispose()
        {
            host.VerifyThread();
            if (IsRetired) return;
            host.VerifyMutation();
            current.Resources.Remove(this);
            try { host.InputOperation(() => { Retire(); return true; }); }
            catch (Exception error) { throw host.FailAttachment(current, error); }
        }

        protected override void DisposeCore()
        {
            callback = null;
            latest = null;
            delivered = null;
            queued = false;
            ticket++;
            var subscription = native;
            native = null;
            subscription?.Dispose();
        }
    }
}
