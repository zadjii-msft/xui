#pragma once

#include "xui/theme.hpp"
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

}
