namespace Xui.Designer;

internal sealed partial class DesignerApplication
{
    private void RequestPicking(bool enabled)
    {
        long request = ++pickRequest;
        if (!window.Post(() =>
        {
            if (disposed || request != pickRequest) return;
            if (enabled && (preview.AppliedVersion != version || previewSource?.Source != editor.Text))
            {
                view.Pick.Checked = pickControls;
                ReportPicking("Render the current source before enabling Pick controls.");
                return;
            }
            try
            {
                preview.SetPointerPickMode(enabled);
                pickControls = enabled;
                view.Pick.Checked = enabled;
                ReportPicking(enabled
                    ? "Click a preview control to select its source. Keyboard and accessibility actions remain live."
                    : "Preview pointer actions are live.");
            }
            catch (XuiException error) when (error.Status is 1 or 7)
            {
                view.Pick.Checked = pickControls;
                ReportPicking($"Could not change preview picking: {error.Message}");
            }
        })) throw new InvalidOperationException("The window rejected the preview picking request.");
    }

    private void OnPreviewPicked(PreviewPick picked)
    {
        if (disposed || !pickControls || picked.Version != version || preview.AppliedVersion != picked.Version ||
            previewSource is not { } source || source.Version != picked.Version) return;
        workspace.SelectFromPreview(source.Source, picked.NodeId);
        ReportPicking(workspace.Inspector.Layout.Feedback.Text);
    }

    private void ReportPicking(string message) => view.PickStatus.Text = Limit(message);
}
