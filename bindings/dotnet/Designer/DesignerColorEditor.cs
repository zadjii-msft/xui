namespace Xui.Designer;

internal sealed class DesignerColorEditor : IDisposable
{
    private readonly Window window;
    private readonly DesignerWorkspace workspace;
    private readonly Action<string> report;
    private Guid revision;
    private int nodeId;
    private string argument = "", draft = "";
    private long generation;
    private bool disposed;

    internal ColorPicker Picker { get; }
    internal DesignerColorLayout Layout { get; }
    internal ContentDialog View { get; }
    internal bool IsPending { get; private set; }
    internal string? ValidationError { get; private set; }
    internal bool CanShow => !disposed && workspace.CanEditSelection && workspace.Inspector.CanChooseColor;

    internal DesignerColorEditor(Window window, DesignerWorkspace workspace, Action<string> report)
    {
        this.window = window;
        this.workspace = workspace;
        this.report = report;
        Picker = window.ColorPicker("Property color").SetAutomationId("designer-color-picker");
        Picker.Channel(3).Enabled = false;
        Layout = new DesignerColorLayout(window, Picker, attach: false);
        View = window.ContentDialog("Choose property color", Layout.Root);
        View.Primary.Text = "Use color";
        View.Primary.SetAutomationId("designer-color-confirm");
        View.CancelButton.SetAutomationId("designer-color-cancel");
        View.OnResult(Complete);
        Picker.Event += OnChange;
        for (uint channel = 0; channel < 4; channel++) Picker.Channel(channel).Editor.Event += OnChange;
        workspace.Inspector.Value.Event += OnChange;
        workspace.Changed += Refresh;
        workspace.SelectionChanged += Refresh;
    }

    internal void Show(Control anchor)
    {
        if (disposed) return;
        long request = ++generation;
        Post(() =>
        {
            if (disposed || request != generation) return;
            if (IsPending) { Picker.Channel(0).Focus(); return; }
            if (!CanShow) { Report("Select an existing literal color property in the current source first."); return; }
            draft = workspace.Inspector.Value.Text;
            if (!DesignerLiteralCodec.TryDecodeRgbColor(draft, out uint color, out var error))
            { Report(error!); return; }
            try
            {
                revision = workspace.Document!.Revision;
                nodeId = workspace.Hierarchy.Selection!.Id;
                argument = workspace.Inspector.Argument!;
                Layout.Summary.Text = $"Color for {workspace.Hierarchy.Selection.Kind}.{argument}";
                Picker.Value = new((byte)(color >> 16), (byte)(color >> 8), (byte)color);
                // A same-color assignment does not clear invalid native channel text.
                Picker.Channel(0).Value = (byte)(color >> 16);
                Picker.Channel(1).Value = (byte)(color >> 8);
                Picker.Channel(2).Value = (byte)color;
                Picker.Channel(3).Value = 255;
                IsPending = true;
                Refresh();
                View.Show(anchor);
                Picker.Channel(0).Focus();
            }
            catch (XuiException failure)
            {
                IsPending = false;
                Report($"Could not open the color editor: {failure.Message}");
            }
        });
    }

    private bool Current => CanShow && workspace.Document?.Revision == revision &&
        workspace.Hierarchy.Selection?.Id == nodeId && workspace.Inspector.Argument == argument &&
        workspace.Inspector.Value.Text == draft;

    private bool ReadValue(out string value, out string? error)
    {
        value = "";
        error = null;
        if (!Current)
        {
            error = "The source, selection, or property draft changed. Cancel and reopen the color editor.";
            return false;
        }
        var color = Picker.Value;
        byte[] channels = [color.Red, color.Green, color.Blue, color.Alpha];
        for (uint channel = 0; channel < channels.Length; channel++)
        {
            if (!double.TryParse(Picker.Channel(channel).Editor.Text, System.Globalization.NumberStyles.Float,
                System.Globalization.CultureInfo.InvariantCulture, out double number) || !double.IsFinite(number) ||
                number < 0 || number > 255 || Math.Round(number, MidpointRounding.AwayFromZero) != channels[channel])
            {
                error = "Enter valid color channel values from 0 through 255.";
                return false;
            }
        }
        if (color.Alpha != 255)
        {
            error = "Style colors must be opaque. Cancel and reopen the color editor.";
            return false;
        }
        try
        {
            value = DesignerLiteralCodec.EncodeRgbColor(draft, (uint)(color.Red << 16 | color.Green << 8 | color.Blue));
            return true;
        }
        catch (ArgumentException failure) { error = failure.Message; return false; }
    }

    private void OnChange(UiEvent value)
    {
        if (value.Kind == EventKind.Change) Refresh();
    }

    internal void Refresh()
    {
        if (disposed || !IsPending) return;
        ReadValue(out _, out var error);
        ValidationError = error;
        View.Primary.Enabled = error is null;
        View.SetValidationMessage(error ?? "");
    }

    private void Complete(bool accepted)
    {
        if (disposed || !IsPending) return;
        IsPending = false;
        long request = ++generation;
        if (!accepted) return;
        bool valid = ReadValue(out string value, out var error);
        Post(() =>
        {
            if (disposed || request != generation) return;
            if (!valid) { Report(error!); return; }
            if (!Current) { Report("The property changed before the color draft was ready. No draft was replaced."); return; }
            try
            {
                if (value != draft) workspace.Inspector.Value.Text = value;
                workspace.Inspector.Layout.Feedback.Text = value == draft
                    ? "The color is unchanged. No source edit was applied."
                    : "Color draft updated. Choose Apply to validate and update source.";
                workspace.Inspector.FocusValue();
            }
            catch (XuiException failure) { Report($"Could not update the color draft: {failure.Message}"); }
        });
    }

    private void Report(string message)
    {
        workspace.Inspector.Layout.Feedback.Text = message;
        report(message);
    }

    private void Post(Action action)
    {
        if (!window.Post(action)) throw new InvalidOperationException("The window rejected the color editor request.");
    }

    public void Dispose()
    {
        if (disposed) return;
        disposed = true;
        generation++;
        IsPending = false;
        Picker.Event -= OnChange;
        for (uint channel = 0; channel < 4; channel++) Picker.Channel(channel).Editor.Event -= OnChange;
        workspace.Inspector.Value.Event -= OnChange;
        workspace.Changed -= Refresh;
        workspace.SelectionChanged -= Refresh;
    }
}
