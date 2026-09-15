#include "xui/styling.hpp"
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
}

std::shared_ptr<const ResourceScope> ResourceScope::create(std::vector<ColorResource> entries,
    std::shared_ptr<const ResourceScope> parent) {
    if (entries.size() > 256) throw std::invalid_argument("A resource scope supports at most 256 entries");
    auto result = std::shared_ptr<ResourceScope>(new ResourceScope);
    result->depth_ = parent ? parent->depth_ + 1 : 1;
    if (result->depth_ > 16) throw std::invalid_argument("Resource scope depth exceeds 16");
    result->parent_ = std::move(parent);
    result->colors_.resize(entries.size());
    for (size_t i = 0; i < entries.size(); ++i) {
        if (entries[i].name.empty() || entries[i].name.size() > 256)
            throw std::invalid_argument("Resource names require 1 to 256 bytes");
        for (size_t j = 0; j < i; ++j)
            if (entries[j].name == entries[i].name) throw std::invalid_argument("Duplicate resource name: " + entries[i].name);
    }
    std::vector<unsigned char> marks(entries.size());
    const auto resolve = [&](auto&& self, size_t index) -> ThemeColor {
        if (marks[index] == 2) return result->colors_[index].second;
        if (marks[index] == 1) throw std::invalid_argument("Cyclic resource reference: " + entries[index].name);
        marks[index] = 1;
        ThemeColor color;
        if (auto direct = std::get_if<ThemeColor>(&entries[index].value)) color = *direct;
        else {
            const auto& alias = std::get<std::string>(entries[index].value);
            auto found = std::find_if(entries.begin(), entries.end(), [&](const auto& entry) { return entry.name == alias; });
            if (found != entries.end()) color = self(self, size_t(found - entries.begin()));
            else if (result->parent_) color = result->parent_->color(alias);
            else throw std::invalid_argument("Missing color resource: " + alias);
        }
        validate_color(color);
        result->colors_[index] = {entries[index].name, color};
        marks[index] = 2;
        return color;
    };
    for (size_t i = 0; i < entries.size(); ++i) resolve(resolve, i);
    return result;
}

ThemeColor ResourceScope::color(std::string_view name) const {
    for (const auto& [key, value] : colors_) if (key == name) return value;
    if (parent_) return parent_->color(name);
    throw std::invalid_argument("Missing color resource: " + std::string(name));
}

bool ButtonStyleValues::empty() const {
    return !background && !foreground && !border_brush && !border_thickness && !padding && !corner_radius;
}
void validate_style_values(const ButtonStyleValues& values) {
    if (values.background) validate_color(*values.background);
    if (values.foreground) validate_color(*values.foreground);
    if (values.border_brush) validate_color(*values.border_brush);
    if (values.border_thickness) validate_insets(*values.border_thickness);
    if (values.padding) validate_insets(*values.padding);
    if (values.corner_radius) validate_dimension(*values.corner_radius);
}
ButtonStyleValues merge_style_values(ButtonStyleValues base, const ButtonStyleValues& overlay) {
    if (overlay.background) base.background = overlay.background;
    if (overlay.foreground) base.foreground = overlay.foreground;
    if (overlay.border_brush) base.border_brush = overlay.border_brush;
    if (overlay.border_thickness) base.border_thickness = overlay.border_thickness;
    if (overlay.padding) base.padding = overlay.padding;
    if (overlay.corner_radius) base.corner_radius = overlay.corner_radius;
    return base;
}
bool style_layout_equal(const ButtonStyleValues& first, const ButtonStyleValues& second) {
    return equal_insets(first.padding, second.padding) && equal_insets(first.border_thickness, second.border_thickness);
}
std::shared_ptr<const ButtonStyle> ButtonStyle::create(ButtonStyleValues values,
    std::vector<ButtonStyleRule> rules, std::shared_ptr<const ButtonStyle> based_on) {
    validate_style_values(values);
    if (rules.size() > 256) throw std::invalid_argument("A button style supports at most 256 rules");
    auto result = std::shared_ptr<ButtonStyle>(new ButtonStyle);
    if (based_on) {
        result->depth_ = based_on->depth_ + 1;
        if (result->depth_ > 16) throw std::invalid_argument("Button style inheritance depth exceeds 16");
        result->base_ = based_on->base_;
        result->states_ = based_on->states_;
    }
    result->base_ = merge_style_values(result->base_, values);
    for (const auto& rule : rules) {
        if (rule.state < ButtonStyleState::focused || rule.state > ButtonStyleState::disabled)
            throw std::invalid_argument("Invalid button style state");
        validate_style_values(rule.values);
        auto& state = result->states_[static_cast<size_t>(rule.state)];
        state = merge_style_values(state, rule.values);
    }
    for (unsigned mask = 0; mask < 32; ++mask) {
        auto& resolved = result->resolved_[mask];
        resolved = result->base_;
        for (unsigned state = 0; state < 5; ++state)
            if (mask & (1u << state)) resolved = merge_style_values(resolved, result->states_[state]);
    }
    return result;
}
}
