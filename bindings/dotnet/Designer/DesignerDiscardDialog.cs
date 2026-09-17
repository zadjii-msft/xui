namespace Xui.Designer;

internal sealed class DesignerDiscardDialog
{
    private sealed record Request(long Version, string Source, Action Continue);
    private readonly Window window;
    private readonly Func<long> version;
    private readonly Func<string> source;
    private readonly Action<string> report;
    private Request? pending;
    private bool queued;

    internal ContentDialog View { get; }
    internal DesignerDiscardLayout Layout { get; }
    internal bool IsPending => pending is not null;

    internal DesignerDiscardDialog(Window window, Func<long> version, Func<string> source, Action<string> report)
    {
        this.window = window;
        this.version = version;
        this.source = source;
        this.report = report;
        Layout = new DesignerDiscardLayout(window, attach: false);
        View = window.ContentDialog("Unsaved source", Layout.Root);
        View.Primary.Text = "Discard edits";
        View.Primary.SetAutomationId("designer-discard-confirm");
        View.CancelButton.Text = "Keep editing";
        View.CancelButton.SetAutomationId("designer-discard-cancel");
        View.OnResult(Complete);
    }

    internal void Show(Control anchor, string operation, Action continuation)
    {
        if (pending is not null)
        {
            report("Finish or cancel the current document confirmation first.");
            return;
        }
        Layout.Message.Text = $"Discard the current unsaved edits to {operation}?";
        pending = new(version(), source(), continuation);
        queued = false;
        try { View.Show(anchor); }
        catch
        {
            pending = null;
            throw;
        }
    }

    private void Complete(bool accepted)
    {
        if (pending is not { } request || queued) return;
        if (!accepted) { pending = null; return; }
        queued = true;
        // Native file dialogs require the content dialog to finish closing first.
        if (window.Post(() =>
        {
            pending = null;
            queued = false;
            if (request.Version != version() || request.Source != source())
            {
                report("The document changed during confirmation. No edits were discarded. Try the action again.");
                return;
            }
            request.Continue();
        })) return;
        pending = null;
        queued = false;
        report("The designer closed before the document action could continue.");
    }
}
