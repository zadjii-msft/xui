using Xui;
using Xui.Designer;

internal static partial class Program
{
    private static void RunPaletteFlyout(VisualStyle style)
    {
        using var window = new Window("Designer control flyout", 1000, 800, visualStyle: style);
        window.SetShowActivated(false);
        var editor = window.MultilineText("Source").SetDocument(
            "component Flyout { view { VStack() { Text(\"First\"); } } }");
        var anchor = window.Button("Add control...");
        var errors = new List<string>();
        using var workspace = new DesignerWorkspace(window, editor, errors.Add);
        var inspector = workspace.Inspector;
        window.SetContent(window.Stack().Add(anchor).Add(window.Stack(Axis.Horizontal)
            .Add(editor, 1).Add(inspector.Layout.Root, 1), 1));
        editor.Event += value => { if (value.Kind == EventKind.Change) workspace.SourceChanged(); };
        int assertions = 0;
        var driver = Task.Run(Drive);
        window.Run();
        driver.GetAwaiter().GetResult();
        Console.WriteLine($"Designer control flyout assertions ({style}): {assertions} passed.");

        async Task Drive()
        {
            using var deadline = new CancellationTokenSource(TimeSpan.FromSeconds(60));
            string original = "";
            ElementBounds editorBounds = default;
            nint nativeSource = 0;
            try
            {
                await Ui(workspace.SourceChanged);
                await Until(() => workspace.IsCurrent);
                await Ui(() =>
                {
                    original = editor.Text;
                    editor.Focus();
                    nativeSource = GetFocus();
                    editor.Selection = new(2, 4);
                    editorBounds = editor.GetBounds();
                    Require(!inspector.IsPaletteOpen && inspector.PaletteLayout.PaletteFilter.GetBounds().Width == 0,
                        "The palette has no workspace footprint before opening.");
                    inspector.ShowPalette(anchor);
                    Require(!inspector.IsPaletteOpen, "Opening is deferred until native dispatch returns.");
                });
                await Until(() => inspector.IsPaletteOpen);
                await Ui(() =>
                {
                    var popup = inspector.PaletteLayout.Root.GetBounds();
                    Require(popup.Y >= anchor.GetBounds().Y + anchor.GetBounds().Height &&
                        inspector.PaletteLayout.PaletteFilter.Focused && editor.GetBounds() == editorBounds,
                        "The native flyout anchors below the button without rearranging the editor.");
                    NativeQuery("slider");
                    Require(inspector.Template == ControlTemplate.RangeInput &&
                        inspector.PaletteLayout.PaletteHelp.Text == "1 control. Slider for a numeric value.",
                        "Real native query input filters the original palette templates.");
                    NativeKey(0x1B);
                });
                await Until(() => !inspector.IsPaletteOpen);
                await Ui(() =>
                {
                    Require(editor.Focused && editor.Selection == new TextSelection(2, 4) &&
                        editor.Text == original && !inspector.PaletteLayout.PaletteFilter.Focused,
                        "Escape restores source focus and selection without a source edit.");
                    inspector.ShowPalette(anchor);
                });
                await Until(() => inspector.IsPaletteOpen);
                await Ui(() =>
                {
                    Require(PostMessageW(nativeSource, 0x0201, 1, 0) &&
                        PostMessageW(nativeSource, 0x0202, 0, 0), "Post an outside click to the owned source editor.");
                });
                await Until(() => !inspector.IsPaletteOpen);
                await Ui(() =>
                {
                    Require(!inspector.PaletteLayout.PaletteFilter.Focused && editor.Text == original,
                        "An outside click dismisses the palette without editing source or retaining hidden focus.");
                    inspector.ShowPalette(anchor);
                });
                await Until(() => inspector.IsPaletteOpen);
                await Ui(() =>
                {
                    Require(inspector.PaletteLayout.PaletteFilter.Text == "slider",
                        "Reopening retains the palette query.");
                    NativeQuery("no-control-matches");
                    Require(inspector.Template is null, "An empty result does not invent a template.");
                    workspace.FindEmptyGridCell();
                    Require(inspector.PaletteLayout.Feedback.Text == inspector.Feedback &&
                        inspector.Feedback.Contains("Select a Grid", StringComparison.Ordinal),
                        "Grid placement errors remain visible in the flyout and inspector.");
                    NativeQuery("button");
                    inspector.PaletteLayout.Insert.Invoke();
                    Require(workspace.IsBusy, "Insertion retains the asynchronous compiler transaction.");
                    workspace.FindEmptyGridCell();
                    Require(inspector.Feedback.Contains("Wait", StringComparison.Ordinal),
                        "Grid placement retains the busy guard.");
                });
                await Until(() => workspace.IsCurrent && !workspace.IsBusy);
                await Ui(() =>
                {
                    Require(!inspector.IsPaletteOpen && editor.Focused &&
                        workspace.Document!.Root!.Children.Count == 2 &&
                        workspace.Hierarchy.Selection?.Kind == "Button",
                        "A compiled insertion closes the flyout before focusing and selecting its source.");
                    editor.Command(TextCommand.Undo);
                });
                await Until(() => workspace.IsCurrent);
                await Ui(() =>
                {
                    Require(editor.Text == original, "One native undo restores the source before insertion.");
                    inspector.ShowPalette(anchor);
                });
                await Until(() => inspector.IsPaletteOpen);
                await Ui(() =>
                {
                    editor.ReplaceRange(new(0, 0), editor.Text, "!");
                    workspace.FindEmptyGridCell();
                    Require(inspector.PaletteLayout.Feedback.Text.Contains("stale", StringComparison.Ordinal),
                        "Source changes retain the stale insertion guard and visible feedback.");
                    inspector.DismissPalette();
                    editor.Command(TextCommand.Undo);
                });
                await Until(() => workspace.IsCurrent);
                await Ui(() =>
                {
                    inspector.ShowPalette(anchor);
                    inspector.DismissPalette();
                });
                await Ui(() => Require(!inspector.IsPaletteOpen, "Dismiss cancels an already-posted opening."));
                await Ui(() => inspector.ShowPalette(anchor));
                await Until(() => inspector.IsPaletteOpen);
                await Ui(() =>
                {
                    inspector.Dispose();
                    inspector.Dispose();
                    Require(!inspector.IsPaletteOpen && !inspector.PaletteLayout.Root.IsOpen &&
                        !inspector.PaletteLayout.PaletteFilter.Focused,
                        "Disposal closes native content and never retains focus in hidden fields.");
                    Require(errors.Count == 0 && window.CallbackStatus == 0 && editor.Text == original,
                        "Flyout operations preserve source and have no callback errors.");
                });
            }
            finally { window.Post(window.Close); }

            async Task Ui(Action action)
            {
                var done = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
                if (!window.Post(() =>
                {
                    try { action(); done.SetResult(); }
                    catch (Exception error) { done.SetException(error); }
                })) throw new InvalidOperationException("The control flyout test window closed.");
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

            void Require(bool condition, string message)
            {
                if (!condition) throw new InvalidOperationException($"{style}: {message}");
                assertions++;
            }
        }
    }
}
