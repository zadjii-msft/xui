using PortableMutation;
using Xui.Experimental.Portable;

internal static partial class Program
{
    private static void ComponentLifetimeChecks()
    {
        LifetimeRetentionAndOrder();
        LifetimeCleanupFailures();
        LifetimeFactoryRollback();
        LifetimeQueuedWork();
        LifetimeCommittedFailures();
        LifetimeSubscriptionCleanup();
    }

    private static void LifetimeRetentionAndOrder()
    {
        var log = new List<string>();
        using var host = new Host(new Dispatcher());
        var board = new MutationBoard(host);
        var backend = new MutationBackend();
        host.Attach(backend);
        var parent = board.Lifetime;
        var parentToken = parent.Token;
        parent.Token.Register(() => log.Add("parent-cancel"));
        parent.Own(new LifetimeResource(() => log.Add("parent-first")));
        var parentLast = parent.Own(new LifetimeResource(() => log.Add("parent-last")));
        Assert(ReferenceEquals(parent, board.Lifetime), "Generated component lifetime identity is stable.");
        Throws<InvalidOperationException>(() => parent.Own(parentLast));
        Throws<ArgumentNullException>(() => parent.Own<IDisposable>(null!));
        Throws<InvalidOperationException>(() => host.GetComponentLifetime(board.CountLabel));
        MutationRow? row = null;
        var item = KeyedItem.Create("owned", h => row = new MutationRow(h, "owned"));
        board.Rows = [item];
        var child = row!.Lifetime;
        var originalRow = row;
        var childToken = child.Token;
        Throws<InvalidOperationException>(() => child.Own(parentLast));
        child.Token.Register(() => log.Add("child-cancel"));
        child.Own(new LifetimeResource(() => log.Add("child-first")));
        child.Own(new LifetimeResource(() => log.Add("child-last")));
        host.Detach();
        Assert(log.Count == 0 && !parentToken.IsCancellationRequested && !childToken.IsCancellationRequested, "Detach retains component cancellation and owned resources.");
        backend = new MutationBackend();
        host.Attach(backend);
        board.Rows = [item];
        Assert(ReferenceEquals(child, row.Lifetime) && log.Count == 0, "Unchanged keyed identity retains lifetime and resources.");
        backend.BeforeUnmount = () => Assert(childToken.IsCancellationRequested &&
            log.SequenceEqual(["child-cancel", "child-last", "child-first"]), "Child cancellation and reverse resource release precede native unmount.");
        board.Rows = [];
        Assert(log.SequenceEqual(["child-cancel", "child-last", "child-first"]), "Permanent row removal retires resources once.");
        Assert(!parentToken.IsCancellationRequested, "Removing a child does not retire its parent.");
        var rejected = new LifetimeResource(() => throw new Exception("Caller-owned rejected resource must not be disposed by the component."));
        Throws<ObjectDisposedException>(() => child.Own(rejected));
        Assert(rejected.Calls == 0, "Late ownership rejection leaves ownership with the caller.");
        Assert(child.Token.IsCancellationRequested, "A captured lifetime token remains readable after retirement.");
        Assert(ReferenceEquals(child, originalRow.Lifetime) && originalRow.Lifetime.Token.IsCancellationRequested, "The generated getter returns an existing retired lifetime without creating another owner.");
        backend.BeforeUnmount = null;
        board.Rows = [item];
        Assert(!ReferenceEquals(child, row!.Lifetime) && !row.Lifetime.Token.IsCancellationRequested, "Readded keys receive fresh component lifetimes.");
        var replacement = row.Lifetime;
        replacement.Token.Register(() => log.Add("replacement-cancel"));
        replacement.Own(new LifetimeResource(() => log.Add("replacement-resource")));
        backend.BeforeUnmount = () => Assert(parentToken.IsCancellationRequested && replacement.Token.IsCancellationRequested, "Host disposal cancels every component before native unmount.");
        host.Dispose();
        Assert(log.SequenceEqual(["child-cancel", "child-last", "child-first",
            "replacement-cancel", "replacement-resource", "parent-cancel", "parent-last", "parent-first"]), "Nested component lifetimes retire children before parents and reverse each resource list.");
        Assert(parentLast.Calls == 1 && backend.Peers.All(p => p.DisposeCount == 1), "Host disposal releases resources and peers exactly once.");
        Assert(ReferenceEquals(parent, board.Lifetime) && board.Lifetime.Token.IsCancellationRequested, "A cached generated lifetime remains readable after host disposal.");
        host.Dispose();
        Assert(parentLast.Calls == 1, "Repeated host disposal does not repeat resource cleanup.");
    }

    private static void LifetimeCleanupFailures()
    {
        using var host = new Host(new Dispatcher());
        var board = new MutationBoard(host);
        var backend = new MutationBackend();
        host.Attach(backend);
        MutationRow? row = null;
        board.Rows = [KeyedItem.Create("errors", h => row = new MutationRow(h, "errors"))];
        var lifetime = row!.Lifetime;
        int callbacks = 0;
        lifetime.Token.Register(() => { callbacks++; throw new ApplicationException("cancel-one"); });
        lifetime.Token.Register(() => { callbacks++; throw new ApplicationException("cancel-two"); });
        var first = lifetime.Own(new LifetimeResource(() => throw new ApplicationException("resource-one")));
        var second = lifetime.Own(new LifetimeResource(() => throw new ApplicationException("resource-two")));
        backend.BeforeUnmount = () => Assert(lifetime.Token.IsCancellationRequested && callbacks == 2 && first.Calls == 1 && second.Calls == 1, "All cancellation and resource failures are collected before native teardown.");
        backend.Failure = "dispose";
        var failure = Throws<KeyedUpdateException>(() => board.Rows = []);
        Assert(failure.ModelCommitted && !host.IsAttached && board.RowsView.Children.Count == 0, "Cleanup failure retains removal and detaches.");
        var aggregate = (AggregateException)failure.InnerException!;
        var messages = aggregate.Flatten().InnerExceptions.Select(error => error.Message).ToArray();
        foreach (string message in new[] { "cancel-one", "cancel-two", "resource-one", "resource-two", "dispose" })
            Assert(messages.Contains(message), $"Cleanup preserves failure '{message}'.");
        Assert(backend.Peers.All(p => p.DisposeCount == 1) && first.Calls == 1 && second.Calls == 1, "Cleanup errors cannot cause double resource or peer disposal.");
        host.Dispose();
        Assert(callbacks == 2 && first.Calls == 1, "Failed retirement remains terminal.");

        using var guarded = new Host(new Dispatcher());
        var guardedBoard = new MutationBoard(guarded);
        var guardedBackend = new MutationBackend();
        guarded.Attach(guardedBackend);
        var own = guardedBoard.Lifetime;
        bool guardedMutation = false, guardedDispose = false, guardedOwn = false;
        own.Token.Register(() =>
        {
            guardedMutation = Throws<InvalidOperationException>(() => guardedBoard.Next = 22) is not null;
            guardedDispose = Throws<InvalidOperationException>(() => guarded.Dispose()) is not null;
            guardedOwn = Throws<ObjectDisposedException>(() => own.Own(new LifetimeResource(() => { }))) is not null;
        });
        guarded.Dispose();
        Assert(guardedMutation && guardedDispose && guardedOwn, "Cancellation callbacks cannot mutate, reenter host disposal, or add resources.");

        using var failedHost = new Host(new Dispatcher());
        var failedBoard = new MutationBoard(failedHost);
        var failedNative = new MutationBackend { Failure = "dispose" };
        failedHost.Attach(failedNative);
        failedBoard.Lifetime.Token.Register(() => throw new ApplicationException("host-cancel"));
        var failedResource = failedBoard.Lifetime.Own(new LifetimeResource(() => throw new ApplicationException("host-resource")));
        var errors = Throws<AggregateException>(() => failedHost.Dispose()).Flatten().InnerExceptions.Select(error => error.Message).ToArray();
        Assert(errors.Contains("host-cancel") && errors.Contains("host-resource") && errors.Contains("dispose"), "Host disposal aggregates cancellation, resource, and native failures.");
        Assert(failedResource.Calls == 1 && failedNative.Peers.All(p => p.DisposeCount == 1), "Host disposal continues after every cleanup error.");
        Throws<ObjectDisposedException>(() => _ = failedBoard.Next);
    }

    private static void LifetimeFactoryRollback()
    {
        using var host = new Host(new Dispatcher());
        var board = new MutationBoard(host);
        var backend = new MutationBackend();
        host.Attach(backend);
        var original = board.Rows;
        ComponentLifetime? earlier = null, failing = null;
        LifetimeResource? earlierResource = null, failingResource = null;
        var failure = Throws<KeyedUpdateException>(() => board.Rows =
        [
            KeyedItem.Create("earlier", h =>
            {
                var row = new MutationRow(h, "earlier");
                earlier = row.Lifetime;
                earlierResource = earlier.Own(new LifetimeResource(() => { }));
                return row;
            }),
            KeyedItem.Create<MutationRow>("failed", h =>
            {
                var row = new MutationRow(h, "failed");
                failing = row.Lifetime;
                failing.Token.Register(() => throw new ApplicationException("rollback-cancel"));
                failingResource = failing.Own(new LifetimeResource(() => throw new ApplicationException("rollback-resource")));
                throw new ApplicationException("factory-original");
            })
        ]);
        Assert(!failure.ModelCommitted && ReferenceEquals(board.Rows, original) && host.IsAttached, "Rollback cleanup failure preserves the old tree and published descriptors.");
        Assert(earlier!.Token.IsCancellationRequested && failing!.Token.IsCancellationRequested &&
            earlierResource!.Calls == 1 && failingResource!.Calls == 1, "Failed factory rollback retires both current and earlier staged sibling lifetimes.");
        var messages = ((AggregateException)failure.InnerException!).Flatten().InnerExceptions.Select(error => error.Message).ToArray();
        Assert(messages.Contains("factory-original") && messages.Contains("rollback-cancel") && messages.Contains("rollback-resource"), "Rollback retains original construction and all cleanup errors.");
        board.Rows = [KeyedItem.Create("healthy", h => new MutationRow(h, "healthy"))];
        Assert(board.RowsView.Children.Count == 1, "Cleanup errors do not leave leaked build scopes.");

        using var rollbackHost = new Host(new Dispatcher());
        ComponentLifetime? nested = null;
        var scope = rollbackHost.BeginBuild();
        var child = new MutationRow(rollbackHost, "candidate");
        nested = child.Lifetime;
        nested.Token.Register(() => Throws<InvalidOperationException>(() => scope.Dispose()));
        var resource = nested.Own(new LifetimeResource(() => { }));
        scope.Dispose();
        Assert(nested.Token.IsCancellationRequested && resource.Calls == 1 && rollbackHost.Root is null, "Outer scope rollback retires completed nested components and rejects reentrant rollback.");
        _ = new MutationBoard(rollbackHost);

        using var constructionHost = new Host(new Dispatcher());
        ComponentLifetime? constructedChild = null;
        var construction = Throws<AggregateException>(() => _ = new LifetimeFailure(constructionHost, h =>
        {
            var row = new MutationRow(h, "nested-failure");
            constructedChild = row.Lifetime;
            constructedChild.Own(new LifetimeResource(() => throw new ApplicationException("constructor-cleanup")));
            return row.Root;
        }, null!));
        Assert(construction.Flatten().InnerExceptions.Any(error => error is ArgumentNullException) &&
            construction.Flatten().InnerExceptions.Any(error => error.Message == "constructor-cleanup"), "Generated constructor preserves original binding failure alongside rollback errors.");
        Assert(constructedChild!.Token.IsCancellationRequested && constructionHost.Root is null, "Failed outer generated construction retires completed child lifetimes.");
        _ = new MutationBoard(constructionHost);
    }

    private static void LifetimeQueuedWork()
    {
        var dispatcher = new Dispatcher();
        using var host = new Host(dispatcher);
        var board = new MutationBoard(host);
        MutationRow? row = null;
        board.Rows = [KeyedItem.Create("work", h => row = new MutationRow(h, "work"))];
        var lifetime = row!.Lifetime;
        var work = lifetime.Own(new UiWorkScope(host));
        int applied = 0;
        var task = work.RunAsync(_ => Task.FromResult("queued"), value => { applied++; row.Entry = value; }, lifetime.Token);
        Assert(!task.IsCompleted, "Owned work has queued its UI apply.");
        board.Rows = [];
        Throws<OperationCanceledException>(() => task.GetAwaiter().GetResult());
        dispatcher.Drain();
        Assert(applied == 0 && lifetime.Token.IsCancellationRequested, "Removed component cancels a queued apply without running authored work.");
        Throws<ObjectDisposedException>(() => work.RunAsync(_ => Task.FromResult(1), _ => { }));
        Task.Run(() => Assert(lifetime.Token.IsCancellationRequested, "Captured lifetime tokens can be observed off-thread after retirement.")).GetAwaiter().GetResult();
        var active = board.Lifetime;
        Task.Run(() => Throws<InvalidOperationException>(() => active.Own(new LifetimeResource(() => { })))).GetAwaiter().GetResult();
        int canceled = 0;
        active.Own(active.Token.Register(() => canceled++));
        host.Dispose();
        Assert(canceled == 1, "Value-type cancellation registrations can be explicitly owned and released after cancellation.");
    }

    private static void LifetimeCommittedFailures()
    {
        using var host = new Host(new Dispatcher());
        var board = new MutationBoard(host);
        var backend = new MutationBackend();
        host.Attach(backend);
        MutationRow? row = null;
        board.Rows = [KeyedItem.Create("typed", h => row = new MutationRow(h, "typed"))];
        var retired = row!.Lifetime;
        board.Rows = [KeyedItem.Create("typed", h => new MutationBanner(h, "typed-banner"))];
        Assert(retired.Token.IsCancellationRequested, "A same-key component type change retires the replaced lifetime.");
        MutationRow? neverOwned = null;
        board.Rows = [KeyedItem.Create("lazy", h => neverOwned = new MutationRow(h, "lazy"))];
        board.Rows = [];
        Throws<ObjectDisposedException>(() => _ = neverOwned!.Lifetime);

        MutationRow? retained = null;
        LifetimeResource? resource = null;
        backend.Failure = "insert";
        var failure = Throws<KeyedUpdateException>(() => board.Rows =
        [
            KeyedItem.Create("retained", h =>
            {
                retained = new MutationRow(h, "retained");
                resource = retained.Lifetime.Own(new LifetimeResource(() => { }));
                return retained;
            })
        ]);
        Assert(failure.ModelCommitted && !retained!.Lifetime.Token.IsCancellationRequested && resource!.Calls == 0, "Postcommit native failure retains the committed component's lifetime.");
        var recovery = new MutationBackend();
        host.Attach(recovery);
        recovery.Failure = "update";
        Throws<ApplicationException>(() => retained!.Entry = "committed text");
        Assert(!host.IsAttached && !retained!.Lifetime.Token.IsCancellationRequested && resource!.Calls == 0, "Property update failure detaches without retiring the component model.");
        host.Dispose();
        Assert(retained!.Lifetime.Token.IsCancellationRequested && resource!.Calls == 1, "Permanent host disposal later retires that retained lifetime once.");
    }

    private sealed class LifetimeResource(Action dispose) : IDisposable
    {
        public int Calls { get; private set; }
        public void Dispose() { Calls++; dispose(); }
    }

    private static void LifetimeSubscriptionCleanup()
    {
        using var host = new Host(new Dispatcher());
        TextInput input;
        Button button;
        Toggle toggle;
        CheckBox check;
        KeyedStack rows;
        using (var build = host.BeginBuild())
        {
            var root = host.Stack(Axis.Vertical);
            input = host.TextInput("Surviving input"); input.AutomationId = "surviving-input";
            button = host.Button("Surviving button"); button.AutomationId = "surviving-button";
            toggle = host.Toggle("Surviving toggle"); toggle.AutomationId = "surviving-toggle";
            check = host.CheckBox("Surviving checkbox"); check.AutomationId = "surviving-check";
            rows = host.KeyedStack(Axis.Vertical);
            root.Add(input).Add(button).Add(toggle).Add(check).Add(rows);
            host.SetContent(root);
            build.Complete();
        }
        var backend = new ViewportBackend();
        host.Attach(backend);
        MutationRow? row = null;
        rows.Reconcile([KeyedItem.Create("subscriber", h => row = new MutationRow(h, "subscriber"))]);
        int calls = 0;
        Action<string> changed = _ => calls++;
        Action submitted = () => calls++;
        Action<TextInteraction> interaction = _ => calls++;
        Action clicked = () => calls++;
        Action<bool> toggled = _ => calls++;
        Action<CheckState> checkedState = _ => calls++;
        input.Changed += changed;
        input.Submitted += submitted;
        input.InteractionChanged += interaction;
        button.Click += clicked;
        toggle.Changed += toggled;
        check.Changed += checkedState;
        row!.Lifetime.Own(new LifetimeResource(() =>
        {
            input.Changed -= changed;
            input.Submitted -= submitted;
            input.InteractionChanged -= interaction;
            button.Click -= clicked;
            toggle.Changed -= toggled;
            check.Changed -= checkedState;
            Throws<InvalidOperationException>(() => input.Changed += changed);
            Throws<InvalidOperationException>(() => input.Text = "forbidden cleanup mutation");
        }));
        rows.Reconcile([]);
        Assert(host.IsAttached, "Owned subscription cleanup may unsubscribe from surviving elements without failing retirement.");
        backend.Find("surviving-input").Events.Change("later");
        backend.Find("surviving-input").Events.Submit();
        ((ITextInteractionEvents)backend.Find("surviving-input").Events).InteractionChanged(new(true, false));
        backend.Find("surviving-button").Events.Click();
        ((IValueControlEvents)backend.Find("surviving-toggle").Events).ToggleChanged(true);
        ((IValueControlEvents)backend.Find("surviving-check").Events).CheckChanged(CheckState.Checked);
        Assert(calls == 0, "No retired subscriber remains reachable through a surviving control.");
        input.Changed += changed;
        var rejected = Throws<KeyedUpdateException>(() => rows.Reconcile(
        [
            KeyedItem.Create<MutationRow>("factory", h =>
            {
                input.Changed -= changed;
                return new MutationRow(h, "factory");
            })
        ]));
        Assert(!rejected.ModelCommitted, "A candidate factory cannot unsubscribe live listeners.");
        var rolledBack = Throws<KeyedUpdateException>(() => rows.Reconcile(
        [
            KeyedItem.Create<MutationRow>("rollback", h =>
            {
                var candidate = new MutationRow(h, "rollback");
                candidate.Lifetime.Own(new LifetimeResource(() => input.Changed -= changed));
                throw new ApplicationException("rollback");
            })
        ]));
        Assert(!rolledBack.ModelCommitted && host.IsAttached, "Candidate cleanup cannot remove subscriptions from the surviving tree.");
        backend.Find("surviving-input").Events.Change("still subscribed");
        Assert(calls == 1, "Rejected precommit removal preserves the live subscription.");
        host.Dispose();
        input.Changed -= changed;
        button.Click -= clicked;
        toggle.Changed -= toggled;
        check.Changed -= checkedState;
        Task.Run(() => Throws<InvalidOperationException>(() => input.Changed -= changed)).GetAwaiter().GetResult();
    }
}
