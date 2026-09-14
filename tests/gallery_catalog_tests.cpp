#include "../demo/gallery_catalog.hpp"
#include <iostream>
#include <set>
#include <stdexcept>

int main() {
    const auto require = [](bool value) { if (!value) throw std::runtime_error("Gallery catalog contract"); };
    try {
        xui::NavigationView catalog;
        catalog.set_items(gallery::navigation_items());
        require(catalog.match_count() == gallery::entries.size());
        std::set<std::wstring> ids;
        std::set<xui::ItemKey> keys;
        for (const auto& item : catalog.entries()) require(keys.insert(item.key).second);
        for (std::size_t i = 0; i < gallery::entries.size(); ++i) {
            require(ids.insert(gallery::entries[i].id).second);
            const xui::ItemKey key{i + 1, 1};
            require(gallery::entry_index(key) == i);
            const auto* item = catalog.find(key);
            require(item && item->label == gallery::entries[i].title && item->parent.has_value());
            require(item->section == xui::NavigationSection::main && item->selectable);
            require(!catalog.find(*item->parent)->selectable);
            require(catalog.items()->source()->find(key).has_value());
            require(*gallery::entries[i].code != L'\0');
        }
        require(gallery::entries.size() == 47);
        require(std::wstring_view(gallery::entries[45].id) == L"web-content");
        require(std::wstring_view(gallery::entries[46].id) == L"navigation-view");
        for (auto id : {L"radio", L"combo", L"popup", L"tooltip", L"actions", L"number", L"range", L"disclosure", L"progress"})
            require(ids.contains(id));
        require(!gallery::entry_index({999, 1}) && !gallery::entry_index({1, 2}));
        require(gallery::entry_index(gallery::home_key) == 0 && gallery::entry_index(gallery::appearance_key) == 16);
        int changes{};
        catalog.on_select([&](xui::ItemKey) { ++changes; });
        require(catalog.select({1, 1}) && catalog.select({1, 1}) && changes == 1);
        const auto input_group = *catalog.find({1, 1})->parent;
        require(catalog.items()->disclose(input_group, false));
        require(!catalog.items()->source()->find({1, 1}) && catalog.selected() == xui::ItemKey{1, 1});
        require(catalog.items()->disclose(input_group, true));
        require(catalog.items()->source()->find({1, 1}).has_value());
        require(catalog.items()->disclose(input_group, false));
        require(catalog.select({1, 1}) && catalog.item_expanded(input_group) && changes == 1);
        catalog.on_filter([&](const auto& query) {
            require(catalog.filter() == query && catalog.search()->text() == query);
            if (query == L"COLLECTIONS") require(catalog.match_count() == 5);
        });
        for (const auto& [query, count] : std::array<std::pair<const wchar_t*, unsigned>, 4>{
            {{L"COLLECTIONS", 5u}, {L"input", 11u}, {L"grid", 3u}, {L"nothing-matches-this", 0u}}}) {
            catalog.set_filter(query);
            require(catalog.match_count() == count && catalog.selected() == xui::ItemKey{1, 1});
            require(catalog.header_items()->source()->size() == 1 && catalog.footer_items()->source()->size() == 1);
        }
        catalog.set_filter(L"buttons");
        require(catalog.match_count() == 1 && catalog.selected() == xui::ItemKey{1, 1});
        require(!catalog.select({1, 1}) && catalog.items()->source()->find({2, 1}).has_value());
        require(catalog.selected() == xui::ItemKey{1, 1});
        require(catalog.header_items()->select(gallery::home_key));
        require(catalog.selected() == gallery::home_key && catalog.items()->selection().empty());
        require(catalog.footer_items()->select(gallery::appearance_key));
        require(catalog.selected() == gallery::appearance_key && catalog.header_items()->selection().empty());
        catalog.set_filter(L"");
        require(catalog.match_count() == gallery::entries.size());
        catalog.set_expanded(false);
        catalog.arrange({0, 0, 260, 600});
        require(!catalog.search()->visible() && !catalog.items()->source()->find({1, 1}));
        require(catalog.header_items()->source()->size() == 1 && catalog.footer_items()->source()->size() == 1);
        catalog.set_expanded(true);
        catalog.arrange({0, 0, 260, 600});
        require(catalog.search()->visible() && catalog.items()->source()->find({1, 1}).has_value());
        require(catalog.selected() == gallery::appearance_key);
        gallery::Numbers ascending, descending(true);
        require(ascending.size() == 100000 && sizeof(ascending) <= 32);
        for (std::size_t i = 0; i < ascending.size(); ++i) {
            require(ascending.find(ascending.key(i)) == i);
            require(descending.find(ascending.key(i)) == ascending.size() - 1 - i);
        }
        xui::DataGrid grid;
        grid.set_columns({{L"Item", 240}, {L"Size", 120, true}});
        grid.set_source(std::make_shared<gallery::Numbers>());
        grid.arrange({0, 0, 500, 260});
        const auto [first, last] = grid.visible_rows();
        require(last - first < 12);
        require(grid.select({100000, 1}));
        grid.set_source(std::make_shared<gallery::Numbers>(true));
        require(grid.selected() == xui::RowKey{100000, 1});
        require(grid.reorder_column(0, 1) && grid.source_column(0) == 1);
        struct Measured : xui::Element {
            int calls{};
            xui::Size measure(xui::Size available) override { ++calls; return Element::measure(available); }
        };
        auto active = std::make_shared<Measured>(), inactive = std::make_shared<Measured>();
        xui::PageView pages;
        pages.add_page(active); pages.add_page(inactive);
        pages.arrange({0, 0, 500, 300});
        require(active->calls > 0 && inactive->calls == 0);
        pages.select(1); pages.arrange({0, 0, 500, 300});
        const auto hidden_calls = active->calls;
        for (int i = 0; i < 50; ++i) pages.arrange({0, 0, 500, 300});
        require(active->calls == hidden_calls && active->bounds().width == 0);
        pages.select(0); pages.arrange({0, 0, 500, 300});
        require(active->bounds().width == 500);
        std::cout << "Gallery navigation metadata, hierarchy, filtering, pinned selection, and grid fixtures passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
