using System.Runtime.InteropServices;
using System.Text;

namespace Xui.Designer;

internal sealed partial class DesignerApplication
{
    private async Task SelectionSmoke()
    {
        const string fixture = """
            component SelectionFixture {
                state string Caption = "Do not execute";
                view {
                    VStack(spacing: 8) {
                        Text("Find target", padding: 4, foreground: 0x112233);
                        Button(Caption, size: (180, 40), enabled: true, click: Activate);
                        TextInput("Preview input", text: "Preview text");
                    }
                }
                code csharp {
                    private void Activate() {
                        Caption = "Activated";
                    }
                }
            }
            """;
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(180));
        int assertions = 0;
        string lastState = "Waiting for the first UI action.";
        try
        {
            var compiled = PreviewCompiler.Compile(fixture, timeout.Token);
            if (!compiled.Success) throw new InvalidOperationException("Selection fixture compilation failed: " + compiled.Diagnostics);
            await Ready();
            await Ui(() => editor.ReplaceRange(new(0, (ulong)editor.Text.Length), editor.Text, fixture));
            await Ready();
            string source = "";
            int buttonId = -1;
            await Ui(() =>
            {
                source = editor.Text;
                buttonId = workspace.Document!.Root!.Children[1].Id;
                sourceSearch.HandleKey(new('F', KeyModifiers.Control, 0));
                sourceSearch.Layout.Query.Text = "Find target";
                sourceSearch.Refresh();
                editor.Selection = new(0, 0);
                sourceSearch.Layout.Next.Invoke();
                Require(editor.Selection.Start == (ulong)source.IndexOf("Find target", StringComparison.Ordinal) &&
                    workspace.Hierarchy.Selection?.Kind == "Text" && editor.Focused,
                    "The integrated Find toolbar selects exact source and its hierarchy control.");
                Require(editor.GetBounds().Height >= 150 && sourceSearch.Layout.Query.GetBounds().Width >= 80,
                    "The full application keeps usable source and query geometry.");
                view.Pick.Invoke();
            });
            await Until(() => pickControls);
            await Ui(() =>
            {
                Require(view.PickStatus.Contains("Keyboard and accessibility", StringComparison.Ordinal),
                    "Pick mode reports its pointer-only contract.");
                SelectionNative.Click("Do not execute");
            });
            await Until(() => workspace.Hierarchy.Selection?.Id == buttonId);
            await Until(() => view.OutlineStatus.StartsWith("Outline: Button.", StringComparison.Ordinal));
            await Ui(() =>
            {
                var button = workspace.Document!.Root!.Children[1];
                Require(editor.Selection == new TextSelection((ulong)button.Span.Start, (ulong)button.Span.End) && editor.Focused,
                    "A real native preview click selects the complete authored control and focuses source.");
                Require(editor.Text == source && preview.AppliedVersion == version &&
                    ButtonText() == "Do not execute",
                    "Picking consumes the authored pointer action without changing source or retiring preview.");
                var selection = editor.Selection;
                Require(!workspace.SelectFromPreview(source, int.MaxValue) && editor.Selection == selection,
                    "An unknown preview node cannot move source selection.");
                Require(!workspace.SelectFromPreview(source + "\r", buttonId) && editor.Selection == selection,
                    "A mismatched preview source cannot reuse current hierarchy IDs.");
                long staleVersion = version;
                editor.ReplaceRange(new(0, 0), source, "// Shift\r");
                Require(view.OutlineStatus.StartsWith("Outline cleared.", StringComparison.Ordinal),
                    "A source revision invalidates the displayed outline feedback immediately.");
                selection = editor.Selection;
                OnPreviewPicked(new(staleVersion, buttonId));
                Require(editor.Selection == selection, "An old preview version cannot select from a newer source revision.");
            });
            await Ready();
            await Ui(() =>
            {
                source = editor.Text;
                editor.Selection = new(0, 0);
                SelectionNative.Click("Do not execute");
            });
            await Until(() => editor.Selection.Start == (ulong)workspace.Document!.Root!.Children[1].Span.Start);
            await Ui(() =>
            {
                Require(pickControls && workspace.Hierarchy.Selection?.Kind == "Button",
                    "Pick mode continues across a successful native preview replacement.");
                editor.Command(TextCommand.Undo);
            });
            await Ready();
            await Ui(() =>
            {
                Require(!editor.Text.StartsWith("// Shift", StringComparison.Ordinal),
                    "Preview picking preserves the preceding native source undo operation.");
                view.Pick.Invoke();
            });
            await Until(() => !pickControls);
            await Ui(() => SelectionNative.Click("Do not execute"));
            await Until(() => ButtonText() == "Activated");
            long styledVersion = 0;
            await Ui(() =>
            {
                styledVersion = version;
                view.StyleToggle.Invoke();
            });
            await Until(() => window.Style == VisualStyle.Classic);
            await Ui(() =>
            {
                Require(version == styledVersion && preview.AppliedVersion == styledVersion && ButtonText() == "Activated",
                    "Classic style preserves the applied preview and its authored state without recompilation.");
                Require(workspace.Hierarchy.Tree.GetControlStyleValues(StylePart.Root, effective: true).RowHeight == 28 &&
                    workspace.Hierarchy.Tree.GetControlStyleValues(StylePart.Row, effective: true).Padding == new Insets(4, 2, 4, 2),
                    "Classic style retains the compact hierarchy rows.");
                view.StyleToggle.Invoke();
            });
            await Until(() => window.Style == VisualStyle.WinUI);
            await Ui(() => Require(version == styledVersion && ButtonText() == "Activated",
                "Switching back to WinUI preserves the same preview state."));
            await Ui(() => viewport.SelectPreset(DesignerViewportPreset.Compact));
            await Until(() => viewport.Preset == DesignerViewportPreset.Compact);
            await Ui(() =>
            {
                viewport.RefreshDimensions();
                var bounds = preview.View.GetBounds();
                Require(bounds.Width == 360 && bounds.Height == 640,
                    "Compact preview applies its exact dimensions in the complete Designer shell.");
                Require(version == styledVersion && preview.AppliedVersion == styledVersion && ButtonText() == "Activated",
                    "Viewport resizing preserves the preview version and authored control state without recompilation.");
                view.Pick.Invoke();
            });
            await Until(() => pickControls);
            await Ui(() => SelectionNative.Click("Do not execute"));
            await Until(() => workspace.Hierarchy.Selection?.Id == buttonId);
            await Ui(() =>
            {
                Require(ButtonText() == "Activated" && editor.Selection.Start ==
                    (ulong)workspace.Document!.Root!.Children[1].Span.Start,
                    "Native pointer picking remains source-aligned inside a fixed-size viewport.");
                viewport.SelectPreset(DesignerViewportPreset.Fit);
            });
            await Until(() => viewport.Preset == DesignerViewportPreset.Fit);
            await Ui(() =>
            {
                Require(version == styledVersion && preview.AppliedVersion == styledVersion && ButtonText() == "Activated",
                    "Returning to Fit retains the same live preview and authored state.");
                view.Commands.Invoke();
            });
            await Until(() => commandPalette.IsOpen && !pickControls);
            await Ui(() =>
            {
                Require(commandPalette.Surface.Editor.Focused && version == styledVersion && ButtonText() == "Activated",
                    "The Commands button leaves pointer picking before opening the native palette without resetting preview state.");
                commandPalette.Surface.Invoke((ulong)DesignerCommandId.Replace);
            });
            await Until(() => !commandPalette.IsOpen && sourceSearch.Layout.ReplaceOpen && sourceSearch.Layout.Replacement.Focused);
            await Ui(() =>
            {
                Require(window.KeyHandler!(new('P', KeyModifiers.Control | KeyModifiers.Shift, editor.Id)),
                    "Ctrl+Shift+P opens Designer command discovery.");
            });
            await Until(() => commandPalette.IsOpen);
            await Ui(() =>
            {
                Require(!window.KeyHandler!(new('S', KeyModifiers.Control, commandPalette.Surface.Editor.Id)),
                    "Source and file shortcuts do not intercept keys inside the command palette.");
                SelectionNative.Key(0x1B);
            });
            await Until(() => !commandPalette.IsOpen);
            await Ui(() => Require(sourceSearch.Layout.FindOpen && sourceSearch.Layout.ReplaceOpen,
                "Escape dismisses the command palette without closing the underlying Find panel."));
            await Ui(view.Commands.Invoke);
            await Until(() => commandPalette.IsOpen);
            await Ui(() => commandPalette.Surface.Invoke((ulong)DesignerCommandId.FocusHierarchySearch));
            await Until(() => !commandPalette.IsOpen && workspace.Hierarchy.Layout.Query.Focused);
            await Ui(() =>
            {
                var button = workspace.Document!.Root!.Children[1];
                var key = workspace.Hierarchy.Key(button);
                workspace.Hierarchy.Layout.Query.Text = "button Caption";
                workspace.Hierarchy.RefreshSearch();
                workspace.Hierarchy.Layout.NextMatch.Invoke();
                Require(workspace.Hierarchy.Selection?.Id == buttonId &&
                    editor.Selection == new TextSelection((ulong)button.Span.Start, (ulong)button.Span.End) &&
                    workspace.Hierarchy.Tree.Selection.Focused == key,
                    "Hierarchy search selects exact source and the existing native tree row through the command palette.");
                Require(version == styledVersion && preview.AppliedVersion == styledVersion && ButtonText() == "Activated",
                    "Hierarchy search preserves the live preview version and authored state.");
                Require(window.KeyHandler!(new(0x1B, KeyModifiers.None, workspace.Hierarchy.Layout.Query.Id)) &&
                    workspace.Hierarchy.Layout.Query.Text == "" && sourceSearch.Layout.FindOpen && sourceSearch.Layout.ReplaceOpen,
                    "Escape in hierarchy search clears only that query instead of closing source Find.");
            });
            ulong? navigationControl = null;
            await Ui(() =>
            {
                Require(preview.TryReadNode(version, buttonId, out var node), "Selection navigation starts from a current native preview.");
                navigationControl = node.ControlId;
            });
            foreach (var (id, expectedId) in new[]
            {
                (DesignerCommandId.SelectParent, 0), (DesignerCommandId.SelectFirstChild, 1),
                (DesignerCommandId.SelectNextSibling, buttonId), (DesignerCommandId.SelectPreviousSibling, 1),
                (DesignerCommandId.SelectRoot, 0)
            })
            {
                await Ui(view.Commands.Invoke);
                await Until(() => commandPalette.IsOpen);
                await Ui(() => commandPalette.Surface.Invoke((ulong)id));
                await Until(() => !commandPalette.IsOpen && workspace.Hierarchy.Tree.Focused);
                await Ui(() =>
                {
                    var node = workspace.Hierarchy.Selection!;
                    Require(node.Id == expectedId && editor.Selection ==
                        new TextSelection((ulong)node.Span.Start, (ulong)node.Span.End),
                        $"Palette action {id} selects the expected hierarchy node and exact source range.");
                    Require(version == styledVersion && preview.AppliedVersion == styledVersion && ButtonText() == "Activated" &&
                        preview.TryReadNode(version, buttonId, out var button) && button.ControlId == navigationControl,
                        "Relative selection retains the native preview identity and authored state.");
                });
            }
            await Ui(view.Commands.Invoke);
            await Until(() => commandPalette.IsOpen);
            await Ui(() =>
            {
                foreach (var id in new[] { DesignerCommandId.SelectParent, DesignerCommandId.SelectRoot,
                    DesignerCommandId.SelectPreviousSibling, DesignerCommandId.SelectNextSibling })
                {
                    bool refused = false;
                    try { commandPalette.Surface.Invoke((ulong)id); }
                    catch (XuiException error) when (error.Message.Contains("disabled", StringComparison.Ordinal)) { refused = true; }
                    Require(refused && commandPalette.IsOpen, $"The native palette disables {id} at the root.");
                }
                commandPalette.Surface.CloseButton.Invoke();
            });
            await Until(() => !commandPalette.IsOpen);
            TextSelection expansionSelection = default;
            await Ui(() =>
            {
                workspace.Inspector.ChooseArgument("spacing");
                workspace.Inspector.Value.Text = "99";
                expansionSelection = editor.Selection;
                view.Commands.Invoke();
            });
            await Until(() => commandPalette.IsOpen);
            await Ui(() => commandPalette.Surface.Invoke((ulong)DesignerCommandId.CollapseHierarchy));
            await Until(() => !commandPalette.IsOpen && workspace.Hierarchy.Tree.Focused);
            await Ui(() =>
            {
                var root = workspace.Document!.Root!;
                workspace.Hierarchy.Tree.Select(workspace.Hierarchy.Key(root.Children[0]));
                Require(workspace.Hierarchy.Tree.Selection.Focused == workspace.Hierarchy.Key(root) &&
                    editor.Selection == expansionSelection && workspace.Inspector.Value.Text == "99",
                    "Palette collapse hides native child rows without changing source selection or the active property draft.");
                view.Commands.Invoke();
            });
            await Until(() => commandPalette.IsOpen);
            await Ui(() => commandPalette.Surface.Invoke((ulong)DesignerCommandId.ExpandHierarchy));
            await Until(() => !commandPalette.IsOpen && !workspace.IsExpandingHierarchy && workspace.Hierarchy.Tree.Focused);
            await Ui(() =>
            {
                Require(editor.Selection == expansionSelection && workspace.Inspector.Value.Text == "99" &&
                    version == styledVersion && preview.AppliedVersion == styledVersion && ButtonText() == "Activated",
                    "Palette expansion preserves source, drafts, preview version, and authored state.");
                var child = workspace.Document!.Root!.Children[0];
                workspace.Hierarchy.Tree.Select(workspace.Hierarchy.Key(child));
                Require(workspace.Hierarchy.Tree.Selection.Focused == workspace.Hierarchy.Key(child),
                    "Expanded children become selectable through the native tree.");
                view.Commands.Invoke();
            });
            await Until(() => commandPalette.IsOpen);
            await Ui(() =>
            {
                foreach (var id in new[] { DesignerCommandId.ExpandHierarchy, DesignerCommandId.CollapseHierarchy,
                    DesignerCommandId.CancelHierarchyExpansion })
                {
                    bool refused = false;
                    try { commandPalette.Surface.Invoke((ulong)id); }
                    catch (XuiException error) when (error.Message.Contains("disabled", StringComparison.Ordinal)) { refused = true; }
                    Require(refused, $"The palette disables {id} for a leaf without pending expansion.");
                }
                commandPalette.Surface.CloseButton.Invoke();
            });
            await Until(() => !commandPalette.IsOpen);
            int selectedTextStart = 0, selectedInputStart = 0;
            await Ui(() =>
            {
                selectedTextStart = workspace.Document!.Root!.Children[0].Span.Start;
                selectedInputStart = workspace.Document.Root.Children[2].Span.Start;
                editor.Selection = new((ulong)selectedTextStart, (ulong)selectedTextStart + 4);
                view.Commands.Invoke();
            });
            await Until(() => commandPalette.IsOpen);
            await Ui(() => commandPalette.Surface.Invoke((ulong)DesignerCommandId.FindSelection));
            await Until(() => !commandPalette.IsOpen && sourceSearch.Layout.Query.Focused);
            await Ui(() => Require(sourceSearch.Layout.Query.Text == "Text" &&
                editor.Selection == new TextSelection((ulong)selectedTextStart, (ulong)selectedTextStart + 4),
                "Find selected text opens the complete native query without changing the source selection."));
            foreach (var (id, start, kind) in new[]
            {
                (DesignerCommandId.FindSelectionNext, selectedInputStart, "TextInput"),
                (DesignerCommandId.FindSelectionPrevious, selectedTextStart, "Text")
            })
            {
                await Ui(view.Commands.Invoke);
                await Until(() => commandPalette.IsOpen);
                await Ui(() => commandPalette.Surface.Invoke((ulong)id));
                await Until(() => !commandPalette.IsOpen && editor.Focused);
                await Ui(() => Require(editor.Selection == new TextSelection((ulong)start, (ulong)start + 4) &&
                    workspace.Hierarchy.Selection?.Kind == kind && version == styledVersion &&
                    preview.AppliedVersion == styledVersion && ButtonText() == "Activated",
                    $"Palette action {id} synchronizes hierarchy selection without changing preview state."));
            }
            await Ui(() =>
            {
                Require(window.KeyHandler!(new(0x72, KeyModifiers.Control, editor.Id)) &&
                    editor.Selection.Start == (ulong)selectedInputStart,
                    "Source Ctrl+F3 uses the current selection in the complete Designer shell.");
                Require(window.KeyHandler!(new(0x72, KeyModifiers.Control | KeyModifiers.Shift, editor.Id)) &&
                    editor.Selection.Start == (ulong)selectedTextStart,
                    "Source Ctrl+Shift+F3 navigates back through the same current matches.");
                view.Path.Focus();
                Require(!window.KeyHandler!(new(0x72, KeyModifiers.Control, view.Path.Id)),
                    "Ctrl+F3 in the file path is not intercepted by source search.");
                editor.Selection = new(0, 0);
                view.Commands.Invoke();
            });
            await Until(() => commandPalette.IsOpen);
            await Ui(() =>
            {
                foreach (var id in new[] { DesignerCommandId.FindSelection, DesignerCommandId.FindSelectionNext,
                    DesignerCommandId.FindSelectionPrevious })
                {
                    bool refused = false;
                    try { commandPalette.Surface.Invoke((ulong)id); }
                    catch (XuiException error) when (error.Message.Contains("disabled", StringComparison.Ordinal)) { refused = true; }
                    Require(refused && commandPalette.IsOpen, $"The palette disables {id} for an empty source selection.");
                }
                commandPalette.Surface.CloseButton.Invoke();
            });
            await Until(() => !commandPalette.IsOpen);
            await Ui(() => view.Pick.Invoke());
            await Until(() => pickControls);
            await Ui(view.GoToLine.Invoke);
            await Until(() => sourceGoTo.IsPending && !pickControls);
            int destination = 0;
            await Ui(() =>
            {
                destination = editor.Text.IndexOf("Find target", StringComparison.Ordinal) + 3;
                var line = Microsoft.CodeAnalysis.Text.SourceText.From(editor.Text).Lines.GetLineFromPosition(destination);
                sourceGoTo.Layout.Line.Text = (line.LineNumber + 1).ToString(System.Globalization.CultureInfo.InvariantCulture);
                sourceGoTo.Layout.Column.Text = (destination - line.Start + 1).ToString(System.Globalization.CultureInfo.InvariantCulture);
                sourceGoTo.Refresh();
                sourceGoTo.View.Primary.Invoke();
            });
            await Until(() => !sourceGoTo.IsPending && editor.Focused);
            await Ui(() =>
            {
                Require(editor.Selection == new TextSelection((ulong)destination, (ulong)destination) &&
                    workspace.Hierarchy.Selection?.Kind == "Text",
                    "Go to line leaves pointer picking and synchronizes the hierarchy without expanding the exact caret.");
                Require(version == styledVersion && preview.AppliedVersion == styledVersion && ButtonText() == "Activated",
                    "Source navigation preserves the compiled preview and authored control state.");
                view.Path.Focus();
                Require(!window.KeyHandler!(new('G', KeyModifiers.Control, view.Path.Id)),
                    "Ctrl+G in a different text field does not open source navigation.");
                editor.Focus();
                Require(window.KeyHandler!(new('G', KeyModifiers.Control, editor.Id)),
                    "Ctrl+G in source opens the location dialog.");
            });
            await Until(() => sourceGoTo.IsPending);
            await Ui(() => sourceGoTo.View.CancelButton.Invoke());
            await Until(() => !sourceGoTo.IsPending);
            await Ui(view.Commands.Invoke);
            await Until(() => commandPalette.IsOpen);
            await Ui(() => commandPalette.Surface.Invoke((ulong)DesignerCommandId.GoToLine));
            await Until(() => sourceGoTo.IsPending);
            await Ui(() =>
            {
                Require(!commandPalette.IsOpen && sourceGoTo.Layout.Line.Focused,
                    "The Go to command opens its native dialog after the command palette closes.");
                sourceGoTo.View.CancelButton.Invoke();
            });
            await Until(() => !sourceGoTo.IsPending);
            await Ui(() =>
            {
                source = editor.Text;
                sourceSearch.HandleKey(new('H', KeyModifiers.Control, 0));
                sourceSearch.Layout.Query.Text = "Find target";
                sourceSearch.Layout.Replacement.Text = "Updated target";
                sourceSearch.Layout.ReplaceAll.Invoke();
                Require(editor.Text.Contains("Updated target", StringComparison.Ordinal) && version > styledVersion,
                    "Integrated replacement updates source and supersedes the preview through the normal edit pipeline.");
            });
            await Ready();
            await Ui(() =>
            {
                Require(preview.AppliedVersion == version && workspace.Document!.Source == editor.Text,
                    "Replacement publishes a matching hierarchy and compiled preview.");
                editor.Command(TextCommand.Undo);
            });
            await Ready();
            await Ui(() =>
            {
                Require(editor.Text == source && preview.AppliedVersion == version,
                    "One native undo restores the pre-replacement source and its preview.");
                sourceSearch.Layout.Close.Invoke();
            });
            await Ui(view.Commands.Invoke);
            await Until(() => commandPalette.IsOpen);
            await Ui(() => commandPalette.Surface.Invoke((ulong)DesignerCommandId.New));
            await Until(() => fileActions.Discard.IsPending);
            await Ui(() =>
            {
                Require(!commandPalette.IsOpen && editor.Text == source,
                    "New from the command palette keeps dirty source behind the existing discard confirmation.");
                fileActions.Discard.View.CancelButton.Invoke();
            });
            await Until(() => !fileActions.Discard.IsPending);
            await Ui(() => Require(editor.Text == source, "Canceling a command-palette file action preserves the complete source."));
            await Ui(view.Commands.Invoke);
            await Until(() => commandPalette.IsOpen);
            await Ui(() => commandPalette.Surface.Invoke((ulong)DesignerCommandId.FocusPalette));
            await Until(() => !commandPalette.IsOpen);
            await Ui(() =>
            {
                var field = workspace.Inspector.Layout.PaletteFilter.GetBounds();
                var panel = view.InspectorPanel.GetBounds();
                Require(workspace.Inspector.Layout.PaletteFilter.Focused &&
                    field.Y >= panel.Y && field.Y + field.Height <= panel.Y + panel.Height,
                    "The control-palette focus command reveals its native field inside the inspector.");
            });
            await Ui(() =>
            {
                var button = workspace.Document!.Root!.Children[1];
                workspace.Hierarchy.Tree.Select(workspace.Hierarchy.Key(button));
                workspace.Inspector.ChooseArgument("size");
                workspace.Inspector.Layout.DimensionMode.Invoke();
                view.Commands.Invoke();
            });
            await Until(() => commandPalette.IsOpen);
            await Ui(() => commandPalette.Surface.Invoke((ulong)DesignerCommandId.FocusPropertySearch));
            await Until(() => !commandPalette.IsOpen && workspace.Inspector.Layout.ArgumentFilter.Focused);
            await Ui(() =>
            {
                var field = workspace.Inspector.Layout.ArgumentFilter.GetBounds();
                var panel = view.InspectorPanel.GetBounds();
                Require(field.Width >= 100 && field.Y >= panel.Y && field.Y + field.Height <= panel.Y + panel.Height,
                    "The property-search command focuses and reveals its native field inside the inspector.");
                workspace.Inspector.Layout.DimensionWidth.Text = "200";
                workspace.Inspector.Layout.ArgumentFilter.Text = "enabled";
                workspace.Inspector.FilterArguments();
                Require(workspace.Inspector.IsDimensionMode && workspace.Inspector.Argument == "size" &&
                    workspace.Inspector.Layout.DimensionWidth.Text == "200" &&
                    workspace.Inspector.Layout.ArgumentLabel.Text == "Editing: size" &&
                    editor.Text == source && preview.TryReadNode(version, buttonId, out var untouched) &&
                    untouched.Bounds.Width == 180,
                    "Property filtering retains the current dimension draft without changing source or preview layout.");
                workspace.Inspector.Layout.ClearArgumentFilter.Invoke();
                Require(workspace.Inspector.IsDimensionMode && workspace.Inspector.Layout.DimensionWidth.Text == "200",
                    "Clearing property filters retains the active draft in the complete application.");
                view.Commands.Invoke();
            });
            await Until(() => commandPalette.IsOpen);
            ulong? revertPreviewControl = null;
            await Ui(() =>
            {
                Require(preview.TryReadNode(version, buttonId, out var beforeRevert) && beforeRevert.ControlId is not null,
                    "Draft reversion starts with an owned native preview control.");
                revertPreviewControl = beforeRevert.ControlId;
                commandPalette.Surface.Invoke((ulong)DesignerCommandId.RevertPropertyDraft);
            });
            await Until(() => !commandPalette.IsOpen && workspace.Inspector.Layout.DimensionWidth.Focused);
            await Ui(() =>
            {
                Require(workspace.Inspector.IsDimensionMode && workspace.Inspector.Layout.DimensionWidth.Text == "180" &&
                    workspace.Inspector.Layout.DimensionHeight.Text == "40" && editor.Text == source &&
                    preview.TryReadNode(version, buttonId, out var unchanged) && unchanged.Bounds.Width == 180 &&
                    unchanged.ControlId == revertPreviewControl,
                    "The Revert draft command restores dimensions without compiling or replacing the current native preview.");
                view.Commands.Invoke();
            });
            await Until(() => commandPalette.IsOpen);
            await Ui(() => commandPalette.Surface.Invoke((ulong)DesignerCommandId.FocusProperty));
            await Until(() => !commandPalette.IsOpen && workspace.Inspector.Layout.DimensionWidth.Focused);
            await Ui(() =>
            {
                Require(workspace.Inspector.IsDimensionMode,
                    "Property navigation preserves the active dimension mode and focuses its width field.");
                workspace.Inspector.Layout.DimensionWidth.Text = "240";
                workspace.Inspector.Layout.DimensionHeight.Text = "52";
                workspace.Inspector.Layout.Apply.Invoke();
            });
            await Ready();
            await Ui(() =>
            {
                Require(preview.TryReadNode(version, buttonId, out var resized) &&
                    resized.Bounds.Width == 240 && resized.Bounds.Height == 52,
                    "Structured dimensions change the actual native preview control through the source compiler.");
                editor.Command(TextCommand.Undo);
            });
            await Ready();
            await Ui(() => Require(editor.Text == source && preview.TryReadNode(version, buttonId, out var restored) &&
                restored.Bounds.Width == 180 && restored.Bounds.Height == 40,
                "One source undo restores the original native preview dimensions."));
            await Ui(() =>
            {
                workspace.Hierarchy.Tree.Select(workspace.Hierarchy.Key(workspace.Document!.Root!.Children[1]));
                workspace.Inspector.ChooseArgument("value");
                view.Commands.Invoke();
            });
            ulong? propertySourcePreview = null;
            await Until(() => commandPalette.IsOpen);
            await Ui(() =>
            {
                bool refused = false;
                try { commandPalette.Surface.Invoke((ulong)DesignerCommandId.RevertPropertyDraft); }
                catch (XuiException error) when (error.Message.Contains("disabled", StringComparison.Ordinal)) { refused = true; }
                Require(refused && commandPalette.IsOpen && !workspace.CanRevertPropertyDraft && editor.Text == source,
                    "The native command palette disables draft reversion for an authored expression.");
                Require(preview.TryReadNode(version, buttonId, out var beforeNavigation) && beforeNavigation.ControlId is not null,
                    "Property source navigation starts with an owned native preview control.");
                propertySourcePreview = beforeNavigation.ControlId;
                commandPalette.Surface.Invoke((ulong)DesignerCommandId.RevealPropertySource);
            });
            await Until(() => !commandPalette.IsOpen && editor.Focused);
            await Ui(() =>
            {
                var argument = workspace.Hierarchy.Selection!.Arguments.Single(value => value.Name == "value");
                Require(editor.Selection == new TextSelection((ulong)argument.ValueSpan.Start, (ulong)argument.ValueSpan.End) &&
                    source.Substring(argument.ValueSpan.Start, argument.ValueSpan.Length) == "Caption" &&
                    workspace.Inspector.Argument == "value" && workspace.Inspector.Value.ReadOnly && editor.Text == source &&
                    preview.TryReadNode(version, buttonId, out var retained) && retained.ControlId == propertySourcePreview,
                    "The palette reveals the exact authored expression after dismissal without changing the inspector or preview ownership.");
                workspace.Inspector.ChooseArgument("id");
                view.Commands.Invoke();
            });
            await Until(() => commandPalette.IsOpen);
            await Ui(() =>
            {
                bool refused = false;
                try { commandPalette.Surface.Invoke((ulong)DesignerCommandId.RevealPropertySource); }
                catch (XuiException error) when (error.Message.Contains("disabled", StringComparison.Ordinal)) { refused = true; }
                Require(refused && commandPalette.IsOpen && !workspace.CanRevealPropertySource,
                    "The palette disables source navigation for a property with no authored value.");
                commandPalette.Surface.CloseButton.Invoke();
            });
            await Until(() => !commandPalette.IsOpen);
            await Ui(() =>
            {
                workspace.Hierarchy.Tree.Select(workspace.Hierarchy.Key(workspace.Document!.Root!.Children[1]));
                workspace.Inspector.ChooseArgument("enabled");
                workspace.Inspector.Layout.BooleanMode.Invoke();
                view.Commands.Invoke();
            });
            await Until(() => commandPalette.IsOpen);
            await Ui(() => commandPalette.Surface.Invoke((ulong)DesignerCommandId.FocusProperty));
            await Until(() => !commandPalette.IsOpen && workspace.Inspector.Layout.BooleanValue.Focused);
            await Ui(() =>
            {
                Require(workspace.Inspector.IsBooleanMode && SelectionNative.Enabled("Do not execute"),
                    "Property navigation focuses the active boolean toggle while the authored preview remains enabled.");
                workspace.Inspector.Layout.BooleanValue.Invoke();
                Require(editor.Text == source && SelectionNative.Enabled("Do not execute"),
                    "A boolean draft does not change source or the current native preview.");
                workspace.Inspector.Layout.Apply.Invoke();
            });
            await Ready();
            await Ui(() =>
            {
                Require(!SelectionNative.Enabled("Do not execute") && editor.Text.Contains("enabled: false", StringComparison.Ordinal),
                    "Applying a boolean property disables the actual native preview button through source compilation.");
                editor.Command(TextCommand.Undo);
            });
            await Ready();
            await Ui(() => Require(editor.Text == source && SelectionNative.Enabled("Do not execute"),
                "One native source undo restores the enabled preview button."));
            float labelHeight = 0;
            await Ui(() =>
            {
                var label = workspace.Document!.Root!.Children[0];
                workspace.Hierarchy.Tree.Select(workspace.Hierarchy.Key(label));
                Require(preview.TryReadNode(version, label.Id, out var before), "The authored label has a native preview node.");
                labelHeight = before.Bounds.Height;
                workspace.Inspector.ChooseArgument("padding");
                workspace.Inspector.Layout.InsetsMode.Invoke();
                view.Commands.Invoke();
            });
            await Until(() => commandPalette.IsOpen);
            await Ui(() => commandPalette.Surface.Invoke((ulong)DesignerCommandId.FocusProperty));
            await Until(() => !commandPalette.IsOpen && workspace.Inspector.Layout.InsetLeft.Focused);
            await Ui(() =>
            {
                Require(workspace.Inspector.IsInsetsMode, "Property navigation preserves inset mode and focuses its Left field.");
                workspace.Inspector.Layout.InsetTop.Text = "20";
                workspace.Inspector.Layout.InsetBottom.Text = "24";
                Require(editor.Text == source, "Inset drafts do not change source before Apply.");
                workspace.Inspector.Layout.Apply.Invoke();
            });
            await Ready();
            await Ui(() =>
            {
                var label = workspace.Document!.Root!.Children[0];
                Require(preview.TryReadNode(version, label.Id, out var padded) &&
                    Math.Abs(padded.Bounds.Height - labelHeight - 36) < 0.1f &&
                    editor.Text.Contains("padding: (4, 20, 4, 24)", StringComparison.Ordinal),
                    "Structured padding changes the actual native label height by the two edited edge deltas.");
                editor.Command(TextCommand.Undo);
            });
            await Ready();
            await Ui(() => Require(editor.Text == source &&
                preview.TryReadNode(version, workspace.Document!.Root!.Children[0].Id, out var restoredLabel) &&
                restoredLabel.Bounds.Height == labelHeight, "One source undo restores native label padding and layout."));
            long colorVersion = 0;
            ulong? colorControl = null;
            await Ui(() =>
            {
                var label = workspace.Document!.Root!.Children[0];
                workspace.Hierarchy.Tree.Select(workspace.Hierarchy.Key(label));
                workspace.Inspector.ChooseArgument("foreground");
                Require(colorEditor.CanShow && preview.TryReadNodeStyle(version, label.Id, StylePart.Root, out var style) &&
                    style?.Foreground == new ThemeColor(0x112233), "The selected literal color matches the actual native preview style.");
                Require(preview.TryReadNode(version, label.Id, out var node), "The color edit has a current native preview node.");
                colorControl = node.ControlId;
                colorVersion = version;
                view.Commands.Invoke();
            });
            await Until(() => commandPalette.IsOpen);
            await Ui(() => commandPalette.Surface.Invoke((ulong)DesignerCommandId.ChooseColor));
            await Until(() => !commandPalette.IsOpen && colorEditor.IsPending);
            await Ui(() =>
            {
                Require(colorEditor.Picker.Value == new RgbaColor(0x11, 0x22, 0x33) &&
                    colorEditor.Picker.Channel(0).GetBounds().Width > 0, "The palette opens the native color picker with the authored RGB channels.");
                colorEditor.Picker.Channel(0).IncreaseButton.Invoke();
                colorEditor.View.Primary.Invoke();
            });
            await Until(() => !colorEditor.IsPending && workspace.Inspector.Value.Focused);
            await Ui(() =>
            {
                Require(workspace.Inspector.Value.Text == "0x122233" && editor.Text == source &&
                    version == colorVersion && preview.AppliedVersion == colorVersion &&
                    preview.TryReadNode(version, workspace.Document!.Root!.Children[0].Id, out var node) && node.ControlId == colorControl,
                    "Use color changes only the property draft without replacing preview ownership.");
                workspace.Inspector.Layout.Apply.Invoke();
            });
            await Ready();
            await Ui(() =>
            {
                Require(editor.Text.Contains("foreground: 0x122233", StringComparison.Ordinal) &&
                    preview.TryReadNodeStyle(version, workspace.Document!.Root!.Children[0].Id, StylePart.Root, out var style) &&
                    style?.Foreground == new ThemeColor(0x122233), "Apply compiles the color draft into the actual native preview style.");
                Require(!preview.TryReadNodeStyle(colorVersion, 0, StylePart.Root, out _),
                    "Style snapshots reject retired preview versions without exposing scoped controls.");
                editor.Command(TextCommand.Undo);
            });
            await Ready();
            await Ui(() => Require(editor.Text == source &&
                preview.TryReadNodeStyle(version, workspace.Document!.Root!.Children[0].Id, StylePart.Root, out var style) &&
                style?.Foreground == new ThemeColor(0x112233), "One native undo restores both authored source and native preview color."));
            await Ui(() =>
            {
                var button = workspace.Document!.Root!.Children[1];
                editor.Selection = new((ulong)button.Span.Start, (ulong)button.Span.End);
                view.Commands.Invoke();
            });
            await Until(() => commandPalette.IsOpen);
            await Ui(() =>
            {
                Require(!window.KeyHandler!(new(0xBF, KeyModifiers.Control, commandPalette.Surface.Editor.Id)),
                    "The source comment shortcut does not intercept command palette input.");
                commandPalette.Surface.Invoke((ulong)DesignerCommandId.ToggleComment);
            });
            await Ready();
            await Ui(() =>
            {
                Require(!commandPalette.IsOpen && editor.Focused && editor.Text.Contains("// Button(Caption", StringComparison.Ordinal) &&
                    workspace.Document!.Root!.Children.Count == 2 && SelectionNative.PeerCount("Do not execute") == 0,
                    "The palette comment action changes source, hierarchy, and the actual native preview through the normal compiler pipeline.");
                Require(window.KeyHandler!(new(0xBF, KeyModifiers.Control, editor.Id)),
                    "Ctrl+/ toggles comments in the focused source editor.");
            });
            await Ready();
            await Ui(() =>
            {
                Require(editor.Text == source && workspace.Document!.Root!.Children.Count == 3 &&
                    SelectionNative.PeerCount("Do not execute") == 1,
                    "The source shortcut restores the selected control and its live native preview.");
                editor.Command(TextCommand.Undo);
            });
            await Ready();
            await Ui(() =>
            {
                Require(editor.Text.Contains("// Button(Caption", StringComparison.Ordinal) && SelectionNative.PeerCount("Do not execute") == 0,
                    "One source undo restores the previous commented document and preview.");
                editor.Command(TextCommand.Undo);
            });
            await Ready();
            await Ui(() => Require(editor.Text == source && SelectionNative.PeerCount("Do not execute") == 1,
                "The preceding palette comment has its own native undo operation."));
            await Ui(() =>
            {
                var text = workspace.Document!.Root!.Children[0];
                editor.Selection = new((ulong)text.Span.Start, (ulong)text.Span.End);
                view.Commands.Invoke();
            });
            await Until(() => commandPalette.IsOpen);
            await Ui(() =>
            {
                Require(!window.KeyHandler!(new(0x28, KeyModifiers.Shift | KeyModifiers.Alt, commandPalette.Surface.Editor.Id)),
                    "The source duplication shortcut does not intercept command palette input.");
                commandPalette.Surface.Invoke((ulong)DesignerCommandId.DuplicateSourceLines);
            });
            await Ready();
            string duplicatedSource = "";
            await Ui(() =>
            {
                var children = workspace.Document!.Root!.Children;
                Require(!commandPalette.IsOpen && editor.Focused && children.Count == 4 &&
                    children[0].Kind == "Text" && children[1].Kind == "Text" &&
                    NativeLabel(children[0].Id) && NativeLabel(children[1].Id),
                    "Palette line duplication compiles a second native label through the ordinary source pipeline.");
                Require(editor.Selection == new TextSelection((ulong)children[1].Span.Start, (ulong)children[1].Span.End),
                    "The selected source characters move into the duplicated control.");
                duplicatedSource = editor.Text;
                workspace.Hierarchy.Layout.Query.Focus();
                Require(!window.KeyHandler!(new(0x28, KeyModifiers.Shift | KeyModifiers.Alt, workspace.Hierarchy.Layout.Query.Id)) &&
                    editor.Text == duplicatedSource, "Shift+Alt+Down does not duplicate source from another input.");
                editor.Focus();
                Require(window.KeyHandler!(new(0x28, KeyModifiers.Shift | KeyModifiers.Alt, editor.Id)),
                    "Shift+Alt+Down duplicates the selected lines from the source editor.");
            });
            await Ready();
            await Ui(() =>
            {
                var children = workspace.Document!.Root!.Children;
                Require(children.Count == 5 && NativeLabel(children[2].Id),
                    "The source shortcut compiles the repeated duplicate into a third native label.");
                editor.Command(TextCommand.Undo);
            });
            await Ready();
            await Ui(() =>
            {
                Require(editor.Text == duplicatedSource && workspace.Document!.Root!.Children.Count == 4,
                    "One native undo removes only the shortcut duplication.");
                editor.Command(TextCommand.Undo);
            });
            await Ready();
            await Ui(() => Require(editor.Text == source && workspace.Document!.Root!.Children.Count == 3 &&
                NativeLabel(workspace.Document.Root.Children[0].Id),
                "A second native undo restores the exact source and preview before palette duplication."));
            foreach (bool usePalette in new[] { true, false })
            {
                await Ui(() =>
                {
                    var text = workspace.Document!.Root!.Children[0];
                    editor.Selection = new((ulong)text.Span.Start, (ulong)text.Span.End);
                    editor.Focus();
                });
                string movedSource = "";
                foreach (bool down in new[] { true, false })
                {
                    if (usePalette)
                    {
                        await Ui(view.Commands.Invoke);
                        await Until(() => commandPalette.IsOpen);
                        await Ui(() =>
                        {
                            Require(!window.KeyHandler!(new(down ? 0x28u : 0x26u, KeyModifiers.Alt, commandPalette.Surface.Editor.Id)),
                                "Source movement shortcuts do not intercept native command palette navigation.");
                            commandPalette.Surface.Invoke((ulong)(down ? DesignerCommandId.MoveSourceLinesDown : DesignerCommandId.MoveSourceLinesUp));
                        });
                    }
                    else
                    {
                        await Ui(() => Require(window.KeyHandler!(new(down ? 0x28u : 0x26u, KeyModifiers.Alt, editor.Id)),
                            "Alt+Up/Down moves lines in the focused source editor."));
                    }
                    await Ready();
                    await Ui(() =>
                    {
                        var children = workspace.Document!.Root!.Children;
                        var text = children[down ? 1 : 0];
                        var button = children[down ? 0 : 1];
                        Require(children.Count == 3 && text.Kind == "Text" && button.Kind == "Button" &&
                            NativeLabel(text.Id) && preview.TryReadNode(version, text.Id, out var textView) &&
                            preview.TryReadNode(version, button.Id, out var buttonView) &&
                            (down ? textView.Bounds.Y > buttonView.Bounds.Y : textView.Bounds.Y < buttonView.Bounds.Y),
                            "Source line movement changes the actual native preview order without losing controls.");
                        Require(editor.Focused && editor.Selection == new TextSelection((ulong)text.Span.Start, (ulong)text.Span.End),
                            "The moved source control keeps its selected characters and native focus.");
                        if (down) movedSource = editor.Text;
                        else Require(editor.Text == source, "The inverse move restores exact source text.");
                    });
                }
                await Ui(() => editor.Command(TextCommand.Undo));
                await Ready();
                await Ui(() =>
                {
                    Require(editor.Text == movedSource && workspace.Document!.Root!.Children[0].Kind == "Button",
                        "One native undo restores the previous source and preview order.");
                    editor.Command(TextCommand.Undo);
                });
                await Ready();
                await Ui(() => Require(editor.Text == source && NativeLabel(workspace.Document!.Root!.Children[0].Id),
                    "A second native undo restores the source and preview before line movement."));
            }
            foreach (bool usePalette in new[] { true, false })
            {
                await Ui(() =>
                {
                    var button = workspace.Document!.Root!.Children[1];
                    editor.Selection = new((ulong)button.Span.Start, (ulong)button.Span.End);
                    workspace.Hierarchy.Layout.Query.Focus();
                    Require(!window.KeyHandler!(new('K', KeyModifiers.Control | KeyModifiers.Shift, workspace.Hierarchy.Layout.Query.Id)),
                        "Ctrl+Shift+K leaves other native inputs unchanged.");
                    editor.Focus();
                });
                if (usePalette)
                {
                    await Ui(view.Commands.Invoke);
                    await Until(() => commandPalette.IsOpen);
                    await Ui(() =>
                    {
                        Require(!window.KeyHandler!(new('K', KeyModifiers.Control | KeyModifiers.Shift, commandPalette.Surface.Editor.Id)),
                            "Line deletion does not intercept command palette input.");
                        commandPalette.Surface.Invoke((ulong)DesignerCommandId.DeleteSourceLines);
                    });
                }
                else
                {
                    await Ui(() => Require(window.KeyHandler!(new('K', KeyModifiers.Control | KeyModifiers.Shift, editor.Id)),
                        "Ctrl+Shift+K deletes selected lines from the source editor."));
                }
                await Ready();
                await Ui(() =>
                {
                    var children = workspace.Document!.Root!.Children;
                    Require(!commandPalette.IsOpen && editor.Focused && children.Count == 2 &&
                        children[0].Kind == "Text" && children[1].Kind == "TextInput" && NativeLabel(children[0].Id) &&
                        SelectionNative.PeerCount("Do not execute") == 0,
                        "Source line deletion removes the selected control from the hierarchy and actual native preview.");
                    Require(editor.Selection.Start == editor.Selection.End &&
                        editor.Selection.Start < (ulong)children[1].Span.Start,
                        "The deletion caret rests at the start of the following source line before its indentation.");
                    editor.Command(TextCommand.Undo);
                });
                await Ready();
                await Ui(() => Require(editor.Text == source && workspace.Document!.Root!.Children.Count == 3 &&
                    SelectionNative.PeerCount("Do not execute") == 1,
                    "One source undo restores the exact deleted line and its native preview control."));
            }
            await Ui(() =>
            {
                workspace.Hierarchy.Tree.Select(workspace.Hierarchy.Key(workspace.Document!.Root!.Children[1]));
                workspace.Inspector.Layout.PaletteFilter.Text = "button";
                workspace.Inspector.FilterPalette();
                workspace.Inspector.Layout.InsertBefore.Focus();
            });
            await Ui(() =>
            {
                var before = workspace.Inspector.Layout.InsertBefore.GetBounds();
                var after = workspace.Inspector.Layout.InsertAfter.GetBounds();
                var panel = view.InspectorPanel.GetBounds();
                Require(before.Width >= 80 && after.Width >= 80 && before.Y >= panel.Y &&
                    after.Y + after.Height <= panel.Y + panel.Height && after.X + after.Width <= panel.X + panel.Width,
                    "Both sibling insertion actions are visible and usable inside the narrow scrollable inspector.");
                workspace.Inspector.Layout.InsertBefore.Invoke();
            });
            await Ready();
            await Ui(() =>
            {
                var children = workspace.Document!.Root!.Children;
                var inserted = children[1];
                Require(children.Count == 4 && ReferenceEquals(workspace.Hierarchy.Selection, inserted) &&
                    preview.TryReadNode(version, inserted.Id, out var added) && added.ControlId is { } addedControl &&
                    SelectionNative.ReadText(addedControl) == "Button" &&
                    preview.TryReadNode(version, children[2].Id, out var anchor) && added.Bounds.Y < anchor.Bounds.Y,
                    "Insert before creates and selects a real native preview button ahead of the original control.");
                editor.Command(TextCommand.Undo);
            });
            await Ready();
            await Ui(() => Require(editor.Text == source && workspace.Document!.Root!.Children.Count == 3 &&
                SelectionNative.PeerCount("Do not execute") == 1, "One undo restores the exact source and preview before sibling insertion."));
            await Ui(() =>
            {
                var root = workspace.Document!.Root!;
                editor.Selection = new((ulong)root.Span.Start, (ulong)root.Span.Start);
                workspace.SelectFromCaret();
                workspace.Inspector.Layout.PaletteFilter.Text = "Grid";
                workspace.Inspector.FilterPalette();
                Require(workspace.Inspector.Template == ControlTemplate.Grid, "The Grid palette template is available.");
                workspace.Inspector.Layout.Insert.Invoke();
            });
            await Ready();
            await Ui(() =>
            {
                var grid = workspace.Document!.Root!.Children[3];
                Require(grid.Kind == "Grid" && ReferenceEquals(workspace.Hierarchy.Selection, grid),
                    "The new empty Grid becomes the current selection.");
                string before = editor.Text;
                workspace.Inspector.Layout.FindCell.Invoke();
                Require(workspace.Inspector.Layout.Row.Text == "0" && workspace.Inspector.Layout.Column.Text == "0" &&
                    editor.Text == before && preview.AppliedVersion == version,
                    "Finding an empty cell preserves the live preview and source.");
                workspace.Inspector.Layout.PaletteFilter.Text = "button";
                workspace.Inspector.FilterPalette();
                workspace.Inspector.Layout.Insert.Invoke();
            });
            await Ready();
            await Ui(() =>
            {
                var child = workspace.Document!.Root!.Children[3].Children.Single();
                Require(preview.TryReadNode(version, child.Id, out var added) && added.ControlId is { } control &&
                    SelectionNative.ReadText(control) == "Button" && added.Bounds.Width > 0 && added.Bounds.Height > 0,
                    "A discovered cell hosts the inserted native Button in the real preview.");
                editor.Command(TextCommand.Undo);
            });
            await Ready();
            await Ui(() =>
            {
                Require(workspace.Document!.Root!.Children[3].Children.Count == 0,
                    "One undo removes the cell insertion without removing its Grid.");
                editor.Command(TextCommand.Undo);
            });
            await Ready();
            await Ui(() => Require(editor.Text == source && workspace.Document!.Root!.Children.Count == 3,
                "The next undo removes the Grid with no extra cell-discovery undo entry."));
            await Ui(() =>
            {
                workspace.Hierarchy.Tree.Select(workspace.Hierarchy.Key(workspace.Document!.Root!));
                view.Commands.Invoke();
            });
            await Until(() => commandPalette.IsOpen);
            await Ui(() =>
            {
                foreach (var id in new[] { DesignerCommandId.DeleteControl, DesignerCommandId.DuplicateControl,
                    DesignerCommandId.MoveControlUp, DesignerCommandId.MoveControlDown, DesignerCommandId.UnwrapControl })
                {
                    bool refused = false;
                    try { commandPalette.Surface.Invoke((ulong)id); }
                    catch (XuiException error) when (error.Message.Contains("disabled", StringComparison.Ordinal)) { refused = true; }
                    Require(refused && commandPalette.IsOpen && editor.Text == source,
                        $"The native palette disables structurally unavailable action {id} for the root.");
                }
                commandPalette.Surface.CloseButton.Invoke();
            });
            await Until(() => !commandPalette.IsOpen);
            foreach (var (id, index) in new[]
            {
                (DesignerCommandId.DuplicateControl, 0), (DesignerCommandId.DeleteControl, 0),
                (DesignerCommandId.MoveControlDown, 0), (DesignerCommandId.MoveControlUp, 1),
                (DesignerCommandId.WrapVertical, 0), (DesignerCommandId.WrapHorizontal, 0), (DesignerCommandId.WrapScroll, 0)
            })
            {
                await Ui(() => workspace.Hierarchy.Tree.Select(workspace.Hierarchy.Key(workspace.Document!.Root!.Children[index])));
                await StructureCommand(id);
                string wrappedSource = "";
                await Ui(() =>
                {
                    var children = workspace.Document!.Root!.Children;
                    switch (id)
                    {
                        case DesignerCommandId.DuplicateControl:
                            Require(children.Count == 4 && children[0].Kind == "Text" && children[1].Kind == "Text" &&
                                ReferenceEquals(workspace.Hierarchy.Selection, children[1]) && NativeLabel(children[1].Id),
                                "Palette duplication selects a new authored node and creates its native preview label.");
                            break;
                        case DesignerCommandId.DeleteControl:
                            Require(children.Count == 2 && children[0].Kind == "Button" &&
                                preview.TryReadNode(version, children[0].Id, out var first) && first.ElementType == "Button",
                                "Palette deletion removes only the selected control and rebuilds the native preview.");
                            break;
                        case DesignerCommandId.MoveControlDown:
                        case DesignerCommandId.MoveControlUp:
                            Require(children[0].Kind == "Button" && children[1].Kind == "Text" &&
                                preview.TryReadNode(version, children[0].Id, out var button) &&
                                preview.TryReadNode(version, children[1].Id, out var label) && button.Bounds.Y < label.Bounds.Y,
                                "Palette movement changes actual native preview order.");
                            break;
                        default:
                            string kind = id == DesignerCommandId.WrapVertical ? "VStack" :
                                id == DesignerCommandId.WrapHorizontal ? "HStack" : "ScrollView";
                            Require(children[0].Kind == kind && children[0].Children.Count == 1 &&
                                ReferenceEquals(workspace.Hierarchy.Selection, children[0]) && NativeLabel(children[0].Children[0].Id),
                                $"Palette wrapping creates {kind} and retains its live native child.");
                            wrappedSource = editor.Text;
                            break;
                    }
                });
                if (id == DesignerCommandId.WrapVertical)
                {
                    await StructureCommand(DesignerCommandId.UnwrapControl);
                    await Ui(() =>
                    {
                        Require(workspace.Document!.Root!.Children[0].Kind == "Text" &&
                            NativeLabel(workspace.Document.Root.Children[0].Id),
                            "Palette unwrapping preserves the authored child and its native preview.");
                        editor.Command(TextCommand.Undo);
                    });
                    await Ready();
                    await Ui(() => Require(editor.Text == wrappedSource, "Unwrapping has a separate native source undo operation."));
                }
                await Ui(() => editor.Command(TextCommand.Undo));
                await Ready();
                await Ui(() => Require(editor.Text == source && workspace.Document!.Root!.Children.Count == 3,
                    $"One source undo restores the exact document after {id}."));
            }
            await Ui(() =>
            {
                Require(!pickControls, "Disabling Pick controls restores actual authored pointer behavior.");
                view.Live.Invoke();
                view.Pick.Invoke();
            });
            await Until(() => view.PickStatus.StartsWith("Render the current source", StringComparison.Ordinal));
            await Ui(() =>
            {
                Require(!pickControls && preview.AppliedVersion != version,
                    "A non-current preview reports a refusal without partially enabling picking.");
                Require(view.OutlineStatus.StartsWith("Outline cleared.", StringComparison.Ordinal),
                    "A paused source revision does not retain current-outline feedback.");
                Require(editor.Text.Contains("SelectionFixture", StringComparison.Ordinal), "A picking refusal preserves source.");
            });
            Console.WriteLine($"Designer source/preview selection assertions: {assertions} passed.");

            string ButtonText()
            {
                if (!preview.TryReadNode(version, buttonId, out var node) || node.ControlId is not { } control)
                    throw new InvalidOperationException("The current preview button has no native control identity.");
                return SelectionNative.ReadText(control);
            }

            bool NativeLabel(int id) => preview.TryReadNode(version, id, out var node) && node.ControlId is { } control &&
                SelectionNative.ReadText(control) == "Find target" && node.Bounds.Width > 0 && node.Bounds.Height > 0;

            async Task StructureCommand(DesignerCommandId id)
            {
                await Ui(view.Commands.Invoke);
                await Until(() => commandPalette.IsOpen);
                await Ui(() =>
                {
                    commandPalette.Surface.Invoke((ulong)id);
                    Require(!workspace.IsBusy, "Structural commands do not edit source inside the native palette callback.");
                });
                await Until(() => !commandPalette.IsOpen);
                await Ready();
            }
        }
        catch (OperationCanceledException error) when (timeout.IsCancellationRequested)
        {
            Console.Error.WriteLine("Selection smoke deadline: " + lastState);
            throw new TimeoutException("Selection smoke deadline: " + lastState, error);
        }
        catch (Exception error)
        {
            Console.Error.WriteLine(error);
            throw;
        }
        finally { window.Post(window.Close); }

        void Require(bool condition, string message)
        {
            if (!condition) throw new InvalidOperationException("Designer selection smoke: " + message);
            assertions++;
        }

        async Task Ui(Action action)
        {
            var done = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
            if (!window.Post(() =>
            {
                try { action(); done.SetResult(); }
                catch (Exception error) { done.SetException(error); }
            })) throw new InvalidOperationException("The selection smoke window rejected an action.");
            await done.Task.WaitAsync(TimeSpan.FromSeconds(30), timeout.Token);
        }

        Task Ready() => Until(() =>
        {
            if (view.Status.Text is "Error. See diagnostics." or "Source has errors. The last valid preview is unchanged.")
                throw new InvalidOperationException("Selection fixture did not render: " + diagnostics.Text);
            return workspace.IsCurrent && !workspace.IsBusy && preview.AppliedVersion == version;
        });

        async Task Until(Func<bool> condition)
        {
            bool ready = false;
            while (!ready)
            {
                await Ui(() =>
                {
                    ready = condition();
                    string state = $"version={version}, applied={preview.AppliedVersion}, hierarchyCurrent={workspace.IsCurrent}, busy={workspace.IsBusy}, picking={pickControls}, pickStatus={view.PickStatus}, " +
                        $"status={view.Status.Text}, diagnostics={diagnostics.Text}";
                    lastState = state;
                });
                if (!ready) await Task.Delay(25, timeout.Token);
            }
        }
    }

    private static class SelectionNative
    {
        internal static int PeerCount(string text) => Peers(text).Count;

        internal static bool Enabled(string text)
        {
            var peers = Peers(text);
            if (peers.Count != 1) throw new InvalidOperationException($"Expected one preview peer named '{text}', found {peers.Count}.");
            return IsWindowEnabled(peers.Single());
        }

        internal static string ReadText(ulong control)
        {
            int status = TextCopy(control, null, 0, out uint length);
            if (status is not (0 or 6)) throw new InvalidOperationException($"Native preview text length failed: {status}.");
            var bytes = new byte[length];
            status = TextCopy(control, bytes, length, out _);
            if (status != 0) throw new InvalidOperationException($"Native preview text read failed: {status}.");
            return Encoding.UTF8.GetString(bytes);
        }

        internal static void Click(string text)
        {
            var peers = Peers(text);
            if (peers.Count != 1) throw new InvalidOperationException($"Expected one preview peer named '{text}' on the owning UI thread, found {peers.Count}.");
            nint peer = peers.Single();
            if (!GetClientRect(peer, out var bounds) || bounds.Right <= 0 || bounds.Bottom <= 0)
                throw new InvalidOperationException("The preview peer has no usable client bounds.");
            nint point = ((bounds.Bottom / 2) << 16) | ((bounds.Right / 2) & 0xffff);
            SendMessageW(peer, 0x201, 1, point);
            SendMessageW(peer, 0x202, 0, point);
        }

        internal static void Key(uint key)
        {
            if (!PostMessageW(GetFocus(), 0x0100, key, 0))
                throw new InvalidOperationException("The preview smoke could not post a native key.");
        }

        private static HashSet<nint> Peers(string text)
        {
            var peers = new HashSet<nint>();
            EnumThreadWindows(GetCurrentThreadId(), (root, _) =>
            {
                EnumChildWindows(root, (child, _) =>
                {
                    var caption = new StringBuilder(128);
                    GetWindowTextW(child, caption, caption.Capacity);
                    if (caption.ToString() == text) peers.Add(child);
                    return true;
                }, 0);
                return true;
            }, 0);
            return peers;
        }

        private delegate bool EnumWindow(nint window, nint context);
        [StructLayout(LayoutKind.Sequential)] private struct Rect { public int Left, Top, Right, Bottom; }
        [DllImport("kernel32.dll")] private static extern uint GetCurrentThreadId();
        [DllImport("user32.dll")] private static extern bool EnumThreadWindows(uint thread, EnumWindow callback, nint context);
        [DllImport("user32.dll")] private static extern bool EnumChildWindows(nint parent, EnumWindow callback, nint context);
        [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern int GetWindowTextW(nint window, StringBuilder text, int count);
        [DllImport("user32.dll")] private static extern bool GetClientRect(nint window, out Rect bounds);
        [DllImport("user32.dll")] private static extern bool IsWindowEnabled(nint window);
        [DllImport("user32.dll")] private static extern nint SendMessageW(nint window, uint message, nint first, nint second);
        [DllImport("user32.dll")] private static extern nint GetFocus();
        [DllImport("user32.dll")] private static extern bool PostMessageW(nint window, uint message, nuint first, nint second);
        [DllImport("xui", EntryPoint = "xui_text_copy")] private static extern int TextCopy(ulong control, [Out] byte[]? bytes, uint capacity, out uint count);
    }
}
