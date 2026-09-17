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
