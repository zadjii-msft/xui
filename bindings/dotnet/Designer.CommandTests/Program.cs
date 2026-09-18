using System.Runtime.InteropServices;
using Xui;
using Xui.Designer;

internal static class Program
{
    private static int assertions;

    [STAThread]
    private static int Main()
    {
        try
        {
            Run(VisualStyle.Classic);
            Run(VisualStyle.WinUI);
            Console.WriteLine($"Designer native command assertions: {assertions} passed.");
            return 0;
        }
        catch (Exception error) { Console.Error.WriteLine(error); return 1; }
    }

    private static void Run(VisualStyle style)
    {
        using var window = new Window("Designer command tests", 900, 700, visualStyle: style);
        window.SetShowActivated(false);
        var source = window.MultilineText("Source").SetDocument("Original");
        var anchor = window.Button("Commands");
        window.SetContent(window.Stack().Add(anchor).Add(source, 1));
        var errors = new List<string>();
        var invoked = new List<DesignerCommandId>();
        bool available = false, openDuringAction = false;
        DesignerCommandPalette? current = null;
        using var palette = new DesignerCommandPalette(window, anchor, Commands, errors.Add);
        current = palette;
        var driver = Task.Run(Drive);
        window.Run();
        driver.GetAwaiter().GetResult();

        IReadOnlyList<DesignerCommand> Commands() =>
        [
            new(DesignerCommandId.Find, "Source: Find text", () => Record(DesignerCommandId.Find), "Ctrl+F"),
            new(DesignerCommandId.Replace, "Source: Find and replace", () => Record(DesignerCommandId.Replace), "Ctrl+H"),
            new(DesignerCommandId.Save, "File: Save component", () => Record(DesignerCommandId.Save), "Ctrl+S", () => available),
            new(DesignerCommandId.Undo, "Source: Undo", () => { Record(DesignerCommandId.Undo); source.Command(TextCommand.Undo); }),
            new(DesignerCommandId.Redo, "Source: Redo", () => { Record(DesignerCommandId.Redo); source.Command(TextCommand.Redo); })
        ];

        void Record(DesignerCommandId id)
        {
            if (current is null) throw new InvalidOperationException("The palette was not constructed.");
            openDuringAction |= current.IsOpen;
            invoked.Add(id);
        }

        async Task Drive()
        {
            using var deadline = new CancellationTokenSource(TimeSpan.FromSeconds(40));
            nint owner = 0, initialForeground = 0;
            try
            {
                await Ui(() =>
                {
                    source.Focus();
                    owner = GetAncestor(GetFocus(), 2);
                    initialForeground = GetForegroundWindow();
                    source.Selection = new(2, 4);
                    palette.Show();
                });
                await Until(() => palette.IsOpen);
                await Check(() => palette.Surface.Editor.Focused && palette.Surface.Editor.GetBounds().Width > 200,
                    "Opening the native palette focuses a usable search editor.");
                await Ui(() =>
                {
                    try { palette.Surface.Invoke((ulong)DesignerCommandId.Save); }
                    catch (XuiException) { return; }
                    throw new InvalidOperationException("A disabled native command was accepted.");
                });
                await Check(() => invoked.Count == 0 && palette.IsOpen, "Disabled commands do not run or close the palette.");

                await Ui(() => Query("replace"));
                await Ui(() => Key(0x0D));
                await Until(() => invoked.Count == 1);
                await Check(() => invoked[0] == DesignerCommandId.Replace && !palette.IsOpen && !openDuringAction,
                    "Real native search and Enter choose the matching command after dismissal.");
                await Check(() => CorrectDismissalFocus() && source.Selection == new TextSelection(2, 4) && source.Text == "Original",
                    "Command dismissal preserves foreground-owner focus, exact source selection, and text.");

                await Ui(palette.Show);
                await Until(() => palette.IsOpen);
                await Check(() => palette.Surface.Editor.Text == "replace", "The native query remains available on reopening.");
                await Ui(() => Query("no such command"));
                await Ui(() => Key(0x0D));
                await Ui(() => { });
                await Check(() => invoked.Count == 1 && palette.IsOpen, "Enter with no matches keeps the palette open without an action.");
                await Ui(() => Key(0x1B));
                await Until(() => !palette.IsOpen);
                await Check(() => CorrectDismissalFocus() && source.Selection == new TextSelection(2, 4),
                    "Native Escape preserves foreground-owner focus and source selection.");

                await Ui(() =>
                {
                    available = true;
                    palette.Show();
                });
                await Until(() => palette.IsOpen);
                await Ui(() => Query(""));
                await Ui(() =>
                {
                    available = false;
                    palette.Surface.Invoke((ulong)DesignerCommandId.Save);
                });
                await Until(() => errors.Count == 1);
                await Check(() => invoked.Count == 1 && !palette.IsOpen &&
                    errors[0].Contains("no longer available", StringComparison.Ordinal),
                    "An action rechecks current availability instead of trusting an old enabled snapshot.");

                await Ui(() =>
                {
                    available = true;
                    palette.Show();
                });
                await Until(() => palette.IsOpen);
                await Ui(() =>
                {
                    palette.Surface.Invoke((ulong)DesignerCommandId.Save);
                    palette.Surface.Invoke((ulong)DesignerCommandId.Save);
                });
                await Until(() => invoked.Count == 2);
                await Check(() => invoked[^1] == DesignerCommandId.Save && !openDuringAction && CorrectDismissalFocus(),
                    "Repeated invocation dispatches only once and dismisses before the deferred action.");

                await Ui(palette.Show);
                await Until(() => palette.IsOpen);
                await Ui(() =>
                {
                    palette.Surface.Invoke((ulong)DesignerCommandId.Find);
                    palette.Show();
                });
                await Until(() => errors.Count == 2 && palette.IsOpen);
                await Check(() => invoked.Count == 2 && errors[1].Contains("reopened", StringComparison.Ordinal),
                    "Reopening cancels an older queued command instead of executing it against a new palette.");
                await Ui(() => palette.Surface.CloseButton.Invoke());
                await Until(() => !palette.IsOpen);

                await Ui(() =>
                {
                    source.ReplaceRange(new(0, 0), source.Text, "// ");
                    palette.Show();
                });
                await Until(() => palette.IsOpen);
                await Ui(() => palette.Surface.Invoke((ulong)DesignerCommandId.Undo));
                await Until(() => invoked.Count == 3);
                await Check(() => source.Text == "Original", "A source command uses the original document's native undo history.");
                await Ui(palette.Show);
                await Until(() => palette.IsOpen);
                await Ui(() => palette.Surface.Invoke((ulong)DesignerCommandId.Redo));
                await Until(() => invoked.Count == 4);
                await Check(() => source.Text == "// Original" && !openDuringAction && window.CallbackStatus == 0,
                    "Redo restores the source edit and command callbacks do not reenter native focus delivery.");

                await Ui(palette.Show);
                await Until(() => palette.IsOpen);
                await Ui(() =>
                {
                    palette.Surface.Invoke((ulong)DesignerCommandId.Find);
                    palette.Dispose();
                    palette.Surface.Dismiss();
                });
                await Ui(() => { });
                await Check(() => invoked.Count == 4, "Controller disposal cancels a queued command.");
                await Check(() => GetForegroundWindow() == initialForeground,
                    "The passive command fixture does not change foreground ownership.");
            }
            finally { window.Post(window.Close); }

            bool CorrectDismissalFocus() => !palette.Surface.Editor.Focused && !palette.Surface.CloseButton.Focused &&
                (GetForegroundWindow() != owner || source.Focused);

            async Task Ui(Action action)
            {
                var done = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
                if (!window.Post(() =>
                {
                    try { action(); done.SetResult(); }
                    catch (Exception error) { done.SetException(error); }
                })) throw new InvalidOperationException("The command test window closed.");
                await done.Task.WaitAsync(deadline.Token);
            }

            async Task Until(Func<bool> condition)
            {
                while (true)
                {
                    bool ready = false;
                    await Ui(() => ready = condition());
                    if (ready) return;
                    await Task.Delay(15, deadline.Token);
                }
            }

            async Task Check(Func<bool> condition, string message)
            {
                await Ui(() =>
                {
                    if (!condition()) throw new InvalidOperationException($"{style}: {message}");
                    assertions++;
                });
            }
        }
    }

    private static void Query(string text)
    {
        nint editor = GetFocus();
        if (editor == 0 || SendMessageW(editor, 0x000C, 0, text) == 0)
            throw new InvalidOperationException("Native palette query replacement failed.");
    }

    private static void Key(uint key)
    {
        if (!PostMessageW(GetFocus(), 0x0100, key, 0)) throw new InvalidOperationException("Native palette key posting failed.");
    }

    [DllImport("user32.dll")] private static extern nint GetFocus();
    [DllImport("user32.dll")] private static extern nint GetForegroundWindow();
    [DllImport("user32.dll")] private static extern nint GetAncestor(nint window, uint flags);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern nint SendMessageW(nint window, uint message, nuint first, string second);
    [DllImport("user32.dll")] private static extern bool PostMessageW(nint window, uint message, nuint first, nint second);
}
