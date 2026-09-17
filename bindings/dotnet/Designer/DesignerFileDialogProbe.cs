using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;

namespace Xui.Designer;

internal sealed class DesignerFileDialogProbe : IDisposable
{
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern int GetClassName(nint window, StringBuilder name, int count);
    [DllImport("user32.dll")] private static extern nint GetWindow(nint window, uint command);
    [DllImport("user32.dll")] private static extern nint GetDlgItem(nint window, int id);
    [DllImport("user32.dll")] private static extern bool IsWindowVisible(nint window);
    [DllImport("user32.dll")] private static extern bool IsWindowEnabled(nint window);
    [DllImport("user32.dll")] private static extern bool IsWindow(nint window);
    [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(nint window, out uint process);
    [DllImport("kernel32.dll")] private static extern uint GetCurrentThreadId();
    [DllImport("user32.dll")] private static extern bool EnumThreadWindows(uint thread, Enumerate callback, nint context);
    [DllImport("user32.dll")] private static extern bool PostMessage(nint window, uint message, nuint wparam, nint lparam);
    [DllImport("user32.dll")] private static extern nuint SetTimer(nint window, nuint timer, uint interval, TimerCallback callback);
    [DllImport("user32.dll")] private static extern bool KillTimer(nint window, nuint timer);
    private delegate bool Enumerate(nint window, nint context);
    private delegate void TimerCallback(nint window, uint message, nuint timer, uint time);
    private readonly uint thread = GetCurrentThreadId();
    private readonly Stopwatch elapsed = Stopwatch.StartNew();
    private readonly TimerCallback callback;
    private readonly nint owner;
    private Stopwatch? actionElapsed;
    private nuint timer;
    private bool observed;
    private bool reportedDisabled;
    private Exception? failure;

    internal DesignerFileDialogProbe(Window window, bool accept, Action? beforeResult = null, bool postResult = true)
    {
        var owners = new List<nint>();
        EnumThreadWindows(thread, (candidate, _) =>
        {
            GetWindowThreadProcessId(candidate, out uint process);
            if (process == Environment.ProcessId && IsWindowVisible(candidate) && ClassName(candidate) == "Xui.Window.1")
                owners.Add(candidate);
            return true;
        }, 0);
        if (owners.Count != 1) throw new InvalidOperationException("The file smoke requires one visible native owner on its UI thread.");
        owner = owners[0];
        callback = (_, _, _, _) =>
        {
            nint dialog = 0;
            EnumThreadWindows(thread, (candidate, _) =>
            {
                if (GetWindow(candidate, 4) != owner || !IsWindowVisible(candidate) || ClassName(candidate) != "#32770") return true;
                dialog = candidate;
                return false;
            }, 0);
            try
            {
                if (dialog != 0 && !IsWindowEnabled(dialog) && !reportedDisabled)
                {
                    Console.WriteLine($"Native chooser is disabled at {elapsed.Elapsed.TotalSeconds:F2}s.");
                    reportedDisabled = true;
                }
                nint button = dialog == 0 ? 0 : GetDlgItem(dialog, accept ? 1 : 2);
                if (dialog != 0 && !observed && button != 0 && IsWindowVisible(button) && IsWindowEnabled(button))
                {
                    if (IsWindowEnabled(owner)) throw new InvalidOperationException("The native chooser did not disable its owner.");
                    observed = true;
                    beforeResult?.Invoke();
                    actionElapsed = Stopwatch.StartNew();
                    if (postResult && !PostMessage(dialog, 0x111, accept ? 1u : 2u, 0))
                        throw new InvalidOperationException("The native chooser rejected its smoke action.");
                }
                if ((actionElapsed ?? elapsed).Elapsed > TimeSpan.FromSeconds(30))
                    throw new TimeoutException(actionElapsed is null
                        ? "The native file chooser did not become ready."
                        : "The native file chooser did not complete its smoke action.");
            }
            catch (Exception error)
            {
                failure ??= error;
                Console.Error.WriteLine($"Native chooser smoke failed after {elapsed.Elapsed.TotalSeconds:F2}s (visible: {dialog != 0}, action sent: {observed}): {error.Message}");
                if (dialog != 0) PostMessage(dialog, 0x111, 2, 0);
                try { Dispose(); window.Close(); }
                catch (Exception shutdownError)
                {
                    failure = new AggregateException(failure, shutdownError);
                    Console.Error.WriteLine(shutdownError);
                }
            }
        };
        timer = SetTimer(0, 0, 25, callback);
        if (timer == 0) throw new InvalidOperationException("The native file smoke timer could not start.");
    }

    internal void Check()
    {
        if (failure is not null) throw new InvalidOperationException("Native chooser smoke failed.", failure);
        if (!observed) throw new InvalidOperationException("The actual owned file chooser did not appear.");
        Console.WriteLine($"Native chooser smoke completed in {elapsed.Elapsed.TotalSeconds:F2}s.");
    }

    internal void CheckOwnerClosed()
    {
        if (IsWindow(owner)) throw new InvalidOperationException("The native file chooser owner survived Window.Run.");
    }

    public void Dispose()
    {
        if (GetCurrentThreadId() != thread) throw new InvalidOperationException("Dispose the native file smoke timer on its UI thread.");
        if (timer != 0)
        {
            if (!KillTimer(0, timer)) throw new InvalidOperationException("The native file smoke timer could not stop.");
            timer = 0;
        }
        GC.KeepAlive(callback);
    }

    private static string ClassName(nint window)
    {
        var name = new StringBuilder(32);
        GetClassName(window, name, name.Capacity);
        return name.ToString();
    }
}
