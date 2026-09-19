# Commands and navigation

Build and test commands are in [CONTRIBUTING](../../CONTRIBUTING.md).
See the [reference index](README.md) for related APIs.
Examples in this reference use C++ unless stated otherwise.
For C# and Rust coverage, use the [binding reference](bindings.md).

## Command and navigation controls

`xui/commands.hpp` supplies `CommandSet`, `CommandMenu`, `CommandSurface`, `CommandBar`, and `CommandBindings`.
The older `Commands` class and flat native `MenuItem` interface remain available.
An immutable command snapshot contains stable IDs, parent IDs, labels, actions, enabled states, optional checks, icons, and shortcut hints.
A pin has its own label and callback. It never calls the primary action.

`Window::show_commands` opens a retained menu or searchable palette.
The palette uses native EDIT for committed text and IME.

The palette opens with focus in its search field and shows an inline search prompt.
The title, close button, rounded frame, soft shadow, and keyboard footer distinguish the popup from the page.
In Classic and WinUI, the palette background is opaque and matches the command list, including the padding inside the frame.
The search field fills the available width. Its icon and padding also accept clicks to focus the editor.
Pointer hover highlights enabled command rows without changing search focus or keyboard selection.
A click runs a command, opens a submenu, or invokes its separate pin action.
Clicks on section headers, disabled commands, and separators leave the palette open. Outside clicks dismiss it.
The close button and Escape dismiss the palette and restore the previous focus.
Switching to another application leaves the palette and its query open.
Searchable palettes center horizontally in the available window area, with a stable search position near the top.
Their height follows the results, up to the configured popup height. Longer results scroll; empty results retain one message row.
Menus without search remain anchored to their invoking control.
In C#, `Window.MenuFlyout(name)` creates a compact menu without palette controls or a keyboard footer.
Its command rows show icons beside labels and checkmarks for checked commands.

Up and Down move the selection. Right opens a submenu. Left and Escape close the current submenu.
Enter runs the selected command. F2 runs only its pin action.
The primary action runs after dismissal. A pin leaves the menu open.
Missing filtered commands cause deterministic focus repair. Disabled commands and separators cannot receive menu selection.
Separators occupy 12 DIPs, rather than a full command row.
Disabled submenu parents also block their descendants.
Submenu UIA state follows popup expansion and collapse. Repeated expansion does not open a duplicate popup.

Shortcut hints are display text.
Only an explicit `CommandBindings::bind` registration creates a binding.
Applications pass keyboard events to `CommandBindings::invoke` from `Window::on_key`.
The command gallery registers Ctrl+O independently of its hint text.

```cpp
auto surface = std::make_shared<xui::CommandSurface>(L"Actions");
surface->set_commands(commands);
window.show_commands(surface, *anchor);

surface->on_query(start_application_query);
// UI-thread completion rejects canceled and foreign requests.
surface->complete(request, next_commands, error);
```

Command snapshots have at most 4,096 records and eight hierarchy levels.
Each label has a 1,024-code-unit limit. Each command has at most eight shortcut hints.
Queries have a 256-code-unit limit and one current cancellation token.
Pending queries keep the previous rows visible. Closure and direct source replacement invalidate the current token.
Each completion consumes its token once.
Applications own worker scheduling and UI-thread completion.
The toolbar has at most 64 records. Its overflow uses the same command IDs and actions.
Command snapshots own checked state. A toolbar click does not change that state independently.
Commands reuse virtual collection rendering and the bounded UIA action mailbox.
UIA exposes Menu, MenuItem, Invoke, SelectionItem, ExpandCollapse, and optional Toggle patterns.
Pins have separate Button providers. No native peer exists for each command row.

Use `CommandKind::section` for a labeled, non-interactive section header.
The header applies to subsequent records with the same parent until the next section header.
Each section needs a stable command ID and a label, but cannot have an action, check state, icon, or shortcut hints.
Section records cannot be parents. Use `CommandKind::submenu` for nested commands.
Filtering retains headers for sections with matching results and removes empty sections.
Section headers occupy 32 DIPs. Keyboard selection skips them, and UIA exposes them as headers without Invoke or SelectionItem patterns.
The command gallery includes two sections.

`xui/navigation.hpp` supplies `NavigationView`, `Breadcrumb`, `NavigationPane`, `LocationPicker`, and `ViewPicker`.
Breadcrumbs contain at most 64 stable path segments.
Overflow retains earlier segments. Arrow keys move between visible segment buttons.
The current segment exposes a current-location description.
Activation sends a navigation request. It does not silently change the committed path.

`NavigationPane` composes `ItemsView`, `Expander`, and static progress.
`LocationPicker` adds a native editor, command toolbar, and keyboard footer inside a retained popup.
`Window::show_location_picker` retains that composition and routes editor arrows to its virtual rows.
Navigation queries have a 1,024-code-unit limit and owner-specific cancellation tokens.
Hidden, collapsed, or closed navigation cancels its current query.
Sources supply cached rows and stable IDs. The framework performs no directory enumeration.

The explorer consumes command search, breadcrumbs, and a path-location picker.
The picker filters cached current-path locations. The existing address suggestions retain their separate asynchronous directory provider.
The path bar keeps its actions outside the native address band, including at narrow widths.
Paths with more than 64 components retain the first 63 and the current location, with an explicit gap marker.
Cancellation does not navigate or change the committed explorer location.
The gallery also composes inline and anchored quick access with `AdaptiveLayout`.
Its view picker changes the actual generic `ItemsView` presentation and row size.
It does not change the legacy explorer `FileList` presentation.

## MenuBar

`xui/menu_bar.hpp` supplies MenuBar.
`MenuBar::set_commands` accepts an immutable CommandSet.
Root records must be submenu groups. A bar accepts at most 64 root groups.
CommandSet limits still apply to the complete hierarchy.
Invalid snapshots preserve the previous commands.

The bar retains heading controls and uses window-managed submenu popups.
The command records own actions and pin callbacks.
The bar does not infer file, clipboard, or browser operations.
Application callbacks supply those operations explicitly.
The [MenuBar guide](controls/commands.md#menubar) includes four-language examples.

F10 enters or exits the menu bar. Alt plus a heading mnemonic opens that heading.
An ampersand marks a mnemonic in the command label. A doubled ampersand displays one literal ampersand.
Left and Right skip disabled headings and can switch the open submenu.
Down or Enter opens a heading submenu.

Escape returns to the heading, then to the previous focus. Tab exits the menu bar.
Outside input dismisses the menu through the normal popup rules.
Disabling the bar or replacing its source cancels its open menu.
UIA exposes a MenuBar root and MenuItem headings with ExpandCollapse.
Action descendants retain the existing Invoke behavior.

WinUI headings reserve four DIPs on each side within their layout slots.
These margins keep the external focus outline inside the bar.
Authored root padding remains outside the heading margins.
Hidden headings reserve no space. Constrained slots clamp their content to nonnegative bounds.
Classic heading layout remains unchanged.

## Optional Shell and caption integration

`xui/shell_commands.hpp` separates discovery from invocation.
`ShellCommandSession` accepts a provider and rejects disabled, stale, canceled, and native-only actions.
Discovery does not invoke verbs. Explicit invocation consumes the session before the provider call.
Synthetic providers support tests without file operations.

The Windows provider owns `IContextMenu`, optional `IContextMenu2` and `IContextMenu3`, and its native menu.
Creation, discovery, invocation, and destruction belong to the same STA thread.
A selection has at most 256 paths with one Shell parent.
Discovery has a 4,096-item bound and an eight-level depth limit.
Native fallback forwards submenu, owner-draw, measurement, and mnemonic messages to the extension.
It preserves extension-owned icons, labels, checks, and shortcut text without custom interpretation.
Snapshots carry optional vector icons, native-icon presence, and up to eight shortcut hints.
Owner closure revokes the Windows provider. Calls from another apartment thread fail before COM dispatch.

`Window::show_shell_commands` opens the native fallback for an explicit user request.
The explorer exposes this command as **Shell commands (native fallback)**.
The gallery requires a user-supplied path for its real Shell menu.
Its default demonstration uses synthetic commands only.

CAUTION: Shell verbs can change or delete files. Only an explicit user choice invokes a verb.

Third-party COM calls can block, allocate outside XUI limits, or fail inside native code.
Cancellation revokes XUI results and actions. It cannot interrupt an extension call already in progress.
This service handles HRESULT failures. It does not isolate extension crashes in another process.
The explorer and gallery report service errors without closing their windows.
No periodic worker, query timer, or Shell verb discovery loop runs at idle.

`WindowOptions::custom_titlebar` defaults to false.
With this flag, `Window::titlebar()` supplies a retained `TitleBar` with the existing `TabStrip` and independent caption Buttons.
The Windows backend removes the standard caption area but retains resize styles and system commands.
Hit tests separate tabs, caption buttons, drag space, and resize borders.
The maximize region reports `HTMAXBUTTON` for Windows Snap integration.
The title remains the actual native window title.

The gallery enables this optional caption. `--system-titlebar` retains the standard caption.
The explorer retains its standard caption and committed-location title.
Caption buttons use Windows-style graphics instead of the rounded XUI button style.
Their 46-by-32-DIP bounds contain centered Windows caption glyphs, with no border or contrasting background at rest.
Hover and press states use rectangular fills. The close button uses red, and inactive windows use dimmed glyphs.
High-contrast mode uses system colors. Caption buttons retain DPI scaling, accessibility actions, and the root render target.
Tab navigation skips caption buttons. Alt+Space opens the native system menu for keyboard window commands.
Native tests open and cancel the system menu, check resize corners, and maximize and restore through UIA.
Physical monitor transitions, Snap flyout appearance, and screen-reader speech still require manual checks.
