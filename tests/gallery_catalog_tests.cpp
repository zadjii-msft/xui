#include "../demo/gallery_catalog.hpp"
#include <iostream>
#include <set>
#include <stdexcept>

int main() {
    const auto require = [](bool value) { if (!value) throw std::runtime_error("Gallery catalog contract"); };
    try {
        gallery::Catalog all;
        require(all.size() == gallery::entries.size());
        std::set<std::wstring> ids;
        for (std::size_t i = 0; i < all.size(); ++i) {
            require(ids.insert(gallery::entries[i].id).second);
            require(all.find(all.key(i)) == i);
            require(!all.text(i, 0).empty());
            require(*gallery::entries[i].code != L'\0');
        }
        require(gallery::Catalog(L"COLLECTIONS").size() == 5);
        require(gallery::Catalog(L"input").size() == 11);
        require(all.size() == 46);
        for (auto id : {L"radio", L"combo", L"popup", L"tooltip", L"actions", L"number", L"range", L"disclosure", L"progress"})
            require(ids.contains(id));
        require(gallery::Catalog(L"grid").size() == 3);
        require(gallery::Catalog(L"nothing-matches-this").size() == 0);
        require(!all.find({999, 1}));
        require(!all.find({1, 2}));
        gallery::Catalog sorted(L"", true);
        require(sorted.text(0, 0) > sorted.text(sorted.size() - 1, 0));
        for (std::size_t i = 0; i < all.size(); ++i) require(sorted.find(all.key(i)).has_value());
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
        std::cout << "Gallery metadata, stable filtering, constant-storage 100k rows, visible range and sorting passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
