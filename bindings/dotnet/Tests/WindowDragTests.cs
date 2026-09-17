using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using Xui;

internal static class WindowDragTests
{
    private const BindingFlags PrivateInstance = BindingFlags.Instance | BindingFlags.NonPublic;
    [StructLayout(LayoutKind.Sequential)]
    private struct DragRecord
    {
        internal uint Size, Kind, SourceStrip, TargetStrip;
        internal ulong TabId, Target, Index;
    }
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate int Dispatch(nint context, ref DragRecord value, out uint accepted);
    private static void Require(bool value, string message)
    {
        if (!value) throw new InvalidOperationException(message);
    }
    private static void Throws<T>(Action action) where T : Exception
    {
        try { action(); }
        catch (T) { return; }
        throw new InvalidOperationException($"Expected {typeof(T).Name}");
    }
    private static GCHandle Root(Window window) =>
        (GCHandle)typeof(Window).GetField("tabDragRoot", PrivateInstance)!.GetValue(window)!;
    private static ulong Handle(Window window) =>
        (ulong)typeof(Window).GetProperty("Handle", PrivateInstance)!.GetValue(window)!;
    private static Exception? Error(Window window) =>
        (Exception?)typeof(Window).GetField("callbackError", PrivateInstance)!.GetValue(window);
    private static void ClearError(Window window) =>
        typeof(Window).GetField("callbackError", PrivateInstance)!.SetValue(window, null);
    internal static void Layouts()
    {
        var native = typeof(Window).Assembly.GetType("Xui.Native")!;
        var placement = native.GetNestedType("WindowPlacement", BindingFlags.NonPublic)!;
        var drag = native.GetNestedType("TabDragEvent", BindingFlags.NonPublic)!;
        Require(Marshal.SizeOf(placement) == 24 && Marshal.OffsetOf(placement, "Maximized") == 20, "Placement ABI layout");
        Require(Marshal.SizeOf(drag) == 40 && Marshal.SizeOf<DragRecord>() == 40, "Tab drag ABI size");
        Require(Marshal.OffsetOf(drag, "TabId") == 16 && Marshal.OffsetOf(drag, "Target") == 24 &&
            Marshal.OffsetOf(drag, "Index") == 32, "Tab drag ABI field offsets");
        Require((uint)TabDragKind.Reorder == 0 && (uint)TabDragKind.TearOut == 1 && (uint)TabDragKind.Drop == 2 &&
            (uint)TabDragKind.Cancel == 3 && (uint)TabDragKind.Completed == 4 && (uint)TabDragKind.QueryDrop == 5,
            "Tab drag enum values");
        Console.WriteLine("Managed window placement and tab drag ABI layouts passed");
    }
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static WeakReference Capture(Window window)
    {
        var capture = new object();
        window.TabDragHandler = _ => { GC.KeepAlive(capture); return false; };
        return new(capture);
    }
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static void Collect()
    {
        GC.Collect(); GC.WaitForPendingFinalizers(); GC.Collect();
    }
    private static void WithForeignWindow(Action<ulong> action)
    {
        using var release = new ManualResetEventSlim();
        var ready = new TaskCompletionSource<ulong>(TaskCreationOptions.RunContinuationsAsynchronously);
        Exception? failure = null;
        var thread = new Thread(() =>
        {
            try
            {
                using var application = new Application();
                using var window = application.CreateWindow("Managed foreign drag target", customTitlebar: true);
                ready.SetResult(Handle(window));
                release.Wait();
            }
            catch (Exception error) { failure = error; ready.TrySetException(error); }
        });
        if (OperatingSystem.IsWindows()) thread.SetApartmentState(ApartmentState.STA);
        thread.Start();
        try { action(ready.Task.GetAwaiter().GetResult()); }
        finally { release.Set(); thread.Join(); }
        if (failure is not null) throw new InvalidOperationException("Foreign window lifetime failed.", failure);
    }
    private static void DispatchContracts(Window source, Window target, ulong foreign)
    {
        var method = typeof(Window).GetMethod("TabDragTrampoline", BindingFlags.Static | BindingFlags.NonPublic)!;
        var dispatch = Marshal.GetDelegateForFunctionPointer<Dispatch>(method.MethodHandle.GetFunctionPointer());
        var value = new DragRecord
        {
            Size = 40, Kind = (uint)TabDragKind.Drop, SourceStrip = 1, TargetStrip = 0,
            TabId = ulong.MaxValue, Target = Handle(target), Index = 3
        };
        int invoked = 0;
        source.TabDragHandler = e =>
        {
            ++invoked;
            Require(e == new TabDragEvent(TabDragKind.Drop, 1, ulong.MaxValue, target, 0, 3),
                "Managed event preserves source strip, ID, target identity and pre-removal slot");
            Throws<XuiException>(source.Dispose);
            return true;
        };
        var context = GCHandle.ToIntPtr(Root(source));
        Require(dispatch(context, ref value, out uint accepted) == 0 && accepted == 1 && invoked == 1,
            "Accepted drag request");
        foreach (var kind in new[] { TabDragKind.TearOut, TabDragKind.Drop, TabDragKind.Cancel, TabDragKind.Completed })
        {
            value.Kind = (uint)kind; value.Target = 0;
            source.TabDragHandler = e => { Require(e.Kind == kind && e.Target is null, "Nullable target preserved"); return false; };
            Require(dispatch(context, ref value, out accepted) == 0 && accepted == 0, "Rejected drag request");
        }
        value.Kind = (uint)TabDragKind.Reorder; value.Target = Handle(source); value.TargetStrip = 1;
        source.TabDragHandler = e => ReferenceEquals(e.Target, source) && e.TargetStrip == 1;
        Require(dispatch(context, ref value, out accepted) == 0 && accepted == 1, "Same-strip reorder resolves source window");
        value.Kind = (uint)TabDragKind.Drop; value.TargetStrip = 0;
        source.TabDragHandler = e => e.Kind == TabDragKind.Drop && ReferenceEquals(e.Target, source) && e.TargetStrip == 0;
        Require(dispatch(context, ref value, out accepted) == 0 && accepted == 1, "Other-strip drop resolves source window");
        value.Kind = (uint)TabDragKind.QueryDrop;
        foreach (var destination in new[] { source, target })
        {
            value.Target = Handle(destination);
            foreach (bool allow in new[] { false, true })
            {
                source.TabDragHandler = e =>
                {
                    Require(e == new TabDragEvent(TabDragKind.QueryDrop, 1, ulong.MaxValue, destination, 0, 3),
                        "QueryDrop preserves hovered target and pre-removal slot");
                    return allow;
                };
                Require(dispatch(context, ref value, out accepted) == 0 && accepted == (allow ? 1u : 0u),
                    "QueryDrop returns the application's acceptance decision");
            }
        }

        value.Target = foreign;
        Require(dispatch(context, ref value, out accepted) == 8 && accepted == 0 && Error(source) is XuiException,
            "Foreign application target fails closed");
        value.Target = Handle(target);
        target.Dispose();
        Require(dispatch(context, ref value, out accepted) == 8 && accepted == 0, "Disposed target fails closed");
        value.Target = 0;
        foreach (var invalid in new[] { value with { Size = 0 }, value with { Kind = 6 }, value with { SourceStrip = 2 },
            value with { TargetStrip = 2 }, value with { TabId = 0 }, value with { Index = (ulong)int.MaxValue + 1 } })
        {
            var record = invalid;
            Require(dispatch(context, ref record, out accepted) == 8 && accepted == 0, "Malformed event fails closed");
        }
        var original = new InvalidOperationException("Original tab drag error");
        source.TabDragHandler = _ => throw original;
        Require(dispatch(context, ref value, out accepted) == 8 && accepted == 0 && ReferenceEquals(Error(source), original),
            "Handler exception stays in managed code with its original identity");
        Task.Run(() =>
        {
            var record = value;
            Require(dispatch(context, ref record, out var result) == 8 && result == 0 &&
                Error(source) is XuiException { Status: 4 }, "Wrong-thread callback fails closed");
        }).GetAwaiter().GetResult();
        ClearError(source);
    }
    internal static void Run()
    {
        Layouts();
        using var application = new Application();
        using var source = application.CreateWindow("Managed drag source", customTitlebar: true);
        using var target = application.CreateWindow("Managed drag target", customTitlebar: true);
        using var plain = application.CreateWindow("No custom title bar");
        using var standalone = new Window("Standalone custom title bar", customTitlebar: true);
        Throws<XuiException>(() => plain.TabDragHandler = _ => true);
        Throws<XuiException>(() => standalone.TabDragHandler = _ => true);
        Require(!Root(plain).IsAllocated && !Root(standalone).IsAllocated, "Failed registration releases its GCHandle");
        try
        {
            _ = source.Placement;
            throw new InvalidOperationException("Fresh unshown window fabricated an initial placement");
        }
        catch (XuiException error) { Require(error.Status == 9, "Unknown initial placement reports a native error"); }
        var placement = new WindowPlacement(-800, -400, 640, 480, true);
        source.Placement = placement;
        Require(source.Placement == placement, "Pre-show physical placement preserves negative coordinates and maximized state");
        foreach (var limit in new[] { new WindowPlacement(-1000000, -1000000, 1, 1, false),
            new WindowPlacement(1000000, 1000000, 65536, 65536, true) })
        {
            source.Placement = limit;
            Require(source.Placement == limit, "Placement boundary is accepted and preserved");
        }
        source.Placement = placement;
        foreach (var invalid in new[] { placement with { Width = 0 }, placement with { Height = -1 },
            placement with { X = int.MaxValue }, placement with { X = -1000001 }, placement with { X = 1000001 },
            placement with { Y = -1000001 }, placement with { Y = 1000001 },
            placement with { Width = 65537 }, placement with { Height = 65537 } })
        {
            Throws<XuiException>(() => source.Placement = invalid);
            Require(source.Placement == placement, "Rejected placement preserves previous state");
        }
        Task.Run(() =>
        {
            Throws<XuiException>(() => _ = source.Placement);
            Throws<XuiException>(() => source.Placement = placement);
            Throws<XuiException>(() => source.TabDragHandler = _ => true);
        }).GetAwaiter().GetResult();
        WithForeignWindow(foreign => DispatchContracts(source, target, foreign));
        var capture = Capture(source);
        Collect();
        Require(capture.IsAlive, "Installed handler retains captured state");
        source.TabDragHandler = null;
        Collect();
        Require(!capture.IsAlive && !Root(source).IsAllocated, "Unsubscribe releases delegate and GCHandle");
        capture = Capture(source);
        source.Dispose();
        Collect();
        Require(!capture.IsAlive && !Root(source).IsAllocated, "Disposal releases delegate and GCHandle");
        Throws<ObjectDisposedException>(() => _ = source.Placement);
        Throws<ObjectDisposedException>(() => source.TabDragHandler = _ => true);
        Console.WriteLine("Managed placement, drag events, targets, guards, exceptions and callback ownership passed");
    }
}
