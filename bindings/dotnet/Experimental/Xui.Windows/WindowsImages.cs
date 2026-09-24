using P = Xui.Experimental.Portable;

namespace Xui.Experimental.Windows;

internal sealed class WindowsImagePeer : WindowsConstrainedPeer, P.IImageElementPeer
{
    private readonly P.IImagePresentationEvents events;
    private readonly P.ImageRequestLifetime lifetime;
    private readonly WindowsImageResources resources = new();
    private bool canceled;
    private bool nativeRetired;

    internal WindowsImagePeer(WindowsBackend backend, P.Element element, P.IControlEvents events)
        : base(backend, element, events)
    {
        this.events = events as P.IImagePresentationEvents ??
            throw new ArgumentException("Native Image requires generation-aware presentation failure delivery.", nameof(events));
        lifetime = new P.ImageRequestLifetime(backend.Dispatcher, backend.ReportImageCleanup);
        StartRequest();
    }

    public void ValidateImage(P.PackagedImageSource? source, P.ImageDecodeOptions options)
    {
        Backend.VerifyAccess();
        ObjectDisposedException.ThrowIf(Disposed || canceled, this);
        ArgumentNullException.ThrowIfNull(options);
        if (!Backend.SupportsMemoryImages)
            throw new NotSupportedException("The native memory Image contract is unavailable.");
        if (source is not null) source.Manifest.Get(source.AssetId);
    }

    public override void Update(P.ElementProperty property)
    {
        if (property == P.ElementProperty.ImageRequest) StartRequest();
        else base.Update(property);
    }

    private void StartRequest()
    {
        Backend.VerifyAccess();
        ObjectDisposedException.ThrowIf(Disposed || canceled, this);
        var model = (P.Image)Element;
        var source = model.Source;
        var options = model.DecodeOptions;
        long generation = model.SourceGeneration;
        ValidateImage(source, options);
        var request = lifetime.Begin();
        ((Image)Native).CancelMemorySource();
        if (source is null) return;
        _ = AcquireAsync(Backend.Images, source, options, generation, request);
    }

    private async Task AcquireAsync(P.ImageResourceCache cache, P.PackagedImageSource source, P.ImageDecodeOptions options, long generation,
        P.ImageRequest request)
    {
        P.EncodedImageLease? encoded = null;
        try
        {
            encoded = await Task.Run(() => cache.AcquireAsync(source, options, request.Token), request.Token)
                .ConfigureAwait(false);
            var delivered = encoded;
            Backend.PostNotification(this, () =>
            {
                using (delivered)
                {
                    if (!Current(request, generation)) return;
                    StartNative(delivered, options, generation, request);
                }
            }, delivered.Dispose);
            encoded = null;
        }
        catch (OperationCanceledException) when (request.Token.IsCancellationRequested) { }
        catch (Exception error)
        {
            PostLoadError(generation, request, error);
        }
        finally { encoded?.Dispose(); }
    }

    private bool Current(P.ImageRequest request, long generation) =>
        !canceled && !Disposed && Backend.Mounted && request.IsCurrent && ((P.Image)Element).SourceGeneration == generation;

    private void StartNative(P.EncodedImageLease encoded, P.ImageDecodeOptions options, long generation, P.ImageRequest request)
    {
        var resource = resources.Create();
        try
        {
            var plan = encoded.Plan;
            plan.ValidateOptions(options);
            resource.Pixels = Backend.ImagePixels.Reserve(plan);
            if (!request.TryOwn(resource)) return;
            var nativeOptions = new MemoryImageOptions(generation, plan.Format switch
            {
                P.PackagedImageFormat.Png => MemoryImageFormat.Png,
                P.PackagedImageFormat.Jpeg => MemoryImageFormat.Jpeg,
                _ => throw new NotSupportedException("Unsupported native packaged Image codec.")
            }, plan.SourceWidth, plan.SourceHeight, plan.OutputWidth, plan.OutputHeight,
                options.OutputWidth, options.OutputHeight);
            resource.Native = ((Image)Native).SetMemorySource(nativeOptions, encoded.Bytes.Span, notification =>
            {
                // Ready and presentation failure are ordered terminal notices, not coalescible metadata.
                Backend.PostNotification(this, () =>
                {
                    if (!Current(request, generation) || notification.State.Generation != generation) return;
                    var state = notification.State;
                    if (state.Status == MemoryImageStatus.Ready)
                    {
                        try
                        {
                            plan.ValidateCodecSource(state.SourceWidth, state.SourceHeight);
                            plan.ValidateDecodedOutput(state.PixelWidth, state.PixelHeight);
                        }
                        catch (Exception invalidPresentation)
                        {
                            events.ImagePresentationFailed(generation, invalidPresentation);
                            return;
                        }
                        events.ImageCompleted(generation, plan, state.PixelWidth, state.PixelHeight);
                    }
                    else if (state.Status == MemoryImageStatus.Error)
                    {
                        resource.ReleasePixels();
                        events.ImageFailed(generation, notification.Error!);
                    }
                    else if (state.Status == MemoryImageStatus.PresentationFailed)
                        events.ImagePresentationFailed(generation, new InvalidOperationException(notification.Error));
                }, () => { });
            });
        }
        catch (Exception error)
        {
            Exception failure = error;
            try { resource.Dispose(); }
            catch (Exception cleanup) { failure = new AggregateException(error, cleanup); }
            if (Current(request, generation)) events.ImageFailed(generation, Message(failure));
            else Backend.ReportImageCleanup(failure);
        }
    }

    private void PostLoadError(long generation, P.ImageRequest request, Exception error)
    {
        try
        {
            Backend.PostNotification(this, () =>
            {
                if (Current(request, generation)) events.ImageFailed(generation, Message(error));
            }, _canceled);
        }
        catch (ObjectDisposedException) when (request.Token.IsCancellationRequested) { }
        catch (Exception dispatchError) { Backend.ReportImageCleanup(new AggregateException(error, dispatchError)); }

        void _canceled() { if (!request.Token.IsCancellationRequested) Backend.ReportImageCleanup(error); }
    }

    private static string Message(Exception error)
    {
        string message = error.Message.Replace('\0', ' ');
        if (string.IsNullOrWhiteSpace(message)) message = "Native Image loading failed.";
        int length = Math.Min(message.Length, 4096);
        if (length < message.Length && length > 0 && char.IsHighSurrogate(message[length - 1])) length--;
        return message[..length];
    }

    public void CancelImage()
    {
        Backend.VerifyAccess();
        if (nativeRetired || Disposed) return;
        canceled = true;
        Exception? failure = null;
        try { lifetime.Dispose(); }
        catch (Exception error) { failure = error; }
        try
        {
            ((Image)Native).CancelMemorySource();
            CompleteNativeRetirement();
        }
        catch (Exception error) { failure = failure is null ? error : new AggregateException(failure, error); }
        if (failure is not null) System.Runtime.ExceptionServices.ExceptionDispatchInfo.Capture(failure).Throw();
    }

    internal void CompleteNativeRetirement()
    {
        Backend.VerifyAccess();
        nativeRetired = true;
        resources.CompleteNativeRetirement();
    }

    public override void Dispose()
    {
        Backend.VerifyAccess();
        if (Disposed) return;
        CancelImage();
        base.Dispose();
    }

}

/// <summary>Keeps failed retirements owned after ImageRequestLifetime drops its reference.</summary>
internal sealed class WindowsImageResources
{
    private readonly HashSet<Resource> owned = [];
    internal int Count => owned.Count;
    internal Resource Create()
    {
        var resource = new Resource(this);
        owned.Add(resource);
        return resource;
    }

    // Call only after Image cancellation or the containing native arena has acknowledged retirement.
    internal void CompleteNativeRetirement()
    {
        foreach (var resource in owned.ToArray()) resource.CompleteNativeRetirement();
    }

    internal sealed class Resource(WindowsImageResources owner) : IDisposable
    {
        internal P.ImagePixelReservation? Pixels;
        internal IDisposable? Native;
        internal void ReleasePixels() { Pixels?.Dispose(); Pixels = null; }
        public void Dispose()
        {
            Native?.Dispose();
            CompleteNativeRetirement();
        }
        internal void CompleteNativeRetirement()
        {
            Native = null;
            ReleasePixels();
            owner.owned.Remove(this);
        }
    }
}
