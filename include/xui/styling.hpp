#pragma once

#include "xui/theme.hpp"
#include <array>
#include <variant>

namespace xui {

struct ThemeColor {
    uint32_t light{}, dark{};
    constexpr ThemeColor() = default;
    constexpr ThemeColor(uint32_t value) : light(value), dark(value) {}
    constexpr ThemeColor(uint32_t light_value, uint32_t dark_value) : light(light_value), dark(dark_value) {}
    constexpr uint32_t resolve(ThemeMode mode) const { return mode == ThemeMode::light ? light : dark; }
    bool operator==(const ThemeColor&) const = default;
};

struct ColorResource {
    std::string name;
    std::variant<ThemeColor, std::string> value;
};

class ResourceScope final {
public:
    ResourceScope(const ResourceScope&) = delete;
    ResourceScope& operator=(const ResourceScope&) = delete;
    static std::shared_ptr<const ResourceScope> create(std::vector<ColorResource> entries,
        std::shared_ptr<const ResourceScope> parent = {});
    ThemeColor color(std::string_view name) const;
private:
    ResourceScope() = default;
    unsigned depth_{1};
    std::shared_ptr<const ResourceScope> parent_;
    std::vector<std::pair<std::string, ThemeColor>> colors_;
};

struct ButtonStyleValues {
    std::optional<ThemeColor> background, foreground, border_brush;
    std::optional<Insets> border_thickness, padding;
    std::optional<float> corner_radius;
    bool empty() const;
};

enum class ButtonStyleState { focused, checked, hovered, pressed, disabled };
struct ButtonStyleRule { ButtonStyleState state; ButtonStyleValues values; };

// Copies and validates input, then precomputes the 32 state combinations.
class ButtonStyle final {
public:
    ButtonStyle(const ButtonStyle&) = delete;
    ButtonStyle& operator=(const ButtonStyle&) = delete;
    static std::shared_ptr<const ButtonStyle> create(ButtonStyleValues values = {},
        std::vector<ButtonStyleRule> rules = {}, std::shared_ptr<const ButtonStyle> based_on = {});
    const ButtonStyleValues& values(unsigned state_mask) const { return resolved_.at(state_mask); }
private:
    ButtonStyle() = default;
    unsigned depth_{1};
    ButtonStyleValues base_;
    std::array<ButtonStyleValues, 5> states_;
    std::array<ButtonStyleValues, 32> resolved_;
};

void validate_style_values(const ButtonStyleValues& values);
ButtonStyleValues merge_style_values(ButtonStyleValues base, const ButtonStyleValues& overlay);
bool style_layout_equal(const ButtonStyleValues& first, const ButtonStyleValues& second);

}
