using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using Xui;

internal static class CallbackOwnershipTests
{
    private static int assertions;
    private static void Expect(bool value, string? message = null)
    {
        if (!value) throw new Exception(message ?? "Callback ownership assertion failed");
        ++assertions;
    }

    private static void Lifetime()
    {
        using var lifetime = new CallbackLifetime();
        int released = 0;
        var target = new object();
        var registration = lifetime.Register(target, () => ++released);
        Expect(lifetime.Count == 1 && !registration.Released);
        using (var call = registration.Enter())
        {
            Expect(call.Active && ReferenceEquals(GCHandle.FromIntPtr(registration.Context).Target, target));
            lifetime.Dispose();
            Expect(lifetime.Retired && lifetime.Count == 0 && released == 0 && !registration.Released);
            using var rejected = registration.Enter();
            Expect(!rejected.Active);
            registration.Dispose();
            Expect(released == 0);
        }
        Expect(registration.Released && released == 1);
        registration.Dispose();
        lifetime.Dispose();
        Expect(released == 1);
        try { lifetime.Register(target, () => ++released); throw new Exception("Expected retired scope rejection"); }
        catch (ObjectDisposedException) { ++assertions; }
        using var failedCleanup = new CallbackLifetime();
        var first = failedCleanup.Register(target, () => throw new InvalidOperationException("Controlled cleanup failure"));
        var second = failedCleanup.Register(target, () => ++released);
        try { failedCleanup.Dispose(); throw new Exception("Expected explicit cleanup failure"); }
        catch (AggregateException) { ++assertions; }
        Expect(first.Released && second.Released && released == 2 && failedCleanup.Retired,
            "Cleanup failure cannot strand other callback roots");
        using var cancellation = new CallbackLifetime();
        var sibling = cancellation.Register(target, () => { });
        cancellation.Register(target, () =>
        {
            using var rejected = sibling.Enter();
            Expect(!rejected.Active, "All registrations retire before cleanup can reenter a sibling");
        });
        cancellation.Dispose();
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static WeakReference Rollback(Window window, ContentHost host)
    {
        var marker = new object();
        var weak = new WeakReference(marker);
        using var candidate = host.BeginUpdate();
        var button = window.Button("Candidate event owner");
        button.Click += () => GC.KeepAlive(marker);
        var commands = window.ItemsView("Candidate menu owner");
        commands.OnContextMenu(() => { GC.KeepAlive(marker); return []; }, _ => GC.KeepAlive(marker));
        var grid = window.DataGrid("Candidate file owner");
        grid.OnFileDrag(() => { GC.KeepAlive(marker); return []; }, _ => GC.KeepAlive(marker));
        grid.OnFileDrop((_, _) => { GC.KeepAlive(marker); return FileTransferEffect.None; },
            (_, _, _) => { GC.KeepAlive(marker); return FileTransferEffect.None; });
        var miller = window.MillerColumns("Candidate Miller owner");
        miller.SelectionChanged += _ => GC.KeepAlive(marker);
        Expect(candidate.CallbackLifetime.Count == 4);
        return weak;
    }

    private static void ManagedSubscriptions()
    {
        using var window = new Window();
        var number = window.NumericInput("Observer-safe numeric");
        int clicks = 0;
        Action observer = () => ++clicks;
        number.IncreaseButton.Click += observer;
        number.IncreaseButton.Invoke();
        Expect(number.Value == 1 && clicks == 1);
        number.IncreaseButton.Click -= observer;
        number.IncreaseButton.Invoke();
        Expect(number.Value == 2 && clicks == 1);
        number.IncreaseButton.Click += observer;
        number.IncreaseButton.Invoke();
        Expect(number.Value == 3 && clicks == 2);

        var reused = window.Button("Reentrant subscription reuse");
        int oldCalls = 0, newCalls = 0;
        Action next = () => ++newCalls;
        Action? old = null;
        old = () =>
        {
            ++oldCalls;
            reused.Click -= old;
            reused.Click += next;
        };
        reused.Click += old;
        reused.Invoke();
        reused.Invoke();
        Expect(oldCalls == 1 && newCalls == 1,
            "Replacing a subscription cannot recycle its still-dispatching native context");
        reused.Click -= next;
        for (int i = 0; i < 32; ++i)
        {
            reused.Click += next;
            reused.Invoke();
            reused.Click -= next;
        }
        Expect(newCalls == 33, "A released registration can be rebound repeatedly without stale cancellation");

        var host = window.CreateContentHost();
        window.SetContent(window.Stack().Add(number).Add(host));
        var weak = Rollback(window, host);
        GC.Collect(); GC.WaitForPendingFinalizers(); GC.Collect();
        Expect(!weak.IsAlive);
        using (var candidate = host.BeginUpdate())
        {
            var root = window.Stack();
            var action = window.Button("Scoped observer");
            root.Add(action);
            int calls = 0;
            Action? handler = null;
            handler = () => { ++calls; action.Click -= handler; };
            action.Click += handler;
            candidate.Commit(root);
            action.Invoke();
            action.Invoke();
            Expect(calls == 1 && candidate.CallbackLifetime.Count == 0);
            action.Click += () => ++calls;
            Expect(candidate.CallbackLifetime.Count == 1);
            host.Clear();
            Expect(candidate.CallbackLifetime.Retired && candidate.CallbackLifetime.Count == 0);
        }
        window.KeyHandler = _ => false;
        window.NavigationHandler = _ => false;
        window.IconErrorHandler = _ => { };
        window.KeyHandler = null;
        window.NavigationHandler = null;
        window.IconErrorHandler = null;
        window.Dispose();
        window.Dispose();
    }

    internal static void Run()
    {
        Lifetime();
        ManagedSubscriptions();
        Console.WriteLine($"Scoped callback ownership assertions: {assertions} passed.");
    }
}
