#include "xui/control_styling.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace xui {
const StyleTargetSchema& control_style_schema(StyleTarget target) {
    using Resolver = const StyleTargetSchema* (*)(StyleTarget);
    constexpr Resolver resolvers[]{basic_text_style_schema, native_fields_style_schema, choices_status_style_schema,
        layouts_style_schema, collections_style_schema, data_grid_chart_style_schema,
        navigation_composites_style_schema, hosts_scenes_style_schema};
    for (const auto resolver : resolvers) if (const auto* schema = resolver(target)) return *schema;
    throw std::invalid_argument("Unknown or unimplemented style target");
}

std::shared_ptr<const StyleFontFamily> make_style_font_family(std::string_view utf8) {
    if (utf8.empty() || utf8.size() > 1024) throw std::invalid_argument("Font family requires 1 through 1024 UTF-8 bytes");
    std::wstring name;
    for (std::size_t i = 0; i < utf8.size();) {
        const auto lead = static_cast<unsigned char>(utf8[i++]);
        uint32_t scalar = lead;
        unsigned tail = 0;
        if (lead >= 0xc2 && lead <= 0xdf) { scalar = lead & 31; tail = 1; }
        else if (lead >= 0xe0 && lead <= 0xef) { scalar = lead & 15; tail = 2; }
        else if (lead >= 0xf0 && lead <= 0xf4) { scalar = lead & 7; tail = 3; }
        else if (!lead || lead >= 0x80) throw std::invalid_argument("Font family contains invalid UTF-8");
        if (i + tail > utf8.size()) throw std::invalid_argument("Font family contains truncated UTF-8");
        for (unsigned j = 0; j < tail; ++j) {
            const auto byte = static_cast<unsigned char>(utf8[i++]);
            if ((byte & 0xc0) != 0x80) throw std::invalid_argument("Font family contains invalid UTF-8");
            scalar = (scalar << 6) | (byte & 63);
        }
        if ((tail == 1 && scalar < 0x80) || (tail == 2 && scalar < 0x800) || (tail == 3 && scalar < 0x10000) ||
            scalar > 0x10ffff || (scalar >= 0xd800 && scalar <= 0xdfff))
            throw std::invalid_argument("Font family contains invalid Unicode");
        if constexpr (sizeof(wchar_t) == 2) {
            if (scalar > 0xffff) {
                scalar -= 0x10000;
                name.push_back(static_cast<wchar_t>(0xd800 + (scalar >> 10)));
                name.push_back(static_cast<wchar_t>(0xdc00 + (scalar & 1023)));
            } else name.push_back(static_cast<wchar_t>(scalar));
        } else name.push_back(static_cast<wchar_t>(scalar));
    }
    return std::shared_ptr<const StyleFontFamily>(new StyleFontFamily(std::string(utf8), std::move(name)));
}
namespace {
void validate_color(ThemeColor value) {
    if (value.light > 0xffffff || value.dark > 0xffffff)
        throw std::invalid_argument("Style colors must be RGB24 values");
}
void validate_dimension(float value) {
    if (!std::isfinite(value) || value < 0 || value > 32768)
        throw std::invalid_argument("Style dimensions must be finite and between 0 and 32768");
}
void validate_insets(Insets value) {
    validate_dimension(value.left); validate_dimension(value.top);
    validate_dimension(value.right); validate_dimension(value.bottom);
}
bool equal_insets(const std::optional<Insets>& a, const std::optional<Insets>& b) {
    if (a.has_value() != b.has_value()) return false;
    return !a || (a->left == b->left && a->top == b->top && a->right == b->right && a->bottom == b->bottom);
}

using PartSchema = StylePartSchema;
using TargetSchema = StyleTargetSchema;
constexpr StylePropertyMask property_bit(StyleProperty value) { return style_property(value); }
const TargetSchema& schema_for(StyleTarget target) {
    return control_style_schema(target);
}
const PartSchema* part_schema(const TargetSchema& schema, StylePart part) {
    for (const auto& entry : schema.parts) if (entry.part == part) return &entry;
    return nullptr;
}
void validate_mask(StyleTarget target, StyleStateMask mask) {
    StyleStateMask supported{};
    for (const auto& part : schema_for(target).parts) supported |= part.states;
    if (mask & ~supported) throw std::invalid_argument("Unsupported state bits for this style target");
}
void validate_schema(const TargetSchema& schema) {
    if (schema.parts.empty() || schema.parts.size() > 64 || schema.precedence.size() > 64)
        throw std::invalid_argument("Invalid style schema bounds");
    StyleStateMask ordered{};
    for (const auto state : schema.precedence) {
        if (!state || (state & (state - 1)) || (ordered & state)) throw std::invalid_argument("Invalid schema state precedence");
        ordered |= state;
    }
    for (std::size_t i = 0; i < schema.parts.size(); ++i) {
        const auto& part = schema.parts[i];
        if (part.states & ~ordered) throw std::invalid_argument("Style schema state has no precedence");
        if (part.allowed & ~StylePropertyMask((1ull << 24) - 1)) throw std::invalid_argument("Unknown property in style schema");
        if (!std::isfinite(part.limits.maximum_font_size) || part.limits.maximum_font_size <= 0 ||
            part.limits.maximum_font_size > 32768 || !part.limits.maximum_font_family_utf16 ||
            part.limits.maximum_font_family_utf16 > 1024 || !part.limits.font_styles || (part.limits.font_styles & ~7u) ||
            !part.limits.horizontal_alignments || (part.limits.horizontal_alignments & ~15u) ||
            !part.limits.vertical_alignments || (part.limits.vertical_alignments & ~15u))
            throw std::invalid_argument("Invalid style value limits");
        for (std::size_t j = 0; j < i; ++j)
            if (schema.parts[j].part == part.part) throw std::invalid_argument("Duplicate style schema part");
        for (const auto source : {part.foreground_from, part.typography_from}) {
            if (!source) continue;
            bool found = false;
            for (std::size_t j = 0; j < i; ++j) found |= schema.parts[j].part == *source;
            if (!found) throw std::invalid_argument("Style inheritance source must precede its destination");
        }
        if (part.typography_from) {
            const auto* source = part_schema(schema, *part.typography_from);
            const auto inherited = source->allowed & part.allowed;
            if (((inherited & style_property(StyleProperty::font_family)) &&
                    source->limits.maximum_font_family_utf16 > part.limits.maximum_font_family_utf16) ||
                ((inherited & style_property(StyleProperty::font_size)) &&
                    source->limits.maximum_font_size > part.limits.maximum_font_size) ||
                ((inherited & style_property(StyleProperty::font_style)) &&
                    (source->limits.font_styles & ~part.limits.font_styles)) ||
                ((inherited & style_property(StyleProperty::horizontal_alignment)) &&
                    (source->limits.horizontal_alignments & ~part.limits.horizontal_alignments)) ||
                ((inherited & style_property(StyleProperty::vertical_alignment)) &&
                    (source->limits.vertical_alignments & ~part.limits.vertical_alignments)))
                throw std::invalid_argument("Inherited text values must fit the destination's limits");
        }
    }
}
StylePropertyMask properties_used(const PartStyleValues& values) {
    StylePropertyMask used = 0;
    if (values.background) used |= property_bit(StyleProperty::background);
    if (values.foreground) used |= property_bit(StyleProperty::foreground);
    if (values.border_brush) used |= property_bit(StyleProperty::border_brush);
    if (values.border_thickness) used |= property_bit(StyleProperty::border_thickness);
    if (values.padding) used |= property_bit(StyleProperty::padding);
    if (values.corner_radius) used |= property_bit(StyleProperty::corner_radius);
    if (values.size) used |= property_bit(StyleProperty::size);
    if (values.font_family) used |= property_bit(StyleProperty::font_family);
    if (values.font_size) used |= property_bit(StyleProperty::font_size);
    if (values.font_weight) used |= property_bit(StyleProperty::font_weight);
    if (values.font_style) used |= property_bit(StyleProperty::font_style);
    if (values.horizontal_alignment) used |= property_bit(StyleProperty::horizontal_alignment);
    if (values.vertical_alignment) used |= property_bit(StyleProperty::vertical_alignment);
    if (values.spacing) used |= property_bit(StyleProperty::spacing);
    if (values.row_height) used |= property_bit(StyleProperty::row_height);
    if (values.header_height) used |= property_bit(StyleProperty::header_height);
    if (values.indentation) used |= property_bit(StyleProperty::indentation);
    if (values.thickness) used |= property_bit(StyleProperty::thickness);
    if (values.width) used |= property_bit(StyleProperty::width);
    if (values.height) used |= property_bit(StyleProperty::height);
    if (values.row_gap) used |= property_bit(StyleProperty::row_gap);
    if (values.column_gap) used |= property_bit(StyleProperty::column_gap);
    if (values.maximum_lines) used |= property_bit(StyleProperty::maximum_lines);
    if (values.wrapping) used |= property_bit(StyleProperty::wrapping);
    return used;
}
}

bool PartStyleValues::empty() const {
    return properties_used(*this) == 0;
}

void validate_part(StyleTarget target, StylePart part) {
    if (!part_schema(schema_for(target), part))
        throw std::invalid_argument("Style part is not supported by this control family");
}
void validate_part_values(StyleTarget target, StylePart part, const PartStyleValues& values) {
    validate_part(target, part);
    const auto* schema = part_schema(schema_for(target), part);
    if (properties_used(values) & ~schema->allowed)
        throw std::invalid_argument("Style property is not supported on this part");
    if (values.background) validate_color(*values.background);
    if (values.foreground) validate_color(*values.foreground);
    if (values.border_brush) validate_color(*values.border_brush);
    if (values.border_thickness) validate_insets(*values.border_thickness);
    if (values.padding) validate_insets(*values.padding);
    if (values.corner_radius) validate_dimension(*values.corner_radius);
    if (values.size) validate_dimension(*values.size);
    if (values.font_family && (values.font_family->utf8.empty() || values.font_family->utf8.size() > 1024 ||
        values.font_family->name.empty())) throw std::invalid_argument("Invalid font family");
    if (values.font_family) {
        std::size_t length{};
        for (const auto value : values.font_family->name)
            length += sizeof(wchar_t) > 2 && static_cast<uint32_t>(value) > 0xffff ? 2 : 1;
        if (length > schema->limits.maximum_font_family_utf16)
            throw std::invalid_argument("Font family exceeds the part's UTF-16 limit");
    }
    if (values.font_size) {
        validate_dimension(*values.font_size);
        if (*values.font_size == 0) throw std::invalid_argument("Font size must be positive");
        if (*values.font_size > schema->limits.maximum_font_size)
            throw std::invalid_argument("Font size exceeds the part's limit");
    }
    if (values.font_weight && (*values.font_weight < 1 || *values.font_weight > 999))
        throw std::invalid_argument("Font weight must be between 1 and 999");
    if (values.font_style && *values.font_style > StyleFontStyle::oblique) throw std::invalid_argument("Unknown font style");
    if (values.font_style && !(schema->limits.font_styles & (1u << static_cast<uint32_t>(*values.font_style))))
        throw std::invalid_argument("Font style is not supported on this part");
    if ((values.horizontal_alignment && *values.horizontal_alignment > StyleAlignment::stretch) ||
        (values.vertical_alignment && *values.vertical_alignment > StyleAlignment::stretch))
        throw std::invalid_argument("Unknown style alignment");
    if ((values.horizontal_alignment &&
            !(schema->limits.horizontal_alignments & (1u << static_cast<uint32_t>(*values.horizontal_alignment)))) ||
        (values.vertical_alignment &&
            !(schema->limits.vertical_alignments & (1u << static_cast<uint32_t>(*values.vertical_alignment)))))
        throw std::invalid_argument("Alignment is not supported on this part");
    for (const auto dimension : {values.spacing, values.row_height, values.header_height, values.indentation,
        values.thickness, values.width, values.height, values.row_gap, values.column_gap})
        if (dimension) validate_dimension(*dimension);
    if (values.row_height && *values.row_height == 0) throw std::invalid_argument("Row height must be positive");
    if (values.maximum_lines && *values.maximum_lines > 32768) throw std::invalid_argument("Invalid maximum line count");
}

PartStyleValues merge_part_values(PartStyleValues base, const PartStyleValues& overlay) {
    if (overlay.background) base.background = overlay.background;
    if (overlay.foreground) base.foreground = overlay.foreground;
    if (overlay.border_brush) base.border_brush = overlay.border_brush;
    if (overlay.border_thickness) base.border_thickness = overlay.border_thickness;
    if (overlay.padding) base.padding = overlay.padding;
    if (overlay.corner_radius) base.corner_radius = overlay.corner_radius;
    if (overlay.size) base.size = overlay.size;
    if (overlay.font_family) base.font_family = overlay.font_family;
    if (overlay.font_size) base.font_size = overlay.font_size;
    if (overlay.font_weight) base.font_weight = overlay.font_weight;
    if (overlay.font_style) base.font_style = overlay.font_style;
    if (overlay.horizontal_alignment) base.horizontal_alignment = overlay.horizontal_alignment;
    if (overlay.vertical_alignment) base.vertical_alignment = overlay.vertical_alignment;
    if (overlay.spacing) base.spacing = overlay.spacing;
    if (overlay.row_height) base.row_height = overlay.row_height;
    if (overlay.header_height) base.header_height = overlay.header_height;
    if (overlay.indentation) base.indentation = overlay.indentation;
    if (overlay.thickness) base.thickness = overlay.thickness;
    if (overlay.width) base.width = overlay.width;
    if (overlay.height) base.height = overlay.height;
    if (overlay.row_gap) base.row_gap = overlay.row_gap;
    if (overlay.column_gap) base.column_gap = overlay.column_gap;
    if (overlay.maximum_lines) base.maximum_lines = overlay.maximum_lines;
    if (overlay.wrapping) base.wrapping = overlay.wrapping;
    return base;
}
bool part_style_layout_equal(const PartStyleValues& first, const PartStyleValues& second) {
    return equal_insets(first.padding, second.padding) && equal_insets(first.border_thickness, second.border_thickness) &&
        first.size == second.size && first.font_family == second.font_family && first.font_size == second.font_size &&
        first.font_weight == second.font_weight && first.font_style == second.font_style &&
        first.horizontal_alignment == second.horizontal_alignment && first.vertical_alignment == second.vertical_alignment &&
        first.spacing == second.spacing && first.row_height == second.row_height && first.header_height == second.header_height &&
        first.indentation == second.indentation && first.thickness == second.thickness && first.width == second.width &&
        first.height == second.height && first.row_gap == second.row_gap && first.column_gap == second.column_gap &&
        first.maximum_lines == second.maximum_lines && first.wrapping == second.wrapping;
}
bool part_style_values_equal(const PartStyleValues& first, const PartStyleValues& second) {
    return first.background == second.background && first.foreground == second.foreground &&
        first.border_brush == second.border_brush && first.corner_radius == second.corner_radius &&
        part_style_layout_equal(first, second);
}
Insets style_content_insets(const PartStyleValues* values, Insets default_padding, Insets default_border) {
    const auto padding = values ? values->padding.value_or(default_padding) : default_padding;
    const auto border = values ? values->border_thickness.value_or(default_border) : default_border;
    return {padding.left + border.left, padding.top + border.top,
        padding.right + border.right, padding.bottom + border.bottom};
}
Rect style_content_bounds(Rect bounds, const PartStyleValues* values, Insets default_padding, Insets default_border) {
    const auto edges = style_content_insets(values, default_padding, default_border);
    return {bounds.x + edges.left, bounds.y + edges.top,
        std::max(0.0f, bounds.width - edges.left - edges.right),
        std::max(0.0f, bounds.height - edges.top - edges.bottom)};
}

std::shared_ptr<const ControlStyle> ControlStyle::create(StyleTarget target,
    std::vector<std::pair<StylePart, PartStyleValues>> base_values,
    std::vector<StyleRule> rules, std::shared_ptr<const ControlStyle> based_on) {
    // Validate the target unconditionally and first: schema_for throws for
    // an unrecognized target even when base_values and rules are both empty.
    validate_schema(schema_for(target));
    if (based_on && based_on->target_ != target)
        throw std::invalid_argument("A style base must target the same control family");
    // Validate every remaining input before any storage is built or mutated.
    for (const auto& [part, values] : base_values) validate_part_values(target, part, values);
    for (const auto& rule : rules) {
        validate_part_values(target, rule.part, rule.values);
        const auto* metadata = part_schema(schema_for(target), rule.part);
        const auto states = metadata->states;
        if (!rule.state || (rule.state & (rule.state - 1)) || (rule.state & ~states))
            throw std::invalid_argument("A style rule requires exactly one supported part state");
        if (properties_used(rule.values) & ~metadata->state_allowed)
            throw std::invalid_argument("Style property is not supported in state rules for this part");
    }
    if (rules.size() > 256) throw std::invalid_argument("A control style supports at most 256 rules");
    if (base_values.size() > 64) throw std::invalid_argument("A control style supports at most 64 base entries");
    const unsigned depth = based_on ? based_on->depth_ + 1 : 1;
    if (depth > 16) throw std::invalid_argument("Control style inheritance depth exceeds 16");

    // Build into local storage; nothing is committed to the returned
    // definition (nor visible to `based_on`) unless every check above passed.
    std::vector<CompiledPart> parts = based_on ? based_on->parts_ : std::vector<CompiledPart>{};
    const auto ensure_part = [&](StylePart part) -> CompiledPart& {
        for (auto& compiled : parts) if (compiled.part == part) return compiled;
        parts.push_back(CompiledPart{part, {}, {}});
        return parts.back();
    };
    for (const auto& [part, values] : base_values) {
        auto& compiled = ensure_part(part);
        compiled.base = merge_part_values(compiled.base, values);
    }
    for (const auto& rule : rules) {
        auto& compiled = ensure_part(rule.part);
        Bucket* bucket = nullptr;
        for (auto& existing : compiled.buckets) if (existing.state == rule.state) { bucket = &existing; break; }
        if (!bucket) { compiled.buckets.push_back(Bucket{rule.state, {}}); bucket = &compiled.buckets.back(); }
        bucket->values = merge_part_values(bucket->values, rule.values);
    }
    // Bound total compiled storage after flattening inheritance, not only
    // this call's own submitted layer.
    if (parts.size() > 64) throw std::invalid_argument("A control style supports at most 64 parts");
    std::size_t total_buckets = 0;
    for (const auto& compiled : parts) total_buckets += compiled.buckets.size();
    if (total_buckets > 1024) throw std::invalid_argument("A control style supports at most 1024 compiled state rules");

    auto result = std::shared_ptr<ControlStyle>(new ControlStyle);
    result->target_ = target;
    result->depth_ = depth;
    result->parts_ = std::move(parts);
    return result;
}

std::optional<PartStyleValues> ControlStyle::resolve(StylePart part, StyleStateMask mask) const {
    validate_part(target_, part);
    if (mask & ~part_schema(schema_for(target_), part)->states)
        throw std::invalid_argument("Unsupported state bits for this style part");
    for (const auto& compiled : parts_) {
        if (compiled.part != part) continue;
        PartStyleValues result = compiled.base;
        for (const auto bit : schema_for(target_).precedence) {
            if (!(mask & bit)) continue;
            for (const auto& bucket : compiled.buckets)
                if (bucket.state == bit) { result = merge_part_values(result, bucket.values); break; }
        }
        return result;
    }
    return std::nullopt;
}
bool ControlStyle::has_part(StylePart part) const {
    validate_part(target_, part);
    for (const auto& compiled : parts_) if (compiled.part == part) return true;
    return false;
}
std::vector<StylePart> ControlStyle::authored_parts() const {
    std::vector<StylePart> result;
    result.reserve(parts_.size());
    for (const auto& compiled : parts_) result.push_back(compiled.part);
    return result;
}

ControlStyleAttachment::ControlStyleAttachment(StyleTarget target) : target_(target) {
    validate_schema(schema_for(target));
}
ControlStyleAttachment::Slot* ControlStyleAttachment::find(StylePart part) {
    for (auto& slot : parts_) if (slot.part == part) return &slot;
    return nullptr;
}
const ControlStyleAttachment::Slot* ControlStyleAttachment::find(StylePart part) const {
    for (const auto& slot : parts_) if (slot.part == part) return &slot;
    return nullptr;
}
ControlStyleAttachment::Slot& ControlStyleAttachment::ensure(StylePart part) {
    if (auto* existing = find(part)) return *existing;
    if (parts_.size() >= max_parts) throw std::invalid_argument("A control style attachment supports at most 64 parts");
    parts_.push_back(Slot{part, {}, {}});
    return parts_.back();
}
bool ControlStyleAttachment::fully_empty() const {
    if (style_) return false;
    for (const auto& slot : parts_) if (!slot.local.empty() || !slot.projected.empty()) return false;
    return true;
}
const PartStyleValues& ControlStyleAttachment::local(StylePart part) const {
    validate_part(target_, part);
    static const PartStyleValues empty_values;
    const auto* slot = find(part);
    return slot ? slot->local : empty_values;
}
const PartStyleValues& ControlStyleAttachment::projection(StylePart part) const {
    validate_part(target_, part);
    static const PartStyleValues empty_values;
    const auto* slot = find(part);
    return slot ? slot->projected : empty_values;
}
std::vector<StylePart> ControlStyleAttachment::required_parts(const std::vector<StylePart>& authored) const {
    std::vector<StylePart> required;
    const auto add = [&](StylePart part) {
        if (std::find(required.begin(), required.end(), part) == required.end()) required.push_back(part);
    };
    for (const auto part : authored) {
        add(part);
    }
    for (const auto& schema : schema_for(target_).parts)
        for (const auto source : {schema.foreground_from, schema.typography_from})
            if (source && std::find(required.begin(), required.end(), *source) != required.end())
                add(schema.part);
    return required;
}
namespace {
constexpr auto inherited_text_properties = style_properties::typography | style_properties::alignment |
    style_property(StyleProperty::maximum_lines) | style_property(StyleProperty::wrapping);
void inherit_typography(PartStyleValues& destination, const PartStyleValues& source, StylePropertyMask allowed) {
    if ((allowed & style_property(StyleProperty::font_family)) && !destination.font_family)
        destination.font_family = source.font_family;
    if ((allowed & style_property(StyleProperty::font_size)) && !destination.font_size)
        destination.font_size = source.font_size;
    if ((allowed & style_property(StyleProperty::font_weight)) && !destination.font_weight)
        destination.font_weight = source.font_weight;
    if ((allowed & style_property(StyleProperty::font_style)) && !destination.font_style)
        destination.font_style = source.font_style;
    if ((allowed & style_property(StyleProperty::horizontal_alignment)) && !destination.horizontal_alignment)
        destination.horizontal_alignment = source.horizontal_alignment;
    if ((allowed & style_property(StyleProperty::vertical_alignment)) && !destination.vertical_alignment)
        destination.vertical_alignment = source.vertical_alignment;
    if ((allowed & style_property(StyleProperty::maximum_lines)) && !destination.maximum_lines)
        destination.maximum_lines = source.maximum_lines;
    if ((allowed & style_property(StyleProperty::wrapping)) && !destination.wrapping)
        destination.wrapping = source.wrapping;
}
}
void ControlStyleAttachment::recompute(StyleStateMask mask) {
    for (auto& slot : parts_) {
        const auto states = part_schema(schema_for(target_), slot.part)->states;
        auto resolved = style_ ? style_->resolve(slot.part, mask & states) : std::nullopt;
        slot.effective = merge_part_values(merge_part_values(slot.projected, resolved.value_or(PartStyleValues{})), slot.local);
    }
    for (const auto& schema : schema_for(target_).parts) {
        auto* destination = find(schema.part);
        if (!destination) continue;
        if (schema.foreground_from) {
            const auto* source = find(*schema.foreground_from);
            if (source && !destination->effective.foreground)
                destination->effective.foreground = source->effective.foreground;
        }
        if (schema.typography_from) {
            const auto* source = find(*schema.typography_from);
            if (source) inherit_typography(destination->effective, source->effective, schema.allowed);
        }
    }
}
const PartStyleValues* ControlStyleAttachment::effective(StylePart part, StyleStateMask mask) {
    validate_part(target_, part);
    validate_mask(target_, mask);
    if (mask_ != mask) { recompute(mask); mask_ = mask; }
    const auto* slot = find(part);
    return slot ? &slot->effective : nullptr;
}
PartStyleValues ControlStyleAttachment::resolve_transient(StylePart part, StyleStateMask item_state, StyleStateMask owner_state,
    bool include_projection) const {
    validate_part(target_, part);
    validate_mask(target_, item_state);
    validate_mask(target_, owner_state);
    item_state |= owner_state & style_states::disabled;
    const auto& schema = schema_for(target_);
    const auto resolve_own = [&](StylePart current) -> PartStyleValues {
        const auto* metadata = part_schema(schema, current);
        const auto state = (current == StylePart::root ? owner_state : item_state) & metadata->states;
        auto result = style_ ? style_->resolve(current, state).value_or(PartStyleValues{}) : PartStyleValues{};
        if (const auto* slot = find(current)) {
            if (include_projection) result = merge_part_values(slot->projected, result);
            result = merge_part_values(std::move(result), slot->local);
        }
        return result;
    };
    auto result = resolve_own(part);
    auto* metadata = part_schema(schema, part);
    auto* source = metadata;
    while (!result.foreground && source->foreground_from) {
        source = part_schema(schema, *source->foreground_from);
        result.foreground = resolve_own(source->part).foreground;
    }
    auto missing = metadata->allowed & inherited_text_properties & ~properties_used(result);
    source = metadata;
    while (missing && source->typography_from) {
        source = part_schema(schema, *source->typography_from);
        missing &= source->allowed;
        inherit_typography(result, resolve_own(source->part), missing);
        missing &= ~properties_used(result);
    }
    return result;
}
std::optional<Invalidation> ControlStyleAttachment::assign_style(std::shared_ptr<const ControlStyle> style, StyleStateMask mask) {
    validate_mask(target_, mask);
    if (style && style->target() != target_)
        throw std::invalid_argument("Style targets a different control family");
    if (style_ == style) return std::nullopt;
    // Compute every part the new style requires (its authored parts plus
    // any schema-declared dependent, e.g. label<-root) and validate the
    // bound before mutating anything: a rejection here leaves the previous
    // style, locals, and effective cache completely untouched.
    const std::vector<StylePart> required = style ? required_parts(style->authored_parts()) : std::vector<StylePart>{};
    std::size_t final_count = parts_.size();
    for (const auto part : required) if (!find(part)) ++final_count;
    if (final_count > max_parts) throw std::invalid_argument("A control style attachment supports at most 64 parts");
    parts_.reserve(final_count);

    std::array<PartStyleValues, max_parts> previous{};
    const auto tracked_before = parts_.size();
    for (std::size_t i = 0; i < tracked_before; ++i) previous[i] = parts_[i].effective;
    // No remaining operation below can throw: the bound was checked and
    // capacity reserved above.
    for (const auto part : required) ensure(part);
    style_ = std::move(style);
    recompute(mask);
    mask_ = mask;
    for (std::size_t i = 0; i < tracked_before; ++i)
        if (!part_style_layout_equal(previous[i], parts_[i].effective)) return Invalidation::layout;
    for (std::size_t i = tracked_before; i < parts_.size(); ++i)
        if (!part_style_layout_equal(PartStyleValues{}, parts_[i].effective)) return Invalidation::layout;
    return Invalidation::paint;
}
Invalidation ControlStyleAttachment::state_changed(StyleStateMask mask) {
    validate_mask(target_, mask);
    if (mask_ == mask) return Invalidation::paint;
    std::array<PartStyleValues, max_parts> previous{};
    for (std::size_t i = 0; i < parts_.size(); ++i) previous[i] = parts_[i].effective;
    recompute(mask);
    mask_ = mask;
    for (std::size_t i = 0; i < parts_.size(); ++i)
        if (!part_style_layout_equal(previous[i], parts_[i].effective)) return Invalidation::layout;
    return Invalidation::paint;
}
std::optional<Invalidation> ControlStyleAttachment::assign_local(StylePart part, PartStyleValues values, StyleStateMask mask) {
    return assign_values(part, std::move(values), mask, false);
}
std::optional<Invalidation> ControlStyleAttachment::assign_projection(StylePart part, PartStyleValues values, StyleStateMask mask) {
    return assign_values(part, std::move(values), mask, true);
}
std::optional<Invalidation> ControlStyleAttachment::assign_values(StylePart part, PartStyleValues values, StyleStateMask mask, bool projected) {
    validate_part_values(target_, part, values);
    validate_mask(target_, mask);
    const auto* slot = find(part);
    if (!slot && values.empty()) return std::nullopt;
    if (slot && part_style_values_equal(projected ? slot->projected : slot->local, values)) {
        if (mask_ == mask) return std::nullopt;
        return state_changed(mask);
    }
    // Same strong-exception-guarantee shape as assign_style: compute the
    // required parts (this part plus its schema-declared dependent, if any)
    // and validate the bound before mutating anything.
    const std::vector<StylePart> required = slot ? std::vector<StylePart>{} : required_parts({part});
    std::size_t final_count = parts_.size();
    for (const auto candidate : required) if (!find(candidate)) ++final_count;
    if (final_count > max_parts) throw std::invalid_argument("A control style attachment supports at most 64 parts");
    parts_.reserve(final_count);

    std::array<PartStyleValues, max_parts> previous{};
    const auto tracked_before = parts_.size();
    for (std::size_t i = 0; i < tracked_before; ++i) previous[i] = parts_[i].effective;
    // No remaining operation below can throw.
    for (const auto candidate : required) ensure(candidate);
    if (projected) find(part)->projected = std::move(values);
    else find(part)->local = std::move(values);
    recompute(mask);
    mask_ = mask;
    for (std::size_t i = 0; i < tracked_before; ++i)
        if (!part_style_layout_equal(previous[i], parts_[i].effective)) return Invalidation::layout;
    for (std::size_t i = tracked_before; i < parts_.size(); ++i)
        if (!part_style_layout_equal(PartStyleValues{}, parts_[i].effective)) return Invalidation::layout;
    return Invalidation::paint;
}

}
