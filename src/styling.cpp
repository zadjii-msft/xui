#include "xui/styling.hpp"
#include <stdexcept>

namespace xui {
namespace {
void validate_color(ThemeColor value) {
    if (value.light > 0xffffff || value.dark > 0xffffff)
        throw std::invalid_argument("Style colors must be RGB24 values");
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

}
