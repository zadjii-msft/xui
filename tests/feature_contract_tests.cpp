#include "xui/xui.h"
#define XUI_FEATURE_CATALOG
#include "../src/feature_contract.g.hpp"
#include <windows.h>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string_view>

namespace {
std::map<std::string, unsigned> counts;
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void ok(xui_status status) { require(status == XUI_OK, "Feature contract API call failed"); }
xui_feature_value value() {
    xui_feature_value result{};
    result.size = sizeof(result);
    result.version = XUI_FEATURE_VERSION;
    return result;
}
xui_handle window() {
    xui_window_options options{sizeof(options), XUI_ABI_VERSION, {"Contract", 8, 0}, 400, 300};
    xui_handle result{};
    ok(xui_window_create(&options, &result));
    return result;
}
xui_handle create(xui_handle owner, unsigned kind, xui_handle child = 0, xui_handle second = 0) {
    xui_feature_options options{sizeof(options), XUI_FEATURE_VERSION, {"Feature", 7, 0}, child, second};
    xui_handle result{};
    ok(xui_feature_create(owner, kind, &options, &result));
    return result;
}
xui_handle label(xui_handle owner) {
    xui_handle result{};
    ok(xui_create(owner, XUI_LABEL, {"Child", 5, 0}, 0, &result));
    return result;
}
void contracts() {
    for (const auto& contract : xui::feature_contract::controls) {
        const auto owner = window();
        const auto first = contract.children ? (contract.id == XUI_VIEW_PICKER ? create(owner, XUI_ITEMS_VIEW) : label(owner)) : 0;
        const auto second = contract.children == 2 ? label(owner) : 0;
        xui_feature_options options{sizeof(options), XUI_FEATURE_VERSION, {"Feature", 7, 0}, first, second};
        xui_handle result{};
        options.mode = contract.mode + 1;
        require(xui_feature_create(owner, contract.id, &options, &result) == XUI_INVALID_ARGUMENT && !result,
                "Out-of-contract construction mode must fail");
        options.mode = 0;
        options.second = contract.children == 2 ? 0 : label(owner);
        require(xui_feature_create(owner, contract.id, &options, &result) == XUI_INVALID_ARGUMENT && !result,
                "Wrong child cardinality must fail");
        options.second = second;
        ok(xui_feature_create(owner, contract.id, &options, &result));
        require(result != 0, "Declared feature must construct");
        ok(xui_window_destroy(owner));
    }
    const auto owner = window();
    const auto range = create(owner, XUI_RANGE_INPUT);
    const auto badge = create(owner, XUI_INFO_BADGE);
    for (const auto& property : xui::feature_contract::properties) {
        if (!property.fields) continue;
        auto invalid = value();
        const auto unused = ~property.fields;
        if (unused & 1) invalid.a = 1;
        else if (unused & 2) invalid.b = 1;
        else if (unused & 4) invalid.c = 1;
        else if (unused & 8) invalid.d = 1;
        else if (unused & 16) invalid.first = 1;
        else if (unused & 32) invalid.second = 1;
        else invalid.text = {"x", 1, 0};
        require(xui_feature_set(range, property.id, &invalid) == XUI_INVALID_ARGUMENT,
                "Every setter must reject an inactive carrier before dispatch");
    }
    auto v = value(); v.a = 12;
    ok(xui_feature_set(range, XUI_F_VALUE, &v));
    require(xui_feature_set(badge, XUI_F_VALUE, &v) == XUI_WRONG_KIND, "Wrong setter control must fail");
    v = value();
    require(xui_feature_get(badge, XUI_F_VALUE, &v) == XUI_WRONG_KIND, "Wrong getter control must fail");
    require(xui_feature_get(range, XUI_F_ORIENTATION, &v) == XUI_INVALID_ARGUMENT, "Write-only property must not acquire a getter");
    require(xui_feature_set(range, XUI_F_FOCUSED, &v) == XUI_INVALID_ARGUMENT, "Read-only property must not acquire a setter");
    require(xui_feature_set(range, 0xffffffffu, &v) == XUI_INVALID_ARGUMENT, "Unknown property must fail");
    require(xui_feature_action(range, XUI_A_SET_DOT, 0, 0) == XUI_WRONG_KIND, "Wrong action target must fail");
    require(xui_feature_action(range, 0xffffffffu, 0, 0) == XUI_INVALID_ARGUMENT, "Unknown action must fail");
    ok(xui_feature_get(range, XUI_F_VALUE, &v));
    require(v.a == 12, "Invalid mutations must preserve the value");
    ok(xui_window_destroy(owner));
}
std::uint64_t ticks(FILETIME value) { return (std::uint64_t(value.dwHighDateTime) << 32) | value.dwLowDateTime; }
template<class F> void measure(const char* name, unsigned count, F action) {
    if (const auto found = counts.find(name); found != counts.end()) count = found->second;
    FILETIME creation{}, exit{}, k0{}, u0{}, k1{}, u1{};
    require(GetThreadTimes(GetCurrentThread(), &creation, &exit, &k0, &u0) != 0, "Read CPU before");
    const auto start = std::chrono::steady_clock::now();
    for (unsigned i = 0; i != count; ++i) action(i);
    const auto wall = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    require(GetThreadTimes(GetCurrentThread(), &creation, &exit, &k1, &u1) != 0, "Read CPU after");
    std::cout << "{\"phase\":\"" << name << "\",\"iterations\":" << count << ",\"wall_ms\":" << wall
        << ",\"cpu_ms\":" << (ticks(k1) + ticks(u1) - ticks(k0) - ticks(u0)) / 10000.0
        << ",\"kernel_before100ns\":" << ticks(k0) << ",\"kernel_after100ns\":" << ticks(k1)
        << ",\"user_before100ns\":" << ticks(u0) << ",\"user_after100ns\":" << ticks(u1) << "}\n";
}
void performance() {
    const auto owner = window();
    const auto range = create(owner, XUI_RANGE_INPUT);
    const auto progress = create(owner, XUI_PROGRESS);
    const auto toggle = create(owner, XUI_TOGGLE_SWITCH);
    auto v = value();
    measure("feature-number-set", 2097152, [&](unsigned i) { v.a = i % 2; ok(xui_feature_set(range, XUI_F_VALUE, &v)); });
    v = value();
    measure("feature-bool-set", 2097152, [&](unsigned i) { v.first = i % 2; ok(xui_feature_set(toggle, XUI_F_CHECKED, &v)); });
    measure("feature-range-get", 2097152, [&](unsigned) { auto output = value(); ok(xui_feature_get(progress, XUI_F_RANGE, &output)); });
    v = value(); v.b = 100; v.c = 1; v.d = 10;
    measure("feature-range-set", 1048576, [&](unsigned i) { v.a = i % 2; ok(xui_feature_set(range, XUI_F_RANGE, &v)); });
    ok(xui_window_destroy(owner));
    measure("feature-composition-lifetime", 8192, [&](unsigned) {
        const auto w = window();
        create(w, XUI_ADAPTIVE_LAYOUT, label(w), label(w));
        create(w, XUI_POPUP, label(w));
        create(w, XUI_TOGGLE_SWITCH);
        ok(xui_window_destroy(w));
    });
}
}
int main(int argc, char** argv) {
    try {
        char* path{};
        size_t length{};
        require(_dupenv_s(&path, &length, "XUI_PERF_COUNTS") == 0, "Read fixed iteration protocol");
        if (path) {
            std::ifstream input(path);
            std::free(path);
            require(bool(input), "Open fixed iteration protocol");
            std::string name; unsigned count{};
            while (input >> name >> count)
                require(count >= 32 && count % 32 == 0 && counts.emplace(name, count).second, "Invalid or duplicate iteration count");
            require(input.eof(), "Malformed iteration protocol");
        }
        contracts();
        if (argc == 2 && std::string_view(argv[1]) == "--measure") performance();
        else require(argc == 1, "Usage: feature_contract_tests [--measure]");
        std::cout << "Feature construction, carrier, applicability and mutability contracts passed.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
