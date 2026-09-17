# Application composition and lifecycle

Build and test commands are in [CONTRIBUTING](../../CONTRIBUTING.md).
See the [reference index](README.md) for related APIs.
Examples in this reference use C++ unless stated otherwise.
For C# and Rust coverage, use the [binding reference](bindings.md).

## Reusable controls

`include\xui\application.hpp` provides `Window` and `Application::run`.
Applications describe a control tree and callbacks. They do not supply a window procedure or drawing callbacks.
`demo\gallery.cpp` and `demo\browser.cpp` contain the application compositions.

| API | Purpose |
| --- | --- |
| `Stack` | Layout, padding, spacing, flex space, an optional surface, and a separator |
| `Label` | Text, heading or caption appearance, semantic color, and an accessible name |
| `Button` | An enabled command with an `on_click` callback and optional vector icon |
| `Toggle` | A checkbox with `checked`, `set_checked`, and an `on_change` callback |
| `TextInput` | Native EDIT, committed-text and submit callbacks, optional asynchronous suggestions, search appearance, placeholder, and shortcut hint |
| `ScrollView` | Retained content, a vertical viewport, a scrollbar, focus reveal, and UIA scroll actions |
| `ContentView` | A clipped retained subtree with a native parent for child controls |
| `TabStrip` | Dynamic tab data, stable IDs, selection, close callbacks, an optional new-tab button, overflow reveal, and UIA tab patterns |
| `SplitView` | Two content hosts, a draggable divider, keyboard resizing, minimum widths, and narrow-window collapse |
| `FileList` | Immutable views, stable selection and item focus, navigation, viewport, empty text, and change callbacks |
| `DataGrid` | Immutable row sources, stable keys, shared multi-selection, header filters, selection check columns, sorting, resize, reorder, and two-axis scrolling |
| `HistoryChart` | A fixed 60-sample history, explicit gaps, a numeric scale, and an accessible metric name |
| `PageView` | Retained pages with one visible content host and no page-selection I/O |
| `NavigationView` | Nested items, native search, pinned header/footer shortcuts, shared selection, icons, badges, and expanded or collapsed panes. C# exposes nested entries, search, and expansion. |
| `ViewTask` | Cancellable source and filter work, latest-generation delivery, and progress counters |
| `SampleTask` | One background worker, a bounded result slot, periodic or manual requests, pause, and UI-thread delivery |
| `Image` | Asynchronous WIC file decoding, bounded pixels, shared bitmaps, explicit unload, and accessible image names |
| `ImageResources` | Process-wide image limits, ownership counters, latency counters, and unused-cache eviction |
| `ContentDialog` | Client-bound modal content, validation, default/cancel actions, and focus return |
| `InlineStatus` | Severity, an independent action, dismissal, and UIA live announcements |
| `MultilineText` | Bounded native RichEdit plain text, selection, clipboard commands, undo, and read-only mode |
| `PasswordInput` | Masked native EDIT, explicit secret access, and an optional reveal preview |
| `RichText` | Native RichEdit styled runs, explicit links, selection, and bounded editing |
| `DateTimePicker` | Native date, time, and calendar presentations with validated Gregorian bounds |
| `ColorPicker` | Retained sRGB byte channels, alpha preview, swatches, and numeric keyboard access |
| `VectorCanvas` | Immutable paths, shapes, transforms, clipping, hit testing, and a virtual accessible element list |
| `MapView` | Offline Mercator coordinates, graticule, authored overlays, stable markers, pan, zoom, and cancelable provider requests |
| `MediaPlayback` | Explicit local audio/video through lazy Windows Media Foundation, with playback, seek, volume, and unload |
| `WebContent` | Optional WebView2, owned HTML, explicit HTTPS origins, script results, and native browser accessibility |
| `Window` | Content ownership, title, size, focus, themes, key callbacks, clipboard text, tasks, closure, and error text |

`Control::set_automation_id` supplies an optional application identity for custom controls.
Without an override, custom controls use their stable element IDs.
`Control::on_context_menu` returns themed native menu items with actions, enabled state, checked state, and separators.
Native EDIT retains its system menu unless the control supplies a context-menu callback. Its native text provider does not change.
Nested native EDIT controls expose the public automation ID and name through their geometry override.
`Control::on_focus` reports a focus transition without a replacement window procedure.

### Application example

For example, this application updates a label through a button callback:

```cpp
#include "xui\application.hpp"
#include <windows.h>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    xui::Window window({L"My application", {480, 240}});
    auto content = std::make_shared<xui::Stack>(xui::Axis::vertical);
    content->set_padding({24, 24, 24, 24});
    content->set_spacing(12);
    auto label = std::make_shared<xui::Label>(L"Ready");
    auto button = std::make_shared<xui::Button>(L"Update label");
    button->on_click([&] { label->set_text(L"Updated"); });
    content->add(label);
    content->add(button);
    window.set_content(content);
    return xui::Application::run(window);
}
```

Link the executable to `xui_windows`.
Include the manifest resource from `demo\xui.rc`, or supply an equivalent common-controls v6 and per-monitor-DPI manifest.
For MSVC builds that use this resource, set `/MANIFEST:NO`, as the gallery target does.

### Ownership and property updates

The window retains its content through `std::shared_ptr`. A control has one layout parent and a stable `Element::id`.
Build the tree before `Application::run`. Set properties and use callbacks on that same UI thread.
The legacy `Application::run(window)` supports one active run on the thread.
Each legacy `Window` runs once, but separate windows can run in sequence.
`ContentHost` provides an explicit exception for a single replaceable root.
`Window::replace_content` changes only that host and preserves surrounding native peers.
It requires the UI thread and rejects replacement from active native input callbacks.
The [binding contract](bindings.md#scoped-content-replacement) describes candidate scopes and managed callback ownership.
The caller must not initialize COM as MTA.

Windows take activation and initial keyboard focus by default.
Before `Run`, `Window::set_show_activated(false)` shows a window without taking either.
`WindowOptions::show_activated` supplies the same initial choice in C++.
The C ABI uses `xui_window_show_activated(window, 0)` from `xui_layout.h`.
C# uses `Window.SetShowActivated(false)`.
Later user activation remains available.
Changes during or after `Run` fail.

Callbacks must not outlive the objects that they reference.
Reference captures in the example remain valid during the blocking `Application::run` call.
Strong captures that refer back to their own control can create an ownership cycle.
Window teardown clears the host invalidator and disconnects accessible providers.
Retained controls remain valid after window destruction.

Enabled state and checked state request paint updates.
Text and typography changes request layout for automatic sizes, or paint for preferred sizes.
Size limits, spacing, and padding request layout and paint updates.
The host combines pending updates. It has no animation timer or continuous render loop.
Text stays on one line unless the text contains an explicit line break.
An ellipsis marks text that exceeds the available width. The accessible name retains the full text.

Standard-control property setters do not call application action callbacks.
List selection operations call `on_selection_change`. View assignments call `on_view_change`.
Both callbacks observe the updated model. Item focus does not invoke the selection callback.
Pointer, keyboard, and UIA actions use the same activation behavior and enabled-state check.
Native committed text calls `TextInput::on_change`. IME preedit text does not call this callback.
Text input defaults to 1,024 UTF-16 units on Windows.
`TextInput::set_maximum_length` selects a limit from 1 to 32,767 units. Explorer address fields use 32,767 units.
The property setter preserves surrogate pairs at the limit and stops at the first NUL.
Callbacks can update other controls, change the theme, or request window closure.
`Window::focus(control, select_all)` requests native focus. The optional selection flag applies to text inputs.
Focus requests reject disabled controls, foreign controls, and closed windows.
Rejected select-all requests leave native selection and focus unchanged.
`Window::copy_text` requires an open window. Calls before startup or after closure throw before they open the clipboard.
The input-state methods on `Control` are backend boundaries, not application focus commands.
Startup errors and callback exceptions return a nonzero result. `Window::error()` supplies the error text.

### Window icons

`Window::set_file_type_icon(extension, directory)` sets the small and large native HWND icons.
The C# method is `Window.SetFileTypeIcon`. Rust uses `Window::set_file_type_icon`.
The C ABI exports `xui_window_file_type_icon`.
An extension such as `.txt` selects its type-association icon without access to the target file.
An empty extension selects the stock document icon. An empty extension with `directory=true` selects the stock folder icon.
The stock path does not resolve a file association or invoke file providers.

Extensions start with a dot, contain at most 255 UTF-16 units, and cannot contain control characters or path separators.
Directory icons require an empty extension.
The method requires the window's UI thread, before Show or while the window is open.
Invalid arguments preserve the existing icons.
Each window owns its icon handles, replaces their sizes on DPI changes, and releases them on closure.
Custom title bars retain their own visual controls. The HWND icons supply Windows taskbar and window-switching surfaces.

### Independent application windows

An instance of `Application` owns one STA message dispatcher.
`create_window(options)` creates a window associated with that application.
After the caller builds its content, `show(window)` creates a visible ownerless top-level window without a nested message loop.
`run()` services all shown windows until the last one closes and deferred cleanup completes.
The caller keeps each C++ `Window` alive and keeps its callback captures valid after `show` returns.

Every window has its own control tree, native text peers, focus, renderer, tasks, and post queue.
The same retained tree cannot belong to two live native hosts.
Window states are `created`, `open`, `closing`, and `closed`.
Closure before the first show or run also rejects later mutations.
`on_closed` runs once after native destruction and after active window dispatch unwinds.
`Window::post` rejects closed windows and discards pending callbacks on closure.
`Application::post` supports deferred cleanup after a window closes.
Both post methods accept worker-thread calls while their owner remains alive.

Ownerless windows can move independently, participate in normal Shell window switching, and survive closure or minimization of another window.
Window closure does not request application shutdown.
`Application::shutdown()` closes all associated windows and rejects new windows.
An application instance runs once. Nested application runs and legacy runs inside an application context are invalid.
Lifecycle calls and destruction belong to the creating UI thread.
The caller must not initialize that thread as MTA.

A window callback failure closes that window without closing healthy windows.
The application retains the failure and returns a nonzero run result.
`Application::error()` supplies the retained messages.
An application-post callback failure requests application shutdown.
Final native-runtime cleanup occurs after all windows close and before OLE teardown, not after each window.

### Owned native file dialogs

`Window::show_open_file_dialog` and `show_save_file_dialog` show the Windows Shell file dialog.
Both methods take `FileDialogOptions` and return `std::optional<std::wstring>`.
A value contains one absolute filesystem path. An empty result means cancellation, including owner closure.
Native failures throw. These methods do not read or write the selected file.
The Save dialog requests overwrite confirmation but leaves the actual write and conflict policy to the application.

The application supplies filters and extensions. XUI has no application-specific file type.
This example belongs in a callback while the window runs:

```cpp
xui::FileDialogOptions options;
options.title = L"Open component";
options.filters = {{L"Components", L"*.xui"}, {L"All files", L"*.*"}};
options.default_extension = L"xui";
const auto selected = window.show_open_file_dialog(options);
```

For Save, `suggested_name` supplies a leaf filename. An optional `initial_directory` selects an existing absolute drive or UNC directory.
An empty directory leaves the initial location to Windows. A missing or invalid directory produces an error, not a different location.
The first filter is selected. Patterns accept `*`, `*.*`, or semicolon-separated `*.extension` entries, with no spaces.
`default_extension` omits the leading dot and wildcards. A compound extension is permitted.

All option strings require valid UTF-16 without NUL or control characters.
The limits, in UTF-16 units, are 256 for titles, 128 for filter names, and 512 for filter patterns.
Default extensions permit 64 units, suggested names permit 255, and directory and result paths permit 32,767.
There are at most 32 filters. Empty filter names, malformed patterns, device paths, and invalid filename characters produce errors.
`FileDialogOptions::validate` checks the options without opening a window.

The caller must use the window's UI thread during its active run.
The window must be visible and enabled, with no active text composition, popup, file dialog, or content replacement.
Calls before startup, during native synchronization, or after closure produce errors.
The existing COM and manifest requirements apply. The API does not expose a native window handle.

The native dialog owns a modal loop and disables its XUI owner.
Messages and native callbacks can run inside that loop, but ordinary `Window::post` delivery waits until it returns.
Nested file dialogs and content replacement are rejected. Option data is copied before native dispatch.
Owner closure requests native cancellation. C++ owner destruction defers native teardown until the modal call returns.
The method retains its backend, not the public `Window`. Callers must not use a deleted `Window` after the method returns.
Focus returns to the surviving native child only while the owner remains active. XUI does not reactivate another window.
The [binding contract](bindings.md#native-file-dialogs) defines C results, managed disposal, and content-scope restrictions.

### Content sizes and constraints

Labels, buttons, and toggles use automatic content sizes by default.
DirectWrite supplies glyph widths and line heights through the `Control::set_text_measurer` backend boundary.
The core has no Windows dependency and does not estimate glyph widths.
Headless callers can supply a `TextMeasurer`. Without a measurer, controls use their original preferred sizes.

Each native label, button, and toggle retains one text layout.
Text or typography changes replace that layout. Hover, focus, scrolling, and paint reuse it.
DPI changes reuse DIP measurements and update the native input font.
Window teardown removes measurer callbacks before it releases the renderer.

| Method | Sizing rule |
| --- | --- |
| `set_auto_size(true)` | Use content size for labels, buttons, and toggles |
| `set_preferred_size({width, height})` | Disable automatic size and retain the original Stack allocation rules |
| `set_fixed_size({width, height})` | Set the preferred size and both limits to the supplied size |
| `set_minimum_size({width, height})` | Set the minimum desired size |
| `set_maximum_size({width, height})` | Limit both the desired size and the arranged size |

Parent constraints take precedence over minimum and fixed sizes. No child can force an overflow from a smaller allocation.
A new minimum raises a smaller maximum. A new maximum lowers a larger minimum.
`set_auto_size` does not clear limits. Zero minimum and infinite maximum values remove the limits.
`TextInput`, `FileList`, and `ScrollView` retain their preferred viewport sizes.

A Stack measures non-flex children first, in order, against the remaining main-axis space.
An explicit Stack preferred size replaces natural measurement unless `set_auto_size(true)` overrides it.
That preferred size includes padding and remains subject to parent bounds and size limits.
Flex children share the remaining main-axis space. On the cross axis, children stretch up to their maximum size.
Maximum limits do not redistribute unused flex space.
In an unbounded main axis, flex children use their natural desired size instead of an infinite share.
A ScrollView gives its content a bounded width and an unbounded height.
This model has no CSS cascade, automatic wrapping layout, or reactive property graph.

This composition gives the button its content width and the form the remaining height:

```cpp
auto root = std::make_shared<xui::Stack>(xui::Axis::vertical);
auto form = std::make_shared<xui::Stack>(xui::Axis::vertical);
form->set_spacing(12);
form->add(std::make_shared<xui::Label>(L"Workspace"));
form->add(std::make_shared<xui::TextInput>(L"Workspace name"));
auto actions = std::make_shared<xui::Stack>(xui::Axis::horizontal);
actions->add(std::make_shared<xui::Button>(L"Apply preferences"));
form->add(actions);
auto viewport = std::make_shared<xui::ScrollView>(form, L"Workspace preferences");
root->add(viewport, 1);
window.set_content(root);
```

### Retained scrolling

`ScrollView` accepts one retained `Element`, including a nested Stack.
Its content has one layout parent. Null content, duplicate ownership, and cycles are invalid.
`offset`, `extent`, and `maximum_offset` use DIPs.
`set_offset`, `scroll_by`, and `reveal` clamp the offset to the content range.
The viewport reserves 12 DIPs for its vertical scrollbar. A larger viewport clamps obsolete offsets automatically.

The mouse wheel follows Windows wheel settings. The scrollbar supports thumb drag and track paging.
With viewport focus, arrow keys, Home, End, Page Up, and Page Down move the viewport.
Page keys also work from ordinary descendants. Native input retains its text-editing keys and IME path.
Tab, Shift+Tab, `Window::focus`, and UIA focus reveal the target through ancestor viewports.
A disabled viewport rejects input and disables descendant actions.

The shared renderer clips content against every ancestor viewport.
Each viewport also owns a native parent HWND. Windows clips child EDIT and caption pixels against that parent.
Nested native fields and captions paint after the shared frame. This order prevents the transparent viewport from erasing native text.
A geometry override supplies clipped native EDIT bounds and offscreen state to UIA.
The override preserves the native accessible name, value pattern, and available native text patterns.
It does not replace editable text, selection, undo, or IME.

The viewport exposes UIA `ScrollPattern`, scroll percentages, viewport fraction, enabled state, and scroll-property events.
Custom descendants expose `ScrollItemPattern` for reveal without selection or focus.
Custom controls and embedded FileList rows expose clipped bounds.
The scrollbar has no separate UIA element.

ScrollView does not virtualize arbitrary content. All its controls and native peers remain in memory.
`FileList` remains the virtualized choice for large collections. ScrollView does not create a control for each FileList row.
The gallery has 17 searchable examples, including measured labels, native fields, disabled controls, and a scrollable preferences form.
F6 cycles dark, light, and explicit high-contrast colors outside grids. Within a grid, F6 selects the header.

### Input and accessibility

Buttons and checkboxes support hover, pressed state, pointer capture, drag-out cancellation, and capture loss.
Window deactivation and cancellation also cancel pending presses.
Tab and Shift+Tab move focus through enabled controls. Space activates a focused button or checkbox on release.
Enter activates a focused button. Enter does not change a checkbox.
The native EDIT retains Windows text selection, undo, clipboard, IME, and native UIA text behavior.
The bridge keeps activation on the top-level window when the native UIA proxy activates the EDIT HWND.
External UIA focus restores a minimized host and leaves keyboard focus on EDIT.
This path does not replace the native Value or Text providers.
Windows foreground-activation restrictions still apply.

The bridge keeps the composition guard active through native `WM_IME_ENDCOMPOSITION` processing.
It then publishes the committed value. Repeated end notifications do not repeat an unchanged text callback.
Same-DPI theme changes reuse the native font. DPI changes replace the font without replacing the EDIT HWND.

Custom controls expose UIA text, button, or checkbox roles.
Their accessible names track the public names. Their automation IDs use the stable element IDs.
Custom control fragments expose explicit focus, Invoke, and Toggle actions where applicable.
The HWND adapters supply native tree placement. Native EDIT keeps its own provider identity and a preceding native label.
The backend publishes name, enabled, focus, and toggle changes, plus button invocation events.
Retained providers reject actions after window teardown.
Lists also publish scroll percentage, viewport fraction, scroll availability, selection, and item-focus property changes.
Unchanged properties do not produce framework property events.
Enabled high-contrast checkmarks use the selection-text color. A pressed focus outline also uses that color.

## Framework boundaries

`xui_core` has no Windows dependencies. Its public API includes stable element identity, explicit property updates, and two-pass layout.
`Stack` supports horizontal and vertical layout, preferred sizes, flex space, gaps, and padding.
Elements distinguish layout invalidation from paint invalidation. The host combines pending layout requests and uses normal invalid paint regions.
The application has no continuous render loop.

`FileList` owns navigation and viewport behavior. `FileListModel` owns items, filter results, and selection identity.
Item focus is separate from selection. An accessibility focus action does not select or deselect an item.
The renderer submits visible rows plus a small buffer. It does not create an element, text layout, or UIA provider for every item.
Items and filter indices remain in memory. This is visual virtualization, not a paged storage system.

`Commands` remains available for command tables.
The browser connects shared callback actions through `Window::on_key` and `Control::on_context_menu`.
The Windows backend separates drawing, list adaptation, asynchronous delivery, native text input, and UI Automation.
Directory enumeration belongs to the sample service. The image backend owns image-file access and WIC decoding.

Each `ViewTask` has one worker for enumeration, sorting, snapshot construction, and filtering outside the UI thread.
Generation numbers reject stale source data and stale filter results.
Window closure requests cancellation without a UI-thread join. A filesystem driver that ignores cancellation can delay final process cleanup.
The UI thread submits committed text without a typing timer. It keeps the previous view until the current result arrives.

`NativeEditBridge` uses a real Windows EDIT control. Windows owns its IME, text selection, Unicode input, undo, clipboard behavior, and accessibility.
This bridge is not a custom text engine or a completed TSF implementation.
The search field has a 1,024-character limit. Framework text drawing never substitutes for editable text.
The empty-field hint uses the theme color. It is decorative text, not part of the editable value.

The custom list exposes UIA list semantics, selection, scroll, fragment navigation, and on-demand item providers.
Selection is optional and single-item. UIA removal clears only the addressed selected item.
Item providers retain stable IDs instead of recycled row positions.
Native EDIT supplies search-field accessibility. The public `Label` provider supplies status-text accessibility.
The window uses per-monitor DPI, system high-contrast colors, and graphics-target recreation.

## Known limits

The legacy entry point supports one active window. An explicit application context supports multiple ownerless windows on the same UI thread.
It does not provide a general styling system, a reactive property graph, or a custom text engine.
Filter matching uses wide-character case conversion, not full Unicode normalization or linguistic search.
Filtering still examines every source row. It runs on the worker, not the UI thread.
The index format supports at most 4,294,967,295 rows, but available memory imposes a much smaller practical limit.
All source items remain in memory. The million-row benchmark is not a million-file desktop test.
Very large scroll extents lose subpixel precision because offsets use floating-point values.
Cancellation cannot interrupt one allocator operation, one substring search, or a filesystem driver that ignores cancellation.
The `ViewTask` cleanup thread waits for cancelled workers. Process exit does not have a fixed shutdown deadline.
UIA clients that enumerate the complete tree still perform work for every item.

UIA supports the patterns that this demo uses, not every Windows control pattern.
Application context menus use the shared theme. Native EDIT keeps its Windows menu when no custom menu callback exists.
Custom touch gestures, drag-and-drop, and file modifications are outside this milestone.
The explorer uses Windows mouse promotion for touch input. Dedicated touch-hardware coverage remains incomplete.
The image sample replaces folders through its path field.
High-contrast changes and graphics-target recreation have implementation paths, but hardware coverage is not exhaustive.
After server exit, the Windows native HWND focus proxy can return `S_OK` for a stale `SetFocus` call.
The native Value provider returns `UIA_E_ELEMENTNOTAVAILABLE`, and the stale focus call must leave another window's focus unchanged.
Custom providers return `UIA_E_ELEMENTNOTAVAILABLE` after disconnection.
A native focus HRESULT alone is not proof of live keyboard focus.
