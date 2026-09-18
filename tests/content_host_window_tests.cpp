#include "xui/application.hpp"
#include "xui/documents.hpp"
#include "xui/map_view.hpp"
#include "xui/runtime_hosts.hpp"
#include "native_focus_diagnostics.hpp"
#include <windows.h>
#include <ole2.h>
#include <UIAutomation.h>
#include <richedit.h>
#include <wrl/client.h>
#include <future>
#include <iostream>
#include <thread>

namespace {
using namespace xui;
using Microsoft::WRL::ComPtr;
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<class Error, class F> void rejects(F callback, const char* message) {
    try { callback(); } catch (const Error&) { return; }
    throw std::runtime_error(message);
}
std::vector<HWND> descendants(HWND root) {
    std::vector<HWND> result;
    EnumChildWindows(root, [](HWND window, LPARAM data) -> BOOL {
        reinterpret_cast<std::vector<HWND>*>(data)->push_back(window);
        return TRUE;
    }, reinterpret_cast<LPARAM>(&result));
    return result;
}
std::wstring text(HWND window) {
    std::wstring value(GetWindowTextLengthW(window) + 1, L'\0');
    value.resize(GetWindowTextW(window, value.data(), static_cast<int>(value.size())));
    return value;
}
HWND named(HWND root, const std::wstring& name) {
    for (auto child : descendants(root)) if (text(child) == name) return child;
    throw std::runtime_error("Expected HWND name was not found");
}
HWND native(HWND root, const wchar_t* expected) {
    for (auto child : descendants(root)) {
        wchar_t name[128]{};
        GetClassNameW(child, name, 128);
        if (_wcsicmp(name, expected) == 0) return child;
    }
    throw std::runtime_error("Expected native editor HWND was not found");
}
HWND frame(const std::wstring& title) {
    const auto window = FindWindowW(L"Xui.Window.1", title.c_str());
    check(window != nullptr, "Owned window exists");
    return window;
}
std::shared_ptr<ContentView> preview(unsigned index) {
    auto children = std::make_shared<Stack>(Axis::vertical);
    children->add(std::make_shared<Button>(L"Preview button " + std::to_wstring(index)));
    children->add(std::make_shared<Label>(L"Preview label " + std::to_wstring(index)));
    return std::make_shared<ContentView>(children, L"Preview pane " + std::to_wstring(index));
}
class DuplicateChildren final : public Control {
public:
    DuplicateChildren() : Control(ControlRole::content_view, L"Duplicate", {20, 20}) {
        children_[0] = std::make_shared<Label>(L"Duplicated child");
        children_[1] = children_[0];
    }
    std::span<const std::shared_ptr<Element>> retained_children() const override { return children_; }
private:
    std::array<std::shared_ptr<Element>, 2> children_;
};
class WrongRole final : public Control {
public:
    WrongRole() : Control(ControlRole::button, L"Not a button", {20, 20}) {}
};
void model_contract() {
    Window window;
    auto old = std::make_shared<Label>(L"Old");
    auto host = std::make_shared<ContentHost>(old);
    auto root = std::make_shared<Stack>(Axis::vertical);
    root->add(host, 1);
    window.set_content(root);
    check(host->content() == old && host->child_count() == 1, "Initial single content");
    unsigned invalidations{};
    root->set_invalidator([&](Invalidation) { ++invalidations; });
    rejects<std::logic_error>([&] { static_cast<Stack&>(*host).add(std::make_shared<Label>(L"Bypass")); },
        "Stack base cannot append to a ContentHost");
    rejects<std::invalid_argument>([&] { window.replace_content(*host, root); }, "Reject ancestor cycle");
    rejects<std::invalid_argument>([&] { window.replace_content(*host, host); }, "Reject self cycle");
    rejects<std::invalid_argument>([&] { window.replace_content(*host, std::make_shared<Element>()); },
        "Reject unsupported root before mutation");
    auto invalid_tree = std::make_shared<Stack>(Axis::vertical);
    invalid_tree->add(std::make_shared<Element>());
    rejects<std::invalid_argument>([&] { window.replace_content(*host, invalid_tree); }, "Validate all candidate descendants");
    rejects<std::invalid_argument>([&] { window.replace_content(*host, std::make_shared<DuplicateChildren>()); },
        "Reject duplicate retained children");
    rejects<std::invalid_argument>([&] { window.replace_content(*host, std::make_shared<WrongRole>()); },
        "Reject unsupported native role before mutation");
    auto other = std::make_shared<Stack>(Axis::vertical);
    auto adopted = std::make_shared<Label>(L"Other parent");
    other->add(adopted);
    rejects<std::invalid_argument>([&] { window.replace_content(*host, adopted); }, "Reject already adopted candidate");
    ContentHost foreign;
    rejects<std::invalid_argument>([&] { window.replace_content(foreign, {}); }, "Reject unattached host even for no-op");
    Window second;
    second.set_content(std::make_shared<Stack>(Axis::vertical));
    rejects<std::invalid_argument>([&] { second.replace_content(*host, {}); }, "Reject foreign window host");
    check(host->content() == old && invalidations == 0, "Invalid candidates preserve original model");
    bool wrong_thread{};
    std::thread worker([&] {
        try { window.replace_content(*host, {}); } catch (const std::logic_error&) { wrong_thread = true; }
    });
    worker.join();
    check(wrong_thread && host->content() == old, "Reject replacement on worker thread");
    auto next = std::make_shared<Label>(L"Next");
    window.replace_content(*host, next);
    check(host->content() == next && invalidations == 1, "Replace before run");
    old->invalidate(Invalidation::layout);
    check(invalidations == 1, "Retired child no longer invalidates the host");
    other->add(old);
    next->invalidate(Invalidation::layout);
    check(invalidations == 2, "Replacement adopts invalidation parent");
    window.replace_content(*host, next);
    check(invalidations == 2, "Replacing identical content is a no-op");
    window.replace_content(*host, {});
    check(!host->content() && host->child_count() == 0, "Null clears before run");
    other->add(next);
    root->set_invalidator({});
    window.close();
    rejects<std::logic_error>([&] { window.replace_content(*host, {}); }, "Reject closed window");
}
void native_contract(VisualStyle style) {
    native_focus_diagnostics::Trace trace(style == VisualStyle::winui ? "ContentHost WinUI" : "ContentHost Classic");
    WindowOptions options;
    options.title = L"XUI ContentHost native contract";
    options.size = {900, 650};
    options.show_activated = false;
    options.visual_style = style;
    Window window(options);
    auto root = std::make_shared<Stack>(Axis::horizontal);
    auto outside = std::make_shared<Stack>(Axis::vertical);
    auto editor = std::make_shared<MultilineText>(L"Outside document");
    editor->set_text(L"Retained document");
    auto input = std::make_shared<TextInput>(L"Outside input");
    input->set_text(L"Retained input");
    auto captured_button = std::make_shared<Button>(L"Outside captured button");
    outside->add(captured_button);
    outside->add(input);
    outside->add(editor, 1);
    auto host = std::make_shared<ContentHost>(preview(0));
    root->add(outside, 1);
    root->add(host, 1);
    window.set_content(root);
    const auto foreground = GetForegroundWindow();
    bool ran{}, input_rejected{}, document_rejected{}, recursion_rejected{};
    input->on_change([&](const auto&) {
        rejects<std::logic_error>([&] { window.replace_content(*host, {}); }, "Reject native EDIT callback replacement");
        input_rejected = true;
    });
    editor->on_change([&](const auto&) {
        rejects<std::logic_error>([&] { window.replace_content(*host, {}); }, "Reject native RichEdit callback replacement");
        document_rejected = true;
    });
    check(window.post([&] {
        const auto hwnd = frame(options.title);
        check(GetForegroundWindow() == foreground, "Initial show does not activate");
        const auto edit_hwnd = native(hwnd, L"RICHEDIT50W");
        const auto input_hwnd = native(hwnd, L"EDIT");
        SendMessageW(edit_hwnd, EM_SETSEL, 0, 0);
        SendMessageW(edit_hwnd, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"Edited "));
        SendMessageW(edit_hwnd, EM_SETSEL, 2, 6);
        SendMessageW(input_hwnd, EM_SETSEL, 0, 0);
        SendMessageW(input_hwnd, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"Edited "));
        SendMessageW(input_hwnd, EM_SETSEL, 1, 4);
        check(input_rejected && document_rejected, "Both native input paths reject synchronous replacement");
        const bool active_owner = GetForegroundWindow() == hwnd;
        if (active_owner) SetFocus(edit_hwnd);
        check((!active_owner || GetFocus() == edit_hwnd) && GetForegroundWindow() == foreground,
            "Native editor focus requires an already active owner");
        const auto captured_hwnd = named(hwnd, L"Outside captured button");
        const auto document_value = text(edit_hwnd), input_value = text(input_hwnd);
        const auto focus_before = GetFocus();
        const auto count = descendants(hwnd).size();
        RECT outside_bounds{}; GetWindowRect(edit_hwnd, &outside_bounds);
        check(SendMessageW(edit_hwnd, EM_CANUNDO, 0, 0), "Document has real native undo history");
        check(SendMessageW(input_hwnd, EM_CANUNDO, 0, 0), "Input has real native undo history");
        for (unsigned i = 1; i <= 100; ++i) {
            const auto previous = host->content();
            auto candidate = preview(i);
            trace.during("replace ContentHost", [&] { window.replace_content(*host, candidate); });
            check(descendants(hwnd).size() == count, "One hundred replacements have bounded HWND count");
            check(native(hwnd, L"RICHEDIT50W") == edit_hwnd && native(hwnd, L"EDIT") == input_hwnd,
                "Outside editor HWNDs are stable");
            check(text(edit_hwnd) == document_value && text(input_hwnd) == input_value, "Outside text is unchanged");
            DWORD start{}, end{};
            SendMessageW(edit_hwnd, EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));
            check(start == 2 && end == 6, "Native document selection is unchanged");
            SendMessageW(input_hwnd, EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));
            check(start == 1 && end == 4, "Native input selection is unchanged");
            check(SendMessageW(edit_hwnd, EM_CANUNDO, 0, 0) && SendMessageW(input_hwnd, EM_CANUNDO, 0, 0),
                "Outside undo history survives replacement");
            RECT bounds{}; GetWindowRect(edit_hwnd, &bounds);
            check(EqualRect(&outside_bounds, &bounds), "Outside native editor bounds are stable");
            auto button = named(hwnd, L"Preview button " + std::to_wstring(i));
            GetWindowRect(button, &bounds);
            check(bounds.right > bounds.left + 1 && bounds.bottom > bounds.top + 1,
                "Replacement returns after actual native layout");
            const auto actual_focus = GetFocus(), actual_foreground = GetForegroundWindow();
            if (actual_focus != focus_before || actual_foreground != foreground)
                std::cerr << "Replacement " << i << " owner=" << hwnd << " focus=" << focus_before
                    << " -> " << actual_focus << " foreground=" << foreground << " -> " << actual_foreground << '\n';
            check(actual_focus == focus_before && actual_foreground == foreground, "Replacement does not change focus or foreground");
            auto recovered = std::make_shared<Stack>(Axis::vertical);
            recovered->add(previous);
            previous->measure({300, 100});
        }
        if (active_owner) {
            SendMessageW(captured_hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(10, 10));
            check(GetCapture() == captured_hwnd, "Outside button owns real pointer capture");
            window.replace_content(*host, preview(101));
            check(GetCapture() == captured_hwnd, "Replacement preserves outside pointer capture");
            SendMessageW(captured_hwnd, WM_LBUTTONUP, 0, MAKELPARAM(10, 10));
        }
        const auto current = host->content();
        rejects<std::invalid_argument>([&] { window.replace_content(*host, std::make_shared<Element>()); },
            "Invalid running candidate rejected");
        check(host->content() == current && descendants(hwnd).size() == count, "Invalid running candidate preserves native tree");
        auto anchor = std::dynamic_pointer_cast<Button>(
            std::dynamic_pointer_cast<Stack>(std::dynamic_pointer_cast<ContentView>(current)->content())->child_at(0));
        auto popup = std::make_shared<Popup>(std::make_shared<Button>(L"Popup child"));
        const std::weak_ptr<Popup> weak_popup = popup;
        popup->on_dismiss([&, weak_popup, anchor](PopupDismissReason) {
            rejects<std::logic_error>([&] { window.replace_content(*host, {}); }, "Reject recursive popup dismissal replacement");
            rejects<std::invalid_argument>([&] { window.show_popup(weak_popup.lock(), *anchor); }, "Retired anchor cannot reopen its popup");
            check(!window.focus(*anchor), "Retired anchor cannot regain focus from a dismissal callback");
            recursion_rejected = true;
        });
        window.show_popup(popup, *anchor);
        check(popup->is_open(), "Retiring subtree has an open popup");
        window.replace_content(*host, {});
        check(!popup->is_open() && recursion_rejected, "Retirement dismisses anchored popups without recursive replacement");
        check(descendants(hwnd).size() == count - 3, "Clear immediately reclaims preview and popup HWNDs");
        check(GetFocus() == (active_owner ? nullptr : focus_before),
            "Retirement clears active popup focus or preserves background focus");
        check(GetForegroundWindow() == foreground, "Popup retirement does not activate another window");
        SendMessageW(edit_hwnd, EM_UNDO, 0, 0);
        check(text(edit_hwnd) == L"Retained document", "Preserved document undo works");
        SendMessageW(input_hwnd, EM_UNDO, 0, 0);
        check(text(input_hwnd) == L"Retained input", "Preserved input undo works");
        auto resources = std::make_shared<Stack>(Axis::vertical);
        auto map = std::make_shared<MapView>();
        auto media = std::make_shared<MediaPlayback>();
        auto web = std::make_shared<WebContent>();
        resources->add(map, 1); resources->add(media, 1); resources->add(web, 1);
        window.replace_content(*host, resources);
        const auto request = map->request_overlay();
        window.replace_content(*host, {});
        check(request.stop.stop_requested() && !map->complete(request, {}), "Retired map requests are cancelled and reject stale delivery");
        check(descendants(hwnd).size() == count - 3, "Native runtime visuals are reclaimed with their peers");
        ran = true;
        window.close();
    }), "Post before run is accepted");
    const auto result = Application::run(window);
    if (result) std::wcerr << window.error() << L'\n';
    check(result == 0 && ran, "Native replacement contract completes");
    rejects<std::logic_error>([&] { window.replace_content(*host, {}); }, "Replacement after shutdown rejected");
    check(!window.post([] {}), "Shutdown rejects future work");
    trace.verify_passive();
}
void provider_contract() {
    WindowOptions options;
    options.title = L"XUI ContentHost provider contract";
    options.show_activated = false;
    Window window(options);
    auto host = std::make_shared<ContentHost>(std::make_shared<Button>(L"Retired provider"));
    window.set_content(host);
    std::thread client;
    std::exception_ptr failure;
    std::promise<void> retired;
    auto retirement = retired.get_future();
    bool unavailable{};
    window.post([&] {
        const auto button = named(frame(options.title), L"Retired provider");
        client = std::thread([&, button] {
            const auto hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            try {
                check(SUCCEEDED(hr), "Initialize provider test client");
                ComPtr<IUIAutomation> automation;
                check(SUCCEEDED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                    IID_PPV_ARGS(&automation))), "Create automation client");
                ComPtr<IUIAutomationElement> element;
                check(SUCCEEDED(automation->ElementFromHandle(button, &element)) && element, "Get actual old provider");
                window.post([&] {
                    window.replace_content(*host, {});
                    retired.set_value();
                });
                check(retirement.wait_for(std::chrono::seconds(20)) == std::future_status::ready, "Wait for native retirement");
                BSTR name{};
                const auto result = element->get_CurrentName(&name);
                SysFreeString(name);
                unavailable = result == UIA_E_ELEMENTNOTAVAILABLE;
                check(unavailable, "Retired provider reports UIA_E_ELEMENTNOTAVAILABLE");
            } catch (...) { failure = std::current_exception(); }
            if (SUCCEEDED(hr)) CoUninitialize();
            window.post([&] { window.close(); });
        });
    });
    const auto result = Application::run(window);
    if (client.joinable()) client.join();
    if (failure) std::rethrow_exception(failure);
    check(result == 0 && unavailable, "Provider invalidation completes");
}
void inactive_page_contract() {
    WindowOptions options;
    options.title = L"XUI inactive ContentHost";
    options.show_activated = false;
    Window window(options);
    auto pages = std::make_shared<PageView>();
    auto first = std::make_shared<Label>(L"Selected page");
    auto host = std::make_shared<ContentHost>();
    pages->add(first);
    pages->add(host);
    window.set_content(pages);
    bool ran{};
    window.post([&] {
        const auto hwnd = frame(options.title);
        const auto selected = named(hwnd, L"Selected page");
        window.replace_content(*host, std::make_shared<Button>(L"Inactive content"));
        const auto inactive = named(hwnd, L"Inactive content");
        check(!IsWindowVisible(inactive), "Inactive host is materialized without showing its child");
        check(IsWindowVisible(selected), "Inactive replacement preserves the selected page");
        window.replace_content(*host, {});
        check(descendants(hwnd).size() == 1, "Inactive clear immediately retires its native child");
        ran = true;
        window.close();
    });
    check(Application::run(window) == 0 && ran, "Inactive page replacement completes");
}
class FailingLayout final : public Stack {
public:
    FailingLayout() : Stack(Axis::vertical) { add(std::make_shared<Button>(L"Materialized before failure")); }
    Size measure(Size) override { throw std::runtime_error("Injected replacement layout failure"); }
};
void failure_contract() {
    WindowOptions options; options.show_activated = false;
    Window window(options);
    auto host = std::make_shared<ContentHost>(std::make_shared<Label>(L"Old"));
    window.set_content(host);
    bool failed{};
    window.post([&] {
        try { window.replace_content(*host, std::make_shared<FailingLayout>()); }
        catch (const std::runtime_error&) { failed = true; }
        rejects<std::logic_error>([&] { window.replace_content(*host, {}); }, "Materialization failure closes the window");
    });
    check(Application::run(window) == 1 && failed && !window.error().empty(), "Native failure is explicit and fatal");
}
}
int main() {
    try {
        model_contract();
        native_contract(VisualStyle::classic);
        native_contract(VisualStyle::winui);
        provider_contract();
        inactive_page_contract();
        failure_contract();
        std::cout << "ContentHost: model, 200 native replacements, editor state, popup, map/runtime retirement, UIA, fatal failure passed\n";
        return 0;
    } catch (const std::exception& failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
