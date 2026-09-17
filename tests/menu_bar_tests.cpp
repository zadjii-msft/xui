#include "xui/menu_bar.hpp"
#include <iostream>

namespace {
using namespace xui;
int checks{};
void require(bool value, const char* text) { ++checks; if (!value) throw std::runtime_error(text); }
template<class F> void rejects(F&& callback) {
    bool threw{};
    try { callback(); } catch (const std::exception&) { threw = true; }
    require(threw, "Invalid menu bar input must fail explicitly");
}
CommandRecord group(CommandId id, std::wstring label, bool enabled = true, CommandId parent = 0) {
    CommandRecord result{id, parent, std::move(label)};
    result.kind = CommandKind::submenu;
    result.enabled = enabled;
    return result;
}
std::shared_ptr<const CommandSet> source(int& actions, bool file_enabled = true) {
    return std::make_shared<CommandSet>(std::vector<CommandRecord>{
        group(1, L"&File", file_enabled), {10, 1, L"Open", [&] { ++actions; }},
        group(11, L"Recent", true, 1), {12, 11, L"Nested", [&] { ++actions; }},
        group(2, L"&Disabled", false), {20, 2, L"Blocked", [&] { ++actions; }},
        group(3, L"&Edit"), {30, 3, L"Checked", [&] { ++actions; }, true, true},
        group(4, L"Tools && &More"), group(5, L"E&xit&")});
}
void validation() {
    MenuBar bar;
    rejects([&] { bar.set_commands({}); });
    for (auto kind : {CommandKind::action, CommandKind::section, CommandKind::separator}) {
        CommandRecord record{1, 0, L"Not a group"}; record.kind = kind;
        rejects([&] { bar.set_commands(std::make_shared<CommandSet>(std::vector{record})); });
    }
    std::vector<CommandRecord> roots;
    for (CommandId id = 1; id <= 64; ++id) roots.push_back(group(id, L"Group"));
    bar.set_commands(std::make_shared<CommandSet>(roots));
    require(bar.retained_children().size() == 64, "Menu bar accepts 64 root groups");
    roots.push_back(group(65, L"Too many"));
    const auto before = bar.commands();
    rejects([&] { bar.set_commands(std::make_shared<CommandSet>(roots)); });
    require(bar.commands() == before && bar.retained_children().size() == 64, "Rejected replacement preserves the model");
    roots.resize(1);
    for (CommandId id = 2; id <= 4096; ++id) roots.push_back({id, 1, L"Child"});
    bar.set_commands(std::make_shared<CommandSet>(std::move(roots)));
    require(bar.retained_children().size() == 1 && !bar.command_button(2), "Only roots create retained buttons");
    bar.set_commands(std::make_shared<CommandSet>(std::vector<CommandRecord>{}));
    require(bar.retained_children().empty() && !bar.current() && !bar.expanded(), "Empty source clears headings and state");
}
void headings_and_navigation() {
    int actions{};
    MenuBar bar(L"Main menu");
    const auto set = source(actions);
    bar.set_commands(set);
    require(bar.role() == ControlRole::content_view && !bar.tab_stop() && !bar.focusable(), "The bar is not another tab stop");
    require(bar.commands() == set && bar.retained_children().size() == 5, "Menu bar retains its immutable command set");
    auto file = bar.heading(1);
    require(file == bar.command_button(1) && file->owner_id() == bar.id() && file->command_id() == 1,
        "Heading exposes stable owner and command identities");
    require(file->current_submenu() == set->find(1), "Heading resolves the current submenu record");
    require(file->appearance() == ButtonAppearance::subtle && file->behavior() == ButtonBehavior::momentary,
        "Headings are subtle buttons, not independently checked commands");
    require(file->display_label() == L"File" && file->mnemonic() == L'F', "Mnemonic marker is not displayed");
    require(bar.heading(4)->display_label() == L"Tools & More" && bar.heading(4)->mnemonic() == L'M',
        "Escaped ampersands remain literal");
    require(bar.heading(5)->display_label() == L"Exit&" && bar.heading(5)->mnemonic() == L'X', "Trailing ampersand is literal");
    require(file->selected() && file->tab_stop() && !file->expanded(), "First enabled root owns the initial tab stop");
    require(bar.adjacent(1, 1) == bar.heading(3) && bar.adjacent(1, -1) == bar.heading(5), "Arrows skip disabled roots and wrap");
    require(bar.mnemonic_heading(L'e') == bar.heading(3) && !bar.mnemonic_heading(L'd'), "Mnemonics fold case and reject disabled roots");
    require(!bar.set_current(2) && !bar.set_current(10) && bar.set_current(3), "Current heading must be an enabled root");
    require(!file->selected() && !file->tab_stop() && bar.heading(3)->tab_stop(), "Roving navigation retains one tab stop");
    bar.set_expanded(3);
    require(bar.heading(3)->expanded() && bar.heading(3)->checked() && bar.expanded() == 3, "Expansion is available to presentation and UIA");
    bar.set_expanded(3);
    rejects([&] { bar.set_expanded(2); });
    require(bar.expanded() == 3, "Rejected expansion preserves current popup state");
    bar.set_current(4);
    require(!bar.expanded() && !bar.heading(3)->expanded(), "Switching headings cancels the previous expansion");
    bar.heading(4)->set_enabled(false);
    bar.arrange({10, 20, 400, 40});
    require(bar.current() == 1 && file->tab_stop(), "Layout repairs a directly disabled current heading");
    std::size_t stops{};
    float edge = 10;
    for (const auto& child : bar.retained_children()) {
        const auto button = std::static_pointer_cast<Button>(child);
        stops += button->tab_stop();
        require(child->bounds().x >= edge && child->bounds().x + child->bounds().width <= 410.01f, "Heading layout stays in the bar");
        edge = child->bounds().x + child->bounds().width;
    }
    require(stops == 1 && bar.measure({400, 100}).height >= 40, "One tab stop and a usable measured height");
    bar.set_expanded(1);
    bar.set_enabled(false);
    require(!bar.expanded() && !bar.adjacent(1, 1), "Disabling the bar cancels expansion and navigation");
    bar.set_enabled(true);
    require(bar.adjacent(1, 1) == bar.heading(3), "Reenabling restores navigation without rebuilding commands");
    bar.set_preferred_size({200, 52});
    const auto preferred = bar.measure({400, 100});
    require(preferred.width == 200 && preferred.height == 52, "Explicit preferred size overrides natural heading measurement");
    bar.set_visible(false);
    const auto hidden = bar.measure({400, 100});
    require(hidden.width == 0 && hidden.height == 0, "Hidden menu bar has no desired size");
}
void lifetime_and_replacement() {
    int actions{}, opened{};
    auto bar = std::make_unique<MenuBar>();
    auto set = source(actions);
    bar->set_commands(set);
    bar->set_open_handler([&](CommandId id) { opened = static_cast<int>(id); });
    auto file = bar->heading(1);
    require(file->invoke() && opened == 1 && actions == 0, "Heading requests a popup, never a descendant action");
    auto previous = file->click_callback();
    bar->set_expanded(1);
    bar->set_commands(source(actions, false));
    require(bar->heading(1) == file && !file->enabled() && !bar->expanded() && bar->current() == 3,
        "Replacement preserves heading identity, repairs focus, and cancels expansion");
    opened = 0;
    previous();
    require(!file->invoke() && opened == 0, "A copied old callback cannot open a replaced or disabled root");
    bar->set_commands(source(actions));
    auto current = file->click_callback();
    previous();
    require(opened == 0, "Reenabling does not revive an old snapshot callback");
    current();
    require(opened == 1 && actions == 0, "Current callback uses the replacement snapshot");
    bar->set_commands(std::make_shared<CommandSet>(std::vector{group(3, L"&Edit")}));
    opened = 0;
    current();
    require(!file->enabled() && !file->current_submenu() && !file->selected() && !file->expanded() && opened == 0,
        "Removed headings and saved callbacks are retired");
    auto edit = bar->heading(3);
    auto escaped = edit->click_callback();
    bar.reset();
    escaped();
    require(!edit->current_submenu() && !edit->enabled() && opened == 0, "Escaped headings never retain or call a destroyed owner");
    bar = std::make_unique<MenuBar>();
    bar->set_commands(set);
    file = bar->heading(1);
    bar->set_open_handler([&](CommandId) { bar.reset(); });
    require(file->invoke() && !bar && !file->current_submenu(), "Host callback can destroy the bar safely");
}
void shared_menu_semantics() {
    int actions{};
    MenuBar bar;
    auto set = source(actions);
    bar.set_commands(set);
    CommandSurface surface(L"File", false);
    surface.set_commands(bar.commands(), 1);
    require(!surface.editor() && surface.menu()->parent() == 1, "Heading reuses a nonsearchable command surface");
    require(surface.menu()->execute(10) && actions == 1, "Children keep existing action semantics");
    CommandId nested{};
    surface.menu()->on_submenu([&](CommandId id) { nested = id; });
    require(surface.menu()->execute(11) && nested == 11, "Nested menus retain their original identities");
    surface.set_commands(set, 11);
    require(surface.menu()->execute(12) && actions == 2, "Nested actions use the shared command snapshot");
    surface.set_commands(set, 2);
    require(!surface.menu()->execute(20), "Disabled root blocks descendant invocation");
}
}
int main() {
    try {
        validation(); headings_and_navigation(); lifetime_and_replacement(); shared_menu_semantics();
        std::cout << checks << " menu bar checks passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
