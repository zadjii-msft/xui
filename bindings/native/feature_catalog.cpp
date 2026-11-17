#include "xui/xui.h"
#define XUI_FEATURE_CATALOG
#include "../../src/feature_contract.g.hpp"
#include <iostream>

int main() {
    std::cout << "{\"properties\":[";
    bool first = true;
    for (const auto& property : xui::feature_contract::properties) {
        if (!first) std::cout << ',';
        first = false;
        std::cout << '[' << property.id << ',' << property.fields << ",\"" << property.setters
            << "\",\"" << property.getters << "\"]";
    }
    std::cout << "],\"controls\":[";
    first = true;
    for (const auto& control : xui::feature_contract::controls) {
        if (!first) std::cout << ',';
        first = false;
        std::cout << '[' << control.id << ',' << control.children << ',' << control.mode << ',' << control.languages << ']';
    }
    std::cout << "]}\n";
}
