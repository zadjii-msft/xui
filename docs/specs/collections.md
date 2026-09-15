# Virtual collections and asynchronous data

Build and test commands are in [CONTRIBUTING](../../CONTRIBUTING.md).
See the [reference index](README.md) for related APIs.
Examples in this reference use C++ unless stated otherwise.
For C# and Rust coverage, use the [binding reference](bindings.md).

## Grid and chart contracts

`include\xui\data_grid.hpp` supplies `GridSource`, `RowKey`, `DataGrid`, and `HistoryChart`.
A source supplies row count, key lookup, and cell text. It must remain immutable and thread-safe.
Keys must be unique within the source and must never identify a different item.
The process source uses PID plus creation time.
An inaccessible process gets a snapshot-local key, so later PID reuse cannot select it silently.
The grid supports at most `INT_MAX` rows and 64 columns.

The renderer requests text only for visible rows plus one boundary row.
It retains no visual, native window, provider cache, or string cache for each row.
Selection uses `CollectionSelection` across refresh, sort, and filter changes.
The compatibility `selected()` accessor returns the focused row key. `selection().contains(key)` reports actual membership.
A missing or filtered key cannot activate a process. A provider for a missing identity rejects later actions.
`ScrollIntoView` changes the viewport without selection.
Column headers expose Invoke for sorting. F6 and arrow keys provide the keyboard equivalent.

`set_columns` assigns source identities from zero in the supplied order. It resets the display order and widths.
Column names do not serve as identities. Duplicate names are valid.
`columns()` returns columns in display order. `column_order()` maps each display ordinal to its source identity.
`source_column(ordinal)` returns that identity. `display_column(identity)` returns its current ordinal, or no value for an invalid identity.
`GridSource::text`, `sort`, `set_sort`, `sort_column`, and sort callbacks use source identities.
`resize_column`, `set_column_width`, `focused_column`, and `column_at` use display ordinals.
`set_column_width` sets an exact width from 48 to 2,000 DIPs without a change to column order.
An invalid ordinal or width throws `std::invalid_argument` without changes.
`reorder_column(from, to)` moves a column between display ordinals. The destination is its final position.
An invalid move returns `false` without changes. A same-position move returns `true` without changes.
`set_column_order` accepts a complete permutation of source identities.
An incorrect size, duplicate identity, or out-of-range identity throws `std::invalid_argument` without changes.
An unconfigured grid accepts an empty permutation. `set_columns` requires 1 to 64 columns.
Each move preserves the column width, numeric format, sort identity, focused identity, and selected row key.
Snapshot replacement through `set_source` preserves column order and widths.
Task Manager uses this path for refresh, sort, filter, pause, resume, and page changes.

The existing mouse resize uses a five-DIP boundary and a horizontal resize cursor.
Mouse and keyboard resizing use widths from 64 to 1,000 DIPs.
A header move starts after six DIPs of pointer movement. A click without a drag sorts on release.
The accent-colored marker shows the insertion position. Pointer movement near a viewport edge scrolls horizontally.
Keyboard header navigation also reveals offscreen columns. There is no drag timer or idle render loop.
Escape, focus loss, capture loss, and window closure cancel an uncommitted column move.
UIA headers retain Invoke for sorting. Their HelpText describes the keyboard resize and move commands.
The grid does not advertise UIA Drag or DropTarget patterns.
Retained cell and header providers use source identities. Their bounds and GridItem column ordinals follow the current display order.
Table headers and cell-header associations use the same mapping.
Column changes publish a locked snapshot and raise layout and structure invalidation events, without a provider for every cell.

The grid exposes UIA Grid, Table, Selection, Scroll, GridItem, TableItem, SelectionItem, Invoke, and ScrollItem patterns where applicable.
Virtual providers resolve stable keys against immutable snapshots.
The host processes value-only action tokens on the UI thread.
UIA events use root-level updates and selected-item changes, not thousands of per-cell events.
Charts expose their current metric and scale as accessible text. They do not advertise an editable value pattern.
The controls use the existing window render target and theme palette.

The [binding reference](bindings.md#current-coverage) describes the available grid, chart, and page APIs in C# and Rust.

## File drag and drop

`DataGrid` supports native OLE file drag sources and drop targets on Windows.
The C++ methods are `on_file_drag` and `on_file_drop`.
The equivalent C# methods return the same grid for fluent configuration:

```csharp
grid.OnFileDrag(
    () => SelectedPaths(),
    effect => RefreshLocations());

grid.OnFileDrop(
    (key, requested) => CanReceiveFiles(key) ? requested : FileTransferEffect.None,
    (key, paths, requested) =>
        window.TransferFiles(paths, DestinationFor(key), requested)
            ? requested : FileTransferEffect.None);
```

The query callback decides acceptance without filesystem work.
The drop callback runs only after release over an accepted target.
It returns the requested effect only after all work completes, or None for cancellation or skipped work.
An error can throw through the existing callback-error contract. Applications can catch errors to display their own status.

The key identifies the source row under the pointer, not its display ordinal.
A null key identifies empty grid body space.
Headers, scrollbars, disabled controls, hidden controls, and nonselectable rows reject the drop before the query callback.
The query must reject nonfolder rows and locations that are not ready.
XUI repeats the query at drop time rather than reusing an earlier acceptance result.

A left-button press on a selected row preserves the current multiselection.
A release without a drag applies ordinary click selection.
Ctrl and Shift retain their selection gestures.
The path factory runs once after movement reaches the Windows drag threshold.
XUI then releases pointer capture before the OLE message loop starts.
Escape, capture loss before drag, closure, and hidden or disabled sources cancel the gesture.

The source allows Copy and Move, but never Link.
A same-process XUI pane drop defaults to Move.
An external drop defaults to Copy unless its data object supplies a preferred effect.
Ctrl requests Copy, and Shift requests Move.
Ctrl+Shift and Alt reject the drop because link operations are not supported.
The accepted effect must also exist in the source effect mask.

The target reports completion only after its drop callback succeeds.
For a completed move, XUI uses Shell optimized-move notifications so an external source does not delete files again.
The source completion callback reports the logical effect when the target supplies it.
The completion callback must not delete source files.
External target completion depends on the target's OLE result and Shell notifications.
XUI supports optimized filesystem moves, as used by Windows Explorer.
For a conventional target that requests source-side deletion, XUI retains the originals and reports Copy instead of an unfinished Move.

`Selection` reports focus, not the complete selected set.
Applications can enumerate their immutable row model and use `DataGrid.Contains(key)` to obtain exact membership.
A context-menu press on a selected row also preserves multiselection.
`ClearFileTransferCallbacks` removes both managed drag and drop callbacks.
The [binding reference](bindings.md#windows-file-transfers) defines clipboard methods, path limits, cancellation, and error behavior.

## Virtual lists and asynchronous delivery

`FileList` derives from `Control`. A `Window` accepts it beside labels, buttons, toggles, and text inputs.
The native backend creates one list window, not one control per row.
The application supplies data and actions. It does not supply paint callbacks or accessibility providers.

This example uses the same public APIs as the real browser:

```cpp
#include "xui/application.hpp"

int run_results() {
    xui::Window window({L"Results", {600, 480}});
    auto root = std::make_shared<xui::Stack>(xui::Axis::vertical);
    auto search = std::make_shared<xui::TextInput>(L"Filter");
    auto results = std::make_shared<xui::FileList>(L"Results");
    root->add(std::make_shared<xui::Label>(L"Available files"));
    root->add(search);
    root->add(results, 1);
    window.set_content(root);
    auto task = window.create_view_task(
        [](const xui::CancelCheck& cancel) {
            auto items = std::make_shared<const std::vector<xui::FileItem>>(
                std::vector<xui::FileItem>{
                    {1, L"Alpha.txt", L"C:\\Data\\Alpha.txt", false},
                    {2, L"Beta.txt", L"C:\\Data\\Beta.txt", false}});
            return xui::SourceResult{xui::FileSnapshot::build(items, cancel), {}};
        },
        [results](xui::ViewResult result) {
            if (result.view) results->set_view(std::move(result.view));
        });
    search->on_change([task](const std::wstring& query) { task->request(query); });
    task->request(L"");
    return xui::Application::run(window);
}
```

The task owns one worker and a bounded result mailbox. The window waits for a shared task event without polling.
The loader runs off the UI thread. Result callbacks run on the window thread.
Query changes cancel obsolete filters. `request(query, true)` also cancels and reloads the source.
Only the latest generation reaches the callback. `generation`, `applied_generation`, and `busy` expose task progress.
The loader must own its data and honor `CancelCheck`. Shared captures keep source services alive until cancellation finishes.

Retain the task handle while the task is necessary. Handle destruction, `cancel`, and window closure revoke delivery.
Cancellation is permanent. Later requests return zero.
`Window::close` revokes tasks immediately, including during a result callback.
The backend joins cancelled workers on a cleanup thread. Window closure does not join the worker on the UI thread.
Result delivery and list replacement also arrange off-thread snapshot disposal.
Retained controls keep their model after closure. Their final snapshot disposal uses the same cleanup facility.
Process exit waits for cleanup. A loader that ignores cancellation can still delay process exit.

## Virtual collections and adaptive layout

### Miller columns

`xui/miller_columns.hpp` supplies `MillerColumns` and `MillerColumn`.
Miller columns display a hierarchy as adjacent lists.
Each column contains siblings. A selected branch identifies the next column.
The control is independent of the filesystem.

`set_columns` replaces the complete path without selection or activation callbacks.
Each descriptor supplies a title, an immutable `ItemsSource`, and an optional selected `ItemKey`.
A null C++ source displays an empty column.
`ItemsSource::hierarchy` supplies the `expandable` flag for branch indicators.
The control retains at most 32 columns and requests content only for visible rows.
Each column has independent vertical scrolling and single selection.

`on_selection` reports the column index and the complete item key.
The application loads children and replaces descendants after successful delivery.
Selection does not activate an item.
`on_activate` reports explicit activation through Enter or a double-click.
Applications must cancel obsolete work and reject obsolete results before they call `set_columns`.
Source methods must not perform filesystem or network work.

`set_active_column` reveals a column horizontally.
Left and Right move between existing columns. Up and Down move within a column.
Horizontal wheel input and Shift+wheel scroll the path without changing selection or the active column.
A bottom scrollbar supports thumb dragging and track paging when the path exceeds the viewport.
Ordinary wheel input scrolls the current list vertically.
Manual horizontal positions survive layout updates. Column activation and width changes reveal the active column.
`horizontal_offset`, `maximum_horizontal`, and `set_horizontal_offset` expose the horizontal position in DIPs.
`scroll_horizontal` applies a relative movement and clamps the result to the available range.

Column headers identify the sibling lists for accessibility.
The lists use the existing virtual collection renderer and UIA providers.
The Miller container exposes the horizontal UIA Scroll pattern.
The control does not create a native window for each row.

The column width accepts 120 to 2,000 DIPs.
Invalid widths, indices, sources, selections, and excessive depth produce explicit errors.
The [binding reference](bindings.md#miller-columns-in-c) describes the C# API.

### Shared collection model

Include `xui/collections.hpp` for `CollectionSelection`, `ItemsSource`, `ItemsView`, `TreeSource`, and `TreeView`.
Include `xui/adaptive_layout.hpp` for `Grid`, `Wrap`, and `AdaptiveLayout`.
These controls do not add a native window or render target for each item.
The [binding reference](bindings.md#advanced-api-gaps) describes the remaining C# and Rust limits.

`CollectionIndex` supplies a count, a stable `ItemKey`, and identity lookup.
`ItemKey` contains an ID and a version. `RowKey` is a compatible alias.
Sources are immutable and thread-safe. Identity lookup must not scan all rows.
`ItemsSource::item(index)` supplies primary text, secondary text, an icon, optional progress, and an optional inline action.
The shared row renderer requests only visible content. It omits secondary text and inline buttons when the available space is too small.

`ItemsView` supports list, tile, and grouped presentations through `set_presentation`.
Groups describe ordered, nonoverlapping source ranges. Group IDs must not collide with item IDs.
Group headers receive focus but do not join item selection. Filtered select-all includes data in collapsed groups.
Collapse changes a small range projection, not an array of item controls.
Hidden selected and focused IDs remain in the selection model.
Arrow input repairs a hidden focus location without activating its item.
Source replacement must preserve the identity namespace or supply new versions for reused IDs.

```cpp
auto items = std::make_shared<xui::ItemsView>(L"Results");
items->set_items(filtered_source, full_source);
items->set_presentation(xui::ItemsPresentation::tiles);
items->set_item_size({180, 56});
items->set_select_all_scope(xui::SelectAllScope::filtered);
items->on_action(open_inline_details);
```

`CollectionSelection` separates focus, anchor, and membership.
Ctrl+click and Space toggle membership. Shift extends a range. Ctrl+arrows move focus without replacing membership.
A tile drag selects a rectangle. Escape cancels the drag.
F2 invokes the focused inline action without activating the row.
Ctrl+A uses the configured `SelectAllScope`.
Full-source scope requires an explicit index that contains every displayed identity.

Ranges and rectangles retain an immutable index plus compact bounds.
Select-all uses one term. Exceptions add point terms.
The model rejects more than 4,096 terms without changing the selection.
Replacement selection releases earlier terms.
Large header summaries never scan the source.
`CollectionIndex::contains_all` can prove domain containment across reordered or filtered snapshots without enumeration.

An explicit full-source index also supplies that containment contract.
Without a containment proof, a large selection from another snapshot has a conservative mixed summary.
Individual membership remains exact.
UIA Selection returns at most 256 identities. Larger or uncounted selections return `UIA_E_INVALIDOPERATION`, not a truncated selected array.
UIA clients can use ItemContainer, SelectionItem, and VirtualizedItem to inspect or reveal individual items.
The fragment tree contains visible rows and required tree ancestors, not every source row.

`TreeView` has a separate hierarchy contract.
`TreeSource::roots` supplies a virtual root index. `has_children` uses cached, nonblocking data.
Right expands a branch or enters its first child. Left collapses a branch or selects its parent.
Collapse retains hidden selection and repairs focus to the collapsed ancestor.
The tree retains at most 4,096 branch records and permits at most 128 expansion levels.
Closed branches retain cached children until source replacement or tree destruction.

```cpp
tree->on_request([weak_tree, start_query](xui::TreeRequest request) {
    // start_query owns worker execution and UI-thread delivery.
    start_query(request, [weak_tree, request](auto children, auto error) {
        if (auto owner = weak_tree.lock())
            owner->complete(request, std::move(children), std::move(error));
    });
});
```

Applications start their own asynchronous work. They must deliver `complete` on the UI thread.
Requests carry an owner-specific cancellation token and generation.
Collapse, cancellation, focus departure during a pending request, disable, hide, source replacement, and owner closure cancel affected work.
Late delivery returns `false`. Error text stays visible and accessible. Right retries a failed branch.
Source methods must not perform filesystem or network work on the UI thread.

`Grid` supports fixed, automatic, and weighted tracks, cell spans, gaps, padding, and child size constraints.
`Wrap` derives its column count from available width and measures each retained child.
`AdaptiveLayout` retains one navigation subtree and one content subtree.
Wide layouts place them side by side. Compact layouts stack them or display navigation over the content.
`set_compact_navigation(CompactNavigation::overlay)` selects the nonmodal overlay recipe.
`set_navigation_open` controls that compact overlay. Escape from navigation closes it.

Inline and overlay transitions keep the same controls, selected IDs, and focused navigation peer.
The overlay uses the root target and masks native fields under its rectangle.
It is client-bound and is not an anchored popup or a modal dialog.
Nested retained popups still use the separate `Window::show_popup` contract.

DataGrid adds `filterable` and `checkable` flags at the end of `GridColumn`.
Existing `GridSource` implementations require no new methods.
Check columns represent the shared row selection, not independently editable Boolean data.
Header checks select or clear all rows under the configured scope. Indeterminate state represents mixed or uncounted membership.
Column filters, sort state, and check flags follow source column identity through resize and reorder.

`set_filter` and `set_checked` are silent setters.
`filter` starts an external query through `on_filter`. Its `GridFilterRequest` contains all filters, a generation, and a cancellation token.
`complete_filter` accepts only the current request. Direct source replacement, cancellation, and owner closure invalidate pending requests.
`on_filter_open` lets an application compose a native text editor in a retained popup.
The gallery filter accepts `even` or an empty string and uses synthetic data only.

F6 switches between grid rows and headers.
F4 selects the sort, filter, or check part of a header. Enter or Space invokes that part.
Header sort exposes Invoke. Header filter exposes Value and Invoke. Header and cell checks expose Toggle.
SelectionItem remains separate from Toggle. UIA focus never replaces row selection.
All UIA mutation uses bounded action mailboxes on the UI thread.

Collection tests cover million-row sources, bounded selection terms, visible content queries, cancellation, stale results, and layout identity.
Native tests cover real Win32 input, external-process UIA, repeated popup cycles, native overlay clipping, target recovery, and zero idle paints.
The theme matrix uses dark, light, high contrast, and injected 96/144/192 DPI.
Physical monitor changes and screen-reader speech remain manual checks.
