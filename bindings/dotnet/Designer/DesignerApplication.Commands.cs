namespace Xui.Designer;

internal sealed partial class DesignerApplication
{
    private void ShowCommands()
    {
        RequestPicking(false);
        commandPalette.Show();
    }

    private void ShowGoTo()
    {
        RequestPicking(false);
        sourceGoTo.Show(view.GoToLine);
    }

    private IReadOnlyList<DesignerCommand> DesignerCommands() =>
    [
        new(DesignerCommandId.New, "File: New from selected template", NewDocument, "Ctrl+N"),
        new(DesignerCommandId.Open, "File: Open component", () => fileActions.OpenChooser(view.OpenPicker), "Ctrl+O"),
        new(DesignerCommandId.Save, "File: Save component", Save, "Ctrl+S"),
        new(DesignerCommandId.SaveAs, "File: Save component as", fileActions.SaveAs, "Ctrl+Shift+S"),
        new(DesignerCommandId.Recovery, "File: Browse recovery drafts", () => view.Recovery.Invoke()),
        new(DesignerCommandId.Undo, "Source: Undo", () => SourceCommand(TextCommand.Undo), "Ctrl+Z"),
        new(DesignerCommandId.Redo, "Source: Redo", () => SourceCommand(TextCommand.Redo), "Ctrl+Y"),
        new(DesignerCommandId.Find, "Source: Find text", () => sourceSearch.HandleKey(new('F', KeyModifiers.Control, editor.Id)), "Ctrl+F"),
        new(DesignerCommandId.Replace, "Source: Find and replace text", () => sourceSearch.HandleKey(new('H', KeyModifiers.Control, editor.Id)), "Ctrl+H"),
        new(DesignerCommandId.GoToLine, "Source: Go to line and column", ShowGoTo, "Ctrl+G in source"),
        new(DesignerCommandId.SelectFromCaret, "Selection: Select control from source caret", workspace.SelectFromCaret, "Ctrl+Shift+L",
            () => workspace.IsCurrent),
        new(DesignerCommandId.FocusSource, "Focus: Source editor", () => editor.Focus()),
        new(DesignerCommandId.FocusHierarchy, "Focus: Control hierarchy", () => workspace.Hierarchy.Tree.Focus(),
            CanExecute: () => workspace.IsCurrent),
        new(DesignerCommandId.FocusPalette, "Focus: Search control palette", () => workspace.Inspector.Layout.PaletteFilter.Focus()),
        new(DesignerCommandId.FocusProperty, "Focus: Property value", workspace.Inspector.FocusValue,
            CanExecute: () => workspace.IsCurrent && workspace.Hierarchy.Selection is not null),
        new(DesignerCommandId.Render, "Preview: Render current source", () => Schedule(immediate: true), "Ctrl+Enter"),
        new(DesignerCommandId.LivePreview, "Preview: Toggle live preview", () => view.Live.Invoke(), Checked: live),
        new(DesignerCommandId.PickControls, "Preview: Pick controls", () => RequestPicking(true),
            CanExecute: () => preview.AppliedVersion == version && previewSource?.Source == editor.Text),
        new(DesignerCommandId.Fit, "Preview size: Fit pane", () => viewport.SelectPreset(DesignerViewportPreset.Fit),
            Checked: viewport.Preset == DesignerViewportPreset.Fit),
        new(DesignerCommandId.Compact, "Preview size: Compact", () => viewport.SelectPreset(DesignerViewportPreset.Compact),
            Checked: viewport.Preset == DesignerViewportPreset.Compact),
        new(DesignerCommandId.Medium, "Preview size: Medium", () => viewport.SelectPreset(DesignerViewportPreset.Medium),
            Checked: viewport.Preset == DesignerViewportPreset.Medium),
        new(DesignerCommandId.Wide, "Preview size: Wide", () => viewport.SelectPreset(DesignerViewportPreset.Wide),
            Checked: viewport.Preset == DesignerViewportPreset.Wide),
        new(DesignerCommandId.Output, "View: Toggle Output", () => view.OutputToggle.Invoke(), Checked: view.OutputExpanded),
        new(DesignerCommandId.VisualStyle, "Appearance: Switch Classic / WinUI", () => view.StyleToggle.Invoke()),
        new(DesignerCommandId.Theme, "Appearance: Toggle light theme", () => view.Light.Invoke(), Checked: light)
    ];
}
