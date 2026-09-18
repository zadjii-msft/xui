#include "../demo/explorer_state.hpp"
#include "../demo/directory.hpp"
#include "../demo/shell_dispatch.hpp"
#include "xui/controls.hpp"
#include "xui/file_list.hpp"
#include <windows.h>
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>
#include "environment_fixture.hpp"

using namespace xui;
using namespace xui::explorer;
namespace {
int checks{};
void require(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
void state_tests() {
    PaneState pane(L"C:\\one"), other(L"D:\\other");
    const auto original = pane.active().id;
    pane.active().history.current().query = L"first";
    pane.active().history.current().selected_path = L"C:\\one\\file.txt";
    pane.active().history.current().offset = 320;
    auto request = pane.begin(Location{L"C:\\two"});
    require(pane.active().history.current().path == L"C:\\one", "Uncommitted navigation preserves current path");
    require(pane.finish(request.serial, false), "Failure is acknowledged");
    require(pane.active().history.size() == 1 && pane.active().history.current().query == L"first", "Failure preserves history and query");
    request = pane.begin(Location{L"C:\\two"});
    pane.set_query(L"new filter");
    require(pane.finish(request.serial, true), "Navigation commits");
    require(pane.active().history.current().query == L"new filter", "Typing during navigation updates pending query");
    auto previous = pane.active().history.relative(-1);
    require(previous.has_value(), "Back enabled after successful navigation");
    request = pane.begin(pane.active().history.at(*previous), previous);
    pane.finish(request.serial, true);
    require(pane.active().history.current().selected_path == L"C:\\one\\file.txt" &&
        pane.active().history.current().offset == 320, "Back restores selected path and scroll");
    require(pane.active().history.relative(1).has_value(), "Back enables forward");
    request = pane.begin(Location{L"C:\\three"});
    pane.finish(request.serial, true);
    require(!pane.active().history.relative(1), "New navigation truncates forward history");
    const auto delayed = pane.begin(Location{L"C:\\delayed"});
    const auto second = pane.add(L"C:\\tab");
    require(!pane.finish(delayed.serial, true), "New tab rejects old completion");
    require(pane.active().id == second, "New tab becomes active");
    pane.select(original);
    require(pane.active().history.current().path == L"C:\\three", "Tab restores its own path");
    const auto switch_request = pane.begin(Location{L"C:\\stale"});
    pane.select(second);
    require(!pane.finish(switch_request.serial, true), "Tab switch revokes completion");
    request = pane.begin(Location{L"C:\\closing"});
    pane.close(second);
    require(!pane.finish(request.serial, true), "Tab close revokes completion");
    require(pane.active().id == original && !pane.close(original), "Last tab stays valid");
    require(other.active().history.current().path == L"D:\\other", "Other pane remains independent");
    request = pane.begin(Location{L"C:\\cancel"});
    pane.cancel();
    require(!pane.finish(request.serial, true), "Cancellation rejects delivery");
    for (int i = 0; i < 150; ++i) {
        request = pane.begin(Location{L"C:\\folder" + std::to_wstring(i)});
        pane.finish(request.serial, true);
    }
    require(pane.active().history.size() == History::maximum_entries, "History storage is bounded");
    for (int i = 1; i < 16; ++i) pane.add(L"C:\\extra");
    bool rejected{};
    try { pane.add(L"C:\\too-many"); } catch (const std::length_error&) { rejected = true; }
    require(rejected && pane.tabs().size() == 16, "Tab storage is bounded");
    require(parent_location(L"C:\\") == L"C:\\", "Drive root stays at root");
    require(parent_location(L"\\\\?\\C:\\") == L"\\\\?\\C:\\", "Extended drive root stays at root");
    require(parent_location(L"C:\\folder\\") == L"C:\\", "Trailing separator parent");
    require(parent_location(L"\\\\server\\share\\") == L"\\\\server\\share\\", "UNC share stays at root");
    require(parent_location(L"\\\\?\\UNC\\server\\share\\") == L"\\\\?\\UNC\\server\\share\\", "Extended UNC share stays at root");
    require(resolve_location(L"..\\child", L"C:\\base\\folder") == L"C:\\base\\child", "Relative address resolves against current folder");
    require(resolve_location(L"\"C:\\Unicode 日本\"", L"D:\\") == L"C:\\Unicode 日本", "Quoted Unicode address");
    EnvironmentFixture absolute(L"C:\\Unicode 日本\\with spaces"), relative(L"..\\child"), missing(nullptr);
    require(resolve_location(L"\"" + absolute.reference() + L"\\child\"", L"D:\\") ==
        L"C:\\Unicode 日本\\with spaces\\child", "Quoted Unicode environment reference and suffix");
    require(resolve_location(relative.reference(), L"C:\\base\\folder") == L"C:\\base\\child",
        "Expanded relative path uses the tab base");
    require(resolve_location(L"%SystemRoot%\\System32", L"D:\\") ==
        resolve_location(expand_path_input(L"%SystemRoot%").text + L"\\System32", L"D:\\"),
        "SystemRoot address expands");
    require(expand_path_input(L"%USERPROFILE%").error.empty(), "USERPROFILE expands");
    for (const auto& input : {missing.reference(), std::wstring(L"%unfinished"),
        std::wstring(32767, L'a'), std::wstring(L"a\0b", 3)}) {
        bool invalid{};
        try { resolve_location(input, L"C:\\base"); } catch (const PathInputError&) { invalid = true; }
        require(invalid, "Invalid environment input is explicit and nonfatal");
    }
    require(resolve_location(L"100% complete", L"C:\\base") == L"C:\\base\\100% complete" &&
        resolve_location(L"tail%", L"C:\\base") == L"C:\\base\\tail%", "Ordinary unpaired literal percents remain valid");
    EnvironmentFixture nested(missing.reference().c_str());
    require(expand_path_input(nested.reference()).text == missing.reference(), "Expansion is one pass, not recursive");
    EnvironmentFixture long_value(std::wstring(32760, L'a').c_str());
    require(expand_path_input(long_value.reference() + L"123456").text.size() == 32766, "Maximum expanded path includes terminator");
    require(!expand_path_input(long_value.reference() + L"1234567").error.empty(), "Expanded output cannot exceed Windows bound");
}
void control_tests() {
    TabStrip tabs;
    const TabColors custom{0x123456, 0, 0xffffff, 0x234567, 0xeeeeee, 0x345678, 0x456789};
    tabs.set_colors(custom);
    require(tabs.colors() == custom, "Tab color overrides preserve black and all authored channels");
    bool invalid_colors{};
    try { auto invalid = custom; invalid.border = 0xff123456; tabs.set_colors(invalid); }
    catch (const std::invalid_argument&) { invalid_colors = true; }
    require(invalid_colors && tabs.colors() == custom, "Invalid tab colors are rejected transactionally");
    tabs.set_colors({});
    require(tabs.colors() == TabColors{}, "Clearing overrides restores theme-derived tab colors");
    tabs.arrange({0, 0, 420, 38});
    int selected{}, closed{};
    tabs.on_select([&](auto) { ++selected; });
    tabs.on_close([&](auto) { ++closed; });
    tabs.set_tabs({{1, L"First"}, {2, L"Second"}, {3, L"Third"}, {4, L"Fourth"}}, 1);
    const auto original_tabs = tabs.tabs();
    const auto original_close = tabs.close_bounds(0), original_tab = tabs.tab_bounds(0);
    auto visual_tabs = original_tabs;
    visual_tabs[0].icon = ButtonIcon::folder;
    visual_tabs[0].image_path = L"C:\\Unicode 日本";
    tabs.set_tabs(visual_tabs, 1);
    require(tabs.tabs() == visual_tabs && tabs.selected() == 1 && selected == 0 &&
        tabs.tab_bounds(0).width == original_tab.width && tabs.close_bounds(0).x == original_close.x,
        "Optional tab visuals preserve identities, selection, callbacks, and hit geometry");
    for (auto invalid : {TabItem{1, L"First", static_cast<ButtonIcon>(999)},
        TabItem{1, L"First", ButtonIcon::folder, std::wstring(32768, L'x')},
        TabItem{1, L"First", ButtonIcon::folder, std::wstring(L"a\0b", 3)}}) {
        bool rejected{};
        try { tabs.set_tabs({invalid}, 1); } catch (const std::invalid_argument&) { rejected = true; }
        require(rejected && tabs.tabs() == visual_tabs, "Invalid tab visuals do not replace existing tabs");
    }
    tabs.set_tabs(original_tabs, 1);
    require(tabs.tabs()[0].icon == ButtonIcon::none && tabs.tabs()[0].image_path.empty(),
        "Existing two-field TabItem construction restores text-only tabs");
    const auto first = tabs.tab_bounds(0), second = tabs.tab_bounds(1);
    require(tabs.prepare_context_menu(Point{second.x + 8, second.y + 8}) &&
        tabs.context_tab() == 2 && tabs.selected() == 1 && selected == 0,
        "Pointer context menus target the clicked tab without selection or activation");
    require(tabs.prepare_context_menu({}) && tabs.context_tab() == 1,
        "Keyboard context menus target the selected tab");
    require(!tabs.prepare_context_menu(Point{-1, 8}) && !tabs.context_tab(),
        "Empty space cannot retain a previous tab context target");
    tabs.set_enabled(false);
    require(!tabs.prepare_context_menu({}), "Disabled tabs reject context menus");
    tabs.set_enabled(true);
    const auto revision = tabs.tabs_revision();
    require(first.x + first.width == second.x && first.y == 0 && first.height == tabs.bounds().height,
        "Tab slots meet and extend to the content edge");
    for (float height : {24.0f, 38.0f, 41.0f, 56.0f}) {
        tabs.arrange({0, 0, 420, height});
        const auto close = tabs.close_bounds(0);
        require(close.width == 24 && close.y + close.height / 2 == height / 2 &&
            close.x + close.width <= tabs.tab_bounds(0).width, "Close target is centered within the visible tab");
    }
    tabs.arrange({0, 0, 420, 38});
    require(selected == 0, "Tab property updates do not call handlers");
    require(tabs.select(2) && selected == 1, "Tab selection callback");
    tabs.step(-1);
    require(tabs.selected() == 1 && selected == 2, "Tab previous");
    tabs.step(-1);
    require(tabs.selected() == 4 && tabs.tab_bounds(3).width > 0, "Tab overflow reveals active item");
    tabs.request_close(4);
    require(closed == 1, "Close dispatch uses stable identity");
    tabs.set_tabs({{1, L"First"}}, 1);
    require(tabs.tabs_revision() != revision, "Replacing tabs invalidates context-menu snapshots");
    require(!tabs.prepare_context_menu(Point{250, 8}), "The unused strip has no context menu");
    tabs.arrange({0, 0, 47, 38});
    require(tabs.close_bounds(0).width == 0, "Narrow tabs hide the close target");
    tabs.arrange({0, 0, 420, 20});
    require(tabs.close_bounds(0).width == 0, "Short tabs hide the close target");
    tabs.arrange({0, 0, 420, 38});
    require(tabs.close_bounds(1).width == 0, "Removed tabs have no close target");
    require(!tabs.select(4), "Removed tab cannot select reused slot");
    bool duplicate{};
    try { tabs.set_tabs({{1, L"A"}, {1, L"B"}}, 1); } catch (const std::invalid_argument&) { duplicate = true; }
    require(duplicate && tabs.tabs().size() == 1, "Invalid tab update is transactional");
    std::vector<std::wstring> tab_events;
    tabs.on_select([&](auto) { tab_events.push_back(L"select"); });
    tabs.on_activate([&](auto) { tab_events.push_back(L"activate"); });
    tabs.set_tabs({{1, L"First"}, {2, L"Second"}}, 1);
    require(tabs.activate_tab(2) && tab_events == std::vector<std::wstring>{L"select", L"activate"},
        "Pointer activation selects before transferring content focus");
    tab_events.clear();
    require(tabs.activate_tab(2) && tab_events == std::vector<std::wstring>{L"activate"},
        "Clicking the selected tab still activates its content");
    tab_events.clear(); tabs.step(-1);
    require(tab_events == std::vector<std::wstring>{L"select"}, "Arrow selection does not activate content");
    tabs.set_enabled(false); tab_events.clear();
    require(!tabs.activate_tab(1) && tab_events.empty(), "Disabled tabs cannot activate content");
    tabs.set_enabled(true);
    tabs.on_select([&](auto) { tabs.set_tabs({{1, L"First"}}, 1); });
    require(!tabs.activate_tab(2) && tab_events.empty(), "Selection callbacks can revoke a stale activation");
    tabs.on_select({});
    int created{};
    tabs.on_new_tab([&] { ++created; });
    require(!tabs.new_tab_button_visible(), "New tab action is opt-in");
    tabs.request_new_tab();
    require(created == 0, "Hidden new tab action cannot dispatch");
    tabs.set_new_tab_button_visible(true);
    const auto button = std::static_pointer_cast<Button>(tabs.retained_children().front());
    require(button->name() == L"New tab" && button->icon() == ButtonIcon::add && button->focusable(),
        "New tab retains the standard accessible icon button");
    for (float width : {0.0f, 16.0f, 32.0f, 48.0f, 160.0f, 420.0f, 800.0f}) {
        tabs.arrange({10, 20, width, 38});
        tabs.set_tabs({{1, L"First"}, {2, L"Second"}, {3, L"Third"}, {4, L"Fourth"}}, 4);
        const auto action = tabs.new_tab_button_bounds(), last = tabs.tab_bounds(3);
        require(action.x >= 0 && action.width >= 0 && action.x + action.width <= width &&
            action.y + action.height <= tabs.bounds().height, "New tab action stays bounded in tiny and overflow layouts");
        require(last.width == 0 || last.x + last.width == action.x, "New tab follows the rightmost visible tab");
        require(!tabs.hit_test(action.x + action.width / 2), "New tab button is not a selectable tab");
        require(button->bounds().x == tabs.bounds().x + action.x, "Native action geometry follows the strip");
    }
    tabs.set_tabs({}, {});
    require(tabs.new_tab_button_bounds().x == 0, "The empty strip retains its new tab action");
    button->invoke();
    require(created == 1 && !tabs.selected(), "New tab dispatch does not select or create a tab");
    tabs.set_enabled(false); button->invoke();
    tabs.set_enabled(true); tabs.set_new_tab_button_visible(false); button->invoke();
    require(created == 1, "Disabled and hidden new tab actions reject invocation");
    tabs.arrange({0, 0, 420, 38}); tabs.set_tabs({{1, L"First"}}, 1);
    require(tabs.tab_bounds(0).width == 180, "Opt-out restores the original tab geometry");
    auto a = std::make_shared<TextInput>(L"A"), b = std::make_shared<TextInput>(L"B");
    SplitView split(a, b);
    split.arrange({10, 20, 1000, 500});
    require(split.expanded() && a->bounds().width == 495 && b->bounds().width == 495, "Split equal arrangement");
    split.set_ratio(0.9f); split.arrange(split.bounds());
    require(b->bounds().width == SplitView::minimum_pane_width, "Splitter enforces minimum pane width");
    split.arrange({0, 0, 500, 400});
    require(!split.expanded() && a->bounds().width == 500 && b->bounds().width == 0, "Narrow split collapses secondary");
    split.arrange({0, 0, 1000, 400});
    require(split.expanded(), "Widening restores pane");
    split.set_secondary_visible(false); split.arrange(split.bounds());
    require(b->bounds().height == 0, "Explicit collapse removes secondary hit area");
    split.set_secondary_visible(true);
    split.set_primary_visible(false);
    split.arrange({10, 20, 500, 400});
    require(split.expanded() && a->bounds().width == 0 && a->bounds().height == 0 &&
        b->bounds().x == 10 && b->bounds().y == 20 && b->bounds().width == 500 && split.divider().width == 0,
        "A hidden primary pane gives the secondary all available width, including narrow windows");
    split.set_primary_visible(true); split.arrange({10, 20, 1000, 400});
    require(split.expanded() && a->bounds().width > 0 && split.divider().width > 0,
        "Restoring the primary pane restores its divider and saved ratio");
    TextInput address(L"Address");
    address.set_maximum_length(32767);
    address.set_text(std::wstring(2000, L'a'));
    require(address.text().size() == 2000, "Address accepts long paths");
    FileList list;
    list.set_items(std::make_shared<const std::vector<FileItem>>(std::vector<FileItem>{
        {1, L"file", L"C:\\folder\\file", false}, {2, L"folder", L"C:\\folder", true}}));
    list.select(0);
    std::wstring dispatched;
    int activations{}, launches{};
    list.on_activate([&](const FileItem& item, FileActivation) {
        ++activations;
        launch_file(item, [&](const std::wstring& path) { ++launches; dispatched = path; return std::wstring{}; });
    });
    require(list.activate_selected(FileActivation::enter) && activations == 1 && launches == 1 &&
        dispatched == L"C:\\folder\\file", "Enter dispatches one exact literal path");
    list.set_filter(L"folder");
    require(!list.activate_selected() && activations == 1, "Hidden selection never activates");
    list.restore_state(1, 1, 0);
    list.set_filter(L"");
    require(list.model().selected_id() == 1 && list.focused_id() == 1, "Hidden restored identity survives filter clear");
    list.activate_selected(FileActivation::double_click);
    require(activations == 2 && launches == 2, "Double-click shares launch command pipeline");
    list.select(1); list.activate_selected();
    require(activations == 3 && launches == 2, "Folder cannot dispatch as executable");
}
void source_tests() {
    const auto fixture = std::filesystem::current_path() / (L"explorer-fixture-" + std::to_wstring(GetCurrentProcessId()));
    require(std::filesystem::create_directory(fixture), "Create owned fixture");
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); } } cleanup{fixture};
    auto deep = fixture;
    for (int i = 0; i < 4; ++i) { deep /= std::wstring(65, L'日') + std::to_wstring(i); std::filesystem::create_directory(deep); }
    const auto file = deep / L"résumé-🙂.txt";
    { std::ofstream stream(file); require(stream.good(), "Create long Unicode fixture file"); stream << "harmless test fixture"; }
    DirectorySource source(deep);
    auto result = source.scan([] { return false; });
    require(result.source && result.error.empty() && result.source->items()->size() == 1, "Long Unicode enumeration");
    require(result.source->items()->front().path == file.wstring(), "Enumeration keeps exact launch path");
    auto again = source.scan([] { return false; });
    require(again.source->items()->front().id == result.source->items()->front().id, "Refresh preserves stable item identities");
    int launches{};
    const auto launch_error = launch_file(again.source->items()->front(), [&](const auto& path) {
        ++launches;
        require(path == file.wstring() && path.size() > 260, "Launch dispatcher receives exact long Unicode path");
        return std::wstring{};
    });
    require(launch_error.empty() && launches == 1, "One explicit activation calls one injected dispatcher");
    DirectorySource missing(fixture / L"missing");
    result = missing.scan([] { return false; });
    require(!result.source && !result.error.empty(), "Nonexistent location reports an error without an empty success");
    DirectorySource not_folder(file);
    require(!not_folder.scan([] { return false; }).error.empty(), "File address is not a folder");
    require(!source.scan([] { return true; }).source, "Cancellation stops enumeration");
    ViewWorker worker([&](const CancelCheck& cancel) { return missing.scan(cancel); }, [] {});
    worker.start();
    const auto generation = worker.request(L"", true);
    std::optional<ViewResult> error;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!error && std::chrono::steady_clock::now() < deadline) { error = worker.take_result(); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
    require(error && error->generation == generation && !error->view &&
        error->error.find(L"Cannot read folder") != std::wstring::npos, "Async scan preserves specific source errors");
    worker.request_stop(); worker.join();
    std::atomic<int> scans{};
    auto snapshot = FileSnapshot::build(std::make_shared<const std::vector<FileItem>>(
        std::vector<FileItem>{{7, L"latest", L"C:\\latest", false}}));
    ViewWorker delayed([&](const CancelCheck& cancel) {
        const auto call = ++scans;
        if (call == 1) while (!cancel()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return cancel() ? SourceResult{} : SourceResult{snapshot, {}};
    }, [] {});
    delayed.start();
    delayed.request(L"obsolete", true);
    require([&] {
        const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!scans && std::chrono::steady_clock::now() < limit) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return scans.load() == 1;
    }(), "Delayed scan starts");
    const auto latest = delayed.request(L"latest", true);
    std::optional<ViewResult> final;
    const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!final && std::chrono::steady_clock::now() < limit) { final = delayed.take_result(); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
    require(final && final->generation == latest && final->view && final->view->query() == L"latest",
        "Superseding navigation cancels delayed scan and publishes only the latest generation");
    delayed.request_stop(); delayed.join();
}
}
int main() {
    try {
        state_tests(); control_tests(); source_tests();
        std::cout << "Explorer: " << checks << " assertions passed; Tab object " << sizeof(Tab) <<
            " bytes, Location object " << sizeof(Location) << " bytes (excluding dynamic string storage)\n";
        return 0;
    }
    catch (const std::exception& e) { std::cerr << "Explorer: " << e.what() << '\n'; return 1; }
}
