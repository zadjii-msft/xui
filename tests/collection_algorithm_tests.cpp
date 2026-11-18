#include "../src/collection_viewport.hpp"
#include "../src/identity_lookup.hpp"
#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <tuple>

namespace {
void require(bool value, const char* message) {
    if (!value) { std::cerr << message << '\n'; std::exit(1); }
}
template<class Height> void viewport_cases() {
    for (std::size_t count : {0, 1, 2, 15, 100, 1000000})
        for (std::size_t columns : {1, 2, 7})
            for (Height height : {Height(16), Height(31.5), Height(112)})
                for (Height viewport : {Height(0), Height(1), Height(32), Height(501.25)})
                    for (double offset : {0., 0.1, 31.7, 500., 10000000.}) {
                        const auto first = std::min(count, static_cast<std::size_t>(offset / height) * columns);
                        const auto end = std::min(count, first +
                            (static_cast<std::size_t>(std::ceil(viewport / height)) + 1) * columns);
                        const auto actual = xui::detail::uniform_viewport(count, height, offset, viewport, columns);
                        require(actual.begin == first && actual.end == end, "Uniform viewport changed finite caller policy");
                    }
}
}
int main() {
    viewport_cases<float>();
    viewport_cases<double>();
    const auto maximum = std::numeric_limits<std::size_t>::max();
    const auto infinity = std::numeric_limits<double>::infinity();
    require(xui::detail::bounded_index(infinity, maximum) == maximum, "Saturating index");
    require(xui::detail::bounded_index(std::numeric_limits<double>::quiet_NaN(), maximum) == 0, "NaN index");
    const auto tail = xui::detail::uniform_viewport(maximum, 1., 0., infinity, 7);
    require(tail.begin == 0 && tail.end == maximum, "Tail multiplication must not overflow");
    require(xui::detail::uniform_viewport(10, 0., 0., 20.).end == 0, "Invalid row height");
    struct Entry {
        int id; int version; int* slot{};
        auto identity() const { return std::pair{id, version}; }
    };
    std::array<Entry, 5> rows{{{3, 1}, {1, 2}, {3, 1}, {3, 2}, {2, 1}}};
    xui::detail::IdentityLookup<Entry, 5> lookup(rows);
    require(lookup.find(std::pair{9, 1}) == nullptr, "Unknown identity misses");
    require(lookup.find(std::pair{3, 1}) != lookup.find(std::pair{3, 2}), "Versions remain distinct");
    int retained{};
    lookup.find(rows[0].identity())->slot = &retained;
    require(lookup.find(rows[2].identity())->slot == &retained, "Duplicates share the canonical retained slot");
    require(rows[0].id == 3 && rows[1].id == 1 && rows[4].id == 2, "Admission order must not change");
    bool rejected{};
    try { xui::detail::IdentityLookup<Entry, 4> invalid(rows); }
    catch (const std::length_error&) { rejected = true; }
    require(rejected, "Excess identity input must fail explicitly");
    xui::detail::IdentityLookup<Entry, 0> empty(std::span<Entry>{});
    require(!empty.find(std::pair{0, 0}), "Empty index misses");
    std::cout << "Collection geometry and bounded identity contracts passed.\n";
}
