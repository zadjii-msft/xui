# Collections

[Control catalog](README.md) · [Collection contract](../collections.md) · [Binding coverage](../bindings.md)

Examples use the [shared fragment context](README.md#use-the-examples).
C++ examples also need `xui\collections.hpp` and `xui\data_grid.hpp`.
Class definitions belong at file scope.
C# fragments assume `using System;` and `using Xui;`.
Rust fragments use `use xui::*;` inside the shared `example` function.
That function returns `std::result::Result<(), Box<dyn std::error::Error>>`.
Rust callbacks use weak handles to avoid ownership cycles.

## Choose a collection

| Requirement | Control |
| --- | --- |
| File records with existing snapshot/filter support | FileList |
| Generic list, tiles, groups, or inline actions | ItemsView |
| Lazy hierarchical data | TreeView |
| A hierarchy path shown as adjacent sibling lists | MillerColumns |
| Virtual rows with multiple logical columns | DataGrid |
| Fixed recent numeric history | HistoryChart |
| A small set of retained cards | [Wrap](layout.md#wrap) |

Virtualization limits visible content work.
It does not make a mutable source safe or provide automatic storage paging.
Sources must preserve stable identity and remain immutable.
Identity lookup must not enumerate all rows.

## FileList

Use `FileList` for immutable `FileItem` records with stable IDs.
The synchronous convenience setters suit small sources.

{% tabs %}
{% tab title=".xui" %}

FileList has no markup constructor. This component accepts the configured control through an Element parameter.

```text
namespace ControlExamples;
component WorkspaceFiles {
    param global::Xui.Element Files;
    view {
        VStack() {
            Content(Files);
        }
    }
}
```

C# setup:

```csharp
var files = window.FileList("Workspace files").SetItems([
    new(1, "Notes.txt", @"C:\Data\Notes.txt"),
    new(2, "Archive", @"C:\Data\Archive", Directory: true)
]);
files.Event += e => {
    if (e.Kind == EventKind.Selection)
        Console.WriteLine("File selection changed");
};
var component = new ControlExamples.WorkspaceFiles(window, files);
```

The bindings expose selection events, but not the C++ FileList activation callback or empty-text setter.

{% endtab %}
{% tab title="C#" %}

```csharp
var files = window.FileList("Workspace files").SetItems([
    new(1, "Notes.txt", @"C:\Data\Notes.txt"),
    new(2, "Archive", @"C:\Data\Archive", Directory: true)
]);
files.Event += e => {
    if (e.Kind == EventKind.Selection)
        Console.WriteLine("File selection changed");
};
root.Add(files, 1);
```

The binding exposes selection events, but not the C++ activation callback or `set_empty_text`.

{% endtab %}
{% tab title="Rust" %}

```rust
let files = window.file_list("Workspace files")?;
files.set_items(&[
    FileItem { id: 1, name: "Notes.txt", path: r"C:\Data\Notes.txt", directory: false },
    FileItem { id: 2, name: "Archive", path: r"C:\Data\Archive", directory: true },
])?;
files.on_event(|event| {
    if event.kind == 5 {
        println!("File selection changed");
    }
    Ok(())
})?;
root.add(&files, 1.)?;
Ok(())
```

Event kind `5` is selection. The binding does not expose the C++ activation callback or empty-text setter.

{% endtab %}
{% tab title="C++" %}

```cpp
auto files = std::make_shared<xui::FileList>(L"Workspace files");
files->set_items(std::make_shared<const std::vector<xui::FileItem>>(
    std::vector<xui::FileItem>{
        {1, L"Notes.txt", L"C:\\Data\\Notes.txt", false},
        {2, L"Archive", L"C:\\Data\\Archive", true}
    }));
files->set_empty_text(L"No matching files", L"Try another filter.");
auto selected = std::make_shared<xui::Label>(L"No file opened");
files->on_activate([selected](const xui::FileItem& item, xui::FileActivation) {
    selected->set_text(item.path);
});
root->add(files, 1);
root->add(selected);
```

{% endtab %}
{% endtabs %}

`select` uses the visible row index.
`focused_id` and model selection remain separate.
`on_selection_change` and `on_view_change` observe the updated model.
Activation does not open a path unless the application implements that action.

For expensive enumeration or filtering, use `Window::create_view_task`.
The [complete worker example](../collections.md#virtual-lists-and-asynchronous-delivery) shows loader, delivery, and cancellation.
Retain the task handle while it is necessary.
Honor its cancellation check during source work.
A null source result means cancellation, not an empty successful list.

`set_thumbnails(true)` requests visible previews through the shared image workers.
`on_thumbnail_error` reports failed visible requests.
Shell icons and vector fallbacks remain available.
The [image contract](../images.md) defines limits and lifetime.

The style target is `file_list`.
Row, text, selection, icon, and scrollbar parts do not create per-row controls.
Styles do not recolor decoded thumbnails or Shell icon pixels.

## ItemsView

Use `ItemsView` for generic immutable sources.
By default, a mouse click selects a row. A double-click or Enter activates it.
C# `SetSingleClickActivation(true)` enables menu-style activation on an unmodified primary press.
C++ and Rust expose `set_single_click_activation(true)`.
The C ABI exposes the Boolean feature property `XUI_F_SINGLE_CLICK_ACTIVATION` (54).
Keyboard and programmatic selection remain separate from activation. Modified clicks retain their selection gestures.

This example source generates rows without a retained array of row strings.

{% tabs %}
{% tab title=".xui" %}

Source callbacks belong in C#, not markup.
This complete component accepts an application-supplied `ImmutableSource source` from the same window.

```text
namespace ControlExamples;
component GeneratedItems {
    view {
        VStack() {
            ItemsView("Results", ref: Items);
        }
    }
}
```

C# setup, with `ImmutableSource source` as an additional function parameter:

```csharp
var component = new ControlExamples.GeneratedItems(window);
component.Items.SetSource(source);
```

The C# tab supplies a complete source declaration.

{% endtab %}
{% tab title="C#" %}

This whole declaration belongs at file scope.

```csharp
public sealed class ExampleItems : IReadOnlyImmutableSource
{
    public ulong Count => 1000;
    public ItemKey Key(ulong index) => new(index + 1, 1);
    public ulong? Find(ItemKey key) =>
        key.Version == 1 && key.Id > 0 && key.Id <= Count ? key.Id - 1 : null;
    public ItemContent Item(ulong index, ulong column = 0) =>
        new($"Item {index + 1}", "Example row");
}
```

{% endtab %}
{% tab title="Rust" %}

This source declaration can live inside the example function.

```rust
struct ExampleItems;
impl ReadOnlyImmutableSource for ExampleItems {
    fn count(&self) -> u64 { 1000 }
    fn key(&self, index: u64) -> xui::Result<ItemKey> {
        Ok(ItemKey { id: index + 1, version: 1 })
    }
    fn find(&self, key: ItemKey) -> xui::Result<Option<u64>> {
        Ok(if key.version == 1 && key.id > 0 && key.id <= self.count() {
            Some(key.id - 1)
        } else { None })
    }
    fn item(&self, index: u64, _column: u64) -> xui::Result<ItemContent> {
        Ok(ItemContent {
            primary: format!("Item {}", index + 1), secondary: "Example row".into(),
            enabled: true, progress: None, checked: None,
        })
    }
}
let source = window.immutable_source(ExampleItems)?;
let items = window.items_view("Results")?;
items.set_source(&source)?;
root.add(&items, 1.)?;
Ok(())
```

{% endtab %}
{% tab title="C++" %}

```cpp
class ExampleItems final : public xui::ItemsSource {
public:
    std::size_t size() const override { return 1000; }
    xui::ItemKey key(std::size_t index) const override {
        return {index + 1, 1};
    }
    std::optional<std::size_t> find(xui::ItemKey value) const override {
        if (value.version != 1 || value.id == 0 || value.id > size())
            return {};
        return static_cast<std::size_t>(value.id - 1);
    }
    xui::ItemContent item(std::size_t index) const override {
        return {L"Item " + std::to_wstring(index + 1), L"Example row"};
    }
};
```

{% endtab %}
{% endtabs %}

The next fragment requires the file-scope `ExampleItems` definition:

{% tabs %}
{% tab title=".xui" %}

The setup accepts an additional `ImmutableSource source` parameter.

```text
namespace ControlExamples;
component ResultTiles {
    view {
        VStack() {
            ItemsView("Results", ref: Items);
            Text("No item activated", ref: Status);
        }
    }
}
```

C# setup:

```csharp
var component = new ControlExamples.ResultTiles(window);
component.Items.SetSource(source).SetPresentation(ItemsPresentation.Tiles).ItemSize(180, 56);
component.Items.Event += e => {
    if (e.Kind == EventKind.Click) component.Status.Text = $"Activated {e.Value}";
};
```

There is no binding setter for the C++ select-all scope.

{% endtab %}
{% tab title="C#" %}

The source declaration is in the preceding example.

```csharp
using var source = window.ImmutableSource(new ExampleItems());
var items = window.ItemsView("Results").SetSource(source)
    .SetPresentation(ItemsPresentation.Tiles).ItemSize(180, 56);
var status = window.Label("No item activated");
items.Event += e => {
    if (e.Kind == EventKind.Click) status.Text = $"Activated {e.Value}";
};
root.Add(items, 1).Add(status);
```

The control retains the attached source after the local lease is disposed.
There is no binding setter for the C++ select-all scope.

{% endtab %}
{% tab title="Rust" %}

This fragment accepts an additional `source: &ImmutableSource` parameter.

```rust
let items = window.items_view("Results")?;
items.set_source(source)?;
items.set_presentation(ItemsPresentation::Tiles)?;
items.item_size(180., 56.)?;
let status = window.label("No item activated")?;
let weak_status = status.downgrade();
items.on_event(move |event| {
    if event.kind == 1 && let Some(status) = weak_status.upgrade() {
        status.set_text(&format!("Activated {}", event.value))
            .inspect_err(|error| eprintln!("Item activation: {error}"))?;
    }
    Ok(())
})?;
root.add(&items, 1.)?;
root.add(&status, 0.)?;
Ok(())
```

There is no binding setter for the C++ select-all scope.

{% endtab %}
{% tab title="C++" %}

```cpp
auto items = std::make_shared<xui::ItemsView>(L"Results");
items->set_items(std::make_shared<const ExampleItems>());
items->set_presentation(xui::ItemsPresentation::tiles);
items->set_item_size({180, 56});
items->set_select_all_scope(xui::SelectAllScope::filtered);
auto status = std::make_shared<xui::Label>(L"No item activated");
items->on_activate([status](xui::ItemKey key) {
    status->set_text(L"Activated " + std::to_wstring(key.id));
});
root->add(items, 1);
root->add(status);
```

{% endtab %}
{% endtabs %}

`list`, `tiles`, and `grouped` share the same selection model.
Groups come from `ItemsSource::groups` and use ordered, nonoverlapping ranges.
Group IDs must not collide with item IDs.
`on_action` handles the separate inline action.
`on_selection` reports membership changes.

`CollectionSelection` separates membership, focus, and anchor.
Select-all uses a compact term instead of one entry per selected row.
Full-source selection needs an explicit full index and stable identity namespace.
The [selection contract](../collections.md#virtual-collections-and-adaptive-layout) defines the term and UIA enumeration limits.

The style target is `items_view`.
Rows, tiles, group headers, inline actions, and progress remain virtual parts.
Tile width supports base/local values only.
Root row metrics remain uniform.
Per-item colors do not imply a per-item style object.

## TreeView

Use `TreeView` for cached roots and lazily supplied children.
`TreeSource::roots` returns an immutable ItemsSource.
`has_children` must use cached, nonblocking data.

This fragment requires `std::shared_ptr<const xui::TreeSource> tree_source` from the application:

{% tabs %}
{% tab title=".xui" %}

TreeView has no markup constructor.
The setup accepts an additional `ImmutableSource source` parameter for cached roots.

```text
namespace ControlExamples;
component FolderHierarchy {
    param global::Xui.Element Tree;
    view {
        VStack() {
            Content(Tree);
        }
    }
}
```

C# setup:

```csharp
var tree = window.TreeView("Folder hierarchy").SetSource(source);
tree.Event += e => {
    if (e.Kind == EventKind.Click) Console.WriteLine($"Node {e.Value}");
};
var component = new ControlExamples.FolderHierarchy(window, tree);
```

This example supplies cached roots only. Expandable nodes also require an `OnRequest` handler.

{% endtab %}
{% tab title="C#" %}

This fragment accepts an additional `ImmutableSource source` parameter for cached roots.

```csharp
var tree = window.TreeView("Folder hierarchy").SetSource(source);
var status = window.Label("No node activated");
tree.Event += e => {
    if (e.Kind == EventKind.Click) status.Text = $"Node {e.Value}";
};
root.Add(tree, 1).Add(status);
```

The source reports cached child availability through `HasChildren`.
Expandable nodes require `OnRequest` and owner-specific `TreeRequest.Complete`.

{% endtab %}
{% tab title="Rust" %}

This fragment accepts an additional `source: &ImmutableSource` parameter for cached roots.

```rust
let tree = window.tree_view("Folder hierarchy")?;
tree.set_source(source)?;
let status = window.label("No node activated")?;
let weak_status = status.downgrade();
tree.on_event(move |event| {
    if event.kind == 1 && let Some(status) = weak_status.upgrade() {
        status.set_text(&format!("Node {}", event.value))
            .inspect_err(|error| eprintln!("Tree activation: {error}"))?;
    }
    Ok(())
})?;
root.add(&tree, 1.)?;
root.add(&status, 0.)?;
Ok(())
```

This example has no lazy provider. `on_request` uses the same subscription slot as `on_event`, so a later registration replaces it.

{% endtab %}
{% tab title="C++" %}

```cpp
auto tree = std::make_shared<xui::TreeView>(L"Folder hierarchy");
tree->set_tree(tree_source);
auto status = std::make_shared<xui::Label>(L"No node activated");
tree->on_activate([status](xui::ItemKey key) {
    status->set_text(L"Node " + std::to_wstring(key.id));
});
root->add(tree, 1);
root->add(status);
```

{% endtab %}
{% endtabs %}

For lazy children:

1. Register `on_request` before users expand a branch.
2. Start application-owned work with the supplied `TreeRequest`.
3. Honor `request.cancellation` during that work.
4. Deliver `complete(request, children, error)` on the UI thread.
5. Use a weak tree reference in completion callbacks.

The [provider recipe](../collections.md#virtual-collections-and-adaptive-layout) shows the callback shape.
`complete` returns false for obsolete, canceled, duplicate, or foreign requests.
Collapse retains cached children and hidden selection.
Right retries a failed branch.
Source callbacks must not perform filesystem or network I/O.

The style target is `tree_view`.
Disclosure, indentation, pending text, and error text extend the virtual collection parts.
Loading and error styles do not change request lifetime.
UIA resolves stable nodes, not a retained native peer per node.

## DataGrid

Use `DataGrid` for virtual tabular data.
Use layout Grid for retained form cells.

This file-scope source supplies one logical column:

{% tabs %}
{% tab title=".xui" %}

Source callbacks belong in C#.
This component accepts the source through C# setup, not a markup source declaration.

```text
namespace ControlExamples;
component RecordSourceGrid {
    view {
        VStack() {
            DataGrid("Records", ref: Records, columns: [new("Name", 240)]);
        }
    }
}
```

C# setup, with an additional `ImmutableSource source` parameter:

```csharp
var component = new ControlExamples.RecordSourceGrid(window);
component.Records.SetSource(source);
```

{% endtab %}
{% tab title="C#" %}

This whole declaration belongs at file scope.

```csharp
public sealed class ExampleGrid : IReadOnlyImmutableSource
{
    public ulong Count => 1000;
    public ItemKey Key(ulong index) => new(index + 1, 1);
    public ulong? Find(ItemKey key) =>
        key.Version == 1 && key.Id > 0 && key.Id <= Count ? key.Id - 1 : null;
    public ItemContent Item(ulong index, ulong column = 0) => new($"Record {index + 1}");
}
```

{% endtab %}
{% tab title="Rust" %}

```rust
struct ExampleGrid;
impl ReadOnlyImmutableSource for ExampleGrid {
    fn count(&self) -> u64 { 1000 }
    fn key(&self, index: u64) -> xui::Result<ItemKey> {
        Ok(ItemKey { id: index + 1, version: 1 })
    }
    fn find(&self, key: ItemKey) -> xui::Result<Option<u64>> {
        Ok(if key.version == 1 && key.id > 0 && key.id <= self.count() {
            Some(key.id - 1)
        } else { None })
    }
    fn item(&self, index: u64, _column: u64) -> xui::Result<ItemContent> {
        Ok(ItemContent {
            primary: format!("Record {}", index + 1), secondary: String::new(),
            enabled: true, progress: None, checked: None,
        })
    }
}
let source = window.immutable_source(ExampleGrid)?;
let grid = window.data_grid("Records")?;
grid.set_columns(&[GridColumn {
    name: "Name".into(), width: 240., numeric: false, filterable: false, checkable: false,
}])?;
grid.set_source(&source)?;
root.add(&grid, 1.)?;
Ok(())
```

{% endtab %}
{% tab title="C++" %}

```cpp
class ExampleGrid final : public xui::GridSource {
public:
    std::size_t size() const override { return 1000; }
    xui::RowKey key(std::size_t index) const override {
        return {index + 1, 1};
    }
    std::optional<std::size_t> find(xui::RowKey value) const override {
        if (value.version != 1 || value.id == 0 || value.id > size())
            return {};
        return static_cast<std::size_t>(value.id - 1);
    }
    std::wstring text(std::size_t row, std::size_t) const override {
        return L"Record " + std::to_wstring(row + 1);
    }
};
```

{% endtab %}
{% endtabs %}

The next fragment requires the file-scope `ExampleGrid` definition:

{% tabs %}
{% tab title=".xui" %}

The setup accepts an additional `ImmutableSource source` parameter.

```text
namespace ControlExamples;
component SelectedRecords {
    view {
        VStack() {
            DataGrid("Records", ref: Records, columns: [new("Name", 240)]);
        }
    }
}
```

C# setup:

```csharp
var component = new ControlExamples.SelectedRecords(window);
component.Records.SetSource(source).Select(new(1, 1));
```

{% endtab %}
{% tab title="C#" %}

The source declaration is in the preceding example.

```csharp
using var source = window.ImmutableSource(new ExampleGrid());
var grid = window.DataGrid("Records").SetColumns([new("Name", 240)])
    .SetSource(source).Select(new(1, 1));
root.Add(grid, 1);
```

{% endtab %}
{% tab title="Rust" %}

This fragment accepts an additional `source: &ImmutableSource` parameter.

```rust
let grid = window.data_grid("Records")?;
grid.set_columns(&[GridColumn {
    name: "Name".into(), width: 240., numeric: false, filterable: false, checkable: false,
}])?;
grid.set_source(source)?;
grid.select(ItemKey { id: 1, version: 1 })?;
root.add(&grid, 1.)?;
Ok(())
```

{% endtab %}
{% tab title="C++" %}

```cpp
auto grid = std::make_shared<xui::DataGrid>(L"Records");
grid->set_columns({{L"Name", 240}});
grid->set_source(std::make_shared<const ExampleGrid>());
grid->select({1, 1});
root->add(grid, 1);
```

{% endtab %}
{% endtabs %}

The application supplies actual sorted and filtered snapshots.
`on_sort` requests sorting. A sort indicator alone does not reorder the source.
`on_filter` supplies a cancelable `GridFilterRequest`.
`complete_filter` accepts current results on the UI thread.

Source-column identities and display ordinals are distinct.
`GridSource::text`, sort callbacks, and filters use source identities.
Width and reorder operations use display ordinals.
`set_source` preserves column order and widths.
`set_columns` resets them.
The [grid contract](../collections.md#grid-and-chart-contracts) defines every mapping.

Selection shares `CollectionSelection`.
UIA Grid, Table, selection, and scroll operations resolve stable keys.
Headers support keyboard sort, resize, and reorder without pointer-only actions.
Cancellation does not commit a pending column move.

The style target is `data_grid`.
Root row and header metrics remain uniform.
Cell and header text parts own fonts.
Styles do not replace sources, sort records, or allocate a visual tree per cell.

## HistoryChart

Use `HistoryChart` for a bounded recent metric.
It retains 60 samples.

{% tabs %}
{% tab title=".xui" %}

HistoryChart has no markup constructor.
The binding supports numeric samples, but not the C++ scale, gap, or history-read APIs.

```text
namespace ControlExamples;
component CpuHistory {
    param global::Xui.Element History;
    view {
        VStack() {
            Content(History);
        }
    }
}
```

C# setup:

```csharp
var history = window.HistoryChart("CPU usage percent");
history.Append(25);
history.Append(40);
var component = new ControlExamples.CpuHistory(window, history);
```

{% endtab %}
{% tab title="C#" %}

```csharp
var history = window.HistoryChart("CPU usage percent");
history.Append(25);
history.Append(40);
root.Add(history);
```

The binding supports numeric samples, but not the C++ scale, gap, or history-read APIs.
The missing sample is omitted, not replaced with zero.

{% endtab %}
{% tab title="Rust" %}

```rust
let history = window.history_chart("CPU usage percent")?;
history.append(25.)?;
history.append(40.)?;
root.add(&history, 0.)?;
Ok(())
```

The binding supports numeric samples, but not the C++ scale, gap, or history-read APIs.
The missing sample is omitted, not replaced with zero.

{% endtab %}
{% tab title="C++" %}

```cpp
auto history = std::make_shared<xui::HistoryChart>(L"CPU usage percent");
history->set_scale(100);
history->append(25);
history->append(std::nullopt);
history->append(40);
root->add(history);
```

{% endtab %}
{% endtabs %}

`std::nullopt` records a gap, not an invented zero.
`at(index)` reads retained history.
`Window::create_sample_task` can deliver immutable measurements without a UI sampling loop.
The [application contract](../application.md) defines worker and payload lifetime.

The accessible name must identify the metric and units.
The chart exposes read-only metric text, not an editable range.
The style target is `history_chart`.
Title, caption, grid lines, and plot line styles do not rewrite sample values.

## MillerColumns

Use `MillerColumns` for adjacent sibling lists along a hierarchy path.
The application supplies immutable snapshots and loads the next column after selection.
These examples reuse the file-scope `ExampleItems` definition from [ItemsView](#itemsview).
The sample source contains flat rows; it does not perform directory queries.

{% tabs %}
{% tab title=".xui" %}

There is no `MillerColumns(...)` markup constructor.
Create the control in C# and pass it through `Content`.

```text
namespace ControlExamples;

component HierarchyColumns {
    param global::Xui.Element Columns;
    view {
        VStack() {
            Content(Columns, flex: 1);
        }
    }
}
```

C# setup:

```csharp
using var source = window.ImmutableSource(new ExampleItems());
var columns = window.MillerColumns("Hierarchy").SetColumnWidth(240);
columns.SetColumns([new("Root", source)]);
columns.SelectionChanged += item =>
    Console.WriteLine($"Selected column {item.Column}, item {item.Key.Id}");
_ = new ControlExamples.HierarchyColumns(window, columns);
```

The component attaches its own Stack root.

{% endtab %}
{% tab title="C#" %}

```csharp
using var source = window.ImmutableSource(new ExampleItems());
var columns = window.MillerColumns("Hierarchy").SetColumnWidth(240);
columns.SetColumns([new("Root", source)]);
columns.SelectionChanged += item =>
    Console.WriteLine($"Selected column {item.Column}, item {item.Key.Id}");
root.Add(columns, 1);
```

`ItemActivated` reports explicit activation separately from selection.
`SetColumns` silently replaces the path.

{% endtab %}
{% tab title="Rust" %}

The typed wrapper currently supplies construction but not column population or typed item events.
This fragment mounts an empty control, not the populated C# or C++ example.
The raw `xui-sys` crate exposes the `xui_miller_*` ABI for applications that implement their own unsafe adapter.

```rust
let columns = window.miller_columns("Hierarchy")?;
root.add(&columns, 1.)?;
Ok(())
```

{% endtab %}
{% tab title="C++" %}

Also include `xui\miller_columns.hpp`.

```cpp
auto columns = std::make_shared<xui::MillerColumns>(L"Hierarchy");
columns->set_column_width(240);
columns->set_columns({{L"Root", std::make_shared<ExampleItems>(), {}}});
auto status = std::make_shared<xui::Label>(L"No selection");
columns->on_selection([status](std::size_t column, xui::ItemKey key) {
    status->set_text(L"Selected column " + std::to_wstring(column) +
        L", item " + std::to_wstring(key.id));
});
root->add(columns, 1);
root->add(status);
```

{% endtab %}
{% endtabs %}

Each `MillerColumnList` belongs to its `MillerColumns` owner and has no public constructor.
C++ `column_list(index)` and C# `Column(index)` return retained lists, not independently mountable controls.
Set their sources through the owner, not through a borrowed list.
The composite root has no style schema. Retained lists use the `ItemsView` schema.

The control retains at most 32 columns. Each column has independent vertical scrolling and single selection.
Column width accepts 120 to 2,000 DIPs.
The application must reject obsolete query results before it replaces descendants.
The [Miller column contract](../collections.md#miller-columns) describes focus, scrolling, activation, and cancellation.

## Language notes

C# and Rust use `ImmutableSource` for bounded source callbacks.
`Find` or `find` must provide identity lookup without enumeration.
Callbacks must stay nonblocking and must not mutate their source window.
Tree completion tokens belong to one control.
C# request objects require disposal. Rust requests cancel on drop.
The [binding contract](../bindings.md#ownership-and-data-limits) defines the exact limits and error behavior.
