# Navigation

[Control catalog](README.md) · [Navigation contract](../commands-and-navigation.md) · [Binding coverage](../bindings.md)

Examples use the [shared fragment context](README.md#use-the-examples).
C++ examples also need `xui\navigation.hpp`.
C# fragments assume `using System;` and `using Xui;`.
Rust fragments use `use xui::*;` inside the shared `example` function.
That function returns `std::result::Result<(), Box<dyn std::error::Error>>`.
Rust callbacks use weak handles to avoid ownership cycles.
Navigation actions request application changes. They do not perform filesystem I/O.

## NavigationView

Use `NavigationView` for application sections with shared selection, search, and an expandable pane.
Use TreeView for lazy general-purpose hierarchies.

{% tabs %}
{% tab title=".xui" %}

```text
namespace ControlExamples;
component ApplicationPages {
    view {
        VStack() {
            NavigationView("Application pages", ref: Navigation);
            Text("Home", ref: Page);
        }
    }
}
```

C# setup:

```csharp
var component = new ControlExamples.ApplicationPages(window);
component.Navigation.SetItems([
    new(1, "Home", Icon: ButtonIcon.Home),
    new(2, "Documents", Icon: ButtonIcon.Folder),
    new(3, "Recent documents", Parent: 2, Icon: ButtonIcon.History)
]).Select(1);
component.Navigation.Event += e => {
    if (e.Kind == EventKind.Selection) component.Page.Text = $"Page {e.Value}";
};
```

The C# model uses IDs without versions. It has no pane-width setter or section placement field.

{% endtab %}
{% tab title="C#" %}

```csharp
var navigation = window.NavigationView("Application pages").SetItems([
    new(1, "Home", Icon: ButtonIcon.Home),
    new(2, "Documents", Icon: ButtonIcon.Folder),
    new(3, "Recent documents", Parent: 2, Icon: ButtonIcon.History)
]).Select(1);
var page = window.Label("Home");
navigation.Event += e => {
    if (e.Kind == EventKind.Selection) page.Text = $"Page {e.Value}";
};
root.Add(navigation, 1).Add(page);
```

The binding has no pane-width setter or section placement field. Entries use IDs without versions.

{% endtab %}
{% tab title="Rust" %}

The wrapper exposes NavigationView construction and retained children, but no entry setter.
It cannot reproduce this populated hierarchy.
This fragment configures the supported pane and search-help surfaces.

```rust
let navigation = window.navigation_view("Application pages")?;
navigation.set_expanded(true)?;
navigation.search()?.help("Search application pages.")?;
root.add(&navigation, 1.)?;
Ok(())
```

{% endtab %}
{% tab title="C++" %}

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

{% endtab %}
{% endtabs %}

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

{% tabs %}
{% tab title=".xui" %}

NavigationList has no markup constructor. Its NavigationView owns the retained list.

```text
namespace ControlExamples;
component PageListOwner {
    param global::Xui.Element Owner;
    view {
        VStack() {
            Content(Owner);
        }
    }
}
```

C# setup:

```csharp
var navigation = window.NavigationView("Pages")
    .SetItems([new(1, "Home", Icon: ButtonIcon.Home)]);
navigation.Items.SetControlStyleValues(StylePart.PrimaryText, new PartStyleValues { FontSize = 14 });
var component = new ControlExamples.PageListOwner(window, navigation);
```

The retained Element has style APIs, but no typed help-text setter. It stays inside NavigationView.

{% endtab %}
{% tab title="C#" %}

```csharp
var navigation = window.NavigationView("Pages")
    .SetItems([new(1, "Home", Icon: ButtonIcon.Home)]);
navigation.Items.SetControlStyleValues(StylePart.PrimaryText, new PartStyleValues { FontSize = 14 });
root.Add(navigation, 1);
```

The retained Element has style APIs, but no typed help-text setter. Do not mount `navigation.Items` separately.

{% endtab %}
{% tab title="Rust" %}

The wrapper has no navigation entry setter. This fragment styles the retained list without another parent.

```rust
let navigation = window.navigation_view("Pages")?;
navigation.items()?.set_control_style_values(StylePart::PrimaryText, PartStyleValues {
    font_size: Some(14.), ..Default::default()
})?;
root.add(&navigation, 1.)?;
Ok(())
```

{% endtab %}
{% tab title="C++" %}

```cpp
auto navigation = std::make_shared<xui::NavigationView>(L"Pages");
navigation->set_items({{{1, 1}, {}, L"Home", xui::ButtonIcon::home}});
navigation->items()->set_help_text(L"Choose an application page.");
root->add(navigation, 1);
```

{% endtab %}
{% endtabs %}

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

{% tabs %}
{% tab title=".xui" %}

Breadcrumb has no markup constructor.

```text
namespace ControlExamples;
component CommittedLocation {
    param global::Xui.Element Path;
    view {
        VStack() {
            Content(Path);
        }
    }
}
```

C# setup:

```csharp
var path = window.Breadcrumb("Current location").SetSegments([
    new(1, "Workspace", Version: 1), new(2, "Documents", Version: 1),
    new(3, "Reports", Version: 1)
]);
path.Event += e => {
    if (e.Kind == EventKind.Selection) Console.WriteLine($"Requested location {e.Value}");
};
var component = new ControlExamples.CommittedLocation(window, path);
```

{% endtab %}
{% tab title="C#" %}

```csharp
var path = window.Breadcrumb("Current location").SetSegments([
    new(1, "Workspace", Version: 1), new(2, "Documents", Version: 1),
    new(3, "Reports", Version: 1)
]);
var request = window.Label("No navigation request");
path.Event += e => {
    if (e.Kind == EventKind.Selection) request.Text = $"Requested location {e.Value}";
};
root.Add(path).Add(request);
```

Events report the ID. The application retains the corresponding segment version.

{% endtab %}
{% tab title="Rust" %}

```rust
let path = window.breadcrumb("Current location")?;
path.set_segments(&[
    Choice { id: 1, version: 1, text: "Workspace".into(), enabled: true },
    Choice { id: 2, version: 1, text: "Documents".into(), enabled: true },
    Choice { id: 3, version: 1, text: "Reports".into(), enabled: true },
])?;
let request = window.label("No navigation request")?;
let weak_request = request.downgrade();
path.on_event(move |event| {
    if event.kind == 5 && let Some(request) = weak_request.upgrade() {
        request.set_text(&format!("Requested location {}", event.value))
            .inspect_err(|error| eprintln!("Breadcrumb navigation: {error}"))?;
    }
    Ok(())
})?;
root.add(&path, 0.)?;
root.add(&request, 0.)?;
Ok(())
```

Selection event kind `5` reports the ID, not the segment version.

{% endtab %}
{% tab title="C++" %}

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

{% endtab %}
{% endtabs %}

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

{% tabs %}
{% tab title=".xui" %}

NavigationPane has no markup constructor.
The setup accepts an additional `ImmutableSource cachedLocations` parameter.

```text
namespace ControlExamples;
component QuickAccessLocations {
    param global::Xui.Element Pane;
    view {
        VStack() {
            Content(Pane);
        }
    }
}
```

C# setup:

```csharp
var pane = window.NavigationPane("Quick access").SetSource(cachedLocations);
pane.Event += e => {
    if (e.Kind == EventKind.Selection) Console.WriteLine($"Requested {e.Value}");
};
var component = new ControlExamples.QuickAccessLocations(window, pane);
```

The binding supports cached sources, but not the C++ query request/completion service.

{% endtab %}
{% tab title="C#" %}

This fragment accepts an additional `ImmutableSource cachedLocations` parameter.

```csharp
var pane = window.NavigationPane("Quick access").SetSource(cachedLocations);
var request = window.Label("No location requested");
pane.Event += e => {
    if (e.Kind == EventKind.Selection) request.Text = $"Requested {e.Value}";
};
root.Add(pane, 1).Add(request);
```

The binding supports cached sources, but not the C++ query request/completion service.

{% endtab %}
{% tab title="Rust" %}

This fragment accepts an additional `cached_locations: &ImmutableSource` parameter.

```rust
let pane = window.navigation_pane("Quick access")?;
pane.set_source(cached_locations)?;
let request = window.label("No location requested")?;
let weak_request = request.downgrade();
pane.on_event(move |event| {
    if event.kind == 5 && let Some(request) = weak_request.upgrade() {
        request.set_text(&format!("Requested {}", event.value))
            .inspect_err(|error| eprintln!("Quick-access navigation: {error}"))?;
    }
    Ok(())
})?;
root.add(&pane, 1.)?;
root.add(&request, 0.)?;
Ok(())
```

The binding supports cached sources, but not the C++ query request/completion service.

{% endtab %}
{% tab title="C++" %}

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

{% endtab %}
{% endtabs %}

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

{% tabs %}
{% tab title=".xui" %}

LocationPicker has no markup constructor.
Its Element handle is the actual Popup root, not its retained content Stack.
The setup accepts an additional `ImmutableSource cachedLocations` parameter.

```text
namespace ControlExamples;
component LocationChoicePopup {
    param global::Xui.Element Picker;
    view {
        Content(Picker);
    }
}
```

C# setup:

```csharp
var picker = window.LocationPicker("Choose location");
picker.Navigation.SetSource(cachedLocations);
picker.Editor.Text = @"C:\Data";
picker.Navigation.Event += e => {
    if (e.Kind == EventKind.Selection) Console.WriteLine($"Requested location {e.Value}");
};
var component = new ControlExamples.LocationChoicePopup(window, picker, attach: false);
anchor.Click += () => picker.Show(anchor);
```

`Show` mounts the Popup root. Do not add the component root or retained children to `root`.

{% endtab %}
{% tab title="C#" %}

This fragment accepts an additional `ImmutableSource cachedLocations` parameter.

```csharp
var picker = window.LocationPicker("Choose location");
picker.Navigation.SetSource(cachedLocations);
picker.Editor.Text = @"C:\Data";
var requested = window.Label("No location requested");
picker.Navigation.Event += e => {
    if (e.Kind == EventKind.Selection) requested.Text = $"Requested location {e.Value}";
};
anchor.Click += () => picker.Show(anchor);
root.Add(requested);
```

{% endtab %}
{% tab title="Rust" %}

This fragment accepts an additional `cached_locations: &ImmutableSource` parameter.

```rust
let picker = window.location_picker("Choose location")?;
picker.navigation()?.set_source(cached_locations)?;
picker.editor()?.set_text(r"C:\Data")?;
let requested = window.label("No location requested")?;
let weak_requested = requested.downgrade();
picker.navigation()?.on_event(move |event| {
    if event.kind == 5 && let Some(requested) = weak_requested.upgrade() {
        requested.set_text(&format!("Requested location {}", event.value))
            .inspect_err(|error| eprintln!("Location navigation: {error}"))?;
    }
    Ok(())
})?;
let weak_picker = picker.weak();
let weak_anchor = anchor.downgrade();
anchor.on_event(move |event| {
    if event.kind == 1
        && let (Some(picker), Some(anchor)) = (weak_picker.upgrade(), weak_anchor.upgrade())
    {
        picker.show(&anchor)
            .inspect_err(|error| eprintln!("Show location picker: {error}"))?;
    }
    Ok(())
})?;
root.add(&requested, 0.)?;
Ok(())
```

{% endtab %}
{% tab title="C++" %}

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

{% endtab %}
{% endtabs %}

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

{% tabs %}
{% tab title=".xui" %}

ViewPicker has no markup constructor. Its Element handle is the actual Popup root.
The setup accepts an additional `ImmutableSource itemSource` parameter.

```text
namespace ControlExamples;
component ResultsViewPopup {
    param global::Xui.Element Picker;
    view {
        Content(Picker);
    }
}
```

C# setup:

```csharp
var items = window.ItemsView("Results").SetSource(itemSource);
var picker = window.ViewPicker("View options", items);
var component = new ControlExamples.ResultsViewPopup(window, picker, attach: false);
anchor.Click += () => picker.Show(anchor);
root.Add(items, 1);
```

`Show` mounts the Popup root. The target ItemsView remains in the page.
Do not mount the retained choices, range, or content Stack separately.

{% endtab %}
{% tab title="C#" %}

This fragment accepts an additional `ImmutableSource itemSource` parameter.

```csharp
var items = window.ItemsView("Results").SetSource(itemSource);
var picker = window.ViewPicker("View options", items);
anchor.Click += () => picker.Show(anchor);
root.Add(items, 1);
```

{% endtab %}
{% tab title="Rust" %}

This fragment accepts an additional `item_source: &ImmutableSource` parameter.

```rust
let items = window.items_view("Results")?;
items.set_source(item_source)?;
let picker = window.view_picker("View options", &items)?;
let weak_picker = picker.weak();
let weak_anchor = anchor.downgrade();
anchor.on_event(move |event| {
    if event.kind == 1
        && let (Some(picker), Some(anchor)) = (weak_picker.upgrade(), weak_anchor.upgrade())
    {
        picker.show(&anchor)
            .inspect_err(|error| eprintln!("Show view picker: {error}"))?;
    }
    Ok(())
})?;
root.add(&items, 1.)?;
Ok(())
```

{% endtab %}
{% tab title="C++" %}

```cpp
auto items = std::make_shared<xui::ItemsView>(L"Results");
items->set_items(item_source);
auto picker = std::make_shared<xui::ViewPicker>(items);
anchor->on_click([&window, &anchor, picker] {
    window.show_popup(picker->popup(), *anchor);
});
root->add(items, 1);
```

{% endtab %}
{% endtabs %}

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
