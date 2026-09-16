# Commands and windows

[Control catalog](README.md) · [Command contract](../commands-and-navigation.md) · [Menu input](../menus-and-input.md)

Examples use the [C++ fragment context](README.md#use-the-examples), except the complete TitleBar function.
Add `xui\commands.hpp` and `xui\titlebar.hpp`.

## Choose a command presentation

| Requirement | API |
| --- | --- |
| One action | Button |
| A retained command row | CommandBar |
| A searchable palette or anchored menu | CommandSurface |
| A directly composed virtual menu | CommandMenu |
| A flat Windows context menu | `Control::on_context_menu` |
| Third-party Shell extensions | `Window::show_shell_commands` |

`CommandSet` is an immutable command model, not a control.
It supplies stable IDs, parent IDs, labels, actions, enabled state, optional checked state, icons, and shortcut hints.
Pins have independent labels and actions.
The older `Commands` class remains a nonvisual callback table.

`CommandKind::submenu` defines nesting.
`section` and `separator` define noninteractive records.
Command snapshots accept at most 4,096 records and eight hierarchy levels.

## CommandMenu

Use `CommandMenu` to compose a virtual menu directly in C++.
Use CommandSurface for window-managed popup presentation and dismissal.

```cpp
auto commands = std::make_shared<const xui::CommandSet>(
    std::vector<xui::CommandRecord>{
        {1, 0, L"Show summary", [&window] { window.set_title(L"Summary"); }}
    });
auto menu = std::make_shared<xui::CommandMenu>(L"Document commands");
menu->set_commands(commands);
root->add(menu, 1);
```

`set_commands` can select a parent command and a query.
`execute(id, pin)` invokes the selected action type.
`on_submenu`, `on_collapse`, and `on_back` connect custom presentation behavior.
Disabled records and separators cannot receive command selection.

The style target is `command_menu`.
Rows, sections, checks, shortcut hints, and submenu arrows are virtual parts.
They are not a retained Button per command.

C# and Rust have no standalone CommandMenu factory.
`CommandSurface.Menu` or `menu()` returns the existing element for styling and layout.
That element is not an ItemsView and does not gain typed collection methods.

## CommandBar

Use `CommandBar` for frequently used actions and automatic overflow.
Its immutable snapshot owns command checked state.

```cpp
auto commands = std::make_shared<const xui::CommandSet>(
    std::vector<xui::CommandRecord>{
        {1, 0, L"Summary", [&window] { window.set_title(L"Summary"); }},
        {2, 0, L"Details", [&window] { window.set_title(L"Details"); }}
    });
auto bar = std::make_shared<xui::CommandBar>(L"View commands");
bar->set_commands(commands);
root->add(bar);
```

The toolbar accepts at most 64 records.
Overflow retains the same command IDs and actions.
A command click does not independently rewrite snapshot checked state.

The style target is `command_bar`.
`command_button(id)` and `overflow_button()` expose real retained Buttons.
Their callbacks belong to the command snapshot.
Styles must not replace those callbacks.
Separator color and base/local thickness support presentation changes.
Arbitrary slot metrics and toolbar section labels remain unsupported.

## CommandSurface

Use `CommandSurface` for an XUI command palette or anchored retained menu.
The constructor defaults to a searchable palette.

```cpp
auto commands = std::make_shared<const xui::CommandSet>(
    std::vector<xui::CommandRecord>{
        {1, 0, L"Show summary", [&window] { window.set_title(L"Summary"); }}
    });
auto surface = std::make_shared<xui::CommandSurface>(L"Document actions");
surface->set_commands(commands);
anchor->on_click([&window, &anchor, surface] {
    window.show_commands(surface, *anchor);
});
```

Pass `false` as the second constructor argument for a nonsearchable anchored menu.
Search uses native EDIT and committed text.
Up and Down move selection. Enter runs the primary action after dismissal.
F2 runs a separate pin action and leaves the menu open.
Escape or the close button dismisses the surface.

`on_query` receives an owner-specific `CommandQuery`.
`complete(request, commands, error)` accepts a current result on the UI thread.
The application owns worker scheduling.
Closure and source replacement invalidate query tokens.
`set_current` can reject commands for an obsolete application context.

The facade root is `popup()`, with target `popup`.
It supports only `open` and inherited `disabled` states.
There is no automatic root loading, error, or empty selector.
`editor()`, `title()`, `status()`, `close_button()`, `content()`, `results()`, and `menu()` retain separate targets.
Status Labels do not gain query-state selectors.

### Register real shortcuts

Shortcut hints show text. They do not register key bindings.
This fragment registers an actual Ctrl+O command:

```cpp
auto commands = std::make_shared<const xui::CommandSet>(
    std::vector<xui::CommandRecord>{
        {1, 0, L"Open", [&window] { window.set_title(L"Open requested"); }}
    });
auto bindings = std::make_shared<xui::CommandBindings>();
bindings->bind({static_cast<std::uint16_t>(xui::Key::o), true, false, false}, 1);
window.on_key([commands, bindings](const xui::KeyEvent& event) {
    return bindings->invoke(*commands, {
        static_cast<std::uint16_t>(event.key), event.control, event.shift, event.alt
    });
});
```

The event handler returns true only for an accepted command.
Native text editors retain character input and editing behavior.
The [input contract](../menus-and-input.md) defines routing and reserved keys.

## Native menus

Use `on_context_menu` for a flat Windows menu with explicit actions.
The framework closes the menu before the selected callback runs.

```cpp
auto field = std::make_shared<xui::TextInput>(L"Document name");
field->on_context_menu([&window] {
    return std::vector<xui::MenuItem>{
        {L"&Copy application title", [&window] { window.copy_text(window.title()); }},
        {L"Unavailable action", {}, false}
    };
});
root->add(field);
```

`&` marks a mnemonic. `&&` shows a literal ampersand.
Shortcut text after a tab character does not register a shortcut.
Without an override, native EDIT retains its system context menu.
The override does not replace its native text provider.

For a user-requested Shell menu:

```cpp
anchor->on_click([&window, &anchor] {
    window.show_shell_commands(*anchor, {L"C:\\Data\\Notes.txt"});
});
```

The path must identify an actual item supplied by the application.
The example path is not a file-creation instruction.

CAUTION: Invoke Shell verbs only after an explicit user choice. A verb can change or delete files.

Discovery does not invoke verbs.
Third-party extension calls can block or allocate outside XUI bounds.
Cancellation revokes results and actions but cannot interrupt a COM call already in progress.
The [Shell contract](../commands-and-navigation.md#optional-shell-and-caption-integration) defines STA ownership, selection limits, and error handling.

Neither native Shell menus nor XUI Shell HMENU presentation gains a generic control style target.
CommandMenu styles do not apply to extension-owned pixels.

### CustomShellMenu

`CustomShellMenu` is the public C++ Shell-command facade in `xui\shell_commands.hpp`.
It combines discovered Shell commands, application menu items, and an explicit Windows-menu fallback.
It is not an Element or a Popup-backed control.

This complete helper belongs at file scope.
The caller supplies an apartment-correct provider, a context-validity callback, and a nonempty fallback action:

```cpp
std::unique_ptr<xui::CustomShellMenu> create_shell_menu(
    std::shared_ptr<xui::ShellCommandProvider> provider,
    std::function<bool()> current,
    std::function<void()> windows_menu) {
    return std::make_unique<xui::CustomShellMenu>(
        std::move(provider), std::vector<xui::MenuItem>{},
        std::move(current), std::move(windows_menu));
}
```

Construction performs discovery but does not invoke a Shell verb.
`commands()` returns an immutable CommandSet. `menu_items()` supplies native menu records.
`discovery_error()` exposes discovery failure text.
`current()` rejects an obsolete context.
`dismiss(reason)` preserves a committed action or cancels uncommitted work.
The UI host must connect dismissal to the actual menu lifetime.

The facade retains original Shell identities instead of reconstructed verbs.
Its lifetime and provider calls belong to the UI thread and provider apartment.
C# and Rust have no standalone factory for this facade.
There is no CustomShellMenu style target or generic Shell HMENU styling.
Use the [Shell contract](../commands-and-navigation.md#optional-shell-and-caption-integration) for native fallback and cancellation limits.

## Window

`Window` owns content, UI-thread callbacks, focus, popup services, tasks, themes, and errors.
It is not a layout child.

```cpp
window.set_title(L"Workspace");
window.set_theme(xui::ThemeMode::dark);
window.set_visual_style(xui::VisualStyle::winui);
anchor->on_click([&window] { window.close(); });
```

`Application::run` runs once for a window.
Only one active window runs on a thread.
The calling thread must not use the COM MTA.
Callback exceptions or startup errors produce a nonzero result.
`error()` supplies the error text.

The window retains its content until destruction.
`post` queues UI-thread work while the window lives.
Close discards queued work.
Worker code must not assume a retained raw Window reference survives destruction.
The [application contract](../application.md) defines shutdown and task ownership.

## TitleBar

Use the window-owned TitleBar for custom caption integration.
Constructing an unrelated TitleBar does not install native window chrome.
The public C++ `TitleBar(std::wstring)` constructor also supports explicit composition.

This complete function replaces the shared fragment context:

```cpp
int run_with_titlebar() {
    xui::WindowOptions options;
    options.title = L"Workspace";
    options.custom_titlebar = true;
    xui::Window window(options);
    auto root = std::make_shared<xui::Stack>(xui::Axis::vertical);
    root->add(std::make_shared<xui::Label>(L"Workspace content"));
    window.titlebar()->tabs()->set_tabs({{1, L"Home"}, {2, L"Documents"}}, 1);
    window.set_content(root);
    return xui::Application::run(window);
}
```

`tabs()`, `secondary_tabs()`, and `leading()` expose retained controls.
`set_tab_panes` aligns tab rows to retained content panes.
The title remains the native window title.
Caption actions, resize regions, system commands, and drag behavior remain window-owned.
Alt+Space supplies native keyboard window commands.

The style target is `title_bar`.
`title()`, `minimize()`, `maximize()`, and `close()` keep their Label or Button targets.
Caption default styles do not replace native action wiring.
High contrast preserves system colors.
Physical monitor transitions, Snap flyout appearance, and screen-reader speech still need manual checks.

## Tooltips

Tooltips use control help text and the Window style API.
There is no Tooltip control constructor or extra focus stop.

```cpp
anchor->set_help_text(L"Open the document actions.");
anchor->set_tooltip_delay(600);
xui::PartStyleValues text;
text.font_size = 14.0f;
window.set_tooltip_style_values(xui::StylePart::text, text);
```

Help text also supplies UIA HelpText.
The framework shows delayed hover or focus help.
Only a visible tooltip has the `open` state.
Hidden tooltips have no timer.

The style target is `tooltip`, but application uses `Window::set_tooltip_style`.
`set_tooltip_style_values` sets local part values.
Styles do not show a hidden tooltip or change its delay.
The `.xui` compiler rejects Tooltip declarations.
C# and Rust expose the Window bridge directly.
The [tooltip contract](../control-styling.md#window-tooltips) contains equivalent binding examples.

Active media or web runtimes suppress tooltips.
The [native-host boundary](media.md#native-host-boundaries) explains popup and overlay restrictions.
