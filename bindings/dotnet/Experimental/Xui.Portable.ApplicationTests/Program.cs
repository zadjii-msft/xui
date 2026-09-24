using System.Collections.Concurrent;
using Xui.Experimental.Portable;

internal static class Program
{
    private static int assertions;
    private static readonly TimeSpan Timeout = TimeSpan.FromSeconds(5);

    private static void Main()
    {
        DeliveryAndErrors();
        CancellationAndLifetime();
        AccessAndArguments();
        LatestResultWins();
        CancellationErrors();
        NavigationOwnership();
        ServiceContracts();
        StorageRoundtrip();
        NativeInputContracts();
        ReentrantOwnership();
        Console.WriteLine($"Portable application work scopes: {assertions} assertions passed.");
    }

    private static void Assert(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
        assertions++;
    }

    private static void Throws<T>(Action action) where T : Exception
    {
        try { action(); }
        catch (T) { assertions++; return; }
        throw new InvalidOperationException($"Expected {typeof(T).Name}.");
    }

    private static void Await(Task task) => task.WaitAsync(Timeout).GetAwaiter().GetResult();
    private static TaskCompletionSource<int> Producer() => new(TaskCreationOptions.RunContinuationsAsynchronously);

    private static void DeliveryAndErrors()
    {
        var dispatcher = new Dispatcher();
        using var host = new Host(dispatcher);
        using var scope = new UiWorkScope(host);
        int result = 0;
        var source = Producer();
        var operation = scope.RunAsync(_ => source.Task, value =>
        {
            Assert(dispatcher.CheckAccess(), "Result is delivered on the UI thread.");
            result = value;
        });
        Task.Run(() => source.SetResult(42)).GetAwaiter().GetResult();
        dispatcher.WaitForPost();
        Assert(result == 0 && !operation.IsCompleted, "Delivery waits for the UI dispatcher.");
        dispatcher.Drain();
        Await(operation);
        Assert(result == 42, "The produced value is delivered exactly once.");
        dispatcher.Drain();
        Assert(result == 42, "Draining twice does not repeat delivery.");

        AwaitFailure<ApplicationException>(scope.RunAsync<int>(_ => throw new ApplicationException("producer"), _ => result++));
        AwaitFailure<ApplicationException>(scope.RunAsync<int>(_ => Task.FromException<int>(new ApplicationException("async producer")), _ => result++));
        AwaitFailure<InvalidOperationException>(scope.RunAsync<int>(_ => null!, _ => result++));
        var callback = scope.RunAsync(_ => Task.FromResult(1), _ => throw new ApplicationException("apply"));
        dispatcher.Drain();
        AwaitFailure<ApplicationException>(callback);
        dispatcher.Reject = true;
        AwaitFailure<InvalidOperationException>(scope.RunAsync(_ => Task.FromResult(1), _ => result++));
        dispatcher.Reject = false;
        Assert(result == 42, "Failures never invoke the success callback.");
    }

    private static void CancellationAndLifetime()
    {
        var dispatcher = new Dispatcher();
        using var host = new Host(dispatcher);
        using var scope = new UiWorkScope(host);
        int applies = 0;
        using (var cancellation = new CancellationTokenSource())
        {
            cancellation.Cancel();
            bool produced = false;
            var cancelled = scope.RunAsync(_ => { produced = true; return Task.FromResult(1); }, _ => applies++, cancellation.Token);
            AwaitFailure<OperationCanceledException>(cancelled);
            Assert(!produced, "Pre-cancelled work does not call the producer.");
        }
        using (var cancellation = new CancellationTokenSource())
        {
            var source = Producer();
            var cancelled = scope.RunAsync(_ => source.Task, _ => applies++, cancellation.Token);
            cancellation.Cancel();
            AwaitFailure<OperationCanceledException>(cancelled);
            source.SetResult(1);
            Assert(applies == 0, "A late producer cannot deliver after cancellation.");
        }
        using (var cancellation = new CancellationTokenSource())
        {
            var queued = scope.RunAsync(_ => Task.FromResult(1), _ => applies++, cancellation.Token);
            cancellation.Cancel();
            AwaitFailure<OperationCanceledException>(queued);
            dispatcher.Drain();
            Assert(applies == 0, "Cancellation rejects an already queued UI update.");
        }
        var pending = Producer();
        CancellationToken supplied = default;
        var ignored = scope.RunAsync(token => { supplied = token; return pending.Task; }, _ => applies++);
        var queuedAtDispose = scope.RunAsync(_ => Task.FromResult(1), _ => applies++);
        scope.Dispose();
        Assert(supplied.IsCancellationRequested, "Scope disposal cancels producers.");
        AwaitFailure<OperationCanceledException>(ignored);
        AwaitFailure<OperationCanceledException>(queuedAtDispose);
        pending.SetResult(7);
        dispatcher.Drain();
        Assert(applies == 0, "Disposed scopes cannot publish stale results.");
        Throws<ObjectDisposedException>(() => scope.RunAsync(_ => Task.FromResult(1), _ => applies++));
        scope.Dispose();
        assertions++;

        using var wrongLifetimeHost = new Host(dispatcher);
        using var wrongLifetimeScope = new UiWorkScope(wrongLifetimeHost);
        var afterHost = wrongLifetimeScope.RunAsync(_ => Task.FromResult(1), _ => applies++);
        wrongLifetimeHost.Dispose();
        dispatcher.Drain();
        AwaitFailure<ObjectDisposedException>(afterHost);
        Assert(applies == 0, "Host disposal never permits a queued application callback.");
    }

    private static void AccessAndArguments()
    {
        var dispatcher = new Dispatcher();
        using var host = new Host(dispatcher);
        using var scope = new UiWorkScope(host);
        Throws<ArgumentNullException>(() => new UiWorkScope(null!));
        Throws<ArgumentNullException>(() => scope.RunAsync<int>(null!, _ => { }));
        Throws<ArgumentNullException>(() => scope.RunAsync(_ => Task.FromResult(1), null!));
        Task.Run(() =>
        {
            Throws<InvalidOperationException>(() => scope.RunAsync(_ => Task.FromResult(1), _ => { }));
            Throws<InvalidOperationException>(scope.Dispose);
        }).GetAwaiter().GetResult();
        var failure = new Dispatcher { WrongThreadDelivery = true };
        using var wrongHost = new Host(failure);
        using var wrongScope = new UiWorkScope(wrongHost);
        var wrong = wrongScope.RunAsync(_ => Task.FromResult(1), _ => throw new Exception("Should not run."));
        failure.Drain();
        AwaitFailure<InvalidOperationException>(wrong);
    }

    private static void LatestResultWins()
    {
        var dispatcher = new Dispatcher();
        using var host = new Host(dispatcher);
        using var latest = new LatestUiWork(host);
        int applied = 0;
        var first = Producer();
        CancellationToken oldToken = default;
        var old = latest.RunAsync(token => { oldToken = token; return first.Task; }, value => applied = value);
        var next = latest.RunAsync(_ => Task.FromResult(2), value => applied = value);
        Assert(oldToken.IsCancellationRequested, "Replacing a request cancels the old producer.");
        AwaitFailure<OperationCanceledException>(old);
        first.SetResult(1);
        dispatcher.Drain();
        Await(next);
        Assert(applied == 2, "A late old result cannot overwrite the newest result.");

        var queuedOld = latest.RunAsync(_ => Task.FromResult(3), value => applied = value);
        var queuedNew = latest.RunAsync(_ => Task.FromResult(4), value => applied = value);
        AwaitFailure<OperationCanceledException>(queuedOld);
        dispatcher.Drain();
        Await(queuedNew);
        Assert(applied == 4, "Superseded queued UI callbacks cannot publish.");
        AwaitFailure<ApplicationException>(latest.RunAsync<int>(_ => throw new ApplicationException(), _ => applied = -1));
        var recovery = latest.RunAsync(_ => Task.FromResult(5), value => applied = value);
        dispatcher.Drain();
        Await(recovery);
        Assert(applied == 5, "A later request works after a surfaced producer failure.");
        latest.Dispose();
        Throws<ObjectDisposedException>(() => latest.RunAsync(_ => Task.FromResult(6), _ => applied = -1));
    }

    private static void CancellationErrors()
    {
        var dispatcher = new Dispatcher();
        using var host = new Host(dispatcher);
        using var scope = new UiWorkScope(host);
        var source = Producer();
        CancellationTokenRegistration callback = default;
        var work = scope.RunAsync(token =>
        {
            callback = token.Register(() => throw new ApplicationException("Cancellation callback failed."));
            return source.Task;
        }, _ => throw new Exception("Cancelled result must not be applied."));
        try
        {
            Throws<AggregateException>(scope.Dispose);
            AwaitFailure<OperationCanceledException>(work);
            source.SetResult(1);
            dispatcher.Drain();
            Throws<ObjectDisposedException>(() => scope.RunAsync(_ => source.Task, _ => { }));
        }
        finally { callback.Dispose(); }
    }

    private static void NavigationOwnership()
    {
        var dispatcher = new Dispatcher();
        using var host = new Host(dispatcher);
        using var navigation = new NavigationStack<string>(host);
        var released = new List<string>();
        int changed = 0;
        navigation.Changed += () => changed++;
        Assert(navigation.Current is null && !navigation.Back(), "Empty navigation has no previous page.");
        var firstResources = new Resource(() => released.Add("first"));
        var first = navigation.Push("home", "draft", firstResources);
        var second = navigation.Push("detail", "selected", new Resource(() => released.Add("second")));
        Assert(navigation.CanGoBack && navigation.Current == second && first.Id != second.Id,
            "Navigation retains history and distinct entry identity.");
        Throws<InvalidOperationException>(() => navigation.Push("duplicate-owner", "", firstResources));
        Throws<ArgumentException>(() => navigation.Push(" ", ""));
        Assert(navigation.Entries.Count == 2 && changed == 2, "Rejected navigation preserves live entries.");
        Assert(navigation.Back() && navigation.Current == first && first.State == "draft", "Back restores the previous authored state.");
        Assert(second.IsRetired && second.Lifetime.IsCancellationRequested && released.SequenceEqual(["second"]),
            "Back cancels and releases only the retired page.");
        var replacement = navigation.Replace("home", "replacement", new Resource(() => released.Add("replacement")));
        Assert(first.IsRetired && first.Lifetime.IsCancellationRequested && replacement.Id != first.Id,
            "Replacement gets a new identity and retires the old entry.");
        var bad = navigation.Push("broken", "", new Resource(() => throw new ApplicationException("cleanup")));
        var additional = navigation.Push("additional", "", new Resource(() => released.Add("additional")));
        Throws<AggregateException>(() => navigation.Reset("fresh", "fresh"));
        Assert(bad.IsRetired && additional.IsRetired && replacement.IsRetired &&
            navigation.Current?.Route == "fresh" && navigation.Entries.Count == 1,
            "Cleanup failures cannot restore retired navigation or strand later cleanup.");
        Assert(released.SequenceEqual(["second", "first", "additional", "replacement"]), "Reset cleans pages in reverse order.");
        Action reenter = () => navigation.Push("nested", "");
        navigation.Changed += reenter;
        Throws<AggregateException>(() => navigation.Push("outer", ""));
        Assert(navigation.Current?.Route == "outer" && navigation.Entries.Count == 2,
            "Observer reentrancy fails explicitly after the original commit.");
        navigation.Changed -= reenter;
        var current = navigation.Current!;
        navigation.Dispose();
        Assert(current.IsRetired && current.Lifetime.IsCancellationRequested, "Disposal cancels every current page.");
        Throws<ObjectDisposedException>(() => _ = navigation.Current);
        navigation.Dispose();
    }

    private sealed class Resource(Action release) : IDisposable
    {
        public void Dispose() => release();
    }

    private static void ServiceContracts()
    {
        var complete = OperationResult<int>.Completed(0);
        Assert(complete.Status == OperationStatus.Completed && complete.Value == 0 && complete.Error is null,
            "A legitimate default value remains distinguishable from a failed operation.");
        var failure = new ApplicationException("platform failure");
        foreach (var result in new[] { OperationResult<int>.Cancelled(), OperationResult<int>.Denied(),
            OperationResult<int>.Unsupported(), OperationResult<int>.Failed(failure) })
            Throws<InvalidOperationException>(() => _ = result.Value);
        Assert(ReferenceEquals(OperationResult<int>.Failed(failure).Error, failure), "Platform failures preserve their original error.");
        Throws<ArgumentNullException>(() => OperationResult<string>.Completed(null!));
        Throws<ArgumentNullException>(() => OperationResult<string>.Failed(null!));
        foreach (string value in new[] { "https://example.test/path", "http://127.0.0.1:5190/", "mailto:ada@example.test", "tel:+15555550123" })
        {
            PlatformServicePolicy.ValidateLaunchUri(new Uri(value));
            assertions++;
        }
        foreach (string value in new[] { "file:///C:/test.exe", "javascript:alert(1)", "data:text/html,test", "https://user:password@example.test" })
            Throws<ArgumentException>(() => PlatformServicePolicy.ValidateLaunchUri(new Uri(value)));
        Throws<ArgumentException>(() => PlatformServicePolicy.ValidateLaunchUri(new Uri("relative", UriKind.Relative)));
        Throws<ArgumentNullException>(() => PlatformServicePolicy.ValidateLaunchUri(null!));
        Throws<ArgumentNullException>(() => PlatformServicePolicy.ValidateClipboardText(null!));
        Throws<ArgumentException>(() => PlatformServicePolicy.ValidateClipboardText("bad\0text"));
        PlatformServicePolicy.ValidateClipboardText("multi\nline \u03bb");
        assertions++;
        Throws<ArgumentOutOfRangeException>(() => PlatformServicePolicy.ValidateCapability((ServiceCapability)999));
    }

    private static void StorageRoundtrip()
    {
        string root = Path.Combine(Path.GetTempPath(), "xui-storage-" + Guid.NewGuid().ToString("N"));
        try
        {
            var storage = new DirectoryApplicationStorage(root, 128);
            var missing = storage.ReadAsync("draft").GetAwaiter().GetResult();
            Assert(missing.Status == OperationStatus.Completed && !missing.Value.Exists, "Missing documents are not empty-value successes.");
            var data = System.Text.Encoding.UTF8.GetBytes("draft \u03bb\n");
            Assert(storage.WriteAsync("draft", data).GetAwaiter().GetResult().Value, "Native document write completes.");
            var read = storage.ReadAsync("draft").GetAwaiter().GetResult().Value;
            Assert(read.Exists && read.Data.Span.SequenceEqual(data), "Native storage preserves exact bytes.");
            Assert(storage.WriteAsync("draft", ReadOnlyMemory<byte>.Empty).GetAwaiter().GetResult().Value, "Empty documents are valid values.");
            var empty = storage.ReadAsync("draft").GetAwaiter().GetResult().Value;
            Assert(empty.Exists && empty.Data.Length == 0, "Existing empty and missing documents remain distinct.");
            foreach (string key in new[] { "", "..", "../escape", "a/b", "a\\b", "Upper", "name.data", new string('a', 129) })
                Throws<ArgumentException>(() => storage.ReadAsync(key));
            Throws<ArgumentNullException>(() => storage.ReadAsync(null!));
            Throws<ArgumentOutOfRangeException>(() => storage.WriteAsync("draft", new byte[129]));
            using var cancellation = new CancellationTokenSource();
            cancellation.Cancel();
            Throws<OperationCanceledException>(() => storage.WriteAsync("draft", data, cancellation.Token));
            Assert(storage.ReadAsync("draft").GetAwaiter().GetResult().Value.Data.Length == 0, "Cancelled writes preserve the previous document.");
            File.WriteAllBytes(Path.Combine(root, "large.data"), new byte[129]);
            Assert(storage.ReadAsync("large").GetAwaiter().GetResult().Status == OperationStatus.Failed, "Oversized stored data fails explicitly.");
            Directory.CreateDirectory(Path.Combine(root, "blocked.data"));
            Assert(storage.WriteAsync("blocked", data).GetAwaiter().GetResult().Status is OperationStatus.Failed or OperationStatus.Denied,
                "A failed atomic replacement is not reported as success.");
            if (OperatingSystem.IsWindows())
            {
                using (var locked = new FileStream(Path.Combine(root, "draft.data"), FileMode.Open, FileAccess.Read, FileShare.None))
                    Assert(storage.WriteAsync("draft", data).GetAwaiter().GetResult().Status is OperationStatus.Failed or OperationStatus.Denied,
                        "A locked destination preserves explicit replacement failure.");
                Assert(storage.ReadAsync("draft").GetAwaiter().GetResult().Value.Data.Length == 0,
                    "Failed replacement preserves the original document.");
            }
            Assert(storage.DeleteAsync("draft").GetAwaiter().GetResult().Value &&
                !storage.ReadAsync("draft").GetAwaiter().GetResult().Value.Exists, "Delete removes a document.");
            Assert(!Directory.EnumerateFiles(root).Any(path => Path.GetFileName(path).StartsWith(".pending-", StringComparison.Ordinal)),
                "Completed writes retain no temporary files.");
            Throws<ArgumentException>(() => new DirectoryApplicationStorage("relative"));
            Throws<ArgumentOutOfRangeException>(() => new DirectoryApplicationStorage(root, 0));
            Throws<ArgumentException>(() => new DirectoryApplicationStorage(Path.Combine(root, "large.data")));
            Throws<ArgumentException>(() => new DirectoryApplicationStorage(Path.Combine(root, "large.data", "child")));
            var replacedAncestor = new DirectoryApplicationStorage(Path.Combine(root, "ancestor", "child"));
            File.WriteAllText(Path.Combine(root, "ancestor"), "not a directory");
            Assert(replacedAncestor.ReadAsync("draft").GetAwaiter().GetResult().Status is OperationStatus.Failed or OperationStatus.Denied,
                "A storage ancestor replaced by a file is not a missing document.");
            Assert(replacedAncestor.DeleteAsync("draft").GetAwaiter().GetResult().Status is OperationStatus.Failed or OperationStatus.Denied,
                "Invalid storage ancestry cannot make delete appear successful.");
        }
        finally
        {
            if (Directory.Exists(root)) Directory.Delete(root, recursive: true);
        }
    }

    private static void NativeInputContracts()
    {
        var dispatcher = new Dispatcher();
        using var host = new Host(dispatcher);
        TextInput input;
        Label label;
        using (var build = host.BeginBuild())
        {
            var root = host.Stack(Axis.Vertical);
            input = host.TextInput("Name");
            input.Text = "A\ud83d\ude00BC";
            label = host.Label("Label");
            root.Add(input).Add(label);
            host.SetContent(root);
            build.Complete();
        }
        Throws<InvalidOperationException>(() => host.TryFocus(input));
        var backend = new InputBackend();
        host.Attach(backend);
        Assert(host.TryFocus(input) && host.HasFocus(input), "Host focus targets the current native peer.");
        Assert(!host.TryFocus(label), "Nonfocusable controls reject the request explicitly.");
        host.SetSelection(input, new TextSelection(2, 2));
        Assert(host.GetSelection(input) == new TextSelection(1, 1), "A caret cannot split a UTF-16 surrogate pair.");
        host.SetSelection(input, new TextSelection(2, 99));
        Assert(host.GetSelection(input) == new TextSelection(1, 5), "Selection clamps and includes complete surrogate pairs.");
        host.SetSelection(input, new TextSelection(3, 1));
        Assert(host.GetSelection(input) == new TextSelection(1, 3), "Selection ranges are ordered.");
        Throws<ArgumentOutOfRangeException>(() => host.SetSelection(input, new TextSelection(-1, 0)));
        input.Enabled = false;
        Assert(!host.TryFocus(input), "Disabled input does not request native focus.");
        input.Enabled = true;
        input.Visible = false;
        Assert(!host.TryFocus(input), "Hidden input does not request native focus.");
        input.Visible = true;
        using var other = new Host(dispatcher);
        Throws<ArgumentException>(() => other.TryFocus(input));
        Task.Run(() => Throws<InvalidOperationException>(() => host.TryFocus(input))).GetAwaiter().GetResult();
        host.Detach();
        Throws<InvalidOperationException>(() => host.GetSelection(input));
        host.Attach(new InputBackend());
        Assert(!host.HasFocus(input) && host.GetSelection(input) == new TextSelection(0, 0), "New attachments do not reuse stale native focus state.");
        host.Dispose();
        Throws<ObjectDisposedException>(() => host.TryFocus(input));
    }

    private sealed class InputBackend : IBackend
    {
        public IElementPeer Create(Element element, IControlEvents events) => new InputPeer(element);
        public void Mount(IElementPeer root) { }
        public void Dispose() { }
    }

    private sealed class InputPeer(Element element) : ITextSelectionPeer
    {
        private TextSelection selection;
        public bool HasFocus { get; private set; }
        public bool TryFocus() => HasFocus = element is TextInput;
        public TextSelection Selection
        {
            get => selection;
            set => selection = value.ClampTo(((TextInput)element).Text);
        }
        public void AddChild(IElementPeer child) { }
        public void Update(ElementProperty property) { }
        public void Dispose() { }
    }

    private static void ReentrantOwnership()
    {
        var dispatcher = new Dispatcher();
        using var host = new Host(dispatcher);
        using (var latest = new LatestUiWork(host))
        {
            var pending = Producer();
            CancellationTokenRegistration registration = default;
            var first = latest.RunAsync(token =>
            {
                registration = token.Register(latest.Dispose);
                return pending.Task;
            }, _ => throw new Exception("Retired work cannot apply."));
            try
            {
                bool produced = false;
                Throws<ObjectDisposedException>(() => latest.RunAsync(_ => { produced = true; return Task.FromResult(2); }, _ => { }));
                Assert(!produced, "Cancellation callbacks cannot restart work after disposing its owner.");
                AwaitFailure<OperationCanceledException>(first);
                pending.SetResult(1);
            }
            finally { registration.Dispose(); }
        }
        using (var latest = new LatestUiWork(host))
        {
            var pending = Producer();
            CancellationTokenRegistration registration = default;
            bool nestedProduced = false;
            var first = latest.RunAsync(token =>
            {
                registration = token.Register(() => Throws<InvalidOperationException>(() =>
                    latest.RunAsync(_ => { nestedProduced = true; return Task.FromResult(99); }, _ => { })));
                return pending.Task;
            }, _ => throw new Exception("Retired work cannot apply."));
            try
            {
                int value = 0;
                var replacement = latest.RunAsync(_ => Task.FromResult(2), result => value = result);
                dispatcher.Drain();
                Await(replacement);
                AwaitFailure<OperationCanceledException>(first);
                Assert(!nestedProduced && value == 2, "Reentrant cancellation cannot orphan a nested request.");
                pending.SetResult(1);
            }
            finally { registration.Dispose(); }
        }
        using var navigation = new NavigationStack<int>(host);
        navigation.Push("home", 0);
        int retiredNotifications = 0;
        Action handler = () => retiredNotifications++;
        navigation.Changed += handler;
        navigation.Push("detail", 1, new Resource(() => navigation.Changed -= handler));
        retiredNotifications = 0;
        Assert(navigation.Back(), "Page resources can unsubscribe while Back retires them.");
        navigation.Push("later", 2);
        Assert(retiredNotifications == 0, "Retired page handlers receive no later navigation.");
        navigation.Dispose();
        navigation.Changed -= handler;
        assertions++;
    }

    private static void AwaitFailure<T>(Task task) where T : Exception => Throws<T>(() => Await(task));

    private sealed class Dispatcher : IUiDispatcher
    {
        private readonly int thread = Environment.CurrentManagedThreadId;
        private readonly ConcurrentQueue<Action> queue = new();
        private readonly AutoResetEvent posted = new(false);
        internal bool Reject { get; set; }
        internal bool WrongThreadDelivery { get; set; }
        public bool CheckAccess() => Environment.CurrentManagedThreadId == thread;
        public void Post(Action action)
        {
            if (Reject) throw new InvalidOperationException("Dispatcher is unavailable.");
            queue.Enqueue(action);
            posted.Set();
        }
        internal void WaitForPost()
        {
            if (!posted.WaitOne(Timeout)) throw new TimeoutException("No work reached the dispatcher.");
        }
        internal void Drain()
        {
            while (queue.TryDequeue(out var action))
            {
                if (WrongThreadDelivery) Task.Run(action).GetAwaiter().GetResult();
                else action();
            }
        }
    }
}
