using PortableDemo;
using Xui.Experimental.Portable;

internal static partial class Program
{
    private static int assertions;
    private static void Assert(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
        assertions++;
    }
    private static void Throws<T>(Action action) where T : Exception
    {
        try { action(); } catch (T) { assertions++; return; }
        throw new InvalidOperationException("Expected " + typeof(T).Name);
    }
    private static void Main()
    {
        ModelChecks();
        BasicChecks();
        ResultChecks();
        FileChecks();
        CancellationChecks();
        UiContextChecks();
        OwnershipChecks();
        CorpusChecks();
        Console.WriteLine($"Platform services workbench: {assertions} assertions passed.");
    }
    private static void ModelChecks()
    {
        foreach (string valid in new[] { "https://example.com", "https://example.com/help?q=x#part", "https://localhost:8443/test" })
            Assert(PlatformServicesWorkbenchState.TryUri(valid, out var uri) && uri!.Scheme == "https", "Valid HTTPS input.");
        foreach (string invalid in new[] { "", " ", "http://example.com", "file:///C:/test", "javascript:alert(1)", "data:text/plain,test",
            "https://user:password@example.com", "https://example.com\nsecret", " https://example.com", "https://example.com ", "/relative", "mailto:a@example.test", new string('a', 2049) })
            Assert(!PlatformServicesWorkbenchState.TryUri(invalid, out _), "Unsafe or malformed launch input is rejected.");
        Assert(PlatformServicesWorkbenchState.SafeMetadata("untrusted\0\nlabel", 120) == "untrusted  label", "Untrusted display labels do not contain control characters.");
        Assert(PlatformServicesWorkbenchState.SafeMetadata(new string('x', 119) + "\U0001F642", 120).Length == 119, "Truncating metadata does not split a UTF-16 pair.");
        Throws<ArgumentOutOfRangeException>(() => PlatformServicesWorkbenchState.AvailabilityText((CapabilityAvailability)99));
        var state = new PlatformServicesWorkbenchState { ClipboardDraft = new string('a', 4097) };
        Assert(!state.CanWriteClipboard && state.ClipboardValidation == "Clipboard draft exceeds 4096 UTF-16 units.", "Explicit clipboard write bound.");
    }
    private static void BasicChecks()
    {
        using var h = new Harness();
        Assert(h.Services.Reads + h.Services.Writes + h.Services.Launches + h.Picker.Opens == 0, "Construction and capability queries perform no I/O.");
        Assert(h.App.Root.Children.Count == 2 && h.App.Root.Children[1] is ScrollView { Flex: 1 }, "Only title fixed outside scroll.");
        var input = h.Backend.Find("services-clipboard-draft");
        Assert(input.Events.Change("copy only on action") && h.Services.Writes == 0 && input.Updates.Count == 0,
            "Editing draft retains native text without clipboard action.");
        Assert(h.App.ClipboardInput.CaptionVisible && h.App.ClipboardInput.FixedSize is null &&
            h.App.ClipboardInput.PreferredSize is null, "Native captioned editor is unconstrained.");
        h.Run(h.Controller.WriteClipboard);
        Assert(h.Services.Writes == 1 && h.Services.LastWrite == "copy only on action" &&
            h.Controller.State.Status == "Clipboard write completed.", "Explicit user action starts and acknowledges clipboard write.");
        h.Run(h.Controller.ReadClipboard);
        Assert(h.Controller.State.ClipboardLength == 14 && h.Controller.State.ClipboardSummary ==
            "Clipboard read: 14 UTF-16 units. Content not displayed." &&
            h.Controller.State.ClipboardDraft == "copy only on action", "Clipboard read retains only length, not contents.");
        h.Controller.SetClipboardDraft("");
        h.Run(h.Controller.WriteClipboard);
        Assert(h.Services.LastWrite == "", "Explicit copy with empty draft writes empty clipboard.");
        h.Controller.SetClipboardDraft(new string('x', 4096));
        Assert(h.Controller.State.CanWriteClipboard, "Exact clipboard draft limit accepted.");
        h.Controller.SetClipboardDraft(new string('x', 4097));
        Assert(!h.Controller.State.CanWriteClipboard, "Over-limit draft remains editable but cannot be copied.");
        Throws<InvalidOperationException>(() => h.Gesture.Run(h.Controller.WriteClipboard));
        Throws<ArgumentException>(() => h.Controller.SetClipboardDraft("bad\0text"));
        h.Controller.SetUriDraft("https://example.com/guide");
        Assert(h.Services.Launches == 0, "Editing URI does not launch anything.");
        h.Run(h.Controller.Launch);
        Assert(h.Services.LastUri!.AbsoluteUri == "https://example.com/guide" &&
            h.Controller.State.Status == "HTTPS launch completed.", "Only explicit HTTPS action calls launcher.");
        foreach (string value in new[] { "http://example.com", "https://user@example.com" })
        {
            h.Controller.SetUriDraft(value);
            Assert(!h.Controller.State.CanOpenUri, "Unsafe draft disables launch.");
            Throws<InvalidOperationException>(() => h.Gesture.Run(h.Controller.Launch));
        }
        h.Services.Clipboard = CapabilityAvailability.Unsupported;
        h.Services.Uri = CapabilityAvailability.Unsupported;
        h.Picker.Availability = CapabilityAvailability.Unsupported;
        h.Controller.RefreshAvailability();
        Assert(!h.Controller.State.CanReadClipboard && !h.Controller.State.CanWriteClipboard &&
            !h.Controller.State.CanOpenUri && !h.Controller.State.CanPickFile, "Unsupported capability disables corresponding native actions.");
        Throws<InvalidOperationException>(() => h.Gesture.Run(h.Controller.ReadClipboard));
        Throws<InvalidOperationException>(() => h.Gesture.Run(h.Controller.PickFile));
        h.Host.Dispose();
        Assert(!h.Services.Disposed && h.Picker.Disposals == 0 && h.App.Lifetime.Token.IsCancellationRequested,
            "Root cleanup owns async work, not injected providers.");
    }
    private static void ResultChecks()
    {
        using var h = new Harness();
        foreach (var result in new[] { OperationResult<string>.Denied(), OperationResult<string>.Unsupported(),
            OperationResult<string>.Cancelled(), OperationResult<string>.Failed(new IOException("PRIVATE_CLIPBOARD_CONTENT")) })
        {
            h.Services.Read = _ => Task.FromResult(result);
            h.Run(h.Controller.ReadClipboard);
            Assert(!h.Controller.State.Busy && h.Controller.State.ClipboardLength is null, "Noncompleted read never fabricates clipboard success.");
            Assert(h.Controller.State.Status == result.Status switch
            {
                OperationStatus.Denied => "Clipboard read denied.",
                OperationStatus.Unsupported => "Clipboard read unsupported.",
                OperationStatus.Cancelled => "Clipboard read dismissed by provider.",
                _ => "Clipboard read failed."
            }, "Provider result is visibly distinguished.");
            Assert(!h.Controller.State.Error.Contains("PRIVATE"), "Provider diagnostics do not leak clipboard content.");
        }
        h.Services.Write = _ => Task.FromResult(OperationResult<bool>.Completed(false));
        h.Run(h.Controller.WriteClipboard);
        Assert(h.Controller.State.Status == "Clipboard write failed.", "False write acknowledgement is not success.");
        h.Services.Launch = _ => throw new IOException("PRIVATE_URI_CONTENT");
        h.Run(h.Controller.Launch);
        Assert(h.Controller.State.Status == "HTTPS launch failed." && !h.Controller.State.Error.Contains("PRIVATE"), "Thrown provider errors are observed and sanitized.");
        h.Services.Read = _ => Task.FromResult(OperationResult<string>.Completed(new string('x', 4097)));
        h.Run(h.Controller.ReadClipboard);
        Assert(h.Controller.State.Status == "Clipboard read failed." && h.Controller.State.ClipboardLength is null,
            "Oversized clipboard response fails without retaining contents.");
        foreach (var result in new[] { OperationResult<PickedFile>.Cancelled(), OperationResult<PickedFile>.Denied(),
            OperationResult<PickedFile>.Unsupported(), OperationResult<PickedFile>.Failed(new IOException("PRIVATE_FILE_CONTENT")) })
        {
            h.Picker.Open = (_, _) => Task.FromResult(result);
            h.Run(h.Controller.PickFile);
            Assert(h.Controller.State.Status != "File inspection completed." &&
                h.Controller.State.FileByteCount is null && !h.Controller.State.Error.Contains("PRIVATE"), "File dismissal/failure is explicit with no fabricated data.");
        }
        Assert(h.Errors.Count == 0, "Expected service outcomes are displayed rather than abandoned tasks.");
    }
    private static void FileChecks()
    {
        using var h = new Harness();
        foreach (int length in new[] { 0, 12, 4096, 65535, 65536, 65537, 100000 })
        {
            var stream = new TrackingStream(new byte[length]);
            h.Picker.Open = (options, _) => Task.FromResult(OperationResult<PickedFile>.Completed(
                new PickedFile("untrusted\0\nlabel.txt", stream, options, length: null)));
            h.Run(h.Controller.PickFile);
            Assert(stream.Disposals == 1 && stream.BytesRead <= 65537,
                "Single owned stream is disposed exactly once within 64 KiB plus internal sentinel budget.");
            Assert(h.Controller.State.FileName == "untrusted  label.txt", "Only bounded sanitized metadata is displayed.");
            if (length <= 65536)
                Assert(h.Controller.State.Status == "File inspection completed." && h.Controller.State.FileByteCount == length &&
                    !h.Controller.State.FileTooLarge, "Exact byte count is measured from the stream, not advisory metadata.");
            else
                Assert(h.Controller.State.Status == "File inspection failed." && h.Controller.State.FileByteCount is null &&
                    h.Controller.State.FileTooLarge && h.Controller.State.Error == "File exceeds the 65536-byte inspection limit.",
                    "Oversize never appears as a truncated successful inspection.");
        }
        var advisory = new TrackingStream(new byte[20]);
        h.Picker.Open = (options, _) => Task.FromResult(OperationResult<PickedFile>.Completed(
            new PickedFile(new string('n', 200), advisory, options, length: 2)));
        h.Run(h.Controller.PickFile);
        Assert(h.Controller.State.FileByteCount == 20 && h.Controller.State.FileName.Length == 120,
            "Untrusted length is not treated as actual bytes and label size is bounded.");
        var badRead = new TrackingStream(new byte[20]) { FailRead = true };
        h.Picker.Open = (options, _) => Task.FromResult(OperationResult<PickedFile>.Completed(new PickedFile("example", badRead, options)));
        h.Run(h.Controller.PickFile);
        Assert(badRead.Disposals == 1 && h.Controller.State.Status == "File inspection failed." &&
            !h.Controller.State.Error.Contains("PRIVATE") && h.Controller.State.FileByteCount is null,
            "Read failures dispose content and cannot expose its contents or stale prior inspection.");
        var badDispose = new TrackingStream(new byte[20]) { FailDispose = true };
        h.Picker.Open = (options, _) => Task.FromResult(OperationResult<PickedFile>.Completed(new PickedFile("example", badDispose, options)));
        h.Run(h.Controller.PickFile);
        Assert(badDispose.Disposals == 1 && h.Controller.State.Status == "File inspection failed." &&
            !h.Controller.State.Error.Contains("PRIVATE"), "Cleanup failure cannot become success or leak provider details.");
        var smallerBudget = new TrackingStream(new byte[50]);
        h.Picker.Open = (_, _) => Task.FromResult(OperationResult<PickedFile>.Completed(
            new PickedFile("provider-limited", smallerBudget, new FileSelectionOptions(32))));
        h.Run(h.Controller.PickFile);
        Assert(smallerBudget.Disposals == 1 && smallerBudget.BytesRead == 33 &&
            h.Controller.State.Status == "File inspection failed." && !h.Controller.State.FileTooLarge &&
            h.Controller.State.FileByteCount is null,
            "A stricter provider stream budget is respected without falsely claiming a 64 KiB file overflow.");
    }
    private static void CancellationChecks()
    {
        using var h = new Harness();
        var pending = new TaskCompletionSource<OperationResult<PickedFile>>(TaskCreationOptions.RunContinuationsAsynchronously);
        h.Picker.Open = (_, _) => pending.Task;
        h.Gesture.Run(h.Controller.PickFile);
        h.Controller.Cancel();
        Assert(h.Picker.Token.IsCancellationRequested && h.Controller.State.Busy &&
            h.Controller.State.CancelRequested && !h.Controller.LastOperation.IsCompleted, "Cancel signals picker and waits for owned-result cleanup.");
        Throws<InvalidOperationException>(() => h.Gesture.Run(h.Controller.ReadClipboard));
        var late = new TrackingStream(new byte[15]);
        pending.SetResult(OperationResult<PickedFile>.Completed(new PickedFile("late", late, new FileSelectionOptions(65536))));
        h.Dispatcher.Finish(h.Controller.LastOperation);
        Assert(late.Disposals == 1 && late.ReadCalls == 0 && h.Controller.State.FileByteCount is null &&
            h.Controller.State.Status == "File inspection canceled. A completed external action cannot be undone.",
            "Late transferred file after cancel is disposed before returning without reading or stale UI.");

        var gate = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var reading = new TrackingStream(new byte[50]) { ReadGate = gate.Task };
        h.Picker.Open = (options, _) => Task.FromResult(OperationResult<PickedFile>.Completed(new PickedFile("reading", reading, options)));
        h.Gesture.Run(h.Controller.PickFile);
        h.Controller.Cancel();
        gate.SetResult();
        h.Dispatcher.Finish(h.Controller.LastOperation);
        Assert(reading.Disposals == 1 && h.Controller.State.FileByteCount is null, "Cancel during stream reading disposes ownership and drops partial count.");

        var clipboard = new TaskCompletionSource<OperationResult<string>>(TaskCreationOptions.RunContinuationsAsynchronously);
        h.Services.Read = _ => clipboard.Task;
        h.Gesture.Run(h.Controller.ReadClipboard);
        Task closing = h.Controller.LastOperation;
        var originalLifetime = h.App.Lifetime;
        h.Host.Dispose();
        clipboard.SetResult(OperationResult<string>.Completed("PRIVATE_CLIPBOARD_CONTENT"));
        h.Dispatcher.Finish(closing);
        Assert(originalLifetime.Token.IsCancellationRequested && h.Services.Token.IsCancellationRequested &&
            h.Errors.Count == 0 && h.Backend.Peers.All(peer => peer.Disposed), "Late clipboard result cannot mutate disposed root.");
        Assert(!h.Services.Disposed && h.Picker.Disposals == 0, "Provider disposal remains caller responsibility after pending work settles.");

        using var terminal = new Harness();
        terminal.Services.Read = _ => Task.FromException<OperationResult<string>>(new IOException("PRIVATE_CLIPBOARD_CONTENT"));
        terminal.Dispatcher.RejectPost = true;
        terminal.Gesture.Run(terminal.Controller.ReadClipboard);
        terminal.Dispatcher.Finish(terminal.Controller.LastOperation);
        Assert(terminal.Errors.Count == 1 && !terminal.Errors[0].ToString().Contains("PRIVATE"),
            "Terminal delivery failure reaches sanitized host reporter, never a silent task.");

        using var retired = new Harness();
        var retiredPick = new TaskCompletionSource<OperationResult<PickedFile>>(TaskCreationOptions.RunContinuationsAsynchronously);
        retired.Picker.Open = (_, _) => retiredPick.Task;
        retired.Gesture.Run(retired.Controller.PickFile);
        var retiredOperation = retired.Controller.LastOperation;
        retired.Host.Dispose();
        var cleanupFailure = new TrackingStream(new byte[5]) { FailDispose = true };
        retiredPick.SetResult(OperationResult<PickedFile>.Completed(
            new PickedFile("unused", cleanupFailure, new FileSelectionOptions(65536))));
        retired.Dispatcher.Finish(retiredOperation);
        Assert(cleanupFailure.Disposals == 1 && retired.Errors.Count == 1 &&
            !retired.Errors[0].ToString().Contains("PRIVATE"), "Cleanup failures after retirement are reported without content leakage.");
    }
    private static void CorpusChecks()
    {
        using var h = new Harness();
        int count = ApplicationScenarioRunner.Run(File.ReadAllText(Path.Combine(AppContext.BaseDirectory, "PlatformServicesScenarios.json")),
            "platform-services", new Driver(h));
        assertions += count;
        Assert(count >= 25, "Workbench has shared literal native-integration scenarios.");
        Assert(h.Services.LastUri?.Host == "example.com" && h.Controller.State.FileByteCount == 12 &&
            h.Controller.State.ClipboardLength == 14 && !h.Controller.State.Busy, "Literal fixture finishes with fake service metadata only.");
        Console.WriteLine($"Services corpus: {count} literal expectations.");
    }
    private static void UiContextChecks()
    {
        var previous = SynchronizationContext.Current;
        using var h = new Harness();
        SynchronizationContext.SetSynchronizationContext(new UiContext(h.Dispatcher));
        try
        {
            int uiThread = Environment.CurrentManagedThreadId;
            var selected = new TaskCompletionSource<OperationResult<PickedFile>>(TaskCreationOptions.RunContinuationsAsynchronously);
            var gate = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
            var stream = new TrackingStream(new byte[30]) { RequiredThread = uiThread, ReadGate = gate.Task };
            h.Picker.Open = (_, _) => selected.Task;
            h.Gesture.Run(h.Controller.PickFile);
            Task operation = h.Controller.LastOperation;
            selected.SetResult(OperationResult<PickedFile>.Completed(new PickedFile("ui-bound", stream, new FileSelectionOptions(65536))));
            gate.SetResult();
            h.Dispatcher.Finish(operation);
            Assert(stream.Disposals == 1 && h.Controller.State.FileByteCount == 30, "Awaited picker, stream reads, and cleanup retain platform UI context.");

            selected = new(TaskCreationOptions.RunContinuationsAsynchronously);
            var lateStream = new TrackingStream(new byte[30]) { RequiredThread = uiThread };
            h.Picker.Open = (_, _) => selected.Task;
            h.Gesture.Run(h.Controller.PickFile);
            operation = h.Controller.LastOperation;
            h.Host.Dispose();
            Assert(h.Picker.Disposals == 0, "Native picker remains caller-owned during late result cleanup.");
            selected.SetResult(OperationResult<PickedFile>.Completed(new PickedFile("late-ui-bound", lateStream, new FileSelectionOptions(65536))));
            h.Dispatcher.Finish(operation);
            Assert(lateStream.Disposals == 1 && lateStream.ReadCalls == 0 && h.Errors.Count == 0,
                "Late UI-bound transferred file is disposed via captured context even after Host.Dispose.");
        }
        finally { SynchronizationContext.SetSynchronizationContext(previous); }
    }
    private static void OwnershipChecks()
    {
        using (var h = new Harness())
        {
            var pending = new TaskCompletionSource<OperationResult<string>>(TaskCreationOptions.RunContinuationsAsynchronously);
            h.Services.Read = _ => pending.Task;
            h.Gesture.Run(h.Controller.ReadClipboard);
            h.Host.Detach();
            Assert(!h.App.Lifetime.Token.IsCancellationRequested && !h.Services.Token.IsCancellationRequested,
                "Native detach does not retire the owning workbench or cancel its operation.");
            pending.SetResult(OperationResult<string>.Completed("four"));
            h.Dispatcher.Finish(h.Controller.LastOperation);
            var replacement = new Backend();
            h.Host.Attach(replacement);
            Assert(((Label)replacement.Find("services-clipboard-summary").Element).Text ==
                "Clipboard read: 4 UTF-16 units. Content not displayed.", "Reattached view presents retained plain metadata.");
            pending = new(TaskCreationOptions.RunContinuationsAsynchronously);
            h.Services.Read = _ => pending.Task;
            h.Gesture.Run(h.Controller.ReadClipboard);
            replacement.FailUpdateId = "services-status";
            pending.SetResult(OperationResult<string>.Completed("four"));
            h.Dispatcher.Finish(h.Controller.LastOperation);
            Assert(!h.Host.IsAttached && h.Errors.Count == 1 &&
                !h.Errors[0].ToString().Contains("PRIVATE"), "Native async update failure detaches and reaches sanitized reporter.");
        }
        var dispatcher = new Dispatcher();
        var gesture = new Gesture();
        using var services = new Services(gesture);
        using var picker = new Picker(gesture);
        using var host = new Host(dispatcher);
        var errors = new List<Exception>();
        PlatformServicesWorkbench? app = null;
        KeyedStack pages;
        using (var build = host.BeginBuild())
        {
            var root = host.Stack(Axis.Vertical);
            pages = host.KeyedStack(Axis.Vertical);
            root.Add(pages);
            pages.Reconcile([KeyedItem.Create("services", h => app = PlatformServicesWorkbench.Create(h, services, picker, errors.Add))]);
            host.SetContent(root);
            build.Complete();
        }
        var backend = new Backend();
        host.Attach(backend);
        var workbench = app ?? throw new InvalidOperationException("Nested workbench missing.");
        var selected = new TaskCompletionSource<OperationResult<PickedFile>>(TaskCreationOptions.RunContinuationsAsynchronously);
        picker.Open = (_, _) => selected.Task;
        gesture.Run(workbench.Controller.PickFile);
        var operation = workbench.Controller.LastOperation;
        pages.Reconcile([]);
        Assert(host.IsAttached && workbench.Lifetime.Token.IsCancellationRequested && picker.Token.IsCancellationRequested &&
            picker.Disposals == 0 && !services.Disposed, "Keyed component retirement cancels work independently of host and provider lifetime.");
        var stream = new TrackingStream(new byte[50]);
        selected.SetResult(OperationResult<PickedFile>.Completed(new PickedFile("retired", stream, new FileSelectionOptions(65536))));
        dispatcher.Finish(operation);
        Assert(stream.Disposals == 1 && stream.ReadCalls == 0 && errors.Count == 0 &&
            backend.Peers.Count(peer => !peer.Disposed) == 2, "Removed component observes and disposes late owned results without recreating UI.");
        Throws<ObjectDisposedException>(() => workbench.Controller.ReadClipboard());
    }
}
