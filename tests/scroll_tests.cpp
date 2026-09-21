#include "xui/application.hpp"
#include "../src/drawing.hpp"
#include "../src/list_peer.hpp"
#include "../src/control_accessibility.hpp"
#include "native_focus_diagnostics.hpp"
#include <windows.h>
#include <commctrl.h>
#include <UIAutomation.h>
#include <wrl/client.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <algorithm>

using namespace xui;
using Microsoft::WRL::ComPtr;
namespace {
void require(bool condition, const char* text) {
    if (!condition) throw std::runtime_error(text);
}
void check(HRESULT result, const char* text) {
    if (FAILED(result)) throw std::runtime_error(std::string(text) + ": " + std::to_string(result));
}
template<class F> void wait(F&& predicate, const char* text) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(6);
    while (!predicate()) {
        require(std::chrono::steady_clock::now() < end, text);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}
BOOL CALLBACK find(HWND window, LPARAM value) {
    DWORD process{};
    GetWindowThreadProcessId(window, &process);
    wchar_t title[80]{};
    GetWindowTextW(window, title, 80);
    if (process == GetCurrentProcessId() && std::wstring_view(title) == L"XUI scroll tests" && IsWindowVisible(window)) {
        *reinterpret_cast<HWND*>(value) = window;
        return FALSE;
    }
    return TRUE;
}
ComPtr<IUIAutomationElement> named(IUIAutomation* automation, IUIAutomationElement* root, const wchar_t* name) {
    VARIANT value{};
    value.vt = VT_BSTR;
    value.bstrVal = SysAllocString(name);
    ComPtr<IUIAutomationCondition> condition;
    const auto result = automation->CreatePropertyCondition(UIA_NamePropertyId, value, &condition);
    VariantClear(&value);
    check(result, "Create name condition");
    ComPtr<IUIAutomationElement> element;
    check(root->FindFirst(TreeScope_Descendants, condition.Get(), &element), "Find control");
    require(element != nullptr, "Named control is present");
    return element;
}
template<class T> ComPtr<T> pattern(IUIAutomationElement* element, PATTERNID id) {
    ComPtr<T> result;
    check(element->GetCurrentPatternAs(id, __uuidof(T), reinterpret_cast<void**>(result.GetAddressOf())), "Get pattern");
    if (!result) throw std::runtime_error("Pattern is present: " + std::to_string(id));
    return result;
}
bool offscreen(IUIAutomationElement* element) {
    BOOL result{};
    check(element->get_CurrentIsOffscreen(&result), "Read offscreen");
    return result != FALSE;
}
RECT bounds(IUIAutomationElement* element) {
    RECT result{};
    check(element->get_CurrentBoundingRectangle(&result), "Read bounds");
    return result;
}
double percent(IUIAutomationScrollPattern* scroll) {
    double result{};
    check(scroll->get_CurrentVerticalScrollPercent(&result), "Read percent");
    return result;
}
void pump() {
    MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_ALLINPUT);
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}
class MeasuredStack final : public Stack {
public:
    MeasuredStack() : Stack(Axis::vertical) {}
    Size measure(Size available) override {
        ++measurements;
        return Stack::measure(available);
    }
    unsigned measurements{};
};
struct NativeMoveCounter {
    explicit NativeMoveCounter(HWND value) : window(value) {
        require(SetWindowSubclass(window, procedure, 91, reinterpret_cast<DWORD_PTR>(this)) != FALSE, "Observe owned editor placement");
    }
    ~NativeMoveCounter() { RemoveWindowSubclass(window, procedure, 91); }
    static LRESULT CALLBACK procedure(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR, DWORD_PTR data) {
        if (message == WM_WINDOWPOSCHANGING) ++reinterpret_cast<NativeMoveCounter*>(data)->moves;
        return DefSubclassProc(hwnd, message, wparam, lparam);
    }
    HWND window;
    unsigned moves{};
};
void retained_scroll_window() {
    native_focus_diagnostics::Trace trace("Retained settings scroll");
    WindowOptions options;
    options.title = L"XUI retained scroll tests";
    options.size = {1000, 650};
    options.show_activated = false;
    options.visual_style = VisualStyle::winui;
    Window window(options);
    auto root = std::make_shared<MeasuredStack>();
    auto content = std::make_shared<Stack>(Axis::vertical);
    std::shared_ptr<TextInput> draft;
    std::shared_ptr<Button> anchor;
    for (unsigned i = 0; i < 138; ++i) {
        auto row = std::make_shared<Stack>(Axis::horizontal);
        row->set_spacing(4);
        for (unsigned j = 0; j < 4; ++j)
            row->add(std::make_shared<Label>(L"Setting"));
        for (unsigned j = 0; j < 4; ++j) {
            auto button = std::make_shared<Button>(L"Action");
            if (!anchor) anchor = button;
            row->add(button);
        }
        row->add(std::make_shared<Toggle>(L"Enabled"));
        auto input = std::make_shared<TextInput>(L"Draft " + std::to_wstring(i));
        input->set_text(L"Uncommitted draft");
        if (!draft) draft = input;
        row->add(input, 1);
        content->add(row);
    }
    auto nested_content = std::make_shared<Stack>(Axis::vertical);
    for (unsigned i = 0; i < 8; ++i)
        nested_content->add(std::make_shared<Button>(L"Nested action"));
    auto nested_scroll = std::make_shared<ScrollView>(nested_content, L"Nested retained settings");
    nested_scroll->set_preferred_size({300, 120});
    content->add(nested_scroll);
    auto host = std::make_shared<ContentHost>(content);
    auto scroll = std::make_shared<ScrollView>(host, L"Retained settings");
    root->add(scroll, 1);
    window.set_content(root);
    bool completed{};
    window.post([&] {
        const auto hwnd = FindWindowW(L"Xui.Window.1", options.title.c_str());
        const auto native = FindWindowExW(hwnd, nullptr, L"Xui.Control.1", L"Retained settings");
        const auto native_content = FindWindowExW(native, nullptr, L"Xui.ScrollContent.1", nullptr);
        const auto edit = FindWindowExW(native_content, nullptr, L"EDIT", nullptr);
        require(hwnd && native && edit, "Retained scroll native tree exists");
        unsigned windows{};
        EnumChildWindows(
            hwnd,
            [](HWND, LPARAM data) -> BOOL {
                ++*reinterpret_cast<unsigned*>(data);
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&windows));
        require(windows >= 1500, "Performance fixture retains more than 1500 native windows");
        const auto flush = [&] { SendMessageW(hwnd, WM_APP + 12, 0, 0); };
        SendMessageW(edit, EM_SETSEL, 0, -1);
        SendMessageW(edit, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"Edited retained draft"));
        require(draft->text() == L"Edited retained draft", "Translated layer forwards native editor notifications");
        flush();
        SendMessageW(edit, EM_SETSEL, 2, 9);
        const auto selection = SendMessageW(edit, EM_GETSEL, 0, 0);
        const auto focus = GetFocus();
        const auto measurements = root->measurements;
        const auto first_y = anchor->bounds().y;
        NativeMoveCounter editor_moves(edit);
        std::vector<double> timings;
        std::vector<double> painted_timings;
        for (unsigned i = 0; i < 40; ++i) {
            const auto start = std::chrono::steady_clock::now();
            SendMessageW(native, WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(-WHEEL_DELTA)), 0);
            timings.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
            require(RedrawWindow(hwnd, nullptr, nullptr, RDW_UPDATENOW | RDW_ALLCHILDREN), "Present retained scroll pixels synchronously");
            painted_timings.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
            flush();
        }
        require(root->measurements == measurements, "Offset-only scroll never measures the root");
        require(editor_moves.moves == 0, "Offset-only scrolling moves the content layer, not native editors");
        require(scroll->offset() > 0 && anchor->bounds().y == first_y - scroll->offset(),
                "Offset-only scroll updates retained hit-test geometry");
        require(GetFocus() == focus && SendMessageW(edit, EM_GETSEL, 0, 0) == selection, "Scrolling preserves native focus and selection");
        wchar_t text[80]{};
        GetWindowTextW(edit, text, 80);
        require(std::wstring_view(text) == draft->text(), "Scrolling preserves the native draft");
        std::sort(timings.begin(), timings.end());
        std::sort(painted_timings.begin(), painted_timings.end());
        std::cout << "retained_scroll hwnds=" << windows << " median_ms=" << timings[20] << " p95_ms=" << timings[38]
                  << " painted_median_ms=" << painted_timings[20] << " painted_p95_ms=" << painted_timings[38]
                  << " root_measures=" << root->measurements - measurements << '\n';
        require(timings[38] < 16, "Retained wheel p95 stays below one frame");
        pump();
        trace.verify_passive();
        auto state = std::make_shared<ControlAccessibility>();
        publish_control(state, nullptr, *scroll, native);
        ComPtr<IRawElementProviderSimple> provider;
        provider.Attach(create_control_provider(state));
        ComPtr<IScrollProvider> scroller;
        check(provider.As(&scroller), "Get retained scroll provider");
        check(scroller->SetScrollPercent(-1, 0), "Scroll retained content to the top through its provider");
        const auto top_clip = clipped_bounds(edit);
        require(scroll->offset() == 0 && !IsRectEmpty(&top_clip), "Provider scroll reveals native input");
        check(scroller->SetScrollPercent(-1, 100), "Scroll retained content to the end through its provider");
        const auto first_clip = clipped_bounds(edit);
        require(scroll->offset() == scroll->maximum_offset() && IsRectEmpty(&first_clip),
                "Provider scroll clips native input without retiring it");
        HWND last_edit = edit;
        while (const auto next = FindWindowExW(native_content, last_edit, L"EDIT", nullptr))
            last_edit = next;
        const auto last_clip = clipped_bounds(last_edit), view_clip = clipped_bounds(native);
        require(!IsRectEmpty(&last_clip) && last_clip.top >= view_clip.top && last_clip.bottom <= view_clip.bottom,
                "Native accessibility bounds follow the translated layer");
        require(GetFocus() == focus && SendMessageW(edit, EM_GETSEL, 0, 0) == selection,
                "UIA scrolling preserves native focus and selection");
        const auto nested_native = FindWindowExW(native_content, nullptr, L"Xui.Control.1", L"Nested retained settings");
        require(nested_native != nullptr, "Nested scroll retains its native viewport");
        const auto before_nested = root->measurements;
        nested_scroll->set_offset(16);
        flush();
        require(root->measurements == before_nested && nested_scroll->offset() == 16, "Nested offsets also avoid root measurement");
        const auto nested_layer = FindWindowExW(nested_native, nullptr, L"Xui.ScrollContent.1", nullptr);
        RECT nested_rect{};
        require(GetWindowRect(nested_layer, &nested_rect), "Read nested native content geometry");
        MapWindowPoints(nullptr, nested_native, reinterpret_cast<POINT*>(&nested_rect), 2);
        require(nested_rect.top == -MulDiv(16, GetDpiForWindow(hwnd), 96), "Nested native translation matches the retained offset");
        draft->set_text(L"Updated during scrolling");
        scroll->scroll_by(-16);
        flush();
        GetWindowTextW(edit, text, 80);
        require(std::wstring_view(text) == draft->text(), "Mixed text and offset invalidations synchronize the native editor");
        SendMessageW(edit, WM_IME_STARTCOMPOSITION, 0, 0);
        flush();
        scroll->scroll_by(-16);
        flush();
        bool composing{};
        try { draft->set_selection({0, 0}); }
        catch (const std::logic_error&) { composing = true; }
        require(composing && GetFocus() == focus, "Scrolling retains native composition and does not transfer focus");
        SendMessageW(edit, WM_IME_ENDCOMPOSITION, 0, 0);
        flush();
        GetWindowTextW(edit, text, 80);
        require(std::wstring_view(text) == draft->text(), "Native composition completion crosses the content layer");
        content->set_spacing(7);
        SendMessageW(native, WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(-WHEEL_DELTA)), 0);
        require(root->measurements > measurements, "Pending content changes force full layout before scrolling");
        scroll->set_offset(0);
        flush();
        auto popup_rows = std::make_shared<Stack>(Axis::vertical);
        for (unsigned i = 0; i < 30; ++i) popup_rows->add(std::make_shared<Button>(L"Popup setting"));
        auto popup_scroll = std::make_shared<ScrollView>(popup_rows, L"Popup retained settings");
        popup_scroll->set_preferred_size({300, 180});
        auto stationary_popup = std::make_shared<Popup>(popup_scroll);
        window.show_popup(stationary_popup, *anchor);
        flush();
        const auto before_popup_scroll = root->measurements;
        const auto popup_bounds = stationary_popup->bounds();
        popup_scroll->set_offset(48);
        flush();
        require(root->measurements == before_popup_scroll && popup_scroll->offset() == 48,
                "Scrolling inside a stationary popup avoids root measurement");
        require(stationary_popup->bounds().x == popup_bounds.x && stationary_popup->bounds().y == popup_bounds.y &&
                stationary_popup->is_open(), "Scrolling popup content preserves popup placement and lifetime");
        require(RedrawWindow(hwnd, nullptr, nullptr, RDW_UPDATENOW | RDW_ALLCHILDREN), "Present scrolled popup pixels synchronously");
        window.dismiss_popup(*stationary_popup);
        flush();
        auto popup = std::make_shared<Popup>(std::make_shared<Button>(L"Popup child"));
        window.show_popup(popup, *anchor);
        scroll->set_offset(scroll->maximum_offset());
        flush();
        require(!popup->is_open(), "Scrolling an anchor offscreen dismisses its popup");
        flush();
        RECT suggested{};
        require(GetWindowRect(hwnd, &suggested), "Read owned DPI fixture bounds");
        const auto before_dpi = root->measurements;
        SendMessageW(hwnd, WM_DPICHANGED, MAKEWPARAM(120, 120), reinterpret_cast<LPARAM>(&suggested));
        flush();
        scroll->set_offset(32);
        flush();
        RECT translated{};
        require(GetWindowRect(native_content, &translated), "Read DPI-scaled native content geometry");
        MapWindowPoints(nullptr, native, reinterpret_cast<POINT*>(&translated), 2);
        require(root->measurements > before_dpi && translated.top == -40,
                "DPI changes invalidate placement caches before offset-only scrolling resumes");
        auto replacement = std::make_shared<Label>(L"Replacement");
        replacement->set_preferred_size({300, 1200});
        window.replace_content(*host, replacement);
        flush();
        require(!IsWindow(edit), "ContentHost replacement retires old native editors");
        const auto replaced_measures = root->measurements;
        scroll->set_offset(100);
        flush();
        require(root->measurements == replaced_measures && replacement->bounds().y == scroll->viewport().y - 100,
                "Replacement content participates in offset-only placement");
        trace.verify_passive();
        completed = true;
        window.close();
    });
    const auto result = Application::run(window);
    if (result != 0) std::wcerr << window.error() << '\n';
    require(result == 0 && completed, "Retained scroll fixture completes");
    trace.verify_passive();
}
// A screen reader keeps its COM apartment alive across window changes.
class AutomationRunner {
public:
    AutomationRunner() : thread_([this] {
        const auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        initialization_ = initialized;
        ready_ = true;
        if (SUCCEEDED(initialized)) {
            for (;;) {
                std::function<void()> action;
                {
                    std::unique_lock lock(mutex_);
                    changed_.wait(lock, [&] { return stopped_ || action_; });
                    if (stopped_) break;
                    action = std::move(action_);
                }
                action();
            }
            CoUninitialize();
        }
        exited_ = true;
    }) {
        while (!ready_) std::this_thread::yield();
        check(initialization_, "Initialize persistent automation thread");
    }
    ~AutomationRunner() {
        { std::lock_guard lock(mutex_); stopped_ = true; }
        changed_.notify_one();
        while (!exited_) pump();
        thread_.join();
    }
    void run(std::function<void()> action) {
        { std::lock_guard lock(mutex_); action_ = std::move(action); }
        changed_.notify_one();
    }
private:
    std::mutex mutex_;
    std::condition_variable changed_;
    std::function<void()> action_;
    bool stopped_{};
    std::atomic<bool> ready_{}, exited_{};
    HRESULT initialization_{E_FAIL};
    std::jthread thread_;
};
void scroll_window(int cycle, AutomationRunner& runner) {
    Window window({L"XUI scroll tests", {520, 350}});
    auto root = std::make_shared<Stack>(Axis::vertical);
    root->set_padding({16, 16, 16, 16});
    root->set_spacing(8);
    auto row = std::make_shared<Stack>(Axis::horizontal);
    row->set_spacing(10);
    auto short_text = std::make_shared<Label>(L"Wide W");
    auto narrow_text = std::make_shared<Label>(L"Thin iii");
    auto long_text = std::make_shared<Label>(L"A long label measured through DirectWrite");
    row->add(short_text);
    row->add(narrow_text);
    row->add(long_text);
    root->add(row);
    auto reference_input = std::make_shared<TextInput>(L"Unscrolled reference input");
    root->add(reference_input);
    auto content = std::make_shared<Stack>(Axis::vertical);
    content->set_padding({8, 8, 8, 8});
    content->set_spacing(8);
    content->set_surface(true);
    auto first = std::make_shared<Button>(L"First action");
    content->add(first);
    auto top_input = std::make_shared<TextInput>(L"Top native input");
    top_input->set_text(L"Native text stays inside the viewport");
    content->add(top_input);
    for (int i = 0; i < 6; ++i) content->add(std::make_shared<Toggle>(L"Option " + std::to_wstring(i)));
    auto list = std::make_shared<FileList>(L"Embedded files");
    list->set_preferred_size({320, 80});
    list->set_items(std::make_shared<const std::vector<FileItem>>(
        std::vector<FileItem>{{1, L"Alpha", L"A", false}, {2, L"Beta", L"B", false}, {3, L"Gamma", L"C", false}}));
    content->add(list);
    auto nested = std::make_shared<Stack>(Axis::vertical);
    nested->set_spacing(8);
    auto bottom_input = std::make_shared<TextInput>(L"Bottom native input");
    auto last = std::make_shared<Button>(L"Last action");
    nested->add(bottom_input);
    nested->add(last);
    content->add(nested);
    auto scroll = std::make_shared<ScrollView>(content, L"Form viewport");
    scroll->set_automation_id(L"form-scroll");
    root->add(scroll, 1);
    window.set_content(root);
    std::atomic<int> verified{};
    std::atomic<bool> thumb_done{};
    window.on_key([&](const KeyEvent& event) {
        if (event.key == Key::f4) {
            HWND host{};
            EnumWindows(find, reinterpret_cast<LPARAM>(&host));
            const auto native = FindWindowExW(host, nullptr, L"Xui.Control.1", L"Form viewport");
            RECT client{};
            require(native && GetClientRect(native, &client), "Read native thumb track");
            const int x = client.right - 6;
            // Keep the synthetic gesture on one UI turn, without unrelated
            // physical mouse messages between its down, move, and up.
            SendMessageW(native, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, 5));
            SendMessageW(native, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(x, client.bottom - 2));
            SendMessageW(native, WM_LBUTTONUP, 0, MAKELPARAM(x, client.bottom - 2));
            require(scroll->offset() == scroll->maximum_offset(), "Native thumb gesture updates the offset");
            thumb_done = true;
            return true;
        }
        if (event.key == Key::f1) {
            require(short_text->bounds().width == short_text->measured_text().width, "Short label uses intrinsic text width");
            require(narrow_text->bounds().width == narrow_text->measured_text().width, "Second label uses intrinsic text width");
            require(short_text->measured_text().width != narrow_text->measured_text().width, "DirectWrite measures proportional glyphs");
            long_text->set_text(L"A changed label that is much longer than the available narrow window width");
            ++verified;
            return true;
        }
        if (event.key == Key::f2) {
            require(long_text->bounds().width <= root->bounds().width, "Long label respects narrow viewport");
            require(bottom_input->bounds().width >= 0 && bottom_input->bounds().height >= 0, "Content dimensions stay nonnegative");
            window.set_theme(window.theme() == ThemeMode::dark ? ThemeMode::light :
                window.theme() == ThemeMode::light ? ThemeMode::high_contrast : ThemeMode::dark);
            ++verified;
            return true;
        }
        if (event.key == Key::f3) { scroll->set_enabled(!scroll->enabled()); ++verified; return true; }
        return false;
    });
    std::exception_ptr failure;
    std::atomic<bool> driver_done{};
    runner.run([&] {
        HWND hwnd{};
        try {
            wait([&] { EnumWindows(find, reinterpret_cast<LPARAM>(&hwnd)); return hwnd != nullptr; }, "Find test window");
            require(SetWindowPos(hwnd, HWND_TOPMOST, 40, 40, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE) != 0,
                "Protect the owned scroll test window");
            const auto metric = [&](int index) { return SendMessageW(hwnd, WM_APP + 60, index, 0); };
            wait([&] { return metric(0) > 0 && metric(13) == 11; }, "Populate cached text layouts");
            ComPtr<IUIAutomation> automation;
            check(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation)),
                "Create automation");
            ComPtr<IUIAutomationElement> root_element;
            check(automation->ElementFromHandle(hwnd, &root_element), "Read root");
            auto viewport = named(automation.Get(), root_element.Get(), L"Form viewport");
            auto scroller = pattern<IUIAutomationScrollPattern>(viewport.Get(), UIA_ScrollPatternId);
            auto first_element = named(automation.Get(), root_element.Get(), L"First action");
            auto last_element = named(automation.Get(), root_element.Get(), L"Last action");
            auto top_edit = named(automation.Get(), root_element.Get(), L"Top native input");
            auto bottom_edit = named(automation.Get(), root_element.Get(), L"Bottom native input");
            require(offscreen(bottom_edit.Get()), "Native caption geometry follows the viewport");
            // STATIC labels and EDIT controls share names. Resolve the native EDIT by HWND.
            const auto scroll_hwnd = FindWindowExW(hwnd, nullptr, L"Xui.Control.1", L"Form viewport");
            require(scroll_hwnd != nullptr, "Viewport owns an input HWND");
            const auto scroll_content = FindWindowExW(scroll_hwnd, nullptr, L"Xui.ScrollContent.1", nullptr);
            const auto top_hwnd = FindWindowExW(scroll_content, nullptr, L"EDIT", nullptr);
            const auto bottom_hwnd = FindWindowExW(scroll_content, top_hwnd, L"EDIT", nullptr);
            require(top_hwnd && bottom_hwnd, "Native inputs belong to their viewport");
            top_edit.Reset();
            bottom_edit.Reset();
            check(automation->ElementFromHandle(top_hwnd, &top_edit), "Read top native provider");
            check(automation->ElementFromHandle(bottom_hwnd, &bottom_edit), "Read bottom native provider");
            BOOL vertical{};
            check(scroller->get_CurrentVerticallyScrollable(&vertical), "Read scroll availability");
            require(vertical && percent(scroller.Get()) == 0, "Initial vertical range");
            require(!offscreen(first_element.Get()) && offscreen(last_element.Get()), "Custom offscreen state follows viewport");
            require(offscreen(bottom_edit.Get()), "Clipped native EDIT is offscreen");
            require(metric(11) == 1, "Scroll content uses the single window render target");
            const auto top_full = bounds(top_edit.Get());
            const auto viewport_full = bounds(viewport.Get());
            double view_size{};
            check(scroller->get_CurrentVerticalViewSize(&view_size), "Read viewport fraction");
            const double maximum = (viewport_full.bottom - viewport_full.top) * (100.0 / view_size - 1);
            const double partial_offset = top_full.top - viewport_full.top + (top_full.bottom - top_full.top) / 2.0;
            check(scroller->SetScrollPercent(-1, partial_offset * 100 / maximum), "Partially clip native input");
            const auto partial = bounds(top_edit.Get());
            std::cout << "native_partial full=" << top_full.top << "," << top_full.bottom << " viewport="
                << viewport_full.top << "," << viewport_full.bottom << " partial=" << partial.top << ","
                << partial.bottom << " offscreen=" << offscreen(top_edit.Get()) << " percent=" << percent(scroller.Get()) << '\n';
            require(partial.top == viewport_full.top && partial.bottom <= viewport_full.bottom &&
                partial.bottom - partial.top < top_full.bottom - top_full.top && !offscreen(top_edit.Get()),
                "Native UIA geometry clips partially visible input without hiding it");
            check(scroller->SetScrollPercent(-1, 0), "Restore full input after clipping test");
            const auto cached = metric(12);
            for (int i = 0; i < 12; ++i) {
                check(scroller->Scroll(ScrollAmount_NoAmount, ScrollAmount_SmallIncrement), "Scroll increment");
                RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
            }
            require(metric(12) == cached && metric(13) == 11, "Scroll and paint reuse cached text layouts");
            require(offscreen(first_element.Get()) && !offscreen(last_element.Get()), "Bottom scroll updates custom bounds");
            const auto view_bounds = bounds(viewport.Get());
            const auto last_bounds = bounds(last_element.Get());
            require(last_bounds.top >= view_bounds.top && last_bounds.bottom <= view_bounds.bottom, "UIA bounds are clipped");
            auto list_element = named(automation.Get(), root_element.Get(), L"Embedded files");
            check(list_element->SetFocus(), "Reveal embedded virtual list");
            const auto list_bounds = bounds(list_element.Get());
            require(list_bounds.top >= view_bounds.top && list_bounds.bottom <= view_bounds.bottom,
                "Embedded FileList bounds respect the outer viewport");
            const auto list_hwnd = FindWindowExW(scroll_content, nullptr, L"Xui.FileList.1", nullptr);
            require(list_hwnd != nullptr, "Embedded FileList retains one HWND");
            const auto previous_paints = metric(0);
            SendMessageW(list_hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(20, 15));
            wait([&] { return metric(0) > previous_paints; }, "Nested list hover redraws the shared root target");
            auto reveal_first = pattern<IUIAutomationScrollItemPattern>(first_element.Get(), UIA_ScrollItemPatternId);
            check(reveal_first->ScrollIntoView(), "Reveal first item");
            wait([&] { return percent(scroller.Get()) < 10 && !offscreen(first_element.Get()); }, "Reveal makes first item visible");
            check(last_element->SetFocus(), "Focus reveals last action");
            wait([&] { return percent(scroller.Get()) > 0 && !offscreen(last_element.Get()); }, "Focus reveals offscreen button");
            check(scroller->SetScrollPercent(-1, 0), "Return to top");
            SendMessageW(hwnd, WM_NEXTDLGCTL, reinterpret_cast<WPARAM>(bottom_hwnd), TRUE);
            wait([&] { return percent(scroller.Get()) > 0 && !offscreen(bottom_edit.Get()); }, "Native focus reveals hidden input");
            check(first_element->SetFocus(), "Move focus before native UIA reveal");
            check(scroller->SetScrollPercent(-1, 0), "Reset before native UIA focus");
            check(bottom_edit->SetFocus(), "Native UIA focus reveals input");
            wait([&] { return percent(scroller.Get()) > 0 && !offscreen(bottom_edit.Get()); }, "Native UIA reveal completes");
            auto value = pattern<IUIAutomationValuePattern>(bottom_edit.Get(), UIA_ValuePatternId);
            ComPtr<IUIAutomationElement> reference_edit;
            check(automation->ElementFromHandle(FindWindowExW(hwnd, nullptr, L"EDIT", nullptr), &reference_edit),
                "Read reference native provider");
            ComPtr<IUIAutomationTextPattern> reference_pattern, text_pattern;
            check(reference_edit->GetCurrentPatternAs(UIA_TextPatternId, IID_PPV_ARGS(&reference_pattern)), "Read native text support");
            check(bottom_edit->GetCurrentPatternAs(UIA_TextPatternId, IID_PPV_ARGS(&text_pattern)), "Read clipped native text support");
            require(bool(reference_pattern) == bool(text_pattern), "Geometry override preserves native text pattern support");
            ComPtr<IUIAutomationTextRange> document;
            if (text_pattern) check(text_pattern->get_DocumentRange(&document), "Native text pattern remains available");
            BSTR committed = SysAllocString(L"Committed native text");
            const auto committed_result = value->SetValue(committed);
            SysFreeString(committed);
            check(committed_result, "Native value pattern remains available");
            SendMessageW(bottom_hwnd, EM_SETSEL, 0, -1);
            for (const wchar_t ch : std::wstring(L"Keyboard")) SendMessageW(bottom_hwnd, WM_CHAR, ch, 0);
            BSTR text{};
            check(value->get_CurrentValue(&text), "Read native keyboard value");
            const bool correct = text && std::wstring_view(text) == L"Keyboard";
            SysFreeString(text);
            require(correct, "Native keyboard text survives scrolling");
            if (document) {
                BSTR document_text{};
                check(document->GetText(-1, &document_text), "Read native text document");
                const bool document_correct = document_text && std::wstring_view(document_text) == L"Keyboard";
                SysFreeString(document_text);
                require(document_correct, "Geometry override preserves the native text provider");
            }
            PostMessageW(bottom_hwnd, WM_KEYDOWN, VK_TAB, 0);
            wait([&] { BOOL focused{}; return SUCCEEDED(last_element->get_CurrentHasKeyboardFocus(&focused)) && focused; },
                "Tab reaches the next retained control");
            check(scroller->SetScrollPercent(-1, 0), "Reset offset");
            SendMessageW(top_hwnd, WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(-WHEEL_DELTA)), 0);
            wait([&] { return percent(scroller.Get()) > 0; }, "Wheel over native input scrolls its viewport");
            check(viewport->SetFocus(), "Focus viewport for keyboard");
            PostMessageW(scroll_hwnd, WM_KEYDOWN, VK_END, 0);
            wait([&] { return percent(scroller.Get()) > 99.9; }, "End scrolls to bottom");
            PostMessageW(scroll_hwnd, WM_KEYDOWN, VK_HOME, 0);
            wait([&] { return percent(scroller.Get()) == 0; }, "Home scrolls to top");
            PostMessageW(hwnd, WM_KEYDOWN, VK_F4, 0);
            wait([&] { return thumb_done.load(); }, "Complete the native thumb gesture");
            require(percent(scroller.Get()) > 99.9, "Thumb drag reaches bottom");
            check(scroller->SetScrollPercent(-1, 0), "Reset after drag");
            require(scroller->SetScrollPercent(20, 0) == UIA_E_INVALIDOPERATION, "Horizontal scrolling is unsupported");
            require(scroller->SetScrollPercent(-1, 101) == E_INVALIDARG, "Invalid percent is rejected");
            PostMessageW(hwnd, WM_KEYDOWN, VK_F1, 0);
            wait([&] { return verified.load() == 1; }, "Verify content-based measurement");
            wait([&] { return metric(12) > cached; }, "Text update regenerates the affected layout");
            for (const UINT dpi : {96u, 120u, 144u, 192u}) {
                const auto previous = metric(2);
                RECT suggested{40, 40, 40 + MulDiv(240, dpi, 96), 40 + MulDiv(360, dpi, 96)};
                SendMessageW(hwnd, WM_DPICHANGED, MAKEWPARAM(dpi, dpi), reinterpret_cast<LPARAM>(&suggested));
                wait([&] { return metric(2) > previous; }, "DPI change arranges controls");
                const auto count = verified.load();
                PostMessageW(hwnd, WM_KEYDOWN, VK_F2, 0);
                wait([&] { return verified.load() > count; }, "Verify narrow layout and theme change");
                require(metric(11) == 1, "DPI changes preserve one target");
                check(scroller->SetScrollPercent(-1, 100), "Scroll after DPI change");
                const auto clip = bounds(viewport.Get());
                const auto visible = bounds(last_element.Get());
                require(visible.top >= clip.top && visible.bottom <= clip.bottom, "DPI-scaled UIA clipping");
                if (dpi == 120) {
                    const auto first_caption = FindWindowExW(scroll_content, nullptr, L"STATIC", nullptr);
                    const auto bottom_caption = FindWindowExW(scroll_content, first_caption, L"STATIC", nullptr);
                    require(bottom_caption != nullptr, "Find native caption in contrast mode");
                    RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
                    RECT caption_bounds{};
                    GetClientRect(bottom_caption, &caption_bounds);
                    HDC dc = GetDC(bottom_caption);
                    require(dc != nullptr, "Read native caption pixels");
                    bool ink{};
                    for (int y = 0; y < caption_bounds.bottom && !ink; ++y)
                        for (int x = 0; x < caption_bounds.right; ++x) {
                            const auto pixel = GetPixel(dc, x, y);
                            if (pixel != CLR_INVALID && pixel != GetSysColor(COLOR_WINDOW)) { ink = true; break; }
                        }
                    ReleaseDC(bottom_caption, dc);
                    require(ink, "Shared repaint preserves native caption text in high contrast");
                }
            }
            const auto count = verified.load();
            PostMessageW(hwnd, WM_KEYDOWN, VK_F3, 0);
            wait([&] { return verified.load() > count; }, "Disable viewport");
            require(scroller->Scroll(ScrollAmount_NoAmount, ScrollAmount_SmallIncrement) == UIA_E_ELEMENTNOTENABLED,
                "Disabled scroll rejects UIA actions");
            auto last_invoke = pattern<IUIAutomationInvokePattern>(last_element.Get(), UIA_InvokePatternId);
            require(last_invoke->Invoke() == UIA_E_ELEMENTNOTENABLED, "Disabled viewport also disables descendant actions");
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
            wait([&] { return !IsWindow(hwnd); }, "Destroy viewport tree");
            const auto stale = scroller->Scroll(ScrollAmount_NoAmount, ScrollAmount_SmallIncrement);
            if (stale != UIA_E_ELEMENTNOTAVAILABLE && stale != RPC_E_SERVER_DIED_DNE && stale != RPC_E_DISCONNECTED)
                throw std::runtime_error("Retained scroll provider disconnects: " + std::to_string(stale));
            std::cout << "scroll_cycle=" << cycle << " cached_layouts=11 shared_targets=1\n";
        } catch (...) { failure = std::current_exception(); if (hwnd) PostMessageW(hwnd, WM_CLOSE, 0, 0); }
        driver_done = true;
    });
    const auto result = Application::run(window);
    while (!driver_done) {
        pump();
    }
    if (failure) std::rethrow_exception(failure);
    require(result == 0, "Scroll test window completes");
    require(Drawing::live_targets() == 0, "Scroll destruction releases the shared target");
}
}
int main(int argc, char** argv) {
    const auto initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initialized)) return 1;
    struct Apartment { ~Apartment() { CoUninitialize(); } } apartment;
    try {
        retained_scroll_window();
        if (argc > 1 && std::string_view(argv[1]) == "--retained-only") return 0;
        AutomationRunner runner;
        DWORD gdi{}, user{};
        for (int cycle = 0; cycle < 8; ++cycle) {
            scroll_window(cycle, runner);
            std::atomic<bool> drained{};
            dispose_later(std::shared_ptr<const void>(new int, [&](const void* pointer) {
                delete static_cast<const int*>(pointer);
                drained = true;
            }));
            wait([&] { return drained.load(); }, "Drain retained virtual-list snapshots");
            const auto settle = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
            while (std::chrono::steady_clock::now() < settle) {
                pump();
            }
            DWORD current{};
            GetProcessHandleCount(GetCurrentProcess(), &current);
            const auto current_gdi = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
            const auto current_user = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
            std::cout << "scroll_resources handles=" << current << " gdi=" << current_gdi << " user=" << current_user << '\n';
            if (cycle == 3) { gdi = current_gdi; user = current_user; }
            // Process handles include this UIA client's asynchronous thread pools.
            // The server-only resource cases in window_tests enforce the handle bound.
            if (cycle > 3) require(current_gdi <= gdi + 2 && current_user <= user + 2,
                "Repeated scroll trees do not retain GDI or USER objects");
        }
        std::cout << "Scroll and sizing desktop tests passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
