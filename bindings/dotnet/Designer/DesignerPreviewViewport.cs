using System.Globalization;

namespace Xui.Designer;

internal enum DesignerViewportPreset { Fit = 1, Compact, Medium, Wide, Custom }

internal sealed class DesignerPreviewViewport : IDisposable
{
    private readonly Window window;
    private readonly Timer dimensionsTimer;
    private int disposed, refreshPending;
    private bool selecting;
    private string? error;
    private Button? toolbarButton;
    private long flyoutRequest;

    internal DesignerPreviewViewport(Window window, Element preview)
    {
        ArgumentNullException.ThrowIfNull(window);
        ArgumentNullException.ThrowIfNull(preview);
        window.VerifyAccess();
        this.window = window;
        Presets = window.ComboBox("Preview viewport size", false)
            .SetAutomationId("designer-viewport-preset");
        Presets.SetItems([
            new(1, "Fit"),
            new(2, "Compact · 360 × 640"),
            new(3, "Medium · 768 × 1024"),
            new(4, "Wide · 1280 × 800"),
            new(5, "Custom")
        ], 1);
        Layout = new(window, preview, attach: false);
        Settings = new(window, Presets, attach: false);
        Settings.Width.Text = "360";
        Settings.Height.Text = "640";
        Presets.Event += OnPreset;
        Settings.Apply.Click += QueueCustom;
        Settings.Width.Submitted += QueueCustom;
        Settings.Height.Submitted += QueueCustom;
        Settings.Reset.Click += QueueReset;
        window.Closed += OnClosed;
        // The binding has no layout notification. Sample bounds without changing sizing or preview ownership.
        dimensionsTimer = new Timer(_ => QueueDimensions(), null, 250, 250);
    }

    internal Element View => Layout.Root;
    internal DesignerPreviewViewportLayout Layout { get; }
    internal DesignerPreviewSizeLayout Settings { get; }
    internal bool IsOpen => Volatile.Read(ref disposed) == 0 && Settings.Root.IsOpen;
    internal ComboBox Presets { get; }
    internal DesignerViewportPreset Preset { get; private set; } = DesignerViewportPreset.Fit;
    internal (int Width, int Height)? RequestedSize { get; private set; }

    internal void SetToolbarButton(Button button)
    {
        Guard();
        toolbarButton = button;
        RefreshDimensions();
    }

    internal void Show(Control anchor)
    {
        Guard();
        long request = ++flyoutRequest;
        Queue(() =>
        {
            if (request != flyoutRequest) return;
            RefreshDimensions();
            if (!Settings.Root.IsOpen) Settings.Root.Show(anchor);
            Presets.Focus();
        });
    }

    internal void Dismiss()
    {
        Guard();
        flyoutRequest++;
        if (Settings.Root.IsOpen) Settings.Root.Dismiss();
    }

    internal void SelectPreset(DesignerViewportPreset preset)
    {
        Guard();
        switch (preset)
        {
            case DesignerViewportPreset.Fit:
                window.Update(
                    new(Layout.Surface, PropertyKind.MinimumSize),
                    new(Layout.Surface, PropertyKind.MaximumSize, A: float.MaxValue, B: float.MaxValue),
                    new(Layout.Surface, PropertyKind.PreferredSize));
                RequestedSize = null;
                break;
            case DesignerViewportPreset.Compact: SetSize(360, 640); break;
            case DesignerViewportPreset.Medium: SetSize(768, 1024); break;
            case DesignerViewportPreset.Wide: SetSize(1280, 800); break;
            case DesignerViewportPreset.Custom:
                ApplyCustom();
                return;
            default: throw new ArgumentOutOfRangeException(nameof(preset));
        }
        CompleteSelection(preset);
    }

    internal bool ApplyCustom()
    {
        Guard();
        if (!TryDimension(Settings.Width.Text, out int width) || !TryDimension(Settings.Height.Text, out int height))
        {
            error = "Enter whole dimensions from 1 to 4096 DIP.";
            SynchronizeSelection();
            RefreshDimensions();
            return false;
        }
        SetSize(width, height);
        CompleteSelection(DesignerViewportPreset.Custom);
        return true;
    }

    private static bool TryDimension(string text, out int value) =>
        int.TryParse(text, NumberStyles.None, CultureInfo.InvariantCulture, out value) && value is >= 1 and <= 4096;

    private void SetSize(int width, int height)
    {
        Layout.Surface.FixedSize(width, height);
        RequestedSize = (width, height);
        Settings.Width.Text = width.ToString(CultureInfo.InvariantCulture);
        Settings.Height.Text = height.ToString(CultureInfo.InvariantCulture);
    }

    private void CompleteSelection(DesignerViewportPreset preset)
    {
        Preset = preset;
        error = null;
        SynchronizeSelection();
        Layout.Scroll.Offset = 0;
        RefreshDimensions();
    }

    private void SynchronizeSelection()
    {
        selecting = true;
        try { Presets.Select((ulong)Preset); }
        finally { selecting = false; }
    }

    private void OnPreset(UiEvent value)
    {
        if (selecting || value.Kind != EventKind.Selection) return;
        var preset = (DesignerViewportPreset)value.Value;
        Queue(() => SelectPreset(preset));
    }

    private void QueueCustom() => Queue(() => ApplyCustom());
    private void QueueReset() => Queue(() => SelectPreset(DesignerViewportPreset.Fit));

    private void Queue(Action action)
    {
        Guard();
        // Return from native input dispatch before a layout change can move a focused child.
        if (!window.Post(() => { if (Volatile.Read(ref disposed) == 0) action(); }))
            throw new InvalidOperationException("The window rejected the viewport request.");
    }

    internal void RefreshDimensions()
    {
        Guard();
        var bounds = Layout.Surface.GetBounds();
        string dimensions = string.Create(CultureInfo.InvariantCulture,
            $"{Preset} · {bounds.Width:0.#} × {bounds.Height:0.#} DIP");
        string status = error ?? (RequestedSize is { } requested && bounds.Width < requested.Width
            ? $"Width limited to the pane. Requested: {requested.Width} DIP."
            : "Vertical scrolling. Width fits the pane.");
        if (Settings.Dimensions.Text != dimensions) Settings.Dimensions.Text = dimensions;
        if (Settings.Status.Text != status) Settings.Status.Text = status;
        if (toolbarButton is not null)
        {
            string text = RequestedSize is { } size
                ? $"{Preset} - {size.Width}x{size.Height}"
                : string.Create(CultureInfo.InvariantCulture, $"Fit - {bounds.Width:0.#}x{bounds.Height:0.#}");
            if (toolbarButton.Text != text) toolbarButton.Text = text;
        }
    }

    private void QueueDimensions()
    {
        if (Volatile.Read(ref disposed) != 0 || Interlocked.Exchange(ref refreshPending, 1) != 0) return;
        if (!window.Post(() =>
        {
            Interlocked.Exchange(ref refreshPending, 0);
            if (Volatile.Read(ref disposed) == 0) RefreshDimensions();
        })) Interlocked.Exchange(ref refreshPending, 0);
    }

    private void Guard()
    {
        window.VerifyAccess();
        ObjectDisposedException.ThrowIf(Volatile.Read(ref disposed) != 0, this);
    }

    private void OnClosed(WindowClosedEventArgs _) => Dispose();

    public void Dispose()
    {
        window.VerifyAccess();
        if (Interlocked.Exchange(ref disposed, 1) != 0) return;
        dimensionsTimer.Dispose();
        flyoutRequest++;
        if (Settings.Root.IsOpen) Settings.Root.Dismiss();
        window.Closed -= OnClosed;
        Presets.Event -= OnPreset;
        Settings.Apply.Click -= QueueCustom;
        Settings.Width.Submitted -= QueueCustom;
        Settings.Height.Submitted -= QueueCustom;
        Settings.Reset.Click -= QueueReset;
        toolbarButton = null;
    }
}
