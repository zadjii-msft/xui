# XUI Designer

The designer is a XUI application for editing one `.xui` component.
Its [layout](../../bindings/dotnet/Designer/DesignerLayout.xui) uses `.xui`.
Its native `MultilineText` editor uses Consolas and keeps Windows selection, clipboard, undo, and IME behavior.
An [LSH-enabled build](../../CONTRIBUTING.md#lsh-highlighting-in-xui-applications) highlights XUI and embedded C# as the source changes.
Highlighting remains active when live preview is paused.
The preview uses the existing XUI compiler and native controls, not an HTML approximation.

Build and run commands are in [CONTRIBUTING](../../CONTRIBUTING.md#xui-designer).
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

Ctrl+F focuses the native Find field above the source editor.
F3 selects the next literal match, and Shift+F3 selects the previous match.
The **Aa** toggle selects case-sensitive matching.
Search reads the current source each time and does not change text or undo history.
Enter in the Find field selects a match, and Escape returns focus to source.

The hierarchy uses a native TreeView with expandable controls.
Selecting a control selects its source range and scrolls the source editor to that range.
**Select from caret**, or Ctrl+Shift+L, selects the control that contains the source caret.
Hierarchy identities belong to one exact source revision.
A new source revision resets tree expansion and selects the control at the current caret.
It does not reuse identities from an older document.

**Pick controls** selects an authored control through a primary-pointer click in the current preview.
The click selects its full source range, hierarchy node, and inspector.
The mode consumes the pointer action instead of running its authored handler.
Keyboard and accessibility actions remain live.
An outdated preview cannot select controls in newer source.
Unsupported preview surfaces produce an explicit error and keep the previous mode.

Selection from the hierarchy, Find, diagnostics, or preview also requests a preview outline.
The outline does not cover native controls or change their input behavior.
The preview status explains hidden, clipped, overlapping, and unsupported selections.
The hierarchy and inspector remain available when an outline cannot appear.
A source revision clears the outline until the current preview is ready.
Later layout changes can hide an outline that was initially visible.

The inspector shows each supported argument and its current source value.
Literal values include quoted text, numbers, booleans, and literal tuples.
**Apply property** compiles the candidate before it changes the source.
Expressions, event handlers, references, and style names remain read-only in the inspector.
The source editor accepts these expressions directly.
Compiler errors appear without changing the document.

**Edit as text** decodes an existing string literal into the native property editor.
This mode accepts ordinary, verbatim, and raw strings, without quotes or escape sequences in the editor.
Apply encodes changed text as a quoted C# literal and converts paragraph breaks to LF in the string value.
Unchanged text preserves the exact original literal, including its delimiters and newline escapes.
An unchanged Apply creates no source edit or undo operation.

The inspector starts in raw literal mode and returns to that mode when the selection or source changes.
Switching between modes preserves an unapplied property value when conversion succeeds.
Failed conversion leaves that value unchanged and shows an error.
Expressions, interpolation, UTF-8 literals, NUL, and invalid Unicode cannot use text mode.

**Reset** removes an authored named literal argument so the control can use its default.
Reset compiles the candidate and creates one native undo operation.
It does not remove positional operands, expressions, references, or comments inside the argument.
Required arguments and unsafe Grid changes produce an error without changing source.

The palette inserts a complete control at the end of the selected stack or grid.
Grid insertion and duplication require an empty, valid row and column.
**Delete**, **Duplicate**, **Move up**, and **Move down** act on the selected hierarchy control.
Unavailable commands are disabled, with the reason beside the commands.
In the hierarchy, Delete deletes a control, Ctrl+D duplicates it, and Alt+Up or Alt+Down moves it.
These shortcuts do not replace native source-editor shortcuts.

The wrap buttons place the selected control inside a VStack, HStack, or ScrollView.
**Unwrap one child** removes a safe one-child wrapper without discarding authored properties or identities.
In the hierarchy, Ctrl+G wraps in a VStack, and Ctrl+Shift+G unwraps.
Each grouping action uses the same compilation and native undo checks as other visual edits.

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
RangeInput and Progress templates start at 50 within the native default range from 0 through 100.
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
