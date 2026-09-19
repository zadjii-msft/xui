# Context menus, tabs, and input

Build and test commands are in [CONTRIBUTING](../../CONTRIBUTING.md).
See the [reference index](README.md) for related APIs.
Examples in this reference use C++ unless stated otherwise.
For C# and Rust coverage, use the [binding reference](bindings.md).

## Shared context menus

Every `Control::on_context_menu` callback uses the same Windows menu backend.
This includes buttons, native text inputs, file lists, and data grids. Applications do not supply colors, drawing code, or HWNDs.
`WindowOptions::theme` and `Window::set_theme` select the shared palette.
System high contrast overrides dark and light colors.

The backend keeps `HMENU` and the native `#32768` popup.
Owner drawing supplies Segoe UI text, DPI-scaled padding, a checkmark column, right-aligned shortcut labels, separators, and selection colors.
The popup uses the shared surface color and a one-pixel border.
Its outer frame has eight-DIP rounded corners, scaled with the menu DPI.
The window region and border use the same outline. The corners outside that outline remain transparent.
Selected rows retain their smaller rounded corners. High contrast keeps square outer corners and square selected rows.
This shared menu appearance applies to both Classic and WinUI controls, without application-specific styling.
Windows retains menu placement, capture, dismissal, and accessibility.

```cpp
control->on_context_menu([&] {
    return std::vector<xui::MenuItem>{
        {L"&Refresh\tF5", refresh},
        {L"Copy path\tCtrl+C", copy_path, has_selection()},
        {L"", {}, true, false, true},
        {L"Dark theme", select_dark_theme, true, dark_theme()}
    };
});
```

The first tab separates the command name from its shortcut label.
A shortcut label does not register a keyboard shortcut. `Window::on_key` can call the same command function.
An ampersand marks a mnemonic. Two ampersands display one literal ampersand.
Other letters select commands by their first letter. Repeated letters cycle through matching commands.
Up, Down, Home, End, Enter, Escape, the context-menu key, and Shift+F10 use the same menu.
Home and End select the first and last enabled commands through the native menu-selection protocol.
Native arrow navigation can focus a disabled command. Enter dismisses that menu without a command call.
Separators do not run commands. UIA invocation rejects disabled commands.

The menu factory and its returned items are snapshots.
The backend closes the native menu and releases its resources before it calls the selected action.
An action can change the theme, open another context menu, close the window, or delete its public `Window`.
The backend remains alive until the active dispatch returns.
Callback exceptions and reported native API failures reach `Window::error` and a nonzero `Application::run` result.

A context menu cancels visible and pending suggestions before it opens.
Late suggestion results cannot reopen the dropdown over the menu.
A custom text-input menu does not open during IME composition.
Menu dismissal preserves native EDIT selection. Focus returns to the previous control only while the same host remains in the foreground.
Outside clicks, host deactivation, host closure, and theme or DPI changes cancel the menu.
Windows places the popup within the destination monitor's work area.

Menu fonts, brushes, thread hooks, and owner subclasses exist only for the active popup.
Closed menus retain no menu resources and schedule no timers or paints.
The backend creates no Direct2D targets.
Native MSAA menu metadata preserves command names and checked state. UIA retains native `Menu`, `MenuItem`, focus, and Invoke behavior.
The current model is flat. Submenus, menu bars, dropdown buttons, and Windows Shell extension menus are not part of this change.

`NavigationView.OnContextMenu` in C# binds the main, header, and footer lists.
Its item factory receives the targeted row ID.
`NavigationList::prepare_context_menu` updates row focus without navigation or selection events.
The C ABI supplies the row ID in the menu's `XUI_REQUEST` event.
Navigation menus reuse collection source checks, Shell discovery, and cancellation.

PNG copies permit image review without changes to the BMP capture tests.
The menu test reports sampled popup visibility latency, not an isolated rendering benchmark.
If Windows still maps an executable from a previous fixture run, Shell thumbnail tests need a fresh fixture directory.

The C++ API provides `Window::set_title` and `Window::title`.
The setter runs on the window owner thread before or during `Application::run`. Repeated values do not update the native caption.
A closed window rejects changes. The getter retains the last title after closure.
Invalid strings, wrong-thread calls, and native title failures throw exceptions.
`Button::set_icon` selects an icon-only presentation without changing the accessible name.
`ButtonIcon::none` restores text. Icon buttons retain standard focus, hover, pressed, disabled, and high-contrast states.
`ButtonIcon::save`, `save_as`, `undo`, and `redo` provide document command icons.
C# exposes `ButtonIcon.Save`, `SaveAs`, `Undo`, and `Redo` through `Button.SetIcon`.
Their ABI values are 23, 24, 25, and 26. Existing icon values, including Open at 22, remain unchanged.
`ButtonIcon::chevron_up`, `chevron_down`, and `chevron_right` provide disclosure icons with ABI values 27, 28, and 29.
C# exposes these as `ButtonIcon.ChevronUp`, `ChevronDown`, and `ChevronRight`.
Rust exposes the right chevron as `ButtonIcon::ChevronRight`.
Classic draws two strokes. WinUI uses the corresponding ChevronUp, ChevronDown, and ChevronRight symbols.
The same icons work in navigation entries, tabs, command records, and collection visuals.
Classic draws vector shapes. WinUI uses the Save, SaveAs, Undo, and Redo symbols from [Segoe Fluent Icons](https://learn.microsoft.com/en-us/windows/apps/design/iconography/segoe-fluent-icons-font).
The [Segoe MDL2 Assets](https://learn.microsoft.com/en-us/windows/apps/design/iconography/segoe-ui-symbol-font) fallback uses the same codepoints: E74E, E792, E7A7, and E7A6.
Applications supply accessible button names and command handlers. An icon does not register a keyboard shortcut or change an editor's undo history.
Values outside the defined icon range cause an argument error.
`TextInput::set_caption_visible(false)` hides the native caption without a search icon.
The native label remains available for EDIT naming. The default caption and search presentations remain unchanged.
The C# explorer also uses icon and caption controls through the bindings.
See the [binding reference](bindings.md) for the available language surface.

`KeyEvent::text_input` identifies text-producing keys outside native editors.
The .NET equivalent is `UiKeyEvent.IsTextInput`.
The C ABI reports this flag in bit zero of `xui_key_event.reserved`.
An application can focus a native text input and return false from its key handler.
XUI then routes the original key to that editor before Windows translates it.
This preserves keyboard layouts, dead keys, and IME input without conversion from virtual keys to text.

## Tabs, split panes, and activation

`TabStrip::set_tabs(items, selected_id)` replaces tab data without a native tree replacement.
It rejects zero IDs, duplicate IDs, and an unknown selected ID before a state change.
IDs represent stable tab identities, not display positions. An application must not reuse an ID for a different tab.
`select`, `step`, and `request_close` share the public action callbacks.
Property assignment does not call `on_select`. `on_close` requests closure without an automatic data change.

Tab selection and content activation are separate operations.
C++ hosts use `TabStrip::on_activate` to focus their selected content after a click, Enter, or Space.
The C ABI reports activation as `XUI_CLICK`. The .NET explorer handles `EventKind.Click` by focusing the file grid.
Arrow-key selection does not activate content or move focus out of the strip.
The focus rectangle appears only during keyboard navigation and stays inside the selected tab.

Tab context menus use the shared native menu backend.
Pointer requests target the tab under the pointer without selection or activation.
Keyboard requests target the selected tab. Empty strip space does not open a tab menu.
`TabStrip::prepare_context_menu(position)` records the target in `context_tab()`.
An absent position means a keyboard request.
The C ABI emits that tab ID in the context-menu `XUI_REQUEST` event.
C# exposes `TabStrip.OnContextMenu(Func<ulong, Command[]>, Action<ulong>)` and `ClearContextMenu()`.
The action callback receives the chosen command ID.
Tab replacement or reordering invalidates pending binding actions.
Tab menus do not accept Shell paths.

### Tab dragging between windows

`Window::on_tab_drag` enables pointer dragging for the two custom title-bar strips.
The window must belong to an `Application`.
Ordinary tab strips and windows without a handler retain their existing input behavior.
A click still selects and activates a tab. Close and New tab buttons do not start a drag.
The system drag threshold separates clicks from drags.

One native Windows move-size loop handles each gesture.
Within the source strip, `reorder` requests a new position while the window stays stationary.
Outside that strip, `tear_out` asks the application to separate the dragged tab.
Until an accepted `join`, the dragged tab stays on the original HWND, in the same strip, with the same identity.
The application creates another window for the remaining models and content.
Before `Application::show`, remainder windows must call `set_show_activated(false)` or use `WindowOptions::show_activated = false`.
Otherwise, the new window can take activation from the native move loop.
During a gesture, the framework inserts nonactivated same-application windows immediately below the moving HWND in the Z-order.
This avoids a second move loop or synthetic mouse input.

`query_drop` asks whether a visible strip can receive the tab.
It must not change tab data.
An accepted target shows an insertion marker.
Targets must belong to the same application and UI thread, with their own drag handler.
Hidden, disabled, minimized, modal, and occluded windows do not receive tabs.

With full-window dragging, `join` requests a temporary model transfer into the hovered destination.
Before an external `join`, `tear_out` preserves the original HWND and creates the remainder workspace when necessary.
An accepted `join` presents the tab in the destination while the original window retains the native move loop.
The native loop hides or shows the original window through supported window-position flags.
Repeated `join` requests to the same destination can reorder the hosted tab.
A handler that ignores or rejects `join` retains release-only `drop` behavior.
Outline-only window dragging also retains the release-only fallback.

Before retargeting or cancellation, `leave` asks the application to return the hosted tab to the initiating strip.
A true result means that the tab is back in its initiating strip, with its original identity.
On release while joined, `drop` commits the existing hosted transfer instead of transferring the tab a second time.
Without an accepted `join`, `drop` requests a transfer on pointer release.

Each event contains the source strip, tab identity, target window, target strip, and insertion index.
Callbacks always run on the initiating window.
The source strip and tab identity remain fixed throughout the gesture, even while another window hosts the tab.
For joined events, the target fields identify that destination.
Strip zero is the primary strip. Strip one is the secondary strip.
The index identifies a slot before removal from the source.
For example, an index equal to the target count appends the tab.
Handlers return true after acceptance of a request.
A rejected release-only drop leaves the detached window open.

`cancel` reports Escape or cancellation of the native move operation.
For a joined tab, `leave` precedes `cancel`.
The application restores its saved model state.
`completed` follows the move loop, including a joined `drop` commit, and releases application drag state.
Applications retain every participating window and control tree until `completed`, including an empty initiating window.
Model transfers never reparent native controls.
Window closure stops later callbacks.
Callback exceptions use the existing window error and application error paths.
Native controls never move between window owners.

`Window::placement` and `set_placement` use outer screen bounds in physical pixels.
Negative coordinates support monitors left of or above the primary monitor.
The placement also contains the maximized state and restored bounds.
A window accepts placement before its first show.
During the initial drag, the source placement remains the pre-drag placement for the remainder window.
The [C# explorer](file-explorers.md) supplies model transfer, cancellation, and multiwindow lifetime management.

### Tab appearance and actions

Classic and WinUI tabs have rounded top corners and an open selected bottom edge.
Inactive tabs share a continuous strip instead of separate button outlines. The close button highlights under the pointer.
The row inherits its parent background, including unused space after the last tab.
Empty rows draw no baseline. A populated row's baseline stops at the selected tab, which opens into its content.
The custom title bar extends this baseline across navigation, split-divider, and caption gaps.
Each populated tab strip keeps its own open selected edge and pane alignment.
Gap borders use the adjacent strip's border color. High contrast uses the system border color.

`TabStrip::set_new_tab_button_visible(true)` adds an optional **New tab** icon button.
The default is `false`. The button follows the rightmost visible tab, not the end of the available strip.
Overflow reserves up to 32 DIPs for the button. Tiny strips clip the button instead of overlapping tabs.
An empty strip keeps the button at its left edge.
`on_new_tab` receives the action without automatic tab creation or selection.
The button retains native pointer capture, cancellation, keyboard focus, Enter, Space, and UIA Button/Invoke behavior.
Disabled strips reject the action. The button does not start a title-bar drag.

The C ABI provides `xui_tab_set_new_button` and `xui_tab_get_new_button` in `xui_layout.h`.
Visibility accepts only zero or one. Activation emits `XUI_ACTION` with ID zero.
C# provides `NewTabButtonVisible`, `SetNewTabButtonVisible(bool)`, and the existing `EventKind.Action` event.
Rust provides `set_new_tab_button_visible` and `new_tab_button_visible`.

The tab strip exposes UIA `Tab`, `TabItem`, `SelectionPattern`, and `SelectionItemPattern`.
It publishes structure, selection, and focus changes. A removed tab provider rejects later actions.
`SplitView` exposes a divider through `RangeValuePattern`, with a ratio from 10 to 90 percent.
The layout also enforces pane minima. A requested ratio can therefore differ from the physical split near the minimum extent.
`set_layout(Axis::vertical, minimum_extent)` places the panes above and below the divider.
The default remains horizontal with a 300-DIP minimum. Each configured minimum must be finite and between 1 and 65536 DIPs.
Left/Right keys resize horizontal panes. Up/Down keys resize vertical panes. Home restores the midpoint.
`on_ratio_changed` reports a changed ratio. Layout changes preserve the ratio and cancel an active drag.
Opt-in visibility and ratio animations use the configured axis and minimum extent.
An axis or minimum change settles active animation before the next layout.
Native children and custom pixels stay inside their content host.
The axis-specific resize cursor applies only to an enabled, expanded divider or its active drag.
Pane controls keep their own cursors, including the native text editor's I-beam.
Capture loss, cancellation, deactivation, and DPI changes cancel a divider drag.
Synthetic pointer messages do not focus a divider in an inactive window.

`SplitView::set_primary_visible(false)` removes the primary pane without replacing either content tree.
If the secondary pane is enabled, it receives the full pane area, including in a narrow window.
`expanded()` still reports secondary-pane visibility.
The divider has no width and does not accept focus or input.
Restoring the primary pane restores the saved ratio and normal responsive layout.
C# exposes this state as `SplitView.FirstVisible`.
The explorer uses it to keep a detached secondary tab full-width during the native move loop.

### Tab icons

`TabItem` accepts an optional `icon` and `image_path` after its identity and title.
Existing two-field construction produces text-only tabs.
Icons do not change tab identities, accessible names, selection events, or close and new-tab targets.
Each icon fits in a 16-DIP square before the title. Narrow tabs clip their content without covering the close button.

```cpp
tabs->set_tabs({
    {1, L"Documents", xui::ButtonIcon::folder, L"C:\\Users\\Public\\Documents"},
    {2, L"Preview"}
}, 1);
```

Visible tabs use the shared asynchronous image service.
Folder fallback icons select the Shell decoder, including folders whose names have image extensions.
Other image paths use the same decoder selection as collection visuals.
Pending or failed images keep the vector fallback. Image failures emit the existing thumbnail diagnostic.
Disabled and high-contrast tabs use theme-colored fallback icons instead of image pixels.

Selection, title changes, and reordering retain requests for the same identity, path, and decoder.
A path change, tab removal, hidden strip, window closure, or DPI change cancels obsolete requests.
Tabs share the existing limits of 24 image slots per control and 48 per window.
No Shell lookup runs on the UI thread.

The C ABI adds `xui_tab_items_visual` with optional parallel `xui_item_visual` records.
The `xui_choice` record and `xui_choices` behavior remain unchanged.
C# adds `TabEntry` and `TabStrip.SetTabItems`, while `SetTabs(ReadOnlySpan<Choice>)` remains available.
The Rust FFI exposes the new C function. Its typed `TabStrip::set_tabs` method remains text-only.

```csharp
tabs.SetTabItems([
    new(1, "Documents", ButtonIcon.Folder, @"C:\Users\Public\Documents"),
    new(2, "Preview")
], selected: 1);
```

The C# FileExplorer supplies each tab's current folder path and a folder fallback icon.
Both titlebar panes use this shared API.

### Tab colors

`TabColors` provides optional colors for the row, selected tab, inactive tabs, hover state, and borders.
Selected and inactive tabs each have separate background and text colors.
"Selected" identifies the active page, not keyboard focus.
Each color uses `0xRRGGBB`. Alpha values are not supported.
Unset colors follow the current theme, and high contrast uses system colors instead of overrides.

```cpp
xui::TabColors colors;
colors.selected_background = 0x26465e;
colors.selected_text = 0xffffff;
tabs->set_colors(colors); // The row still inherits its parent background.
tabs->set_colors({});     // Restore theme colors.
```

The .NET binding provides `TabStrip.Colors` and `SetColors(new TabColors(...))`.
Nullable fields restore individual theme colors, and `SetColors(default)` clears all overrides.
Rust provides `TabStrip::set_colors` and `colors`, with `Option<u32>` fields.
The C ABI provides `xui_tab_set_colors` and `xui_tab_get_colors` in `xui_layout.h`.
Its versioned record uses a mask to distinguish an unset color from black.

Applications must pair custom backgrounds with readable text colors.
Explicit colors stay unchanged across theme switches until the application replaces or clears them.
The gallery's Tabs page includes a **Custom tab colors** toggle.

### File activation

`FileList::on_activate` receives a copied `FileItem` and a `FileActivation` reason.
The reasons distinguish Enter, double-click, and an application command.
`activate_selected` rejects disabled lists and hidden selections.
`restore_state` restores selection, independent item focus, and scroll offset against the current snapshot.
It preserves hidden identities that still exist in the source.

C# and Rust expose workspace controls through the [feature extension](bindings.md#current-coverage).
The extension preserves existing ABI entry points and layouts.

## Address suggestions

Both explorer address fields provide folder suggestions. The search fields remain separate filters.
Typing does not navigate. A mouse click or Enter accepts the selected folder and submits the address once.
Tab accepts a selected folder without submission. Without a selected folder, Tab keeps its normal focus behavior.
Shift+Tab keeps reverse focus traversal. Escape first closes suggestions without a text change.
Another Escape restores the current explorer path or cancels a pending navigation.
Down can open suggestions without a text change. Ctrl+L alone does not enumerate folders or network drives.

The public C++ API is optional:

```cpp
#include "xui\application.hpp"
#include "xui\suggestions.hpp"

auto address = std::make_shared<xui::TextInput>(L"Folder address");
address->set_maximum_length(32767);
address->set_suggestions(xui::folder_suggestions());
address->set_suggestion_context(L"D:\\Documents");
```

The context is the absolute base folder for relative text. Each explorer pane updates this context from its active tab.
`set_text`, context changes, and source changes cancel old suggestions. Even a same-value `set_text` cancels an open dropdown.
These property updates do not open a dropdown. `set_suggestions(nullptr)` disables the feature.
The C ABI and language bindings do not expose this optional API yet.

`SuggestionSource::suggest` receives text, context, an explicit-request flag, and a cancellation function.
The worker calls the source outside the UI thread. Source implementations must not access controls or retain window callbacks.
The source can outlive the control during cancellation. Its destructor must not require the UI thread.
A source must bound its own work and allocations. Results contain at most 64 strings, each with at most 32,767 UTF-16 units.
The delivery boundary also enforces these limits. It limits status text to 256 units and excludes values that exceed the input limit.
Custom COM sources require their own apartment and correct marshaling. XUI does not transfer COM interfaces between threads for this feature.

### Filesystem source and cancellation

The filesystem source calls `FindFirstFileExW` and `FindNextFileW` on the worker.
The search pattern contains the typed prefix. A second ordinal, case-insensitive comparison excludes DOS wildcard aliases.
Results contain directories, not executable files, URL history, or shell commands.
Supported input includes Unicode, spaces, folder names with dots, drive roots, absolute paths, relative paths, and both separator forms.
Dot and dot-dot expand against the current base folder. Quoted paths match the explorer navigation rules.
Environment references use `%name%`. Navigation and suggestions share `xui::expand_path_input` from `xui\path_input.hpp`.
The helper reads Unicode environment values through `GetEnvironmentVariableW`. It expands each reference once, without shell execution or process-directory changes.
Relative expanded paths use the current tab folder. Suggestions display complete expanded paths.
Unknown paired references report an error without a folder scan. A component such as `%USERPRO` reports an incomplete reference.
Unpaired percent signs inside names, a trailing `%`, and `%%` remain literal.
Other paired percent signs denote environment references, not literal filename characters.
Input and expanded output each have a 32,767-unit limit, including the terminator. Windows getter errors remain visible.
The suggestion worker reads environment values outside the UI thread. Navigation reads them when the address is submitted.
Drive-relative paths such as `C:folder` remain unsupported.
UNC share paths are valid. Server-only paths do not enumerate network shares.
An explicit request with empty text and empty context returns drive names without a scan of their contents.

An 80-ms timer combines successive edits. Only the focused input keeps an active timer.
An open dropdown retains its rows and size during the next request, then replaces the results in place.
Typing clears the old selection. Pending rows cannot accept keyboard or mouse actions for an outdated query.
The loading message appears only when the dropdown first opens, not between successive results.
One shared worker callback runs at a time. It retains one pending request, which the latest request replaces.
Generation checks reject old results after typing, tab changes, focus loss, or closure.
Each scan retains at most 64 paths and examines at most 4,096 matching entries.
A 100-ms elapsed-time budget stops the scan between filesystem calls. A status explains partial results or errors.
The retained subset is sorted, not the complete directory. More specific text narrows the next scan.
There is no directory cache, polling loop, thread per keystroke, or new Direct2D target.

One Windows filesystem call can block beyond the elapsed-time budget, especially on a network path.
Cancellation cannot interrupt that call. The UI remains responsive, but the latest request waits for the single worker.
Window closure revokes delivery without a UI-thread join. The worker owns its pending resources until the call returns.
The implementation does not start replacement workers to bypass a blocked network call.

### Native input and popup

The real Windows EDIT retains text, caret, selection, clipboard, undo, IME, and native text accessibility.
The popup contains a native LISTBOX and a separate native status label. The status is not a selectable folder.
UIA exposes the native List and SelectionItem patterns, full path names, and selection state.
The native LegacyIAccessible default action accepts a folder and submits the address.
The popup does not take EDIT focus. It uses the complete field bounds and clamps its position to the monitor work area.
Focus loss, pane hiding, window movement, resize, minimize, and destruction close the popup.
IME composition closes suggestions and suppresses application shortcuts. Committed input can start a new request.
Disabled and read-only inputs do not open suggestions.

### Browser mouse navigation

`Window::on_navigation` receives a `NavigationEvent` with a direction, a target control, and an optional mouse position in client DIPs.
The handler returns `true` to consume the event. This public C++ callback does not change focus or text selection.
The C ABI and language bindings do not expose this callback.
`WM_XBUTTONUP` dispatches Back for `XBUTTON1` and Forward for `XBUTTON2`.
The host consumes button-down and double-click messages without a second navigation. Parent notifications do not dispatch navigation.
Native EDIT, FileList, tab, and custom control peers use the same host callback.
`WM_APPCOMMAND` supports browser Back and Forward. Non-mouse commands identify the source control or the focused control.
The explorer activates the target pane and uses its active tab history. Unavailable history causes no folder request.
Alt+Left and Alt+Right retain their existing keyboard behavior.

The list and status use the application text and background colors, including system high-contrast colors.
Windows retains control of the selection highlight, scrollbar, and border appearance.
The popup is not a pixel-identical copy of a particular Explorer version.

The Shell APIs were considered:
[IACList::Expand](https://learn.microsoft.com/en-us/windows/win32/api/shlobj_core/nf-shlobj_core-iaclist-expand)
requests candidates,
[IACList2::SetOptions](https://learn.microsoft.com/en-us/windows/win32/api/shlobj_core/nf-shlobj_core-iaclist2-setoptions)
controls the source, and
[IAutoComplete2](https://learn.microsoft.com/en-us/windows/win32/api/shldisp/nn-shldisp-iautocomplete2)
controls Shell autocomplete behavior.
These contracts do not establish bounded filesystem latency or cancellation for a slow UNC request.
XUI therefore uses the asynchronous source and native popup described here, not `IACList2` or `IAutoComplete2`.
This choice does not establish which implementation any particular Explorer version uses.
