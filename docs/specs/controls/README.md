# Control guides

These guides explain how to choose, create, and connect the current XUI controls.
They complement the [public contracts](../README.md), which define behavior and limits.

## Choose a guide

| Task | Guide |
| --- | --- |
| Show text, accept a command, or edit one line | [Basic controls](basic.md) |
| Arrange content, scroll, or switch panes | [Layout and workspace](layout.md) |
| Choose an item, edit a number, or show progress | [Choices and values](choices.md) |
| Edit documents, collect form values, or open a dialog | [Documents and forms](documents.md) |
| Show large lists, tables, trees, or history | [Collections](collections.md) |
| Navigate pages, paths, and locations | [Navigation](navigation.md) |
| Share commands, open menus, or customize window chrome | [Commands and windows](commands.md) |
| Show images, scenes, maps, media, or web content | [Images and native hosts](media.md) |

## Use the examples

Each example has tabs in this order: **.xui**, **C#**, **Rust**, **C++**.
The first tab shows the declarative path.
Some controls require C# construction and the `Content(...)` bridge rather than a dedicated `.xui` node.
Those tabs include the required setup.
An unavailable API has an explicit support note instead of a substitute with different behavior.

Each example is independent. The examples do not form one concatenated application.
Use the matching context below unless a guide supplies a complete application or additional source parameters.

{% tabs %}
{% tab title=".xui" %}

A `.xui` tab contains a complete component.
Save this example as `GuidePanel.xui`:

```xui
namespace ControlExamples;

component GuidePanel {
    view {
        VStack(spacing: 8, padding: 16) {
            Text("Ready");
        }
    }
}
```

Create the component from an STA entry point:

```csharp
using System;
using Xui;

internal static class Program
{
    [STAThread]
    private static int Main()
    {
        try
        {
            using var window = new Window("Control example", 800, 600);
            _ = new ControlExamples.GuidePanel(window);
            window.Run();
            return 0;
        }
        catch (Exception error)
        {
            Console.Error.WriteLine(error);
            return 1;
        }
    }
}
```

Replace component creation with the selected guide's constructor and any required C# setup.
The component attaches its own Stack root by default.
An element passed to `Content(...)` must belong to this window and must not already have a parent.
The [declarative guide](../languages/declarative.md#project-integration) describes project integration.

{% endtab %}
{% tab title="C#" %}

C# fragments use `window`, `root`, and `anchor` from this context.
Put complete helper types at file scope.

```csharp
using System;
using Xui;

internal static class Program
{
    [STAThread]
    private static int Main()
    {
        try
        {
            using var window = new Window("Control example", 800, 600);
            var root = window.Stack().Padding(16).Spacing(8);
            var anchor = window.Button("Open");
            root.Add(anchor);
            // Insert one C# guide fragment here.
            window.SetContent(root);
            window.Run();
            return 0;
        }
        catch (Exception error)
        {
            Console.Error.WriteLine(error);
            return 1;
        }
    }
}
```

The window disposes its owned controls after `Run` returns.
The [C# guide](../languages/csharp.md#project-integration) describes the managed reference, manifest, and native DLL.

{% endtab %}
{% tab title="Rust" %}

Rust fragments use `window`, `root`, and `anchor` from this context.
Question marks propagate native errors to the caller.
Replace the body of `example` with the selected fragment.
The three parameters are borrowed references.

```rust
use xui::*;

fn example(
    window: &Window,
    root: &Stack,
    anchor: &Button,
) -> std::result::Result<(), Box<dyn std::error::Error>> {
    let _ = (window, root, anchor);
    // Replace this body with one Rust guide fragment.
    Ok(())
}

fn main() -> std::result::Result<(), Box<dyn std::error::Error>> {
    let window = Window::new("Control example", 800., 600.)?;
    let root = window.stack(Axis::Vertical)?;
    root.padding(16.)?;
    root.spacing(8.)?;
    let anchor = window.button("Open")?;
    root.add(&anchor, 0.)?;
    example(&window, &root, &anchor)?;
    window.set_content(&root)?;
    window.run()?;
    Ok(())
}
```

Use weak control or Window captures for callbacks that the same native owner retains.
The [Rust guide](../languages/rust.md#ownership-and-callback-cycles) explains ownership and callback errors.

{% endtab %}
{% tab title="C++" %}

C++ fragments use `window`, `root`, and `anchor` from this context:

```cpp
#include "xui\application.hpp"

int run_example() {
    xui::Window window({L"Control example", {800, 600}});
    auto root = std::make_shared<xui::Stack>(xui::Axis::vertical);
    root->set_padding({16, 16, 16, 16});
    root->set_spacing(8);
    auto anchor = std::make_shared<xui::Button>(L"Open");
    root->add(anchor);
    // Insert one guide fragment here, with its additional headers.
    window.set_content(root);
    return xui::Application::run(window);
}
```

Put additional includes at file scope, not inside `run_example`.
Each guide identifies any extra source parameter.
The anchor already belongs to the root.
Popup examples open only from its callback, after the window starts.
All referenced local variables remain alive during `Application::run`.

{% endtab %}
{% endtabs %}

For executable setup, use [application composition](../application.md#application-example).
For binding setup and coverage, use the [binding guide](../bindings.md#examples).
For declarative components, use the [`.xui` language guide](../xui-language.md).

### Shared ownership and input rules

- Each retained child has one layout parent. Null children, duplicate parents, and cycles are invalid.
- The window retains its content. C++ controls use `std::shared_ptr`.
- UI properties and callbacks belong to the creating UI thread.
- Worker callbacks must deliver completion through a UI queue, such as C++ `Window::post`.
- Callbacks must not outlive their captures. Strong references back to an owning control can create cycles.
- Native editors retain Windows text input, IME, selection, clipboard, and undo behavior.
- `Window::focus` requests application focus. Backend state setters do not replace this API.
- Most property setters are silent. Semantic actions and collection selection have their documented event contracts.
- Accessible names must describe the purpose, including icon-only buttons and nonvisual alternatives.

C# controls belong to a disposable `Window`.
Rust controls retain their window owner and are not `Send` or `Sync`.
Borrowed children do not support independent destruction.
The [binding ownership contract](../bindings.md#ownership-and-data-limits) defines callback, snapshot, and request-token lifetimes.

## Complete control catalog

The table separates public types, retained children, and presentations.
Factory names refer to methods on the C# or Rust `Window`, unless the cell says otherwise.
Availability does not imply identical properties across languages.
The [binding reference](../bindings.md) defines the exact surface.

| Public control or API | Guide section | C# entry | Rust entry |
| --- | --- | --- | --- |
| `Element`, `Control` base APIs | [Common properties](basic.md#common-properties) | Base wrappers, no factory | Base wrappers, no factory |
| `Label` | [Label](basic.md#label) | `Label` | `label` |
| `Button` and behavior variants | [Button](basic.md#button) | `Button` | `button` |
| `HyperlinkButton` | [HyperlinkButton](basic.md#hyperlinkbutton) | `HyperlinkButton` | `hyperlink_button` |
| `Toggle` | [Toggle](basic.md#toggle) | `Toggle` | `toggle` |
| `CheckBox` | [CheckBox](basic.md#checkbox) | `CheckBox` | `check_box` |
| `InfoBadge` | [InfoBadge](basic.md#infobadge) | `InfoBadge` | `info_badge` |
| `ToggleSwitch` | [ToggleSwitch](basic.md#toggleswitch) | `ToggleSwitch` | `toggle_switch` |
| `ToggleButton` | [ToggleButton](basic.md#togglebutton) | `ToggleButton` | `toggle_button` |
| `TextInput` and search variant | [TextInput](basic.md#textinput) | `TextInput` | `text_input` |
| `NativeEditBridge` backend boundary | [NativeEditBridge](basic.md#nativeeditbridge) | No factory | No factory |
| `Stack` | [Stack](layout.md#stack) | `Stack` | `stack` |
| `ContentHost` | [ContentHost](layout.md#contenthost) | `CreateContentHost` | No typed wrapper |
| `Grid` | [Grid](layout.md#grid) | `Grid` | `grid` |
| `Wrap` | [Wrap](layout.md#wrap) | `Wrap` | `wrap` |
| `AdaptiveLayout` | [AdaptiveLayout](layout.md#adaptivelayout) | `AdaptiveLayout` | `adaptive_layout` |
| `ContentView` | [ContentView](layout.md#contentview) | No standalone factory | No standalone factory |
| `Reveal` | [Reveal](layout.md#reveal) | `Reveal` | `reveal` |
| `ScrollView` | [ScrollView](layout.md#scrollview) | `ScrollView` | `scroll_view` |
| `SplitView` | [SplitView](layout.md#splitview) | `SplitView` | `split_view` |
| `PageView` | [PageView](layout.md#pageview) | `PageView` | `page_view` |
| `TabStrip` | [TabStrip](layout.md#tabstrip) | `TabStrip` | `tab_strip` |
| `RadioGroup` | [RadioGroup](choices.md#radiogroup) | `RadioGroup` | `radio_group` |
| `SelectorBar` | [SelectorBar](choices.md#selectorbar) | `SelectorBar` | `selector_bar` |
| `ChoiceList` presentation | [ChoiceList](choices.md#choicelist) | Borrowed `ComboBox.Choices` | Borrowed `ComboBox::choices` |
| `ComboBox` | [ComboBox](choices.md#combobox) | `ComboBox` | `combo_box` |
| `NumericInput` | [NumericInput](choices.md#numericinput) | `NumericInput` | `numeric_input` |
| `RangeInput` | [RangeInput](choices.md#rangeinput) | `RangeInput` | `range_input` |
| `Progress` | [Progress](choices.md#progress) | `Progress` | `progress` |
| `ProgressRing` | [ProgressRing](choices.md#progressring) | `ProgressRing` | `progress_ring` |
| `Expander` | [Expander](choices.md#expander) | `Expander` | `expander` |
| `SplitButton` | [SplitButton](choices.md#splitbutton) | `SplitButton` | `split_button` |
| `Popup` | [Popup](choices.md#popup) | `Popup` | `popup` |
| `MultilineText` | [MultilineText](documents.md#multilinetext) | `MultilineText` | `multiline_text` |
| `RichText` | [RichText](documents.md#richtext) | `RichText` | `rich_text` |
| `PasswordInput` | [PasswordInput](documents.md#passwordinput) | `PasswordInput` | `password_input` |
| `DateTimePicker` variants | [DateTimePicker](documents.md#datetimepicker) | `DateTimePicker` | `date_time_picker` |
| `InlineStatus` | [InlineStatus](documents.md#inlinestatus) | `InlineStatus` | `inline_status` |
| `ColorPicker` | [ColorPicker](documents.md#colorpicker) | `ColorPicker` | `color_picker` |
| `ContentDialog` facade | [ContentDialog](documents.md#contentdialog) | `ContentDialog` | `content_dialog` |
| `FileList` | [FileList](collections.md#filelist) | `FileList` | `file_list` |
| `ItemsView` variants | [ItemsView](collections.md#itemsview) | `ItemsView` | `items_view` |
| `TreeView` | [TreeView](collections.md#treeview) | `TreeView` | `tree_view` |
| `MillerColumns` | [Miller columns](collections.md#millercolumns) | `MillerColumns` | `miller_columns`, construction only |
| `MillerColumnList` retained child | [Miller columns](collections.md#millercolumns) | Borrowed `MillerColumns.Column(index)` as `ItemsView` | No typed child accessor |
| `DataGrid` | [DataGrid](collections.md#datagrid) | `DataGrid` | `data_grid` |
| `HistoryChart` | [HistoryChart](collections.md#historychart) | `HistoryChart` | `history_chart` |
| `NavigationView` | [NavigationView](navigation.md#navigationview) | `NavigationView` | `navigation_view` |
| `NavigationList` retained child | [NavigationList](navigation.md#navigationlist) | Retained `NavigationView` children | Retained `NavigationView` children |
| `Breadcrumb` | [Breadcrumb](navigation.md#breadcrumb) | `Breadcrumb` | `breadcrumb` |
| `NavigationPane` | [NavigationPane](navigation.md#navigationpane) | `NavigationPane` | `navigation_pane` |
| `LocationPicker` facade | [LocationPicker](navigation.md#locationpicker) | `LocationPicker` | `location_picker` |
| `ViewPicker` facade | [ViewPicker](navigation.md#viewpicker) | `ViewPicker` | `view_picker` |
| `CommandMenu` | [CommandMenu](commands.md#commandmenu) | Borrowed `CommandSurface.Menu`, no factory | Borrowed `CommandSurface::menu`, no factory |
| `CommandBar` | [CommandBar](commands.md#commandbar) | `CommandBar` | `command_bar` |
| `MenuBar` | [MenuBar](commands.md#menubar) | `MenuBar` | `menu_bar` |
| `CommandSurface` facade | [CommandSurface](commands.md#commandsurface) | `CommandSurface` | `command_surface` |
| Native context menus and Shell services | [Native menus](commands.md#native-menus) | [Binding contracts](../bindings.md) | [Binding contracts](../bindings.md) |
| `CustomShellMenu` facade | [CustomShellMenu](commands.md#customshellmenu) | No standalone factory | No standalone factory |
| `Window` | [Window](commands.md#window) | `new Window` | `Window::new` |
| `TitleBar` | [TitleBar](commands.md#titlebar) | `Window.Titlebar` and child accessors | `Window::titlebar` and child accessors |
| Tooltip API | [Tooltips](commands.md#tooltips) | `Window.SetTooltipStyle` | `Window::set_tooltip_style` |
| `Image` | [Image](media.md#image) | `Image` | `image` |
| `VectorCanvas` | [VectorCanvas](media.md#vectorcanvas) | `VectorCanvas` | `vector_canvas` |
| `MapView` | [MapView](media.md#mapview) | `MapView` | `map_view` |
| `MediaPlayback` | [MediaPlayback](media.md#mediaplayback) | `MediaPlayback` | `media_playback` |
| `WebContent` | [WebContent](media.md#webcontent) | `WebContent` | `web_content` |

`Control`, `DocumentText`, `VirtualCollection`, and `RuntimeHost` provide shared behavior, not public standalone constructors.
`Element` is constructible in C++, but it has no control behavior or style target.
`NativeEditBridge` is a public Windows backend boundary, not a normal application-tree control.
`NavigationList` has a private constructor and belongs to `NavigationView`.
`MillerColumnList` has a private constructor and belongs to `MillerColumns`.
`ChoiceList` is a `RadioGroup` presentation, not a C++ class.
`ContentDialog`, `CommandSurface`, `LocationPicker`, and `ViewPicker` expose real Popup roots.
Tooltip has no control constructor.

Models and services are not additional controls.
Examples include `ItemsSource`, `GridSource`, `TreeSource`, `CollectionSelection`, `CommandSet`, `CommandBindings`, `Commands`, and `ImageResources`.
`ViewTask` and `SampleTask` supply work delivery, not visual elements.
The linked contracts describe those companion APIs.

## Language and styling boundaries

The declarative language has 25 built-in nodes.
Layout and composition use `VStack`, `HStack`, `Grid`, `ScrollView`, `Popup`, `SplitView`, `Reveal`, and `Content`.
Basic controls use `Text`, `Button`, `Toggle`, `ToggleSwitch`, `ToggleButton`, `CheckBox`, `HyperlinkButton`, `InfoBadge`, and `TextInput`.
Other forms are `DataGrid`, `NavigationView`, `ItemsView`, `RangeInput`, `Progress`, `ProgressRing`, `SelectorBar`, and `MenuBar`.
`Content` mounts an existing element. It does not add a new native control class.
Other bound controls require C# creation and explicit composition.
The [language guide](../xui-language.md) defines that workflow.

The style catalog contains **46 targets and 223 target-part entries**.
These numbers do not count public constructors or declarative nodes.
Some targets describe retained children or presentations.
Other public facades use the Popup target instead of a facade target.
ToggleSwitch, ToggleButton, and ProgressRing reuse the Toggle, Button, and Progress targets.
CheckBox, HyperlinkButton, SelectorBar, InfoBadge, and MenuBar reuse Toggle, Button, ChoiceList, InlineStatus, and CommandBar targets, respectively.

Styles change supported properties and named parts.
They do not replace retained children or native behavior.
There are no shipped control templates, item templates, or implicit selectors.
Shell HMENU appearance does not gain a style target.
High contrast retains system colors and visible focus.

Use the [styling contract](../control-styling.md) for syntax, precedence, and diagnostics.
Use the [styling inventory](../control-styling-inventory.md) for exact parts and native boundaries.
Use [retained child accessors](../bindings.md#retained-composition-children) for C# and Rust paths.
