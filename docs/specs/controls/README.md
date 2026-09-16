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

The C++ examples use the public headers.
Except for complete class definitions, each block is an independent function-body fragment.
The examples do not form one concatenated application.

Use this context for the fragments:

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

For executable setup, use [application composition](../application.md#application-example).
For C# and Rust examples, use the [binding guide](../bindings.md#examples).
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
| `Toggle` | [Toggle](basic.md#toggle) | `Toggle` | `toggle` |
| `TextInput` and search variant | [TextInput](basic.md#textinput) | `TextInput` | `text_input` |
| `NativeEditBridge` backend boundary | [NativeEditBridge](basic.md#nativeeditbridge) | No factory | No factory |
| `Stack` | [Stack](layout.md#stack) | `Stack` | `stack` |
| `Grid` | [Grid](layout.md#grid) | `Grid` | `grid` |
| `Wrap` | [Wrap](layout.md#wrap) | `Wrap` | `wrap` |
| `AdaptiveLayout` | [AdaptiveLayout](layout.md#adaptivelayout) | `AdaptiveLayout` | `adaptive_layout` |
| `ContentView` | [ContentView](layout.md#contentview) | No standalone factory | No standalone factory |
| `ScrollView` | [ScrollView](layout.md#scrollview) | `ScrollView` | `scroll_view` |
| `SplitView` | [SplitView](layout.md#splitview) | `SplitView` | `split_view` |
| `PageView` | [PageView](layout.md#pageview) | `PageView` | `page_view` |
| `TabStrip` | [TabStrip](layout.md#tabstrip) | `TabStrip` | `tab_strip` |
| `RadioGroup` | [RadioGroup](choices.md#radiogroup) | `RadioGroup` | `radio_group` |
| `ChoiceList` presentation | [ChoiceList](choices.md#choicelist) | Borrowed `ComboBox.Choices` | Borrowed `ComboBox::choices` |
| `ComboBox` | [ComboBox](choices.md#combobox) | `ComboBox` | `combo_box` |
| `NumericInput` | [NumericInput](choices.md#numericinput) | `NumericInput` | `numeric_input` |
| `RangeInput` | [RangeInput](choices.md#rangeinput) | `RangeInput` | `range_input` |
| `Progress` | [Progress](choices.md#progress) | `Progress` | `progress` |
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
`ChoiceList` is a `RadioGroup` presentation, not a C++ class.
`ContentDialog`, `CommandSurface`, `LocationPicker`, and `ViewPicker` expose real Popup roots.
Tooltip has no control constructor.

Models and services are not additional controls.
Examples include `ItemsSource`, `GridSource`, `TreeSource`, `CollectionSelection`, `CommandSet`, `CommandBindings`, `Commands`, and `ImageResources`.
`ViewTask` and `SampleTask` supply work delivery, not visual elements.
The linked contracts describe those companion APIs.

## Language and styling boundaries

The declarative language has 14 built-in nodes:
`VStack`, `HStack`, `Text`, `Button`, `Toggle`, `TextInput`, `Grid`, `DataGrid`, `NavigationView`, `ItemsView`, `ScrollView`, `Popup`, `SplitView`, and `Content`.
`Content` mounts an existing element. It does not add a new native control class.
Other bound controls require C# creation and explicit composition.
The [language guide](../xui-language.md) defines that workflow.

The style catalog contains **46 targets and 223 target-part entries**.
These numbers do not count public constructors or declarative nodes.
Some targets describe retained children or presentations.
Other public facades use the Popup target instead of a facade target.

Styles change supported properties and named parts.
They do not replace retained children or native behavior.
There are no shipped control templates, item templates, or implicit selectors.
Shell HMENU appearance does not gain a style target.
High contrast retains system colors and visible focus.

Use the [styling contract](../control-styling.md) for syntax, precedence, and diagnostics.
Use the [styling inventory](../control-styling-inventory.md) for exact parts and native boundaries.
Use [retained child accessors](../bindings.md#retained-composition-children) for C# and Rust paths.
