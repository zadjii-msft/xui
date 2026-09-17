using System.Diagnostics;
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
            Run();
            RunCloseLifetime();
            Console.WriteLine($"Designer viewport native assertions: {assertions} passed.");
            return 0;
        }
        catch (Exception error) { Console.Error.WriteLine(error); return 1; }
    }

    private static void RunCloseLifetime()
    {
        using var window = new Window("Designer viewport close regression", 640, 480);
        window.SetShowActivated(false);
        var preview = window.Label("Borrowed preview");
        Throws<ArgumentNullException>(() => new DesignerPreviewViewport(null!, preview),
            "A null window is rejected.");
        Throws<ArgumentNullException>(() => new DesignerPreviewViewport(window, null!),
            "A null preview is rejected.");
        using var viewport = new DesignerPreviewViewport(window, preview);
        window.SetContent(window.Stack().Add(viewport.View, 1));
        window.Post(() =>
        {
            viewport.Layout.Apply.Invoke();
            window.Close();
        });
        window.Run();
        Throws<ObjectDisposedException>(() => viewport.RefreshDimensions(),
            "Closing the window disposes the viewport and cancels posted sizing work.");
    }

    private static void Run()
    {
        using var window = new Window("Designer viewport regression", 1700, 900, visualStyle: VisualStyle.WinUI);
        window.SetShowActivated(false);
        var host = window.CreateContentHost();
        using var viewport = new DesignerPreviewViewport(window, host);
        var frame = window.Stack().Add(viewport.View, 1).MaximumSize(1400, 720);
        window.SetContent(frame);
        using var candidate = host.BeginUpdate();
        var previewRoot = window.Stack().Spacing(4);
        var counter = window.Button("Count: 0");
        var input = window.TextInput("Retained preview input");
        var editor = window.MultilineText("Retained document").SetDocument("Original");
        int count = 0, inputChanges = 0;
        counter.Click += () => counter.Text = $"Count: {++count}";
        input.Changed += _ => inputChanges++;
        previewRoot.Add(counter).Add(input).Add(editor, 1);
        candidate.SetInspectionTargets([new(0, previewRoot), new(1, counter), new(2, input)]);
        candidate.Commit(previewRoot);
        ulong counterId = counter.Id, inputId = input.Id;
        Exception? failure = null;
        var test = Task.Run(async () =>
        {
            try
            {
                var elapsed = Stopwatch.StartNew();
                bool ready = false;
                while (!ready)
                {
                    await Ui(() => ready = host.GetBounds().Height > 100);
                    if (elapsed.Elapsed > TimeSpan.FromSeconds(15)) throw new TimeoutException("The viewport did not receive usable bounds.");
                    if (!ready) await Task.Delay(30);
                }
                float fitWidth = 0, fitHeight = 0;
                await Ui(() =>
                {
                    Require(viewport.Preset == DesignerViewportPreset.Fit && viewport.RequestedSize is null,
                        "The viewport starts in Fit.");
                    fitWidth = host.GetBounds().Width;
                    fitHeight = host.GetBounds().Height;
                    Require(fitWidth > 300 && fitHeight > 100, "Fit fills the available preview pane.");
                    Require(fitHeight == viewport.Layout.Scroll.GetBounds().Height,
                        "Fit uses the scroll viewport height, not the preview's preferred height.");
                    viewport.RefreshDimensions();
                    Require(viewport.Layout.Dimensions.Text.Contains($"{fitWidth:0.#} × {fitHeight:0.#}", StringComparison.Ordinal),
                        "Fit reports the actual arranged dimensions.");
                    Require(viewport.Layout.Apply.Text == "Set" && viewport.Layout.Width.GetBounds().Width > 30,
                        "The native custom-dimension controls have usable bounds.");
                    var style = viewport.Layout.ToolbarHost.GetControlStyleValues(StylePart.Root, effective: true);
                    Require(style.Background == new ThemeColor(0xF3F3F3, 0x272727) &&
                        style.BorderBrush == new ThemeColor(0xCCCCCC, 0x454545),
                        "The viewport toolbar uses theme-aware colors.");
                    counter.Invoke();
                    input.Text = "State survives sizing";
                    input.Selection = new(2, 6);
                    editor.ReplaceRange(new(0, 0), editor.Text, "Edited ");
                    viewport.SelectPreset(DesignerViewportPreset.Compact);
                });
                await Settle();
                float top = 0;
                await Ui(() =>
                {
                    Size(360, 640, "Compact");
                    top = host.GetBounds().Y;
                    viewport.Layout.Scroll.Offset = 100;
                });
                await Settle();
                await Ui(() =>
                {
                    var scrolled = host.GetBounds();
                    Require(scrolled.Y < top && scrolled.Height == 640 && scrolled.Width == 360,
                        "A tall fixed viewport scrolls vertically without shrinking its content.");
                    viewport.SelectPreset(DesignerViewportPreset.Medium);
                });
                await Settle();
                await Ui(() =>
                {
                    Size(Math.Min(768, fitWidth), 1024, "Medium");
                    Require(host.GetBounds().Y == top, "A new preset starts at the top.");
                    viewport.SelectPreset(DesignerViewportPreset.Wide);
                });
                await Settle();
                await Ui(() =>
                {
                    Size(Math.Min(1280, fitWidth), 800, "Wide");
                    Require(viewport.RequestedSize == (1280, 800), "Wide retains the requested dimensions.");
                    viewport.SelectPreset(DesignerViewportPreset.Fit);
                });
                await Settle();
                await Ui(() =>
                {
                    Size(fitWidth, fitHeight, "Restored Fit");
                    Require(counter.Id == counterId && input.Id == inputId && counter.Text == "Count: 1" &&
                        input.Text == "State survives sizing" && input.Selection == new TextSelection(2, 6),
                        "Preset changes preserve native control identities, values, and selection.");
                    var buttonBounds = counter.GetBounds();
                    Require(host.TryHitTest(buttonBounds.X + 2, buttonBounds.Y + 2, out int picked) && picked == 1,
                        "The retained content inspection map uses the updated viewport bounds.");
                    Require(candidate.Highlight(1) == ContentHighlightResult.Applied,
                        "The retained candidate still accepts an outline.");
                    candidate.Highlight(null);
                    viewport.Layout.Width.Text = "240";
                    viewport.Layout.Height.Text = "900";
                    viewport.Layout.Apply.Invoke();
                    Require(viewport.Preset == DesignerViewportPreset.Fit,
                        "The custom-size button defers layout changes until native dispatch returns.");
                });
                await Settle();
                await Ui(() =>
                {
                    Require(viewport.Preset == DesignerViewportPreset.Custom, "The posted custom-size request runs.");
                    Size(240, 900, "Custom");
                    foreach (string invalid in new[] { "", "0", "-1", "4097", "NaN", "Infinity", "1.5", " 20", "2147483648" })
                    {
                        viewport.Layout.Width.Text = invalid;
                        viewport.Layout.Height.Text = "800";
                        Require(!viewport.ApplyCustom(), $"Invalid width '{invalid}' is rejected.");
                        Size(240, 900, "Invalid width preserves the last viewport");
                        Require(viewport.Layout.Status.Text == "Enter whole dimensions from 1 to 4096 DIP.",
                            "Invalid dimensions show a visible error.");
                    }
                    viewport.Layout.Width.Text = "300";
                    viewport.Layout.Height.Text = "0";
                    Require(!viewport.ApplyCustom() && viewport.RequestedSize == (240, 900),
                        "An invalid height does not partially apply a valid width.");
                    viewport.Layout.Width.Text = "1";
                    viewport.Layout.Height.Text = "1";
                    Require(viewport.ApplyCustom(), "The lower custom limit is accepted.");
                });
                await Settle();
                await Ui(() =>
                {
                    Size(1, 1, "Minimum custom size");
                    viewport.Layout.Width.Text = "4096";
                    viewport.Layout.Height.Text = "4096";
                    Require(viewport.ApplyCustom(), "The upper custom limit is accepted.");
                });
                await Settle();
                await Ui(() =>
                {
                    Size(fitWidth, 4096, "Maximum custom size caps width to the pane");
                    viewport.RefreshDimensions();
                    Require(viewport.Layout.Status.Text.Contains("Width limited", StringComparison.Ordinal),
                        "A requested width beyond the pane has an explicit limitation.");
                    Throws<ArgumentOutOfRangeException>(() => viewport.SelectPreset((DesignerViewportPreset)99),
                        "An unknown preset is rejected.");
                    viewport.SelectPreset(DesignerViewportPreset.Compact);
                    frame.MaximumSize(438, 500);
                });
                await Settle();
                await Ui(() =>
                {
                    Size(360, 640, "Compact in the default Designer pane");
                    var toolbar = viewport.Layout.ToolbarHost.GetBounds();
                    var width = viewport.Layout.Width.GetBounds();
                    var height = viewport.Layout.Height.GetBounds();
                    var apply = viewport.Layout.Apply.GetBounds();
                    Require(toolbar.Width == 438 && width.Width >= 70 && height.Width >= 70 && apply.Width == 44,
                        "The dimension controls remain usable in a 438-DIP Designer pane.");
                    Require(width.X + width.Width <= height.X && height.X + height.Width <= apply.X &&
                        apply.X + apply.Width <= toolbar.X + toolbar.Width &&
                        viewport.Presets.GetBounds().Width <= toolbar.Width,
                        "The viewport selector and dimension controls stay inside the default Designer pane.");
                    frame.MaximumSize(240, 500);
                });
                await Task.Delay(600);
                await Ui(() =>
                {
                    var narrow = host.GetBounds();
                    Require(narrow.Width < 240 && narrow.Height == 640, "A narrow pane reflows width and retains the fixed height.");
                    Require(viewport.Layout.Dimensions.Text.Contains($"{narrow.Width:0.#} × 640", StringComparison.Ordinal),
                        "The dimension label follows a pane resize without an explicit refresh.");
                    Require(viewport.Layout.Status.Text.Contains("Width limited", StringComparison.Ordinal),
                        "The resize reports the missing native horizontal-scroll support.");
                    viewport.SelectPreset(DesignerViewportPreset.Fit);
                });
                await Settle();
                await Ui(() =>
                {
                    Require(host.GetBounds().Height == viewport.Layout.Scroll.GetBounds().Height &&
                        host.GetBounds().Height < fitHeight, "Fit follows the resized pane.");
                    frame.MaximumSize(1400, 720);
                    window.SetTheme(Theme.Light);
                    window.SetVisualStyle(VisualStyle.Classic);
                    viewport.SelectPreset(DesignerViewportPreset.Compact);
                });
                await Settle();
                await Ui(() =>
                {
                    Size(360, 640, "Classic compact");
                    window.SetVisualStyle(VisualStyle.WinUI);
                    viewport.SelectPreset(DesignerViewportPreset.Fit);
                });
                await Settle();
                await Ui(() =>
                {
                    Size(fitWidth, fitHeight, "Fit after theme and style changes");
                    counter.Invoke();
                    Require(count == 2 && counter.Id == counterId && input.Id == inputId &&
                        input.Text == "State survives sizing" && inputChanges == 0,
                        "Theme, style, scrolling, and dimensions preserve the content and its callbacks.");
                    editor.Command(TextCommand.Undo);
                    Require(editor.Text == "Original", "Sizing preserves native preview undo history.");
                    viewport.Presets.Select((ulong)DesignerViewportPreset.Medium);
                    Require(viewport.Preset == DesignerViewportPreset.Fit, "Preset selection defers the layout change.");
                });
                await Settle();
                await Ui(() =>
                {
                    Require(viewport.Preset == DesignerViewportPreset.Medium, "The native selector applies the posted preset.");
                    Size(Math.Min(768, fitWidth), 1024, "Selector medium");
                    viewport.Dispose();
                    viewport.Dispose();
                    Throws<ObjectDisposedException>(() => viewport.SelectPreset(DesignerViewportPreset.Fit),
                        "Disposed viewport operations are rejected.");
                    Require(counter.Text == "Count: 2" && input.Text == "State survives sizing",
                        "Viewport disposal does not dispose the borrowed preview.");
                });
            }
            catch (Exception error) { failure = error; }
            finally { window.Post(window.Close); }

            Task Settle() => Task.Delay(80);
            Task Ui(Action action)
            {
                var done = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
                if (!window.Post(() =>
                {
                    try { action(); done.SetResult(); }
                    catch (Exception error) { done.SetException(error); }
                })) throw new InvalidOperationException("The viewport test window closed early.");
                return done.Task.WaitAsync(TimeSpan.FromSeconds(15));
            }
            void Size(float width, float height, string context)
            {
                var bounds = host.GetBounds();
                Require(bounds.Width == width && bounds.Height == height,
                    $"{context}: expected {width} × {height}, actual {bounds.Width} × {bounds.Height}.");
            }
        });
        window.Run();
        test.GetAwaiter().GetResult();
        if (failure is not null) throw new InvalidOperationException("Viewport native regression failed.", failure);
    }

    private static void Require(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
        assertions++;
    }

    private static void Throws<T>(Action action, string message) where T : Exception
    {
        try { action(); }
        catch (T) { assertions++; return; }
        throw new InvalidOperationException(message);
    }
}
