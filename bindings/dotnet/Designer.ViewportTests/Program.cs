using System.Diagnostics;
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
        var anchor = window.Button("Size");
        window.SetContent(window.Stack().Add(anchor).Add(viewport.View, 1));
        window.Post(() =>
        {
            viewport.Show(anchor);
            viewport.Settings.Apply.Invoke();
            window.Close();
        });
        window.Run();
        Throws<ObjectDisposedException>(() => viewport.RefreshDimensions(),
            "Closing the window disposes the viewport and cancels posted sizing work.");
    }

    private static void Run(VisualStyle style)
    {
        using var window = new Window("Designer viewport regression", 1700, 900, visualStyle: style);
        window.SetShowActivated(false);
        var host = window.CreateContentHost();
        using var viewport = new DesignerPreviewViewport(window, host);
        var anchor = window.Button("Size");
        viewport.SetToolbarButton(anchor);
        var frame = window.Stack().Add(anchor).Add(viewport.View, 1).MaximumSize(1400, 620);
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
                await Ui(() => viewport.Show(anchor));
                await Settle();
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
                    Require(viewport.Settings.Dimensions.Text.Contains($"{fitWidth:0.#} × {fitHeight:0.#}", StringComparison.Ordinal),
                        "Fit reports the actual arranged dimensions.");
                    Require(viewport.IsOpen && viewport.Settings.Apply.Text == "Set" &&
                        viewport.Settings.Width.GetBounds().Width > 30 && viewport.Settings.Root.GetBounds().Y >=
                        anchor.GetBounds().Y + anchor.GetBounds().Height,
                        "The custom-dimension controls open in a native flyout below their anchor.");
                    Require(anchor.Text == $"Fit - {fitWidth:0.#}x{fitHeight:0.#}",
                        "The toolbar reports the actual Fit extent.");
                    viewport.Settings.Width.Focus();
                    Require(PostMessageW(GetFocus(), 0x0100, 0x1B, 0), "Post native Escape to the size field.");
                });
                await Settle();
                await Ui(() =>
                {
                    Require(!viewport.IsOpen, "Escape closes the size flyout.");
                    viewport.Show(anchor);
                });
                await Settle();
                await Ui(() =>
                {
                    viewport.Presets.Popup.Show(viewport.Presets);
                    viewport.Presets.Choices.Focus();
                    Require(viewport.IsOpen && viewport.Presets.Popup.IsOpen,
                        "The preset ComboBox opens its native popup inside the size flyout.");
                    Require(PostMessageW(GetFocus(), 0x0100, 0x1B, 0), "Post native Escape to the preset choices.");
                });
                await Settle();
                await Ui(() =>
                {
                    Require(viewport.IsOpen && !viewport.Presets.Popup.IsOpen,
                        "The first Escape dismisses only the nested preset popup.");
                    viewport.Settings.Width.Focus();
                    Require(PostMessageW(GetFocus(), 0x0100, 0x1B, 0), "Post Escape to the parent flyout.");
                });
                await Settle();
                await Ui(() =>
                {
                    Require(!viewport.IsOpen && !viewport.Settings.Width.Focused &&
                        host.GetBounds().Width == fitWidth && host.GetBounds().Height == fitHeight,
                        "Escape closes the native flyout without reserving preview space or leaving hidden focus.");
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
                    Require(anchor.Text == "Wide - 1280x800", "Wide reports requested dimensions in the toolbar.");
                    viewport.Settings.Reset.Invoke();
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
                    viewport.Settings.Width.Text = "240";
                    viewport.Settings.Height.Text = "900";
                    viewport.Settings.Apply.Invoke();
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
                        viewport.Settings.Width.Text = invalid;
                        viewport.Settings.Height.Text = "800";
                        Require(!viewport.ApplyCustom(), $"Invalid width '{invalid}' is rejected.");
                        Size(240, 900, "Invalid width preserves the last viewport");
                        Require(viewport.Settings.Status.Text == "Enter whole dimensions from 1 to 4096 DIP.",
                            "Invalid dimensions show a visible error.");
                    }
                    viewport.Settings.Width.Text = "300";
                    viewport.Settings.Height.Text = "0";
                    Require(!viewport.ApplyCustom() && viewport.RequestedSize == (240, 900),
                        "An invalid height does not partially apply a valid width.");
                    viewport.Settings.Width.Text = "1";
                    viewport.Settings.Height.Text = "1";
                    Require(viewport.ApplyCustom(), "The lower custom limit is accepted.");
                });
                await Settle();
                await Ui(() =>
                {
                    Size(1, 1, "Minimum custom size");
                    viewport.Settings.Width.Text = "4096";
                    viewport.Settings.Height.Text = "4096";
                    Require(viewport.ApplyCustom(), "The upper custom limit is accepted.");
                });
                await Settle();
                await Ui(() =>
                {
                    Size(fitWidth, 4096, "Maximum custom size caps width to the pane");
                    viewport.RefreshDimensions();
                    Require(viewport.Settings.Status.Text.Contains("Width limited", StringComparison.Ordinal),
                        "A requested width beyond the pane has an explicit limitation.");
                    Throws<ArgumentOutOfRangeException>(() => viewport.SelectPreset((DesignerViewportPreset)99),
                        "An unknown preset is rejected.");
                    viewport.SelectPreset(DesignerViewportPreset.Compact);
                    frame.MaximumSize(438, 500);
                    viewport.Show(anchor);
                });
                await Settle();
                await Ui(() =>
                {
                    Size(360, 640, "Compact in the default Designer pane");
                    var toolbar = viewport.Settings.Root.GetBounds();
                    var width = viewport.Settings.Width.GetBounds();
                    var height = viewport.Settings.Height.GetBounds();
                    var apply = viewport.Settings.Apply.GetBounds();
                    Require(toolbar.Width == 340 && width.Width >= 70 && height.Width >= 70 && apply.Width == 44,
                        "The size flyout has usable dimensions independent of the 438-DIP preview pane.");
                    Require(width.X + width.Width <= height.X && height.X + height.Width <= apply.X &&
                        apply.X + apply.Width <= toolbar.X + toolbar.Width &&
                        viewport.Presets.GetBounds().Width <= toolbar.Width,
                        "The viewport selector and dimension controls stay inside the anchored flyout.");
                    viewport.Dismiss();
                    frame.MaximumSize(240, 500);
                });
                await Task.Delay(600);
                await Ui(() =>
                {
                    var narrow = host.GetBounds();
                    Require(narrow.Width < 240 && narrow.Height == 640, "A narrow pane reflows width and retains the fixed height.");
                    Require(viewport.Settings.Dimensions.Text.Contains($"{narrow.Width:0.#} × 640", StringComparison.Ordinal),
                        "The dimension label follows a pane resize without an explicit refresh.");
                    Require(viewport.Settings.Status.Text.Contains("Width limited", StringComparison.Ordinal),
                        "The resize reports the missing native horizontal-scroll support.");
                    Require(anchor.Text == "Compact - 360x640",
                        "A capped preview width does not replace the requested size in the toolbar.");
                    viewport.SelectPreset(DesignerViewportPreset.Fit);
                });
                await Task.Delay(600);
                await Ui(() =>
                {
                    Require(host.GetBounds().Height == viewport.Layout.Scroll.GetBounds().Height &&
                        host.GetBounds().Height < fitHeight, "Fit follows the resized pane.");
                    var fit = host.GetBounds();
                    Require(anchor.Text == $"Fit - {fit.Width:0.#}x{fit.Height:0.#}",
                        "The dimension sampler updates the Fit toolbar label after pane resizing without an explicit refresh.");
                    frame.MaximumSize(1400, 620);
                    window.SetTheme(Theme.Light);
                    window.SetVisualStyle(VisualStyle.Classic);
                    viewport.SelectPreset(DesignerViewportPreset.Compact);
                });
                await Settle();
                await Ui(() =>
                {
                    Size(360, 640, "Classic compact");
                    window.SetVisualStyle(style);
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
                    viewport.RefreshDimensions();
                    Require(anchor.Text == $"Fit - {fitWidth:0.#}x{fitHeight:0.#}",
                        "Reset cropping restores the current actual extent in the toolbar.");
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

    [DllImport("user32.dll")] private static extern nint GetFocus();
    [DllImport("user32.dll")] private static extern bool PostMessageW(nint window, uint message, nuint first, nint second);
}
