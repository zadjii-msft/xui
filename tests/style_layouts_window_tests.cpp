#include "xui/application.hpp"
#include "xui/adaptive_layout.hpp"
#include "xui/documents.hpp"
#include "owned_window_capture.hpp"
#include "../src/drawing.hpp"
#include <UIAutomation.h>
#include <wrl/client.h>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <iostream>
#include <thread>

namespace {
using namespace xui;
constexpr wchar_t title[] = L"XUI layout style integration";
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void success(HRESULT result) { require(SUCCEEDED(result), "Layout UIA call failed"); }
void pump(unsigned milliseconds) {
    const auto end = GetTickCount64() + milliseconds;
    do {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message); DispatchMessageW(&message);
        }
        Sleep(1);
    } while (GetTickCount64() < end);
}
void flush(HWND host) {
    pump(30); RedrawWindow(host, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW); pump(20);
}
HWND find_host() {
    HWND result{};
    EnumWindows([](HWND window, LPARAM data) {
        DWORD process{}; GetWindowThreadProcessId(window, &process);
        wchar_t name[128]{}; GetWindowTextW(window, name, 128);
        if (process == GetCurrentProcessId() && std::wstring_view(name) == title) {
            *reinterpret_cast<HWND*>(data) = window; return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&result));
    return result;
}
std::vector<HWND> peers(HWND host) {
    std::vector<HWND> result;
    EnumChildWindows(host, [](HWND child, LPARAM data) {
        reinterpret_cast<std::vector<HWND>*>(data)->push_back(child); return TRUE;
    }, reinterpret_cast<LPARAM>(&result));
    return result;
}
std::shared_ptr<Element> blank(float height = 20) {
    auto result = std::make_shared<Stack>(Axis::vertical); result->set_minimum_size({100, height}); return result;
}
PartStyleValues surface(uint32_t color) {
    PartStyleValues result; result.background = ThemeColor{color};
    result.border_brush = ThemeColor{0xabcdef}; result.border_thickness = Insets{3, 4, 5, 6};
    result.padding = Insets{8, 8, 8, 8}; result.corner_radius = 0.0f;
    return result;
}
void apply(Element& element, StyleTarget target, uint32_t color) {
    element.set_control_style(ControlStyle::create(target, {{StylePart::root, surface(color)}}, {}));
}
void check_pixel(HWND host, const owned_window_capture::Pixels& image, Point point, uint32_t expected) {
    const auto scale = GetDpiForWindow(host) / 96.0f;
    const auto x = int(point.x * scale), y = int(point.y * scale);
    require(x >= 0 && x < image.width && y >= 0 && y < image.height, "Layout pixel is inside owned capture");
    const auto actual = image.data[std::size_t(y) * image.width + x] & 0xffffff;
    require(actual == expected, "Application layout surface did not paint its authored color");
}
struct Runner {
    static inline Runner* active{};
    Window& window;
    std::function<void(HWND)> action;
    UINT_PTR timer{};
    ULONGLONG deadline{GetTickCount64() + 10000};
    std::exception_ptr failure;
    bool ran{};
    static void CALLBACK on_timer(HWND, UINT, UINT_PTR timer_id, DWORD) {
        auto& self = *active;
        const auto host = find_host();
        if (!host && GetTickCount64() < self.deadline) return;
        KillTimer(nullptr, timer_id); self.timer = 0;
        try { require(host != nullptr, "Layout window starts"); self.ran = true; self.action(host); }
        catch (...) { self.failure = std::current_exception(); }
        self.window.close();
    }
    Runner(Window& owner, std::function<void(HWND)> body) : window(owner), action(std::move(body)) {
        active = this;
        timer = SetTimer(nullptr, 0, 20, on_timer);
        require(timer != 0, "Schedule layout window test");
    }
    ~Runner() { if (timer) KillTimer(nullptr, timer); active = nullptr; }
};
void run() {
    Window window({title, {1000, 900}, ThemeMode::light});
    auto root = std::make_shared<Stack>(Axis::vertical);
    apply(*root, StyleTarget::stack, 0x102030);
    auto grid = std::make_shared<Grid>(); grid->set_auto_size(false); grid->set_preferred_size({900, 65}); grid->add(blank(), 0, 0);
    auto wrap = std::make_shared<Wrap>(); wrap->set_auto_size(false); wrap->set_preferred_size({900, 65}); wrap->add(blank());
    auto adaptive = std::make_shared<AdaptiveLayout>(blank(), blank()); adaptive->set_preferred_size({900, 65});
    auto pages = std::make_shared<PageView>(); pages->set_preferred_size({900, 65}); pages->add_page(blank()); pages->add_page(blank());
    auto content = std::make_shared<ContentView>(blank()); content->set_preferred_size({900, 65});
    auto nested_layout = std::static_pointer_cast<Stack>(content->content());
    PartStyleValues normal_layout; normal_layout.padding = Insets{1, 1, 1, 1};
    PartStyleValues disabled_layout; disabled_layout.padding = Insets{15, 4, 8, 9};
    nested_layout->set_control_style(ControlStyle::create(StyleTarget::stack, {{StylePart::root, normal_layout}},
        {{StylePart::root, style_states::disabled, disabled_layout}}));
    content->set_enabled(false);
    auto scroll = std::make_shared<ScrollView>(blank(400)); scroll->set_preferred_size({900, 80});
    auto first_pane = std::make_shared<Stack>(Axis::horizontal);
    auto second_pane = std::make_shared<Stack>(Axis::horizontal);
    first_pane->add(std::make_shared<Button>(L"First address"));
    first_pane->add(std::make_shared<TextInput>(L"Pane editor"));
    second_pane->add(std::make_shared<Button>(L"Second address"));
    auto split = std::make_shared<SplitView>(first_pane, second_pane); split->set_preferred_size({900, 80});
    auto expander = std::make_shared<Expander>(L"Styled header", blank()); expander->set_preferred_size({900, 100});
    const std::vector<std::pair<std::shared_ptr<Element>, uint32_t>> panels{
        {grid, 0x203040}, {wrap, 0x304050}, {adaptive, 0x405060}, {pages, 0x506070},
        {content, 0x607080}, {scroll, 0x708090}, {split, 0x8090a0}, {expander, 0x90a0b0}};
    const StyleTarget targets[]{StyleTarget::grid, StyleTarget::wrap, StyleTarget::adaptive_layout, StyleTarget::page_view,
        StyleTarget::content_view, StyleTarget::scroll_view, StyleTarget::split_view, StyleTarget::expander};
    for (std::size_t i = 0; i < panels.size(); ++i) {
        apply(*panels[i].first, targets[i], panels[i].second); root->add(panels[i].first);
    }
    PartStyleValues track; track.width = 24.0f; track.background = ThemeColor{0x1122dd};
    PartStyleValues thumb; thumb.background = ThemeColor{0xdd2211};
    scroll->set_control_style_values(StylePart::scrollbar_track, track);
    scroll->set_control_style_values(StylePart::scrollbar_thumb, thumb);
    PartStyleValues divider; divider.width = 20.0f; divider.background = ThemeColor{0x22dd11};
    split->set_control_style_values(StylePart::divider, divider);
    PartStyleValues header; header.background = ThemeColor{0x881122}; header.height = 44.0f;
    expander->set_control_style_values(StylePart::header, header);
    auto popup = std::make_shared<Popup>(blank());
    popup->set_preferred_size({200, 100}); apply(*popup, StyleTarget::popup, 0xaabb22);
    window.set_content(root);
    Runner runner(window, [&](HWND host) {
        flush(host);
        require(nested_layout->effective_layout_insets().left == 15, "Non-Control layout receives initial disabled-ancestor context");
        content->set_enabled(true); flush(host);
        require(nested_layout->effective_layout_insets().left == 1, "Non-Control layout context clears before its next layout");
        const auto original = peers(host);
        if (!Palette::system(ThemeMode::light).high_contrast) {
            auto image = owned_window_capture::capture(host);
            check_pixel(host, image, {5, 5}, 0x102030);
            for (const auto& [panel, color] : panels) {
                const auto b = panel->bounds();
                check_pixel(host, image, {b.x + 5, b.y + 6}, color);
                check_pixel(host, image, {b.x + 1, b.y + 20}, 0xabcdef);
            }
            const auto track = scroll->scrollbar_track(), thumb = scroll->thumb(), divider = split->divider();
            check_pixel(host, image, {track.x + 1, track.y + track.height - 4}, 0x1122dd);
            check_pixel(host, image, {thumb.x + thumb.width / 2, thumb.y + thumb.height / 2}, 0xdd2211);
            check_pixel(host, image, {divider.x + 1, divider.y + 4}, 0x22dd11);
        }
        HWND scroll_peer{}, split_peer{}, first_address{}, second_address{}, pane_editor{};
        for (auto peer : original) {
            wchar_t name[128]{}; GetWindowTextW(peer, name, 128);
            if (std::wstring_view(name) == scroll->name()) scroll_peer = peer;
            if (std::wstring_view(name) == split->name()) split_peer = peer;
            if (std::wstring_view(name) == L"First address") first_address = peer;
            if (std::wstring_view(name) == L"Second address") second_address = peer;
        }
        require(first_address && second_address, "Split panes retain address buttons");
        require(split_peer != nullptr, "SplitView peer exists");
        for (auto peer : peers(split_peer)) {
            wchar_t type[64]{}; GetClassNameW(peer, type, 64);
            if (_wcsicmp(type, L"EDIT") == 0) pane_editor = peer;
        }
        require(pane_editor != nullptr, "Split panes retain a native editor");
        const auto previous_cursor = GetCursor();
        struct RestoreCursor { HCURSOR previous; ~RestoreCursor() { SetCursor(previous); } } restore_cursor{previous_cursor};
        const auto arrow = LoadCursorW(nullptr, IDC_ARROW), resize = LoadCursorW(nullptr, IDC_SIZEWE);
        const auto scale = GetDpiForWindow(host) / 96.0f;
        const auto divider_bounds = split->divider(), split_bounds = split->bounds();
        SendMessageW(split_peer, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(
            int((divider_bounds.x - split_bounds.x + divider_bounds.width / 2) * scale),
            int((divider_bounds.y - split_bounds.y + divider_bounds.height / 2) * scale)));
        require(GetCapture() == split_peer, "Styled divider starts a captured drag");
        SetCursor(arrow);
        SendMessageW(split_peer, WM_SETCURSOR, reinterpret_cast<WPARAM>(split_peer), MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
        require(GetCursor() == resize, "Captured divider drag retains the resize cursor");
        SendMessageW(split_peer, WM_LBUTTONUP, 0, 0);
        require(GetCapture() != split_peer, "Divider releases capture before the pointer reaches pane controls");
        for (const auto address : {first_address, second_address}) {
            SetCursor(resize);
            SendMessageW(address, WM_SETCURSOR, reinterpret_cast<WPARAM>(address), MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
            require(GetCursor() == arrow, "SplitView must not override an address button's arrow cursor");
        }
        SetCursor(resize);
        SendMessageW(pane_editor, WM_SETCURSOR, reinterpret_cast<WPARAM>(pane_editor), MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
        require(GetCursor() == LoadCursorW(nullptr, IDC_IBEAM), "SplitView preserves the native editor's I-beam cursor");
        split->set_enabled(false);
        SetCursor(resize);
        SendMessageW(split_peer, WM_SETCURSOR, reinterpret_cast<WPARAM>(split_peer), MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
        require(GetCursor() == arrow, "Disabled SplitView does not claim the resize cursor");
        split->set_enabled(true);
        split->set_secondary_visible(false);
        SetCursor(resize);
        SendMessageW(split_peer, WM_SETCURSOR, reinterpret_cast<WPARAM>(split_peer), MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
        require(GetCursor() == arrow, "Collapsed SplitView does not claim the resize cursor");
        split->set_secondary_visible(true);
        flush(host);
        require(scroll_peer != nullptr, "Scroll peer exists");
        const auto input_scale = GetDpiForWindow(host) / 96.0f;
        const auto track_bounds = scroll->scrollbar_track(), scroll_bounds = scroll->bounds();
        SendMessageW(scroll_peer, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(
            int((track_bounds.x - scroll_bounds.x + 3) * input_scale),
            int((track_bounds.y - scroll_bounds.y + track_bounds.height - 3) * input_scale)));
        SendMessageW(scroll_peer, WM_LBUTTONUP, 0, 0);
        flush(host);
        require(scroll->offset() == scroll->viewport().height, "Scrollbar hit testing uses the authored track width and page height");
        const auto offset = scroll->offset();
        SendMessageW(scroll_peer, WM_KEYDOWN, VK_NEXT, 0); flush(host);
        require(scroll->offset() == std::min(scroll->maximum_offset(), offset + scroll->viewport().height),
            "Scrollbar keyboard paging uses the styled viewport");
        const auto viewport = scroll->viewport();
        const auto scroll_extent = scroll->extent();
        const auto expected_view = viewport.height * 100 / scroll_extent;
        const auto expected_divider = split->divider();
        POINT client_origin{}; ClientToScreen(host, &client_origin);
        require(split_peer != nullptr, "SplitView peer exists");
        std::atomic<bool> done{};
        std::exception_ptr automation_failure;
        std::thread automation([&] {
            const auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            try {
                success(initialized);
                Microsoft::WRL::ComPtr<IUIAutomation> client;
                success(CoCreateInstance(CLSID_CUIAutomation8, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&client)));
                Microsoft::WRL::ComPtr<IUIAutomation2> bounded;
                success(client.As(&bounded)); success(bounded->put_ConnectionTimeout(5000)); success(bounded->put_TransactionTimeout(5000));
                Microsoft::WRL::ComPtr<IUIAutomationElement> host_element;
                success(client->ElementFromHandle(scroll_peer, &host_element));
                VARIANT value{}; value.vt = VT_BOOL; value.boolVal = VARIANT_TRUE;
                Microsoft::WRL::ComPtr<IUIAutomationCondition> condition;
                success(client->CreatePropertyCondition(UIA_IsScrollPatternAvailablePropertyId, value, &condition));
                Microsoft::WRL::ComPtr<IUIAutomationElement> semantic;
                success(host_element->FindFirst(TreeScope_Subtree, condition.Get(), &semantic));
                require(semantic != nullptr, "Styled ScrollView retains UIA ScrollPattern");
                Microsoft::WRL::ComPtr<IUIAutomationScrollPattern> pattern;
                success(semantic->GetCurrentPatternAs(UIA_ScrollPatternId, IID_PPV_ARGS(&pattern)));
                double view_size{}; success(pattern->get_CurrentVerticalViewSize(&view_size));
                if (!(std::abs(view_size - expected_view) < 0.1))
                    throw std::runtime_error("UIA viewport uses styled padding and border metrics: expected percent=" +
                        std::to_string(expected_view) + ", actual percent=" + std::to_string(view_size) +
                        ", viewport height=" + std::to_string(viewport.height) +
                        ", outer height=" + std::to_string(scroll_bounds.height) +
                        ", extent=" + std::to_string(scroll_extent));
                Microsoft::WRL::ComPtr<IUIAutomationElement> split_host, split_semantic;
                success(client->ElementFromHandle(split_peer, &split_host));
                success(client->CreatePropertyCondition(UIA_IsRangeValuePatternAvailablePropertyId, value, &condition));
                success(split_host->FindFirst(TreeScope_Subtree, condition.Get(), &split_semantic));
                require(split_semantic != nullptr, "Styled SplitView retains UIA RangeValuePattern");
                RECT accessible{}; success(split_semantic->get_CurrentBoundingRectangle(&accessible));
                const bool matching_divider = std::abs(accessible.left - (client_origin.x + expected_divider.x * input_scale)) <= 1 &&
                    std::abs(accessible.top - (client_origin.y + expected_divider.y * input_scale)) <= 1 &&
                    std::abs((accessible.right - accessible.left) - expected_divider.width * input_scale) <= 1 &&
                    std::abs((accessible.bottom - accessible.top) - expected_divider.height * input_scale) <= 1;
                if (!matching_divider)
                    throw std::runtime_error("SplitView UIA bounds mismatch: expected pixels=" +
                        std::to_string(client_origin.x + expected_divider.x * input_scale) + "," +
                        std::to_string(client_origin.y + expected_divider.y * input_scale) + "," +
                        std::to_string(expected_divider.width * input_scale) + "," +
                        std::to_string(expected_divider.height * input_scale) + "; actual pixels=" +
                        std::to_string(accessible.left) + "," + std::to_string(accessible.top) + "," +
                        std::to_string(accessible.right - accessible.left) + "," +
                        std::to_string(accessible.bottom - accessible.top));
            } catch (...) { automation_failure = std::current_exception(); }
            if (SUCCEEDED(initialized)) CoUninitialize();
            done = true;
        });
        while (!done) pump(10);
        automation.join(); if (automation_failure) std::rethrow_exception(automation_failure);
        window.show_popup(popup, *expander); flush(host);
        require(popup->is_open(), "Popup opened through its model");
        if (!Palette::system(ThemeMode::light).high_contrast) {
            const auto image = owned_window_capture::capture(host);
            const auto b = popup->bounds();
            check_pixel(host, image, {b.x + 6, b.y + 7}, 0xaabb22);
        }
        window.dismiss_popup(*popup); flush(host);
        auto dialog = std::make_shared<ContentDialog>(L"Modal layout context", blank());
        window.show_dialog(dialog, *expander); flush(host);
        require(content->enabled() && nested_layout->effective_layout_insets().left == 15,
            "Modal context disables non-Control styles without changing local enabled state");
        dialog->cancel(); flush(host);
        require(nested_layout->effective_layout_insets().left == 1, "Closing modal restores non-Control layout context");
        expander->set_expanded(false); flush(host);
        for (const auto& [panel, color] : panels) panel->set_control_style(nullptr);
        scroll->set_control_style_values(StylePart::scrollbar_track, {});
        scroll->set_control_style_values(StylePart::scrollbar_thumb, {});
        split->set_control_style_values(StylePart::divider, {});
        expander->set_control_style_values(StylePart::header, {});
        flush(host);
        require(!expander->expanded() && expander->content()->bounds().height == 0, "Clearing styles keeps content collapsed");
        const auto retained = peers(host);
        for (auto peer : original)
            require(IsWindow(peer) && std::find(retained.begin(), retained.end(), peer) != retained.end(), "Styles replaced a retained peer");
    });
    const auto result = Application::run(window);
    if (runner.failure) std::rethrow_exception(runner.failure);
    require(runner.ran && result == 0 && window.error().empty(), "Layout integration completes");
}
}
int main() {
    try { run(); std::cout << "Layout style window tests passed\n"; return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
