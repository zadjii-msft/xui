using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text.Json;
using Xui;

public static class CallbackPerformanceStudy
{
    private static bool measured;
    private static long callbacks;
    private static readonly Dictionary<string,int> counts = [];
    private static void Require(bool value, string message)
    { if (!value) throw new InvalidOperationException(message); }
    [DllImport("kernel32.dll")] private static extern nint GetCurrentThread();
    [DllImport("kernel32.dll")] private static extern bool GetThreadTimes(nint thread, out long creation, out long exit,
        out long kernel, out long user);
    [DllImport("user32.dll")] private static extern uint GetGuiResources(nint process, uint kind);

    private static void Phase(string name, int count, Action<int> action, int expected)
    {
        count = measured ? count : 8;
        if(measured && counts.TryGetValue(name,out int fixedCount)) count=fixedCount;
        callbacks = 0;
        Require(GetThreadTimes(GetCurrentThread(), out _, out _, out long k0, out long u0), "Read managed owner CPU");
        long allocated = GC.GetAllocatedBytesForCurrentThread();
        int gen0 = GC.CollectionCount(0), gen1 = GC.CollectionCount(1), gen2 = GC.CollectionCount(2);
        var clock = Stopwatch.StartNew();
        for (int i = 0; i < count; ++i) action(i);
        clock.Stop();
        long bytes = GC.GetAllocatedBytesForCurrentThread() - allocated;
        Require(GetThreadTimes(GetCurrentThread(), out _, out _, out long k1, out long u1), "Read managed owner CPU after work");
        Require(callbacks == (long)count * expected, "Exact managed callback effect count");
        Console.WriteLine(JsonSerializer.Serialize(new { phase = name, iterations = count, measured,
            wall_ms = measured ? clock.Elapsed.TotalMilliseconds : 0, callbacks,
            kernel_before100ns = k0, kernel_after100ns = k1, user_before100ns = u0, user_after100ns = u1,
            managed_bytes = bytes, gen0 = GC.CollectionCount(0) - gen0, gen1 = GC.CollectionCount(1) - gen1,
            gen2 = GC.CollectionCount(2) - gen2 }));
    }

    public static int Run(string[] args)
    {
        measured = args is ["--measure"];
        if (args.Length != 0 && !measured) { Console.Error.WriteLine("Usage: callback study [--measure]"); return 2; }
        try
        {
            if (Environment.GetEnvironmentVariable("XUI_PERF_COUNTS") is { } path)
                foreach(string line in File.ReadAllLines(path))
                {
                    var fields=line.Split(' ',StringSplitOptions.RemoveEmptyEntries);
                    Require(fields.Length==2 && int.TryParse(fields[1],out int count) && count>=32 && count%32==0,
                        "Complete fixed managed count protocol");
                    counts.Add(fields[0],int.Parse(fields[1]));
                }
            // JIT the same public operations before collecting a phase; not a discarded process sample.
            using (var warm = new Window())
            {
                var warmButton = warm.Button("Warm");
                Action handler = () => { };
                warmButton.Click += handler; warmButton.Invoke(); warmButton.Click -= handler;
            }
            using var window = new Window();
            var button = window.Button("Observed");
            button.Click += () => ++callbacks;
            Phase("managed-observed-invoke", 524288, _ => button.Invoke(), 1);
            Action observer = () => ++callbacks;
            var replace = window.Button("Replace");
            Phase("managed-subscribe-remove", 131072, _ => {
                replace.Click += observer; replace.Invoke(); replace.Click -= observer;
            }, 1);
            var radio = window.RadioGroup("Radio");
            radio.SetItems([new(1, "First"), new(2, "Second")], 1);
            radio.Event += e => { if (e.Kind == EventKind.Selection) ++callbacks; };
            Phase("managed-choice-selection", 131072, i => radio.Select(i % 2 == 0 ? 2UL : 1UL), 1);
            var host = window.CreateContentHost();
            window.SetContent(window.Stack().Add(host));
            Phase("managed-content-rollback", 512, _ => {
                using var scope = host.BeginUpdate();
                var action = window.Button("Scoped");
                action.Click += () => ++callbacks;
                var menu = window.ItemsView("Menu");
                menu.OnContextMenu(() => [], _ => { });
                var grid = window.DataGrid("Files");
                grid.OnFileDrag(() => [], _ => { });
                grid.OnFileDrop((_, _) => FileTransferEffect.None, (_, _, _) => FileTransferEffect.None);
                var miller = window.MillerColumns("Columns");
                miller.SelectionChanged += _ => ++callbacks;
            }, 0);
            Phase("managed-content-commit-clear", 2048, _ => {
                using var scope = host.BeginUpdate();
                var root = window.Stack();
                var action = window.Button("Scoped"); action.Click += () => ++callbacks;
                root.Add(action); scope.Commit(root); action.Invoke(); host.Clear();
            }, 1);
            Phase("managed-window-retirement", 2048, _ => {
                using var owner = new Window();
                var action = owner.Button("Window"); action.Click += () => ++callbacks;
                owner.KeyHandler = _ => false; owner.NavigationHandler = _ => false; owner.IconErrorHandler = _ => { };
                action.Invoke();
            }, 1);
            using var process = Process.GetCurrentProcess();
            GC.Collect(); GC.WaitForPendingFinalizers(); GC.Collect();
            process.Refresh();
            Console.WriteLine(JsonSerializer.Serialize(new { resources = "post-study",
                managed_live_bytes = GC.GetTotalMemory(true), private_bytes = process.PrivateMemorySize64,
                handles = process.HandleCount, gdi = GetGuiResources(process.Handle, 0), user = GetGuiResources(process.Handle, 1) }));
            return 0;
        }
        catch (Exception error) { Console.Error.WriteLine(error); return 1; }
    }
}
