# Commands and windows

[Control catalog](README.md) · [Command contract](../commands-and-navigation.md) · [Menu input](../menus-and-input.md)

Examples use the [shared fragment context](README.md#use-the-examples), except the complete C++ TitleBar function.
C++ examples also need `xui\commands.hpp` and `xui\titlebar.hpp`.
The MenuBar example also needs `xui\menu_bar.hpp`.
C# fragments assume `using System;` and `using Xui;`.
Rust fragments use `use xui::*;` inside the shared `example` function.
That function returns `std::result::Result<(), Box<dyn std::error::Error>>`.
Rust callbacks use weak handles to avoid ownership cycles.

## Choose a command presentation

| Requirement | API |
| --- | --- |
| One action | Button |
| A retained command row | CommandBar |
| Persistent headings with submenu popups | MenuBar |
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

## MenuBar

Use `MenuBar` for persistent menu headings and their submenus.
The root command records must be submenu groups.
The native window manages popup display and keyboard input.
Application code supplies command actions, not a custom popup loop.

{% tabs %}
{% tab title=".xui" %}

```text
namespace ControlExamples;
component DocumentMenuBar {
    state string Message = "Ready";
    view {
        VStack() {
            MenuBar("Document menu", commands: new global::Xui.Command[] {
                new(1, "File", Kind: global::Xui.CommandKind.Submenu),
                new(2, "Show summary", Parent: 1)
            }, invoke: Execute);
            Text(Message);
        }
    }
    code csharp {
        void Execute(ulong id) {
            if (id == 2) Message = "Summary requested";
        }
    }
}
```

{% endtab %}
{% tab title="C#" %}

```csharp
var status = window.Label("Ready");
var menu = window.MenuBar("Document menu").SetCommands([
    new(1, "File", Kind: CommandKind.Submenu),
    new(2, "Show summary", Parent: 1)
]);
menu.Invoked += id => {
    if (id == 2) status.Text = "Summary requested";
};
root.Add(menu).Add(status);
```

{% endtab %}
{% tab title="Rust" %}

```rust
let status = window.label("Ready")?;
let menu = window.menu_bar("Document menu")?;
menu.set_commands(&[
    Command {
        id: 1, parent: 0, label: "File".into(), kind: CommandKind::Submenu,
        enabled: true, checked: None, shortcut_hint: String::new(), pin_label: String::new(),
    },
    Command {
        id: 2, parent: 1, label: "Show summary".into(), kind: CommandKind::Action,
        enabled: true, checked: None, shortcut_hint: String::new(), pin_label: String::new(),
    },
])?;
let output = status.downgrade();
menu.on_invoke(move |id| {
    if id == 2 && let Some(output) = output.upgrade() {
        output.set_text("Summary requested")?;
    }
    Ok(())
})?;
root.add(&menu, 0.0)?;
root.add(&status, 0.0)?;
```

{% endtab %}
{% tab title="C++" %}

```cpp
auto status = std::make_shared<xui::Label>(L"Ready");
auto menu = std::make_shared<xui::MenuBar>(L"Document menu");
menu->set_commands(std::make_shared<const xui::CommandSet>(
    std::vector<xui::CommandRecord>{
        {.id = 1, .label = L"File", .kind = xui::CommandKind::submenu},
        {.id = 2, .parent = 1, .label = L"Show summary",
            .action = [status] { status->set_text(L"Summary requested"); }}
    }));
root->add(menu);
root->add(status);
```

{% endtab %}
{% endtabs %}

Command snapshots retain stable IDs, enabled states, hierarchy, and separate pin actions.
Malformed snapshots leave the previous commands intact.
C# exposes `Invoked` and `Pinned`. The `.xui` arguments are `invoke` and `pin`.
Rust exposes `on_invoke` and `on_pin`.
Rust callback registration replaces the previous subscription.
A single `on_event` handler can process both event kinds.

`Invoke` or `invoke` requests an action by command ID.
`Bind` or `bind` registers a keyboard shortcut. Shortcut hint text alone does not register one.
The [command contract](../commands-and-navigation.md) defines snapshot and popup limits.

MenuBar uses the `command_bar` style target.
Its retained headings use Button styles, and submenu content uses CommandMenu styles.
A MenuBar does not acquire CommandBar overflow behavior through a shared style target.

F10 enters or exits the menu bar. Alt plus a heading mnemonic opens that heading.
Arrows navigate the menu structure, and Down or Enter opens a heading submenu.
Escape returns to the heading, then to the previous focus.
Tab exits the menu bar. Outside input dismisses the menu.

## CommandMenu

Use `CommandMenu` to compose a virtual menu directly in C++.
Use CommandSurface for window-managed popup presentation and dismissal.

{% tabs %}
{% tab title=".xui" %}

CommandMenu has no markup or C# factory. The supported alternative is a CommandSurface with its retained menu.
The facade Element represents the actual Popup root.

```text
namespace ControlExamples;
component DocumentMenuPopup {
    param global::Xui.Element Surface;
    view {
        Content(Surface);
    }
}
```

C# setup:

```csharp
var surface = window.CommandSurface("Document commands")
    .SetCommands([new(1, "Show summary")]);
surface.OnCommand((id, pin) => {
    if (id == 1 && !pin) window.SetTitle("Summary");
});
surface.Menu.SetControlStyleValues(StylePart.PrimaryText, new PartStyleValues { FontSize = 14 });
var component = new ControlExamples.DocumentMenuPopup(window, surface, attach: false);
anchor.Click += () => surface.Show(anchor);
```

`Show` mounts the Popup root. Do not mount `surface.Menu` or another retained child separately.

{% endtab %}
{% tab title="C#" %}

There is no standalone CommandMenu factory. This example uses the retained menu of a CommandSurface.

```csharp
var surface = window.CommandSurface("Document commands")
    .SetCommands([new(1, "Show summary")]);
surface.OnCommand((id, pin) => {
    if (id == 1 && !pin) window.SetTitle("Summary");
});
surface.Menu.SetControlStyleValues(StylePart.PrimaryText, new PartStyleValues { FontSize = 14 });
anchor.Click += () => surface.Show(anchor);
```

{% endtab %}
{% tab title="Rust" %}

There is no standalone CommandMenu factory. This example uses the retained menu of a CommandSurface.
The wrapper has no Window title setter, so the action updates a Label.

```rust
let surface = window.command_surface("Document commands")?;
surface.set_commands(&[Command {
    id: 1, parent: 0, label: "Show summary".into(), kind: CommandKind::Action,
    enabled: true, checked: None, shortcut_hint: String::new(), pin_label: String::new(),
}])?;
surface.menu()?.set_control_style_values(StylePart::PrimaryText, PartStyleValues {
    font_size: Some(14.), ..Default::default()
})?;
let status = window.label("No command selected")?;
let weak_status = status.downgrade();
surface.on_event(move |event| {
    if event.kind == 1 && event.value == 1 && let Some(status) = weak_status.upgrade() {
        status.set_text("Summary")
            .inspect_err(|error| eprintln!("Command action: {error}"))?;
    }
    Ok(())
})?;
let weak_surface = surface.weak();
let weak_anchor = anchor.downgrade();
anchor.on_event(move |event| {
    if event.kind == 1
        && let (Some(surface), Some(anchor)) = (weak_surface.upgrade(), weak_anchor.upgrade())
    {
        surface.show(&anchor)
            .inspect_err(|error| eprintln!("Show commands: {error}"))?;
    }
    Ok(())
})?;
root.add(&status, 0.)?;
Ok(())
```

{% endtab %}
{% tab title="C++" %}

```cpp
auto commands = std::make_shared<const xui::CommandSet>(
    std::vector<xui::CommandRecord>{
        {1, 0, L"Show summary", [&window] { window.set_title(L"Summary"); }}
    });
auto menu = std::make_shared<xui::CommandMenu>(L"Document commands");
menu->set_commands(commands);
root->add(menu, 1);
```

{% endtab %}
{% endtabs %}

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

{% tabs %}
{% tab title=".xui" %}

CommandBar has no markup constructor.

```text
namespace ControlExamples;
component ViewCommandRow {
    param global::Xui.Element Bar;
    view {
        VStack() {
            Content(Bar);
        }
    }
}
```

C# setup:

```csharp
var bar = window.CommandBar("View commands")
    .SetCommands([new(1, "Summary"), new(2, "Details")]);
bar.Event += e => {
    if (e.Kind == EventKind.Click)
        window.SetTitle(e.Value == 1 ? "Summary" : "Details");
};
var component = new ControlExamples.ViewCommandRow(window, bar);
```

{% endtab %}
{% tab title="C#" %}

```csharp
var bar = window.CommandBar("View commands")
    .SetCommands([new(1, "Summary"), new(2, "Details")]);
bar.Event += e => {
    if (e.Kind == EventKind.Click)
        window.SetTitle(e.Value == 1 ? "Summary" : "Details");
};
root.Add(bar);
```

{% endtab %}
{% tab title="Rust" %}

The wrapper has no Window title setter, so these commands update a Label.

```rust
let bar = window.command_bar("View commands")?;
bar.set_commands(&[
    Command {
        id: 1, parent: 0, label: "Summary".into(), kind: CommandKind::Action,
        enabled: true, checked: None, shortcut_hint: String::new(), pin_label: String::new(),
    },
    Command {
        id: 2, parent: 0, label: "Details".into(), kind: CommandKind::Action,
        enabled: true, checked: None, shortcut_hint: String::new(), pin_label: String::new(),
    },
])?;
let status = window.label("No view selected")?;
let weak_status = status.downgrade();
bar.on_event(move |event| {
    if event.kind == 1 && let Some(status) = weak_status.upgrade() {
        status.set_text(if event.value == 1 { "Summary" } else { "Details" })
            .inspect_err(|error| eprintln!("View command: {error}"))?;
    }
    Ok(())
})?;
root.add(&bar, 0.)?;
root.add(&status, 0.)?;
Ok(())
```

{% endtab %}
{% tab title="C++" %}

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

{% endtab %}
{% endtabs %}

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

{% tabs %}
{% tab title=".xui" %}

CommandSurface has no markup constructor. Its Element handle represents the actual Popup root.

```text
namespace ControlExamples;
component DocumentActionsPopup {
    param global::Xui.Element Surface;
    view {
        Content(Surface);
    }
}
```

C# setup:

```csharp
var surface = window.CommandSurface("Document actions")
    .SetCommands([new(1, "Show summary")]);
surface.OnCommand((id, pin) => {
    if (id == 1 && !pin) window.SetTitle("Summary");
});
var component = new ControlExamples.DocumentActionsPopup(window, surface, attach: false);
anchor.Click += () => surface.Show(anchor);
```

`Show` mounts the Popup root. Do not add the component root or retained children to a Stack.
The C# factory has no nonsearchable mode or query request/completion service.

{% endtab %}
{% tab title="C#" %}

```csharp
var surface = window.CommandSurface("Document actions")
    .SetCommands([new(1, "Show summary")]);
surface.OnCommand((id, pin) => {
    if (id == 1 && !pin) window.SetTitle("Summary");
});
anchor.Click += () => surface.Show(anchor);
```

The factory has no nonsearchable mode or query request/completion service.

{% endtab %}
{% tab title="Rust" %}

The wrapper has no nonsearchable mode, query request/completion service, or Window title setter.
The action updates a Label instead.

```rust
let surface = window.command_surface("Document actions")?;
surface.set_commands(&[Command {
    id: 1, parent: 0, label: "Show summary".into(), kind: CommandKind::Action,
    enabled: true, checked: None, shortcut_hint: String::new(), pin_label: String::new(),
}])?;
let status = window.label("No command selected")?;
let weak_status = status.downgrade();
surface.on_event(move |event| {
    if event.kind == 1 && event.value == 1 && let Some(status) = weak_status.upgrade() {
        status.set_text("Summary")
            .inspect_err(|error| eprintln!("Command action: {error}"))?;
    }
    Ok(())
})?;
let weak_surface = surface.weak();
let weak_anchor = anchor.downgrade();
anchor.on_event(move |event| {
    if event.kind == 1
        && let (Some(surface), Some(anchor)) = (weak_surface.upgrade(), weak_anchor.upgrade())
    {
        surface.show(&anchor)
            .inspect_err(|error| eprintln!("Show command surface: {error}"))?;
    }
    Ok(())
})?;
root.add(&status, 0.)?;
Ok(())
```

{% endtab %}
{% tab title="C++" %}

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

{% endtab %}
{% endtabs %}

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

{% tabs %}
{% tab title=".xui" %}

Shortcut registration belongs in C#. This component embeds the command owner, not a separate CommandSet.

```text
namespace ControlExamples;
component OpenCommandShortcut {
    param global::Xui.Element Bar;
    view {
        VStack() {
            Content(Bar);
        }
    }
}
```

C# setup:

```csharp
var bar = window.CommandBar("Document shortcuts")
    .SetCommands([new(1, "Open", ShortcutHint: "Ctrl+O")])
    .Bind(1, 0x4F, KeyModifiers.Control);
bar.Event += e => {
    if (e.Kind == EventKind.Click && e.Value == 1) window.SetTitle("Open requested");
};
var component = new ControlExamples.OpenCommandShortcut(window, bar);
```

{% endtab %}
{% tab title="C#" %}

`Bind` registers the shortcut on the command owner. There is no standalone CommandBindings wrapper.

```csharp
var bar = window.CommandBar("Document shortcuts")
    .SetCommands([new(1, "Open", ShortcutHint: "Ctrl+O")])
    .Bind(1, 0x4F, KeyModifiers.Control);
bar.Event += e => {
    if (e.Kind == EventKind.Click && e.Value == 1) window.SetTitle("Open requested");
};
root.Add(bar);
```

{% endtab %}
{% tab title="Rust" %}

`bind` registers the shortcut on the command owner. The action updates a Label because there is no Window title setter.

```rust
let bar = window.command_bar("Document shortcuts")?;
bar.set_commands(&[Command {
    id: 1, parent: 0, label: "Open".into(), kind: CommandKind::Action,
    enabled: true, checked: None, shortcut_hint: "Ctrl+O".into(), pin_label: String::new(),
}])?;
bar.bind(1, 0x4F, KeyModifiers { control: true, shift: false, alt: false })?;
let status = window.label("No open request")?;
let weak_status = status.downgrade();
bar.on_event(move |event| {
    if event.kind == 1 && event.value == 1 && let Some(status) = weak_status.upgrade() {
        status.set_text("Open requested")
            .inspect_err(|error| eprintln!("Open shortcut: {error}"))?;
    }
    Ok(())
})?;
root.add(&bar, 0.)?;
root.add(&status, 0.)?;
Ok(())
```

{% endtab %}
{% tab title="C++" %}

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

{% endtab %}
{% endtabs %}

The event handler returns true only for an accepted command.
Native text editors retain character input and editing behavior.
The [input contract](../menus-and-input.md) defines routing and reserved keys.

## Native menus

Use `on_context_menu` for a flat Windows menu with explicit actions.
The framework closes the menu before the selected callback runs.

{% tabs %}
{% tab title=".xui" %}

The compiler has no native context-menu declaration.
The C# binding has no TextInput context-menu override or clipboard-copy method for this example.
Native TextInput keeps its system context menu.
`DataGrid.OnContextMenu` is a separate supported C# surface, not a general Control override.

{% endtab %}
{% tab title="C#" %}

This TextInput context-menu override is unavailable.
The binding only exposes application context-menu callbacks through `DataGrid.OnContextMenu`.
Native TextInput keeps its system context menu.

{% endtab %}
{% tab title="Rust" %}

This TextInput context-menu override is unavailable in the safe wrapper.
Native TextInput keeps its system context menu.
`show_shell_commands` supports an explicit Shell menu, as shown next.

{% endtab %}
{% tab title="C++" %}

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

{% endtab %}
{% endtabs %}

`&` marks a mnemonic. `&&` shows a literal ampersand.
Shortcut text after a tab character does not register a shortcut.
Without an override, native EDIT retains its system context menu.
The override does not replace its native text provider.

For a user-requested Shell menu:

{% tabs %}
{% tab title=".xui" %}

Shell menu presentation belongs to the Window service, not a markup control.

```text
namespace ControlExamples;
component ShellMenuAction {
    view {
        VStack() {
            Button("File commands", ref: Open);
        }
    }
}
```

C# setup:

```csharp
var component = new ControlExamples.ShellMenuAction(window);
component.Open.Click += () => window.ShowShellCommands(component.Open, [@"C:\Data\Notes.txt"]);
```

{% endtab %}
{% tab title="C#" %}

```csharp
anchor.Click += () => window.ShowShellCommands(anchor, [@"C:\Data\Notes.txt"]);
```

{% endtab %}
{% tab title="Rust" %}

```rust
let weak_window = window.downgrade();
let weak_anchor = anchor.downgrade();
anchor.on_event(move |event| {
    if event.kind == 1
        && let (Some(window), Some(anchor)) = (weak_window.upgrade(), weak_anchor.upgrade())
    {
        window.show_shell_commands(&anchor, &[r"C:\Data\Notes.txt"])
            .inspect_err(|error| eprintln!("Show Shell commands: {error}"))?;
    }
    Ok(())
})?;
Ok(())
```

{% endtab %}
{% tab title="C++" %}

```cpp
anchor->on_click([&window, &anchor] {
    window.show_shell_commands(*anchor, {L"C:\\Data\\Notes.txt"});
});
```

{% endtab %}
{% endtabs %}

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

{% tabs %}
{% tab title=".xui" %}

CustomShellMenu is a C++-only service facade, not an Element or a markup constructor.
The Content bridge cannot embed it.
Use the Window Shell menu bridge in the preceding example.

{% endtab %}
{% tab title="C#" %}

There is no CustomShellMenu factory or ShellCommandProvider wrapper.
`Window.ShowShellCommands` supports the explicit Windows Shell menu without this custom facade.

{% endtab %}
{% tab title="Rust" %}

There is no CustomShellMenu factory or ShellCommandProvider wrapper.
`Window::show_shell_commands` supports the explicit Windows Shell menu without this custom facade.

{% endtab %}
{% tab title="C++" %}

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

{% endtab %}
{% endtabs %}

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

{% tabs %}
{% tab title=".xui" %}

Window creation and services belong to C#, not the markup tree.

```text
namespace ControlExamples;
component WorkspaceWindowContent {
    view {
        VStack() {
            Text("Workspace");
            Button("Close", ref: Close);
        }
    }
}
```

C# setup:

```csharp
window.SetTitle("Workspace").SetTheme(Theme.Dark).SetVisualStyle(VisualStyle.WinUI);
var component = new ControlExamples.WorkspaceWindowContent(window);
component.Close.Click += () => window.Close();
```

{% endtab %}
{% tab title="C#" %}

```csharp
window.SetTitle("Workspace").SetTheme(Theme.Dark).SetVisualStyle(VisualStyle.WinUI);
anchor.Click += () => window.Close();
```

{% endtab %}
{% tab title="Rust" %}

The wrapper supports theme changes and closure, but no title or visual-style setter.
The title comes from `Window::new` at construction.

```rust
window.set_theme(Theme::Dark)?;
let weak_window = window.downgrade();
anchor.on_event(move |event| {
    if event.kind == 1 && let Some(window) = weak_window.upgrade() {
        window.close()
            .inspect_err(|error| eprintln!("Close window: {error}"))?;
    }
    Ok(())
})?;
Ok(())
```

{% endtab %}
{% tab title="C++" %}

```cpp
window.set_title(L"Workspace");
window.set_theme(xui::ThemeMode::dark);
window.set_visual_style(xui::VisualStyle::winui);
anchor->on_click([&window] { window.close(); });
```

{% endtab %}
{% endtabs %}

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

{% tabs %}
{% tab title=".xui" %}

TitleBar has no markup constructor. The window owns it and its retained children.
This component supplies only the window content.

```text
namespace ControlExamples;
component CaptionWorkspace {
    param global::Xui.Element Body;
    view {
        VStack() {
            Content(Body);
        }
    }
}
```

This C# function-body fragment creates and runs a separate window.
The component attaches its Stack root. The title-bar tabs remain window-owned.

```csharp
using var captionWindow = new Window("Workspace", customTitlebar: true);
var content = captionWindow.Label("Workspace content");
var component = new ControlExamples.CaptionWorkspace(captionWindow, content);
captionWindow.TitlebarTabs.SetTabs([new(1, "Home"), new(2, "Documents")], 1);
captionWindow.Run();
```

{% endtab %}
{% tab title="C#" %}

This function-body fragment creates and runs a separate window.

```csharp
using var captionWindow = new Window("Workspace", customTitlebar: true);
var content = captionWindow.Stack().Add(captionWindow.Label("Workspace content"));
captionWindow.TitlebarTabs.SetTabs([new(1, "Home"), new(2, "Documents")], 1);
captionWindow.SetContent(content);
captionWindow.Run();
```

There is no standalone TitleBar factory. The accessor returns the window-owned element.

{% endtab %}
{% tab title="Rust" %}

This function-body fragment creates and runs a separate window.

```rust
let caption_window = Window::with_titlebar("Workspace", 600., 720.)?;
let content = caption_window.stack(Axis::Vertical)?;
let heading = caption_window.label("Workspace content")?;
content.add(&heading, 0.)?;
caption_window.titlebar_tabs()?.set_items(&[
    Choice { id: 1, version: 0, text: "Home".into(), enabled: true },
    Choice { id: 2, version: 0, text: "Documents".into(), enabled: true },
], Some(1))?;
caption_window.set_content(&content)?;
caption_window.run()?;
Ok(())
```

There is no standalone TitleBar factory. The accessor returns the window-owned element.

{% endtab %}
{% tab title="C++" %}

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

{% endtab %}
{% endtabs %}

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

{% tabs %}
{% tab title=".xui" %}

The compiler rejects Tooltip declarations. Help text belongs to the control, and tooltip styles use the Window bridge.

```text
namespace ControlExamples;
component DocumentActionHelp {
    view {
        VStack() {
            Button("Actions", ref: Action, help: "Open the document actions.");
        }
    }
}
```

C# setup:

```csharp
var component = new ControlExamples.DocumentActionHelp(window);
component.Action.TooltipDelay(600);
window.SetTooltipStyleValues(StylePart.Text, new PartStyleValues { FontSize = 14 });
```

{% endtab %}
{% tab title="C#" %}

```csharp
anchor.Help("Open the document actions.").TooltipDelay(600);
window.SetTooltipStyleValues(StylePart.Text, new PartStyleValues { FontSize = 14 });
```

{% endtab %}
{% tab title="Rust" %}

```rust
anchor.help("Open the document actions.")?;
anchor.tooltip_delay(600)?;
window.set_tooltip_style_values(StylePart::Text, PartStyleValues {
    font_size: Some(14.), ..Default::default()
})?;
Ok(())
```

{% endtab %}
{% tab title="C++" %}

```cpp
anchor->set_help_text(L"Open the document actions.");
anchor->set_tooltip_delay(600);
xui::PartStyleValues text;
text.font_size = 14.0f;
window.set_tooltip_style_values(xui::StylePart::text, text);
```

{% endtab %}
{% endtabs %}

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
