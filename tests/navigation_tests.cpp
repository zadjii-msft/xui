#include "xui/navigation.hpp"
#include "xui/shell_commands.hpp"
#include "xui/titlebar.hpp"
#include <iostream>

namespace {
using namespace xui;
int checks{};
void require(bool value, const char* text) { ++checks; if (!value) throw std::runtime_error(text); }
template<class F> void rejects(F&& callback) { bool threw{}; try { callback(); } catch (const std::exception&) { threw = true; } require(threw, "Invalid input must fail explicitly"); }
std::shared_ptr<const CommandSet> commands(int& primary, int& pin) {
    return std::make_shared<CommandSet>(std::vector<CommandRecord>{
        {1, 0, L"Alpha", [&] { ++primary; }, true, true, ButtonIcon::up, {L"Ctrl+A", L"Enter"}, L"Pin", [&] { ++pin; }},
        {2, 0, L"Disabled", [&] { ++primary; }, false},
        {3, 0, L"", {}, true, {}, ButtonIcon::none, {}, L"", {}, CommandKind::separator},
        {4, 0, L"More", {}, true, {}, ButtonIcon::none, {}, L"", {}, CommandKind::submenu},
        {5, 4, L"Nested", [&] { ++primary; }},
        {6, 0, L"Omega", [&] { ++primary; }}});
}
struct Shell final : ShellCommandProvider {
    int discoveries{}, invocations{}; bool fail{}, disabled_parent{}; std::stop_token last;
    std::function<void()> during;
    std::vector<ShellCommandInfo> discover(std::stop_token token) override {
        ++discoveries; last = token; if (during) during();
        if (fail) throw std::runtime_error("Provider failure");
        return {{{1, 9}, L"Inspect", L"inspect", true, true, false, false, {}, ButtonIcon::up, {L"Ctrl+I", L"Enter"}},
            {{2, 9}, L"Disabled", L"disabled", false},
            {{3, 9}, L"More", L"", !disabled_parent, false, false, true, {{{4, 9}, L"Child", L"child"}}}};
    }
    void invoke(ItemKey key) override { require(key == ItemKey{1, 9} || key == ItemKey{4, 9}, "Exact Shell identity"); ++invocations; }
};
void shell_contracts() {
    auto provider = std::make_shared<Shell>(); ShellCommandSession session(provider);
    session.discover(); require(provider->discoveries == 1 && provider->invocations == 0, "Discovery never runs verbs");
    require(session.commands()[0].checked && session.commands()[2].children[0].verb == L"child", "Shell properties and submenus");
    require(session.commands()[0].icon == ButtonIcon::up && session.commands()[0].shortcut_hints.size() == 2, "Synthetic Shell icon and multiple shortcut hints");
    require(!session.invoke({2, 9}) && !session.invoke({1, 8}) && !session.invoke({3, 9}), "Disabled, stale and native-only Shell actions rejected");
    require(session.invoke({4, 9}) && provider->invocations == 1 && provider->last.stop_requested(), "Exact explicit Shell invocation cancels session");
    require(!session.invoke({1, 9}), "Closed Shell session cannot invoke");
    provider->disabled_parent = true; session.discover();
    require(!session.invoke({4, 9}), "Disabled Shell parent blocks its child");
    provider->disabled_parent = false;
    provider->during = [&] { require(!session.invoke({1, 9}), "Discovery cannot invoke previous snapshot"); };
    session.discover();
    provider->during = [&] { session.cancel(); }; session.discover();
    require(session.commands().empty(), "Reentrant Shell cancellation rejects result");
    provider->during = {}; provider->fail = true; rejects([&] { session.discover(); });
    require(session.commands().empty() && provider->last.stop_requested(), "Shell failure revokes actions");
    provider->fail = false;
    auto owned = std::make_unique<ShellCommandSession>(provider);
    provider->during = [&] { owned.reset(); }; owned->discover();
    require(!owned && provider->last.stop_requested(), "Provider can delete its session during discovery");
    provider->during = {};
}
void command_contracts() {
    int primary{}, pin{}; auto set = commands(primary, pin);
    CommandBindings bindings;
    require(!bindings.invoke(*set, {0x41, true}), "Shortcut hints never register bindings");
    bindings.bind({0x41, true}, 1); require(bindings.invoke(*set, {0x41, true}) && primary == 1, "Explicit shortcut invokes shared record");
    primary = 0; rejects([&] { bindings.bind({0x41, true}, 6); });
    CommandMenu menu; int changes{}; menu.on_selection([&] { ++changes; });
    menu.set_commands(set); menu.arrange({0, 0, 400, 300});
    require(changes == 0, "Command source property changes do not raise selection callbacks");
    require(menu.selection().focused() == ItemKey{1, 1}, "Initial deterministic focus");
    menu.step(1); require(menu.selection().focused() == ItemKey{4, 1}, "Skip separators and disabled actions");
    menu.step(1); require(menu.selection().focused() == ItemKey{6, 1}, "Next enabled command");
    menu.set_commands(set, 0, L"Omega"); require(menu.selection().focused() == ItemKey{6, 1}, "Filter preserves stable focus");
    menu.set_commands(set, 0, L"Alpha"); require(menu.selection().focused() == ItemKey{1, 1}, "Filter repairs missing focus");
    menu.set_commands(set, 0, L"none"); require(!menu.selection().focused(), "Empty filter has no focus");
    menu.set_commands(set); require(menu.execute(1, true) && pin == 1 && primary == 0, "Pin never invokes primary");
    require(!menu.execute(2) && !menu.execute(3) && !menu.execute(5), "Invisible and disabled commands cannot execute");
    CommandId submenu{}; menu.on_submenu([&](CommandId id) { submenu = id; });
    menu.execute(4); require(submenu == 4, "Submenu identity");
    menu.set_expanded(4);
    require(menu.source()->hierarchy(*menu.source()->find({4, 1})).expanded, "Expanded submenu snapshot");
    require(menu.visible_content()[3].expandable && menu.visible_content()[3].content.submenu,
        "Visible submenu rows carry disclosure rendering metadata");
    menu.on_collapse([&](CommandId id) { require(id == 4, "Collapse preserves submenu identity"); menu.set_expanded({}); });
    require(menu.disclose({4, 1}, false) && !menu.expanded(), "Collapse clears only selected submenu");
    bool closed{}; menu.on_accept([&] { closed = true; menu.set_commands(set, 0, L"none"); });
    require(menu.execute(1) && primary == 1 && closed, "Reentrant close preserves copied action");
    auto owned = std::make_unique<CommandMenu>(); owned->set_commands(set);
    owned->on_accept([&] { owned.reset(); });
    require(owned->execute(1) && !owned && primary == 2, "Owner deletion during acceptance is safe");
    CommandSurface surface; surface.set_commands(set);
    CommandQuery pending; surface.on_query([&](CommandQuery query) { pending = query; });
    auto first = surface.request(L"a"); auto second = surface.request(L"b");
    require(first.cancellation.stop_requested() && !surface.complete(first, set), "Superseded query canceled");
    require(surface.menu()->source()->size() == 5, "Old rows remain visible during pending query");
    CommandSurface foreign; foreign.set_commands(set); foreign.request(L"b");
    require(!foreign.complete(second, set), "Owner-specific query token");
    surface.cancel(); require(second.cancellation.stop_requested() && !surface.complete(second, set), "Closed surface rejects late results");
    auto third = surface.request(L"c"); require(surface.complete(third, {}, L"Provider error") && surface.error() == L"Provider error", "Provider errors preserve old rows");
    require(!surface.complete(third, set), "Query completion is consumed once");
    auto replaced = surface.request(L"replace"); surface.set_commands(set);
    require(replaced.cancellation.stop_requested() && !surface.complete(replaced, set), "Direct command replacement revokes pending query");
    surface.on_query([&](CommandQuery request) { pending = request; throw std::runtime_error("Query failure"); });
    rejects([&] { surface.request(L"failure"); }); require(pending.cancellation.stop_requested(), "Throwing query cancels its work");
    auto deleted = std::make_unique<CommandSurface>();
    deleted->on_query([&](CommandQuery request) { pending = request; deleted.reset(); });
    deleted->request(L"destroy"); require(!deleted && pending.cancellation.stop_requested(), "Query owner can delete itself");
    rejects([&] { surface.request(std::wstring(257, L'x')); });
    rejects([] { CommandSet duplicate({{1, 0, L"A"}, {1, 0, L"B"}}); });
    rejects([] { CommandSet cycle({{1, 1, L"Cycle", {}, true, {}, ButtonIcon::none, {}, L"", {}, CommandKind::submenu}}); });
    rejects([] { CommandSet invalid({{1, 0, L"Pin", {}, true, {}, ButtonIcon::none, {}, L"Pin"}}); });
    CommandSet disabled_parent({{1, 0, L"Disabled group", {}, false, {}, ButtonIcon::none, {}, L"", {}, CommandKind::submenu},
        {2, 1, L"Child", [&] { ++primary; }}});
    require(!disabled_parent.enabled(2) && !disabled_parent.invoke(2), "Disabled submenu blocks direct and shortcut invocation");
    std::vector<CommandRecord> many;
    for (CommandId i = 1; i <= 4096; ++i) many.push_back({i, 0, L"Bounded"});
    auto bounded = std::make_shared<CommandSet>(many); menu.set_commands(bounded);
    require(menu.visible_content().size() <= 8, "Only visible command rows are materialized");
    many.push_back({4097, 0, L"Too many"}); rejects([&] { CommandSet over(many); });
    CommandBar bar; bar.set_commands(std::make_shared<CommandSet>(std::vector<CommandRecord>{{1, 0, L"A"}, {2, 0, L"B"}, {3, 0, L"C"}}));
    bar.arrange({0, 0, 240, 40}); require(bar.overflow_commands()->records().size() == 2, "Toolbar overflow retains hidden command records");
}
class Rows final : public ItemsSource {
public:
    std::size_t size() const override { return 1000000; }
    ItemKey key(std::size_t index) const override { return {index + 1, 1}; }
    std::optional<std::size_t> find(ItemKey key) const override { return key.version == 1 && key.id && key.id <= size() ? std::optional<std::size_t>{key.id - 1} : std::nullopt; }
    ItemContent item(std::size_t i) const override { return {L"Location " + std::to_wstring(i), L"Cached", ButtonIcon::up, 0.5}; }
};
void navigation_contracts() {
    Breadcrumb breadcrumb; breadcrumb.set_segments({{{1, 1}, L"Home"}, {{2, 1}, L"Work"}, {{3, 1}, L"Current"}});
    breadcrumb.arrange({0, 0, 160, 38}); require(breadcrumb.current() == ItemKey{3, 1} && breadcrumb.overflow_commands()->records().size() == 2, "Breadcrumb overflow preserves current segment");
    ItemKey requested{}; breadcrumb.on_navigate([&](ItemKey key) { requested = key; });
    breadcrumb.overflow_commands()->invoke(1); require(requested == ItemKey{1, 1} && breadcrumb.current() == ItemKey{3, 1}, "Navigation requests are not property mutation");
    rejects([&] { breadcrumb.set_segments({{{1, 1}, L"A"}, {{1, 1}, L"B"}}); });
    auto rows = std::make_shared<Rows>(); NavigationPane pane; pane.set_items(rows); pane.arrange({0, 0, 320, 280});
    pane.items()->select({25, 1});
    auto request = pane.request(L"pending");
    require(pane.items()->selection().contains({25, 1}), "Query retains selection");
    pane.cancel(); require(!pane.complete(request, rows) && request.cancellation.stop_requested(), "Navigation cancellation rejects late result");
    auto next = pane.request(L"new"); require(pane.complete(next, rows), "Navigation completion");
    require(!pane.complete(next, rows), "Navigation completion is consumed once");
    auto replaced = pane.request(L"replace"); pane.set_items(rows);
    require(replaced.cancellation.stop_requested() && !pane.complete(replaced, rows), "Direct navigation replacement revokes pending query");
    pane.group()->set_expanded(false); pane.arrange({0, 0, 320, 280});
    require(pane.items()->bounds().height == 0 && pane.items()->selection().contains({25, 1}), "Collapse preserves selection without visible rows");
    auto items = std::make_shared<ItemsView>(); items->set_items(rows); ViewPicker picker(items);
    picker.choices()->select(2); require(items->presentation() == ItemsPresentation::tiles, "View picker changes actual presentation");
    picker.size()->move(RangeKey::increase); require(items->item_size().height == picker.size()->value(), "Vertical range changes actual row size");
    TitleBar caption(L"Honest title"); caption.tabs()->set_tabs({{1, L"Document"}}, 1); caption.arrange({0, 0, 800, 44});
    require(caption.hit_test({10, 20}) == CaptionHit::drag && caption.hit_test({790, 20}) == CaptionHit::close, "Caption regions");
    const auto b = caption.tabs()->bounds(); require(caption.hit_test({b.x + 10, 20}) == CaptionHit::client, "Tab interaction never drags window");
    int actions{}; caption.on_caption([&](CaptionAction action) { if (action == CaptionAction::maximize_restore) ++actions; });
    caption.maximize()->invoke(); require(actions == 1, "Accessible caption activation boundary");
}
}
int main() {
    try { shell_contracts(); command_contracts(); navigation_contracts(); std::cout << checks << " navigation contracts passed\n"; return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
