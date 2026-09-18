# XUI Designer

The designer is a XUI application for editing one `.xui` component.
Its [layout](../../bindings/dotnet/Designer/DesignerLayout.xui) uses `.xui`.
Its native `MultilineText` editor uses Consolas and keeps Windows selection, clipboard, undo, and IME behavior.
An [LSH-enabled build](../../CONTRIBUTING.md#lsh-highlighting-in-xui-applications) highlights XUI and embedded C# as the source changes.
Highlighting remains active when live preview is paused.
The preview uses the existing XUI compiler and native controls, not an HTML approximation.

Build and run commands are in [CONTRIBUTING](../../CONTRIBUTING.md#xui-designer).
Release downloads include separate [Designer archives](packages.md#designer-archives) for Windows x64 and ARM64.
These archives include the .NET runtime and compiler, so no separate .NET installation is necessary.
The [language guide](xui-language.md) describes the source syntax.

## Example documents

The designer includes a catalog of self-contained examples:

- **Blank canvas:** A stack ready for controls.
- **Interactive counter:** State, event handlers, and button styles.
- **Contact form:** Native text fields, a toggle, and an in-memory submit action.
- **Settings panel:** Two-way state updates and theme-aware styles.
- **Dashboard:** Grid columns, metric cards, and button actions.
- **Split workspace:** A resizable split view with navigation and details.
- **Value controls:** A native range input updates component state, a progress meter, and a numeric label.

The [template sources](../../bindings/dotnet/Designer/Templates) use the same language as ordinary applications.
The [counter source](../../bindings/dotnet/Designer/Starter.xui) supplies the initial example.
The examples do not access the network or save application data.
The template selector and **New** create a document from the selected example.
New requires a saved or unchanged current document.
The new document has no file path and needs a save destination.

## Visual workspace

The workspace contains a native source editor, control hierarchy, property inspector, control palette, and preview.
The divider between source and preview changes their widths.
The inspector scrolls independently.
The source editor retains native selection, clipboard, undo, and IME behavior.

Enter copies the current line's leading spaces and tabs onto the new line.
Within the indentation, Enter copies only the whitespace before the caret.
With no selection, Tab within the leading whitespace adds four spaces.
Shift+Tab removes up to four leading spaces or one leading tab, without moving focus.
Tab after source text or with a selection keeps the existing focus-navigation behavior.
Each indentation edit creates one native undo action.

**Source: Toggle line comments** in the command palette comments or uncomments the selected source lines.
On US keyboards, Ctrl+/ runs this action only when the source editor has focus.
With no selection, the action uses the caret's line.
A selection that ends at the next line's start excludes that next line.

The action preserves indentation and blank lines.
If every nonblank line starts with `//`, it removes those markers and at most one following space.
Otherwise, it adds `// ` after each nonblank line's indentation, including lines that already have a comment.
The source selection follows the original characters through the prefix changes.
Each action creates one native undo operation.
An all-blank selection reports a no-op. Read-only source and excessive result length produce explicit errors without partial changes.

Line comments are text edits, not syntax-aware refactoring.
The action can introduce syntax errors inside strings or incomplete control declarations.
The ordinary source pipeline updates the hierarchy and preview, and invalid source retains the last valid preview.

**Source: Duplicate selected lines** copies the selected source lines immediately after the original lines.
Shift+Alt+Down runs this action only when the source editor has focus.
With no selection, the action copies the caret's line.
A selection that ends at the next line's start excludes that next line.
The action preserves indentation, blank lines, and native line endings.
The caret or selected characters move into the copy.

Each duplication creates one native undo operation.
A final line without a line ending receives a separator before its copy.
An empty document or a final empty line receives one new line.
Read-only source and excessive result length produce explicit errors without partial changes.
This action copies text, not control identities. Duplicate IDs or declarations can cause compiler errors.
The ordinary source pipeline retains the last valid preview when the copied text is invalid.

**Source: Move selected lines up** and **Source: Move selected lines down** exchange the selected block with the adjacent line.
Alt+Up and Alt+Down run these actions only when the source editor has focus.
The same shortcuts in the hierarchy retain their control-movement behavior.
With no selection, the action moves the caret's line.
A selection that ends at the next line's start excludes that next line.
Indentation, blank lines, and line content remain unchanged.

The caret or selection follows the moved block.
If the block moves to the document's end, the selection excludes any line separator that no longer follows it.
Each text change creates one native undo operation.
Identical adjacent lines move the selection without changing source or adding an undo operation.
At a document boundary, the palette action is disabled and the source shortcut reports a no-op.
Read-only source refuses the action.

Line movement is a text edit, not syntax-aware control movement.
The action can move declarations outside their container or introduce compiler errors.
Invalid source retains the last valid preview.

**Source: Delete selected lines** deletes each line touched by the source selection without changing the clipboard.
Ctrl+Shift+K runs this action only when the source editor has focus.
With no selection, the action deletes the caret's line.
A selection that ends at the next line's start excludes that next line.
The caret moves to the start of the following line.
If no line follows the deleted block, the action removes the preceding separator and places the caret at the previous line's end.

Each deletion creates one native undo operation.
Read-only or empty source disables the palette action. Direct shortcuts report an explicit refusal or no-op.
This action deletes text, not a parsed control. It can remove required syntax and cause compiler errors.
Invalid source retains the last valid preview.

The file and preview controls occupy a separate, shaded toolbar above the workspace.
The toolbar uses theme-aware colors and keeps the existing commands and shortcuts.
New, open, save, recovery, undo, redo, and render use compact icon buttons.
Each icon keeps a descriptive accessible name and a tooltip.
**Style: WinUI** / **Style: Classic** switches the Designer and preview between visual styles.
The switch preserves control values, source undo, and preview state without recompilation.
The visual style is independent of the **Light theme** setting.
Routine hierarchy and property hints use tooltips instead of persistent labels.
Read-only reasons and errors remain visible.

**Commands**, or Ctrl+Shift+P, opens a searchable native command palette.
The palette includes file actions, source editing, control structure, focus navigation, preview sizes, preview modes, Output, theme, and visual style.
Up and Down select a result. Enter runs the selected command after the palette closes.
Escape and the close button dismiss the palette and restore the previous focus.
The query remains available when the palette opens again.
Source and file shortcuts do not intercept keys inside the palette.

Opening Commands turns off **Pick controls** because native popups cannot open during pointer picking.
The **Preview: Pick controls** command enables that mode again after the palette closes.
Unavailable commands appear disabled. Each action checks its availability again before execution.
File commands use the existing discard confirmation, file chooser, and disk-conflict checks.
Command discovery does not replace source, clear undo, or rebuild the preview.

**Go to line**, above the source editor, opens a native dialog for an exact source location.
Ctrl+G opens the same dialog when the source editor has focus.
The command palette includes **Source: Go to line and column**.
Ctrl+G in the hierarchy retains its existing grouping action.
Opening the dialog turns off **Pick controls**.

The dialog starts with the current caret's line and column.
Both fields accept positive whole numbers, without spaces or signs.
Line and column numbers start at 1. Columns count UTF-16 code units, as compiler diagnostics do.
The position after a line's last character is valid, including the last empty line.
The dialog rejects missing lines, excessive columns, and positions inside a surrogate pair.

Enter confirms a valid location. Escape or Cancel preserves the previous source selection.
After confirmation, the dialog closes before the source caret moves and receives focus.
Navigation updates the hierarchy when the current source model is available.
It also works with syntax-invalid source and does not change text, native undo, or the live preview.
A changed source revision cancels navigation, even if the source text later returns to its earlier value.

Find starts collapsed.
Ctrl+F opens a compact Find panel above the source editor and focuses its native field.
F3 selects the next literal match, and Shift+F3 selects the previous match.
These shortcuts also open the panel if it is closed.
The **Aa** toggle selects case-sensitive matching.
The **Word** toggle excludes matches inside longer words or identifiers.
Word boundaries include Unicode letters, digits, combining marks, connector punctuation, and format characters.
Search never selects half of a UTF-16 surrogate pair.
Search reads the current source each time and does not change text or undo history.
Enter in the Find field selects a match.
Escape or the close button collapses the panel and returns focus to source.
Escape also closes the panel after a search returns focus to source.
The panel retains its query, replacement text, and matching choices between uses.

**Source: Find selected text** in the command palette copies the current source selection into Find and focuses the query.
It does not move the source selection.
**Source: Find next occurrence of selected text** and **Source: Find previous occurrence of selected text** also navigate immediately.
Ctrl+F3 and Ctrl+Shift+F3 provide these navigation actions only when the source editor has focus.

These actions use the complete current selection and retain the case, whole-word, and replacement settings.
Navigation skips the selected occurrence and wraps through the current matches.
The selection must contain text from one line, with at most 1024 UTF-16 code units.
Empty, multiline, oversized, or incomplete Unicode selections are unavailable in the palette.
A direct shortcut refusal reports the reason and keeps the previous query and source selection.
Ordinary Ctrl+F still opens the retained query without copying source text.

Ctrl+H opens the replacement row and focuses **Replace with**.
The chevron beside the navigation buttons also shows or hides this row.
**Replace** changes the selected match, then selects the next match.
If the selection is not a current match, Replace selects a match without changing source.
Enter in **Replace with** has the same behavior.
**Replace all** changes all matches from one source snapshot as one native undo action.
Replacement text is literal. Dollar signs and backslashes have no special replacement syntax.
An empty replacement deletes matched text.

Replacement uses the same case and word choices as Find.
Identical replacements do not create source edits or undo entries.
Read-only source and excessive replacement length produce explicit errors without partial changes.
Replacement follows the ordinary source-edit pipeline, including recovery drafts, hierarchy updates, and live compilation.
Like source typing, replacement can introduce syntax errors. The last valid preview remains available.

The hierarchy, property inspector, and Output have theme-aware backgrounds and borders.
Hierarchy rows use a compact 28-DIP height with reduced padding and indentation.

Output starts collapsed to a status row at the bottom of the window.
The borderless up-chevron beside **Output** expands the pane; the down-chevron collapses it.
When expanded, the button and status move to the top of the pane, above its contents.
The pane contains file feedback and compiler diagnostics.
Compile, preview, and file errors expand Output automatically.
The source editor keeps its document, selection, and undo history as either panel opens or closes.

The hierarchy uses a native TreeView with expandable controls.
Each row shows the control kind and its optional positional value, without source offsets.
Selecting a control selects its source range and scrolls the source editor to that range.
**Select from caret**, or Ctrl+Shift+L, selects the control that contains the source caret.
Hierarchy identities belong to one exact source revision.
A new source revision resets tree expansion and selects the control at the current caret.
It does not reuse identities from an older document.

The command palette includes **Selection:** actions for the parent, first child, previous sibling, next sibling, and root.
These actions use the current hierarchy selection, not the source caret.
They expand the required ancestors, select the exact source range, and focus the hierarchy.
The inspector shows the target control, and the preview outline follows selection.

Sibling navigation stays within the immediate parent and does not wrap at either boundary.
Unavailable directions are disabled, including parent and root navigation when the root is already selected.
Stale source and pending visual edits disable these actions.
Navigation does not change source, native undo, or preview state.
Native text fields keep their existing keyboard shortcuts.

**Hierarchy: Expand selected subtree** opens the selected container and its descendants through the command palette.
It processes small batches so source input remains available.
**Hierarchy: Cancel expansion** stops pending batches and leaves completed branches open.
Source edits, selection actions, visual edits, and disposal also cancel pending expansion.

**Hierarchy: Collapse selected branch** hides the selected container's descendants and cancels pending expansion.
It retains nested expansion choices, so reopening the branch restores its previous nested view.
Expansion and collapse keep the current source selection, property draft, search query, and preview state.
Leaf nodes, stale source, and pending visual edits disable these commands.
An active expansion can reopen manually collapsed branches until it finishes or is canceled.

**Find in hierarchy** searches control types and full authored argument names and values.
The search is case-insensitive. Every whitespace-separated term must match the same control.
For example, `button Save` finds buttons with `Save` in an argument.
Queries can also include an `id`, a `ref`, or text beyond a row's shortened label.
The search reads source text and does not evaluate expressions.

Enter in the query selects the next match. Shift+Enter selects the previous match.
The arrow buttons provide the same actions, with wraparound in source order.
Match navigation expands the required ancestors and selects the control's exact source range.
The tree keeps its complete structure and revision-scoped identities.
Typing a query does not move selection. Escape or the clear button clears only the hierarchy query.

**Hierarchy: Find controls** in the command palette focuses the query.
Queries remain available across source changes.
During parsing or invalid source, search navigation is disabled and its status explains the restriction.
A valid source revision rematches the retained query against the new controls.
Search does not change source, native undo, or preview state.

**Pick controls** selects an authored control through a primary-pointer click in the current preview.
The click selects its full source range, hierarchy node, and inspector.
The mode consumes the pointer action instead of running its authored handler.
Keyboard and accessibility actions remain live.
An outdated preview cannot select controls in newer source.
Unsupported preview surfaces produce an explicit error and keep the previous mode.

Selection from the hierarchy, Find, Go to line, diagnostics, or preview also requests a preview outline.
The outline does not cover native controls or change their input behavior.
The **Live preview** heading tooltip explains hidden, clipped, overlapping, and unsupported outlines.
The **Pick controls** tooltip describes the current pointer mode.
Failures appear in the Output status row.
The hierarchy and inspector remain available when an outline cannot appear.
A source revision clears the outline until the current preview is ready.
Later layout changes can hide an outline that was initially visible.

### Preview sizes

The preview size selector provides **Fit**, **Compact**, **Medium**, **Wide**, and **Custom**.
Fit fills the available preview pane.
Compact requests a 360-by-640-DIP viewport.
Medium requests 768 by 1024 DIP. Wide requests 1280 by 800 DIP.

The width and height fields specify custom dimensions in device-independent pixels.
Each custom dimension accepts whole numbers from 1 through 4096, without spaces, signs, or fractions.
**Apply** changes the viewport without compiling source or resetting authored control state.
The dimension label shows the actual arranged size.
Invalid custom dimensions leave the current viewport unchanged and show an error.
The fields retain invalid text for correction.

Tall viewports scroll vertically within the preview pane.
The native scroll container does not support horizontal scrolling.
If the requested width exceeds the pane width, the viewport fits the pane and shows a width-limit notice.
The source-preview divider can provide more width.
The presets change layout constraints, not display scaling or device emulation.
Preview picking and source outlines retain their existing version checks.

### Properties and structure

The inspector shows each supported argument and its current source value.
Literal values include quoted text, numbers, booleans, and literal tuples.
**Apply property** compiles the candidate before it changes the source.
Expressions, event handlers, references, and style names remain read-only in the inspector.
The source editor accepts these expressions directly.
Compiler errors appear without changing the document.

**Properties: Choose literal color** opens a native color picker for an existing `foreground`, `background`, or `borderBrush` integer literal.
The picker reads the active property draft, not a computed preview color.
Native RGB fields and swatches provide color choices.
The alpha field is disabled because style colors are opaque.
Invalid channel text disables **Use color** instead of accepting the previous channel value.

**Use color** updates the property draft and returns focus to its editor.
**Apply property** remains a separate compiled source edit with native undo.
Cancel or Escape leaves the property draft unchanged.
An unchanged color retains its exact source spelling, including decimal or hexadecimal notation.
A changed color uses six hexadecimal digits, such as `0x12ABCD`, and retains surrounding whitespace.

Theme colors, resource references, expressions, unset properties, and non-color arguments remain outside this picker.
Invalid drafts report an error without opening the dialog.
Source, selection, or draft changes invalidate an open dialog and prevent stale results from replacing a newer draft.
Opening or accepting the picker does not replace the preview or create a source undo entry.

**Properties: Reveal value in source** in the command palette selects the active property's exact authored value and focuses the native source editor.
It works for literals, expressions, event-handler names, and references.
The selection excludes the argument name, separators, and comments before the value.
Multiline values and UTF-16 positions use the current source model.
Event handlers and references select their authored names, not their definitions.

Navigation preserves the selected control, property, filters, and unapplied drafts, including invalid fields.
It does not change source, native undo, or preview ownership.
Unset properties, stale source, and pending visual edits prevent navigation.
After navigation, source typing uses the ordinary native editing and preview pipeline.

**Find a property** filters the property selector by name, without changing the active editor.
Search is case-insensitive. Every whitespace-separated term must match the same property name.
For example, `font si` matches `fontSize`.
**Authored only** limits matches to arguments explicitly set on the selected control, including expressions.
The clear button clears both filters and returns focus to the query.

The active property stays in the selector even when it does not match the filters.
Its entry then includes **(current)**, and the **Editing:** label identifies the active editor.
The match count excludes this retained entry.
Filtering, clearing, and reselecting the current property preserve raw and structured drafts.
Selecting a different property retains the existing behavior: its source value replaces the previous draft.

**Revert draft** discards unapplied changes to the active property and restores its latest authored value.
An unset property returns to an empty draft. The action does not add or remove source arguments.
It retains the property selection, filters, and active raw, text, dimension, boolean, or inset mode.
Invalid draft fields can also revert. Focus returns to the active property editor.
**Properties: Revert draft** in the command palette provides the same action.
Expressions, stale source, and pending visual edits prevent reversion.

Revert does not compile source, change the preview, or create a source undo entry.
**Reset** is different: it removes an authored named argument through a compiled source edit.

**Properties: Find a property** in the command palette focuses the query.
Filters remain available across selection and source changes, including parsing and invalid source.
They do not change source, native undo, preview state, or read-only restrictions.

**Edit as text** decodes an existing string literal into the native property editor.
This mode accepts ordinary, verbatim, and raw strings, without quotes or escape sequences in the editor.
Apply encodes changed text as a quoted C# literal and converts paragraph breaks to LF in the string value.
Unchanged text preserves the exact original literal, including its delimiters and newline escapes.
An unchanged Apply creates no source edit or undo operation.

The inspector starts in raw literal mode and returns to that mode when the selection or source changes.
Switching between modes preserves an unapplied property value when conversion succeeds.
Failed conversion leaves that value unchanged and shows an error.
Expressions, interpolation, UTF-8 literals, NUL, and invalid Unicode cannot use text mode.

**Edit dimensions** replaces the raw property editor with **Width** and **Height** fields for existing literal `size` and `preferredSize` tuples.
Each field accepts one finite, non-negative numeric literal within the single-precision range.
Values use C# syntax, including decimal points, exponents, hexadecimal numbers, and numeric suffixes.
**Apply property** uses the same compilation, revision, and native undo checks as other property edits.
Parent layout constraints still determine the final arranged size.

Unchanged fields preserve the exact original tuple.
Changed dimensions preserve tuple whitespace and the spelling of the other field.
Switching back to raw mode preserves the unapplied dimension draft.
Invalid field syntax prevents conversion and leaves the draft visible for correction.
Commented tuples, named tuples, and expressions cannot use dimension mode.
Selection or source changes return the inspector to raw mode, and property-focus commands target the currently active editor.

**Edit boolean** replaces the raw editor with a native toggle for an existing `true` or `false` literal.
The caption shows **Value: true** or **Value: false**.
The toggle changes a draft. **Apply property** compiles the candidate before it changes source.
Unchanged values preserve the exact original source and native undo history.
Changed values preserve surrounding whitespace.

Switching back to raw mode preserves the boolean draft.
Expressions and unset arguments stay in raw mode.
Source comments outside the literal remain unchanged. A raw draft with comments cannot enter boolean mode.
Failed conversion preserves the raw draft and shows an error.
Reset uses the existing guarded removal of named arguments.
Selection or source changes clear boolean mode, and property-focus commands target the active toggle.

**Edit insets** provides native **Left**, **Top**, **Right**, and **Bottom** fields for existing literal `padding` and `borderThickness` arguments.
Each field accepts a C# numeric literal from 0 through 32768 device-independent pixels, without a sign.
A uniform scalar populates all four fields.
Stack `padding` remains a uniform structural value and cannot use this mode.

Unchanged fields preserve the exact original source.
A scalar becomes a four-value tuple when the edge spellings differ. Equal edge spellings retain scalar syntax.
An existing tuple stays a tuple, with its whitespace and unchanged numeric spelling intact.
Named tuples, comments within the value, expressions, and unset arguments cannot use inset mode.

Mode changes preserve unapplied drafts. Invalid fields keep the draft visible and identify the rejected field.
**Apply property** and **Reset** retain their compilation, revision, and native undo checks.
Selection or source changes clear inset mode, and property-focus commands target **Left**.

**Reset** removes an authored named literal argument so the control can use its default.
Reset compiles the candidate and creates one native undo operation.
It does not remove positional operands, expressions, references, or comments inside the argument.
Required arguments and unsafe Grid changes produce an error without changing source.

The palette filters controls by name and description.
**Find a control** accepts case-insensitive terms such as `button`, `slider`, `table`, or `layout`.
Multiple terms must all match the control name or description.
The description explains the selected template before insertion.
Filtering preserves the selected template when it remains in the results.
An empty result disables insertion. The clear button restores the complete catalog.
The filter remains available during source errors and pending edits, without changing source.

**Insert control** adds a complete palette control at the end of the selected stack or grid.
**Insert before** and **Insert after** add a sibling beside the selected control.
Sibling insertion requires a VStack, HStack, or Grid parent.
The new control becomes the hierarchy and source selection after compilation.
The root cannot gain siblings, and fixed-child containers retain their required child counts.
Grid insertion and duplication require an empty, valid row and column.
For Grid siblings, before/after changes source order. The row and column fields determine visual placement.
Sibling insertion preserves existing identities and does not copy the selected control.

**Find empty cell** fills the row and column fields with the first free 1-by-1 cell in row-major order.
The search respects existing row and column spans.
A selected Grid uses its own cells, even when its parent is another Grid.
A non-Grid selection uses its immediate Grid parent.
The feedback identifies which Grid supplies the result.

The action does not change source, selection, undo history, or preview state.
Full Grids, unknown track lengths, expression-based placement, overlaps, and out-of-bounds children produce an error without changing the fields.
Stale source and pending visual edits also prevent cell discovery.
Insertion and duplication remain separate actions with their existing compilation and placement checks.

**Delete**, **Duplicate**, **Move up**, and **Move down** act on the selected hierarchy control.
Unavailable commands are disabled, with the reason beside the commands.
In the hierarchy, Delete deletes a control, Ctrl+D duplicates it, and Alt+Up or Alt+Down moves it.
These shortcuts do not replace native source-editor shortcuts.

The wrap buttons place the selected control inside a VStack, HStack, or ScrollView.
**Unwrap one child** removes a safe one-child wrapper without discarding authored properties or identities.
In the hierarchy, Ctrl+G wraps in a VStack, and Ctrl+Shift+G unwraps.
Each grouping action uses the same compilation and native undo checks as other visual edits.

The command palette includes these actions under **Control:**.
They use the current hierarchy selection, even when another editor has focus.
Root deletion, fixed-child restrictions, and movement boundaries disable the corresponding commands.
Stale source and pending visual edits disable all structural commands.
Grid duplication uses the inspector's current row and column fields.

The palette closes before a structural action starts.
Compilation still checks identities, Grid placement, and safe unwrapping.
An enabled command can report a semantic restriction without changing source.
These commands do not add shortcuts to native text fields.

Each visual change creates one native undo operation.
The **Undo** and **Redo** buttons act on the source editor.
Only one visual change compiles at a time.
Typing cancels that change, and the editor rejects results for an older source snapshot.

Source typing does not require valid syntax.
During parsing or after a syntax error, the previous hierarchy has an explicit read-only status.
The editor remains available, and invalid source does not replace the last valid preview.
Pausing live preview does not pause the source hierarchy.

## Source editing API

`Xui.Generator.XuiSourceParser.Parse` exposes the existing compiler parser without a preview or native window.
The result contains the ordered node hierarchy, authored arguments, diagnostics, and exact source ranges.
Parsing does not compile or run authored C#.
`Success` describes syntax, not C# type correctness.
Malformed source returns diagnostics and no editable root.

`SourceRange.Start` and `Length` count UTF-16 code units in the original source.
Ranges use an exclusive `End`.
The parser preserves CR, CRLF, and LF line endings.
A node range starts at its control name and ends after its semicolon or closing brace.
`ArgumentsSpan` and `BodySpan` exclude their surrounding delimiters.
Argument ranges exclude separators, and `ValueSpan` identifies only the expression.

`XuiSourceArgument.ValueKind` distinguishes strings, numbers, booleans, literal tuples, and C# expressions.
Interpolated strings and tuples with state references count as expressions.
`SupportedArguments` contains the property names that the parser accepts for that node.
The `value` name identifies the positional argument.
This metadata does not guarantee that a proposed value has the correct C# type.

`Xui.Designer.VisualDocument` adds revision checks, caret selection, and source edit proposals:

```csharp
var document = VisualDocument.Parse(source, cancellation);
var node = document.FindNode(caretOffset);
if (node is not null)
{
    var result = document.SetArgument(
        document.Revision, node.Id, "size", "(120, 40)",
        cancellation: cancellation);
    if (result.Success)
    {
        var edit = result.Edit!;
        string updatedSource = edit.Apply(document.Revision, source);
        // The resulting node occupies edit.Selection in updatedSource.
    }
}
```

`FindNode` returns the innermost node that contains the caret offset.
It returns no node outside the view or source bounds.
Node IDs belong only to one document revision.
Each new `VisualDocument` has a new revision, even for identical text.

`TryFindEmptyGridCell(revision, gridId, out placement, out error, cancellation)` returns the first free 1-by-1 `GridPlacement`.
The query requires a current Grid node and supports cancellation.
It checks explicit track counts and literal child placement without compiling or running authored code.
A refusal returns `false` and an error message.
The query does not create an edit or change the document revision.

Each successful edit returns one `VisualEdit` with `Revision`, `ExpectedSource`, `Range`, `Replacement`, and `Selection`.
`Selection` identifies the full node range in the resulting source.
`Apply` checks the source and revision preconditions, then returns the changed string.
It does not change a native editor.
The model preserves all source outside the replacement range.

For native editor integration:

1. Parse the current editor text after each text change.
2. Run an edit proposal on a worker with a cancellation token.
3. Before application, compare the live revision and complete source with the proposal.
4. If either precondition differs, discard the proposal.
5. Apply `Range` and `Replacement` through the native undo-preserving range API.
6. Select the resulting `Selection` range.

Edit proposals compile the original and resulting component with the existing generator and Roslyn.
Compilation emits only to memory and does not load or run the authored assembly.
Compiler-invalid documents and compiler-invalid edits return an actionable `Error`, not a replacement.
Cancellation propagates as `OperationCanceledException`.
The model supports self-contained components, including required parameters, but does not load code-behind or project dependencies.

### Diagnostic locations

C# diagnostics for unchanged authored expressions retain their source line, UTF-16 column, and expression range.
This mapping includes positional and named arguments, handlers, placement, state initializers, and embedded C#.
Grid track expressions retain separate mappings inside the generated tuple.
The generator uses enhanced C# `#line` directives without changes to the XUI grammar.

Synthesized expressions retain a source-line fallback instead of an invented exact column.
Ordinary generator input beyond the enhanced directive column limit also retains the source-line fallback.
These fallbacks do not reject otherwise valid source.
The preview compiler displays one-based line and column positions from these mappings.

### Property and structure limits

`SetArgument` accepts one complete C# expression.
It replaces an existing value or inserts one supported named argument.
It preserves unrelated arguments, comments, styles, whitespace, and C# code.
An existing expression requires explicit `replaceExpression: true`.
This approval also applies to event handlers, references, and style names.

`RemoveArgument(revision, nodeId, argumentName, cancellation)` removes an existing named argument and one adjacent comma.
It preserves the remaining arguments and the comments and whitespace between them.
It refuses positional operands and comments or directives inside the argument.
The method compiles the original and candidate source and checks Grid placement after a reset.
Calling this method explicitly authorizes removal of a named expression.
The inspector applies a stricter policy and permits only literal resets.

`DeleteNode` and `DuplicateNode` require a Stack or Grid parent.
`InsertSibling(revision, nodeId, after, template, placement, cancellation)` inserts a palette template beside the selected node.
It resolves the immediate parent and reuses `InsertControl` with the preceding or following child index.
The method retains the same compilation, source-limit, Grid overlap, revision, and cancellation checks.
Grid parents require explicit placement. Fixed-child containers and the view root reject sibling insertion.
The view root cannot move, disappear, or duplicate.
`ScrollView` and `Popup` retain exactly one child.
`SplitView` retains exactly two children.
`MoveNode` swaps adjacent siblings with `delta: -1` or `delta: 1`, including the two SplitView panes.
Inter-node comments remain between the nodes, while comments inside a node move with that node.

`WrapNode(revision, nodeId, wrapper, cancellation)` accepts `ControlTemplate.VStack`, `HStack`, or `ScrollView`.
It can wrap the view root or a nested node.
The new wrapper contains the existing subtree and becomes the selected node.
`UnwrapNode(revision, nodeId, cancellation)` replaces a container with its only child and selects that child.
Both operations preserve parent arity and return one contiguous replacement.

Wrapping does not reindent the existing subtree.
Unwrapping preserves the exact body, including comments before and after the child.
These rules preserve raw and verbatim C# string contents.
New separators use the first authored line ending.
For a single-line document, new separators use native editor CR line endings.

Wrapping transfers authored parent placement arguments from the node to its new wrapper.
Unwrapping transfers these arguments from the wrapper to its child.
The transfer preserves the complete argument text, including expressions and internal comments.
This rule includes Stack `flex` and Grid `row`, `column`, `rowSpan`, and `columnSpan`.
It retains existing Grid cells without a new placement choice, including expressions and unknown track lengths.

Unwrapping refuses a child with its own placement arguments.
It also refuses wrapper configuration or identities that the operation cannot retain.
Only placement arguments and a literal positional wrapper name can disappear from the wrapper header.
Placement transfers to the child, while the literal name disappears with the removed container.
Other values, expressions, `ref`, and automation IDs require an explicit source edit.
Header comments outside transferred arguments also require source editing.

Duplication rejects subtrees with `ref`, `id`, `searchId`, or `Content`.
This rule prevents duplicate identities and repeated ownership of an existing element.
Grid duplication also requires an explicit `GridPlacement`.
It changes placement only in the new copy and refuses existing placement expressions.

`InsertControl` inserts a complete template at an ordered child index in a Stack or Grid.
Templates include Text, Button, Toggle, TextInput, VStack, HStack, Grid, ScrollView, SplitView, DataGrid, NavigationView, RangeInput, and Progress.
ToggleSwitch, ToggleButton, and ProgressRing also have dedicated templates.
CheckBox, HyperlinkButton, SelectorBar, InfoBadge, and MenuBar have dedicated templates.
RangeInput and Progress templates start at 50 within the native default range from 0 through 100.
Both toggle templates start unchecked.
The ProgressRing template retains the native indeterminate default and requests a 32-by-32-DIP preferred size.
CheckBox starts unchecked with three-state input off.
SelectorBar starts with First and Second choices and selects First.
InfoBadge starts as a dot.
MenuBar starts with a File submenu and an Open command.
The HyperlinkButton and MenuBar templates have no application actions.
These palette templates have no handlers or external dependencies.
Wrapper templates contain the required children.
DataGrid starts with Name and Value columns and no rows.
NavigationView starts with its header and search field and no navigation entries.
These two placeholders do not install data providers or event handlers.
Grid insertion requires an explicit `GridPlacement`.
Insertion, duplication, and placement changes reject unknown track lengths, placement expressions, out-of-bounds cells, and overlapping cells.
Intentional overlaps remain available through source editing.

The parser limit is 65,536 UTF-16 code units and 128 nested nodes.
NUL characters and unpaired surrogates produce diagnostics.
These limits apply to visual tooling, not ordinary generator builds.
The source API does not require an embedded preview or change the native editor undo history.

### Optional generated element mapping

The `XUI_DESIGNER` compilation symbol adds two internal members to each generated component:

```csharp
internal int __xuiDesignerNodeCount { get; }
internal global::Xui.Element __xuiDesignerElement(int nodeId);
```

The count and IDs match the preorder hierarchy from `XuiSourceParser`.
ID zero identifies the root, including Stack, Grid, and Content roots.
The lookup returns the existing element instance without a wrapper or allocation.
For `Content`, this instance is the authored external element.
The lookup enforces the owning window thread and rejects IDs outside the component range.
The caller borrows the element and does not acquire ownership.

A preview adapter can call these members directly from a wrapper in the same generated assembly.
It does not need reflection or generated field names.
The adapter must enable `XUI_DESIGNER` in the generator driver and wrapper parse options.
The adapter must associate the mapping with the exact source revision that produced the preview.
The source-tools API does not enable this symbol automatically.

Ordinary builds omit both members and add no designer fields or runtime work.
The mapping does not add native event handlers, ownership changes, or preview UI.

### Applied preview snapshots

The designer compiler enables `XUI_DESIGNER` for its generator driver and runtime wrapper.
Its internal `PreviewHost` adapter exposes these UI-thread-only reads:

```csharp
long? AppliedVersion { get; }
bool TryReadNodeMap(long expectedVersion, out IReadOnlyList<PreviewNodeSnapshot> nodes);
bool TryReadNode(long expectedVersion, int nodeId, out PreviewNodeSnapshot node);
```

Each immutable snapshot contains `Version`, `NodeId`, `ElementType`, `Bounds`, and nullable numeric `ControlId`.
`Bounds` contains the latest arranged rectangle in window-client coordinates, measured in device-independent pixels.
The rectangle is not a visible or clipped region and does not provide hit testing.
`ElementType` is the runtime binding type name, not the authored syntax name.
For example, `VStack` maps to `Stack`, while `Content` maps to its supplied element.
`ControlId` is a diagnostic value, not an ownership grant or a stable identity across revisions.

The reads return `false` when no preview exists or the requested version differs from `AppliedVersion`.
For the matching version, an invalid node ID throws `ArgumentOutOfRangeException`.
A pending or failed edit does not relabel the last successful map with a newer source revision.
The caller must compare the source revision before it uses node IDs for source selection.
After retirement, the old map is no longer available through the adapter.

The adapter creates value snapshots without retaining an element array.
Retained snapshots contain no element, component, delegate, or assembly-context references.
Snapshot reads do not create native peers or handles.
These internal reads do not change the public ownership contract of generated element lookups.

### Versioned pointer selection

The internal preview adapter exposes `SetPointerPickMode(bool enabled)` and the `Picked` event.
`PreviewPick` contains only `Version` and `NodeId`.
The adapter registers the generated source identities with the [native content inspection API](bindings.md#content-pointer-picking).
It never infers hit targets from snapshot rectangles.

Pick delivery requires the same candidate, applied version, and requested source version.
`Supersede` suppresses picks from an older displayed preview even when that preview remains visible after an error.
Scope retirement discards obsolete notifications and releases callback delegates.
The shell checks the exact source snapshot before it uses the versioned ID for source, hierarchy, and inspector selection.

The mode is **pointer picking**, not a disabled-code mode or a sandbox.
Native keyboard input and accessibility actions retain their normal behavior.
Unsupported surfaces and active capture, composition, or modal routes produce explicit errors.
Pointer picking does not require an outline and does not change authored styles.

### Versioned selection outlines

`PreviewHost.TryHighlight(long expectedVersion, int? nodeId)` requests a [non-occluding content outline](bindings.md#non-occluding-content-outlines).
Its result is `Applied`, `Cleared`, `StaleVersion`, `NotVisible`, `OccludedNative`, or `UnsupportedSurface`.
Null explicitly clears an outline for the matching source version.
An invalid ID for that version throws.
A stale request cannot change the outline of a newer preview.

`Applied` describes the current layout only.
A later hidden or unsafe layout suppresses painting rather than changing native regions or input.
A new hidden or unsupported selection clears the old outline instead of leaving the wrong control marked.
The hierarchy and inspector remain the fallback for targets that cannot receive an outline.

`Supersede` rejects stale requests immediately and queues one bounded UI-thread clear.
A fresh accepted highlight cancels an obsolete queued clear.
Content retirement and disposal also clear the selection.
The adapter retains no additional element or assembly references for highlighting.

## Edit and preview

1. Start the designer with its example component or a trusted `.xui` file.
2. Find the native preview beside the source editor.
3. Edit the source in the designer.
4. Read compiler errors in the diagnostics pane.

The compiler waits for a 300 ms pause between edits.
It compiles on a worker, with one queued source snapshot.
New edits cancel obsolete compilation.
XUI and C# diagnostics include the source line and column.
Invalid source leaves the last valid preview unchanged.

**Next**, or F8, selects the next diagnostic that has an authored source location.
**Previous**, or Shift+F8, selects the previous location.
**Go to selected** uses the caret in the diagnostics pane.
Navigation selects the exact source location and its hierarchy control when a current hierarchy is available.
Generated-file locations do not point into the source editor.
Source changes invalidate navigation until the compiler supplies new diagnostics.

The preview supports styles, C# state, and event handlers.
Each successful update replaces the content of the embedded preview and resets component state.
The editor retains its text, selection, and undo history.
Replacement does not activate another window or take focus from the editor.
The preview keeps its place in the designer layout.

With **Live preview** off, automatic compilation pauses.
**Render**, or Ctrl+Enter, compiles the current source even during a pause.
This command also restores a preview after a managed callback error.
**Light theme** changes the designer theme, including the preview.
The preview does not have an independent theme.
Preview construction errors leave the previous content unchanged.
A managed event exception stops further callbacks from that preview.
The designer then removes the failed preview and reports the exception.
The next successful render creates new content.

## Files and recovery

The **File path** field accepts a `.xui` path.
**Open path** loads the path from this field.
**Open...**, or Ctrl+O, opens the native file chooser.
Open accepts UTF-8 source, with or without a byte-order mark.
Invalid syntax does not prevent Open or native source editing.
**New**, or Ctrl+N, creates an unchanged, untitled document from the selected template.
New, Open, and Recovery replace the native document and reset its undo history.
Visual actions use native range edits instead.

For unsaved edits, New and Open require **Discard edits** or **Keep editing** in a native confirmation dialog.
Keep editing preserves the source, undo history, and recovery draft.
Discard approval does not delete the source before the replacement succeeds.
Canceling the file chooser also preserves the current document after discard approval.
Source or revision changes invalidate pending approval and file-chooser results.

**Save**, or Ctrl+S, writes UTF-8 source with LF line endings.
**Save as...**, or Ctrl+Shift+S, opens the native destination chooser.
Save also opens this chooser when the File path field is empty.
Canceling the chooser does not write a file or change the document identity.
Save uses a temporary file in the destination directory before replacing the destination.

Save rejects an existing destination unless the designer loaded that file.
This protection also applies to Save As, regardless of the native chooser's overwrite confirmation.
It compares raw file hashes and rejects changes since the last open or save, including changes to encoding or the byte-order mark.
These checks are not a lock against concurrent writes.
A different destination path keeps both versions.
The designer does not watch files for external edits.

Each edit also writes a separate recovery draft under `%LOCALAPPDATA%\Xui\Designer\Drafts`.
Each designer instance has its own draft name.
A successful save deletes that instance's draft.
Restoring the last saved text also deletes the draft.
Unsaved drafts remain after the designer closes.
Each draft has a metadata file with its source hash, original file path, and saved file hash.

**Recovery** opens a native picker for drafts from other designer instances.
The picker shows a source preview before recovery.
Recovery loads a dirty copy and keeps the original draft.
It does not replace unsaved edits or load drafts automatically.
The picker requires explicit approval before draft deletion.
Damaged metadata or a changed draft reports an error instead of loading unverified file identity.

The file status area reports file and recovery errors without replacing compiler diagnostics.
A successful preview does not clear a file error.
If a save succeeds but draft cleanup fails, the document stays clean and the file status reports the cleanup error.

Programmatic closure during a native file chooser can leave the chooser open in the current designer.
If this occurs, select **Cancel** in the chooser.

## Scope and execution

**CAUTION: Open only trusted source.**
The preview runs arbitrary C# with your account permissions inside the designer process.
It is not a sandbox.
Authored code can access files, start work, hang, or terminate the process.
The editor and the preview share the same UI thread.
The designer cannot interrupt arbitrary C# safely.
Collectible assembly unloading requires authored tasks and static references to release their objects.

The preview uses a scoped native ownership boundary, not process isolation.
The scope releases its controls, handles, resources, and managed callback registrations after replacement.
Queued scoped callbacks cannot restore retired content.
Source versions prevent obsolete compilation results from replacing newer content.

Native materialization errors and immutable-source query errors retain the normal XUI fatal-window contract.
They can close the designer.
The designer guarantees old-content preservation for parse, compile, and managed construction errors, not arbitrary authored side effects.

The first version accepts one self-contained component with no required parameters.
It supports stack and non-stack roots.
It does not load a project, code-behind files, additional components, or NuGet dependencies.
The source limit is 65,536 UTF-16 code units.
The designer rejects NUL characters and invalid Unicode.

The preview uses the explicit [content scope](bindings.md#scoped-content-replacement) API.
Ordinary application topology remains fixed before `Run`.
This version has no drag-and-drop design surface, syntax highlighting, or state-preserving reload.
The designer requires managed, untrimmed, multi-file deployment.
Unlike ordinary generated applications, this development tool includes Roslyn and the XUI generator at runtime.
