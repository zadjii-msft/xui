# Layout and workspace

[Control catalog](README.md) · [Layout contract](../application.md#content-sizes-and-constraints) · [Workspace input](../menus-and-input.md)

Examples use the [C++ fragment context](README.md#use-the-examples).
Add `xui\adaptive_layout.hpp` for Grid, Wrap, and AdaptiveLayout.

## Stack

Use `Stack` for a horizontal row or vertical column.
A positive flex value gives a child a share of remaining main-axis space.

```cpp
auto row = std::make_shared<xui::Stack>(xui::Axis::horizontal);
row->set_spacing(8);
row->set_padding({12, 12, 12, 12});
row->add(std::make_shared<xui::Label>(L"Workspace"));
row->add(std::make_shared<xui::TextInput>(L"Workspace name"), 1);
root->add(row);
```

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

`GridTrack` selects fixed, automatic, or weighted space.
The final two `add` arguments specify row and column spans.
The style target is `grid`.
Track definitions, placement, and explicit layout setters remain structural properties.

## Wrap

Use `Wrap` for a small collection of retained controls that changes its column count.
Use `ItemsView` tiles for a large source.

```cpp
auto cards = std::make_shared<xui::Wrap>();
cards->set_item_width(180);
cards->set_spacing(12);
cards->add(std::make_shared<xui::Button>(L"Documents"));
cards->add(std::make_shared<xui::Button>(L"Pictures"));
cards->add(std::make_shared<xui::Button>(L"Downloads"));
root->add(cards);
```

`columns()` reports the current layout.
Every child remains retained.
The style target is `wrap`, with frame, separator, padding, and spacing.

## AdaptiveLayout

Use `AdaptiveLayout` for navigation and content that share a row at wide widths.
Compact mode can stack the same children or show a navigation overlay.

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

```cpp
auto content = std::make_shared<xui::Stack>(xui::Axis::vertical);
content->add(std::make_shared<xui::TextInput>(L"Pane filter"));
auto pane = std::make_shared<xui::ContentView>(content, L"Filter pane");
root->add(pane, 1);
```

The host supplies a native parent for child controls.
It does not add an independent renderer or message loop.
The style target is `content_view`.
The frame and content alignment do not style the subtree recursively.
C# and Rust have no standalone ContentView factory.

## ScrollView

Use `ScrollView` for a retained form that needs vertical scrolling.
It retains every child and is not a replacement for virtual collections.

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

```cpp
auto pages = std::make_shared<xui::PageView>();
pages->add_page(std::make_shared<xui::TextInput>(L"General preferences"));
pages->add_page(std::make_shared<xui::TextInput>(L"Advanced preferences"));
pages->select(0);
anchor->on_click([pages] { pages->select(1); });
root->add(pages, 1);
```

`select` uses a zero-based page index.
Page changes perform no application I/O.
Native editors retain state after their first use.
Hidden runtime hosts are an [explicit exception](media.md#native-host-boundaries): they unload and need another load.

The style target is `page_view`.
Page content keeps its own styles and identity.

## TabStrip

Use `TabStrip` for stable tab IDs, selection, activation, and close requests.
Connect it to a PageView or application model explicitly.

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
