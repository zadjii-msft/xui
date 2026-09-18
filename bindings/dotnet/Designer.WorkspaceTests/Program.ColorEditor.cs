using Xui;
using Xui.Designer;

internal static partial class Program
{
    private static void RunColorEditor(VisualStyle style)
    {
        const string fixture = """
            component Colors {
                resources { Accent: 0xFF0000; }
                view { VStack() {
                    Text("Color", foreground: 0x112233, background: theme(light: 0xFFFFFF, dark: 0),
                        borderBrush: resource(Accent));
                    Button("Other", foreground: 0xABCDEF);
                } }
            }
            """;
        using var window = new Window("Designer color editor", 1200, 1000, visualStyle: style);
        window.SetShowActivated(false);
        var editor = window.MultilineText("Source").SetDocument(fixture);
        var errors = new List<string>();
        using var workspace = new DesignerWorkspace(window, editor, errors.Add);
        using var color = new DesignerColorEditor(window, workspace, errors.Add);
        var inspector = workspace.Inspector;
        var open = window.Button("Choose color");
        open.Click += () => color.Show(open);
        editor.Event += e => { if (e.Kind == EventKind.Change) { workspace.SourceChanged(); color.Refresh(); } };
        window.SetContent(window.Stack().Add(open).Add(window.Stack(Axis.Horizontal).Add(editor, 1)
            .Add(workspace.Hierarchy.Layout.Root, 1).Add(inspector.Layout.Root, 1), 1));
        int assertions = 0;
        var driver = Task.Run(Drive);
        window.Run();
        driver.GetAwaiter().GetResult();
        Console.WriteLine($"Designer color editor assertions ({style}): {assertions} passed.");

        async Task Drive()
        {
            using var deadline = new CancellationTokenSource(TimeSpan.FromSeconds(90));
            string initial = "", typed = "";
            try
            {
                await Ui(() => { Require(!color.CanShow, "An unpublished hierarchy cannot open color editing."); workspace.SourceChanged(); });
                await Ready();
                await Ui(() =>
                {
                    initial = editor.Text;
                    Choose(0);
                    foreach (string argument in new[] { "background", "borderBrush", "padding", "value" })
                    {
                        inspector.ChooseArgument(argument);
                        Require(!color.CanShow, "Expressions, resources, unset properties, and non-color properties stay outside the color editor.");
                    }
                    inspector.ChooseArgument("foreground");
                    inspector.Value.Text = "1 + 2";
                    open.Invoke();
                });
                await Until(() => errors.Count == 1);
                await Ui(() =>
                {
                    Require(!color.IsPending && inspector.Value.Text == "1 + 2" && editor.Text == initial,
                        "An invalid draft reports an error without opening or changing source.");
                    inspector.Value.Text = " 0x112233UL ";
                });
                await Open();
                await Ui(() =>
                {
                    Require(color.Picker.Value == new RgbaColor(0x11, 0x22, 0x33) &&
                        color.Picker.Channel(0).GetBounds().Width > 0 && color.ValidationError is null,
                        "The dialog decodes the active draft and arranges real native color channels.");
                    color.Picker.Channel(3).DecreaseButton.Invoke();
                    Require(color.Picker.Value.Alpha == 255 && color.Picker.Channel(3).Value == 255,
                        "The disabled native alpha channel refuses a step without changing its value.");
                    color.View.Primary.Invoke();
                });
                await Until(() => !color.IsPending && inspector.Value.Focused);
                await Ui(() =>
                {
                    Require(inspector.Value.Text == " 0x112233UL " && editor.Text == initial,
                        "Accepting an unchanged color preserves exact draft spelling and source.");
                });
                await Open();
                await Ui(() =>
                {
                    color.Picker.Channel(0).IncreaseButton.Invoke();
                    Require(color.Picker.Value.Red == 0x12 && color.ValidationError is null, "The native channel button changes the actual picker color.");
                    color.Picker.Channel(0).Editor.Focus();
                    NativeKey(0x1B);
                });
                await Until(() => !color.IsPending);
                await Ui(() => Require(inspector.Value.Text == " 0x112233UL " && editor.Text == initial,
                    "Native Escape discards only the color dialog draft."));
                await Open();
                await Ui(() =>
                {
                    color.Picker.Channel(0).Editor.Focus();
                    NativeQuery("not a channel");
                });
                await Until(() => color.ValidationError is not null);
                await Ui(() =>
                {
                    bool disabled = false;
                    try { color.View.Primary.Invoke(); }
                    catch (XuiException error) when (error.Message.Contains("disabled", StringComparison.Ordinal)) { disabled = true; }
                    Require(disabled && color.ValidationError!.Contains("channel", StringComparison.Ordinal) &&
                        editor.Text == initial && inspector.Value.Text == " 0x112233UL ",
                        "Invalid native channel text disables acceptance instead of silently using the previous color.");
                    color.View.CancelButton.Invoke();
                });
                await Until(() => !color.IsPending);
                await Open();
                await Ui(() =>
                {
                    Require(color.ValidationError is null && color.Picker.Channel(0).Editor.Text == "17",
                        "Reopening the same color clears cancelled invalid channel text.");
                    color.Picker.Channel(0).Editor.Focus();
                    NativeQuery("invalid again");
                });
                await Until(() => color.ValidationError is not null);
                await Ui(() =>
                {
                    NativeQuery("18");
                });
                await Until(() => color.ValidationError is null && color.Picker.Value.Red == 18);
                await Ui(() =>
                {
                    Require(color.Picker.Channel(0).Editor.Text == "18", "Valid native channel input recovers after a rejected draft.");
                    color.View.CancelButton.Invoke();
                });
                await Until(() => !color.IsPending);
                await Open();
                await Ui(() =>
                {
                    color.Picker.Channel(0).IncreaseButton.Invoke();
                    color.View.Primary.Invoke();
                });
                await Until(() => !color.IsPending && inspector.Value.Focused);
                await Ui(() =>
                {
                    Require(inspector.Value.Text == " 0x122233 " && editor.Text == initial && !workspace.IsBusy,
                        "Use color updates the raw draft only, after native modal dismissal.");
                    workspace.RevertPropertyDraft();
                    Require(inspector.Value.Text == "0x112233", "The existing Revert draft action restores an accepted color draft.");
                    editor.ReplaceRange(new(0, 0), editor.Text, "// Prior typing\r");
                });
                await Ready();
                await Ui(() => { typed = editor.Text; Choose(0); });
                await Open();
                await Ui(() =>
                {
                    color.Picker.Value = new(0x45, 0x67, 0x89);
                    color.Refresh();
                    color.View.Primary.Invoke();
                });
                await Until(() => !color.IsPending && inspector.Value.Focused);
                await Ui(() => inspector.Layout.Apply.Invoke());
                await Ready();
                await Ui(() =>
                {
                    Require(editor.Text.Contains("foreground: 0x456789", StringComparison.Ordinal),
                        "The accepted draft compiles through the existing Apply path.");
                    editor.Command(TextCommand.Undo);
                });
                await Ready();
                await Ui(() =>
                {
                    Require(editor.Text == typed, "One native source undo removes the color edit.");
                    editor.Command(TextCommand.Undo);
                });
                await Ready();
                await Ui(() => { Require(editor.Text == initial, "Color interaction preserves preceding source undo."); Choose(0); });
                await Open();
                await Ui(() =>
                {
                    color.Picker.Value = new(1, 2, 3, 4);
                    color.Refresh();
                    bool disabled = false;
                    try { color.View.Primary.Invoke(); }
                    catch (XuiException error) when (error.Message.Contains("disabled", StringComparison.Ordinal)) { disabled = true; }
                    Require(disabled && color.IsPending && color.ValidationError!.Contains("opaque", StringComparison.Ordinal),
                        "A nonopaque programmatic color cannot become a style literal.");
                    color.View.CancelButton.Invoke();
                });
                await Until(() => !color.IsPending);
                await Open();
                await Ui(() =>
                {
                    inspector.Value.Text = "0x010203";
                    color.Refresh();
                    Require(color.ValidationError!.Contains("draft changed", StringComparison.Ordinal),
                        "A changed property draft invalidates an open color dialog.");
                    color.View.CancelButton.Invoke();
                });
                await Until(() => !color.IsPending);
                await Ui(() => { Require(inspector.Value.Text == "0x010203", "Cancel keeps a newer external draft."); Choose(0); });
                await Open();
                await Ui(() =>
                {
                    color.Picker.Value = new(1, 2, 3);
                    color.View.Primary.Invoke();
                    Choose(1);
                });
                await Until(() => errors.Count == 2);
                await Ui(() => Require(inspector.Value.Text == "0xABCDEF" && editor.Text == initial,
                    "Selection changes after acceptance invalidate queued draft delivery."));
                await Open();
                await Ui(() =>
                {
                    editor.Text = initial + "\r// Changed";
                    workspace.SourceChanged();
                    color.Refresh();
                    Require(color.ValidationError is not null, "A new source snapshot invalidates the dialog before publication.");
                    color.View.CancelButton.Invoke();
                });
                await Until(() => !color.IsPending);
                await Ready();
                await Ui(() =>
                {
                    Require(errors.Count == 2 && window.CallbackStatus == 0, "Only deliberate refusals report errors; native callbacks remain valid.");
                    color.Show(open);
                    color.Dispose();
                });
                await Ui(() => Require(!color.IsPending && !color.CanShow, "Disposal cancels queued color opening."));
            }
            finally { window.Post(window.Close); }

            async Task Open()
            {
                await Ui(() => open.Invoke());
                await Until(() => color.IsPending);
            }
            Task Ready() => Until(() => workspace.IsCurrent && !workspace.IsBusy);
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
            async Task Ui(Action action)
            {
                var done = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
                if (!window.Post(() =>
                {
                    try { action(); done.SetResult(); }
                    catch (Exception error) { done.SetException(error); }
                })) throw new InvalidOperationException("The color test window rejected an action.");
                await done.Task.WaitAsync(deadline.Token);
            }
        }

        void Choose(int index)
        {
            var node = workspace.Document!.Root!.Children[index];
            editor.Selection = new((ulong)node.Span.Start, (ulong)node.Span.Start);
            workspace.SelectFromCaret();
            inspector.ChooseArgument("foreground");
        }
        void Require(bool condition, string message)
        {
            if (!condition) throw new InvalidOperationException($"{style}: {message}");
            assertions++;
        }
    }
}
