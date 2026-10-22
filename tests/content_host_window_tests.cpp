#include "xui/application.hpp"
#include "xui/documents.hpp"
#include "xui/map_view.hpp"
#include "xui/runtime_hosts.hpp"
#include "xui/reveal.hpp"
#include "../src/content_inspection.hpp"
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
    OwnedChildren owned_children() const override { return OwnedChildren{children_}; }
private:
    std::array<std::shared_ptr<Element>, 2> children_;
};
class WrongRole final : public Control {
public:
    WrongRole() : Control(ControlRole::button, L"Not a button", {20, 20}) {}
};
class InvalidChildren final : public Control {
public:
    InvalidChildren() : Control(ControlRole::content_view, L"Invalid topology", {20, 20}) {}
    OwnedChildren owned_children() const override { return OwnedChildren{children}; }
    std::vector<std::shared_ptr<Element>> children;
};
void owned_model_contract() {
    Window window;
    auto host = std::make_shared<ContentHost>();
    window.set_content(host);
    auto pages = std::make_shared<PageView>();
    auto selected = std::make_shared<Button>(L"Selected");
    auto hidden = std::make_shared<Button>(L"Hidden");
    pages->add(selected); pages->add(hidden);
    const Rect clip{0, 0, 800, 600};
    pages->arrange(clip);
    check(inspection::children(pages, clip, true).size() == 1 &&
        inspection::children(pages, clip, true)[0].element == selected, "Inspection follows the selected page");
    const auto registration = inspection::Registration::prepare(pages, {{0, pages}, {1, selected}, {2, hidden}}, [](auto) {});
    check(registration.keys.size() == 3 &&
        inspection::hit(pages, {20, 20}, clip, registration, nullptr).key == 1, "Inactive pages register but cannot win hit testing");
    auto expander = std::make_shared<Expander>(L"Closed", pages); expander->set_expanded(false);
    auto split = std::make_shared<SplitView>(expander, std::make_shared<TextInput>(L"Second"));
    split->set_layout(Axis::vertical, 100); split->arrange(clip);
    check(inspection::children(split, clip, true)[0].clip.height == split->first_pane_area().height &&
        inspection::children(split, clip, true)[1].clip.y == split->second_pane_area().y, "Inspection clips along the SplitView axis");
    split->set_secondary_visible(false); split->arrange(clip);
    check(inspection::children(split, clip, true).size() == 1 && split->owned_children().size() == 2, "Collapsed panes are still owned");
    check(inspection::children(expander, clip, true).size() == 0 &&
        inspection::children(expander, clip, false).size() == 1, "Closed expanders distinguish ownership from hit testing");
    auto reveal = std::make_shared<Reveal>(std::make_shared<ContentView>(std::make_shared<ScrollView>(split)));
    window.replace_content(*host, reveal, {{0, reveal}, {1, hidden}}, [](auto) {});
    check(!reveal->open() && !expander->expanded(), "Replacement preflight does not open lazy content");
    std::weak_ptr<Element> retired = hidden;
    pages.reset(); selected.reset(); hidden.reset(); expander.reset(); split.reset(); reveal.reset();
    window.replace_content(*host, {});
    check(retired.expired(), "Replacement releases hidden descendants and weak inspection metadata");
    auto cycle = std::make_shared<InvalidChildren>();
    cycle->children.push_back(cycle);
    rejects<std::invalid_argument>([&] { window.replace_content(*host, cycle); }, "Custom cycles still fail preflight");
    cycle->children.clear();
    cycle->children.push_back({});
    rejects<std::invalid_argument>([&] { window.replace_content(*host, cycle); }, "Null owned children still fail preflight");
    check(!host->content(), "Invalid topology never changes the host");
}
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
void owned_deferred_contract(VisualStyle style) {
    WindowOptions options; options.title = L"XUI owned deferred children"; options.show_activated = false;
    options.visual_style = style;
    Window window(options);
    auto pages = std::make_shared<PageView>();
    auto active = std::make_shared<TextInput>(L"First editor"); active->set_text(L"first draft");
    auto hidden = std::make_shared<TextInput>(L"Second editor"); hidden->set_text(L"second draft");
    auto first = std::make_shared<ContentHost>(active), second = std::make_shared<ContentHost>(hidden);
    pages->add(first); pages->add(second);
    auto deferred = std::make_shared<TextInput>(L"Deferred editor"); deferred->set_text(L"deferred draft");
    auto lazy_host = std::make_shared<ContentHost>(std::make_shared<ContentView>(
        std::make_shared<ScrollView>(std::make_shared<SplitView>(deferred, std::make_shared<Label>(L"Other pane")))));
    auto reveal = std::make_shared<Reveal>(lazy_host);
    auto root = std::make_shared<Stack>(Axis::vertical);
    root->add(pages, 1); root->add(reveal, 1);
    window.set_content(root);
    const auto exists = [](HWND owner, std::wstring_view name) {
        for (const auto child : descendants(owner)) if (text(child) == name) return true;
        return false;
    };
    HWND owner{}, first_hwnd{}, second_hwnd{};
    std::size_t retained_count{};
    bool ran{};
    const auto foreground = GetForegroundWindow();
    window.post([&] {
        owner = frame(options.title);
        first_hwnd = named(owner, L"first draft");
        check(!exists(owner, L"Second editor") && !exists(owner, L"Deferred editor") &&
            !exists(owner, L"second draft") && !exists(owner, L"deferred draft"),
            "Initial realization excludes inactive pages and unopened nested Reveal");
        check(deferred->visual_style() == style, "Unopened Reveal propagates presentation without HWNDs");
        const auto count = descendants(owner).size();
        for (int i = 0; i < 100; ++i) {
            std::size_t visits{};
            const auto walk = [&](const auto& self, const Element& element) -> void {
                ++visits;
                for (const auto& child : element.owned_children()) self(self, *child);
            };
            walk(walk, *root);
            check(visits == 15 && descendants(owner).size() == count, "Ownership enumeration never realizes lazy peers");
        }
        SendMessageW(first_hwnd, EM_SETSEL, 0, 0);
        SendMessageW(first_hwnd, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"edited "));
        SendMessageW(first_hwnd, EM_SETSEL, 1, 4);
        pages->select(1);
        window.post([&] {
            second_hwnd = named(owner, L"second draft");
            check(IsWindow(first_hwnd) && !IsWindowVisible(first_hwnd), "Visited inactive editor retains its native peer");
            retained_count = descendants(owner).size();
            auto replacement = std::make_shared<TextInput>(L"Replacement first"); replacement->set_text(L"replacement draft");
            const std::weak_ptr<Element> retired = active;
            active.reset();
            window.replace_content(*first, replacement);
            check(retired.expired() && !IsWindow(first_hwnd), "Inactive replacement retires the old model and HWND");
            check(!IsWindowVisible(named(owner, L"replacement draft")) && IsWindow(second_hwnd) &&
                text(second_hwnd) == L"second draft", "Inactive replacement preserves the active native editor");
            check(descendants(owner).size() == retained_count, "Inactive replacement keeps peer resources bounded");
            window.replace_content(*lazy_host, std::make_shared<TextInput>(L"Replaced deferred"));
            check(exists(owner, L"Replaced deferred") && !exists(owner, L"Deferred editor"),
                "Explicit replacement inside closed Reveal keeps its established materialization exception");
            reveal->set_open(true);
            window.post([&] {
                const auto lazy = named(owner, L"Replaced deferred");
                check(IsWindowVisible(lazy), "Opening Reveal presents the already replaced native child");
                const auto opened_count = descendants(owner).size();
                reveal->set_open(false);
                window.post([&, lazy, opened_count] {
                    check(IsWindow(lazy) && !IsWindowVisible(lazy) && descendants(owner).size() == opened_count,
                        "Closing Reveal retains visited peers without additional resources");
                    check(GetForegroundWindow() == foreground, "Traversal and lazy replacement never activate a window");
                    ran = true; window.close();
                });
            });
        });
    });
    const auto result = Application::run(window);
    if (result) std::wcerr << window.error() << '\n';
    check(result == 0 && ran, "Nested ownership/native realization contract completes");
}
class FailingLayout final : public Stack {
public:
    FailingLayout() : Stack(Axis::vertical) { add(std::make_shared<Button>(L"Materialized before failure")); }
    Size measure(Size) override { throw std::runtime_error("Injected replacement layout failure"); }
};
class TypographyProbe final : public Stack {
public:
    explicit TypographyProbe(std::size_t& calls) : Stack(Axis::vertical), calls_(calls) {}
protected:
    std::optional<StyleTarget> control_style_target() const override {
        ++calls_;
        return Stack::control_style_target();
    }
private:
    std::size_t& calls_;
};
void active_collection_contract() {
    std::size_t previous_active_calls{};
    for (const int inactive_pages : {1, 16}) {
        std::size_t active_calls{}, inactive_calls{};
        WindowOptions options; options.show_activated = false; options.title = L"XUI bounded page collection";
        Window window(options);
        window.set_presentation("Segoe UI", 14, false, false);
        auto pages = std::make_shared<PageView>();
        auto active = std::make_shared<TypographyProbe>(active_calls);
        auto button = std::make_shared<Button>(L"Active interaction");
        active->add(button); pages->add(active);
        for (int page = 0; page < inactive_pages; ++page) {
            auto inactive = std::make_shared<TypographyProbe>(inactive_calls);
            for (int item = 0; item < 128; ++item) inactive->add(std::make_shared<Label>(L"Unvisited"));
            pages->add(inactive);
        }
        window.set_content(pages);
        bool ran{};
        window.post([&] {
            const auto owner = frame(options.title);
            const auto count = descendants(owner).size();
            check(count == 1, "Inactive population adds no native peers");
            active_calls = inactive_calls = 0;
            for (int i = 0; i < 32; ++i) {
                button->pointer_move(i % 2 == 0);
                SendMessageW(owner, WM_APP + 12, 0, 0);
            }
            check(active_calls > 0 && inactive_calls == 0, "Ordinary active interactions never collect or apply typography to inactive pages");
            check(!previous_active_calls || active_calls == previous_active_calls, "Active collection work is independent of inactive population");
            check(descendants(owner).size() == count, "Active interactions do not realize unvisited peers");
            previous_active_calls = active_calls;
            std::cout << "active_collection inactive_pages=" << inactive_pages << " inactive_items=" << inactive_pages * 128
                << " interactions=32 active_typography_calls=" << active_calls << " inactive_typography_calls=" << inactive_calls
                << " hwnds=" << count << '\n';
            ran = true; window.close();
        });
        const auto result = Application::run(window);
        if (result) std::wcerr << window.error() << '\n';
        check(result == 0 && ran, "Bounded active PageView collection completes");
    }
}
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
int main(int argc, char** argv) {
    const std::string_view mode = argc > 1 ? argv[1] : "";
    if (argc > 2 || (!mode.empty() && mode != "--model-only" && mode != "--owned-only") ||
        (argc == 2 && mode.empty())) {
        std::cerr << "Usage: xui_content_host_window_tests [--model-only|--owned-only]\n";
        return 2;
    }
    try {
        model_contract();
        owned_model_contract();
        if (mode == "--model-only") {
            std::cout << "ContentHost model ownership, replacement and inspection passed without HWND realization\n";
            return 0;
        }
        if (mode == "--owned-only") {
            owned_deferred_contract(VisualStyle::classic);
            owned_deferred_contract(VisualStyle::winui);
            active_collection_contract();
            std::cout << "Owned children: deferred native peers, inactive replacement and bounded active collection passed\n";
            return 0;
        }
        native_contract(VisualStyle::classic);
        native_contract(VisualStyle::winui);
        provider_contract();
        inactive_page_contract();
        owned_deferred_contract(VisualStyle::classic);
        owned_deferred_contract(VisualStyle::winui);
        active_collection_contract();
        failure_contract();
        std::cout << "ContentHost: model, 200 native replacements, editor state, popup, map/runtime retirement, UIA, fatal failure passed\n";
        return 0;
    } catch (const std::exception& failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
