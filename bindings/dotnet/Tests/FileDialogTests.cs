using System.Runtime.InteropServices;
using System.Text;
using Xui;

internal static class FileDialogTests
{
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern nint FindWindow(string kind, string title);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern int GetClassName(nint window, StringBuilder name, int count);
    [DllImport("user32.dll")] private static extern nint GetWindow(nint window, uint command);
    [DllImport("user32.dll")] private static extern bool IsWindowVisible(nint window);
    [DllImport("user32.dll")] private static extern bool IsWindowEnabled(nint window);
    [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(nint window, out uint process);
    [DllImport("kernel32.dll")] private static extern uint GetCurrentThreadId();
    [DllImport("user32.dll")] private static extern bool EnumThreadWindows(uint thread, Enumerate callback, nint context);
    [DllImport("user32.dll")] private static extern bool PostMessage(nint window, uint message, nuint wparam, nint lparam);
    [DllImport("user32.dll")] private static extern nuint SetTimer(nint window, nuint timer, uint interval, TimerCallback callback);
    [DllImport("user32.dll")] private static extern bool KillTimer(nint window, nuint timer);
    private delegate bool Enumerate(nint window, nint context);
    private delegate void TimerCallback(nint window, uint message, nuint timer, uint time);
    private static void Expect(bool value, string message)
    {
        if (!value) throw new InvalidOperationException(message);
    }
    private static void Fails(Action action, int status)
    {
        try { action(); }
        catch (XuiException e) when (e.Status == status) { return; }
        throw new InvalidOperationException($"Expected dialog status {status}.");
    }
    private static void Throws<T>(Action action) where T : Exception
    {
        try { action(); }
        catch (T) { return; }
        throw new InvalidOperationException($"Expected {typeof(T).Name}.");
    }
    private sealed class Probe : IDisposable
    {
        private readonly nuint timer;
        private readonly TimerCallback callback;
        private readonly DateTime deadline = DateTime.UtcNow.AddSeconds(30);
        private Exception? error;
        private bool observed;
        internal Probe(Window owner, Action<nint> action)
        {
            nint hwnd = FindWindow("Xui.Window.1", "Managed file dialogs");
            GetWindowThreadProcessId(hwnd, out uint process);
            Expect(hwnd != 0 && process == Environment.ProcessId, "Find only this fixture's native owner.");
            callback = (_, _, _, _) =>
            {
                nint dialog = 0;
                EnumThreadWindows(GetCurrentThreadId(), (window, _) =>
                {
                    var name = new StringBuilder(32);
                    GetClassName(window, name, name.Capacity);
                    if (GetWindow(window, 4) != hwnd || name.ToString() != "#32770" || !IsWindowVisible(window)) return true;
                    dialog = window;
                    return false;
                }, 0);
                try
                {
                    if (dialog != 0 && !observed)
                    {
                        Expect(!IsWindowEnabled(hwnd), "Actual native dialog disables its owner.");
                        observed = true;
                        action(dialog);
                    }
                    Expect(DateTime.UtcNow < deadline, "Owned managed dialog timed out.");
                }
                catch (Exception e)
                {
                    error ??= e;
                    if (dialog != 0) PostMessage(dialog, 0x111, 2, 0);
                    owner.Close();
                }
            };
            timer = SetTimer(0, 0, 25, callback);
            Expect(timer != 0, "Create native fixture timer.");
        }
        internal void Check()
        {
            if (error is not null) throw new InvalidOperationException("Native dialog probe failed.", error);
            Expect(observed, "Actual owned dialog appeared.");
        }
        public void Dispose() { KillTimer(0, timer); GC.KeepAlive(callback); }
    }
    internal static void Run()
    {
        using var window = new Window("Managed file dialogs");
        var options = new FileDialogOptions
        {
            Title = "Choose source",
            Filters = [new("Components", "*.xui"), new("All files", "*.*")],
            DefaultExtension = "xui",
            SuggestedName = $"xui-\u65e5\U0001F600-{Guid.NewGuid():N}",
            InitialDirectory = Path.GetTempPath()
        };
        Fails(() => window.ShowOpenFileDialog(options), 7);
        Fails(() => window.ShowOpenFileDialog(options with { Filters = [new("Bad", "xui")] }), 1);
        Fails(() => window.ShowSaveFileDialog(options with { DefaultExtension = ".xui" }), 1);
        Throws<ArgumentException>(() => window.ShowOpenFileDialog(options with { Title = "a\0b" }));
        Throws<EncoderFallbackException>(() => window.ShowOpenFileDialog(options with { Title = "\ud800" }));
        Task.Run(() => Fails(() => window.ShowOpenFileDialog(options), 4)).GetAwaiter().GetResult();
        var host = window.CreateContentHost();
        window.SetContent(window.Stack().Add(host));
        bool activeScopeRan = false;
        using var scope = host.BeginUpdate();
        Throws<InvalidOperationException>(() => window.ShowOpenFileDialog(options));
        scope.Commit(window.Label("Preview"));
        scope.Post(() =>
        {
            Throws<InvalidOperationException>(() => window.ShowSaveFileDialog(options));
            activeScopeRan = true;
        });
        bool ran = false;
        window.Post(() =>
        {
            Expect(activeScopeRan, "Active preview callback rejects dialogs before native dispatch.");
            using (var probe = new Probe(window, dialog =>
            {
                Throws<InvalidOperationException>(() => window.ShowSaveFileDialog(options));
                Fails(window.Dispose, 7);
                PostMessage(dialog, 0x111, 2, 0);
            }))
            {
                Expect(window.ShowOpenFileDialog(options) is null, "Cancellation returns null, not an error.");
                probe.Check();
            }
            Fails(() => window.ShowOpenFileDialog(options with
            {
                InitialDirectory = Path.Combine(Path.GetTempPath(), $"xui-absent-{Guid.NewGuid():N}")
            }), 9);
            string destination = Path.Combine(options.InitialDirectory, options.SuggestedName + ".xui");
            Expect(!File.Exists(destination), "Selected save destination starts absent.");
            using (var probe = new Probe(window, dialog => PostMessage(dialog, 0x111, 1, 0)))
            {
                Expect(window.ShowSaveFileDialog(options) == destination, "Save returns exact Unicode path and default extension.");
                probe.Check();
            }
            Expect(!File.Exists(destination), "Dialog selection does not create the file.");
            string executable = Environment.ProcessPath ?? throw new InvalidOperationException("Fixture executable path is missing.");
            var open = options with
            {
                Filters = [new("Executables", "*.exe")],
                DefaultExtension = "exe",
                InitialDirectory = Path.GetDirectoryName(executable)!,
                SuggestedName = Path.GetFileName(executable)
            };
            using (var probe = new Probe(window, dialog => PostMessage(dialog, 0x111, 1, 0)))
            {
                Expect(window.ShowOpenFileDialog(open) == executable, "Open copies the exact existing filesystem path.");
                probe.Check();
            }
            using (var probe = new Probe(window, _ => window.Close()))
            {
                Expect(window.ShowSaveFileDialog(options) is null, "Owner closure cancels its active dialog.");
                probe.Check();
            }
            ran = true;
        });
        window.Run();
        Expect(ran, "Managed modal fixture completed.");
        Fails(() => window.ShowOpenFileDialog(options), 11);
        scope.Dispose();
        window.Dispose();
        Throws<ObjectDisposedException>(() => window.ShowOpenFileDialog(options));
        Console.WriteLine("C# owned Open/Save results, cancellation, errors, affinity, scopes and lifetime passed");
    }
}
