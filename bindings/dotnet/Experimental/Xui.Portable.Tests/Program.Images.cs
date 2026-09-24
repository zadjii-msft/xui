using System.Reflection;
using PortableDemo;
using PortableMutation;
using Xui.Experimental.Portable;

internal static partial class Program
{
    private static PackagedAssetManifest ImageAssets() => new(typeof(Program).Assembly);
    private static ImageDecodePlan ImagePlan(string id, ImageDecodeOptions options)
    {
        using var input = typeof(Program).Assembly.GetManifestResourceStream("Xui.Asset." + id)!;
        using var bytes = new MemoryStream();
        input.CopyTo(bytes);
        return ImageDecodePlan.Inspect(bytes.ToArray(), options);
    }

    private static void ImageElementChecks()
    {
        var dispatcher = new Dispatcher();
        using var host = new Host(dispatcher);
        var app = new ImageWorkbench(host, ImageAssets());
        var image = app.Preview;
        Assert(image.State == ImageLoadState.Empty && image.Source is null, "An image starts empty without an implicit load.");
        var legacy = new Backend();
        Throws<NotSupportedException>(() => host.Attach(legacy));
        Assert(legacy.Disposed && legacy.Peers.All(peer => peer.Disposed), "A missing image capability explicitly cleans up a partial attachment.");
        var backend = new ImageBackend();
        host.Attach(backend);
        var peer = backend.Find("packaged-image");
        int states = 0;
        image.StateChanged += _ => { Assert(!peer.InOperation, "Image state handlers never run inline in native operations."); states++; };
        Assert(states == 0, "Attach does not invoke authored image notifications inline.");
        dispatcher.Drain();
        Assert(states == 1 && app.Status == "Empty", "Initial Empty state is queued after attachment.");
        backend.Find("image-load").Events.Click();
        long firstGeneration = image.SourceGeneration;
        Assert(image.State == ImageLoadState.Loading && states == 1 && image.PixelWidth == 0, "Source changes immediately retain Loading metadata but queue the event.");
        dispatcher.Drain();
        Assert(states == 2 && app.Status == "Loading", "Loading is an explicit asynchronous status, not a success-shaped placeholder.");
        var firstSource = app.Source!;
        image.SetImage(new(firstSource.Manifest, firstSource.AssetId), new(64, 64));
        Assert(image.SourceGeneration == firstGeneration && peer.Updates.Count == 1, "Semantically equal sources and options are a no-op.");
        var plan = ImagePlan("zoey.png", image.DecodeOptions);
        Assert(peer.Complete(firstGeneration, plan), "A current native decoder completion is accepted.");
        Assert(image.State == ImageLoadState.Ready && states == 2, "Ready metadata commits without calling authored handlers inline.");
        dispatcher.Drain();
        Assert(app.Status == "Ready: 64x64" && image.PixelWidth == 64 && image.PixelHeight == 64 && states == 3,
            "Ready publishes actual validated dimensions after decode/presentation.");
        Assert(!peer.Complete(firstGeneration, plan), "A duplicate terminal completion does not create another image event.");
        Assert(!peer.Fail(firstGeneration, "late decode error") && host.IsAttached &&
            image.State == ImageLoadState.Ready && image.PixelWidth == 64 && image.ErrorMessage is null,
            "An ordinary decode failure cannot replace an already completed generation or imply attachment failure.");
        backend.Find("image-notes").Events.Change("retain \u674e draft");
        var notes = backend.Find("image-notes");
        backend.Find("image-smaller").Events.Click();
        long smallerGeneration = image.SourceGeneration;
        Assert(smallerGeneration > firstGeneration && image.State == ImageLoadState.Loading && image.PixelWidth == 0 && image.ErrorMessage is null,
            "Changing decode options invalidates old dimensions before a new request.");
        Assert(!peer.Complete(firstGeneration, plan) && !peer.Fail(firstGeneration, "obsolete failure"),
            "An old generation cannot overwrite a new request with success or error.");
        var smaller = ImagePlan("zoey.png", image.DecodeOptions);
        peer.Complete(smallerGeneration, smaller);
        dispatcher.Drain();
        Assert(app.Status == "Ready: 32x32" && app.Draft == "retain \u674e draft" && notes.Updates.Count == 0,
            "Image source changes retain sibling editors without rewriting their text.");
        backend.Find("image-invalid").Events.Click();
        peer.Fail(image.SourceGeneration, "The packaged fixture is not a supported image.");
        dispatcher.Drain();
        Assert(image.State == ImageLoadState.Error && image.PixelWidth == 0 && app.Status.StartsWith("Error:", StringComparison.Ordinal),
            "Expected decoder failures have an explicit Error state and visible metadata.");
        backend.Find("image-clear").Events.Click();
        dispatcher.Drain();
        Assert(image.State == ImageLoadState.Empty && image.ErrorMessage is null && image.Source is null &&
            image.PixelWidth == 0 && peer.HasImage == false, "Null source clears native output and stale error/dimension state.");
        peer.Reject = true;
        long unchanged = image.SourceGeneration;
        Throws<NotSupportedException>(() => image.Source = firstSource);
        Assert(image.Source is null && image.SourceGeneration == unchanged && host.IsAttached, "Native request validation rejects before state/generation mutation.");
        peer.Reject = false;
        image.Source = firstSource;
        long detachedGeneration = image.SourceGeneration;
        int before = states;
        host.Detach();
        Assert(image.SourceGeneration > detachedGeneration && image.State == ImageLoadState.Loading &&
            image.PixelWidth == 0 && image.ErrorMessage is null && backend.Trace.IndexOf("cancel") < backend.Trace.IndexOf("unmount"),
            "Detach invalidates generation and releases native requests before unmount, retaining only the pending source model.");
        Assert(!peer.Complete(detachedGeneration, smaller) && !peer.Fail(detachedGeneration, "late"), "Late decode callbacks are stale after detach.");
        dispatcher.Drain();
        Assert(states == before, "Old queued image notifications do not leak into a detached host.");
        var replacement = new ImageBackend();
        host.Attach(replacement);
        dispatcher.Drain();
        Assert(image.State == ImageLoadState.Loading && image.SourceGeneration > detachedGeneration, "Reattachment starts a fresh request rather than retaining a false Ready state.");
        replacement.Find("packaged-image").Fail(image.SourceGeneration, "Reattached failure");
        dispatcher.Drain();
        host.Dispose();
        Assert(replacement.Peers.All(p => p.Disposed), "Image attachment release disposes every peer.");
        Throws<ObjectDisposedException>(() => image.Source = null);
        ImageFailureChecks();
        ImageSubtreeChecks();
        ImageNotificationChecks();
        ImagePreflightInteractionChecks();
        ImageStructuralDeliveryChecks();
        ImageLayoutIndependenceChecks();
        ImagePresentationFailureChecks();
    }

    private static void ImageFailureChecks()
    {
        foreach (string failure in new[] { "wrong-plan", "wrong-output", "blank-error", "nul-error", "large-error", "native-update", "cleanup", "notification" })
        {
            var dispatcher = new Dispatcher();
            using var host = new Host(dispatcher);
            var app = new ImageWorkbench(host, ImageAssets());
            app.Source = new(app.Assets, "zoey.png");
            var backend = new ImageBackend();
            host.Attach(backend);
            dispatcher.Drain();
            var image = app.Preview;
            var peer = backend.Find("packaged-image");
            var plan = ImagePlan("zoey.png", image.DecodeOptions);
            if (failure == "cleanup")
            {
                peer.FailCancel = true;
                Throws<AggregateException>(() => host.Detach());
                Assert(backend.Disposed && backend.Peers.All(p => p.Disposed), "Image cancellation errors do not skip backend/peer cleanup.");
            }
            else if (failure == "notification")
            {
                image.StateChanged += _ => throw new ApplicationException("image observer");
                peer.Complete(image.SourceGeneration, plan);
                Throws<AggregateException>(() => dispatcher.Drain());
                Assert(host.IsAttached && image.State == ImageLoadState.Ready, "Authored image observer failures are explicit without pretending a decode failure.");
            }
            else
            {
                Throws<Exception>(() =>
                {
                    switch (failure)
                    {
                        case "wrong-plan":
                            peer.Complete(image.SourceGeneration, ImagePlan("zoey.png", new(16, 16)));
                            break;
                        case "wrong-output":
                            ((IImageControlEvents)peer.Events).ImageCompleted(image.SourceGeneration, plan, 1, 1);
                            break;
                        case "blank-error": peer.Fail(image.SourceGeneration, "  "); break;
                        case "nul-error": peer.Fail(image.SourceGeneration, "invalid\0metadata"); break;
                        case "large-error": peer.Fail(image.SourceGeneration, new string('x', 4097)); break;
                        case "native-update":
                            peer.FailUpdate = true;
                            app.Decode = new(32, 32);
                            break;
                    }
                });
                Assert(!host.IsAttached && backend.Disposed && backend.Peers.All(p => p.Disposed),
                    failure + " rejects invalid native output or failed application and cleans the attachment.");
                Assert(image.State == ImageLoadState.Loading && image.PixelWidth == 0, "A detached image never exposes a stale successful output.");
            }
        }
        using (var host = new Host(new Dispatcher()))
        {
            var app = new ImageWorkbench(host, ImageAssets());
            long original = app.Preview.SourceGeneration;
            typeof(Image).GetField("generation", BindingFlags.Instance | BindingFlags.NonPublic)!.SetValue(app.Preview, long.MaxValue);
            Throws<OverflowException>(() => app.Preview.Source = new(app.Assets, "zoey.png"));
            Assert(app.Preview.Source is null && app.Preview.State == ImageLoadState.Empty, "Generation overflow is rejected before model mutation.");
            typeof(Image).GetField("generation", BindingFlags.Instance | BindingFlags.NonPublic)!.SetValue(app.Preview, original);
        }
        using (var host = new Host(new InlineImageDispatcher()))
        {
            _ = new ImageWorkbench(host, ImageAssets());
            var backend = new ImageBackend();
            Throws<InvalidOperationException>(() => host.Attach(backend));
            Assert(!host.IsAttached && backend.DisposeCalls == 1, "Inline notification dispatch fails explicitly without double unmount.");
        }
        var rejecting = new Dispatcher { Reject = true };
        using (var host = new Host(rejecting))
        {
            _ = new ImageWorkbench(host, ImageAssets());
            var backend = new ImageBackend();
            Throws<InvalidOperationException>(() => host.Attach(backend));
            Assert(backend.DisposeCalls == 1 && backend.Peers.All(p => p.Disposed), "A rejected image notification post does not retain an unusable attachment.");
        }
    }

    private static void ImageSubtreeChecks()
    {
        var dispatcher = new Dispatcher();
        using var host = new Host(dispatcher);
        var app = new PortableNavigation.RetainedPagesWorkbench(host);
        ImagePage? first = null;
        var source = new PackagedImageSource(ImageAssets(), "zoey.png");
        app.OpenPages = ([PageItem.Create(1, "Image", h => first = new ImagePage(h, source)),
            PageItem.Create(2, "Other", h => new MutationRow(h, "other-image-page"))], 1UL);
        var backend = new ImageBackend();
        host.Attach(backend);
        dispatcher.Drain();
        var peer = backend.Find("owned-image");
        long generation = first!.Image.SourceGeneration;
        app.Documents.SetSelected(2);
        Assert(first.Image.SourceGeneration == generation && peer.CancelCalls == 0,
            "Hiding an inactive retained page does not retire its image request.");
        app.OpenPages = ([app.OpenPages.Items[1]], 2UL);
        Assert(peer.CancelCalls == 1 && backend.Trace.IndexOf("cancel") < backend.Trace.IndexOf("remove"),
            "Permanent page retirement cancels its image before native subtree removal.");
        Assert(!peer.Fail(generation, "late page failure"), "A removed page cannot publish a later decoder result.");
        Assert(!((IImagePresentationEvents)peer.Events).ImagePresentationFailed(generation,
            new ApplicationException("removed page surface")) && host.IsAttached,
            "A removed image peer cannot fail the surviving attachment.");
    }

    private static void ImageNotificationChecks()
    {
        var dispatcher = new Dispatcher();
        using var host = new Host(dispatcher);
        var app = new ImageWorkbench(host, ImageAssets());
        var backend = new ImageBackend();
        host.Attach(backend);
        dispatcher.Drain();
        var image = app.Preview;
        var peer = backend.Find("packaged-image");
        int notices = 0;
        image.StateChanged += _ => notices++;
        app.Source = new(app.Assets, "zoey.png");
        app.Decode = new(32, 32);
        app.Source = null;
        Assert(notices == 0, "Rapid source/option changes never invoke state handlers inline.");
        dispatcher.Drain();
        Assert(notices == 1 && image.State == ImageLoadState.Empty, "Pending image notifications coalesce to the latest generation/state.");
        app.Source = new(app.Assets, "zoey.png");
        long pending = image.SourceGeneration;
        var plan = ImagePlan("zoey.png", image.DecodeOptions);
        app.Preview.Enabled = false;
        app.Preview.Visible = false;
        Assert(peer.Complete(pending, plan), "Loading completion is metadata even when the native image is disabled or hidden.");
        dispatcher.Drain();
        Assert(image.State == ImageLoadState.Ready, "Hidden image completion retains successful state without exposing user input.");
        int before = notices;
        app.Decode = new(64, 64);
        long currentGeneration = image.SourceGeneration;
        Task.Run(() => Throws<InvalidOperationException>(() => peer.Fail(currentGeneration, "wrong thread"))).GetAwaiter().GetResult();
        peer.Validating = () => Throws<InvalidOperationException>(() => app.Draft = "reentrant validation");
        app.Decode = new(16, 16);
        peer.Validating = null;
        Assert(app.Draft == "", "Image backend preflight cannot mutate authored state.");
        Action<ImageLoadState> clearOnReady = state => { if (state == ImageLoadState.Ready) image.Source = null; };
        image.StateChanged += clearOnReady;
        peer.Complete(image.SourceGeneration, ImagePlan("zoey.png", image.DecodeOptions));
        dispatcher.Drain();
        Assert(image.Source is null && image.State == ImageLoadState.Empty && notices == before + 2,
            "A Ready handler can replace its source; a subsequent queued event reports the replacement without recursion.");
        image.StateChanged -= clearOnReady;
        image.Source = new(app.Assets, "zoey.png");
        long beforeFailure = image.SourceGeneration;
        peer.FailUpdate = true;
        Throws<ApplicationException>(() => image.Source = null);
        Assert(!host.IsAttached && image.Source is null && image.State == ImageLoadState.Empty &&
            image.SourceGeneration > beforeFailure, "A failed native clear retains the committed empty request, not the earlier source.");

        var canceledDispatcher = new CancelDispatcher();
        using var canceledHost = new Host(canceledDispatcher);
        _ = new ImageWorkbench(canceledHost, ImageAssets());
        var canceledBackend = new ImageBackend();
        canceledHost.Attach(canceledBackend);
        Throws<ApplicationException>(() => canceledDispatcher.Cancel!(new ApplicationException("dispatcher shutdown")));
        Assert(!canceledHost.IsAttached && canceledBackend.DisposeCalls == 1,
            "Cancellation of accepted image notification work explicitly fails and releases the attachment.");
        canceledDispatcher.Action!();
        Assert(canceledBackend.DisposeCalls == 1, "A late canceled dispatch cannot duplicate image cleanup.");
    }

    private static void ImagePreflightInteractionChecks()
    {
        var dispatcher = new Dispatcher();
        using var host = new Host(dispatcher);
        var app = new ImageWorkbench(host, ImageAssets());
        var backend = new ImageBackend();
        host.Attach(backend);
        dispatcher.Drain();
        var image = app.Preview;
        var peer = backend.Find("packaged-image");
        var inputEvents = (ITextInteractionEvents)backend.Find("image-notes").Events;
        var first = new PackagedImageSource(app.Assets, "zoey.png");
        var replacement = new PackagedImageSource(app.Assets, "invalid.png");
        long generation = image.SourceGeneration;
        PackagedImageSource? observedSource = null;
        long observedGeneration = -1;
        app.Notes.InteractionChanged += _ =>
        {
            observedSource = image.Source;
            observedGeneration = image.SourceGeneration;
            image.SetImage(replacement, new(32, 32));
        };
        peer.Validating = () =>
        {
            peer.Validating = null;
            inputEvents.InteractionChanged(new(true, false));
        };
        image.SetImage(first, new(64, 64));
        Assert(ReferenceEquals(observedSource, first) && observedGeneration == generation + 1,
            "Preflight interaction handlers run only after the first image request commits.");
        Assert(ReferenceEquals(image.Source, replacement) && image.SourceGeneration == generation + 2 &&
            image.DecodeOptions.OutputWidth == 32,
            "An interaction handler's later image request is not overwritten or assigned a reused generation.");
        Assert(peer.Requests.Select(request => request.Generation).SequenceEqual([generation + 1, generation + 2]) &&
            peer.Requests.Select(request => request.Source?.AssetId).SequenceEqual(["zoey.png", "invalid.png"]),
            "Native request order follows the two committed source generations.");
        dispatcher.Drain();
        Assert(app.Status == "Loading", "The queued state reflects the final reentrant request.");
    }

    private static void ImageStructuralDeliveryChecks()
    {
        foreach (string phase in new[] { "factory", "nested-build", "updater" })
        {
            var dispatcher = new Dispatcher();
            using var host = new Host(dispatcher);
            var app = new PortableNavigation.RetainedPagesWorkbench(host);
            ImagePage? page = null;
            var source = new PackagedImageSource(ImageAssets(), "zoey.png");
            var originalItem = PageItem.Create(1, "Retained image", h => page = new ImagePage(h, source));
            app.OpenPages = ([originalItem], 1UL);
            var backend = new ImageBackend();
            host.Attach(backend);
            int notifications = 0;
            page!.Image.StateChanged += _ => notifications++;
            Element? abandoned = null;
            PageItem[] requested = phase == "updater"
                ? [PageItem.Create(1, "Retained image", h => new ImagePage(h, source), _ => dispatcher.Drain())]
                : [originalItem, PageItem.Create(2, "New component", h =>
                {
                    if (phase == "factory") dispatcher.Drain();
                    using var nested = h.BeginBuild();
                    var root = h.Stack(Axis.Vertical);
                    abandoned = root;
                    root.Add(h.Label("Candidate"));
                    if (phase == "nested-build") dispatcher.Drain();
                    h.SetContent(root);
                    nested.Complete();
                    return new ImageCandidate(root);
                })];
            var failure = Throws<KeyedUpdateException>(() => app.OpenPages = (requested, 1UL));
            Assert(failure.ModelCommitted == (phase == "updater") && notifications == 0,
                phase + " dispatcher pumping never delivers authored image state inside structural work.");
            Assert(!host.IsAttached && backend.DisposeCalls == 1 && backend.Peers.All(peer => peer.Disposed),
                phase + " notification protocol rejection retires native attachment ownership once.");
            Assert(app.Documents.Children.Count == 1 &&
                page.Image.State == ImageLoadState.Loading && page.Image.PixelWidth == 0,
                phase + " preserves the appropriate managed page tree with no stale Ready image.");
            if (abandoned is not null)
                Throws<ObjectDisposedException>(() => _ = abandoned.Children);
            dispatcher.Drain();
            Assert(notifications == 0, phase + " failure cannot leak old notifications after scope rollback.");
        }
    }

    private sealed record ImageCandidate(Element Root) : IPortableComponent;

    private static void ImageLayoutIndependenceChecks()
    {
        var source = new PackagedImageSource(ImageAssets(), "zoey.png");
        foreach (string sizing in new[] { "unsized", "preferred", "fixed-with-axis-overrides" })
        {
            var dispatcher = new Dispatcher();
            using var host = new Host(dispatcher);
            Image image;
            using (var build = host.BeginBuild())
            {
                var root = host.Stack(Axis.Vertical);
                image = host.Image("Quality-independent layout");
                image.AutomationId = "layout-image";
                if (sizing == "preferred") ElementExtensions.PreferredSize(image, 192, 144);
                if (sizing == "fixed-with-axis-overrides")
                {
                    ElementExtensions.FixedSize(image, 240, 180);
                    image.SetConstraints(new AxisConstraints(Minimum: 80, Maximum: 160), AxisConstraints.Auto);
                }
                root.Add(image);
                host.SetContent(root);
                build.Complete();
            }
            var authored = (image.FixedSize, image.PreferredSize, image.WidthConstraints, image.HeightConstraints);
            var backend = new ImageBackend();
            host.Attach(backend);
            var peer = backend.Find("layout-image");
            var originalPeers = backend.Peers.ToArray();
            void CheckLayout(ImageLoadState state)
            {
                Assert(image.State == state &&
                    (image.FixedSize, image.PreferredSize, image.WidthConstraints, image.HeightConstraints) == authored,
                    sizing + " retains authored layout metadata in " + state + ".");
                Assert(originalPeers.SequenceEqual(backend.Peers) &&
                    peer.Updates.All(property => property == ElementProperty.ImageRequest),
                    sizing + " image state and quality changes never replace peers or issue layout-property writes.");
            }
            dispatcher.Drain();
            CheckLayout(ImageLoadState.Empty);
            foreach (int dimension in new[] { 64, 32, 16 })
            {
                image.SetImage(source, new(dimension, dimension));
                CheckLayout(ImageLoadState.Loading);
                var plan = ImagePlan("zoey.png", image.DecodeOptions);
                Assert(peer.Complete(image.SourceGeneration, plan), "Current quality generation completes.");
                dispatcher.Drain();
                Assert(image.PixelWidth == dimension && image.PixelHeight == dimension,
                    "Decode quality changes physical output metadata, not authored layout.");
                CheckLayout(ImageLoadState.Ready);
            }
            image.SetImage(new(source.Manifest, "invalid.png"), image.DecodeOptions);
            peer.Fail(image.SourceGeneration, "The packaged fixture is not a supported image.");
            dispatcher.Drain();
            CheckLayout(ImageLoadState.Error);
            image.Source = null;
            dispatcher.Drain();
            CheckLayout(ImageLoadState.Empty);
            image.Source = source;
            host.Detach();
            Assert(image.State == ImageLoadState.Loading &&
                (image.FixedSize, image.PreferredSize, image.WidthConstraints, image.HeightConstraints) == authored,
                sizing + " retains only pending source and authored layout across detach.");
        }
    }

    private static void ImagePresentationFailureChecks()
    {
        foreach (bool ready in new[] { false, true })
        foreach (bool hidden in new[] { false, true })
        foreach (bool cleanupFailure in new[] { false, true })
        {
            var dispatcher = new Dispatcher();
            using var host = new Host(dispatcher);
            var app = new ImageWorkbench(host, ImageAssets());
            app.Source = new(app.Assets, "zoey.png");
            app.Preview.Visible = !hidden;
            app.Preview.Enabled = !hidden;
            var backend = new ImageBackend();
            host.Attach(backend);
            dispatcher.Drain();
            var peer = backend.Find("packaged-image");
            var events = (IImagePresentationEvents)peer.Events;
            long generation = app.Preview.SourceGeneration;
            int notifications = 0;
            app.Preview.StateChanged += _ => notifications++;
            if (ready) peer.Complete(generation, ImagePlan("zoey.png", app.Decode));
            peer.FailCancel = cleanupFailure;
            var fatal = new ApplicationException("native image surface failed");
            var reported = Throws<Exception>(() => events.ImagePresentationFailed(generation, fatal));
            if (cleanupFailure)
            {
                var errors = ((AggregateException)reported).Flatten().InnerExceptions;
                Assert(errors.Contains(fatal) && errors.Any(error => error.Message == "image cleanup"),
                    "Terminal image reporting preserves the original failure and every cleanup error.");
            }
            else Assert(ReferenceEquals(reported, fatal), "The original native presentation error reaches the managed callback boundary.");
            Assert(!host.IsAttached && backend.DisposeCalls == 1 && backend.Peers.All(value => value.Disposed) &&
                peer.CancelCalls == 1 && backend.Trace.IndexOf("cancel") < backend.Trace.IndexOf("unmount"),
                "A current presentation failure actually detaches and retires the image before native unmount.");
            Assert(app.Preview.State == ImageLoadState.Loading && app.Preview.PixelWidth == 0 &&
                app.Preview.ErrorMessage is null && app.Preview.SourceGeneration > generation,
                "Terminal detachment retains only the pending source model, not Ready or an ordinary decode Error.");
            dispatcher.Drain();
            Assert(notifications == 0, "Queued Ready state cannot escape an attachment retired by a presentation failure.");
            Assert(!events.ImagePresentationFailed(generation, fatal) && !peer.Fail(generation, "late codec callback"),
                "Reentrant or repeated failure delivery after teardown is stale and cannot repeat cleanup.");
            var replacement = new ImageBackend();
            host.Attach(replacement);
            Assert(!events.ImagePresentationFailed(app.Preview.SourceGeneration, fatal) && host.IsAttached,
                "An old sink cannot fail a replacement attachment even with its new generation.");
            var next = replacement.Find("packaged-image");
            Assert(next.Complete(app.Preview.SourceGeneration, ImagePlan("zoey.png", app.Decode)),
                "Explicit reattachment starts a new request after terminal image cleanup.");
        }
        ImagePresentationFailureIdentityChecks();
        ImagePresentationFailureBoundaryChecks();
        ImagePresentationFailureOwnedServicesChecks();
    }

    private static void ImagePresentationFailureIdentityChecks()
    {
        var dispatcher = new Dispatcher();
        using var host = new Host(dispatcher);
        var app = new ImageWorkbench(host, ImageAssets());
        var backend = new ImageBackend();
        host.Attach(backend);
        var peer = backend.Find("packaged-image");
        var events = (IImagePresentationEvents)peer.Events;
        var fatal = new ApplicationException("presentation failed");
        Assert(!events.ImagePresentationFailed(app.Preview.SourceGeneration, fatal) && host.IsAttached,
            "An Empty image has no native request to fail.");
        app.Source = new(app.Assets, "zoey.png");
        long obsolete = app.Preview.SourceGeneration;
        app.Decode = new(32, 32);
        Assert(!events.ImagePresentationFailed(obsolete, fatal) && host.IsAttached && peer.CancelCalls == 0,
            "An obsolete presentation generation cannot tear down a newer request on the same peer.");
        long current = app.Preview.SourceGeneration;
        Throws<ArgumentNullException>(() => events.ImagePresentationFailed(current, null!));
        Throws<InvalidOperationException>(() =>
            ((IImagePresentationEvents)backend.Find("image-notes").Events).ImagePresentationFailed(current, fatal));
        Task.Run(() => Throws<InvalidOperationException>(() =>
            events.ImagePresentationFailed(current, fatal))).GetAwaiter().GetResult();
        Assert(host.IsAttached && peer.CancelCalls == 0, "Malformed or wrong-thread failure delivery cannot retire native owners.");
        peer.Fail(current, "expected codec failure");
        Assert(!events.ImagePresentationFailed(current, fatal) && app.Preview.State == ImageLoadState.Error,
            "A completed codec error has no live presentation to fail again.");
        app.Decode = new(16, 16);
        current = app.Preview.SourceGeneration;
        peer.Complete(current, ImagePlan("zoey.png", app.Decode));
        dispatcher.Drain();
        bool returnedToNative = false;
        peer.Canceling = () =>
        {
            Assert(!host.IsAttached && !events.ImagePresentationFailed(current, fatal),
                "Cleanup invalidates attachment identity before reentrant native failure reporting.");
        };
        dispatcher.Post(() =>
        {
            var reported = Throws<ApplicationException>(() => events.ImagePresentationFailed(current, fatal));
            Assert(ReferenceEquals(reported, fatal) && !host.IsAttached && backend.Disposed,
                "The backend's queued callback observes completed Host teardown before reporting the error.");
            returnedToNative = true;
        });
        dispatcher.Drain();
        Assert(returnedToNative && peer.CancelCalls == 1, "A queued failure after Ready has one explicit managed reporting boundary.");
    }

    private static void ImagePresentationFailureBoundaryChecks()
    {
        foreach (string phase in new[] { "nested-build", "validation", "native-update", "factory", "updater" })
        {
            var dispatcher = new Dispatcher();
            using var host = new Host(dispatcher);
            var app = new PortableNavigation.RetainedPagesWorkbench(host);
            ImagePage? page = null;
            var source = new PackagedImageSource(ImageAssets(), "zoey.png");
            var item = PageItem.Create(1, "Image", h => page = new ImagePage(h, source));
            app.OpenPages = ([item], 1UL);
            var backend = new ImageBackend();
            host.Attach(backend);
            dispatcher.Drain();
            var peer = backend.Find("owned-image");
            var events = (IImagePresentationEvents)peer.Events;
            var fatal = new ApplicationException("failure inside " + phase);
            bool checkedBoundary = false;
            void RejectInline()
            {
                var error = Throws<InvalidOperationException>(() =>
                    events.ImagePresentationFailed(page!.Image.SourceGeneration, fatal));
                Assert(ReferenceEquals(error.InnerException, fatal) && host.IsAttached && peer.CancelCalls == 0,
                    phase + " rejects unsafe inline failure delivery without retiring resources inside the operation.");
                checkedBoundary = true;
            }
            if (phase is "validation" or "native-update")
            {
                if (phase == "validation") peer.Validating = RejectInline;
                else peer.Updating = RejectInline;
                page!.Image.DecodeOptions = new(32, 32);
                peer.Validating = peer.Updating = null;
            }
            else if (phase is "factory" or "nested-build")
                app.OpenPages = ([item, PageItem.Create(2, "Candidate", h =>
                {
                    if (phase == "nested-build")
                    {
                        using var build = h.BeginBuild();
                        RejectInline();
                    }
                    else RejectInline();
                    return new MutationRow(h, "candidate-after-image-failure");
                })], 1UL);
            else
                app.OpenPages = ([PageItem.Create(1, "Image", h => new ImagePage(h, source), _ => RejectInline())], 1UL);
            Assert(checkedBoundary && host.IsAttached, "The " + phase + " fixture executes its guarded callback.");
            var reported = Throws<ApplicationException>(() =>
                events.ImagePresentationFailed(page!.Image.SourceGeneration, fatal));
            Assert(ReferenceEquals(reported, fatal) && !host.IsAttached,
                "Failure delivery after " + phase + " unwinds performs the normal terminal cleanup.");
        }
    }

    private static void ImagePresentationFailureOwnedServicesChecks()
    {
        var dispatcher = new Dispatcher();
        using var host = new Host(dispatcher);
        Greeting greeting;
        ImageWorkbench app;
        using (var build = host.BeginBuild())
        {
            var root = host.Stack(Axis.Vertical);
            greeting = new Greeting(host);
            app = new ImageWorkbench(host, ImageAssets());
            root.Add(greeting.Root).Add(app.Root);
            host.SetContent(root);
            build.Complete();
        }
        app.Source = new(app.Assets, "zoey.png");
        var backend = new ImageServiceBackend();
        host.Attach(backend);
        dispatcher.Drain();
        var peer = backend.Images.Find("packaged-image");
        var events = (IImagePresentationEvents)peer.Events;
        long generation = app.Preview.SourceGeneration;
        var fatal = new ApplicationException("native surface unavailable");
        IVirtualViewportLease? lease = null;
        using var ownedLease = lease = host.BeginVirtualViewport((ScrollView)greeting.Root.Children[1], 10, 32, 1, request =>
        {
            Assert(lease!.TryBeginUpdate(request.Epoch) == VirtualViewportUpdateResult.Ready,
                "The image failure fixture reserves a real Host viewport staging guard.");
            var error = Throws<InvalidOperationException>(() => events.ImagePresentationFailed(generation, fatal));
            Assert(ReferenceEquals(error.InnerException, fatal) && host.IsAttached && peer.CancelCalls == 0,
                "Terminal image delivery cannot retire native references while viewport staging is active.");
            lease.Cancel(request.Epoch);
        });
        backend.Viewports.Leases.Single().Emit(Request(1));
        peer.Complete(generation, ImagePlan("zoey.png", app.Decode));
        var dialog = host.ShowMessageAsync(MessageDialogRequest.Alert("Notice", "Owned native request."));
        var error = Throws<ApplicationException>(() => events.ImagePresentationFailed(generation, fatal));
        Assert(ReferenceEquals(error, fatal) && !host.IsAttached && backend.Disposed,
            "A modal input guard does not suppress terminal native image failure reporting.");
        Throws<OperationCanceledException>(() => dialog.GetAwaiter().GetResult());
        Assert(backend.Dialogs.Requests.Single().DisposeCalls == 1 && backend.Viewports.Leases.Single().DisposeCalls == 1,
            "Image fatal teardown retires all attachment-owned dialog and viewport resources, not just the failing peer.");
        Assert(!app.Lifetime.Token.IsCancellationRequested && !greeting.Lifetime.Token.IsCancellationRequested,
            "Image presentation failure retires the native attachment without retiring retained component lifetimes.");
    }

    private sealed class ImageServiceBackend : IMessageDialogBackend
    {
        internal ImageBackend Images { get; } = new();
        internal ViewportBackend Viewports { get; } = new();
        internal MessageBackend Dialogs { get; } = new();
        internal bool Disposed { get; private set; }
        public CapabilityAvailability MessageDialogAvailability => CapabilityAvailability.Available;
        public IMessageDialogRequest BeginMessageDialog(MessageDialogRequest request,
            Action<OperationResult<MessageDialogDecision>> completed) => Dialogs.BeginMessageDialog(request, completed);
        public IElementPeer Create(Element element, IControlEvents events) =>
            element is ScrollView ? Viewports.Create(element, events) : Images.Create(element, events);
        public void Mount(IElementPeer root) { }
        public void Dispose() { Disposed = true; Images.Dispose(); Viewports.Dispose(); Dialogs.Dispose(); }
    }

    private sealed class ImagePage : IPortableComponent
    {
        public Element Root { get; }
        public Image Image { get; }
        public ImagePage(Host host, PackagedImageSource source)
        {
            using var build = host.BeginBuild();
            var root = host.Stack(Axis.Vertical);
            Image = host.Image("Owned image");
            Image.AutomationId = "owned-image";
            Image.Source = source;
            root.Add(Image);
            host.SetContent(root);
            build.Complete();
            Root = root;
        }
    }

    private sealed class InlineImageDispatcher : IUiDispatcher
    {
        public bool CheckAccess() => true;
        public void Post(Action action) => action();
    }

    private sealed class ImageBackend : IBackend
    {
        public List<ImagePeer> Peers { get; } = [];
        public List<string> Trace { get; } = [];
        public bool Disposed => DisposeCalls != 0;
        public int DisposeCalls { get; private set; }
        public ImagePeer Find(string id) => Peers.Last(p => p.Id == id);
        public IElementPeer Create(Element element, IControlEvents events)
        {
            var peer = new ImagePeer(this, element, events);
            Peers.Add(peer);
            return peer;
        }
        public void Mount(IElementPeer root) { }
        public void Dispose() { DisposeCalls++; Trace.Add("unmount"); }
    }

    private sealed class ImagePeer(ImageBackend backend, Element element, IControlEvents events) : IImageElementPeer, IPageViewElementPeer, IPageSelectorElementPeer, IConstrainedElementPeer
    {
        public string Id { get; } = element is Control control ? control.AutomationId : "";
        public IControlEvents Events { get; } = events;
        public List<ElementProperty> Updates { get; } = [];
        public List<(long Generation, PackagedImageSource? Source)> Requests { get; } = [];
        public bool HasImage { get; private set; }
        public bool InOperation { get; private set; }
        public bool Reject { get; set; }
        public bool FailUpdate { get; set; }
        public bool FailCancel { get; set; }
        public bool Disposed { get; private set; }
        public Action? Validating { get; set; }
        public Action? Updating { get; set; }
        public Action? Canceling { get; set; }
        public int CancelCalls { get; private set; }
        public void ValidateImage(PackagedImageSource? source, ImageDecodeOptions options)
        {
            Validating?.Invoke();
            if (Reject) throw new NotSupportedException("Image requests are not supported by this native configuration.");
        }
        public void Update(ElementProperty property)
        {
            InOperation = true;
            try
            {
                Updating?.Invoke();
                if (FailUpdate) throw new ApplicationException("image update");
                Updates.Add(property);
                if (property == ElementProperty.ImageRequest)
                {
                    var image = (Image)element;
                    Requests.Add((image.SourceGeneration, image.Source));
                    HasImage = false;
                }
            }
            finally { InOperation = false; }
        }
        public bool Complete(long generation, ImageDecodePlan plan)
        {
            bool accepted = ((IImageControlEvents)Events).ImageCompleted(generation, plan, plan.OutputWidth, plan.OutputHeight);
            if (accepted) HasImage = true;
            return accepted;
        }
        public bool Fail(long generation, string message) => ((IImageControlEvents)Events).ImageFailed(generation, message);
        public void CancelImage()
        {
            CancelCalls++;
            HasImage = false;
            backend.Trace.Add("cancel");
            Canceling?.Invoke();
            if (FailCancel) throw new ApplicationException("image cleanup");
        }
        public void AddChild(IElementPeer child) { }
        public void InsertChild(int index, IElementPeer child) { }
        public void RemoveChild(IElementPeer child) => backend.Trace.Add("remove");
        public void ValidateMove(IElementPeer child, int index) { }
        public void MoveChild(IElementPeer child, int index) { }
        public void ValidatePages(IReadOnlyList<PageEntry> pages, ulong? selected) { }
        public void ValidateVisibility(bool visible) { }
        public void ConnectPages(IPageViewElementPeer pages) { }
        public void Dispose() { Disposed = true; }
    }
}
