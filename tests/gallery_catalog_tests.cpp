#include "../demo/gallery_catalog.hpp"
#include "../demo/gallery_reference.hpp"
#include "../demo/gallery_urls.hpp"
#include <filesystem>
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
        require(gallery::references.size() == gallery::entries.size());
        std::array<std::size_t, 3> language_examples{};
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
            const auto& reference = gallery::references[i];
            require(std::wstring_view(reference.id) == gallery::entries[i].id);
            require(std::wstring_view(reference.usage).size() >= 30);
            require(std::wstring_view(reference.exercise).size() >= 30);
            require(std::wstring_view(reference.notes).size() >= 30);
            const std::wstring_view docs = reference.docs;
            require(docs.starts_with(L"docs/specs/") && docs.ends_with(L".md"));
            require(docs.find(L"..") == std::wstring_view::npos);
            require(std::filesystem::is_regular_file(std::filesystem::path(XUI_SOURCE_DIRECTORY) / docs));
            require(gallery::documentation_url(docs).starts_with(L"https://zadjii-msft.github.io/xui/"));
            const wchar_t* snippets[]{reference.csharp, reference.rust, reference.xui};
            const wchar_t* languages[]{L"C#", L"Rust", L".xui"};
            for (std::size_t language = 0; language != language_examples.size(); ++language) {
                if (*snippets[language]) ++language_examples[language];
                else require(std::wstring_view(reference.notes).find(languages[language]) != std::wstring_view::npos);
            }
        }
        for (auto count : language_examples) require(count > 0);
        require(gallery::entries.size() == 52);
        require(std::wstring_view(gallery::entries[51].id) == L"document-motion");
        require(std::wstring_view(gallery::references[51].docs) == L"docs/specs/animations.md");
        require(std::wstring_view(gallery::entries[50].id) == L"content-motion");
        require(std::wstring_view(gallery::references[50].docs) == L"docs/specs/animations.md");
        require(std::wstring_view(gallery::entries[49].id) == L"feedback-motion");
        require(std::wstring_view(gallery::entries[48].id) == L"animations");
        require(std::wstring_view(gallery::entries[48].title) == L"Motion");
        require(std::wstring_view(gallery::references[48].docs) == L"docs/specs/animations.md");
        for (const auto* query : {L"motion", L"animations"}) {
            catalog.set_filter(query);
            require(catalog.item_matches({49, 1}));
        }
        catalog.set_filter(L"");
        require(std::wstring_view(gallery::entries[45].id) == L"web-content");
        require(std::wstring_view(gallery::entries[46].id) == L"navigation-view");
        require(std::wstring_view(gallery::entries[47].id) == L"miller-columns");
        require(std::wstring_view(gallery::entries[47].group) == L"Collections");
        require(std::wstring_view(gallery::entries[47].code).find(L"xui::MillerColumns") != std::wstring_view::npos);
        require(std::wstring_view(gallery::entries[47].code).find(L"next.resize(column + 1)") != std::wstring_view::npos);
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
            if (query == L"COLLECTIONS") require(catalog.match_count() == 6);
        });
        for (const auto& [query, count] : std::array<std::pair<const wchar_t*, unsigned>, 4>{
            {{L"COLLECTIONS", 6u}, {L"input", 11u}, {L"grid", 3u}, {L"nothing-matches-this", 0u}}}) {
            catalog.set_filter(query);
            require(catalog.match_count() == count && catalog.selected() == xui::ItemKey{1, 1});
            require(catalog.header_items()->source()->size() == 1 && catalog.footer_items()->source()->size() == 2);
        }
        for (const auto* query : {L"miller-columns", L"MILLER", L"folder", L"hierarchy", L"immutable",
                L"horizontal scrolling", L"vertical scrolling"}) {
            catalog.set_filter(query);
            require(catalog.item_matches({48, 1}) && catalog.items()->source()->find({48, 1}).has_value());
            require(catalog.selected() == xui::ItemKey{1, 1});
            if (std::wstring_view(query) == L"miller-columns") require(catalog.match_count() == 1);
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
        require(catalog.header_items()->source()->size() == 1 && catalog.footer_items()->source()->size() == 2);
        catalog.set_expanded(true);
        catalog.arrange({0, 0, 260, 600});
        require(catalog.search()->visible() && catalog.items()->source()->find({1, 1}).has_value());
        require(catalog.selected() == gallery::appearance_key);
        require(!catalog.item_expanded(gallery::links_key));
        require(catalog.set_item_expanded(gallery::links_key, true));
        for (const auto& link : gallery::navigation_links) {
            require(catalog.find(link.key)->parent == gallery::links_key);
            require(catalog.footer_items()->source()->find(link.key).has_value());
            require(!gallery::entry_index(link.key));
            require(std::wstring_view(gallery::navigation_link(link.key)) == link.path);
            require(std::filesystem::is_regular_file(std::filesystem::path(XUI_SOURCE_DIRECTORY) / link.path));
        }
        require(!gallery::navigation_link(gallery::home_key));
        require(catalog.match_count() == gallery::entries.size());
        require(gallery::documentation_url(L"docs/specs/README.md") == L"https://zadjii-msft.github.io/xui/");
        require(gallery::documentation_url(L"docs/specs/controls/basic.md") ==
            L"https://zadjii-msft.github.io/xui/choose-a-control/controls/basic/");
        require(gallery::documentation_url(L"docs/specs/languages/declarative.md") ==
            L"https://zadjii-msft.github.io/xui/learn/languages/declarative/");
        require(gallery::documentation_url(L"docs/specs/menus-and-input.md") ==
            L"https://zadjii-msft.github.io/xui/contracts/menus-and-input/");
        require(gallery::documentation_url(L"docs/specs/winui-style.md") ==
            L"https://zadjii-msft.github.io/xui/contracts/winui-style/");
        gallery::FixtureMillerPath miller;
        require(miller.columns().size() == 1 && miller.columns().front().title == L"Projects");
        const auto roots = miller.columns().front().source;
        require(roots->size() == 28 && !miller.columns().front().selected);
        std::set<xui::ItemKey> fixture_keys;
        for (std::size_t column = 0; column < gallery::FixtureMillerItems::levels; ++column) {
            const auto source = miller.columns()[column].source;
            require(source->size() == 28);
            for (std::size_t row = 0; row < source->size(); ++row) {
                const auto key = source->key(row);
                require(fixture_keys.insert(key).second && source->find(key) == row);
                require(!source->find({key.id, 2}) && !source->item(row).primary.empty());
                require(source->hierarchy(row).expandable ==
                    (column + 1 < gallery::FixtureMillerItems::levels && row < 3));
            }
            require(!source->find({0, 1}));
            require(miller.select(column, source->key(0)));
            require(miller.columns()[column].selected == source->key(0));
            require(miller.columns()[column].source == source);
            require(miller.columns().size() == std::min(column + 2, gallery::FixtureMillerItems::levels));
        }
        require(miller.columns().size() == 8 && miller.columns().front().source == roots);
        const auto deep_path = miller.columns();
        const auto engineering = deep_path[1].source->key(1);
        require(miller.select(1, engineering));
        require(miller.columns().size() == 3 && miller.columns()[2].title == L"Engineering");
        require(miller.columns()[0].selected == deep_path[0].selected);
        require(miller.columns()[1].selected == engineering && !miller.columns()[2].selected);
        require(!miller.columns()[2].source->find(deep_path[2].source->key(0)));
        require(deep_path.size() == 8 && deep_path[1].selected == deep_path[1].source->key(0));
        const auto replacement = miller.columns()[2].source;
        require(!miller.select(3, engineering) && !miller.select(1, {engineering.id, 2}));
        require(miller.columns().size() == 3 && miller.columns()[2].source == replacement);
        require(miller.select(1, miller.columns()[1].source->key(3)));
        require(miller.columns().size() == 2 && miller.columns()[1].selected == deep_path[1].source->key(3));
        require(miller.select(0, roots->key(1)));
        require(miller.columns().size() == 2 && miller.columns()[1].title == L"Beacon");
        require(!miller.columns()[1].source->find(deep_path[1].source->key(0)));
        require(miller.select(0, roots->key(27)));
        require(miller.columns().size() == 1 && miller.columns().front().selected == roots->key(27));
        miller = gallery::FixtureMillerPath{};
        require(miller.columns().size() == 1 && !miller.columns().front().selected);
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
        std::cout << "Gallery metadata, filtering, pinned selection, Miller hierarchy, and grid fixtures passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
