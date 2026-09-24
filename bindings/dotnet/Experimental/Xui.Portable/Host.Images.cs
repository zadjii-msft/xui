namespace Xui.Experimental.Portable;

public sealed partial class Host
{
    public Image Image(string name) => Create(() => new Image(this, name));
    internal T ImageOperation<T>(Func<T> operation) => DeferInteractionDelivery(operation);

    internal void ValidateImageSupport(Image image, PackagedImageSource? source, ImageDecodeOptions options)
    {
        if (attachment is { } current && current.Peers.TryGetValue(image, out var peer))
        {
            if (peer is not IImageElementPeer native)
                throw new NotSupportedException("Packaged images require an IImageElementPeer backend.");
            InputOperation(() => { native.ValidateImage(source, options); return true; });
        }
    }

    internal void QueueImageState(Image image)
    {
        if (attachment is not { } current || !current.Peers.ContainsKey(image)) return;
        if (current.PublishedImageStates.TryGetValue(image, out var published) &&
            published == (image.SourceGeneration, image.State)) return;
        current.ImageStates[image] = image.SourceGeneration;
        if (current.ImageStateQueued) return;
        current.ImageStateQueued = true;
        bool posting = true;
        bool inline = false;
        void Run()
        {
            VerifyThread();
            if (posting) { inline = true; return; }
            current.ImageStateQueued = false;
            if (disposed || !ReferenceEquals(attachment, current)) return;
            if (building is not null || reconciling != 0 || transitioning || updating != 0 || viewportUpdates != 0)
                throw FailAttachment(current, new InvalidOperationException("Image notifications must be queued outside construction, reconciliation, native operations, and viewport staging."));
            var pending = current.ImageStates.ToArray();
            current.ImageStates.Clear();
            var failures = new List<Exception>();
            foreach (var item in pending)
            {
                if (!ReferenceEquals(attachment, current) || item.Key.Disposed || !current.Peers.ContainsKey(item.Key) ||
                    item.Key.SourceGeneration != item.Value) continue;
                current.PublishedImageStates[item.Key] = (item.Value, item.Key.State);
                try { item.Key.NotifyState(); }
                catch (Exception error) { failures.Add(error); }
            }
            if (failures.Count != 0) throw new AggregateException("Image state notification failed.", failures);
        }
        void Canceled(Exception error)
        {
            VerifyThread();
            current.ImageStateQueued = false;
            if (disposed || !ReferenceEquals(attachment, current)) return;
            throw FailAttachment(current, error);
        }
        try
        {
            if (dispatcher is ICancellableUiDispatcher cancellable)
                cancellable.Post(Run, Canceled);
            else dispatcher.Post(Run);
            if (inline) throw new InvalidOperationException("Image state delivery requires queued UI dispatch, not inline execution.");
        }
        catch (Exception error)
        {
            current.ImageStateQueued = false;
            throw FailAttachment(current, error);
        }
        finally { posting = false; }
    }

    private static void CancelAttachmentImages(Attachment current, ISet<Element>? removed, List<Exception> failures)
    {
        foreach (var element in current.Order)
        {
            if (removed is not null && !removed.Contains(element)) continue;
            if (element is not Image image) continue;
            current.ImageStates.Remove(image);
            current.PublishedImageStates.Remove(image);
            image.EndAttachment();
            if (current.Peers[element] is IImageElementPeer peer)
            {
                try { peer.CancelImage(); }
                catch (Exception error) { failures.Add(error); }
            }
        }
    }
}
