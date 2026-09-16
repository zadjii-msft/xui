#pragma once

// Shared sparse styles. ButtonStyle remains a separate, compatible API.

#include "xui/core.hpp"
#include "xui/styling.hpp"
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace xui {

// Each target supplies a schema to the common resolver.
enum class StyleTarget : uint32_t {
    toggle = 0, button, label, text_input, multiline_text, rich_text, password_input, date_time_picker,
    radio_group, choice_list, combo_box, numeric_input, range_input, progress, inline_status, color_picker,
    stack, grid, wrap, adaptive_layout, page_view, content_view, scroll_view, split_view, expander, popup,
    file_list, items_view, tree_view, navigation_list, command_menu, data_grid, history_chart,
    navigation_view, breadcrumb, navigation_pane, location_picker, view_picker, tab_strip, split_button,
    command_bar, command_surface, command_palette, shell_menu, content_dialog, title_bar, tooltip,
    image, vector_canvas, map_view, media_playback, web_content
};

// Parts are presentation roles, not controls or accessibility peers.
enum class StylePart : uint32_t {
    root = 0, label, indicator, mark, text, icon, content, header, footer, separator, field, placeholder,
    clear_action, shortcut, reveal_action, decrement, increment, item, selected_marker, arrow, popup,
    track, fill, thumb, caption, message, action, dismiss, stripe, preview, checkerboard, swatch, channel,
    row, secondary_text, focus_marker, scrollbar, scrollbar_track, scrollbar_thumb, tile, group_header,
    disclosure, pending, error, cell, grid_line, sort_icon, filter_icon, reorder_marker, alternating_row,
    title, plot, pane, divider, grip, tab, close_action, add_action, overflow, search, primary_action,
    cancel_action, selection, empty, validation, badge, toolbar, coordinate, status, primary_text,
    heading, first_pane, second_pane, checkerboard_light, checkerboard_dark, channel_label, channel_field,
    caption_button, caption_close
};

// Storage depends on authored rules, not the number of state combinations.
using StyleStateMask = std::uint64_t;
namespace style_states {
inline constexpr StyleStateMask focused = 1ull << 0;
inline constexpr StyleStateMask checked = 1ull << 1;
inline constexpr StyleStateMask hovered = 1ull << 2;
inline constexpr StyleStateMask pressed = 1ull << 3;
inline constexpr StyleStateMask disabled = 1ull << 4;
inline constexpr StyleStateMask selected = 1ull << 5, expanded = 1ull << 6, invalid = 1ull << 7,
    loading = 1ull << 8, error = 1ull << 9, empty = 1ull << 10, open = 1ull << 11,
    dragging = 1ull << 12, minimum = 1ull << 13, maximum = 1ull << 14, indeterminate = 1ull << 15,
    paused = 1ull << 16, unknown = 1ull << 17, information = 1ull << 18, success = 1ull << 19,
    warning = 1ull << 20, dismissed = 1ull << 21, revealed = 1ull << 22, read_only = 1ull << 23,
    sorted = 1ull << 24, descending = 1ull << 25, filtered = 1ull << 26, mixed = 1ull << 27,
    filter_pending = 1ull << 28, selected_descendant = 1ull << 29, compact = 1ull << 30,
    overflowed = 1ull << 31, current = 1ull << 32, active = 1ull << 33, inactive = 1ull << 34,
    maximized = 1ull << 35, ready = 1ull << 36, idle = 1ull << 37, playing = 1ull << 38,
    stopped = 1ull << 39, suspended = 1ull << 40, scrollable = 1ull << 41, determinate = 1ull << 42;
inline constexpr StyleStateMask interaction = focused | hovered | pressed | disabled;
}

enum class StyleProperty : std::uint32_t {
    background = 1u << 0, foreground = 1u << 1, border_brush = 1u << 2,
    border_thickness = 1u << 3, padding = 1u << 4, corner_radius = 1u << 5, size = 1u << 6,
    font_family = 1u << 7, font_size = 1u << 8, font_weight = 1u << 9, font_style = 1u << 10,
    horizontal_alignment = 1u << 11, vertical_alignment = 1u << 12, spacing = 1u << 13,
    row_height = 1u << 14, header_height = 1u << 15, indentation = 1u << 16, thickness = 1u << 17,
    width = 1u << 18, height = 1u << 19, row_gap = 1u << 20, column_gap = 1u << 21,
    maximum_lines = 1u << 22, wrapping = 1u << 23
};
using StylePropertyMask = std::uint64_t;
constexpr StylePropertyMask style_property(StyleProperty property) { return static_cast<StylePropertyMask>(property); }
namespace style_properties {
inline constexpr StylePropertyMask surface = 63;
inline constexpr StylePropertyMask typography = style_property(StyleProperty::font_family) |
    style_property(StyleProperty::font_size) | style_property(StyleProperty::font_weight) | style_property(StyleProperty::font_style);
inline constexpr StylePropertyMask alignment = style_property(StyleProperty::horizontal_alignment) | style_property(StyleProperty::vertical_alignment);
inline constexpr StylePropertyMask text = style_property(StyleProperty::foreground) | typography | alignment;
}
enum class StyleFontStyle : uint32_t { normal, italic, oblique };
enum class StyleAlignment : uint32_t { start, center, end, stretch };

// Both encodings belong to one immutable allocation owner, never to a virtual row.
class StyleFontFamily final {
public:
    const std::string utf8;
    const std::wstring name;
private:
    StyleFontFamily(std::string text, std::wstring native) : utf8(std::move(text)), name(std::move(native)) {}
    friend std::shared_ptr<const StyleFontFamily> make_style_font_family(std::string_view utf8);
};
std::shared_ptr<const StyleFontFamily> make_style_font_family(std::string_view utf8);

struct StyleValueLimits {
    float maximum_font_size{32768};
    uint32_t maximum_font_family_utf16{1024};
    uint32_t font_styles{7};
    uint32_t horizontal_alignments{15};
    uint32_t vertical_alignments{15};
};
struct StylePartSchema {
    StylePart part;
    StylePropertyMask allowed;
    StyleStateMask states;
    std::optional<StylePart> foreground_from;
    StylePropertyMask state_allowed{~StylePropertyMask(0)};
    std::optional<StylePart> typography_from;
    StyleValueLimits limits;
};
struct StyleTargetSchema {
    std::span<const StylePartSchema> parts;
    std::span<const StyleStateMask> precedence;
};
// Family fragments return nullptr for targets they do not implement.
const StyleTargetSchema* basic_text_style_schema(StyleTarget target);
const StyleTargetSchema* native_fields_style_schema(StyleTarget target);
const StyleTargetSchema* choices_status_style_schema(StyleTarget target);
const StyleTargetSchema* layouts_style_schema(StyleTarget target);
const StyleTargetSchema* collections_style_schema(StyleTarget target);
const StyleTargetSchema* data_grid_chart_style_schema(StyleTarget target);
const StyleTargetSchema* navigation_composites_style_schema(StyleTarget target);
const StyleTargetSchema* hosts_scenes_style_schema(StyleTarget target);
const StyleTargetSchema& control_style_schema(StyleTarget target);

// The target/part schema rejects unsupported properties. Zero remains set.
// Toggle's size is the indicator's outer square size in DIPs.
struct PartStyleValues {
    std::optional<ThemeColor> background, foreground, border_brush;
    std::optional<Insets> border_thickness, padding;
    std::optional<float> corner_radius, size;
    std::shared_ptr<const StyleFontFamily> font_family;
    std::optional<float> font_size;
    std::optional<uint32_t> font_weight;
    std::optional<StyleFontStyle> font_style;
    std::optional<StyleAlignment> horizontal_alignment, vertical_alignment;
    std::optional<float> spacing, row_height, header_height, indentation, thickness, width, height, row_gap, column_gap;
    std::optional<uint32_t> maximum_lines;
    std::optional<bool> wrapping;
    bool empty() const;
};

// A rule always carries exactly one positive state bit for this pilot.
struct StyleRule { StylePart part; StyleStateMask state; PartStyleValues values; };

// Invalid target, part, property, color, or dimension throws invalid_argument.
void validate_part(StyleTarget target, StylePart part);

void validate_part_values(StyleTarget target, StylePart part, const PartStyleValues& values);

PartStyleValues merge_part_values(PartStyleValues base, const PartStyleValues& overlay);
bool part_style_layout_equal(const PartStyleValues& first, const PartStyleValues& second);
bool part_style_values_equal(const PartStyleValues& first, const PartStyleValues& second);
Insets style_content_insets(const PartStyleValues* values, Insets default_padding = {}, Insets default_border = {});
Rect style_content_bounds(Rect bounds, const PartStyleValues* values, Insets default_padding = {}, Insets default_border = {});

// Immutable definition with separate ordinary values and inherited state buckets.
class ControlStyle final {
public:
    ControlStyle(const ControlStyle&) = delete;
    ControlStyle& operator=(const ControlStyle&) = delete;
    // Inputs are copied and validated before publication. The base is unchanged.
    static std::shared_ptr<const ControlStyle> create(StyleTarget target,
        std::vector<std::pair<StylePart, PartStyleValues>> base_values,
        std::vector<StyleRule> rules, std::shared_ptr<const ControlStyle> based_on = {});
    StyleTarget target() const { return target_; }
    // Applies schema precedence. Unauthored parts return nullopt.
    std::optional<PartStyleValues> resolve(StylePart part, StyleStateMask mask) const;
    // Throws for a part unsupported by this target (see validate_part).
    bool has_part(StylePart part) const;
    std::vector<StylePart> authored_parts() const;
private:
    ControlStyle() = default;
    struct Bucket { StyleStateMask state; PartStyleValues values; };
    struct CompiledPart { StylePart part; PartStyleValues base; std::vector<Bucket> buckets; };
    StyleTarget target_{};
    unsigned depth_{1};
    std::vector<CompiledPart> parts_;
};

// Optional Element-owned storage. Only assignment can allocate.
// State changes update existing slots. Schema dependents are tracked automatically.
class ControlStyleAttachment final {
public:
    explicit ControlStyleAttachment(StyleTarget target);
    ControlStyleAttachment(const ControlStyleAttachment&) = delete;
    ControlStyleAttachment& operator=(const ControlStyleAttachment&) = delete;
    StyleTarget target() const { return target_; }
    std::shared_ptr<const ControlStyle> style() const { return style_; }
    bool empty() const { return !style_ && parts_.empty(); }
    // True when the owner can release the attachment.
    bool fully_empty() const;
    void set_context_enabled(bool enabled) { context_enabled_ = enabled; }
    bool context_enabled() const { return context_enabled_; }
    // Throws for a part unsupported by this target (see validate_part).
    const PartStyleValues& local(StylePart part) const;
    const PartStyleValues& projection(StylePart part) const;
    // No allocation. Untracked parts return nullptr; unsupported parts throw.
    const PartStyleValues* effective(StylePart part, StyleStateMask mask);
    PartStyleValues resolve_transient(StylePart part, StyleStateMask item_state, StyleStateMask owner_state,
        bool include_projection = true) const;
    // Same identity is a no-op. Failed assignment preserves all previous values.
    std::optional<Invalidation> assign_style(std::shared_ptr<const ControlStyle> style, StyleStateMask mask);
    // Replaces a part's locals transactionally. Empty values clear the part.
    std::optional<Invalidation> assign_local(StylePart part, PartStyleValues values, StyleStateMask mask);
    std::optional<Invalidation> assign_projection(StylePart part, PartStyleValues values, StyleStateMask mask);
    // No allocation; compares effective metrics to select layout or paint.
    Invalidation state_changed(StyleStateMask mask);
private:
    struct Slot { StylePart part; PartStyleValues local; PartStyleValues effective; PartStyleValues projected; };
    static constexpr std::size_t max_parts = 64;
    Slot* find(StylePart part);
    const Slot* find(StylePart part) const;
    Slot& ensure(StylePart part);
    void recompute(StyleStateMask mask);
    std::optional<Invalidation> assign_values(StylePart part, PartStyleValues values, StyleStateMask mask, bool projected);
    std::vector<StylePart> required_parts(const std::vector<StylePart>& authored) const;
    StyleTarget target_;
    std::shared_ptr<const ControlStyle> style_;
    std::vector<Slot> parts_;
    StyleStateMask mask_{~StyleStateMask(0)};
    bool context_enabled_{true};
};

}
