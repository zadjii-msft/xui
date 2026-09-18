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
        new(DesignerCommandId.ToggleComment, "Source: Toggle line comments", sourceComments.Toggle, "Ctrl+/ in source",
            () => !editor.ReadOnly),
        new(DesignerCommandId.SelectFromCaret, "Selection: Select control from source caret", workspace.SelectFromCaret, "Ctrl+Shift+L",
            () => workspace.IsCurrent),
        new(DesignerCommandId.SelectParent, "Selection: Select parent control", () => workspace.SelectRelative(DesignerSelectionTarget.Parent),
            CanExecute: () => workspace.CanSelectRelative(DesignerSelectionTarget.Parent)),
        new(DesignerCommandId.SelectFirstChild, "Selection: Select first child", () => workspace.SelectRelative(DesignerSelectionTarget.FirstChild),
            CanExecute: () => workspace.CanSelectRelative(DesignerSelectionTarget.FirstChild)),
        new(DesignerCommandId.SelectPreviousSibling, "Selection: Select previous sibling", () => workspace.SelectRelative(DesignerSelectionTarget.PreviousSibling),
            CanExecute: () => workspace.CanSelectRelative(DesignerSelectionTarget.PreviousSibling)),
        new(DesignerCommandId.SelectNextSibling, "Selection: Select next sibling", () => workspace.SelectRelative(DesignerSelectionTarget.NextSibling),
            CanExecute: () => workspace.CanSelectRelative(DesignerSelectionTarget.NextSibling)),
        new(DesignerCommandId.SelectRoot, "Selection: Select root control", () => workspace.SelectRelative(DesignerSelectionTarget.Root),
            CanExecute: () => workspace.CanSelectRelative(DesignerSelectionTarget.Root)),
        new(DesignerCommandId.FocusSource, "Focus: Source editor", () => editor.Focus()),
        new(DesignerCommandId.FocusHierarchy, "Focus: Control hierarchy", () => workspace.Hierarchy.Tree.Focus(),
            CanExecute: () => workspace.IsCurrent),
        new(DesignerCommandId.FocusHierarchySearch, "Hierarchy: Find controls", () => workspace.Hierarchy.Layout.Query.Focus()),
        new(DesignerCommandId.FocusPalette, "Focus: Search control palette", () => workspace.Inspector.Layout.PaletteFilter.Focus()),
        new(DesignerCommandId.FocusPropertySearch, "Properties: Find a property", () => workspace.Inspector.Layout.ArgumentFilter.Focus()),
        new(DesignerCommandId.FocusProperty, "Focus: Property value", workspace.Inspector.FocusValue,
            CanExecute: () => workspace.IsCurrent && workspace.Hierarchy.Selection is not null),
        new(DesignerCommandId.RevertPropertyDraft, "Properties: Revert draft", workspace.RevertPropertyDraft,
            CanExecute: () => workspace.CanRevertPropertyDraft),
        new(DesignerCommandId.RevealPropertySource, "Properties: Reveal value in source", workspace.RevealPropertySource,
            CanExecute: () => workspace.CanRevealPropertySource),
        new(DesignerCommandId.ChooseColor, "Properties: Choose literal color", () => colorEditor.Show(view.Commands),
            CanExecute: () => colorEditor.CanShow),
        new(DesignerCommandId.DuplicateControl, "Control: Duplicate selected control", workspace.Duplicate, "Ctrl+D in hierarchy",
            () => workspace.CanEditSelection && workspace.Inspector.CanDeleteOrDuplicate),
        new(DesignerCommandId.DeleteControl, "Control: Delete selected control", workspace.DeleteSelection, "Delete in hierarchy",
            () => workspace.CanEditSelection && workspace.Inspector.CanDeleteOrDuplicate),
        new(DesignerCommandId.MoveControlUp, "Control: Move up", () => workspace.Move(-1), "Alt+Up in hierarchy",
            () => workspace.CanEditSelection && workspace.Inspector.CanMoveUp),
        new(DesignerCommandId.MoveControlDown, "Control: Move down", () => workspace.Move(1), "Alt+Down in hierarchy",
            () => workspace.CanEditSelection && workspace.Inspector.CanMoveDown),
        new(DesignerCommandId.WrapVertical, "Control: Wrap in VStack", () => workspace.Wrap(ControlTemplate.VStack), "Ctrl+G in hierarchy",
            () => workspace.CanEditSelection && workspace.Inspector.CanWrap),
        new(DesignerCommandId.WrapHorizontal, "Control: Wrap in HStack", () => workspace.Wrap(ControlTemplate.HStack),
            CanExecute: () => workspace.CanEditSelection && workspace.Inspector.CanWrap),
        new(DesignerCommandId.WrapScroll, "Control: Wrap in ScrollView", () => workspace.Wrap(ControlTemplate.ScrollView),
            CanExecute: () => workspace.CanEditSelection && workspace.Inspector.CanWrap),
        new(DesignerCommandId.UnwrapControl, "Control: Unwrap one-child container", workspace.Unwrap, "Ctrl+Shift+G in hierarchy",
            () => workspace.CanEditSelection && workspace.Inspector.CanUnwrap),
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
