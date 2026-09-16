# Layout and workspace

[Control catalog](README.md) · [Layout contract](../application.md#content-sizes-and-constraints) · [Workspace input](../menus-and-input.md)

Examples use the [language fragment contexts](README.md#use-the-examples).
Add `xui\adaptive_layout.hpp` for Grid, Wrap, and AdaptiveLayout.

## Stack

Use `Stack` for a horizontal row or vertical column.
A positive flex value gives a child a share of remaining main-axis space.

{% tabs %}
{% tab title=".xui" %}

```text
namespace ControlExamples;
component WorkspaceRow {
    view {
        HStack(spacing: 8, padding: 12) {
            Text("Workspace");
            TextInput("Workspace name", flex: 1);
        }
    }
}
```

{% endtab %}
{% tab title="C#" %}

```csharp
var row = window.Stack(Axis.Horizontal).Spacing(8).Padding(12);
row.Add(window.Label("Workspace"));
row.Add(window.TextInput("Workspace name"), 1);
root.Add(row);
```

{% endtab %}
{% tab title="Rust" %}

```rust
let row = window.stack(Axis::Horizontal)?;
row.spacing(8.0)?;
row.padding(12.0)?;
row.add(&*window.label("Workspace")?, 0.0)?;
row.add(&*window.text_input("Workspace name")?, 1.0)?;
root.add(&row, 0.0)?;
```

{% endtab %}
{% tab title="C++" %}

```cpp
auto row = std::make_shared<xui::Stack>(xui::Axis::horizontal);
row->set_spacing(8);
row->set_padding({12, 12, 12, 12});
row->add(std::make_shared<xui::Label>(L"Workspace"));
row->add(std::make_shared<xui::TextInput>(L"Workspace name"), 1);
root->add(row);
```

{% endtab %}
{% endtabs %}

`set_surface` requests a surface.
`set_separator_after` requests a separator after the content.
The style target is `stack`.
Explicit padding and spacing override style values, including zero.
Passive layout roots reject foreground text color.
Child text uses the child style.

## Grid

Use `Grid` for aligned rows and columns.
Use `DataGrid` for virtual data rows, sorting, and selection.
Grid retains each child and does not virtualize cells.

{% tabs %}
{% tab title=".xui" %}

```text
namespace ControlExamples;
component NameForm {
    style FormSpacing for Grid { rowGap: 8; columnGap: 12; }
    view {
        VStack() {
            Grid("Name form", style: FormSpacing,
                rows: [new(global::Xui.TrackSizing.Automatic), new(global::Xui.TrackSizing.Automatic)],
                columns: [new(global::Xui.TrackSizing.Fixed, 140), new(global::Xui.TrackSizing.Star, 1)]) {
                Text("Name", row: 0, column: 0);
                TextInput("Name", row: 0, column: 1);
                Button("Apply", row: 1, column: 0, columnSpan: 2);
            }
        }
    }
}
```

{% endtab %}
{% tab title="C#" %}

The binding supplies gaps through style values, not a structural `SetGap` setter.

```csharp
var form = window.Grid("Name form");
form.SetTracks(
    [new(TrackSizing.Automatic), new(TrackSizing.Automatic)],
    [new(TrackSizing.Fixed, 140), new(TrackSizing.Star, 1)]);
form.SetControlStyleValues(StylePart.Root, new PartStyleValues { ColumnGap = 12, RowGap = 8 });
form.Add(window.Label("Name"), 0, 0);
form.Add(window.TextInput("Name"), 0, 1);
form.Add(window.Button("Apply"), 1, 0, 1, 2);
root.Add(form);
```

{% endtab %}
{% tab title="Rust" %}

The binding supplies gaps through style values, not a structural gap setter.

```rust
let form = window.grid("Name form")?;
form.set_tracks(
    &[GridTrack { sizing: TrackSizing::Automatic, ..Default::default() }; 2],
    &[
        GridTrack { sizing: TrackSizing::Fixed, value: 140.0, ..Default::default() },
        GridTrack::default(),
    ],
)?;
form.set_control_style_values(StylePart::Root, PartStyleValues {
    column_gap: Some(12.0), row_gap: Some(8.0), ..Default::default()
})?;
form.add(&*window.label("Name")?, 0, 0, 1, 1)?;
form.add(&*window.text_input("Name")?, 0, 1, 1, 1)?;
form.add(&*window.button("Apply")?, 1, 0, 1, 2)?;
root.add(&form, 0.0)?;
```

{% endtab %}
{% tab title="C++" %}

```cpp
auto form = std::make_shared<xui::Grid>();
form->set_tracks(
    {{xui::TrackSizing::automatic}, {xui::TrackSizing::automatic}},
    {{xui::TrackSizing::fixed, 140}, {xui::TrackSizing::star, 1}});
form->set_gap(12, 8);
form->add(std::make_shared<xui::Label>(L"Name"), 0, 0);
form->add(std::make_shared<xui::TextInput>(L"Name"), 0, 1);
form->add(std::make_shared<xui::Button>(L"Apply"), 1, 0, 1, 2);
root->add(form);
```

{% endtab %}
{% endtabs %}

`GridTrack` selects fixed, automatic, or weighted space.
The final two `add` arguments specify row and column spans.
The style target is `grid`.
Track definitions, placement, and explicit layout setters remain structural properties.

## Wrap

Use `Wrap` for a small collection of retained controls that changes its column count.
Use `ItemsView` tiles for a large source.

{% tabs %}
{% tab title=".xui" %}

`Wrap` has no markup constructor.
This component requires a C# `Wrap` from the same window, without an existing parent.

```text
namespace ControlExamples;
component FolderCards {
    param global::Xui.Wrap Cards;
    view {
        VStack() {
            Content(Cards);
        }
    }
}
```

C# constructs the cards and passes them to the component:

```csharp
var cards = window.Wrap("Folders").SetItemWidth(180);
cards.SetControlStyleValues(StylePart.Root, new PartStyleValues { Spacing = 12 });
cards.Add(window.Button("Documents"));
cards.Add(window.Button("Pictures"));
cards.Add(window.Button("Downloads"));
var view = new ControlExamples.FolderCards(window, cards);
```

{% endtab %}
{% tab title="C#" %}

The binding supplies spacing through style values.

```csharp
var cards = window.Wrap("Folders").SetItemWidth(180);
cards.SetControlStyleValues(StylePart.Root, new PartStyleValues { Spacing = 12 });
cards.Add(window.Button("Documents"));
cards.Add(window.Button("Pictures"));
cards.Add(window.Button("Downloads"));
root.Add(cards);
```

{% endtab %}
{% tab title="Rust" %}

The binding supplies spacing through style values.

```rust
let cards = window.wrap("Folders")?;
cards.set_item_width(180.0)?;
cards.set_control_style_values(StylePart::Root, PartStyleValues {
    spacing: Some(12.0), ..Default::default()
})?;
cards.add(&*window.button("Documents")?)?;
cards.add(&*window.button("Pictures")?)?;
cards.add(&*window.button("Downloads")?)?;
root.add(&cards, 0.0)?;
```

{% endtab %}
{% tab title="C++" %}

```cpp
auto cards = std::make_shared<xui::Wrap>();
cards->set_item_width(180);
cards->set_spacing(12);
cards->add(std::make_shared<xui::Button>(L"Documents"));
cards->add(std::make_shared<xui::Button>(L"Pictures"));
cards->add(std::make_shared<xui::Button>(L"Downloads"));
root->add(cards);
```

{% endtab %}
{% endtabs %}

`columns()` reports the current layout.
Every child remains retained.
The style target is `wrap`, with frame, separator, padding, and spacing.

## AdaptiveLayout

Use `AdaptiveLayout` for navigation and content that share a row at wide widths.
Compact mode can stack the same children or show a navigation overlay.

{% tabs %}
{% tab title=".xui" %}

`AdaptiveLayout` has no markup constructor.
This component requires a C# layout from the same window, without an existing parent.

```text
namespace ControlExamples;
component AdaptiveWorkspace {
    param global::Xui.AdaptiveLayout Layout;
    view {
        VStack() {
            Button("Open navigation", click: OpenNavigation);
            Content(Layout, flex: 1);
        }
    }
    code csharp {
        void OpenNavigation() => Layout.SetNavigationOpen(true);
    }
}
```

C# constructs the layout. The component supplies the navigation button:

```csharp
var navigation = window.Stack().Add(window.Button("Home"));
var content = window.Stack().Add(window.Label("Home page"));
var adaptive = window.AdaptiveLayout("Workspace", navigation, content)
    .SetBreakpoint(640).SetNavigationExtent(220)
    .SetCompactNavigation(CompactNavigation.Overlay).SetNavigationOpen(false);
var view = new ControlExamples.AdaptiveWorkspace(window, adaptive);
```

{% endtab %}
{% tab title="C#" %}

```csharp
var navigation = window.Stack().Add(window.Button("Home"));
var content = window.Stack().Add(window.Label("Home page"));
var adaptive = window.AdaptiveLayout("Workspace", navigation, content)
    .SetBreakpoint(640).SetNavigationExtent(220)
    .SetCompactNavigation(CompactNavigation.Overlay).SetNavigationOpen(false);
anchor.Click += () => adaptive.NavigationOpen = true;
root.Add(adaptive, 1);
```

{% endtab %}
{% tab title="Rust" %}

```rust
let navigation = window.stack(Axis::Vertical)?;
navigation.add(&*window.button("Home")?, 0.0)?;
let content = window.stack(Axis::Vertical)?;
content.add(&*window.label("Home page")?, 0.0)?;
let adaptive = window.adaptive_layout("Workspace", &navigation, &content)?;
adaptive.set_breakpoint(640.0)?;
adaptive.set_navigation_extent(220.0)?;
adaptive.set_compact_navigation(CompactNavigation::Overlay)?;
adaptive.set_navigation_open(false)?;
let layout = adaptive.weak();
anchor.on_event(move |event| {
    let Some(layout) = layout.upgrade() else { return Ok(()); };
    if event.kind == 1 {
        layout.set_navigation_open(true).map_err(|error| {
            eprintln!("Navigation callback: {error}");
            error
        })?;
    }
    Ok(())
})?;
root.add(&adaptive, 1.0)?;
```

{% endtab %}
{% tab title="C++" %}

```cpp
auto navigation = std::make_shared<xui::Stack>(xui::Axis::vertical);
navigation->add(std::make_shared<xui::Button>(L"Home"));
auto content = std::make_shared<xui::Stack>(xui::Axis::vertical);
content->add(std::make_shared<xui::Label>(L"Home page"));
auto adaptive = std::make_shared<xui::AdaptiveLayout>(navigation, content);
adaptive->set_breakpoint(640);
adaptive->set_navigation_extent(220);
adaptive->set_compact_navigation(xui::CompactNavigation::overlay);
adaptive->set_navigation_open(false);
anchor->on_click([adaptive] { adaptive->set_navigation_open(true); });
root->add(adaptive, 1);
```

{% endtab %}
{% endtabs %}

The overlay is nonmodal.
Escape from navigation closes it.
Transitions retain child identities instead of a replacement tree.
Native runtime hosts have a separate [overlay and unload boundary](media.md#native-host-boundaries).

The style target is `adaptive_layout`.
Its `compact` and `expanded` states follow layout and navigation state.
Styles do not create a breakpoint or change ownership.

## ContentView

Use `ContentView` to clip a retained subtree without a scrollbar.
Use `ScrollView` to let users reach content outside the viewport.

{% tabs %}
{% tab title=".xui" %}

Unavailable as a standalone host. There is no C# `ContentView` factory or markup constructor.
`Content(existingElement)` embeds an existing element but does not create a clipping host.
Use `ScrollView` for a viewport with scrolling, or `SplitView` for retained pane hosts.

{% endtab %}
{% tab title="C#" %}

Unavailable as a standalone host. The binding has no `ContentView` factory.
The `ScrollView` and `SplitView` examples provide supported viewport compositions with different behavior.

{% endtab %}
{% tab title="Rust" %}

Unavailable as a standalone host. The binding has no `ContentView` factory.
The `ScrollView` and `SplitView` examples provide supported viewport compositions with different behavior.

{% endtab %}
{% tab title="C++" %}

```cpp
auto content = std::make_shared<xui::Stack>(xui::Axis::vertical);
content->add(std::make_shared<xui::TextInput>(L"Pane filter"));
auto pane = std::make_shared<xui::ContentView>(content, L"Filter pane");
root->add(pane, 1);
```

{% endtab %}
{% endtabs %}

The host supplies a native parent for child controls.
It does not add an independent renderer or message loop.
The style target is `content_view`.
The frame and content alignment do not style the subtree recursively.
C# and Rust have no standalone ContentView factory.

## ScrollView

Use `ScrollView` for a retained form that needs vertical scrolling.
It retains every child and is not a replacement for virtual collections.

{% tabs %}
{% tab title=".xui" %}

```text
namespace ControlExamples;
component PreferencesViewport {
    view {
        VStack() {
            ScrollView("Preferences", preferredSize: (360, 160), flex: 1) {
                VStack(spacing: 12) {
                    TextInput("Name");
                    TextInput("Description");
                    TextInput("Owner");
                }
            }
        }
    }
}
```

{% endtab %}
{% tab title="C#" %}

```csharp
var form = window.Stack().Spacing(12);
form.Add(window.TextInput("Name"));
form.Add(window.TextInput("Description"));
form.Add(window.TextInput("Owner"));
var viewport = window.ScrollView(form, "Preferences").PreferredSize(360, 160);
root.Add(viewport, 1);
```

{% endtab %}
{% tab title="Rust" %}

```rust
let form = window.stack(Axis::Vertical)?;
form.spacing(12.0)?;
form.add(&*window.text_input("Name")?, 0.0)?;
form.add(&*window.text_input("Description")?, 0.0)?;
form.add(&*window.text_input("Owner")?, 0.0)?;
let viewport = window.scroll_view(&form, "Preferences")?;
window.update(&[Property {
    a: 360.0, b: 160.0, ..Property::new(&viewport, PropertyKind::PreferredSize)
}])?;
root.add(&viewport, 1.0)?;
```

{% endtab %}
{% tab title="C++" %}

```cpp
auto form = std::make_shared<xui::Stack>(xui::Axis::vertical);
form->set_spacing(12);
form->add(std::make_shared<xui::TextInput>(L"Name"));
form->add(std::make_shared<xui::TextInput>(L"Description"));
form->add(std::make_shared<xui::TextInput>(L"Owner"));
auto viewport = std::make_shared<xui::ScrollView>(form, L"Preferences");
viewport->set_preferred_size({360, 160});
root->add(viewport, 1);
```

{% endtab %}
{% endtabs %}

`set_offset`, `scroll_by`, and `reveal` use DIPs and clamp to the content range.
Focus traversal reveals nested controls.
Native editors retain their editing keys and IME path.
UIA supplies Scroll and descendant ScrollItem behavior.

The style target is `scroll_view`.
Scrollbar metrics affect layout, hit testing, and accessible bounds together.
The viewport root rejects foreground.
`set_overlay_scrollbar` and `set_passthrough` select explicit viewport behavior, not style-only variants.

## SplitView

Use `SplitView` for two panes with a user-adjustable divider.
Use `AdaptiveLayout` for a navigation breakpoint without divider interaction.

{% tabs %}
{% tab title=".xui" %}

C# sets the ratio through the generated reference.

```text
namespace ControlExamples;
component PreviewPanes {
    view {
        VStack() {
            SplitView("Preview divider", ref: Split, secondVisible: true, flex: 1) {
                VStack() { Text("Primary pane"); }
                VStack() { Text("Preview pane"); }
            }
        }
    }
}
```

```csharp
var view = new ControlExamples.PreviewPanes(window);
view.Split.SetRatio(0.6);
```

{% endtab %}
{% tab title="C#" %}

```csharp
var first = window.Stack().Add(window.Label("Primary pane"));
var second = window.Stack().Add(window.Label("Preview pane"));
var split = window.SplitView("Preview divider", first, second)
    .SetRatio(0.6).SetSecondVisible(true);
root.Add(split, 1);
```

{% endtab %}
{% tab title="Rust" %}

```rust
let first = window.stack(Axis::Vertical)?;
first.add(&*window.label("Primary pane")?, 0.0)?;
let second = window.stack(Axis::Vertical)?;
second.add(&*window.label("Preview pane")?, 0.0)?;
let split = window.split_view("Preview divider", &first, &second)?;
split.set_ratio(0.6)?;
split.set_second_visible(true)?;
root.add(&split, 1.0)?;
```

{% endtab %}
{% tab title="C++" %}

```cpp
auto first = std::make_shared<xui::Stack>(xui::Axis::vertical);
first->add(std::make_shared<xui::Label>(L"Primary pane"));
auto second = std::make_shared<xui::Stack>(xui::Axis::vertical);
second->add(std::make_shared<xui::Label>(L"Preview pane"));
auto split = std::make_shared<xui::SplitView>(first, second, L"Preview divider");
split->set_ratio(0.6f);
split->set_secondary_visible(true);
root->add(split, 1);
```

{% endtab %}
{% endtabs %}

`first()` and `second()` return retained ContentView hosts.
Narrow widths collapse the second pane.
`on_expanded` reports an actual expansion transition.
The divider supports keyboard resizing and cancellation.

The style target is `split_view`.
Pane frames, divider, and grip do not replace either ContentView.
Divider metrics preserve the shared drawing and hit-test geometry.

## PageView

Use `PageView` for retained pages with one active page.
It does not supply its own page selector.

{% tabs %}
{% tab title=".xui" %}

`PageView` has no markup constructor.
This component requires a C# page view from the same window, without an existing parent.

```text
namespace ControlExamples;
component PreferencePages {
    param global::Xui.PageView Pages;
    view {
        VStack() {
            Button("Show advanced preferences", click: ShowAdvanced);
            Content(Pages, flex: 1);
        }
    }
    code csharp {
        void ShowAdvanced() => Pages.SetSelectedPage(1);
    }
}
```

C# constructs the pages. The component supplies the page-selection button:

```csharp
var pages = window.PageView("Preferences");
pages.Add(window.TextInput("General preferences"));
pages.Add(window.TextInput("Advanced preferences"));
pages.SetSelectedPage(0);
var view = new ControlExamples.PreferencePages(window, pages);
```

{% endtab %}
{% tab title="C#" %}

```csharp
var pages = window.PageView("Preferences");
pages.Add(window.TextInput("General preferences"));
pages.Add(window.TextInput("Advanced preferences"));
pages.SetSelectedPage(0);
anchor.Click += () => pages.SelectedPage = 1;
root.Add(pages, 1);
```

{% endtab %}
{% tab title="Rust" %}

```rust
let pages = window.page_view("Preferences")?;
pages.add(&*window.text_input("General preferences")?)?;
pages.add(&*window.text_input("Advanced preferences")?)?;
pages.set_selected_page(0)?;
let selected_pages = pages.weak();
anchor.on_event(move |event| {
    let Some(selected_pages) = selected_pages.upgrade() else { return Ok(()); };
    if event.kind == 1 {
        selected_pages.set_selected_page(1).map_err(|error| {
            eprintln!("Page callback: {error}");
            error
        })?;
    }
    Ok(())
})?;
root.add(&pages, 1.0)?;
```

{% endtab %}
{% tab title="C++" %}

```cpp
auto pages = std::make_shared<xui::PageView>();
pages->add_page(std::make_shared<xui::TextInput>(L"General preferences"));
pages->add_page(std::make_shared<xui::TextInput>(L"Advanced preferences"));
pages->select(0);
anchor->on_click([pages] { pages->select(1); });
root->add(pages, 1);
```

{% endtab %}
{% endtabs %}

`select` uses a zero-based page index.
Page changes perform no application I/O.
Native editors retain state after their first use.
Hidden runtime hosts are an [explicit exception](media.md#native-host-boundaries): they unload and need another load.

The style target is `page_view`.
Page content keeps its own styles and identity.

## TabStrip

Use `TabStrip` for stable tab IDs, selection, activation, and close requests.
Connect it to a PageView or application model explicitly.

{% tabs %}
{% tab title=".xui" %}

`TabStrip` and `PageView` have no markup constructors.
This component requires C# elements from the same window, without existing parents.

```text
namespace ControlExamples;
component TabbedPreferences {
    param global::Xui.TabStrip Tabs;
    param global::Xui.PageView Pages;
    view {
        VStack() {
            Content(Tabs);
            Content(Pages, flex: 1);
        }
    }
}
```

C# constructs both elements and connects selection:

```csharp
var pages = window.PageView("Preferences");
pages.Add(window.Label("General")).Add(window.Label("Advanced"));
var tabs = window.TabStrip("Preference pages");
tabs.SetTabs([new(10, "General"), new(20, "Advanced")], 10);
tabs.Event += e => {
    if (e.Kind == EventKind.Selection) pages.SelectedPage = e.Value == 10 ? 0UL : 1UL;
};
var view = new ControlExamples.TabbedPreferences(window, tabs, pages);
```

{% endtab %}
{% tab title="C#" %}

```csharp
var pages = window.PageView("Preferences");
pages.Add(window.Label("General")).Add(window.Label("Advanced"));
var tabs = window.TabStrip("Preference pages");
tabs.SetTabs([new(10, "General"), new(20, "Advanced")], 10);
tabs.Event += e => {
    if (e.Kind == EventKind.Selection) pages.SelectedPage = e.Value == 10 ? 0UL : 1UL;
};
root.Add(tabs).Add(pages, 1);
```

{% endtab %}
{% tab title="Rust" %}

```rust
let pages = window.page_view("Preferences")?;
pages.add(&*window.label("General")?)?;
pages.add(&*window.label("Advanced")?)?;
let tabs = window.tab_strip("Preference pages")?;
tabs.set_items(&[
    Choice { id: 10, text: "General".into(), enabled: true, version: 0 },
    Choice { id: 20, text: "Advanced".into(), enabled: true, version: 0 },
], Some(10))?;
let selected_pages = pages.weak();
tabs.on_event(move |event| {
    let Some(selected_pages) = selected_pages.upgrade() else { return Ok(()); };
    if event.kind == 5 {
        selected_pages.set_selected_page(if event.value == 10 { 0 } else { 1 })
            .map_err(|error| {
                eprintln!("Tab callback: {error}");
                error
            })?;
    }
    Ok(())
})?;
root.add(&tabs, 0.0)?;
root.add(&pages, 1.0)?;
```

{% endtab %}
{% tab title="C++" %}

```cpp
auto pages = std::make_shared<xui::PageView>();
pages->add_page(std::make_shared<xui::Label>(L"General"));
pages->add_page(std::make_shared<xui::Label>(L"Advanced"));
auto tabs = std::make_shared<xui::TabStrip>(L"Preference pages");
tabs->set_tabs({{10, L"General"}, {20, L"Advanced"}}, 10);
tabs->on_select([pages](std::uint64_t id) {
    pages->select(id == 10 ? 0 : 1);
});
root->add(tabs);
root->add(pages, 1);
```

{% endtab %}
{% endtabs %}

`on_activate` also handles activation of the already-selected tab.
`on_close` enables close requests. The application updates the tab data after acceptance.
One retained control supplies virtual tab providers, not one native peer per tab.

The style target is `tab_strip`.
Tab labels, selection markers, and close actions use stable tab identities.
There is no tab icon, add action, or drag state in this model.

## Related contracts

- [Scrolling, ownership, and accessibility](../application.md)
- [Virtual collections and adaptive layout](../collections.md#virtual-collections-and-adaptive-layout)
- [Parts and retained child boundaries](../control-styling-inventory.md)
- [C# and Rust layout APIs](../bindings.md)
