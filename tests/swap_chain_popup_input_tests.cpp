#include "xui/application.hpp"
#include "xui/documents.hpp"
#include "../src/drawing.hpp"
#include "../demo/swap_chain_renderer.hpp"
#include "native_focus_diagnostics.hpp"
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

namespace xui {
struct DrawingTestAccess {
    static void observe(void (*callback)(HWND)) { Drawing::present_observer_ = callback; }
    static void fail_next(HRESULT result) { Drawing::end_result_override_ = result; }
};
}
namespace {
using namespace xui;
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
HWND child(HWND root, const wchar_t* name) {
    struct Search { const wchar_t* name; HWND result{}; } search{name};
    EnumChildWindows(root, [](HWND hwnd, LPARAM value) -> BOOL {
        auto& search = *reinterpret_cast<Search*>(value);
        wchar_t text[256]{};
        GetWindowTextW(hwnd, text, 256);
        if (std::wstring_view(text) == search.name) { search.result = hwnd; return FALSE; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    require(search.result != nullptr, "Find owned popup control");
    return search.result;
}
HWND editor(HWND root) {
    HWND result{};
    EnumChildWindows(root, [](HWND hwnd, LPARAM value) -> BOOL {
        wchar_t name[32]{};
        GetClassNameW(hwnd, name, 32);
        if (_wcsicmp(name, L"EDIT") == 0) {
            *reinterpret_cast<HWND*>(value) = hwnd; return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&result));
    require(result != nullptr, "Popup retains a native EDIT child");
    return result;
}
void flush(HWND root) {
    SendMessageW(root, WM_APP + 12, 0, 0);
    RedrawWindow(root, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
}
struct Injection {
    HWND root{}, popup{};
    Window* window{};
    Popup* model{};
    HRESULT result{S_OK};
    bool close{}, dismiss{}, fired{};
};
Injection* injection{};
void presented(HWND hwnd) {
    auto* test = injection;
    if (!test || test->fired) return;
    if (FAILED(test->result) && hwnd == test->root) {
        test->fired = true;
        DrawingTestAccess::fail_next(test->result);
    } else if (hwnd == test->popup && (test->close || test->dismiss)) {
        test->fired = true;
        if (test->close) test->window->close();
        else test->window->dismiss_popup(*test->model);
    }
}
void run(bool paint_failure) {
    native_focus_diagnostics::Trace trace(paint_failure ? "popup paint failure" : "popup input and lifetime");
    const auto foreground = GetForegroundWindow();
    const auto targets = Drawing::live_targets();
    WindowOptions options;
    options.title = L"Owned popup input and lifetime fixture";
    options.size = {800, 600};
    options.show_activated = false;
    options.visual_style = VisualStyle::winui;
    Window window(options);
    auto root = std::make_shared<Stack>(Axis::vertical);
    auto anchor = std::make_shared<Button>(L"Popup anchor");
    auto panel = std::make_shared<SwapChainPanel>(L"Live input fixture producer");
    root->add(anchor); root->add(panel, 1);
    window.set_content(root);
    auto content = std::make_shared<Stack>(Axis::vertical);
    auto search = std::make_shared<TextInput>(L"Overlay native search");
    auto nested_anchor = std::make_shared<Button>(L"Nested overlay anchor");
    auto results = std::make_shared<ContentHost>();
    content->add(search); content->add(nested_anchor); content->add(results, 1);
    auto popup = std::make_shared<Popup>(content, L"Input overlay");
    popup->set_fixed_size({420, 250});
    popup->set_placement(PopupPlacement::below_center);
    auto nested = std::make_shared<Popup>(std::make_shared<Label>(L"Nested native surface"), L"Nested overlay");
    nested->set_fixed_size({240, 110});
    std::unique_ptr<swap_chain_sample::Renderer> producer;
    std::atomic<bool> finished{};
    std::string failure;
    bool exercised{};
    window.on_closed([&] {
        DrawingTestAccess::observe(nullptr);
        injection = nullptr;
        producer.reset();
        finished = true;
    });
    std::jthread worker([&](std::stop_token stop) {
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        require(window.post([&] {
            try {
                const auto host = panel->native_window();
                const auto hwnd = GetAncestor(host, GA_ROOT);
                require(GetForegroundWindow() == foreground, "Fixture starts without activation");
                producer = std::make_unique<swap_chain_sample::Renderer>(false, true);
                panel->set_swap_chain(producer->chain());
                producer->render(panel->metrics(), false);
                const auto metrics = panel->metrics();
                trace.during("initial popup", [&] { window.show_popup(popup, *anchor, search.get()); flush(hwnd); });
                const auto original_editor = editor(child(hwnd, L"Input overlay"));
                const auto first_result = std::make_shared<Button>(L"First scoped result");
                trace.during("first result", [&] { window.replace_content(*results, first_result); flush(hwnd); });
                require(child(hwnd, L"First scoped result"), "Popup accepts owned ContentHost replacement");
                trace.during("nested popup", [&] { window.show_popup(nested, *first_result); flush(hwnd); });
                trace.during("retire nested anchor", [&] {
                    window.replace_content(*results, std::make_shared<Button>(L"Second scoped result")); flush(hwnd);
                });
                require(!nested->is_open(), "Replacing a result dismisses its anchored nested popup");
                require(child(hwnd, L"Second scoped result"), "Popup result replacement retains its independent surface");
                trace.during("empty results", [&] { window.replace_content(*results, {}); flush(hwnd); });
                require(!results->content() && editor(child(hwnd, L"Input overlay")) == original_editor,
                    "Empty result replacement preserves the native search editor");
                Window foreign(options);
                auto foreign_root = std::make_shared<Stack>(Axis::vertical);
                auto foreign_host = std::make_shared<ContentHost>();
                foreign_root->add(foreign_host);
                foreign.set_content(foreign_root);
                bool foreign_rejected{};
                try { window.replace_content(*foreign_host, std::make_shared<Label>(L"Foreign result")); }
                catch (const std::invalid_argument&) { foreign_rejected = true; }
                require(foreign_rejected, "Popup support does not permit foreign hosts");
                auto popup_hwnd = child(hwnd, L"Input overlay");
                const auto edit = editor(popup_hwnd);
                require(panel->metrics() == metrics && panel->native_window() == host, "Popup preserves producer identity and metrics");
                require(GetForegroundWindow() == foreground, "Initial-focus request does not activate background owner");
                require(Drawing::live_targets() == targets + 2, "Popup owns a separate drawing target");
                require(SetWindowTextW(edit, L"Search \x03a9 \x4e2d") != FALSE, "Set native search text");
                flush(hwnd);
                require(search->text() == L"Search \x03a9 \x4e2d", "Native EDIT changes reach retained model");
                RECT actual{}; GetWindowRect(edit, &actual);
                MapWindowPoints(nullptr, hwnd, reinterpret_cast<POINT*>(&actual), 2);
                const auto bounds = search->bounds();
                require(actual.left >= bounds.x * GetDpiForWindow(hwnd) / 96.0f - 1, "Native editor uses root-relative popup geometry");
                auto region = CreateRectRgn(0, 0, 0, 0);
                require(region && GetWindowRgn(popup_hwnd, region) != ERROR, "Popup has an owned rounded region");
                require(!PtInRegion(region, 0, 0) && PtInRegion(region, 20, 20), "Rounded hit boundary excludes the corner");
                DeleteObject(region);

                if (!paint_failure) {
                    trace.during("outside dismissal", [&] {
                        SendMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(0, 0)); flush(hwnd);
                    });
                    require(!popup->is_open() && GetForegroundWindow() == foreground, "Outside dismissal does not activate background owner");
                    bool detached_rejected{};
                    try { window.replace_content(*results, std::make_shared<Label>(L"Detached result")); }
                    catch (const std::invalid_argument&) { detached_rejected = true; }
                    require(detached_rejected, "Closed popup content is not a mounted replacement target");
                    auto dialog = std::make_shared<ContentDialog>(L"Owned modal overlay", std::make_shared<Label>(L"Modal input boundary"));
                    trace.during("show modal", [&] { window.show_dialog(dialog, *anchor); flush(hwnd); });
                    require(!IsWindowEnabled(host) && panel->metrics() == metrics && panel->has_content(),
                        "Modal input disables the producer peer without hiding its live surface");
                    SendMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(0, 0)); flush(hwnd);
                    require(dialog->popup()->is_open(), "Outside input does not dismiss a modal overlay");
                    trace.during("cancel modal", [&] { dialog->cancel(); flush(hwnd); });
                    require(IsWindowEnabled(host) && !dialog->popup()->is_open(), "Modal dismissal restores producer input");
                    window.show_popup(popup, *anchor, search.get()); flush(hwnd);
                    window.replace_content(*results, std::make_shared<Button>(L"Reopened scoped result")); flush(hwnd);
                    require(child(hwnd, L"Reopened scoped result"), "Reopened popup accepts new owned results");
                    popup_hwnd = child(hwnd, L"Input overlay");
                    window.show_popup(nested, *nested_anchor); flush(hwnd);
                    auto nested_hwnd = child(hwnd, L"Nested overlay");
                    require(GetWindow(nested_hwnd, GW_HWNDPREV) == nullptr, "Nested popup is above all root siblings");
                    require(Drawing::live_targets() == targets + 3, "Nested target resources stay separate");
                    search->set_text(L"Changed while nested"); flush(hwnd);
                    require(GetWindow(nested_hwnd, GW_HWNDPREV) == nullptr, "Model updates preserve nested z-order");
                    SendMessageW(nested_hwnd, WM_KEYDOWN, VK_ESCAPE, 0); flush(hwnd);
                    require(!nested->is_open() && popup->is_open(), "Escape dismisses only the top popup");
                    require(Drawing::live_targets() == targets + 2, "Dismissal releases nested target");
                    RECT outer{}; GetWindowRect(hwnd, &outer);
                    require(SetWindowPos(hwnd, nullptr, 0, 0, 620, 460, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE) != FALSE,
                        "Resize background owner");
                    flush(hwnd);
                    GetWindowRect(hwnd, &outer);
                    trace.during("DPI change", [&] {
                        SendMessageW(hwnd, WM_DPICHANGED, MAKEWPARAM(144, 144), reinterpret_cast<LPARAM>(&outer)); flush(hwnd);
                    });
                    RECT clip{}, local{}; GetClientRect(hwnd, &clip); GetWindowRect(popup_hwnd, &local);
                    MapWindowPoints(nullptr, hwnd, reinterpret_cast<POINT*>(&local), 2);
                    require(local.left >= 0 && local.top >= 0 && local.right <= clip.right && local.bottom <= clip.bottom,
                        "Popup stays in client clip after resize and DPI change");
                    require(search->text() == L"Changed while nested", "Native text survives layout and DPI changes");
                    const auto current_popup = child(hwnd, L"Input overlay");
                    Injection recreate{hwnd, current_popup, &window, popup.get(), D2DERR_RECREATE_TARGET};
                    injection = &recreate; DrawingTestAccess::observe(presented);
                    trace.during("recreate popup target", [&] { flush(hwnd); });
                    require(recreate.fired, "Recreation injected into popup target after root presentation");
                    injection = nullptr; DrawingTestAccess::observe(nullptr);
                    flush(hwnd);
                    require(Drawing::live_targets() == targets + 2 && popup->is_open(), "Popup target recreation preserves popup and root");
                    Injection dismiss{hwnd, current_popup, &window, popup.get(), S_OK, false, true};
                    injection = &dismiss; DrawingTestAccess::observe(presented);
                    trace.during("reentrant dismiss", [&] { flush(hwnd); });
                    require(dismiss.fired && !popup->is_open(), "Popup dismisses reentrantly after its presentation");
                    injection = nullptr; DrawingTestAccess::observe(nullptr); flush(hwnd);
                    require(Drawing::live_targets() == targets + 1, "Reentrant dismissal releases popup target");
                    window.show_popup(popup, *anchor, search.get()); flush(hwnd);
                    Injection close{hwnd, child(hwnd, L"Input overlay"), &window, popup.get(), S_OK, true};
                    injection = &close; DrawingTestAccess::observe(presented);
                    trace.during("reentrant close", [&] { flush(hwnd); });
                    require(close.fired, "Owner closes from popup presentation");
                } else {
                    Injection fail{hwnd, popup_hwnd, &window, popup.get(), E_FAIL};
                    injection = &fail; DrawingTestAccess::observe(presented);
                    trace.during("paint failure", [&] { flush(hwnd); });
                    require(fail.fired && !window.error().empty(), "Popup drawing failure reaches explicit window error");
                }
                const auto actual_foreground = GetForegroundWindow();
                if (actual_foreground != foreground)
                    std::cerr << "Popup owner=" << hwnd << " foreground=" << foreground
                        << " -> " << actual_foreground << '\n';
                require(actual_foreground == foreground, "Popup input and teardown do not activate owner");
                exercised = true;
            } catch (const std::exception& error) { failure = error.what(); }
            injection = nullptr;
            DrawingTestAccess::observe(nullptr);
            window.close();
        }), "Post owned popup fixture");
        for (int i = 0; i < 400 && !finished && !stop.stop_requested(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (!finished && !stop.stop_requested()) std::_Exit(2);
    });
    const auto result = Application::run(window);
    worker.request_stop(); worker.join();
    require(failure.empty(), failure.c_str());
    require(exercised && (paint_failure ? result != 0 : result == 0), "Expected popup lifetime result");
    require(Drawing::live_targets() == targets, "Owner closure releases every popup drawing target");
    trace.verify_passive();
}
}
int main() {
    try {
        run(false); run(true);
        std::cout << "Popup native input, nesting, DPI, target recreation, reentrant dismissal/close, and explicit paint errors passed without activation.\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
