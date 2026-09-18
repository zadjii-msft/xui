using System.Runtime.InteropServices;
using Xui;
using Xui.Designer;

internal static partial class Program
{
    private static void GoToCoordinates()
    {
        foreach (string newline in new[] { "\r", "\n", "\r\n" })
        {
            string source = $"Alpha{newline}A\U0001F680B{newline}";
            Require(DesignerGoTo.TryLocate(source, "2", "2", out var span, out var error) &&
                span.Start == 6 + newline.Length && span.Length == 0 && error is null,
                "Go to uses exact native, LF, and CRLF offsets before a Unicode pair.");
            Require(!DesignerGoTo.TryLocate(source, "2", "3", out _, out error) &&
                error!.Contains("Unicode", StringComparison.Ordinal), "A split surrogate pair is rejected.");
            Require(DesignerGoTo.TryLocate(source, "2", "5", out span, out _) && span.Start == 9 + newline.Length,
                "The column after the last complete character is a valid caret position.");
            Require(DesignerGoTo.TryLocate(source, "3", "1", out span, out _) && span.Start == source.Length,
                "A trailing empty line accepts the end-of-document caret.");
            Require(!DesignerGoTo.TryLocate(source, "4", "1", out _, out _), "Nonexistent lines are rejected.");
            Require(!DesignerGoTo.TryLocate(source, "2", "6", out _, out _), "Columns beyond the line are rejected.");
        }
        Require(DesignerGoTo.TryLocate("", "1", "1", out var empty, out _) && empty.Start == 0,
            "Empty documents accept line 1, column 1.");
        foreach (string invalid in new[] { "", "0", "-1", "+1", " 1", "1 ", "1.0", "1e1", "0x1", "2147483648", "one" })
        {
            Require(!DesignerGoTo.TryLocate("abc", invalid, "1", out _, out _), "Invalid line input is rejected: " + invalid);
            Require(!DesignerGoTo.TryLocate("abc", "1", invalid, out _, out _), "Invalid column input is rejected: " + invalid);
        }
    }

    private static void RunGoTo(VisualStyle style)
    {
        using var window = new Window("Designer source navigation tests", 900, 700, visualStyle: style);
        window.SetShowActivated(false);
        var editor = window.MultilineText("Source").SetDocument("Alpha\rA\U0001F680B\r");
        var anchor = window.Button("Go to line");
        window.SetContent(window.Stack().Add(anchor).Add(editor, 1));
        var errors = new List<string>();
        int navigations = 0;
        long version = 1;
        editor.Event += value => { if (value.Kind == EventKind.Change) version++; };
        using var goTo = new DesignerGoTo(window, editor, () => version, () => navigations++, errors.Add);
        var driver = Task.Run(Drive);
        window.Run();
        driver.GetAwaiter().GetResult();

        async Task Drive()
        {
            using var deadline = new CancellationTokenSource(TimeSpan.FromSeconds(40));
            string source = "";
            try
            {
                await Ui(() =>
                {
                    source = editor.Text;
                    editor.Focus();
                    editor.Selection = new(7, 9);
                    goTo.Show(anchor);
                });
                await Until(() => goTo.IsPending);
                await Check(() => goTo.Layout.Line.Focused && goTo.Layout.Line.Text == "2" && goTo.Layout.Column.Text == "2" &&
                    goTo.Layout.Line.GetBounds().Width >= 100 && goTo.Layout.Column.GetBounds().Width >= 100,
                    "The native dialog focuses its line field and shows the current coordinates in usable fields.");
                await Ui(() => NativeText("99"));
                await Check(() => goTo.ValidationError is not null, "Native line input immediately reports a nonexistent line.");
                await Ui(() => NativeKey(0x0D));
                await Ui(() => { });
                await Check(() => goTo.IsPending && editor.Selection == new TextSelection(7, 9),
                    "Enter cannot submit an invalid native line.");
                await Ui(() =>
                {
                    NativeText("2");
                    goTo.Layout.Column.Focus(selectAll: true);
                    NativeText("3");
                });
                await Check(() => goTo.ValidationError?.Contains("Unicode", StringComparison.Ordinal) == true,
                    "Native column input reports a split Unicode character.");
                await Ui(() => NativeText("4"));
                await Check(() => goTo.ValidationError is null, "Corrected native input clears the error.");
                await Ui(() => NativeKey(0x0D));
                await Until(() => navigations == 1);
                await Check(() => !goTo.IsPending && editor.Focused && editor.Selection == new TextSelection(9, 9) && editor.Text == source,
                    "Native Enter closes the dialog, then focuses the exact source caret without an edit.");

                await Open();
                await Ui(() => NativeKey(0x1B));
                await Until(() => !goTo.IsPending);
                await Check(() => editor.Focused && editor.Selection == new TextSelection(9, 9) && navigations == 1,
                    "Native Escape restores source focus and leaves its selection unchanged.");

                await Open();
                await Ui(() =>
                {
                    editor.Text = "// " + source;
                    version++;
                    goTo.Refresh();
                });
                await Check(() => goTo.ValidationError?.Contains("source changed", StringComparison.Ordinal) == true,
                    "Programmatic source replacement invalidates the open dialog.");
                await Ui(() =>
                {
                    editor.Text = source;
                    version++;
                    goTo.Refresh();
                });
                await Check(() => goTo.ValidationError is not null, "Restored source text cannot reuse a location from an old revision.");
                await Ui(() =>
                {
                    bool rejected = false;
                    try { goTo.View.Primary.Invoke(); }
                    catch (XuiException error) when (error.Message.Contains("disabled", StringComparison.Ordinal)) { rejected = true; }
                    Require(rejected, "The native primary button rejects invocation while source is stale.");
                });
                await Check(() => goTo.IsPending && navigations == 1, "A stale dialog cannot submit a navigation.");
                await Ui(() => goTo.View.CancelButton.Invoke());

                await Open();
                await Ui(() =>
                {
                    goTo.Layout.Line.Text = "1";
                    goTo.Layout.Column.Text = "1";
                    goTo.Refresh();
                    goTo.View.Primary.Invoke();
                    editor.ReplaceRange(new(0, 0), editor.Text, "// ");
                });
                await Until(() => errors.Count == 1);
                await Check(() => navigations == 1 && errors[0].Contains("source changed", StringComparison.Ordinal),
                    "A change after dismissal cancels the deferred navigation explicitly.");
                await Ui(() => editor.Command(TextCommand.Undo));
                await Check(() => editor.Text == source, "A rejected navigation preserves the native source undo operation.");

                await Ui(() =>
                {
                    editor.ReplaceRange(new(0, 0), editor.Text, "not valid xui\r");
                    editor.ReadOnly = true;
                });
                string changed = "";
                await Ui(() => changed = editor.Text);
                await Open();
                await Ui(() =>
                {
                    goTo.Layout.Line.Text = "3";
                    goTo.Layout.Column.Text = "4";
                    goTo.Refresh();
                    goTo.View.Primary.Invoke();
                });
                await Until(() => navigations == 2);
                await Check(() => editor.Text == changed && editor.Selection.Start == (ulong)(changed.IndexOf("A\U0001F680B", StringComparison.Ordinal) + 3),
                    "Navigation works with syntax-invalid, read-only source.");
                await Ui(() =>
                {
                    editor.ReadOnly = false;
                    editor.Command(TextCommand.Undo);
                });
                await Check(() => editor.Text == source, "Navigation does not consume or clear native undo.");

                await Open();
                await Ui(() =>
                {
                    goTo.Layout.Line.Text = "1";
                    goTo.Layout.Column.Text = "1";
                    goTo.Refresh();
                    goTo.Layout.Column.Text = "99";
                    goTo.View.Primary.Invoke();
                });
                await Until(() => errors.Count == 2);
                await Check(() => navigations == 2, "Submission rechecks field values rather than trusting cached validation.");

                await Open();
                await Ui(() =>
                {
                    goTo.View.Primary.Invoke();
                    goTo.Show(anchor);
                });
                await Until(() => goTo.IsPending);
                await Check(() => navigations == 2, "Reopening cancels navigation queued by an older dialog.");
                await Ui(() =>
                {
                    goTo.View.Primary.Invoke();
                    goTo.Dispose();
                });
                await Ui(() => { });
                await Check(() => navigations == 2 && window.CallbackStatus == 0,
                    "Disposal cancels pending navigation without native callback errors.");
            }
            finally { window.Post(window.Close); }

            async Task Open()
            {
                await Ui(() => goTo.Show(anchor));
                await Until(() => goTo.IsPending);
            }

            async Task Ui(Action action)
            {
                var done = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
                if (!window.Post(() =>
                {
                    try { action(); done.SetResult(); }
                    catch (Exception error) { done.SetException(error); }
                })) throw new InvalidOperationException("The source navigation test window closed.");
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

            Task Check(Func<bool> condition, string message) => Ui(() => Require(condition(), $"{style}: {message}"));
        }
    }

    private static void NativeText(string text)
    {
        nint editor = GetFocus();
        if (editor == 0 || SendMessageW(editor, 0x000C, 0, text) == 0)
            throw new InvalidOperationException("The native coordinate field rejected text.");
    }

    private static void NativeKey(uint key)
    {
        if (!PostMessageW(GetFocus(), 0x0100, key, 0)) throw new InvalidOperationException("Native dialog key posting failed.");
    }

    [DllImport("user32.dll")] private static extern nint GetFocus();
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern nint SendMessageW(nint window, uint message, nuint first, string second);
    [DllImport("user32.dll")] private static extern bool PostMessageW(nint window, uint message, nuint first, nint second);
}
