# Navigation

[Control catalog](README.md) · [Navigation contract](../commands-and-navigation.md) · [Binding coverage](../bindings.md)

Examples use the [C++ fragment context](README.md#use-the-examples).
Add `xui\navigation.hpp`.
Navigation actions request application changes. They do not perform filesystem I/O.

## NavigationView

Use `NavigationView` for application sections with shared selection, search, and an expandable pane.
Use TreeView for lazy general-purpose hierarchies.

```cpp
auto navigation = std::make_shared<xui::NavigationView>(L"Application pages");
navigation->set_items({
    {{1, 1}, {}, L"Home", xui::ButtonIcon::home},
    {{2, 1}, {}, L"Documents", xui::ButtonIcon::folder},
    {{3, 1}, xui::ItemKey{2, 1}, L"Recent documents", xui::ButtonIcon::history}
});
navigation->set_pane_widths(280, 64);
navigation->select({1, 1});
auto page = std::make_shared<xui::Label>(L"Home");
navigation->on_select([page](xui::ItemKey key) {
    page->set_text(L"Page " + std::to_wstring(key.id));
});
root->add(navigation, 1);
root->add(page);
```

`NavigationItem::section` selects header, main, or footer placement.
Keys are unique across sections.
Parents control descendant availability and expansion.
`select` reveals ancestors and reports an actual selection change.
`on_activate` also handles activation without a new selection.

`set_filter`, `on_filter`, and the retained search editor support application search behavior.
`set_search_visible` and `set_header_visible` control those presentations.
`set_expanded` changes pane expansion.
The model supports at most 4,096 entries and 64 hierarchy levels.

The style target is `navigation_view`.
Its root frame does not substitute for retained child styling.
`search()`, `toggle_button()`, `title()`, and `empty_message()` have their own targets.

## NavigationList

NavigationView owns three NavigationList children:
`header_items()`, `items()`, and `footer_items()`.
There is no public NavigationList constructor.

```cpp
auto navigation = std::make_shared<xui::NavigationView>(L"Pages");
navigation->set_items({{{1, 1}, {}, L"Home", xui::ButtonIcon::home}});
navigation->items()->set_help_text(L"Choose an application page.");
root->add(navigation, 1);
```

Do not add these children to another parent.
The NavigationView coordinates their shared selection and hierarchy.
They use virtual rows and tree input/accessibility adapters.

Their style target is `navigation_list`, not `tree_view`.
Selection markers, badges, icons, section headers, and disclosure are virtual parts.
Selected-descendant and compact states follow navigation state.
C# and Rust expose retained Element wrappers, not TreeView factories.

## Breadcrumb

Use `Breadcrumb` for a committed path of stable segments.
Use TextInput for raw address entry.

```cpp
auto path = std::make_shared<xui::Breadcrumb>(L"Current location");
path->set_segments({
    {{1, 1}, L"Workspace"},
    {{2, 1}, L"Documents"},
    {{3, 1}, L"Reports"}
});
auto request = std::make_shared<xui::Label>(L"No navigation request");
path->on_navigate([request](xui::ItemKey key) {
    request->set_text(L"Requested location " + std::to_wstring(key.id));
});
root->add(path);
root->add(request);
```

Activation requests navigation but does not replace the path.
After successful navigation, update `set_segments` from the committed application state.
Earlier segments move into overflow at narrow widths.
The limit is 64 segments.
Arrow keys move among visible segment buttons.

The style target is `breadcrumb`.
`overflow_button()` and `segment_button(ItemKey)` expose retained Buttons.
Segment keys include versions.
The root supports `overflowed` and inherited `disabled`, not a synthetic current-segment selector.

## NavigationPane

Use `NavigationPane` for cached quick-access rows and cancelable queries.
It composes ItemsView, Expander, Progress, and status text.

This fragment requires `std::shared_ptr<const xui::ItemsSource> cached_locations`:

```cpp
auto pane = std::make_shared<xui::NavigationPane>(L"Quick access");
pane->set_items(cached_locations);
auto request = std::make_shared<xui::Label>(L"No location requested");
pane->on_navigate([request](xui::ItemKey key) {
    request->set_text(L"Requested " + std::to_wstring(key.id));
});
root->add(pane, 1);
root->add(request);
```

`on_query` receives a `NavigationQuery`.
`request(text)` starts that query path.
Application work honors its cancellation token and delivers `complete` on the UI thread.
Hide, collapse, or closure cancels current work.
Completion rejects stale or foreign requests.
Sources do not enumerate directories through their row callbacks.

The style target is `navigation_pane`.
Query `loading`, `error`, and `empty` states belong to this control.
`items()`, `group()`, `progress()`, `status()`, and `content()` expose real retained children.
Each child accepts its own style target.

## LocationPicker

Use `LocationPicker` for a native address editor and quick-access navigation inside a popup.
It adds a CommandBar toolbar and keyboard footer.

This fragment requires `std::shared_ptr<const xui::ItemsSource> cached_locations`:

```cpp
auto picker = std::make_shared<xui::LocationPicker>(L"Choose location");
picker->navigation()->set_items(cached_locations);
picker->editor()->set_text(L"C:\\Data");
auto requested = std::make_shared<xui::Label>(L"No location requested");
picker->navigation()->on_navigate([requested](xui::ItemKey key) {
    requested->set_text(L"Requested location " + std::to_wstring(key.id));
});
anchor->on_click([&window, &anchor, picker] {
    window.show_location_picker(picker, *anchor);
});
root->add(requested);
```

`show_location_picker` retains the composition and routes editor arrows to virtual rows.
The application decides whether a selected location is valid and commits navigation.
Cancellation must not change the committed location.
The picker does not supply directory enumeration or filesystem validation.

The facade root is `popup()`, with target `popup`.
Its root has only `open` and inherited `disabled`.
Query states belong to `navigation()`.
`editor()`, `toolbar()`, `content()`, and `footer()` keep their real targets.

## ViewPicker

Use `ViewPicker` to change a real ItemsView presentation and item size.
It does not change FileList presentation.

This fragment requires `std::shared_ptr<const xui::ItemsSource> item_source`:

```cpp
auto items = std::make_shared<xui::ItemsView>(L"Results");
items->set_items(item_source);
auto picker = std::make_shared<xui::ViewPicker>(items);
anchor->on_click([&window, &anchor, picker] {
    window.show_popup(picker->popup(), *anchor);
});
root->add(items, 1);
```

The picker retains radio choices and a separate vertical RangeInput.
`choices()` changes presentation.
`size()` changes row or tile size.
Do not move those children into another layout parent.

The facade root accepts `popup`, not `view_picker`.
Selection styling belongs to the actual choices.
The range and content Stack have independent style attachments.

## Combine navigation with pages

1. Create PageView pages before the window starts.
2. Assign stable keys to NavigationView entries.
3. Map accepted navigation keys to page indexes in `on_select`.
4. Update expensive page data outside the selection callback.
5. Deliver completed snapshots on the UI thread.

The [PageView example](layout.md#pageview) shows retained pages.
The [collection guide](collections.md) describes source and selection ownership.
The [retained-child contract](../bindings.md#retained-composition-children) lists C# and Rust accessors.
