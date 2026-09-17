# C ABI, C#, and Rust bindings

Build and test commands are in [CONTRIBUTING](../../CONTRIBUTING.md).
See the [reference index](README.md) for related APIs.
Examples in this reference use C++ unless stated otherwise.

## Feature bindings (1.1 extension)

### Scoped content replacement

C++ and C# support one replaceable root inside a stable `ContentHost`.
The C declarations are in `include\xui\xui_content.h`.
Rust does not yet expose a typed wrapper for this extension.
The host uses ordinary retained controls and native input, not another renderer or an embedded top-level window.

```csharp
var host = window.CreateContentHost();
window.SetContent(window.Stack().Add(host, 1));
window.Post(() =>
{
    var update = host.BeginUpdate();
    try
    {
        var root = window.Stack().Add(window.Label("New content"));
        update.Commit(root);
    }
    catch
    {
        update.Dispose();
        throw;
    }
});
window.Run();
```

The host must belong to the window tree before a live replacement.
`BeginUpdate` permits candidate construction on that window's UI thread.
Only one candidate can exist per window.
Construction and commit or rollback must finish within the same UI-thread action.
Candidate construction cannot change unrelated topology or replace window-wide callback handlers.
An unattached root from that candidate is the only valid commit target.
Scopes cannot share control handles or resource handles.
Ordinary tree construction remains a before-run operation.

`Commit` replaces the host content and completes native layout before returning.
It retires the previous scope, including unattached objects that scope created.
`Dispose` rolls back an uncommitted candidate.
After commit, `Dispose` clears that scope if it remains current.
`ContentHost.Clear` explicitly removes current content.
Disposal of an already retired managed scope is harmless.
Retired native handles are invalid.

Replacement must occur outside native input callbacks.
`Window.Post` provides a deferred UI-thread action for this purpose.
The host preserves native peers outside its content.
This includes editor selection, undo history, and focus outside the replaced subtree.
Replacement does not take foreground activation.

`ContentUpdate.CallbackFailed` opts into scoped managed event-error reporting.
The scope stops further managed callbacks after the first exception.
The error handler runs later on the UI thread and must clear or replace the failed content.
Without this handler, callback exceptions keep the fatal-window contract.
The scoped path covers control events, collection menus, file callbacks, Miller callbacks, and managed posted actions.
Immutable-source queries and native failures retain the fatal-window contract.
They cannot return fabricated data as an error substitute.

Posted actions inherit the managed scope through the execution context.
`ContentUpdate.Post` explicitly queues work that belongs to an active scope.
A retired or failed scope rejects later posts.
Retirement releases queued action delegates before native delivery.
Each scope accepts at most 256 pending managed posts.
Authored tasks that suppress execution-context flow remain the application's responsibility.

Model and construction errors preserve the previous content.
A fatal native materialization error can close the window instead.
Scope rollback does not reverse arbitrary authored side effects.
Scopes are ownership boundaries, not sandboxes or process isolation.

The C++ counterpart is `Window::replace_content(ContentHost&, std::shared_ptr<Element>)`.
A null content argument clears the host.
C++ callers retain responsibility for their own callback captures and resources.
The C ABI adds begin, commit, release, clear, context, owner, and handle-count operations.
The context operation attributes new resources to a scoped callback without enabling topology changes.

### Windows file transfers

The C# library supports filesystem clipboard transfers and native OLE drag-and-drop without Windows Forms or WPF.
The declarations are in `include\xui\xui_file_transfer.h`, included by `xui.h`.
The .NET declarations are handwritten in `Native.FileTransfers.cs`. The generated feature files remain unchanged.
The [collection contract](collections.md#file-drag-and-drop) describes grid selection and target behavior.

```csharp
[Flags]
public enum FileTransferEffect { None = 0, Copy = 1, Move = 2 }
public sealed record FileClipboardContent(string[] Paths, FileTransferEffect Effect);

window.SetFileClipboard(paths, FileTransferEffect.Copy); // Move means cut.
FileClipboardContent? clipboard = window.GetFileClipboard();
window.SetClipboardText("A path or other text");
bool complete = window.TransferFiles(paths, destination, FileTransferEffect.Move);
bool? pasted = window.PasteFiles(destination);
```

All methods require the creating UI thread. They work before or during `Run`, but not after closure.
`SetFileClipboard` accepts Copy or Move, not a combination.
Cut places paths on the clipboard without deleting files.
The clipboard contains Unicode `CF_HDROP` and `Preferred DropEffect`.
XUI flushes its OLE clipboard data, so the paths remain available after application exit.
Clipboard calls retry temporary contention for at most two seconds per call, then report an explicit error.
`SetClipboardText` supplies `CF_UNICODETEXT` and does not intercept editor keyboard input.
The C++ counterpart is `set_clipboard_text`. The existing `copy_text` method still requires a running window.

`GetFileClipboard` returns null for a clipboard without filesystem paths.
Its result is a snapshot, not ownership of the clipboard.
`PasteFiles` retains the original `IDataObject` throughout the transfer.
It returns null for no file clipboard, true for complete work, or false for cancellation or skipped work.
After success, it sends the Shell completion formats to that original object.
Foreign sources can reject optional notification formats. XUI never requests source-side deletion.
It clears a completed cut only when the clipboard sequence still matches, with the comparison inside the clipboard lock.

`TransferFiles` uses Windows `IFileOperation` with normal conflict, progress, and elevation UI.
It returns true only when every requested item completes without reported cancellation, skipped work, or errors.
Native errors throw `XuiException`. Cancellation and skipped work return false.
The operation runs synchronously in the UI STA. Shell dialogs can dispatch nested window messages.
XUI rejects another transfer on that thread until the current transfer returns.
Closure requests cancel remaining work. Earlier completed copies or moves are not rolled back.

CAUTION: Do not delete source files in completion callbacks or after a false result.
The Shell owns each copy or move, including source deletion.
After a partial result, refresh both locations before another attempt.
`GetFileClipboard` plus `TransferFiles` does not supply clipboard completion notifications. Use `PasteFiles` for clipboard paste.

Each transfer accepts 1 to 4,096 absolute filesystem paths.
Each path has a limit of 32,767 UTF-16 units. The complete `CF_HDROP` payload has a 16 MiB limit.
Each ABI UTF-8 span also has the existing 1 MiB limit.
An empty drag snapshot cancels the drag. Relative paths, embedded NULs, and link effects are not supported.
The clipboard reader accepts Unicode and ANSI `CF_HDROP`, but not virtual files in `FILECONTENTS`.

The ABI uses borrowed path spans in `xui_file_receiver` and `xui_file_drop_handler`.
Callback consumers must copy paths before the callback returns.
The clipboard receiver receives zero paths for no file clipboard.
`xui_window_transfer_files` writes 1 for complete or 0 for cancellation or skipped work.
`xui_window_paste_files` writes 0 for no files, 1 for complete, or 2 for cancellation or skipped work.
The standard status result remains separate and reports errors.
Callback exceptions use the existing callback-error contract and close the owning window.

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
The extension adds typed constructors to both C# and Rust, including `NavigationView` and `MillerColumns`.
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
| Navigation | `NavigationView`, `Breadcrumb`, `NavigationPane`, `LocationPicker`, `ViewPicker` | Path segments, immutable sources, typed borrowed children, presentation and size controls |
| Documents | `MultilineText`, `RichText`, `PasswordInput` | Native editing, authored runs, selection, editing commands, read-only mode, scoped password access |
| Forms | `DateTimePicker`, `ColorPicker`, `InlineStatus`, `ContentDialog` | Local Gregorian fields, RGBA values, status dismissal, arbitrary dialog content, validation message, result events |
| Scenes and maps | `VectorCanvas`, `MapView` | Immutable shapes, transforms, clips, stable IDs, offline markers, view state, cancelable overlay tokens |
| Native hosts | `MediaPlayback`, `WebContent` | Explicit local media load, volume, seek, playback actions, allowed origins, HTML, JavaScript result tokens, stop and unload |
| Window integration | `Window` | Optional custom title bar and explicit native Shell menu fallback |

`include\xui\xui_features.h` declares the extension through `xui.h`.
`bindings\generate_features.py` generates both FFI declarations from that header.
It generates typed constructors and scalar properties from `bindings\features.json`.
The handwritten feature modules implement collections, scoped secrets, request ownership, and typed records.
The handwritten C# `TreeView.Select(ItemKey)` method uses the native collection selection action.
It retains the source identity, version checks, and selection callback behavior.
C and C# also support [undo-preserving plain document edits](documents.md#undo-preserving-range-replacement).
That additive API uses a separate header and handwritten managed imports.

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
The ABI constructor tests cover the feature kinds.
Both language test suites exercise typed properties, source limits, callback failures, and disposal.
The tests preserve the existing native focus and accessibility assertions.

<a id="generic-control-styles-toggle-pilot"></a>

### Generic control styles

`xui_control_style_create` accepts a bounded array of `xui_style_property` records.
Each record identifies a part, a property, a value type, and a 64-bit state.
State zero specifies an ordinary value.
Other records require one supported state bit.
The record contains separate color, insets, number, and text carriers.
Unused carriers and reserved fields must contain zero.
The text carrier supplies immutable font-family names, not user input.
Each schema accepts only text properties that its adapter supports.
Colors retain separate light and dark RGB24 values.
Dimensions require finite values from zero through 32768 DIPs.
Font sizes and row heights must also be positive.
Per-part limits can further restrict font size, UTF-16 family length, font style, and alignment.
Native paragraph parts reject vertical stretch, while supported container layout retains it.

`xui_control_style_get_schema` exposes base/local properties, states, and the separate state-rule property mask.
`xui_control_style_get_limits` exposes the per-part font and alignment limits.
These queries require no window.
The C#, Rust, and compiler catalogs use the same exported contracts.

Records contain exact size and version fields.
A definition accepts at most 2048 property records and 16 inheritance layers.
Duplicate `(part, state, property)` records produce errors.
Definitions validate the target and its part/property schema before publication.
Invalid input preserves the previous style and local values.
Existing Button records and exports remain unchanged.

`xui_control_set_style` applies a live handle from the same window.
Zero clears the style but preserves locals.
`xui_control_set_style_values` replaces all local values for one part.
An empty span clears that part.
Every local record must name that part and state zero.
`xui_control_get_style_values` reads locals or effective values before platform defaults and high contrast.
A zero-capacity query returns the required record count.
An insufficient buffer returns `XUI_BUFFER_TOO_SMALL` without partial record output.

`xui_control_style_release` releases the caller's handle.
Applied controls retain the immutable definition.
The create result also acts as a weak per-window identity.
`xui_control_style_reacquire` returns a fresh live handle, or zero after the definition expires.
`xui_control_try_set_style` applies a retained identity without a new handle.
Unknown and expired identities produce cache misses.
All mutation and thread guards also apply to cache hits.
Wrong control targets and incompatible handle kinds produce errors.
There is no ABI setter for backend disabled context.

C# exposes `ControlStyle`, `PartStyleValues`, `PartStyle`, and `ControlStyleRule`.
Rust exposes the same types with native Rust member names.
Both wrappers keep weak per-window identities and release temporary native handles after application.
Definition construction copies the supplied part and rule collections.
The [Toggle examples](control-styling.md#toggle-pilot) show declaration and application.

### Retained composition children

ContentDialog, CommandSurface, LocationPicker, and ViewPicker handles already refer to their real Popup for generic Element styling.
Their root accepts `StyleTarget.Popup`, not a facade-specific style target.
Only `open` and inherited `disabled` states exist on these roots.
Root `invalid`, `loading`, `error`, `selected`, and `overflowed` rules reject.
Dialog validation uses the retained InlineStatus's error state; LocationPicker navigation uses its NavigationPane's actual query states.
CommandSurface status Labels do not expose automatic query-state selectors.
Child accessors return existing Elements with their own native targets and attachments.
Repeated lookup preserves the native handle, and C# properties also retain the same wrapper.
Children belong to the parent Window and do not have independent destruction.

C# exposes these retained children:

- `Window`: `Titlebar`, `TitlebarTitle`, `TitlebarMinimize`, `TitlebarMaximize`, `TitlebarClose`, `TitlebarTabs`, `TitlebarLeading`, and `TitlebarSecondaryTabs`.
- `NavigationView`: `Search`, `ToggleButton`, `Items`, `HeaderItems`, `FooterItems`, `Title`, and `EmptyMessage`.
- `Breadcrumb`: `OverflowButton` and `SegmentButton(ItemKey)`.
- `CommandBar`: `OverflowButton` and `CommandButton(id)`.
- `ContentDialog`: `Primary`, `CancelButton`, `Title`, `Validation`, `Body`, and `Footer`.
- `CommandSurface`: `Editor`, `Title`, `Status`, `CloseButton`, `Content`, `Results`, and `Menu`.
- `LocationPicker`: `Editor`, `Navigation`, `Content`, `Footer`, and `Toolbar`.
- `ViewPicker`: `Choices`, `Size`, and `Content`.
- `NavigationPane`: `Items`, `Status`, `Content`, `Group`, and `Progress`.
- `ComboBox`: optional `Editor`, `Popup`, and `Choices`.
- `NumericInput`: `Editor`, `DecreaseButton`, and `IncreaseButton`.
- `InlineStatus`: `ActionButton` and `DismissButton`.
- `ColorPicker`: `Channel(index)` and `SwatchButton(index)`.

Rust exposes the corresponding methods with snake-case names.
`CommandSurface.Menu` returns a C# `RetainedElement`; Rust `menu()` returns an `Element`.
The native object remains a CommandMenu, not an ItemsView.
This accessor exposes ordinary Element styling and layout, not a new CommandMenu factory or typed collection API.

Window titlebar access requires a custom titlebar.
`Window.Titlebar` and the NavigationView lists also use C# `RetainedElement` and Rust `Element` wrappers.
Their native style targets remain `TitleBar` and `NavigationList`.
The wrappers do not cast these objects to ContentView or TreeView.
NavigationPane exposes its existing Expander as `Group` and its query indicator as `Progress`.

Breadcrumb segment lookup uses both the item ID and version.
CommandBar lookup uses the command ID, not its current position.
Missing keys reject explicitly.
The ABI provides `xui_breadcrumb_segment_button` and `xui_command_bar_button` for these lookups.
CommandBar snapshot refresh preserves retained Button identity and subscriptions.
Removed command and segment Buttons become non-actionable, even when an application retains their handles.
Reintroduced keys use the current child, not a detached Button's obsolete action.

The noneditable ComboBox editor returns C# `null`, Rust `None`, or ABI success with handle zero.
Its `Choices` object uses the `ChoiceList` style target, although its typed wrapper is `RadioGroup`.
ColorPicker channels use red, green, blue, and alpha indices 0 through 3.
Swatch access rejects indices outside the current palette.
Swatch fills remain authored RGBA data, not child theme substitutions.
Applications can style the retained swatch Button border and radius.
Child accessors do not change action visibility or expose an unsupported ColorPicker `channel_field` part.

The ABI extends `xui_feature_child` without changing existing indices.
The public header lists each index and identifies the retained-only `XUI_RETAINED_ELEMENT` kind.
Button and TextInput event subscriptions preserve the composition's original action, change, and submit callbacks.
NumericInput, RadioGroup, RangeInput, and Popup subscriptions also preserve their original callbacks.
Repeated subscriptions do not wrap the binding callbacks recursively.
Style replacement and clearing preserve child-local values.

The `.xui` compiler rejects facade-specific style declarations.
`Content(existingElement, style: PopupStyle)` can apply a Popup definition to an existing Popup-backed facade.
The native target check still rejects a definition for the wrong child.
Complete schema-based authoring requires the generated catalog that matches the native library.

### Button styles (additive stage 1)

Button styles change presentation.
Native behavior, input, and accessibility stay unchanged.
This compatible `ButtonStyle` API supports `Button` only.
Typography and named parts use the [generic control-style API](#generic-control-styles).
Neither API provides control templates, item templates, animations, or arbitrary brushes.
`Window.Style` selects Classic or WinUI presentation.
`Button.Style` supplies an application-authored definition on that presentation.

`include\xui\styling.hpp` declares the C++ definitions.
`ButtonStyle::create` copies values and rules into an immutable definition.
`Button::set_style` attaches a shared definition.
`Button::set_style_values` replaces local overrides.
`Button::style_values` and `Button::effective_style_values` expose local and merged values.
The C++ effective getter returns a pointer that can be null for an unstyled button.

#### Values and precedence

`ThemeColor` contains opaque `0xRRGGBB` colors in light/dark order.
`Insets` contains left, top, right, and bottom dimensions in DIPs.
`ButtonStyleValues` contains optional background, foreground, border brush, border thickness, padding, and corner radius values.
An absent value differs from an explicit black color or zero dimension.
Dimensions must be finite and within 0 through 32,768 DIPs.

Each definition contains at most 256 rules.
Inheritance contains at most 16 layers, including the definition itself.
Rules use the states `Focused`, `Checked`, `Hovered`, `Pressed`, and `Disabled`.
The C++ enum uses the corresponding lowercase names.
Native definitions precompute all 32 state combinations.

Resolution starts with inherited base values and then applies derived base values.
For each state, derived rules overlay inherited rules.
Later rules for the same state replace only the fields they specify.
Active states then overlay base values in this order: focused, checked, hovered, pressed, disabled.
Local overrides apply last.
Missing fields use the remaining sources rather than clearing them.

Clearing the shared style preserves local overrides.
An empty local record clears all local overrides without clearing the shared style.
Effective getters return merged authored values, before platform defaults and high-contrast protection.
They do not return final pixel colors or geometry.
High contrast preserves native face and focus treatment instead of authored face and foreground overrides.

#### Native ABI

The additive exports are in `include\xui\xui_features.h`, through `xui.h`.
Existing records, exports, and version negotiation remain unchanged.
`XUI_BUTTON_STYLE_VERSION` is `0x00010000`.
The style record version is separate from `XUI_FEATURE_VERSION`.
Consumers require a DLL that exports these functions.
The unchanged ABI version alone does not establish style support.

| C record | Fields, in declaration order | ARM64 size |
| --- | --- | ---: |
| `xui_theme_color` | `uint32_t light, dark` | 8 |
| `xui_style_insets` | `float left, top, right, bottom` | 16 |
| `xui_button_style_values` | `uint32_t size, version, mask, reserved`, three colors, two insets, `float corner_radius`, `uint32_t reserved_end` | 80 |
| `xui_button_style_rule` | `uint32_t size, state`, `xui_button_style_values values` | 88 |
| `xui_button_style_options` | `uint32_t size, version`, `values`, `rules` pointer, `uint32_t rule_count, reserved`, `xui_handle based_on` | 112 |

The three color fields are `background`, `foreground`, and `border_brush`.
The two inset fields are `border_thickness` and `padding`.
In the options record, `rules` starts at offset 88 and `based_on` starts at offset 104.
The generated C# and `xui-sys` records preserve this layout.

The values mask uses bits 1, 2, 4, 8, 16, and 32 for the six properties in their listed order.
Rule states use ordinal values 0 through 4, not masks.
Each input record requires its exact size.
Options and values require the style version.
Reserved fields and absent value fields must be zero.
The ABI copies values and rule arrays before return.

| Export | Contract |
| --- | --- |
| `xui_button_style_create(window, options, result)` | Creates a window-owned handle. `based_on` is zero or a live style handle from the same window. Failure clears a valid result pointer. |
| `xui_button_style_release(style)` | Revokes the caller's style handle. Buttons and derived definitions remain valid. |
| `xui_button_style_reacquire(window, identity, result)` | Returns a temporary handle to a retained definition, or zero for a cache miss. |
| `xui_button_try_set_style(button, identity, applied)` | Applies a retained definition without a new handle. Returns `applied=1` on a hit, including an unchanged assignment. |
| `xui_button_set_style(button, style)` | Attaches a same-window definition. Zero clears the shared style. |
| `xui_button_set_style_values(button, values)` | Replaces the complete local record. An empty mask clears local overrides. |
| `xui_button_get_style_values(button, effective, values)` | Returns locals for `effective=0` or merged authored values for `effective=1`. The output record requires size and version. |

All style exports require the creating UI thread.
Style handles count toward the window limit of 65,536 live handles.
Window destruction revokes every remaining style handle.
Explicit release requires `xui_button_style_release`, not a control or source release function.
Release does not clear a style from a button.
Replacement or clearing releases the definition retained by that button.

A creation result also identifies the exact immutable native definition after handle release.
This weak identity is not a live handle and requires no release.
Unknown, zero, expired, or other-window identities produce a successful cache miss in the two cache helpers.
A miss preserves the button.
Reacquired handles require release and preserve the original identity.
Neither helper creates a new definition.

Invalid sizes, masks, reserved fields, values, states, limits, selectors, or cross-window style handles return `XUI_INVALID_ARGUMENT`.
Invalid record versions return `XUI_VERSION_MISMATCH`.
Stale handles return `XUI_INVALID_HANDLE`.
A handle of the wrong kind returns `XUI_WRONG_KIND`.
Wrong-thread calls return `XUI_WRONG_THREAD`.
Source-callback mutations return `XUI_BUSY`, and mutations after the window run returns report `XUI_CLOSED`.

Creation and setters leave the previous button presentation unchanged on failure.
Native allocation failures use the existing status and diagnostic contract.
`xui_error_copy` supplies the diagnostic.
Successful release clears the thread-local diagnostic, like other successful exports.
Callers must capture an earlier error before cleanup.

#### C# and Rust ownership

C# exposes `ThemeColor`, `Insets`, `ButtonStyleValues`, `ButtonStyleState`, `ButtonStyleRule`, `ButtonStyle`, and `ResourceScope`.
`Button.Style` and `SetStyle` attach or clear a definition.
`StyleValues` and `SetStyleValues` replace locals.
`EffectiveStyleValues` reads merged authored values.
`Button.Style` returns the last successfully assigned managed definition.

Rust exposes corresponding safe types and `ColorResource`.
`ButtonStyle::new(values, rules, based_on)` creates an immutable definition.
`values()`, `rules()`, and `based_on()` expose its contents.
`Button::set_style`, `set_style_values`, `style_values`, and `effective_style_values` provide the native operations.
`None` clears a shared style.
`ButtonStyleValues::default()` clears local overrides.

Both wrappers can reuse one language definition across windows.
The wrappers require both cache helper exports from the loaded native DLL.
Within one window, buttons share the exact native definition for the same C# object or Rust `ButtonStyle` and its clones.
Equal values in separate language definitions do not imply shared identity.
Sharing persists while any native button or temporary handle retains that definition, even after control wrappers disappear.
Assignment of the same definition preserves the native effective-value cache without allocation, value resolution, or invalidation.
Each assignment still checks native thread, window, and callback restrictions.

The first application creates temporary native handles in the target window.
Both wrappers release every temporary handle before return, including failure paths.
There is no persistent per-window handle cache.
One style application requires at most 16 temporary style handles.
A later application reuses a retained definition without temporary handles.
An inheritance operation can reacquire a retained base through a temporary handle.
Native derived definitions contain copied inherited values and do not retain the base definition.
After the last native owner releases a definition, a later application rebuilds it.

The native cache contains weak references and uses the window's limit of 65,536 live handles as its bound.
Each retained definition requires at least one live button or style handle.
Release, replacement, and clearing remove expired entries synchronously.
The last entry's removal releases the cache storage.
Unstyled windows allocate no native cache, and paint does not consult this cache.
The wrappers store identities with weak window references, not strong references to windows or controls.

The native button retains its applied definition after temporary handle release.
C# retains the language definition through `Button.Style` until replacement or clearing.
Rust can drop its language definition immediately after successful application.
Neither language definition retains a window.
Garbage collection and Rust destructors do not need to call the native style-release export.
Clearing and replacement perform native cleanup synchronously on the UI thread.

C# validates definitions with argument exceptions.
Native failures become `XuiException` with their status and diagnostic.
Disposed-window access throws `ObjectDisposedException`.
Rust returns `Result` errors and retains the window through each control owner.
Dropping the last Rust window/control owner destroys the native window.
Rust controls remain neither `Send` nor `Sync`.

#### Immutable color resources

Resource scopes resolve colors before style application.
They do not create native resource handles or dynamic resource subscriptions.
Each scope contains at most 256 entries.
The parent chain contains at most 16 layers, including the scope itself.
Aliases resolve local names first, then parent names.
Missing names, duplicate entries, cycles, and invalid colors fail explicitly.

Construction copies entries, so later input changes do not change the scope.

C# entries contain `ThemeColor` or string aliases.
Rust entries contain `ColorResource::Color` or `ColorResource::Alias`.
Binding names require 1 through 1,024 characters without NUL.
C# counts UTF-16 code units, while Rust counts Unicode scalar values.
C++ resource definitions separately limit entry names to 256 bytes.
No resource-scope structure or creation function exists in the C ABI.

#### Binding examples

```csharp
using var window = new Xui.Window();
var style = new Xui.ButtonStyle(
    new() { Background = new Xui.ThemeColor(0xcc2222, 0x992222), Padding = new Xui.Insets(8) },
    [new(Xui.ButtonStyleState.Disabled, new() { Foreground = new Xui.ThemeColor(0x888888) })]);
var button = window.Button("Apply").SetStyle(style);
button.StyleValues = new() { CornerRadius = 0 };
button.Style = null;                 // Preserves the local corner radius.
button.StyleValues = new();          // Restores platform defaults.
```

```rust
let window = xui::Window::new("Styles", 400., 200.)?;
let style = xui::ButtonStyle::new(
    xui::ButtonStyleValues {
        background: Some(xui::ThemeColor::new(0xcc2222, 0x992222)),
        padding: Some(xui::Insets::uniform(8.)),
        ..Default::default()
    },
    &[],
    None,
)?;
let button = window.button("Apply")?;
button.set_style(Some(&style))?;
drop(style);                         // The native button retains its definition.
button.set_style(None)?;
button.set_style_values(xui::ButtonStyleValues::default())?;
```

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
