#pragma once

#include "host_accessibility.hpp"
#include "collection_presentation.hpp"
#include "xui/data_grid.hpp"

namespace xui {

struct CollectionSnapshot : HostAccessibilityIdentity {
    std::shared_ptr<const ItemsSource> collection;
    std::shared_ptr<const detail::CollectionPresentation> collection_presentation;
    CollectionSelection selection;
    bool single_selection{};
    std::size_t collection_columns{1};
    std::vector<GridColumn> collection_details;
    double collection_item_height{56}, collection_offset{}, collection_width{}, collection_height{};
    double collection_viewport_x{}, collection_viewport_y{};
    bool operator==(const CollectionSnapshot&) const = default;
};
using CollectionAccessibility = RowAccessibilityState<CollectionSnapshot>;

IRawElementProviderSimple* create_collection_provider(std::shared_ptr<CollectionAccessibility> state);
void publish_collection(const std::shared_ptr<CollectionAccessibility>& state,
    IRawElementProviderSimple* provider, const VirtualCollection& control, HWND window);
void raise_collection_changes(IRawElementProviderSimple* provider,
    const CollectionSnapshot& before, const CollectionSnapshot& after);

}
