#pragma once
#include "host_accessibility.hpp"
#include "xui/data_grid.hpp"

namespace xui {

struct GridSnapshot : HostAccessibilityIdentity {
    std::shared_ptr<const GridSource> grid;
    std::shared_ptr<const CollectionIndex> full_source;
    CollectionSelection selection;
    std::vector<GridColumn> columns;
    std::vector<std::size_t> column_order;
    std::optional<RowKey> selected_row;
    double grid_x{}, grid_y{}, grid_width{}, grid_height{};
    GridGeometry grid_geometry;
    std::size_t sort_column{}, header_column{};
    bool descending{}, header_focus{};
    std::vector<std::wstring> grid_filters;
    GridHeaderPart header_part{};
    bool operator==(const GridSnapshot&) const = default;
};
using GridAccessibility = RowAccessibilityState<GridSnapshot>;

IRawElementProviderSimple* create_grid_provider(std::shared_ptr<GridAccessibility> state);
void publish_grid(const std::shared_ptr<GridAccessibility>& state,
    IRawElementProviderSimple* provider, const DataGrid& control, HWND window);
void raise_grid_changes(IRawElementProviderSimple* provider, const GridSnapshot& before, const GridSnapshot& after);

}
