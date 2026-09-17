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
                ReportPicking("Render the current source before enabling Pick controls.", error: true);
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
                ReportPicking($"Could not change preview picking: {error.Message}", error: true);
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

    private void ReportPicking(string message, bool error = false)
    {
        view.PickStatus = Limit(message);
        if (error) ReportNavigation(message);
    }

    private void RequestHighlight()
    {
        if (disposed || highlightPosted) return;
        highlightPosted = true;
        if (!window.Post(() =>
        {
            highlightPosted = false;
            if (disposed || !workspace.IsCurrent || preview.AppliedVersion != version ||
                previewSource is not { } source || source.Version != version ||
                source.Source != editor.Text || workspace.Document?.Source != source.Source) return;
            var selected = workspace.Hierarchy.Selection;
            try
            {
                var result = preview.TryHighlight(version, selected?.Id);
                string detail = result switch
                {
                    PreviewHighlightResult.Applied => "Visible at the current layout.",
                    PreviewHighlightResult.Cleared => "No selected control.",
                    PreviewHighlightResult.StaleVersion => "The preview is not current.",
                    PreviewHighlightResult.NotVisible => "The control is hidden or clipped.",
                    PreviewHighlightResult.OccludedNative => "A native control overlaps the border.",
                    PreviewHighlightResult.UnsupportedSurface => "This surface does not support an outline.",
                    _ => throw new InvalidOperationException("Unknown preview outline result.")
                };
                view.OutlineStatus = $"Outline: {selected?.Kind ?? "none"}. {detail}";
            }
            catch (XuiException error) when (error.Status is 1 or 7)
            {
                view.OutlineStatus = Limit($"Outline update failed: {error.Message}");
                ReportNavigation(view.OutlineStatus);
            }
        }))
        {
            highlightPosted = false;
            throw new InvalidOperationException("The window rejected the preview outline request.");
        }
    }
}
