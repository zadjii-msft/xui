using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using Xui;

internal static class MultiWindowTests
{
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern nint FindWindowW(string? className, string title);
    [DllImport("user32.dll")]
    private static extern bool IsWindow(nint window);
    [DllImport("user32.dll")]
    private static extern uint GetWindowThreadProcessId(nint window, out uint process);
    [DllImport("user32.dll")]
    private static extern bool SetWindowPos(nint window, nint after, int x, int y, int width, int height, uint flags);
    private static void Require(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
    }
    private static void Throws<T>(Action action) where T : Exception
    {
        try { action(); }
        catch (T) { return; }
        throw new InvalidOperationException($"Expected {typeof(T).Name}");
    }
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static WeakReference Create(Application application, string title, Action<Window, MultilineText> closed)
    {
        var window = application.CreateWindow(title, 420, 320, visualStyle: VisualStyle.WinUI);
        var text = window.MultilineText("Captured file contents").SetReadOnly(true);
        text.Text = "independent native text";
        window.SetContent(window.Stack().Add(text, 1));
        window.Closed += _ => closed(window, text);
        application.Show(window);
        return new(window);
    }
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static void Collect()
    {
        GC.Collect(); GC.WaitForPendingFinalizers(); GC.Collect();
    }
    private static Window Get(WeakReference weak) => (Window)(weak.Target ?? throw new InvalidOperationException("Lost live window root"));
    private static void Lifetime(bool fail)
    {
        using var application = new Application();
        WeakReference? survivor = null;
        int closed = 0, disposed = 0;
        nint survivorHwnd = 0;
        var opener = Create(application, "Managed opener", (window, _) =>
        {
            ++closed;
            Throws<XuiException>(window.Dispose);
            Require(window.State == WindowState.Closed, "Closed notification sees closed state");
            Require(!window.Post(() => throw new Exception("Stale post executed")), "Closed window rejects work");
            Require(application.Post(() =>
            {
                window.Dispose(); ++disposed;
                var remaining = Get(survivor ?? throw new InvalidOperationException("Missing survivor"));
                Require(remaining.State == WindowState.Open && IsWindow(survivorHwnd),
                    "Survivor remains open after opener disposal");
                Require(SetWindowPos(survivorHwnd, 0, 30, 30, 550, 350, 0x14), "Survivor resizes");
                remaining.SetClipboardText("surviving clipboard text");
                remaining.Close();
            }), "Application accepts deferred disposal");
        });
        survivor = Create(application, "Managed survivor", (window, text) =>
        {
            ++closed;
            Require(text.Text == "independent native text", "Independent document retained its captured text");
            Require(application.Post(() => { window.Dispose(); ++disposed; }), "Last-window cleanup accepted");
        });
        var firstHwnd = FindWindowW("Xui.Window.1", "Managed opener");
        survivorHwnd = FindWindowW("Xui.Window.1", "Managed survivor");
        Require(firstHwnd != 0 && survivorHwnd != 0 && firstHwnd != survivorHwnd, "Distinct native windows exist");
        Require(GetWindowThreadProcessId(firstHwnd, out uint p1) == GetWindowThreadProcessId(survivorHwnd, out uint p2)
            && p1 == Environment.ProcessId && p2 == p1, "Both HWNDs share one process and UI thread");
        Collect();
        Require(opener.IsAlive && survivor.IsAlive, "Application roots windows after Show and GC");
        Require(application.Post(() =>
        {
            Throws<XuiException>(() => Get(opener).Run());
            if (fail) Get(opener).Post(() => throw new InvalidOperationException("Managed original callback failure"));
            else Get(opener).Close();
        }), "Initial work accepted");
        if (fail)
        {
            try { application.Run(); throw new Exception("Failure was lost"); }
            catch (XuiException error)
            {
                Require(error.Status == 8 && error.InnerException?.ToString().Contains("Managed original callback failure") == true,
                    "Original failure survives opener disposal and later closures");
            }
        }
        else application.Run();
        Require(closed == 2 && disposed == 2, "Every window closes and disposes exactly once");
        Require(!IsWindow(firstHwnd) && !IsWindow(survivorHwnd), "All native hosts retired");
        Require(!application.Post(() => { }), "Stopped application rejects work");
        Collect();
        Require(!opener.IsAlive && !survivor.IsAlive, "Closed windows release their callback roots");
    }
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static WeakReference CanceledPost(Window window)
    {
        var sentinel = new object();
        var reference = new WeakReference(sentinel);
        Require(window.Post(() => { GC.KeepAlive(sentinel); throw new Exception("Canceled post executed"); }), "Post accepted");
        return reference;
    }
    internal static void Run()
    {
        Lifetime(false);
        Lifetime(true);
        using (var application = new Application())
        {
            var window = application.CreateWindow("Never shown");
            var reference = CanceledPost(window);
            window.Close();
            application.Run();
            window.Dispose();
            Collect();
            Require(!reference.IsAlive, "Discarded post releases its managed capture");
        }
        using (var legacy = new Window("Legacy compatibility"))
        {
            legacy.SetContent(legacy.Stack().Add(legacy.Label("Legacy run")));
            legacy.Post(legacy.Close);
            legacy.Run();
        }
        Console.WriteLine("Managed multi-window lifetime, GC, callbacks and native survival passed");
    }
}
