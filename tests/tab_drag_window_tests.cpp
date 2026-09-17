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
        if (message == WM_SYSCOMMAND && (w & 0xfff0) == SC_MOVE) {
            ++driver.loops;
            try { driver.run(hwnd); }
            catch (...) { driver.failure = std::current_exception(); }
            return 0;
        }
        return DefSubclassProc(hwnd, message, w, l);
    }
};
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
                DWORD_PTR result{};
                delivered = SendMessageTimeoutW(hwnd, WM_MOVING, WMSZ_LEFT, reinterpret_cast<LPARAM>(&rect),
                    SMTO_ABORTIFHUNG, 3000, &result) != 0 && result == TRUE;
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
        require(SendMessageW(hwnd, WM_MOVING, WMSZ_LEFT, reinterpret_cast<LPARAM>(&proposed)) == TRUE,
            "Native WM_MOVING is handled");
        if (!tear) require(EqualRect(&before, &proposed), "Reorder pins native window instead of moving content");
        else {
            require(torn == 1 && IsWindow(hwnd), "Tear-out keeps native move HWND alive");
            require(SetWindowPos(hwnd, HWND_TOP, proposed.left, proposed.top, proposed.right - proposed.left,
                proposed.bottom - proposed.top, SWP_NOACTIVATE) != 0, "Apply native proposed rectangle");
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
}
int main() {
    try {
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
        native_loop();
        std::cout << "Tab drag geometry, native transitions, drop rejection, cancellation and retirement passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
