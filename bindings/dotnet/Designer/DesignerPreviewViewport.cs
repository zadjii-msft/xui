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
        Layout = new(window, preview, Presets, attach: false);
        Layout.Width.Text = "360";
        Layout.Height.Text = "640";
        Presets.Event += OnPreset;
        Layout.Apply.Click += QueueCustom;
        Layout.Width.Submitted += QueueCustom;
        Layout.Height.Submitted += QueueCustom;
        window.Closed += OnClosed;
        // The binding has no layout notification. Sample bounds without changing sizing or preview ownership.
        dimensionsTimer = new Timer(_ => QueueDimensions(), null, 250, 250);
    }

    internal Element View => Layout.Root;
    internal DesignerPreviewViewportLayout Layout { get; }
    internal ComboBox Presets { get; }
    internal DesignerViewportPreset Preset { get; private set; } = DesignerViewportPreset.Fit;
    internal (int Width, int Height)? RequestedSize { get; private set; }

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
        if (!TryDimension(Layout.Width.Text, out int width) || !TryDimension(Layout.Height.Text, out int height))
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
        Layout.Width.Text = width.ToString(CultureInfo.InvariantCulture);
        Layout.Height.Text = height.ToString(CultureInfo.InvariantCulture);
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
        if (Layout.Dimensions.Text != dimensions) Layout.Dimensions.Text = dimensions;
        if (Layout.Status.Text != status) Layout.Status.Text = status;
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
        window.Closed -= OnClosed;
        Presets.Event -= OnPreset;
        Layout.Apply.Click -= QueueCustom;
        Layout.Width.Submitted -= QueueCustom;
        Layout.Height.Submitted -= QueueCustom;
    }
}
