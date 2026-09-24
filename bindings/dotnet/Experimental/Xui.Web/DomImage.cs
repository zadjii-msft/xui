using System.Globalization;
using Microsoft.JSInterop;
using Xui.Experimental.Portable;
using PortableImage = Xui.Experimental.Portable.Image;

namespace Xui.Experimental.Web;

internal sealed class DomImage : IDisposable
{
    private readonly DomBackend owner;
    private readonly int id;
    private readonly PortableImage image;
    private readonly IImageControlEvents events;
    private readonly ImageRequestLifetime lifetime;
    private readonly Action<Exception> report;
    private bool disposed;

    internal DomImage(DomBackend owner, int id, PortableImage image, IImageControlEvents events)
    {
        this.owner = owner;
        this.id = id;
        this.image = image;
        this.events = events;
        report = owner.ReportImageFailure;
        lifetime = new ImageRequestLifetime(new BrowserDispatcher(report), report);
    }
    internal void Start()
    {
        var request = lifetime.Begin();
        long generation = image.SourceGeneration;
        owner.Surface.InvokeVoid("cancelImage", id);
        if (image.Source is not { } source) return;
        _ = Load(request, source, image.DecodeOptions, generation);
    }
    private async Task Load(ImageRequest request, PackagedImageSource source, ImageDecodeOptions options, long generation)
    {
        IJSInProcessObjectReference? native = null;
        ImagePixelReservation? canvas = null, displayed = null;
        try
        {
            await Task.Yield();
            request.Token.ThrowIfCancellationRequested();
            using var encoded = await owner.ImageCache.AcquireAsync(source, options, request.Token);
            request.Token.ThrowIfCancellationRequested();
            using var admission = owner.AdmitImageDecode(encoded.Plan);
            canvas = owner.OutputPixels.Reserve(encoded.Plan);
            displayed = owner.OutputPixels.Reserve(encoded.Plan);
            native = await owner.Surface.InvokeAsync<IJSInProcessObjectReference>("decodeImage", id,
                generation.ToString(CultureInfo.InvariantCulture), encoded.Bytes.ToArray(), new
                {
                    contentType = encoded.Plan.ContentType,
                    sourceWidth = encoded.Plan.SourceWidth, sourceHeight = encoded.Plan.SourceHeight,
                    width = encoded.Plan.OutputWidth, height = encoded.Plan.OutputHeight
                });
            canvas.Dispose();
            canvas = null;
            var result = native.Invoke<DecodeResult>("result");
            if (result.Canceled || !request.IsCurrent) return;
            if (result.Error is not null) throw new InvalidDataException(result.Error);
            encoded.Plan.ValidateCodecSource(result.SourceWidth, result.SourceHeight);
            encoded.Plan.ValidateDecodedOutput(result.Width, result.Height);
            var lease = new NativeImageLease(native, displayed);
            native = null;
            displayed = null;
            if (request.TryOwn(lease) && !events.ImageCompleted(generation, encoded.Plan, result.Width, result.Height))
                lease.Dispose();
        }
        catch (OperationCanceledException) when (request.Token.IsCancellationRequested) { }
        catch (Exception error)
        {
            if (request.IsCurrent)
            {
                string message = error.Message;
                if (message.Length > 4096) message = message[..4096];
                try { events.ImageFailed(generation, message); }
                catch (Exception callbackError) { report(new AggregateException(error, callbackError)); }
            }
            else report(error);
        }
        finally
        {
            List<Exception> failures = [];
            if (native is not null)
            {
                try { native.InvokeVoid("dispose"); } catch (Exception error) { failures.Add(error); }
                try { native.Dispose(); } catch (Exception error) { failures.Add(error); }
            }
            try { canvas?.Dispose(); } catch (Exception error) { failures.Add(error); }
            try { displayed?.Dispose(); } catch (Exception error) { failures.Add(error); }
            if (failures.Count != 0) report(new AggregateException("Image resource cleanup failed.", failures));
        }
    }
    internal void Cancel()
    {
        if (disposed) return;
        lifetime.Begin();
        owner.Surface.InvokeVoid("cancelImage", id);
    }
    public void Dispose()
    {
        if (disposed) return;
        disposed = true;
        lifetime.Dispose();
    }
    public sealed class DecodeResult
    {
        public bool Canceled { get; set; }
        public string? Error { get; set; }
        public int SourceWidth { get; set; }
        public int SourceHeight { get; set; }
        public int Width { get; set; }
        public int Height { get; set; }
    }
    private sealed class NativeImageLease(IJSInProcessObjectReference native, ImagePixelReservation pixels) : IDisposable
    {
        private bool disposed;
        public void Dispose()
        {
            if (disposed) return;
            disposed = true;
            try { native.InvokeVoid("dispose"); }
            finally { try { native.Dispose(); } finally { pixels.Dispose(); } }
        }
    }
}
