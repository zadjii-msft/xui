#include "xui/control_styling.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace xui {
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

struct PartSchema {
    StylePart part;
    std::uint32_t allowed;
    StyleStateMask states;
    std::optional<StylePart> foreground_from;
};
struct TargetSchema {
    std::span<const PartSchema> parts;
    std::span<const StyleStateMask> precedence;
};

constexpr std::uint32_t property_bit(StyleProperty value) { return static_cast<std::uint32_t>(value); }
constexpr std::uint32_t root_properties = property_bit(StyleProperty::background) | property_bit(StyleProperty::foreground) |
    property_bit(StyleProperty::border_brush) | property_bit(StyleProperty::border_thickness) |
    property_bit(StyleProperty::corner_radius) | property_bit(StyleProperty::padding);
constexpr std::uint32_t label_properties = property_bit(StyleProperty::foreground);
constexpr std::uint32_t indicator_properties = property_bit(StyleProperty::background) | property_bit(StyleProperty::border_brush) |
    property_bit(StyleProperty::border_thickness) | property_bit(StyleProperty::corner_radius) | property_bit(StyleProperty::size);
constexpr std::uint32_t mark_properties = property_bit(StyleProperty::foreground);

const TargetSchema& schema_for(StyleTarget target) {
    static constexpr StyleStateMask states[] {style_states::focused, style_states::checked,
        style_states::hovered, style_states::pressed, style_states::disabled};
    static constexpr auto all_states = style_states::focused | style_states::checked |
        style_states::hovered | style_states::pressed | style_states::disabled;
    // Inheritance sources precede their dependents.
    static constexpr PartSchema parts[] {
        {StylePart::root, root_properties, all_states, {}},
        {StylePart::label, label_properties, all_states, StylePart::root},
        {StylePart::indicator, indicator_properties, all_states, {}},
        {StylePart::mark, mark_properties, all_states, {}},
    };
    static const TargetSchema toggle_schema{parts, states};
    switch (target) {
        case StyleTarget::toggle: return toggle_schema;
    }
    throw std::invalid_argument("Unknown style target");
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
std::uint32_t properties_used(const PartStyleValues& values) {
    std::uint32_t used = 0;
    if (values.background) used |= property_bit(StyleProperty::background);
    if (values.foreground) used |= property_bit(StyleProperty::foreground);
    if (values.border_brush) used |= property_bit(StyleProperty::border_brush);
    if (values.border_thickness) used |= property_bit(StyleProperty::border_thickness);
    if (values.padding) used |= property_bit(StyleProperty::padding);
    if (values.corner_radius) used |= property_bit(StyleProperty::corner_radius);
    if (values.size) used |= property_bit(StyleProperty::size);
    return used;
}
}

bool PartStyleValues::empty() const {
    return !background && !foreground && !border_brush && !border_thickness && !padding && !corner_radius && !size;
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
}

PartStyleValues merge_part_values(PartStyleValues base, const PartStyleValues& overlay) {
    if (overlay.background) base.background = overlay.background;
    if (overlay.foreground) base.foreground = overlay.foreground;
    if (overlay.border_brush) base.border_brush = overlay.border_brush;
    if (overlay.border_thickness) base.border_thickness = overlay.border_thickness;
    if (overlay.padding) base.padding = overlay.padding;
    if (overlay.corner_radius) base.corner_radius = overlay.corner_radius;
    if (overlay.size) base.size = overlay.size;
    return base;
}
bool part_style_layout_equal(const PartStyleValues& first, const PartStyleValues& second) {
    return equal_insets(first.padding, second.padding) && equal_insets(first.border_thickness, second.border_thickness) &&
        first.size == second.size;
}

std::shared_ptr<const ControlStyle> ControlStyle::create(StyleTarget target,
    std::vector<std::pair<StylePart, PartStyleValues>> base_values,
    std::vector<StyleRule> rules, std::shared_ptr<const ControlStyle> based_on) {
    // Validate the target unconditionally and first: schema_for throws for
    // an unrecognized target even when base_values and rules are both empty.
    schema_for(target);
    if (based_on && based_on->target_ != target)
        throw std::invalid_argument("A style base must target the same control family");
    // Validate every remaining input before any storage is built or mutated.
    for (const auto& [part, values] : base_values) validate_part_values(target, part, values);
    for (const auto& rule : rules) {
        validate_part_values(target, rule.part, rule.values);
        const auto states = part_schema(schema_for(target), rule.part)->states;
        if (!rule.state || (rule.state & (rule.state - 1)) || (rule.state & ~states))
            throw std::invalid_argument("A style rule requires exactly one supported part state");
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
    if (parts.size() > 8) throw std::invalid_argument("A control style supports at most 8 parts");
    std::size_t total_buckets = 0;
    for (const auto& compiled : parts) total_buckets += compiled.buckets.size();
    if (total_buckets > 256) throw std::invalid_argument("A control style supports at most 256 compiled state rules");

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
    schema_for(target);
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
    if (parts_.size() >= max_parts) throw std::invalid_argument("A control style attachment supports at most 8 parts");
    parts_.push_back(Slot{part, {}, {}});
    return parts_.back();
}
bool ControlStyleAttachment::fully_empty() const {
    if (style_) return false;
    for (const auto& slot : parts_) if (!slot.local.empty()) return false;
    return true;
}
const PartStyleValues& ControlStyleAttachment::local(StylePart part) const {
    validate_part(target_, part);
    static const PartStyleValues empty_values;
    const auto* slot = find(part);
    return slot ? slot->local : empty_values;
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
        if (schema.foreground_from &&
            std::find(required.begin(), required.end(), *schema.foreground_from) != required.end())
            add(schema.part);
    return required;
}
void ControlStyleAttachment::recompute(StyleStateMask mask) {
    for (auto& slot : parts_) {
        const auto states = part_schema(schema_for(target_), slot.part)->states;
        auto resolved = style_ ? style_->resolve(slot.part, mask & states) : std::nullopt;
        slot.effective = merge_part_values(resolved.value_or(PartStyleValues{}), slot.local);
    }
    for (const auto& schema : schema_for(target_).parts) {
        if (!schema.foreground_from) continue;
        auto* destination = find(schema.part);
        const auto* source = find(*schema.foreground_from);
        if (destination && source && !destination->effective.foreground)
            destination->effective.foreground = source->effective.foreground;
    }
}
const PartStyleValues* ControlStyleAttachment::effective(StylePart part, StyleStateMask mask) {
    validate_part(target_, part);
    validate_mask(target_, mask);
    if (mask_ != mask) { recompute(mask); mask_ = mask; }
    const auto* slot = find(part);
    return slot ? &slot->effective : nullptr;
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
    if (final_count > max_parts) throw std::invalid_argument("A control style attachment supports at most 8 parts");
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
    validate_part_values(target_, part, values);
    validate_mask(target_, mask);
    if (empty() && values.empty()) return std::nullopt;
    // Same strong-exception-guarantee shape as assign_style: compute the
    // required parts (this part plus its schema-declared dependent, if any)
    // and validate the bound before mutating anything.
    const std::vector<StylePart> required = required_parts({part});
    std::size_t final_count = parts_.size();
    for (const auto candidate : required) if (!find(candidate)) ++final_count;
    if (final_count > max_parts) throw std::invalid_argument("A control style attachment supports at most 8 parts");
    parts_.reserve(final_count);

    std::array<PartStyleValues, max_parts> previous{};
    const auto tracked_before = parts_.size();
    for (std::size_t i = 0; i < tracked_before; ++i) previous[i] = parts_[i].effective;
    // No remaining operation below can throw.
    for (const auto candidate : required) ensure(candidate);
    find(part)->local = std::move(values);
    recompute(mask);
    mask_ = mask;
    for (std::size_t i = 0; i < tracked_before; ++i)
        if (!part_style_layout_equal(previous[i], parts_[i].effective)) return Invalidation::layout;
    for (std::size_t i = tracked_before; i < parts_.size(); ++i)
        if (!part_style_layout_equal(PartStyleValues{}, parts_[i].effective)) return Invalidation::layout;
    return Invalidation::paint;
}

}
