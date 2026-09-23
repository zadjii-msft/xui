#include "xui/application.hpp"
#include "xui/titlebar.hpp"
#include <windows.h>
#include <commctrl.h>
#include <iostream>
#include <atomic>
#include <chrono>
#include <thread>

namespace {
using namespace xui;
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
HWND peer(HWND host) {
    HWND result{};
    EnumChildWindows(host, [](HWND hwnd, LPARAM value) -> BOOL {
        wchar_t name[100]{};
        GetWindowTextW(hwnd, name, 100);
        if (std::wstring_view(name) == L"Title bar tabs") {
            *reinterpret_cast<HWND*>(value) = hwnd;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&result));
    require(result != nullptr, "Find tab peer");
    return result;
}
struct MoveDriver {
    std::function<void(HWND)> run;
    std::exception_ptr failure;
    int loops{};
    static LRESULT CALLBACK procedure(HWND hwnd, UINT message, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data) {
        auto& driver = *reinterpret_cast<MoveDriver*>(data);
        if (message == WM_NCLBUTTONDOWN && w == HTCAPTION) {
            ++driver.loops;
            try { driver.run(hwnd); }
            catch (...) { driver.failure = std::current_exception(); }
            return 0;
        }
        return DefSubclassProc(hwnd, message, w, l);
    }
};
void apply_move(HWND hwnd, RECT& rect) {
    BOOL full_drag{};
    require(SystemParametersInfoW(SPI_GETDRAGFULLWINDOWS, 0, &full_drag, 0) != 0, "Read desktop drag mode");
    WINDOWPOS position{hwnd, nullptr, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
        SWP_NOZORDER | SWP_NOACTIVATE};
    if (full_drag) {
        SendMessageW(hwnd, WM_WINDOWPOSCHANGING, 0, reinterpret_cast<LPARAM>(&position));
        rect = {position.x, position.y, position.x + position.cx, position.y + position.cy};
    } else {
        require(SendMessageW(hwnd, WM_MOVING, WMSZ_LEFT, reinterpret_cast<LPARAM>(&rect)) == TRUE,
            "Outline drag handles native WM_MOVING");
        position.x = rect.left; position.y = rect.top;
        position.cx = rect.right - rect.left; position.cy = rect.bottom - rect.top;
    }
    require(SetWindowPos(hwnd, position.hwndInsertAfter, position.x, position.y, position.cx, position.cy,
        position.flags | SWP_NOSENDCHANGING) != 0, "Apply native move flags and rectangle");
}
bool above(HWND first, HWND second) {
    for (HWND hwnd = GetTopWindow(nullptr); hwnd; hwnd = GetWindow(hwnd, GW_HWNDNEXT)) {
        if (hwnd == first) return true;
        if (hwnd == second) return false;
    }
    throw std::runtime_error("Both fixture windows must exist in native Z order");
}
void geometry() {
    TabStrip strip;
    strip.set_tabs({{1, L"One"}, {2, L"Two"}, {3, L"Three"}}, 1);
    strip.arrange({0, 0, 600, 40});
    require(strip.insertion_index(-20) == 0, "Insertion before first tab");
    require(strip.insertion_index(100) == 1, "Insertion uses tab midpoint");
    require(strip.insertion_index(599) == 3, "Insertion after last tab");
    strip.set_drop_indicator(3);
    require(strip.drop_indicator() == 3, "End marker accepted");
    bool rejected{};
    try { strip.set_drop_indicator(4); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected && strip.drop_indicator() == 3, "Bad index preserves marker");
    strip.set_tabs({}, {});
    require(!strip.drop_indicator() && strip.insertion_index(0) == 0, "Empty strip accepts slot zero and clears stale marker");
}
void placement_contracts() {
    Application app;
    auto window = app.create_window({L"XUI maximized placement", {700, 400}, ThemeMode::dark, {}, true});
    auto root = std::make_shared<Stack>(Axis::vertical);
    root->add(std::make_shared<TextInput>(L"Placement content"));
    window->set_content(root);
    window->set_show_activated(false);
    bool rejected{};
    try { window->placement(); } catch (const std::logic_error&) { rejected = true; }
    require(rejected, "Unshown automatic position is not a fake zero rectangle");
    const WindowPlacement original{40, 60, 700, 400, true};
    window->set_placement(original);
    const auto foreground = GetForegroundWindow();
    app.show(*window);
    const auto hwnd = FindWindowW(L"Xui.Window.1", L"XUI maximized placement");
    require(hwnd && IsZoomed(hwnd), "Pre-show maximized state is honored");
    require(GetForegroundWindow() == foreground, "Maximized remainder does not steal native move-loop activation");
    const auto saved = window->placement();
    require(saved.maximized && saved.x == original.x && saved.y == original.y &&
        saved.width == original.width && saved.height == original.height, "Maximize retains restored screen bounds");
    window->set_placement({-120, 80, 720, 420, false});
    const auto restored = window->placement();
    require(!restored.maximized && restored.x == -120 && restored.y == 80 &&
        restored.width == 720 && restored.height == 420, "Placement supports negative physical screen coordinates");
    app.shutdown();
    require(app.run() == 0, "Placement fixture retires");
}
void secondary_only() {
    Application app;
    auto window = app.create_window({L"XUI secondary-only tab drag", {700, 400}, ThemeMode::dark, {}, true});
    window->set_show_activated(false);
    auto first = std::make_shared<TextInput>(L"First native pane");
    auto second = std::make_shared<TextInput>(L"Second native pane");
    auto split = std::make_shared<SplitView>(first, second);
    split->set_primary_visible(false);
    auto root = std::make_shared<Stack>(Axis::vertical);
    root->add(split, 1);
    window->set_content(root);
    window->titlebar()->set_title_visible(false);
    window->titlebar()->set_tab_panes(first, second);
    window->titlebar()->secondary_tabs()->set_visible(true);
    window->titlebar()->secondary_tabs()->set_tabs({{9, L"Detached secondary tab"}}, 9);
    app.show(*window);
    require(first->bounds().width == 0 && second->bounds().width == split->pane_area().width,
        "Secondary native content fills the workspace while its strip identity stays fixed");
    require(window->titlebar()->secondary_tabs()->bounds().x == second->bounds().x,
        "Secondary title tabs follow the full-width pane");
    require(!window->focus(*first) && !window->focus(*split) && window->focus(*second),
        "Hidden pane and divider do not receive native focus; surviving editor does");
    split->set_primary_visible(true);
    const auto hwnd = FindWindowW(L"Xui.Window.1", L"XUI secondary-only tab drag");
    SendMessageW(hwnd, WM_APP + 12, 0, 0);
    require(first->bounds().width > 0 && split->divider().width > 0 && window->focus(*first),
        "Restoring the first pane restores native focus and divider layout");
    app.shutdown();
    require(app.run() == 0, "Secondary-only native fixture retires");
}
void native_loop() {
    Application app;
    POINT cursor{};
    require(GetCursorPos(&cursor) != 0, "Read native-loop pointer");
    auto source = app.create_window({L"XUI real tab move loop", {700, 400}, ThemeMode::dark, {}, true});
    source->set_show_activated(false);
    source->set_placement({cursor.x - 80, cursor.y + 100, 800, 440});
    auto root = std::make_shared<Stack>(Axis::vertical);
    root->add(std::make_shared<TextInput>(L"Move-loop content"));
    source->set_content(root);
    source->titlebar()->set_title_visible(false);
    source->titlebar()->tabs()->set_tabs({{1, L"Dragged"}, {2, L"Remaining"}}, 1);
    std::shared_ptr<Window> remainder;
    HWND hwnd{};
    bool torn{};
    int completed{};
    source->on_tab_drag([&](const TabDragEvent& e) {
        if (e.kind == TabDragKind::tear_out) {
            GUITHREADINFO state{sizeof(state)};
            require(GetGUIThreadInfo(GetCurrentThreadId(), &state) && (state.flags & GUI_INMOVESIZE),
                "Tear-out runs inside the real user32 move-size loop");
            require(state.hwndMoveSize == hwnd && GetCapture() == hwnd, "Original HWND owns the move loop and capture");
            remainder = app.create_window({L"XUI real move remainder", {700, 400}, ThemeMode::dark, {}, true});
            remainder->set_show_activated(false);
            remainder->set_placement(source->placement());
            auto content = std::make_shared<Stack>(Axis::vertical);
            content->add(std::make_shared<Label>(L"Remaining tab"));
            remainder->set_content(content);
            remainder->titlebar()->tabs()->set_tabs({{2, L"Remaining"}}, 2);
            app.show(*remainder);
            source->titlebar()->tabs()->set_tabs({{1, L"Dragged"}}, 1);
            require(GetGUIThreadInfo(GetCurrentThreadId(), &state) && state.hwndMoveSize == hwnd,
                "Remainder creation does not replace the active move-loop HWND");
            torn = true;
            return true;
        }
        if (e.kind == TabDragKind::completed) ++completed;
        return false;
    });
    app.show(*source);
    hwnd = FindWindowW(L"Xui.Window.1", L"XUI real tab move loop");
    require(hwnd != nullptr, "Find native-loop source");
    const auto thread = GetCurrentThreadId();
    std::atomic<bool> done{}, entered{}, delivered{};
    std::jthread driver([&](std::stop_token stop) {
        for (int i = 0; i != 200 && !stop.stop_requested() && !done; ++i) {
            GUITHREADINFO state{sizeof(state)};
            if (GetGUIThreadInfo(thread, &state) && state.hwndMoveSize == hwnd && (state.flags & GUI_INMOVESIZE)) {
                entered = true;
                RECT rect{}; GetWindowRect(hwnd, &rect);
                OffsetRect(&rect, 12, 12);
                DWORD_PTR result{};
                BOOL full_drag{};
                SystemParametersInfoW(SPI_GETDRAGFULLWINDOWS, 0, &full_drag, 0);
                WINDOWPOS position{hwnd, nullptr, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
                    SWP_NOZORDER | SWP_NOACTIVATE};
                delivered = SendMessageTimeoutW(hwnd, full_drag ? WM_WINDOWPOSCHANGING : WM_MOVING,
                    full_drag ? 0 : WMSZ_LEFT, full_drag ? reinterpret_cast<LPARAM>(&position) : reinterpret_cast<LPARAM>(&rect),
                    SMTO_ABORTIFHUNG, 3000, &result) != 0;
                PostMessageW(hwnd, WM_LBUTTONUP, 0, 0);
                for (int wait = 0; wait != 20 && !done && !stop.stop_requested(); ++wait)
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                if (!done) PostMessageW(hwnd, WM_CANCELMODE, 0, 0);
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        PostMessageW(hwnd, WM_CANCELMODE, 0, 0);
    });
    const auto control = peer(hwnd);
    const auto dpi = GetDpiForWindow(hwnd);
    SendMessageW(control, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(MulDiv(40, dpi, 96), MulDiv(20, dpi, 96)));
    SendMessageW(control, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(MulDiv(60, dpi, 96), MulDiv(20, dpi, 96)));
    done = true;
    driver.request_stop();
    driver.join();
    require(source->error().empty(), "Real native-loop callback succeeds");
    require(completed == 1 && IsWindow(hwnd), "Native move request returns once without replacing its HWND");
    if (entered) require(delivered && torn, "The live user32 loop receives and applies tear-out");
    else {
        require(!torn && !remainder, "A refused native move does not tear out a tab");
        std::cout << "  No physical button is held; user32 declined the live move loop. Native request cleanup passed.\n";
    }
    app.shutdown();
    require(app.run() == 0, "Real move-loop fixture retires");
}
void gesture(bool tear, bool merge, bool cancel, bool reject, UINT cancellation = WM_CANCELMODE) {
    std::cout << "Gesture tear=" << tear << " merge=" << merge << " cancel=" << cancel << " reject=" << reject << std::endl;
    Application app;
    POINT cursor{};
    require(GetCursorPos(&cursor) != 0, "Read pointer without synthesizing user input");
    auto source = app.create_window({L"XUI tab drag source", {700, 400}, ThemeMode::dark, {}, true});
    auto target = app.create_window({L"XUI tab drag target", {700, 400}, ThemeMode::dark, {}, true});
    for (auto& window : {source, target}) {
        window->set_show_activated(false);
        auto root = std::make_shared<Stack>(Axis::vertical);
        auto editor = std::make_shared<TextInput>(L"Native content");
        editor->set_text(L"Selection and editor ownership stay with this HWND");
        root->add(editor);
        window->set_content(root);
        window->titlebar()->set_title_visible(false);
    }
    const int dpi = GetDpiForSystem();
    const int offset = MulDiv(24, dpi, 96);
    WindowPlacement original{cursor.x - MulDiv(400, dpi, 96), cursor.y + (tear ? 120 : -offset), 800, 440};
    source->set_placement(original);
    target->set_placement({cursor.x - 100, cursor.y + (merge ? -offset : 600), 800, 440});
    source->titlebar()->tabs()->set_tabs({{1, L"One"}, {2, L"Two"}, {3, L"Three"}}, 1);
    target->titlebar()->tabs()->set_tabs({{7, L"Target"}}, 7);
    target->on_tab_drag([](const TabDragEvent&) { return false; });
    int reordered{}, torn{}, dropped{}, cancelled{}, completed{}, queries{};
    HWND source_hwnd{};
    std::shared_ptr<Window> remainder;
    source->on_tab_drag([&](const TabDragEvent& e) {
        require(e.source_strip == 0 && e.tab_id == 1, "Gesture retains source strip and tab identity");
        if (e.kind == TabDragKind::reorder) {
            require(e.target == source.get() && e.index > 1 && e.index <= 3, "Stable pre-removal reorder slot");
            auto tabs = source->titlebar()->tabs()->tabs();
            auto moved = tabs.front();
            tabs.erase(tabs.begin());
            tabs.insert(tabs.begin() + e.index - 1, moved);
            source->titlebar()->tabs()->set_tabs(std::move(tabs), 1);
            ++reordered;
        } else if (e.kind == TabDragKind::tear_out) {
            require(IsWindow(source_hwnd), "Source HWND survives tear-out request");
            require(source->placement().x == original.x, "Tear-out sees original placement");
            remainder = app.create_window({L"XUI tab drag remainder", {700, 400}, ThemeMode::dark, {}, true});
            remainder->set_show_activated(false);
            remainder->set_placement(source->placement());
            auto root = std::make_shared<Stack>(Axis::vertical);
            root->add(std::make_shared<Label>(L"Remaining content"));
            remainder->set_content(root);
            remainder->titlebar()->tabs()->set_tabs({{2, L"Two"}, {3, L"Three"}}, 2);
            app.show(*remainder);
            source->titlebar()->tabs()->set_tabs({{1, L"One"}}, 1);
            ++torn;
        } else if (e.kind == TabDragKind::query_drop) {
            require(e.target == target.get() && e.target_strip == 0 && e.index <= 1, "Same-application target and slot");
            ++queries;
            return !reject;
        } else if (e.kind == TabDragKind::drop) {
            require(torn == 1 && IsWindow(source_hwnd), "Drop occurs after tear-out on same HWND");
            target->titlebar()->tabs()->set_tabs({{7, L"Target"}, {1, L"One"}}, 1);
            ++dropped;
        } else if (e.kind == TabDragKind::cancel) {
            source->titlebar()->tabs()->set_tabs({{1, L"One"}, {2, L"Two"}, {3, L"Three"}}, 1);
            ++cancelled;
        } else if (e.kind == TabDragKind::completed) ++completed;
        else if (e.kind == TabDragKind::join || e.kind == TabDragKind::leave) return false;
        return true;
    });
    app.show(*target);
    app.show(*source);
    source_hwnd = FindWindowW(L"Xui.Window.1", L"XUI tab drag source");
    require(source_hwnd != nullptr, "Source HWND exists");
    const auto target_hwnd = FindWindowW(L"Xui.Window.1", L"XUI tab drag target");
    require(SetWindowPos(target_hwnd, HWND_TOP, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE) != 0, "Expose owned drop target without activation");
    MoveDriver driver;
    bool target_unoccluded = !merge;
    driver.run = [&](HWND hwnd) {
        if (merge) {
            POINT now{}; GetCursorPos(&now);
            for (HWND top = GetTopWindow(nullptr); top; top = GetWindow(top, GW_HWNDNEXT)) {
                RECT rect{}; GetWindowRect(top, &rect);
                if (top == hwnd || !IsWindowVisible(top) || IsIconic(top) || !PtInRect(&rect, now)) continue;
                target_unoccluded = top == target_hwnd;
                break;
            }
            if (!target_unoccluded)
                std::cout << "  Desktop occludes the target; checking occlusion rejection instead of acceptance\n";
        }
        RECT before{}, proposed{};
        require(GetWindowRect(hwnd, &before) != 0, "Read moving HWND");
        proposed = before;
        OffsetRect(&proposed, 12, 12);
        SendMessageW(hwnd, WM_CANCELMODE, 0, 0);
        SendMessageW(hwnd, WM_ENTERSIZEMOVE, 0, 0);
        apply_move(hwnd, proposed);
        if (!tear) require(EqualRect(&before, &proposed), "Reorder pins native window instead of moving content");
        else {
            require(torn == 1 && IsWindow(hwnd), "Tear-out keeps native move HWND alive");
            const auto remainder_hwnd = FindWindowW(L"Xui.Window.1", L"XUI tab drag remainder");
            require(remainder_hwnd && above(hwnd, remainder_hwnd), "Remaining content stays below the moving HWND");
        }
        if (cancel) SendMessageW(hwnd, cancellation, 0,
            cancellation == WM_CAPTURECHANGED ? reinterpret_cast<LPARAM>(target_hwnd) : 0);
        SendMessageW(hwnd, WM_EXITSIZEMOVE, 0, 0);
    };
    require(SetWindowSubclass(source_hwnd, MoveDriver::procedure, 1, reinterpret_cast<DWORD_PTR>(&driver)) != 0,
        "Install deterministic native move-loop driver");
    const auto tab_peer = peer(source_hwnd);
    for (bool hide : {false, true}) {
        SendMessageW(tab_peer, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(MulDiv(40, dpi, 96), MulDiv(20, dpi, 96)));
        if (hide) source->titlebar()->tabs()->set_visible(false);
        else source->titlebar()->tabs()->set_enabled(false);
        SendMessageW(source_hwnd, WM_APP + 12, 0, 0);
        require(GetCapture() != tab_peer, "Hiding or disabling the strip cancels a pending tab press");
        source->titlebar()->tabs()->set_visible(true);
        source->titlebar()->tabs()->set_enabled(true);
        SendMessageW(source_hwnd, WM_APP + 12, 0, 0);
        SendMessageW(tab_peer, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(MulDiv(60, dpi, 96), MulDiv(20, dpi, 96)));
        require(driver.loops == 0, "A cancelled press does not resume after re-enabling the strip");
    }
    SendMessageW(tab_peer, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(MulDiv(40, dpi, 96), MulDiv(20, dpi, 96)));
    require(driver.loops == 0, "Click alone does not enter native move loop");
    SendMessageW(source_hwnd, WM_APP + 12, 0, 0);
    require(GetCapture() == tab_peer, "Queued redraw preserves the pending tab drag until the pointer crosses the threshold");
    SendMessageW(tab_peer, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(MulDiv(60, dpi, 96), MulDiv(20, dpi, 96)));
    RemoveWindowSubclass(source_hwnd, MoveDriver::procedure, 1);
    if (driver.failure) std::rethrow_exception(driver.failure);
    require(source->error().empty(), "Native drag callback succeeds");
    require(driver.loops == 1 && completed == 1, "One native loop and one completion per gesture");
    require(torn == (tear ? 1 : 0) && reordered == (tear ? 0 : 1), "Expected drag transition");
    require(cancelled == (cancel ? 1 : 0), "Cancellation is distinct from drop");
    std::cout << "  reordered=" << reordered << " torn=" << torn << " queries=" << queries << " dropped=" << dropped << std::endl;
    require(dropped == (merge && target_unoccluded && !cancel && !reject ? 1 : 0), "Only accepted, released drops merge");
    require(!target->titlebar()->tabs()->drop_indicator(), "Drop marker is cleared after every exit");
    if (merge && target_unoccluded) require(queries > 0, "Target gets non-mutating acceptance query");
    if (!target_unoccluded) require(queries == 0, "Occluded targets do not receive a query");
    if (cancel) require(source->titlebar()->tabs()->tabs().size() == 3 &&
        source->placement().x == original.x && source->placement().y == original.y, "Cancel restores data and placement");
    app.shutdown();
    require(app.run() == 0, "All windows retire without callback failures");
}
void hover_join(bool cancel, bool reject_drop, bool maximized_remainder, bool disable_target = false,
    bool single = false) {
    BOOL full_drag{};
    require(SystemParametersInfoW(SPI_GETDRAGFULLWINDOWS, 0, &full_drag, 0) != 0, "Read hover drag mode");
    if (!full_drag) {
        std::cout << "  Outline-only desktop: live hover uses the release-only fallback\n";
        return;
    }
    Application app;
    POINT point{};
    require(GetCursorPos(&point) != 0, "Read stationary hover fixture pointer");
    const int dpi = GetDpiForSystem();
    auto source = app.create_window({L"XUI hover source", {700, 400}, ThemeMode::dark, {}, true});
    auto target = app.create_window({L"XUI hover target", {700, 400}, ThemeMode::dark, {}, true});
    for (const auto& window : {source, target}) {
        window->set_show_activated(false);
        auto root = std::make_shared<Stack>(Axis::vertical);
        auto editor = std::make_shared<TextInput>(L"Stable native owner");
        if (single && window == source) {
            auto second = std::make_shared<TextInput>(L"Dormant pane");
            auto split = std::make_shared<SplitView>(editor, second);
            split->set_secondary_visible(false);
            root->add(split, 1);
            window->titlebar()->set_tab_panes(editor, second);
            window->titlebar()->secondary_tabs()->set_visible(true);
            window->titlebar()->secondary_tabs()->set_enabled(false);
            window->titlebar()->secondary_tabs()->set_tabs({{3, L"Dormant tab"}}, 3);
        } else root->add(editor);
        window->set_content(root);
        window->titlebar()->tabs()->on_activate([weak = std::weak_ptr<Window>(window), editor](std::uint64_t) {
            if (const auto host = weak.lock()) require(host->focus(*editor), "Tab activation focuses its native content");
        });
        window->titlebar()->set_title_visible(false);
        window->set_placement({point.x - 100, point.y + 150, 800, 440});
    }
    if (single) {
        source->titlebar()->tabs()->set_tabs({{1, L"Dragged"}}, 1);
        source->set_placement({point.x - 100, point.y - MulDiv(24, dpi, 96), 800, 440});
    } else source->titlebar()->tabs()->set_tabs({{1, L"Dragged"}, {2, L"Remaining"}}, 1);
    target->titlebar()->tabs()->set_tabs({{7, L"Destination"}}, 7);
    target->on_tab_drag([](const auto&) { return false; });
    std::shared_ptr<Window> remainder;
    HWND source_hwnd{}, remainder_hwnd{};
    int tears{}, joins{}, leaves{}, drops{}, cancellations{}, completions{};
    source->on_tab_drag([&](const TabDragEvent& event) {
        require(event.source_strip == 0 && event.tab_id == 1, "Hosted tab keeps its gesture identity");
        switch (event.kind) {
        case TabDragKind::tear_out: {
            ++tears;
            if (single) return true;
            remainder = app.create_window({L"XUI hover remainder", {700, 400}, ThemeMode::dark, {}, true});
            remainder->set_show_activated(false);
            auto placement = source->placement();
            placement.maximized = maximized_remainder;
            remainder->set_placement(placement);
            auto root = std::make_shared<Stack>(Axis::vertical);
            root->add(std::make_shared<Label>(L"Remaining model"));
            remainder->set_content(root);
            remainder->titlebar()->tabs()->set_tabs({{2, L"Remaining"}}, 2);
            app.show(*remainder);
            remainder_hwnd = FindWindowW(L"Xui.Window.1", L"XUI hover remainder");
            require(remainder_hwnd && above(source_hwnd, remainder_hwnd),
                "Remainder is below dragged HWND at first show, not just at drag completion");
            require((IsZoomed(remainder_hwnd) != FALSE) == maximized_remainder, "Remainder preserves maximized state");
            source->titlebar()->tabs()->set_tabs({{1, L"Dragged"}}, 1);
            return true;
        }
        case TabDragKind::query_drop:
            require(event.target == target.get() && event.index <= target->titlebar()->tabs()->tabs().size(),
                "Query resolves the visible hover destination");
            return true;
        case TabDragKind::join:
            require(event.target == target.get() && IsWindow(source_hwnd), "Join retains native moving HWND");
            source->titlebar()->tabs()->set_tabs({}, {});
            source->titlebar()->tabs()->set_visible(false);
            target->titlebar()->tabs()->set_tabs({{7, L"Destination"}, {1, L"Dragged"}}, 1);
            ++joins;
            return true;
        case TabDragKind::leave:
            require(event.target == target.get(), "Leave identifies previous host");
            target->titlebar()->tabs()->set_tabs({{7, L"Destination"}}, 7);
            source->titlebar()->tabs()->set_visible(true);
            source->titlebar()->tabs()->set_tabs({{1, L"Dragged"}}, 1);
            ++leaves;
            return true;
        case TabDragKind::drop:
            require(!IsWindowVisible(source_hwnd) && target->titlebar()->tabs()->tabs().size() == 2,
                "Release commits the existing hover transfer without duplicating it");
            ++drops;
            return !reject_drop;
        case TabDragKind::cancel:
            require(source->titlebar()->tabs()->tabs().size() == 1, "Leave precedes rollback");
            if (!single) source->titlebar()->tabs()->set_tabs({{1, L"Dragged"}, {2, L"Remaining"}}, 1);
            ++cancellations;
            return true;
        case TabDragKind::completed:
            ++completions;
            return true;
        default:
            return false;
        }
    });
    app.show(*source);
    app.show(*target);
    source_hwnd = FindWindowW(L"Xui.Window.1", L"XUI hover source");
    const auto target_hwnd = FindWindowW(L"Xui.Window.1", L"XUI hover target");
    require(source_hwnd && target_hwnd, "Find hover fixture HWNDs");
    const auto foreground = GetForegroundWindow();
    require(SetWindowPos(source_hwnd, HWND_TOPMOST, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE) != 0, "Expose fixture without stealing activation");
    MoveDriver driver;
    driver.run = [&](HWND hwnd) {
        const auto check_anchor = [&] {
            RECT band{};
            require(GetWindowRect(peer(hwnd), &band) != 0, "Locate visible dragged tab after native position change");
            require(std::abs(point.x - band.left - MulDiv(40, dpi, 96)) <= 1 &&
                std::abs(point.y - band.top - MulDiv(20, dpi, 96)) <= 1,
                "Detached tab retains the original pointer grab offset within one physical pixel");
        };
        SendMessageW(hwnd, WM_ENTERSIZEMOVE, 0, 0);
        RECT rect{}; GetWindowRect(hwnd, &rect);
        OffsetRect(&rect, 12, 12);
        apply_move(hwnd, rect);
        require(tears == 1 && IsWindowVisible(hwnd), "First movement tears out before hover");
        require(single ? !remainder : remainder != nullptr, "A single tab does not create a remainder window");
        check_anchor();
        auto move_target = [&](bool over) {
            require(SetWindowPos(target_hwnd, HWND_TOPMOST, point.x - 100,
                point.y + (over ? -MulDiv(24, dpi, 96) : 600), 980, 600,
                SWP_NOACTIVATE) != 0, "Move only the fixture destination beneath the stationary pointer");
        };
        move_target(true);
        const auto overlay = CreateWindowExW(WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
            L"STATIC", L"XUI tab occlusion fixture", WS_POPUP, point.x - 20, point.y - 20, 40, 40,
            nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        require(overlay != nullptr, "Create layered overlay fixture");
        struct Destroy { HWND hwnd; ~Destroy() { DestroyWindow(hwnd); } } destroy{overlay};
        require(SetLayeredWindowAttributes(overlay, 0, 255, LWA_ALPHA) != 0, "Make overlay opaque");
        require(SetWindowPos(overlay, HWND_TOPMOST, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW) != 0, "Expose opaque overlay");
        apply_move(hwnd, rect);
        require(joins == 0 && IsWindowVisible(hwnd), "Opaque layered windows still block hover merge");
        if (cancel) {
            SetLastError(0);
            const auto previous = SetWindowLongPtrW(overlay, GWL_EXSTYLE,
                GetWindowLongPtrW(overlay, GWL_EXSTYLE) | WS_EX_TRANSPARENT);
            require(previous != 0 || GetLastError() == 0, "Make layered overlay pass pointer input through");
        } else if (reject_drop) {
            const auto region = CreateRectRgn(0, 0, 5, 5);
            require(region != nullptr, "Create overlay region outside the pointer");
            if (!SetWindowRgn(overlay, region, TRUE)) {
                DeleteObject(region);
                throw std::runtime_error("Apply overlay region");
            }
        } else require(SetLayeredWindowAttributes(overlay, 0, 0, LWA_ALPHA) != 0, "Make overlay transparent");
        apply_move(hwnd, rect);
        require(joins == 1 && drops == 0 && !IsWindowVisible(hwnd) && IsWindow(hwnd),
            "Transparent overlay does not block live merge; moving HWND hides but stays alive");
        apply_move(hwnd, rect);
        require(joins == 2 && drops == 0, "Hover reorder stays in the same native gesture");
        move_target(false);
        apply_move(hwnd, rect);
        require(leaves == 1 && IsWindowVisible(hwnd) && above(hwnd, target_hwnd),
            "Leaving a target restores dragged content and lowers its former host");
        require(rect.right - rect.left == 800 && rect.bottom - rect.top == 440,
            "Unjoin restores detached dimensions rather than keeping destination dimensions");
        check_anchor();
        move_target(true);
        apply_move(hwnd, rect);
        require(joins == 3 && !IsWindowVisible(hwnd), "A second hover join does not start a second loop");
        if (disable_target) EnableWindow(target_hwnd, FALSE);
        if (cancel) SendMessageW(hwnd, WM_CANCELMODE, 0, 0);
        SendMessageW(hwnd, WM_EXITSIZEMOVE, 0, 0);
    };
    require(SetWindowSubclass(source_hwnd, MoveDriver::procedure, 1, reinterpret_cast<DWORD_PTR>(&driver)) != 0,
        "Install hover move-loop driver");
    const auto control = peer(source_hwnd);
    SendMessageW(control, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(MulDiv(40, dpi, 96), MulDiv(20, dpi, 96)));
    SendMessageW(control, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(MulDiv(60, dpi, 96), MulDiv(20, dpi, 96)));
    RemoveWindowSubclass(source_hwnd, MoveDriver::procedure, 1);
    if (driver.failure) std::rethrow_exception(driver.failure);
    require(source->error().empty(), "Native live join callbacks succeed");
    require(driver.loops == 1 && completions == 1 && cancellations == (cancel ? 1 : 0),
        "One native gesture has one completion and explicit cancellation");
    require(drops == (cancel || disable_target ? 0 : 1) && leaves == (cancel || reject_drop || disable_target ? 2 : 1),
        "Release commits once; cancellation and rejected commit unjoin first");
    require(target->titlebar()->tabs()->tabs().size() == (cancel || reject_drop || disable_target ? 1 : 2),
        "The tab exists in exactly the intended destination");
    require(!target->titlebar()->tabs()->drop_indicator(), "Live hover feedback clears after the loop");
    require(GetForegroundWindow() == foreground, "Background fixture does not steal foreground");
    std::cout << "  Live hover: joins=" << joins << " leaves=" << leaves << " drops=" << drops
        << " cancelled=" << cancellations << " maximized-remainder=" << maximized_remainder << '\n';
    app.shutdown();
    require(app.run() == 0, "Hover fixture retires all HWNDs");
}
}
int main(int argc, char** argv) {
    try {
        hover_join(false, false, false, false, true);
        hover_join(true, false, false, false, true);
        hover_join(false, true, false, false, true);
        if (argc == 2 && std::string_view(argv[1]) == "--single-tab") {
            std::cout << "Single-tab hover merge, cancellation and rejection passed\n";
            return 0;
        }
        geometry();
        placement_contracts();
        secondary_only();
        gesture(false, false, false, false);
        gesture(true, false, false, false);
        gesture(true, true, false, false);
        gesture(true, true, true, false);
        gesture(true, false, true, false, WM_ACTIVATE);
        gesture(true, false, true, false, WM_CAPTURECHANGED);
        gesture(true, true, false, true);
        hover_join(false, false, false);
        hover_join(true, false, true);
        hover_join(false, true, false);
        hover_join(false, false, false, true);
        native_loop();
        std::cout << "Tab drag geometry, live hover, Z-order, drop rejection, cancellation and retirement passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
