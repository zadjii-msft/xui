# C ABI, C#, and Rust bindings

Build and test commands are in [CONTRIBUTING](../../CONTRIBUTING.md).
See the [reference index](README.md) for related APIs.
Examples in this reference use C++ unless stated otherwise.

## Feature bindings (1.1 extension)

### Fluent C# setters

C# configuration methods return the original object, with its concrete type.
Size methods and shared control methods preserve this type throughout a chain.

```csharp
var map = w.MapView("Offline map")
    .SetView(new(47.6, -122.3), 4)
    .SetMarkers([new(1, new(47.6, -122.3), "Seattle")])
    .FixedSize(650, 180);

var range = w.RangeInput("Zoom")
    .FixedSize(200, 24)
    .SetRange(new(0, 100))
    .SetValue(20);
```

Writable properties retain their assignment syntax and also have `Set<Property>` methods.
For example, `range.Value = 20` and `range.SetValue(20)` use the same setter.
`SetText` uses the document setter for rich and multiline text controls, including references typed as `Control`.
Setters retain their validation and UI-thread requirements.
They return the receiver only after the operation succeeds.
Events, lifecycle methods, and methods that return resources retain their existing contracts.

The native ABI does not change.
The managed method signatures change, so consumers must rebuild.
Size methods are now generic extension methods in the `Xui` namespace.
Void delegate assignments can require a lambda, such as `Action apply = () => range.SetValue(20);`.

### Feature extension

This extension supersedes earlier statements that the new controls have C++ APIs only.
The original nine control kinds remain available.
The extension adds typed constructors to both C# and Rust.
These include compositions and the earlier workspace controls.
The bindings use the existing native controls, layout, drawing, input, and accessibility.
They contain no second renderer or retained row array.

### Current coverage

| Family | C# and Rust types | Usable contracts |
| --- | --- | --- |
| Numeric and exclusive choice | `RangeInput`, `NumericInput`, `RadioGroup`, `ComboBox`, `Progress` | Ranges, values, orientation, selection, editable combo creation, state, events |
| Actions and disclosure | `SplitButton`, `Expander`, `Popup` | Independent actions, expanded state, arbitrary popup content, explicit owner and anchor |
| Virtual collections | `ItemsView`, `TreeView`, `ImmutableSource` | Constant-cost identity lookup, bounded row callbacks, compact select-all, lazy tree requests, owner-bound completion |
| Layout and workspace | `Grid`, `Wrap`, `AdaptiveLayout`, `TabStrip`, `SplitView`, `PageView` | Tracks, cells, wrapping, breakpoints, retained panes, tabs, active pages |
| Data and history | `DataGrid`, `HistoryChart` | Immutable source, logical columns, resize, reorder, filter state, sort state, check selection, samples |
| Commands | `CommandBar`, `CommandSurface` | Nested command records, separate pin actions, shortcuts, hints, overflow, explicit popup display |
| Navigation | `Breadcrumb`, `NavigationPane`, `LocationPicker`, `ViewPicker` | Path segments, immutable sources, typed borrowed children, presentation and size controls |
| Documents | `MultilineText`, `RichText`, `PasswordInput` | Native editing, authored runs, selection, editing commands, read-only mode, scoped password access |
| Forms | `DateTimePicker`, `ColorPicker`, `InlineStatus`, `ContentDialog` | Local Gregorian fields, RGBA values, status dismissal, arbitrary dialog content, validation message, result events |
| Scenes and maps | `VectorCanvas`, `MapView` | Immutable shapes, transforms, clips, stable IDs, offline markers, view state, cancelable overlay tokens |
| Native hosts | `MediaPlayback`, `WebContent` | Explicit local media load, volume, seek, playback actions, allowed origins, HTML, JavaScript result tokens, stop and unload |
| Window integration | `Window` | Optional custom title bar and explicit native Shell menu fallback |

`include\xui\xui_features.h` declares the extension through `xui.h`.
`bindings\generate_features.py` generates both FFI declarations from that header.
It generates typed constructors and scalar properties from `bindings\features.json`.
The handwritten feature modules implement collections, scoped secrets, request ownership, and typed records.

### Miller columns in C#

`Window.MillerColumns` creates the native hierarchy control.
`SetColumns` accepts a complete path of `MillerColumn` records.
Each record contains a title, an `ImmutableSource`, and an optional selected key.
The source supplies `HasChildren` for branch indicators.

```csharp
var columns = window.MillerColumns("Folders");
using var source = window.ImmutableSource(rootItems);
columns.SetColumns([new("Root", source)]);
columns.SelectionChanged += item => StartChildQuery(item.Column, item.Key);
columns.ItemActivated += item => OpenItem(item.Column, item.Key);
```

The application supplies `rootItems`, `StartChildQuery`, and `OpenItem`.
The application delivers completed queries through `Window.Post`.
The application must reject obsolete results after selection, tab changes, cancellation, or window closure.
`SetColumns` is a silent setter. Neither selection nor source replacement opens a file.

`ActiveColumn` identifies the active sibling list. `ColumnWidth` accepts 120 to 2,000 DIPs.
`FocusColumn` reveals a column and moves native focus into its list.
`Column(index)` returns a stable borrowed list for selection, scrolling, focus, and context menus.
The owning control supplies column sources through `SetColumns`, not through the borrowed list.
`MillerColumns.MaxColumns` is 32.

`HorizontalOffset` reads or sets the horizontal position in DIPs.
`MaximumHorizontalOffset` supplies the current limit. The limit is zero before layout.
The setter rejects non-finite values and positions outside the current range.
Horizontal wheel input, Shift+wheel, and the bottom scrollbar work without application event handlers.
Horizontal movement preserves selection and the vertical position of each column.

The native control retains source references after an `ImmutableSource` wrapper is disposed.
A disposed wrapper cannot be supplied to a later `SetColumns` call.
Applications can retain wrappers for unchanged columns and dispose them after path replacement.
Events contain the column index and both parts of `ItemKey`.

`xui_miller_*` exports supply the C ABI.
The generated Rust FFI includes these exports.
The typed Rust wrapper currently supplies construction only, without the C# path and event helpers.
Declarative markup can contain the control through an application-supplied element.
The FileExplorer sample creates it in C# inside its declarative layout.

### ABI versions

The baseline `xui_abi_version()` remains `0x00010000`.
Old records, kind values, exports, and version negotiation remain unchanged.
`xui_feature_version()` returns `0x00010001`.
Top-level feature options and values contain explicit size and version fields.
Other record arrays validate their sizes.
The wrappers check the feature major version before construction.
`xui_capabilities()` reports build support for WebView2 without starting a runtime.

### Ownership and data limits

All UI calls, source callbacks, and completions require the creating UI thread.
Background tasks must deliver their results through an application-owned UI queue.
The wrappers do not capture a synchronization context or marshal worker calls automatically.
C# uses a disposable window owner. Rust uses `Rc` ownership and does not implement `Send` or `Sync` for controls.
Borrowed children retain their window owner and reject use after native disposal.

The immutable source interface supplies `Count`, `Key`, `Find`, and row content.
`Find` must not enumerate the source. The count and identity mapping must remain stable for each snapshot.
The binding copies only requested row fields, not the complete source.
Selection exposes the focused key and compact term count.
`Contains` in C#, or `contains` in Rust, tests membership without expanding all selected identities.
Each primary or secondary field has a 1,024-byte UTF-8 limit.
An oversized field fails instead of triggering a full-row-array fallback.
Source callbacks must remain nonblocking and must not call another source callback.
The ABI rejects mutations of the source window during a source callback.

Source contexts use native retain/release ownership.
C# snapshots implement `IDisposable`. Rust releases a snapshot handle when its last wrapper drops.
Attached controls and selection terms retain their own source references.
Replacing a source can therefore release its old context without waiting for window destruction.
Managed exceptions and Rust panics become callback failures.
Different controls can dispatch nested events. Recursion on the same event source fails with `XUI_BUSY`.
Window destruction remains blocked during a callback or `Run`.
Closing a window revokes delivery before destruction releases its source contexts.

Tree requests expose their stable `ItemKey`.
Map and tree completion tokens belong to one control, not merely a generation number.
Successful completion consumes a token. Cancellation or disposal revokes it.
Rust request objects cancel on drop. C# request objects implement `IDisposable`.
Window destruction also revokes all remaining tokens.

Document strings cross the ABI as UTF-8. Selections count UTF-16 code units.
The selection setter rejects offsets inside a surrogate pair.
Password controls have no ordinary plaintext getter.
C# `WithPassword` supplies a scoped UTF-8 span. Rust `with_password` supplies a borrowed byte slice.
Native temporary plaintext buffers are erased after the callback.
The password limit is at most 4,096 UTF-16 code units.

Creating a media or web control starts no optional runtime.
Only explicit source loads start the native hosts.
JavaScript evaluation returns a disposable native-owned result token.
`TryGetResult` or `try_result` reads completion on the UI thread.
No managed delegate or Rust closure remains pinned for JavaScript completion.
Stop, unload, source replacement, hidden-host unload, and window close reject stale results.
Token disposal releases storage even when WebView2 never calls its completion handler.

### Examples

The [contributor guide](../../CONTRIBUTING.md#binding-generation-and-compatibility) describes how to build the feature samples.
Each application shows choices, a range, an offline map, and a million-row virtual source.
F6 opens a document and color dialog. Escape cancels it. F8 changes the range. F12 closes the window.
The `--callback-fail` argument makes the F8 event report a controlled callback failure.
The source logs its actual visible-row fetch count after a normal run.
The examples perform no Shell verb and load no remote website.

A C# range uses ordinary typed properties:

```csharp
using var window = new Xui.Window();
var root = window.Stack();
var range = window.RangeInput("Volume");
range.Range = new(0, 100, 1, 10);
range.Value = 25;
range.Event += e => Console.WriteLine(range.Value);
root.Add(range);
window.SetContent(root);
window.Run();
```

The complete examples are [C#](../../bindings/dotnet/Sample/FeatureDemo.cs), [Rust](../../bindings/rust/sample/src/features.rs), and [C++](../../bindings/native/features.cpp).
The ABI constructor test covers all 35 added kinds.
Both language test suites exercise typed properties, source limits, callback failures, and disposal.
The tests preserve the existing native focus and accessibility assertions.

### Advanced API gaps

Coverage is family-level, not full C++ method parity.
The extension does not expose custom collection groups, full-source selection domains, rectangle gestures, or custom row action/icon metadata.
It does not expose navigation-query completion, command-query completion, DataGrid filter-worker completion, or the abstract Shell provider session.
Applications can replace immutable snapshots explicitly and handle command or header events.
DataGrid filter and sort setters store state; applications supply the filtered or sorted snapshot.

Map route polylines, custom date bounds, locale selection, color swatch configuration, and live dialog validation functions remain C++ APIs.
Dialogs instead accept a validation message that applications update with their form state.
JavaScript completion uses result polling rather than a language callback subscription.
Optional browser and media adapter interfaces remain internal.
The source interface is UI-thread-only and does not supply a worker dispatcher.

The [binding measurements](../llm/bindings-history.md) remain historical evidence.
This extension makes no new process-memory reduction claim and does not alter the gallery memory baseline.

## C ABI and language bindings (phase 4)

This section describes the original ABI baseline.
The feature extension section supersedes its control-coverage limitations.
The native engine owns layout, drawing, input, accessibility, image work, and virtual rows.
C# and Rust supply control properties, data, and event handlers. Neither binding contains a second retained engine or a row painter.
The existing static C++ targets do not load the new DLL.

### Files and supported surface

| Path | Purpose |
| --- | --- |
| `include\xui\xui.h` | Public C header, calling convention, fixed-width structures, statuses, and exports |
| `src\c_api.cpp` | Handle registry, UTF-8 conversion, ownership checks, batches, and exception boundary |
| `bindings\dotnet\Xui` | Source-generated `LibraryImport` declarations and managed wrapper classes |
| `bindings\dotnet\Sample` | Actual managed application |
| `bindings\dotnet\Tests` | Separate executable wrapper tests |
| `bindings\rust\xui-sys` | Raw `repr(C)` declarations and import-library linkage |
| `bindings\rust\xui` | Safe `Rc`-owned wrappers, `Result`, and callback containment |
| `bindings\rust\sample` | Actual Rust application |
| `bindings\native\direct.cpp` | Equivalent static C++ application |
| `bindings\native\sample.cpp` | Equivalent C++ application through the C ABI |
| `tests\phase4.ps1` | Reproducible builds, tests, deployment copies, and measurements |

The surface covers Window, Stack, Label, Button, Toggle, TextInput, ScrollView, Image, and FileList.
Sizing includes automatic, preferred, fixed, minimum, and maximum sizes.
Properties also cover text, accessible names, automation IDs, enabled state, checked state, padding, spacing, scroll offsets, and window themes.
The engine retains the same fonts, theme tokens, cached text layouts, and one render target per window.

FileList accepts copied item arrays with stable 64-bit IDs, synchronous filters, selection, and view/selection events.
Each item array contains at most 1,000,000 rows.
Item ID zero is valid. Selection presence has a separate result.
Hidden selections retain their IDs. Row indices refer to the visible view.
`UINT32_MAX` clears selection. Duplicate item IDs return an error without replacement of the previous view.

The C FileList API is a small-data convenience, not the asynchronous `ViewTask` API.
Large source construction and filtering can block its UI thread.
The existing native asynchronous API remains available to C++ applications.
The original ABI does not expose arbitrary drawing, mutable borrowed spans, context menus, clipboard operations, tree replacement, or custom row templates.
Later headers and wrappers extend this baseline. See [current coverage](#current-coverage).

Image accepts a copied file path and decode bounds from 1 through 1,024 pixels per dimension.
An empty path unloads the image. The status query returns Empty, Loading, Ready, or Error.
The bindings do not expose image error details, image resource limits, or image resource counters.
Synchronous API errors still use the status and diagnostic contract below.

### ABI contract

Version `0x00010000` identifies this interface.
`xui_abi_version()` supplies the runtime version. Window creation checks the caller version again.
The current wrappers require an exact version match and report a mismatch before object use.
The high 16 bits identify the major version. The low 16 bits identify the minor version.

Version 1 uses exact structure sizes, not an implicit append-only layout.
Reserved fields must be zero. Unknown properties and kinds return an error.
Future additions must preserve existing exports and accepted version-1 structures.
A larger structure needs a new versioned entry point or an explicitly supported size.
A breaking change needs a new major version and a distinct deployment contract.
Replacing this DLL with an incompatible runtime is not supported.

| C structure | ARM64 size | Important offset |
| --- | ---: | ---: |
| `xui_string` | 16 | `length`: 8 |
| `xui_window_options` | 40 | `title`: 8 |
| `xui_property` | 56 | `integer`: 48 |
| `xui_event` | 24 | `source`: 8 |
| `xui_file_item` | 48 | `id`: 8 |

All exported functions use C linkage and `__cdecl`. The header exposes no STL or C++ class layout.
Handles are nonzero 64-bit generation tokens, not addresses.
A process-wide monotonic counter never reuses a token. Exhaustion returns an error instead of wraparound.
Registry lookup rejects arbitrary, stale, and wrong-kind tokens before native object access.
The registry lock also protects concurrent lookup. A valid handle still requires its creating UI thread.
Raw C buffer pointers must reference accessible caller-owned memory for their stated lengths.
The ABI cannot make an invalid arbitrary buffer pointer safe.

### Ownership, strings, and errors

A window owns an arena of at most 65,536 handles, including its own handle.
Controls remain valid until `xui_window_destroy`. There is no independent control-release operation.
The window retains detached controls too. This policy gives callback targets and tree children one explicit lifetime.
Destroy revokes every subscription and handle before native resource release.
A later call with any revoked handle returns `XUI_INVALID_HANDLE`.

All object operations, callbacks, and destruction use the creating UI thread.
Tree construction finishes before `xui_window_run`. A window runs once.
Only one window can run on a UI thread at a time.
The caller must use STA-compatible COM initialization, not MTA.
`close` requests closure. It does not destroy the arena.
Destroy during a run or callback returns `XUI_BUSY` without partial teardown.

Strings use UTF-8 with explicit byte lengths. The input spans remain valid only for the duration of the call.
The native engine copies strings and item arrays. Callers keep ownership of all input and output buffers.
No caller frees memory from the native allocator.
Embedded NUL, malformed UTF-8, nonzero reserved fields, and strings larger than 1 MiB return `XUI_INVALID_ARGUMENT`.
The managed wrapper also rejects unpaired UTF-16 surrogates. Rust `str` already requires valid UTF-8.
TextInput retains its native 1,024-UTF-16-unit limit and preserves surrogate pairs at that limit.
The text property updates native input text for TextInput and the displayed name for other controls.
Native output replaces unpaired UTF-16 units with U+FFFD.
EDIT can publish a temporary high surrogate before the next `WM_CHAR` completes the pair.
This output policy keeps foreign callbacks valid without changes to the native text buffer.
The final pair returns its original Unicode scalar. Caller-supplied malformed UTF-8 remains an error.

Copy functions return a required byte count without a terminator.
A null buffer with zero capacity requests the count.
An insufficient buffer returns `XUI_BUFFER_TOO_SMALL` without a partial copy.
The caller supplies a larger buffer and repeats the call on the same thread.
Empty output requires zero bytes and succeeds with a null, zero-capacity buffer.

Every status-returning export contains native exceptions, including allocation failures.
Successful exports clear the thread-local error. Failed exports replace it with a status and diagnostic.
Diagnostics contain at most 1,023 UTF-8 bytes without a partial code point.
Malformed bytes in an unexpected native exception message become question marks.
`xui_error_copy` is the exception: it never changes the stored error, even during a size query.
Its own return value describes the copy operation. Its `error` output identifies the original error.
This separation removes ambiguity between a failed operation and an insufficient diagnostic buffer.

A callback receives a temporary `xui_event` and returns zero on success.
The event has no borrowed text pointer. Text access uses `xui_text_copy`.
Key values contain the virtual key in the low bits, Ctrl in bit 32, and Shift in bit 33.
Key callbacks report errors, not key consumption. Native default keyboard behavior continues.
Callbacks must not let foreign exceptions escape.

Native dispatch contains unexpected C++ callback exceptions too.
A nonzero callback result becomes a persistent window failure and requests closure.
The enclosing invoke, list operation, or run returns `XUI_CALLBACK_FAILED`.
`xui_window_callback_error` returns the original callback status after that operation.
The wrappers preserve the managed exception or Rust error/panic message.
Recursive event delivery returns a callback failure instead of recursive foreign borrowing.

A null callback revokes its subscription synchronously.
Revocation inside the active callback is valid. The current dispatch finishes, but no later dispatch uses that context.
Destroy remains invalid until the callback and run return.
Callbacks can change properties, revoke subscriptions, and request closure.

### Batched updates

`xui_update` accepts at most 4,096 property records.
It checks all handles, kinds, numeric values, structure sizes, and UTF-8 strings before mutation.
An invalid record leaves the entire batch unchanged.
The apply phase does not invoke application property callbacks or pump the message loop.
Native invalidation combines the resulting work for the next frame.
Separate setter calls also combine invalidations until the message loop draws.

A batch is not a database transaction against resource exhaustion.
A native allocation or platform failure during apply can leave earlier valid properties applied.
The caller receives an error, not success.
The throughput benchmark distinguishes batched boundary calls from individual calls. It does not claim a different renderer or a paint per setter.

### Managed ownership and use

`Window` implements `IDisposable`. Each managed element belongs to that window.
Disposal revokes native handles before it frees callback contexts.
Element access after disposal throws `ObjectDisposedException`.
Wrong-thread access throws `XuiException` before the P/Invoke call.
Dispose during a callback or `Run` throws a busy error. A callback uses `Close` instead.

These UI-thread-owned handles deliberately do not use a finalizer-driven `SafeHandle`.
A finalizer cannot safely destroy HWNDs or revoke callbacks on the UI thread.
The wrapper has no finalizer. Applications must dispose their windows.
Failure to dispose retains the native arena until process exit.

Subscriptions use weak `GCHandle` contexts and strong managed ownership during `Run`.
The native registration does not permanently root the managed window.
The trampoline retains its current subscription through callback completion, including self-unsubscribe.
Every trampoline catches managed exceptions and returns a native callback error.
CsWin32 is not involved. It generates Windows API bindings, not arbitrary C++ class bindings.

This example requires a C# entry point with `[STAThread]`:

```csharp
using Xui;

internal static class Program
{
    [STAThread]
    private static void Main()
    {
        using var window = new Window("Managed XUI", 480, 240);
        var root = window.Stack();
        root.Padding(20);
        root.Spacing(12);
        var label = window.Label("Ready 😀");
        var button = window.Button("Apply");
        button.Click += () => label.Text = "Applied";
        root.Add(label);
        root.Add(button);
        window.SetContent(root);
        window.Run();
    }
}
```

`Window.Update` accepts a span of `Property` records.
`Button.Click`, `Toggle.Changed`, `TextInput.Changed`, `TextInput.Submitted`, and `Window.Key` provide managed events.
`Control.Event` supplies the lower-level event record, including FileList events.
The managed assembly is `Xui.Managed.dll`. Its name avoids a Windows loader collision with the native `xui.dll`.

### Rust ownership and use

The raw `xui-sys` functions remain unsafe. The `xui` crate exposes owned strings and checked operations.
Private raw handles prevent callers from constructing a dangling safe wrapper.
`Window` and every element contain `Rc` ownership. They are neither `Send` nor `Sync`.
Compile-fail doctests enforce these restrictions.
The last owner destroys the native arena on its UI thread.
Elements can outlive the Rust `Window` variable because they retain the same arena.

Each trampoline retains its callback slot before foreign code runs.
Unsubscribe and subscription replacement revoke native use before old slot release.
The callback closure uses checked `RefCell` access.
`catch_unwind` contains panics, records the diagnostic, and returns failure to native dispatch.
The release profile uses `panic = "unwind"` so callback containment remains active.
Abort-mode panics, process aborts, and allocation aborts are not recoverable callback errors.

Use weak captures for callbacks that refer to their own window or controls.
A strong `Rc` capture can form an ordinary Rust ownership cycle.
`downgrade` and `upgrade` provide the cycle-free path. The sample uses that path for every retained control capture.
The native renderer never calls a Rust painter or borrows a Rust item span after a call.

```rust
use xui::{Axis, Result, Window};

fn main() -> Result<()> {
    let window = Window::new("Rust XUI", 480., 240.)?;
    let root = window.stack(Axis::Vertical)?;
    let label = window.label("Ready 😀")?;
    let button = window.button("Apply")?;
    let weak = label.downgrade();
    button.on_event(move |_| {
        if let Some(label) = weak.upgrade() {
            label.set_text("Applied")?;
        }
        Ok(())
    })?;
    root.add(&label, 0.)?;
    root.add(&button, 0.)?;
    window.set_content(&root)?;
    window.run()
}
```
