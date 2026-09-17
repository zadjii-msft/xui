# XUI Designer

The designer is a XUI application for editing one `.xui` component.
Its [layout](../../bindings/dotnet/Designer/DesignerLayout.xui) uses `.xui`.
Its native `MultilineText` editor uses Consolas and keeps Windows selection, clipboard, undo, and IME behavior.
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

The [template sources](../../bindings/dotnet/Designer/Templates) use the same language as ordinary applications.
The [counter source](../../bindings/dotnet/Designer/Starter.xui) supplies the initial example.
The examples do not access the network or save application data.

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

### Property and structure limits

`SetArgument` accepts one complete C# expression.
It replaces an existing value or inserts one supported named argument.
It preserves unrelated arguments, comments, styles, whitespace, and C# code.
An existing expression requires explicit `replaceExpression: true`.
This approval also applies to event handlers, references, and style names.

`DeleteNode` and `DuplicateNode` require a Stack or Grid parent.
The view root cannot move, disappear, or duplicate.
`ScrollView` and `Popup` retain exactly one child.
`SplitView` retains exactly two children.
`MoveNode` swaps adjacent siblings with `delta: -1` or `delta: 1`, including the two SplitView panes.
Inter-node comments remain between the nodes, while comments inside a node move with that node.

Duplication rejects subtrees with `ref`, `id`, `searchId`, or `Content`.
This rule prevents duplicate identities and repeated ownership of an existing element.
Grid duplication also requires an explicit `GridPlacement`.
It changes placement only in the new copy and refuses existing placement expressions.

`InsertControl` inserts a complete template at an ordered child index in a Stack or Grid.
Templates include Text, Button, Toggle, TextInput, VStack, HStack, Grid, ScrollView, and SplitView.
Wrapper templates contain the required children.
Grid insertion requires an explicit `GridPlacement`.
Insertion, duplication, and placement changes reject unknown track lengths, placement expressions, out-of-bounds cells, and overlapping cells.
Intentional overlaps remain available through source editing.

The parser limit is 65,536 UTF-16 code units and 128 nested nodes.
NUL characters and unpaired surrogates produce diagnostics.
These limits apply to visual tooling, not ordinary generator builds.
The source API does not require an embedded preview or change the native editor undo history.

## Edit and preview

1. Start the designer with its example component or a trusted `.xui` file.
2. Place the designer and preview windows beside each other.
3. Edit the source in the designer.
4. Read compiler errors in the diagnostics pane.

The compiler waits for a 300 ms pause between edits.
It compiles on a worker, with one queued source snapshot.
New edits cancel obsolete compilation.
XUI and C# diagnostics include the source line and column.
Invalid source leaves the last valid preview unchanged.

The preview supports styles, C# state, and event handlers.
Each successful update creates a new preview window and resets component state.
The editor retains its text, selection, and undo history.
The new preview does not take keyboard focus.
Window position and size do not persist between updates.

With **Live preview** off, automatic compilation pauses.
**Render / reopen**, or Ctrl+Enter, compiles the current source even during a pause.
This command also opens a preview after you close its window.
**Light preview** selects the light theme and creates a new preview.
Preview construction errors leave the previous window unchanged.
An exception from a preview callback closes that preview and appears in the designer.

## Files and recovery

The **File path** field accepts a `.xui` path.
Open accepts UTF-8 source, with or without a byte-order mark.
**Open** replaces an unchanged document.
If the document has unsaved edits, save it before opening another file.
**Save**, or Ctrl+S, writes UTF-8 source with LF line endings.
Save uses a temporary file in the destination directory before replacing the destination.

Save rejects an existing destination unless the designer loaded that file.
It also rejects changes detected on disk since the last open or save.
These checks are not a lock against concurrent writes.
A different destination path keeps both versions.
The designer does not watch files for external edits.

Each edit also writes a separate recovery draft under `%LOCALAPPDATA%\Xui\Designer\Drafts`.
Each designer instance has its own draft name.
A successful save deletes that instance's draft.
Restoring the last saved text also deletes the draft.
Unsaved drafts remain after the designer closes.
Recovery drafts require an explicit **Open** and do not load automatically.
The diagnostics pane reports file and recovery errors.

## Scope and execution

**CAUTION: Open only trusted source.**
The preview runs arbitrary C# with your account permissions inside the designer process.
It is not a sandbox.
Authored code can access files, start work, hang, or terminate the process.
Closing the designer requests preview shutdown but cannot safely interrupt arbitrary C#.

The first version accepts one self-contained component with no required parameters.
It supports stack and non-stack roots.
It does not load a project, code-behind files, additional components, or NuGet dependencies.
The source limit is 65,536 UTF-16 code units.
The designer rejects NUL characters and invalid Unicode.

The preview has a separate window because XUI fixes native tree ownership before `Run`.
This version has no embedded preview pane, drag-and-drop design surface, syntax highlighting, or state-preserving reload.
The designer requires managed, untrimmed, multi-file deployment.
Unlike ordinary generated applications, this development tool includes Roslyn and the XUI generator at runtime.
