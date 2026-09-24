using PortableDemo;
using Xui;
using Xui.Experimental.Windows;
using P = Xui.Experimental.Portable;

internal static partial class Program
{
    private static void NativeImageScenarios()
    {
        Check(Image.SupportsMemorySource, "The qualified memory Image runtime is required.");
        using var application = new Application();
        using var window = application.CreateWindow("Portable native scenarios", 620, 760, visualStyle: VisualStyle.WinUI);
        var surface = Surface(window);
        uint baseline = HandleCount(window);
        using var dispatcher = new WindowsDispatcher(window);
        using var host = new P.Host(dispatcher);
        var assets = new P.PackagedAssetManifest(typeof(Program).Assembly);
        var sample = new ImageWorkbench(host, assets);
        var backend = new WindowsBackend(surface, dispatcher);
        host.Attach(backend);
        application.Show(window);
        var ui = new Driver(application, window, host, () => backend);
        var work = Task.Run(() =>
        {
            try
            {
                ui.Wait(() => sample.Preview.State == P.ImageLoadState.Empty);
                ui.Change("image-notes", "Retained native Image draft");
                nint editor = ui.Ui(() =>
                {
                    Check(host.TryFocus(sample.Notes), "Image fixture notes did not receive focus.");
                    host.SetSelection(sample.Notes, new(2, 6));
                    return GetFocus();
                });
                var image = ui.Ui(() => (Image)backend.FindControls("packaged-image").Single());
                var initialBounds = ui.Ui(image.GetBounds);
                ui.Click("image-load");
                ui.Wait(() => sample.Preview.State is P.ImageLoadState.Ready or P.ImageLoadState.Error,
                    () => $"Image did not settle: model={sample.Preview.State}, native={image.MemoryState}, errors={sample.Preview.ErrorMessage}.");
                ui.Ui(() =>
                {
                    Check(sample.Preview.State == P.ImageLoadState.Ready && image.MemoryState.Status == MemoryImageStatus.Ready &&
                        image.MemoryState.PixelWidth == 64 && image.MemoryState.PixelHeight == 64,
                        $"The real packaged image failed native decode/presentation: {sample.Preview.ErrorMessage}.");
                    Check(image.GetBounds() == initialBounds, "Loading changed the authored Image viewport dimensions.");
                    using var input = typeof(Program).Assembly.GetManifestResourceStream("Xui.Asset.zoey.png")!;
                    using var encoded = new MemoryStream();
                    input.CopyTo(encoded);
                    var before = image.MemoryState;
                    Throws<XuiException>(() => image.SetMemorySource(new(before.Generation + 1, MemoryImageFormat.Png,
                        before.SourceWidth, before.SourceHeight, 1, 1, 64, 64), encoded.ToArray(), _ => { }));
                    Check(image.MemoryState == before && NativeObserverCount(window, "memoryImages") == 1,
                        "A rejected raw Image decode plan canceled the existing native request or callback owner.");
                });
                ui.Click("image-smaller");
                ui.Wait(() => sample.Preview.State == P.ImageLoadState.Ready && sample.Preview.PixelWidth == 32);
                ui.Ui(() =>
                {
                    Check(image.MemoryState.PixelWidth == 32 && image.GetBounds() == initialBounds,
                        "A smaller physical decode hint changed logical layout or reported stale native pixels.");
                    Check(host.TryFocus(sample.Notes) && GetFocus() == editor &&
                        host.GetSelection(sample.Notes) == new P.TextSelection(2, 6) &&
                        sample.Draft == "Retained native Image draft",
                        "Image decoding replaced or rewrote the neighboring native editor.");
                    P.ElementExtensions.FixedSize(sample.Preview, 192, 144);
                    sample.Preview.Visible = false;
                    sample.Preview.SetImage(new(assets, "pixels.png"), new(64, 64));
                });
                ui.Wait(() => sample.Preview.State == P.ImageLoadState.Ready);
                ui.Ui(() =>
                {
                    Check(image.MemoryState is { Status: MemoryImageStatus.Ready, SourceWidth: 3, SourceHeight: 2, PixelWidth: 3, PixelHeight: 2 },
                        "A hidden image failed to become ready independently of its visibility.");
                    sample.Preview.Visible = true;
                });
                ui.Wait(() => image.GetBounds().Height > 0);
                ui.Ui(() =>
                {
                    var frame = WindowCapture.Read(GetAncestor(editor, 2));
                    var bounds = image.GetBounds();
                    float scale = GetDpiForWindow(editor) / 96f;
                    int left = Math.Clamp((int)(bounds.X * scale), 0, frame.Width - 1);
                    int top = Math.Clamp((int)(bounds.Y * scale), 0, frame.Height - 1);
                    int right = Math.Clamp((int)((bounds.X + bounds.Width) * scale), left + 1, frame.Width);
                    int bottom = Math.Clamp((int)((bounds.Y + bounds.Height) * scale), top + 1, frame.Height);
                    bool red = false, green = false, blue = false, brown = false;
                    for (int y = top; y < bottom; y++)
                    for (int x = left; x < right; x++)
                    {
                        int offset = (y * frame.Width + x) * 4;
                        byte b = frame.Pixels[offset], g = frame.Pixels[offset + 1], r = frame.Pixels[offset + 2];
                        red |= r > 245 && g < 10 && b < 10;
                        green |= g > 245 && r < 10 && b < 10;
                        blue |= b > 245 && r < 10 && g < 10;
                        brown |= Math.Abs(r - 128) < 4 && Math.Abs(g - 64) < 4 && Math.Abs(b - 32) < 4;
                    }
                    Check(red && green && blue && brown,
                        "The actual owned-window pixels did not contain the approved native decoded image colors.");
                    sample.Preview.SetImage(new(assets, "pixels.png"), new(2, 2));
                });
                ui.Wait(() => sample.Preview.State == P.ImageLoadState.Ready && sample.Preview.PixelWidth == 2);
                ui.Ui(() =>
                {
                    Check(image.MemoryState is { SourceWidth: 3, SourceHeight: 2, PixelWidth: 2, PixelHeight: 1 },
                        "Native physical decoding did not preserve the original source aspect metadata.");
                    var frame = WindowCapture.Read(GetAncestor(editor, 2));
                    var bounds = image.GetBounds();
                    float scale = GetDpiForWindow(editor) / 96f;
                    int x = (int)((bounds.X + bounds.Width / 2) * scale);
                    int y = (int)((bounds.Y + 20) * scale);
                    int pixel = (y * frame.Width + x) * 4;
                    byte b = frame.Pixels[pixel], g = frame.Pixels[pixel + 1], r = frame.Pixels[pixel + 2];
                    Check(Math.Abs(r - g) > 12 || Math.Abs(g - b) > 12,
                        "Contain used the rounded 2x1 thumbnail ratio instead of the original 3x2 source ratio.");
                });
                ui.Click("image-invalid");
                ui.Wait(() => sample.Preview.State == P.ImageLoadState.Error);
                ui.Ui(() => Check(!string.IsNullOrWhiteSpace(sample.Preview.ErrorMessage) &&
                    sample.Preview.PixelWidth == 0, "Invalid packaged bytes did not surface a bounded explicit error."));
                ui.Ui(() =>
                {
                    for (int i = 0; i < 20; i++)
                        sample.Preview.SetImage(new(assets, i % 2 == 0 ? "zoey.png" : "pixels.png"), new(32, 32));
                });
                ui.Wait(() => sample.Preview.State == P.ImageLoadState.Ready);
                ui.Ui(() =>
                {
                    Check(image.MemoryState is { SourceWidth: 3, SourceHeight: 2 } &&
                        image.MemoryState.Generation == sample.Preview.SourceGeneration,
                        "A replaced Image generation published stale source data.");
                    var imagePeer = (WindowsImagePeer)backend.Peer(sample.Preview);
                    var resources = typeof(WindowsImagePeer)
                        .GetField("resources", System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic)!.GetValue(imagePeer)!;
                    var owned = (System.Collections.IEnumerable)typeof(WindowsImageResources)
                        .GetField("owned", System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic)!.GetValue(resources)!;
                    var resource = owned.Cast<WindowsImageResources.Resource>().Single();
                    var cleanupFailure = new IOException("Synthetic native Image cancellation failure.");
                    var cancellation = new ImageCancelFailure(resource.Native!, cleanupFailure);
                    resource.Native = cancellation;
                    AggregateException? reported = null;
                    try { host.Detach(); }
                    catch (AggregateException error) { reported = error; }
                    Check(reported is not null && reported.Flatten().InnerExceptions.Contains(cleanupFailure) &&
                        cancellation.Calls == 1 && !host.IsAttached,
                        "A native Image cleanup failure was swallowed or prevented the rest of Host detachment.");
                    Check(HandleCount(window) == baseline && NativeObserverCount(window, "memoryImages") == 0 &&
                        backend.ImagePixels.ReservedBytes == 0,
                        "Image attachment teardown leaked native callback roots, pixels, or arena handles.");
                    backend = new WindowsBackend(surface, dispatcher);
                    host.Attach(backend);
                });
                ui.Wait(() => sample.Preview.State == P.ImageLoadState.Ready);
                ui.Ui(() =>
                {
                    var replacement = (Image)backend.FindControls("packaged-image").Single();
                    Check(replacement.MemoryState.Generation == sample.Preview.SourceGeneration,
                        "Reattachment reused stale Image completion state.");
                    var requests = (System.Collections.IDictionary)typeof(Window)
                        .GetField("memoryImages", System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic)!.GetValue(window)!;
                    var request = requests.Values.Cast<MemoryImageRequest>().Single();
                    var notify = (Action<MemoryImageNotification>)typeof(MemoryImageRequest)
                        .GetField("changed", System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic)!.GetValue(request)!;
                    var staleNotice = new MemoryImageNotification(replacement.MemoryState with
                    {
                        Generation = replacement.MemoryState.Generation - 1,
                        Status = MemoryImageStatus.PresentationFailed
                    }, "Synthetic stale presentation failure.");
                    notify(staleNotice);
                });
                ui.Ui(() => Check(host.IsAttached, "A stale Image presentation failure detached the current native attachment."));
                ui.Ui(() =>
                {
                    var replacement = (Image)backend.FindControls("packaged-image").Single();
                    sample.Preview.Source = null;
                    Check(replacement.MemoryState.Status == MemoryImageStatus.Empty,
                        "Clearing the source did not immediately revoke its native presentation.");
                    sample.Preview.SetImage(new(assets, "pixels.png"), new(32, 32));
                });
                ui.Wait(() => sample.Preview.State == P.ImageLoadState.Ready);
                ui.Ui(() =>
                {
                    var replacement = (Image)backend.FindControls("packaged-image").Single();
                    var requests = (System.Collections.IDictionary)typeof(Window)
                        .GetField("memoryImages", System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic)!.GetValue(window)!;
                    var request = requests.Values.Cast<MemoryImageRequest>().Single();
                    var notify = (Action<MemoryImageNotification>)typeof(MemoryImageRequest)
                        .GetField("changed", System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic)!.GetValue(request)!;
                    notify(new(replacement.MemoryState with { Status = MemoryImageStatus.PresentationFailed },
                        "Synthetic post-Ready presentation failure."));
                    Check(host.IsAttached, "A native Image observer detached the Host inline instead of deferring delivery.");
                    bool failed = false;
                    try
                    {
                        typeof(WindowsDispatcher).GetMethod("Drain",
                            System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic)!.Invoke(dispatcher, null);
                    }
                    catch (System.Reflection.TargetInvocationException error) when
                        (error.InnerException is AggregateException aggregate &&
                         aggregate.ToString().Contains("Synthetic post-Ready presentation failure.", StringComparison.Ordinal))
                    {
                        failed = true;
                    }
                    Check(failed && !host.IsAttached && requests.Count == 0,
                        "The Image presentation-failure protocol did not detach the real Host before reporting its failure.");
                    host.Detach();
                    Check(HandleCount(window) == baseline && backend.ImagePixels.ReservedBytes == 0,
                        "A replacement Image attachment leaked owned resources.");
                });
                ui.Wait(() => Image.GetMemoryStatistics().EncodedBytes == 0);
            }
            finally { ui.Ui(window.Close); }
        });
        RunNativeWork(application, work);
    }

    private sealed class ImageCancelFailure(IDisposable native, Exception failure) : IDisposable
    {
        internal int Calls;
        public void Dispose()
        {
            if (++Calls == 1) throw failure;
            native.Dispose();
        }
    }
}
