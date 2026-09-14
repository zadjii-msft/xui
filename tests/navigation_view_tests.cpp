#include "xui/navigation.hpp"
#include <iostream>
#include <limits>

namespace {
using namespace xui;
int checks{};
void require(bool value, const char* message) { ++checks; if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F&& callback) {
    bool threw{};
    try { callback(); } catch (const std::exception&) { threw = true; }
    require(threw, "Invalid navigation data must fail explicitly");
}
std::vector<NavigationItem> fixture() {
    return {
        {{1, 1}, {}, L"Home", ButtonIcon::home, {}, {}, true, true, true, NavigationSection::header},
        {{10, 1}, {}, L"Workspace", ButtonIcon::folder, L"projects", L"3", true, false},
        {{11, 1}, ItemKey{10, 1}, L"Overview", ButtonIcon::library},
        {{12, 1}, ItemKey{10, 1}, L"Team", ButtonIcon::folder, {}, {}, true, false, false},
        {{13, 1}, ItemKey{12, 1}, L"Members", ButtonIcon::home, L"people collaborators"},
        {{14, 1}, ItemKey{10, 1}, L"Disabled", ButtonIcon::close, {}, {}, false},
        {{20, 1}, {}, L"Reports", ButtonIcon::library},
        {{30, 1}, {}, L"Unavailable", ButtonIcon::folder, {}, {}, false, false},
        {{31, 1}, ItemKey{30, 1}, L"Unavailable child"},
        {{40, 1}, {}, L"Settings", ButtonIcon::settings, {}, {}, true, true, true, NavigationSection::footer}};
}
void state_and_input() {
    NavigationView nav(L"Test navigation"); nav.set_items(fixture()); nav.arrange({0, 0, 280, 600});
    require(nav.header_items()->source()->size() == 1 && nav.footer_items()->source()->size() == 1, "Pinned sections have separate viewports");
    require(nav.items()->source()->size() == 7 && nav.match_count() == 5, "Initial tree hides a closed nested branch");
    require(nav.items()->source()->hierarchy(*nav.items()->source()->find({12, 1})).depth == 1, "Nested item depth");
    require(!nav.items()->multiple_selection(), "Navigation is single-selection");
    int selections{}, activations{}, disclosures{}, filters{};
    nav.on_select([&](ItemKey) { ++selections; });
    nav.on_activate([&](ItemKey) { ++activations; });
    nav.on_expanded([&](bool) { ++disclosures; });
    nav.on_filter([&](const auto&) { ++filters; });
    require(nav.select({13, 1}) && nav.item_expanded({12, 1}), "Selecting a nested page reveals its ancestors");
    require(nav.selected() == ItemKey{13, 1} && selections == 1, "One navigation event per changed key");
    require(nav.select({13, 1}) && selections == 1, "Reselecting is idempotent");
    require(!nav.select({14, 1}) && !nav.select({31, 1}) && !nav.select({10, 1}) && !nav.select({20, 2}), "Disabled, group and stale identities cannot select");
    nav.items()->select({20, 1}, SelectionGesture::toggle);
    nav.items()->select({20, 1}, SelectionGesture::toggle);
    require(nav.selected() == ItemKey{20, 1} && nav.items()->selection().storage_size() == 1, "Ctrl and Space cannot remove the active page");
    nav.items()->select_all();
    require(nav.items()->selection().storage_size() == 1, "Ctrl+A cannot create multiple selections");
    require(nav.items()->remove_selection({20, 1}) && !nav.selected() && nav.items()->selection().empty(),
        "Explicit accessibility removal clears selection instead of toggling navigation");
    nav.items()->select({11, 1}, SelectionGesture::extend);
    require(nav.selected() == ItemKey{11, 1} && !nav.items()->selection().contains({20, 1}), "Shift replaces instead of range selecting");
    nav.items()->select({20, 1}, SelectionGesture::focus_only);
    require(nav.selected() == ItemKey{11, 1}, "UIA focus does not navigate");
    nav.items()->activate_item({20, 1});
    require(nav.selected() == ItemKey{20, 1} && activations == 1, "Activation selects and invokes");
    nav.header_items()->select({1, 1});
    require(nav.selected() == ItemKey{1, 1} && nav.items()->selection().empty(), "Pinned selection clears main selection");
    nav.footer_items()->select({40, 1});
    require(nav.header_items()->selection().empty() && nav.footer_items()->selection().contains({40, 1}), "Footer shares one selected identity");
    nav.select({13, 1}); nav.items()->disclose({12, 1}, false);
    require(nav.selected() == ItemKey{13, 1} && nav.items()->selection().focused() == ItemKey{12, 1}, "Collapse preserves page and moves row focus to ancestor");
    nav.items()->horizontal(true, SelectionGesture::replace);
    require(nav.item_expanded({12, 1}), "Right expands focused group");
    nav.items()->horizontal(true, SelectionGesture::replace);
    require(nav.items()->selection().focused() == ItemKey{13, 1}, "Right enters expanded group");
    nav.items()->horizontal(false, SelectionGesture::replace);
    require(nav.items()->selection().focused() == ItemKey{12, 1}, "Left focuses parent without toggling it");
    nav.items()->edge(false);
    require(nav.items()->selection().focused() == ItemKey{10, 1}, "Home focuses first group");
    nav.items()->step(1);
    require(nav.selected() == ItemKey{11, 1}, "Down selects first page");
    nav.items()->select({14, 1}, SelectionGesture::focus_only);
    nav.items()->select({13, 1}, SelectionGesture::focus_only);
    nav.items()->step(1);
    require(nav.selected() == ItemKey{20, 1}, "Arrows skip disabled rows");
    nav.items()->edge(true);
    require(nav.selected() == ItemKey{20, 1}, "End skips disabled branches");
    nav.select({13, 1}); nav.set_expanded(false); nav.arrange({0, 0, 64, 600});
    require(nav.measure({1000, 600}).width == 64 && !nav.search()->visible(), "Compact width hides native search");
    require(nav.items()->source()->size() == 3, "Compact rail shows root icons only");
    const auto rows = nav.items()->visible_content();
    require(rows[0].compact && rows[0].selected_descendant && rows[0].content.icon == ButtonIcon::folder, "Collapsed ancestor marks active nested page");
    require(nav.items()->disclosure_hit({20, 20}), "Compact group is one disclosure target");
    nav.items()->select({10, 1});
    require(nav.expanded() && disclosures == 2 && nav.selected() == ItemKey{13, 1}, "Rail group opens pane without changing page");
    nav.arrange({0, 0, 280, 600});
    require(!nav.items()->disclosure_hit({20, 20}) && nav.items()->disclosure_hit({250, 20}), "Expanded chevron has a separate trailing hit target");
    nav.items()->hover_item(ItemKey{10, 1});
    require(nav.items()->visible_content()[0].hovered && nav.items()->help_text() == L"Workspace", "Hover supplies the full item label");
    nav.items()->hover_item({});
    require(nav.items()->help_text().empty(), "Pointer exit clears row tooltip");
    nav.set_item_expanded({12, 1}, false);
    nav.set_filter(L"COLLABORATORS");
    require(filters == 1 && nav.search()->text() == L"COLLABORATORS", "Filter property synchronizes native editor");
    require(nav.match_count() == 1 && nav.items()->source()->size() == 3, "Keyword match keeps all ancestors");
    require(nav.item_expanded({12, 1}) && nav.set_item_expanded({12, 1}, false) && !nav.item_expanded({12, 1}),
        "Matching paths initially expand but can be collapsed while filtering");
    require(nav.header_items()->source()->size() == 1 && nav.footer_items()->source()->size() == 1, "Search does not remove pinned actions");
    nav.set_filter(L"projects");
    require(nav.match_count() == 3, "Matching category keywords includes descendants");
    nav.set_filter(L"nothing-matches");
    require(nav.match_count() == 0 && nav.items()->source()->size() == 0 && !nav.items()->selection().focused(), "Empty search repairs virtual focus");
    require(nav.selected() == ItemKey{13, 1} && !nav.select({11, 1}), "Hidden page identity survives but cannot be invoked by filtered navigation");
    nav.set_filter(L"");
    require(!nav.item_expanded({12, 1}) && nav.selected() == ItemKey{13, 1}, "Clearing filter restores saved expansion and page");
    nav.search()->commit_text(L"reports");
    require(nav.filter() == L"reports" && nav.match_count() == 1, "Committed native search routes through built-in filtering");
    nav.set_filter(L""); nav.set_search_visible(false); nav.arrange({0, 0, 280, 600});
    require(!nav.search()->visible(), "Applications can hide the built-in search");
    nav.set_enabled(false);
    require(!nav.items()->select({20, 1}) && !nav.set_item_expanded({10, 1}, false), "Disabled view rejects child navigation");
}
void search_disclosure() {
    NavigationView nav;
    auto items = fixture();
    items.push_back({{50, 1}, {}, L"Reference", ButtonIcon::folder, {}, {}, true, false, false});
    items.push_back({{51, 1}, ItemKey{50, 1}, L"Guide"});
    nav.set_items(items); nav.arrange({0, 0, 280, 600});
    nav.select({13, 1});
    nav.set_item_expanded({10, 1}, false); nav.set_item_expanded({12, 1}, false);
    nav.set_filter(L"collaborators");
    require(nav.item_expanded({10, 1}) && nav.item_expanded({12, 1}), "Search opens collapsed ancestors of a matching item");
    require(!nav.item_expanded({50, 1}), "Search leaves a collapsed nonmatching section closed");
    require(nav.items()->disclose({12, 1}, false) && nav.items()->source()->size() == 2,
        "Search-result disclosure hides nested descendants");
    require(nav.item_matches({13, 1}) && nav.match_count() == 1 && nav.selected() == ItemKey{13, 1},
        "Collapsed matches retain filter membership, count and selected identity");
    nav.search()->commit_text(L"people");
    require(!nav.item_expanded({12, 1}) && !nav.items()->source()->find({13, 1}),
        "Native query edits retain a manual collapse");
    nav.set_items(items);
    nav.set_expanded(false); nav.set_expanded(true);
    require(!nav.item_expanded({12, 1}), "Source refresh and pane transitions retain search disclosure overrides");
    nav.set_filter(L"no matches");
    require(nav.match_count() == 0 && !nav.item_matches({13, 1}), "Empty results clear membership without resetting disclosure overrides");
    nav.set_filter(L"Members");
    require(!nav.item_expanded({12, 1}), "A temporarily absent match retains manual collapse when it returns");
    require(nav.set_item_expanded({12, 1}, true), "A collapsed search group can be reopened");
    nav.set_filter(L"projects");
    require(nav.item_expanded({12, 1}), "Query edits retain manual expansion");
    nav.items()->disclose({10, 1}, false);
    nav.set_filter(L"Guide");
    require(nav.item_expanded({50, 1}) && nav.items()->source()->find({51, 1}),
        "A newly matching section expands unless it has a manual override");
    nav.set_filter(L"Members");
    require(!nav.item_expanded({10, 1}) && nav.items()->source()->size() == 1,
        "Outer section collapse survives query changes and hidden matches");
    nav.set_filter(L"");
    require(!nav.item_expanded({10, 1}) && !nav.item_expanded({12, 1}) && !nav.item_expanded({50, 1}),
        "Clearing search restores pre-search disclosure states");
    nav.set_filter(L"Members");
    require(nav.item_expanded({10, 1}) && nav.item_expanded({12, 1}), "A new search starts without previous manual overrides");
    nav.set_item_expanded({10, 1}, false);
    require(nav.select({13, 1}) && nav.item_expanded({10, 1}), "Explicit navigation still reveals a collapsed matching page");
    nav.set_item_expanded({12, 1}, false);
    auto removed = items;
    std::erase_if(removed, [](const auto& item) { return item.key == ItemKey{12, 1} || item.key == ItemKey{13, 1}; });
    nav.set_items(removed); nav.set_items(items);
    require(nav.item_expanded({12, 1}), "Removed identities do not leak search overrides into replacement items");
    nav.set_filter(L"");
    require(!nav.item_expanded({10, 1}), "Navigation within search does not overwrite pre-search disclosure");
}
void scrolled_focus_repair() {
    NavigationView nav;
    std::vector<NavigationItem> items{{{1, 1}, {}, L"Branch", ButtonIcon::folder, {}, {}, true, false}};
    for (std::uint64_t i = 2; i < 82; ++i) items.push_back({{i, 1}, ItemKey{1, 1}, L"Child"});
    for (std::uint64_t i = 100; i < 200; ++i) items.push_back({{i, 1}, {}, L"Root"});
    nav.set_items(items); nav.arrange({0, 0, 280, 400});
    nav.select({80, 1}); nav.arrange({0, 0, 280, 400});
    require(nav.items()->offset() > 1000, "Long branch scrolls the selected child into view");
    nav.set_item_expanded({1, 1}, false); nav.arrange({0, 0, 280, 400});
    require(nav.items()->selection().focused() == ItemKey{1, 1} && nav.items()->offset() == 0,
        "Collapsed branch reveals repaired ancestor even when later roots keep the viewport scrollable");
    nav.select({80, 1}); nav.arrange({0, 0, 280, 400});
    nav.set_expanded(false); nav.arrange({0, 0, 64, 400});
    require(nav.items()->selection().focused() == ItemKey{1, 1} && nav.items()->offset() == 0,
        "Compact rail reveals active ancestor rather than an absent selected descendant");
    nav.set_expanded(true); nav.select({80, 1}); nav.arrange({0, 0, 280, 400});
    nav.set_filter(L"Root"); nav.arrange({0, 0, 280, 400});
    require(nav.items()->selection().focused() == ItemKey{100, 1} && nav.items()->offset() == 0,
        "Filtering reveals repaired focus while preserving the hidden page identity");
    require(nav.selected() == ItemKey{80, 1}, "Focus repair never navigates away from the active page");
}
void validation_and_lifetime() {
    NavigationView nav; nav.set_items(fixture()); nav.select({13, 1});
    auto invalid = fixture(); invalid.push_back(invalid.front());
    rejects([&] { nav.set_items(invalid); });
    require(nav.selected() == ItemKey{13, 1} && nav.entries().size() == 10, "Invalid replacement preserves prior snapshot");
    invalid = fixture(); invalid[1].parent = ItemKey{13, 1};
    rejects([&] { nav.set_items(invalid); });
    invalid = fixture(); invalid[1].parent = ItemKey{999, 1};
    rejects([&] { nav.set_items(invalid); });
    invalid = fixture(); invalid[1].parent = ItemKey{1, 1};
    rejects([&] { nav.set_items(invalid); });
    invalid = fixture(); invalid[0].label.clear();
    rejects([&] { nav.set_items(invalid); });
    rejects([&] { nav.set_filter(std::wstring(257, L'x')); });
    rejects([&] { nav.set_pane_widths(200, 20); });
    rejects([&] { nav.set_pane_widths(160, 200); });
    rejects([&] { nav.set_pane_widths(std::numeric_limits<float>::infinity(), 64); });
    auto items = fixture(); items.erase(items.begin() + 4);
    nav.set_items(items); require(!nav.selected(), "Removed selected identity is cleared");
    nav.select({11, 1}); items[1].enabled = false; nav.set_items(items);
    require(!nav.selected(), "Disabling an ancestor clears its selected page");
    std::vector<NavigationItem> many;
    for (std::uint64_t i = 1; i <= NavigationView::maximum_items; ++i) many.push_back({{i, 1}, {}, L"Page"});
    nav.set_items(many); nav.arrange({0, 0, 280, 600});
    require(nav.items()->visible_content().size() < 20 && nav.retained_children().size() == 7, "Thousands of pages retain only visible rows and fixed controls");
    many.push_back({{9000, 1}, {}, L"Extra"}); rejects([&] { nav.set_items(many); });
    many.clear();
    for (std::uint64_t i = 1; i <= 65; ++i) many.push_back({{i, 1}, i > 1 ? std::optional{ItemKey{i - 1, 1}} : std::nullopt, L"Deep"});
    rejects([&] { nav.set_items(many); });
    nav.set_items(fixture());
    for (float width : {0.0f, 32.0f, 64.0f, 280.0f}) for (float height : {0.0f, 40.0f, 120.0f, 600.0f}) {
        nav.arrange({0, 0, width, height});
        for (const auto& child : nav.retained_children()) {
            const auto b = child->bounds();
            require(b.width >= 0 && b.height >= 0, "Tiny layouts never produce negative bounds");
            if (b.width > 0 && b.height > 0) require(b.x >= 0 && b.y >= 0 && b.x + b.width <= width && b.y + b.height <= height,
                "Visible navigation children remain inside allocated bounds");
        }
    }
    auto owned = std::make_unique<NavigationView>(); owned->set_items(fixture());
    const auto retained = owned->items();
    owned->on_select([&](ItemKey) { owned.reset(); });
    require(retained->select({20, 1}) && !owned, "Selection callback can release its view");
    require(!retained->select({11, 1}) && !retained->disclose({10, 1}, true), "Retained rows disconnect after owner destruction");
    auto active = std::make_unique<NavigationView>(); active->set_items(fixture());
    const auto active_rows = active->items(); int activated{};
    active->on_select([&](ItemKey) { active.reset(); });
    active->on_activate([&](ItemKey) { ++activated; });
    active_rows->activate_item({20, 1});
    require(!active && activated == 1, "Activation copies callback before reentrant owner deletion");
}
}
int main() {
    try { state_and_input(); search_disclosure(); scrolled_focus_repair(); validation_and_lifetime(); std::cout << checks << " navigation view checks passed\n"; return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
