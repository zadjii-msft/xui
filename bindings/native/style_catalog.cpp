#include "control_style_catalog_checks.g.hpp"
#include <charconv>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
uint32_t count(const char* text) {
    uint32_t value{};
    const auto end = text + std::strlen(text);
    const auto parsed = std::from_chars(text, end, value);
    if (parsed.ec != std::errc{} || parsed.ptr != end || !value || value > 1024)
        throw std::invalid_argument("Catalog bounds require an integer from 1 through 1024");
    return value;
}
}
int main(int argc, char** argv) {
    try {
        if (argc != 3) throw std::invalid_argument("Expected target count and part count");
        const auto targets = count(argv[1]), parts = count(argv[2]);
        std::cout << std::setprecision(std::numeric_limits<float>::max_digits10) << "{\"schemas\":[";
        bool first = true;
        for (uint32_t target = 0; target < targets; ++target) {
            for (uint32_t part = 0; part < parts; ++part) {
                uint64_t properties{}, states{}, state_properties{};
                const auto status = xui_control_style_get_schema(target, part, &properties, &states, &state_properties);
                if (status == XUI_INVALID_ARGUMENT) continue;
                if (status != XUI_OK) throw std::runtime_error("Native schema lookup failed");
                float font_size{};
                uint32_t font_length{}, font_styles{}, horizontal{}, vertical{};
                if (xui_control_style_get_limits(target, part, &font_size, &font_length, &font_styles, &horizontal, &vertical) != XUI_OK)
                    throw std::runtime_error("Native style limits lookup failed");
                if (!first) std::cout << ',';
                first = false;
                std::cout << "{\"target\":" << target << ",\"part\":" << part
                    << ",\"properties\":" << properties << ",\"states\":" << states
                    << ",\"state_properties\":" << state_properties << ",\"maximum_font_size\":" << font_size
                    << ",\"maximum_font_family_utf16\":" << font_length << ",\"font_styles\":" << font_styles
                    << ",\"horizontal_alignments\":" << horizontal << ",\"vertical_alignments\":" << vertical << '}';
            }
        }
        std::cout << "]}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
