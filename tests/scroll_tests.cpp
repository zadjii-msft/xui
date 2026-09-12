#include "xui/application.hpp"
#include "../src/drawing.hpp"
#include "../src/list_peer.hpp"
#include <windows.h>
#include <UIAutomation.h>
#include <wrl/client.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>

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
            const auto top_hwnd = FindWindowExW(scroll_hwnd, nullptr, L"EDIT", nullptr);
            const auto bottom_hwnd = FindWindowExW(scroll_hwnd, top_hwnd, L"EDIT", nullptr);
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
            const auto list_hwnd = FindWindowExW(scroll_hwnd, nullptr, L"Xui.FileList.1", nullptr);
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
                    const auto first_caption = FindWindowExW(scroll_hwnd, nullptr, L"STATIC", nullptr);
                    const auto bottom_caption = FindWindowExW(scroll_hwnd, first_caption, L"STATIC", nullptr);
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
int main() {
    const auto initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initialized)) return 1;
    struct Apartment { ~Apartment() { CoUninitialize(); } } apartment;
    try {
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
