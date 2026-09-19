#include "xui/control_styling.hpp"

namespace xui {
const StyleTargetSchema* collections_style_schema(StyleTarget target) {
    using namespace style_states;
    using namespace style_properties;
    constexpr auto metric = [](StyleProperty value) { return style_property(value); };
    constexpr auto row_states = selected | focused | hovered | disabled;
    constexpr auto owner_states = focused | hovered | disabled;
    constexpr auto item_states = row_states | checked | expanded;
    constexpr auto tree_states = row_states | checked | expanded | loading | error;
    constexpr auto nav_states = row_states | expanded | selected_descendant | compact;
    constexpr auto menu_states = row_states | checked | open;
    static constexpr StyleStateMask order[]{compact, expanded, loading, error, open,
        selected_descendant, selected, focused, checked, hovered, disabled};
    constexpr auto root = surface | typography | metric(StyleProperty::row_height);
    constexpr auto all_state_properties = ~StylePropertyMask(0);
    constexpr StyleValueLimits text_limits{.vertical_alignments = 7};
    constexpr auto face = surface & ~(metric(StyleProperty::padding) | metric(StyleProperty::foreground));
    constexpr auto decoration = face | metric(StyleProperty::foreground) | metric(StyleProperty::size);
    // Uniform row height belongs to the root. Tile width is base/local only.
    // Item-state padding changes the content box, not the virtual row extent.
    static constexpr StylePartSchema files[]{
        {StylePart::root, root, owner_states, {}},
        {StylePart::row, surface, row_states, StylePart::root},
        {StylePart::primary_text, text, row_states, StylePart::row, all_state_properties, StylePart::root, text_limits},
        {StylePart::secondary_text, text, row_states, StylePart::row, all_state_properties, StylePart::root, text_limits},
        {StylePart::icon, metric(StyleProperty::foreground), row_states, StylePart::row},
        {StylePart::selected_marker, decoration, row_states, StylePart::row},
        {StylePart::focus_marker, decoration, row_states, StylePart::row},
        {StylePart::scrollbar, metric(StyleProperty::width), disabled, {}},
        {StylePart::scrollbar_track, face, disabled, StylePart::root},
        {StylePart::scrollbar_thumb, face, disabled, StylePart::root},
        {StylePart::empty, text, disabled, StylePart::root, all_state_properties, StylePart::root, text_limits},
    };
    static constexpr StylePartSchema items[]{
        {StylePart::root, root, owner_states, {}},
        {StylePart::row, surface, item_states, StylePart::root},
        {StylePart::tile, surface | metric(StyleProperty::width), item_states, StylePart::row, surface},
        {StylePart::primary_text, text, item_states, StylePart::row, all_state_properties, StylePart::root, text_limits},
        {StylePart::secondary_text, text, item_states, StylePart::row, all_state_properties, StylePart::root, text_limits},
        {StylePart::icon, metric(StyleProperty::foreground), item_states, StylePart::row},
        {StylePart::selected_marker, decoration, item_states, StylePart::row},
        {StylePart::focus_marker, decoration, item_states, StylePart::row},
        {StylePart::scrollbar, metric(StyleProperty::width), disabled, {}},
        {StylePart::scrollbar_track, face, disabled, StylePart::root},
        {StylePart::scrollbar_thumb, face, disabled, StylePart::root},
        {StylePart::action, surface | text, item_states, StylePart::row, all_state_properties, StylePart::root, text_limits},
        {StylePart::track, surface | metric(StyleProperty::thickness), item_states, StylePart::row},
        {StylePart::fill, face, item_states, StylePart::track},
        {StylePart::group_header, surface | text, item_states, StylePart::root, all_state_properties, StylePart::root, text_limits},
        {StylePart::disclosure, metric(StyleProperty::foreground), item_states, StylePart::row},
        {StylePart::mark, metric(StyleProperty::foreground), item_states, StylePart::row},
        {StylePart::shortcut, surface | text, item_states, StylePart::row, all_state_properties, StylePart::root, text_limits},
        {StylePart::empty, text, disabled, StylePart::root, all_state_properties, StylePart::root, text_limits},
    };
    static constexpr StylePartSchema trees[]{
        {StylePart::root, root | metric(StyleProperty::indentation), owner_states, {}},
        {StylePart::row, surface, tree_states, StylePart::root},
        {StylePart::primary_text, text, tree_states, StylePart::row, all_state_properties, StylePart::root, text_limits},
        {StylePart::secondary_text, text, tree_states, StylePart::row, all_state_properties, StylePart::root, text_limits},
        {StylePart::icon, metric(StyleProperty::foreground) | metric(StyleProperty::size), tree_states, StylePart::row},
        {StylePart::selected_marker, decoration, tree_states, StylePart::row},
        {StylePart::focus_marker, decoration, tree_states, StylePart::row},
        {StylePart::scrollbar, metric(StyleProperty::width), disabled, {}},
        {StylePart::scrollbar_track, face, disabled, StylePart::root},
        {StylePart::scrollbar_thumb, face, disabled, StylePart::root},
        {StylePart::action, surface | text, tree_states, StylePart::row, all_state_properties, StylePart::root, text_limits},
        {StylePart::track, surface | metric(StyleProperty::thickness), tree_states, StylePart::row},
        {StylePart::fill, face, tree_states, StylePart::track},
        {StylePart::disclosure, metric(StyleProperty::foreground), tree_states, StylePart::row},
        {StylePart::pending, text, tree_states, StylePart::row, all_state_properties, StylePart::secondary_text, text_limits},
        {StylePart::error, text, tree_states, StylePart::row, all_state_properties, StylePart::secondary_text, text_limits},
        {StylePart::mark, metric(StyleProperty::foreground), tree_states, StylePart::row},
        {StylePart::empty, text, disabled, StylePart::root, all_state_properties, StylePart::root, text_limits},
    };
    static constexpr StylePartSchema navigation[]{
        {StylePart::root, root | metric(StyleProperty::indentation), owner_states, {}},
        {StylePart::row, surface, nav_states, StylePart::root},
        {StylePart::primary_text, text, nav_states, StylePart::row, all_state_properties, StylePart::root, text_limits},
        {StylePart::secondary_text, text, nav_states, StylePart::row, all_state_properties, StylePart::root, text_limits},
        {StylePart::icon, metric(StyleProperty::foreground) | metric(StyleProperty::size), nav_states, StylePart::row},
        {StylePart::selected_marker, decoration, nav_states, StylePart::row},
        {StylePart::focus_marker, decoration, nav_states, StylePart::row},
        {StylePart::scrollbar, metric(StyleProperty::width), disabled, {}},
        {StylePart::scrollbar_track, face, disabled, StylePart::root},
        {StylePart::scrollbar_thumb, face, disabled, StylePart::root},
        {StylePart::group_header, surface | text, nav_states, StylePart::root, all_state_properties, StylePart::root, text_limits},
        {StylePart::disclosure, metric(StyleProperty::foreground), nav_states, StylePart::row},
        {StylePart::badge, surface | text, nav_states, StylePart::row, all_state_properties, StylePart::root, text_limits},
        {StylePart::empty, text, disabled, StylePart::root, all_state_properties, StylePart::root, text_limits},
    };
    static constexpr StylePartSchema menus[]{
        {StylePart::root, root, owner_states, {}},
        {StylePart::row, surface, menu_states, StylePart::root},
        {StylePart::primary_text, text, menu_states, StylePart::row, all_state_properties, StylePart::root, text_limits},
        {StylePart::secondary_text, text, menu_states, StylePart::row, all_state_properties, StylePart::root, text_limits},
        {StylePart::icon, metric(StyleProperty::foreground), menu_states, StylePart::row},
        {StylePart::selected_marker, decoration, menu_states, StylePart::row},
        {StylePart::focus_marker, decoration, menu_states, StylePart::row},
        {StylePart::scrollbar, metric(StyleProperty::width), disabled, {}},
        {StylePart::scrollbar_track, face, disabled, StylePart::root},
        {StylePart::scrollbar_thumb, face, disabled, StylePart::root},
        {StylePart::action, surface | text, menu_states, StylePart::row, all_state_properties, StylePart::root, text_limits},
        {StylePart::group_header, surface | text, disabled, StylePart::root, all_state_properties, StylePart::root, text_limits},
        {StylePart::separator, face | metric(StyleProperty::thickness), disabled, StylePart::root},
        {StylePart::mark, metric(StyleProperty::foreground), menu_states, StylePart::row},
        {StylePart::shortcut, surface | text, menu_states, StylePart::row, all_state_properties, StylePart::root, text_limits},
        {StylePart::arrow, metric(StyleProperty::foreground), menu_states, StylePart::row},
        {StylePart::empty, text, disabled, StylePart::root, all_state_properties, StylePart::root, text_limits},
    };
    static const StyleTargetSchema schemas[]{{files, order}, {items, order}, {trees, order}, {navigation, order}, {menus, order}};
    switch (target) {
    case StyleTarget::file_list: return &schemas[0];
    case StyleTarget::items_view: return &schemas[1];
    case StyleTarget::tree_view: return &schemas[2];
    case StyleTarget::navigation_list: return &schemas[3];
    case StyleTarget::command_menu: return &schemas[4];
    default: return nullptr;
    }
}
}
