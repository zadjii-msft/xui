#include "../src/content_inspection.hpp"
#include "../src/control_accessibility.hpp"
#include <windowsx.h>
#include <richedit.h>
#include <deque>
#include <iostream>
#include <thread>

namespace {
using namespace xui;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class Error, class F> void rejects(F&& operation, const char* message) {
    try { operation(); } catch (const Error&) { return; }
    throw std::runtime_error(message);
}
std::vector<HWND> descendants(HWND parent) {
    std::vector<HWND> result;
    EnumChildWindows(parent, [](HWND hwnd, LPARAM data) -> BOOL {
        reinterpret_cast<std::vector<HWND>*>(data)->push_back(hwnd); return TRUE;
    }, reinterpret_cast<LPARAM>(&result));
    return result;
}
std::wstring text(HWND hwnd) {
    std::wstring value(GetWindowTextLengthW(hwnd) + 1, L'\0');
    value.resize(GetWindowTextW(hwnd, value.data(), static_cast<int>(value.size())));
    return value;
}
HWND named(HWND parent, const std::wstring& name) {
    for (const auto hwnd : descendants(parent)) if (text(hwnd) == name) return hwnd;
    throw std::runtime_error("Named native control not found");
}
HWND klass(HWND parent, const wchar_t* expected, int index = 0) {
    for (const auto hwnd : descendants(parent)) {
        wchar_t value[128]{}; GetClassNameW(hwnd, value, 128);
        if (_wcsicmp(value, expected) == 0 && index-- == 0) return hwnd;
    }
    throw std::runtime_error("Native editor not found");
}
Point center(Rect bounds) { return {bounds.x + bounds.width / 2, bounds.y + bounds.height / 2}; }
Point client_point(HWND window, HWND child) {
    RECT rect{}; GetWindowRect(child, &rect);
    POINT point{(rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2};
    ScreenToClient(window, &point);
    const auto scale = 96.0f / GetDpiForWindow(window);
    return {point.x * scale, point.y * scale};
}
void click(HWND window, HWND child, Point point, UINT down = WM_LBUTTONDOWN) {
    const auto scale = GetDpiForWindow(window) / 96.0f;
    POINT pixel{static_cast<LONG>(std::lround(point.x * scale)), static_cast<LONG>(std::lround(point.y * scale))};
    MapWindowPoints(window, child, &pixel, 1);
    const auto position = MAKELPARAM(pixel.x, pixel.y);
    SendMessageW(child, down, MK_LBUTTON, position);
    SendMessageW(child, WM_MOUSEMOVE, MK_LBUTTON, position);
    SendMessageW(child, WM_LBUTTONUP, 0, position);
}
struct Fixture {
    WindowOptions options;
    Window window;
    std::shared_ptr<ContentHost> host = std::make_shared<ContentHost>();
    std::shared_ptr<TextInput> outside = std::make_shared<TextInput>(L"Outside editor");
    std::vector<std::uint32_t> picks;
    std::deque<std::function<void()>> steps;
    HWND hwnd{}, outside_hwnd{}, foreground{};
    explicit Fixture(VisualStyle style = VisualStyle::classic) :
        options{L"XUI pointer inspection", {960, 760}, ThemeMode::dark, {}, false, style, false}, window(options) {
        auto root = std::make_shared<Stack>(Axis::horizontal);
        outside->set_text(L"Outside selection remains");
        outside->set_fixed_size({260, 68});
        root->add(outside);
        root->add(host, 1);
        window.set_content(root);
    }
    void replace(const std::shared_ptr<Element>& root, std::vector<ContentInspectionTarget> targets) {
        window.replace_content(*host, root, std::move(targets), [this](std::uint32_t key) { picks.push_back(key); });
    }
    void run() {
        foreground = GetForegroundWindow();
        std::function<void()> advance;
        advance = [&] {
            if (steps.empty()) { window.close(); return; }
            auto action = std::move(steps.front()); steps.pop_front();
            action();
            if (!steps.empty()) check(window.post(advance), "Queue next inspection test step");
            else window.close();
        };
        window.post([&] {
            hwnd = FindWindowW(L"Xui.Window.1", options.title.c_str());
            check(hwnd && GetForegroundWindow() == foreground, "Inspection window does not activate");
            outside_hwnd = klass(hwnd, L"EDIT");
            SetFocus(outside_hwnd);
            SendMessageW(outside_hwnd, EM_SETSEL, 1, 5);
            check(window.post(advance), "Queue inspection actions");
        });
        const auto result = Application::run(window);
        if (result) std::wcerr << window.error() << L'\n';
        check(result == 0 && steps.empty(), "Native inspection fixture completed");
        check(GetForegroundWindow() == foreground, "Inspection never changes foreground window");
    }
};

void registration_contract() {
    Window window;
    auto host = std::make_shared<ContentHost>();
    window.set_content(host);
    auto first = std::make_shared<Stack>(Axis::vertical);
    auto child = std::make_shared<Button>(L"Child");
    first->add(child);
    unsigned callbacks{};
    window.replace_content(*host, first, {{0, first}, {1, child}}, [&](std::uint32_t) { ++callbacks; });
    auto next = std::make_shared<Stack>(Axis::vertical);
    auto leaf = std::make_shared<Label>(L"Next");
    next->add(leaf);
    const auto callback = [](std::uint32_t) {};
    rejects<std::invalid_argument>([&] { window.replace_content(*host, next, {{1, next}}, callback); }, "Reject sparse keys");
    rejects<std::invalid_argument>([&] { window.replace_content(*host, next, {{0, next}, {0, leaf}}, callback); }, "Reject duplicate keys");
    rejects<std::invalid_argument>([&] { window.replace_content(*host, next, {{0, next}, {1, next}}, callback); }, "Reject duplicate elements");
    rejects<std::invalid_argument>([&] { window.replace_content(*host, next, {{0, child}}, callback); }, "Reject foreign targets");
    rejects<std::invalid_argument>([&] { window.replace_content(*host, next, {{0, {}}}, callback); }, "Reject expired weak targets");
    rejects<std::invalid_argument>([&] { window.replace_content(*host, next, {{0, next}}, {}); }, "Reject absent callback");
    check(host->content() == first && callbacks == 0, "Preflight failures preserve current root and metadata");
}

void routed_editors(VisualStyle style) {
    Fixture fixture(style);
    auto root = std::make_shared<Stack>(Axis::vertical);
    root->set_spacing(18); root->set_padding({18, 18, 18, 18});
    auto button = std::make_shared<Button>(L"Authored button");
    auto input = std::make_shared<TextInput>(L"Authored input");
    input->set_text(L"Native input text");
    auto document = std::make_shared<MultilineText>(L"Authored document");
    document->set_text(L"Native document text");
    document->set_fixed_size({500, 130});
    auto number = std::make_shared<NumericInput>(L"Authored number");
    root->add(button); root->add(input); root->add(document); root->add(number);
    fixture.replace(root, {{0, root}, {1, button}, {2, input}, {3, document}, {4, number}});
    unsigned activated{}, input_changes{}, document_changes{};
    button->on_click([&] { ++activated; });
    input->on_change([&](const auto&) { ++input_changes; });
    document->on_change([&](const auto&) { ++document_changes; });
    HWND input_hwnd{}, document_hwnd{}, button_hwnd{}, caption{};
    TextInput::Selection selection{};
    TextSelection document_selection{};
    fixture.steps.push_back([&] {
        button_hwnd = named(fixture.hwnd, L"Authored button");
        caption = named(fixture.hwnd, L"Authored input");
        input_hwnd = klass(fixture.hwnd, L"EDIT", 1);
        document_hwnd = klass(fixture.hwnd, L"RICHEDIT50W");
        SendMessageW(input_hwnd, EM_SETSEL, 0, 0);
        SendMessageW(input_hwnd, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"Edited "));
        SendMessageW(input_hwnd, EM_SETSEL, 2, 7);
        SendMessageW(document_hwnd, EM_SETSEL, 0, 0);
        SendMessageW(document_hwnd, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"Edited "));
        SendMessageW(document_hwnd, EM_SETSEL, 1, 6);
        selection = input->selection(); document_selection = document->selection();
        input_changes = document_changes = 0;
        fixture.window.set_content_pointer_picking(*fixture.host, true);
        check(SendMessageW(input_hwnd, WM_MOUSEACTIVATE, reinterpret_cast<WPARAM>(fixture.hwnd),
            MAKELPARAM(HTCLIENT, WM_LBUTTONDOWN)) == MA_NOACTIVATE, "Native input cannot activate the preview");
        click(fixture.hwnd, button_hwnd, client_point(fixture.hwnd, button_hwnd));
        check(fixture.picks.empty() && activated == 0, "Pointer callback is deferred and author activation is suppressed");
    });
    fixture.steps.push_back([&] {
        check(fixture.picks == std::vector<std::uint32_t>{1}, "Exact button source key");
        click(fixture.hwnd, caption, client_point(fixture.hwnd, caption));
    });
    fixture.steps.push_back([&] {
        check(fixture.picks.back() == 2 && fixture.picks.size() == 2, "Caption maps to authored TextInput");
        click(fixture.hwnd, input_hwnd, client_point(fixture.hwnd, input_hwnd), WM_LBUTTONDBLCLK);
    });
    fixture.steps.push_back([&] {
        check(fixture.picks.back() == 2 && fixture.picks.size() == 3, "Native EDIT double-click picks its exact source");
        click(fixture.hwnd, document_hwnd, client_point(fixture.hwnd, document_hwnd));
    });
    fixture.steps.push_back([&] {
        check(fixture.picks.back() == 3 && fixture.picks.size() == 4, "RichEdit maps to authored document");
        const auto number_hwnd = named(fixture.hwnd, L"Authored number");
        const auto internal = klass(number_hwnd, L"EDIT");
        check(fixture.window.hit_test_content(*fixture.host, client_point(fixture.hwnd, internal)) == 4u,
            "Internal native input maps to nearest registered numeric ancestor");
        click(fixture.hwnd, internal, client_point(fixture.hwnd, internal));
    });
    fixture.steps.push_back([&] {
        check(fixture.picks.back() == 4, "Internal control routes through registered authored ancestry");
        check(GetFocus() == fixture.outside_hwnd && input->selection() == selection &&
            document->selection() == document_selection, "Picking preserves outside focus and preview selections");
        check(input_changes == 0 && document_changes == 0 &&
            SendMessageW(input_hwnd, EM_CANUNDO, 0, 0) && SendMessageW(document_hwnd, EM_CANUNDO, 0, 0),
            "Picking preserves text, native undo, and authored callbacks");
        const auto gap = Point{root->bounds().x + 4, root->bounds().y + 4};
        check(fixture.window.hit_test_content(*fixture.host, gap) == 0u, "Stack padding picks its registered root");
        click(fixture.hwnd, fixture.hwnd, gap);
    });
    fixture.steps.push_back([&] {
        check(fixture.picks.back() == 0, "Root HWND routes a Stack blank gap");
        POINT screen{}; ClientToScreen(button_hwnd, &screen);
        screen.x += 10; screen.y += 10;
        const auto flags = POINTER_MESSAGE_FLAG_PRIMARY | POINTER_MESSAGE_FLAG_FIRSTBUTTON;
        SendMessageW(button_hwnd, WM_POINTERDOWN, MAKEWPARAM(1, flags), MAKELPARAM(screen.x, screen.y));
        const auto previous = SetMessageExtraInfo(static_cast<LPARAM>(0xff515780));
        click(fixture.hwnd, button_hwnd, client_point(fixture.hwnd, button_hwnd));
        SetMessageExtraInfo(previous);
        SendMessageW(button_hwnd, WM_POINTERUP, MAKEWPARAM(1, POINTER_MESSAGE_FLAG_PRIMARY), MAKELPARAM(screen.x, screen.y));
    });
    fixture.steps.push_back([&] {
        check(fixture.picks.size() == 7 && fixture.picks.back() == 1, "Pointer promotion produces one deferred pick");
        const auto before = activated;
        check(SendMessageW(button_hwnd, control_action_message, button->id(), 0) == S_OK && activated == before + 1,
            "UIA invoke route remains ordinary");
        check(fixture.window.focus(*button), "Ordinary keyboard focus remains available");
        SendMessageW(button_hwnd, WM_KEYDOWN, VK_RETURN, 0);
        SendMessageW(button_hwnd, WM_KEYUP, VK_RETURN, 0);
        check(activated > before + 1, "Keyboard activation remains ordinary");
        fixture.window.set_content_pointer_picking(*fixture.host, false);
        const auto pointer_before = activated;
        click(fixture.hwnd, button_hwnd, client_point(fixture.hwnd, button_hwnd));
        check(activated == pointer_before + 1, "Interactive mode preserves ordinary primary activation");
    });
    fixture.run();
}

void layout_contract() {
    Fixture fixture;
    auto grid = std::make_shared<Grid>();
    grid->set_tracks({{TrackSizing::fixed, 100}, {TrackSizing::fixed, 160}, {TrackSizing::star, 1}},
        {{TrackSizing::star, 1}, {TrackSizing::star, 1}});
    grid->set_gap(20, 20);
    auto front = std::make_shared<Button>(L"Front overlap");
    auto back = std::make_shared<Button>(L"Back overlap");
    grid->add(front, 0, 0); grid->add(back, 0, 0);
    auto pages = std::make_shared<PageView>();
    auto active = std::make_shared<Button>(L"Active page");
    auto hidden = std::make_shared<Button>(L"Hidden page");
    pages->add(active); pages->add(hidden);
    grid->add(pages, 0, 1);
    auto scrolled = std::make_shared<Stack>(Axis::vertical);
    auto upper = std::make_shared<Button>(L"Upper clipped");
    auto lower = std::make_shared<Button>(L"Lower clipped");
    upper->set_fixed_size({250, 140}); lower->set_fixed_size({250, 140});
    scrolled->add(upper); scrolled->add(lower);
    auto scroll = std::make_shared<ScrollView>(scrolled);
    grid->add(scroll, 1, 0);
    auto panel = std::make_shared<Stack>(Axis::vertical);
    panel->set_padding({15, 15, 15, 15});
    auto internal = std::make_shared<Button>(L"Nested authored child");
    panel->add(internal);
    auto content = std::make_shared<ContentView>(panel, L"Clipped pane");
    grid->add(content, 1, 1);
    fixture.replace(grid, {{0, grid}, {1, front}, {2, back}, {3, pages}, {4, active}, {5, hidden},
        {6, scroll}, {7, upper}, {8, lower}, {9, content}, {10, panel}, {11, internal}});
    fixture.steps.push_back([&] {
        fixture.window.set_content_pointer_picking(*fixture.host, true);
        const auto a = named(fixture.hwnd, L"Front overlap"), b = named(fixture.hwnd, L"Back overlap");
        SetWindowPos(a, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        check(fixture.window.hit_test_content(*fixture.host, center(front->bounds())) == 1u,
            "Native frontmost sibling wins independently of registration or source order");
        SetWindowPos(b, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        check(fixture.window.hit_test_content(*fixture.host, center(front->bounds())) == 2u, "Native z-order changes hit target");
        check(fixture.window.hit_test_content(*fixture.host, center(active->bounds())) == 4u, "Only selected PageView participates");
        const auto gap = Point{grid->bounds().x + grid->bounds().width / 2, grid->bounds().y + 110};
        check(fixture.window.hit_test_content(*fixture.host, gap) == 0u, "Grid row and column whitespace belongs to grid");
        click(fixture.hwnd, fixture.hwnd, gap);
        const auto outside_clip = Point{scroll->bounds().x + 10, scroll->bounds().y + scroll->bounds().height + 10};
        check(fixture.window.hit_test_content(*fixture.host, outside_clip) == 0u, "Scrolled native overflow cannot escape its viewport");
        scroll->set_offset(120);
        check(fixture.window.hit_test_content(*fixture.host, center(scroll->viewport())) == 8u, "Scroll offset and nested clipping are current");
        check(fixture.window.hit_test_content(*fixture.host, {panel->bounds().x + 2, panel->bounds().y + 2}) == 10u,
            "Native parent blank area maps through nested retained layout ancestry");
        pages->select(1);
        check(fixture.window.hit_test_content(*fixture.host, center(pages->bounds())) == 5u, "Newly selected page replaces hidden source target");
        back->set_visible(false);
        check(fixture.window.hit_test_content(*fixture.host, center(front->bounds())) == 1u, "Hidden sibling does not intercept inspection");
        front->set_enabled(false);
        check(fixture.window.hit_test_content(*fixture.host, center(front->bounds())) == 1u, "Disabled authored controls remain inspectable");
    });
    fixture.steps.push_back([&] { check(fixture.picks == std::vector<std::uint32_t>{0}, "Whitespace click delivered exact root key"); });
    fixture.run();
}

void clear_affordance_contract() {
    Fixture fixture(VisualStyle::winui);
    auto input = std::make_shared<TextInput>(L"Clearable authored input");
    input->set_text(L"Retained native undo");
    fixture.replace(input, {{0, input}});
    HWND editor{};
    TextInput::Selection selection{};
    std::wstring contents;
    fixture.steps.push_back([&] {
        check(fixture.window.focus(*input), "Focus native preview input before inspection");
        editor = GetFocus();
        SendMessageW(editor, EM_SETSEL, 0, 0);
        SendMessageW(editor, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"Edited "));
        SendMessageW(editor, EM_SETSEL, 2, 6);
        SendMessageW(fixture.hwnd, WM_APP + 12, 0, 0);
        const auto clear = named(fixture.hwnd, L"Clear Clearable authored input");
        check(IsWindowVisible(clear), "Native clear affordance is present");
        contents = text(editor); selection = input->selection();
        fixture.window.set_content_pointer_picking(*fixture.host, true);
        check(fixture.window.hit_test_content(*fixture.host, client_point(fixture.hwnd, clear)) == 0u,
            "Synthetic clear peer maps through its native TextInput owner");
        click(fixture.hwnd, clear, client_point(fixture.hwnd, clear));
        check(GetFocus() == editor && text(editor) == contents && input->selection() == selection &&
            SendMessageW(editor, EM_CANUNDO, 0, 0), "Picking clear affordance preserves focused native editor state");
    });
    fixture.steps.push_back([&] {
        check(fixture.picks == std::vector<std::uint32_t>{0}, "Clear affordance delivers only the authored input source");
    });
    fixture.run();
}

void split_and_callback_contract() {
    Fixture fixture;
    auto first = std::make_shared<Button>(L"First split source");
    auto second = std::make_shared<Button>(L"Second split source");
    auto split = std::make_shared<SplitView>(first, second);
    fixture.replace(split, {{0, split}, {1, first}, {2, second}});
    bool delivered{};
    fixture.steps.push_back([&] {
        fixture.window.set_content_pointer_picking(*fixture.host, true);
        check(fixture.window.hit_test_content(*fixture.host, center(first->bounds())) == 1u, "Split first pane has exact authored ancestry");
        check(fixture.window.hit_test_content(*fixture.host, center(second->bounds())) == 2u, "Split second pane has exact authored ancestry");
        check(fixture.window.hit_test_content(*fixture.host, center(split->divider())) == 0u, "Splitter whitespace maps to the split source");
        split->set_secondary_visible(false);
        check(fixture.window.hit_test_content(*fixture.host, center(split->bounds())) == 1u, "Collapsed split pane cannot intercept picking");
        auto next = std::make_shared<Button>(L"Deferred clear");
        fixture.window.replace_content(*fixture.host, next, {{0, next}}, [&](std::uint32_t key) {
            check(key == 0u, "Deferred callback receives exact source key");
            fixture.window.replace_content(*fixture.host, {});
            delivered = true;
        });
        click(fixture.hwnd, named(fixture.hwnd, L"Deferred clear"), center(next->bounds()));
        check(!delivered, "Native gesture does not invoke callback synchronously");
    });
    fixture.steps.push_back([&] {
        check(delivered && !fixture.host->content(), "Pick callback can replace content after native input unwinds");
    });
    fixture.run();
}

void outside_capture_contract() {
    Fixture fixture;
    auto button = std::make_shared<Button>(L"Captured outside route");
    unsigned activated{};
    button->on_click([&] { ++activated; });
    fixture.replace(button, {{0, button}});
    fixture.steps.push_back([&] {
        fixture.window.set_content_pointer_picking(*fixture.host, true);
        const auto native_button = named(fixture.hwnd, L"Captured outside route");
        SendMessageW(native_button, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(10, 10));
        fixture.window.set_content_pointer_picking(*fixture.host, false);
        SendMessageW(native_button, WM_LBUTTONUP, 0, MAKELPARAM(10, 10));
        check(activated == 0, "Disabling picking still consumes the preceding inspected gesture release");
        fixture.window.set_content_pointer_picking(*fixture.host, true);
        SendMessageW(native_button, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(10, 10));
        SendMessageW(fixture.hwnd, WM_CANCELMODE, 0, 0);
        SendMessageW(fixture.outside_hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(5, 5));
        check(GetCapture() == fixture.outside_hwnd, "Outside editor can start ordinary native selection capture");
        const auto position = center(button->bounds());
        const auto scale = GetDpiForWindow(fixture.hwnd) / 96.0f;
        POINT pixel{static_cast<LONG>(position.x * scale), static_cast<LONG>(position.y * scale)};
        MapWindowPoints(fixture.hwnd, fixture.outside_hwnd, &pixel, 1);
        SendMessageW(fixture.outside_hwnd, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(pixel.x, pixel.y));
        check(GetCapture() == fixture.outside_hwnd, "Dragging outside editor across preview preserves its capture");
        SendMessageW(fixture.outside_hwnd, WM_LBUTTONUP, 0, MAKELPARAM(pixel.x, pixel.y));
        check(!GetCapture(), "Outside native selection receives its release over preview");
    });
    fixture.steps.push_back([&] { check(fixture.picks.empty(), "Outside captured gesture never creates a preview pick"); });
    fixture.run();
}

void busy_and_unsupported() {
    Fixture fixture;
    auto input = std::make_shared<TextInput>(L"Inspection input");
    fixture.replace(input, {{0, input}});
    fixture.steps.push_back([&] {
        ContentHost foreign;
        rejects<std::invalid_argument>([&] { fixture.window.set_content_pointer_picking(foreign, true); }, "Reject unattached host");
        bool wrong_thread{};
        std::thread thread([&] {
            try { fixture.window.set_content_pointer_picking(*fixture.host, true); }
            catch (const std::logic_error&) { wrong_thread = true; }
        });
        thread.join(); check(wrong_thread, "Reject wrong-thread inspection");
        SetCapture(fixture.outside_hwnd);
        rejects<std::logic_error>([&] { fixture.window.set_content_pointer_picking(*fixture.host, true); }, "Capture is Busy");
        ReleaseCapture();
        const auto editor = klass(fixture.hwnd, L"EDIT", 1);
        SendMessageW(editor, WM_IME_STARTCOMPOSITION, 0, 0);
        rejects<std::logic_error>([&] { fixture.window.set_content_pointer_picking(*fixture.host, true); }, "IME is Busy");
        SendMessageW(editor, WM_IME_ENDCOMPOSITION, 0, 0);
        bool callback_busy{};
        input->on_change([&](const auto&) {
            rejects<std::logic_error>([&] { fixture.window.set_content_pointer_picking(*fixture.host, true); }, "Native input callback is Busy");
            callback_busy = true;
        });
        SetWindowTextW(editor, L"Native callback");
        input->on_change({});
        check(callback_busy, "Native callback refusal was exercised");
        const auto foreign_child = CreateWindowExW(0, L"STATIC", L"Foreign child", WS_CHILD | WS_VISIBLE,
            1, 1, 10, 10, editor, nullptr, GetModuleHandleW(nullptr), nullptr);
        check(foreign_child != nullptr, "Create owned foreign-child fixture");
        rejects<std::invalid_argument>([&] { fixture.window.set_content_pointer_picking(*fixture.host, true); }, "Unknown native descendant refused");
        DestroyWindow(foreign_child);
        fixture.window.set_content_pointer_picking(*fixture.host, true);
        auto popup = std::make_shared<Popup>(std::make_shared<Button>(L"Popup child"));
        rejects<std::invalid_argument>([&] { fixture.window.show_popup(popup, *input); }, "Opening popup while picking is refused");
        rejects<std::invalid_argument>([&] { fixture.window.confirm(L"No modal", L"Must not open"); }, "Opening modal while picking is refused");
        rejects<std::invalid_argument>([&] { fixture.window.show_open_file_dialog({}); }, "Opening native file dialog while picking is refused");
        auto runtime = std::make_shared<WebContent>();
        rejects<std::invalid_argument>([&] { fixture.replace(runtime, {{0, runtime}}); }, "Active picking refuses runtime candidate before mutation");
        check(fixture.host->content() == input, "Unsupported candidate preserves current content and mode");
        fixture.window.set_content_pointer_picking(*fixture.host, false);
        fixture.replace(runtime, {{0, runtime}});
        rejects<std::invalid_argument>([&] { fixture.window.set_content_pointer_picking(*fixture.host, true); }, "Runtime has no fallback picking mode");
        auto date = std::make_shared<DateTimePicker>();
        fixture.replace(date, {{0, date}});
        rejects<std::invalid_argument>([&] { fixture.window.set_content_pointer_picking(*fixture.host, true); }, "Native date descendants are unsupported");
        auto list = std::make_shared<FileList>();
        fixture.replace(list, {{0, list}});
        rejects<std::invalid_argument>([&] { fixture.window.set_content_pointer_picking(*fixture.host, true); }, "Native file-list descendants are unsupported");
        auto nested = std::make_shared<ContentHost>(std::make_shared<Button>(L"Nested host"));
        fixture.replace(nested, {{0, nested}});
        rejects<std::invalid_argument>([&] { fixture.window.set_content_pointer_picking(*fixture.host, true); }, "Nested picking hosts are unsupported");
        fixture.replace(input, {{0, input}});
        auto existing_popup = std::make_shared<Popup>(std::make_shared<Button>(L"Existing popup"));
        fixture.window.show_popup(existing_popup, *input);
        rejects<std::invalid_argument>([&] { fixture.window.set_content_pointer_picking(*fixture.host, true); }, "Existing popup surface is unsupported");
        fixture.window.dismiss_popup(*existing_popup);
    });
    fixture.run();
}
void overlapping_host_contract() {
    WindowOptions options;
    options.title = L"Overlapping inspection hosts";
    options.show_activated = false;
    Window window(options);
    auto root = std::make_shared<Grid>();
    auto first = std::make_shared<ContentHost>();
    auto second = std::make_shared<ContentHost>();
    root->add(first, 0, 0); root->add(second, 0, 0);
    window.set_content(root);
    auto content = std::make_shared<Stack>(Axis::vertical);
    window.replace_content(*first, content, {{0, content}}, [](std::uint32_t) {});
    bool checked{};
    window.post([&] {
        rejects<std::invalid_argument>([&] { window.set_content_pointer_picking(*first, true); },
            "Overlapping host surfaces are refused instead of choosing registration order");
        checked = true;
        window.close();
    });
    check(Application::run(window) == 0 && checked, "Overlapping host refusal completes");
}

void retirement_contract() {
    Fixture fixture;
    auto first = std::make_shared<Button>(L"Generation 0");
    fixture.replace(first, {{0, first}});
    std::vector<std::weak_ptr<int>> retired;
    unsigned callback_count{};
    fixture.steps.push_back([&] {
        fixture.window.set_content_pointer_picking(*fixture.host, true);
        const auto count = descendants(fixture.hwnd).size();
        for (int i = 1; i <= 100; ++i) {
            const auto old = fixture.host->content();
            const auto hwnd = named(fixture.hwnd, L"Generation " + std::to_wstring(i - 1));
            click(fixture.hwnd, hwnd, center(old->bounds()));
            auto marker = std::make_shared<int>(i);
            retired.push_back(marker);
            auto next = std::make_shared<Button>(L"Generation " + std::to_wstring(i));
            fixture.window.replace_content(*fixture.host, next, {{0, next}},
                [&, marker](std::uint32_t) { ++callback_count; });
            check(descendants(fixture.hwnd).size() == count, "Repeated inspection replacements keep constant native ownership");
            if (i > 1) check(retired[i - 2].expired(), "Previous registration callback releases before successful return");
        }
        const auto hwnd = named(fixture.hwnd, L"Generation 100");
        click(fixture.hwnd, hwnd, center(fixture.host->content()->bounds()));
        fixture.window.replace_content(*fixture.host, {});
        check(retired.back().expired(), "Clear releases current callback and target metadata immediately");
        check(callback_count == 0 && fixture.picks.empty(), "No pending generation delivered during native input");
    });
    fixture.steps.push_back([&] {
        check(callback_count == 0 && fixture.picks.empty(), "Replaced and cleared pending picks never deliver");
        auto next = std::make_shared<Button>(L"After clear");
        fixture.replace(next, {{0, next}});
        for (int i = 0; i < 10; ++i) click(fixture.hwnd, named(fixture.hwnd, L"After clear"), center(next->bounds()));
    });
    fixture.steps.push_back([&] {
        check(fixture.picks == std::vector<std::uint32_t>{0}, "Mode survives clear; at most one pick is pending per host");
        auto current = fixture.host->content();
        click(fixture.hwnd, named(fixture.hwnd, L"After clear"), center(current->bounds()));
        fixture.window.close();
    });
    fixture.run();
    check(fixture.picks.size() == 1, "Close suppresses pending pick delivery");
}
}
int main() {
    try {
        registration_contract();
        routed_editors(VisualStyle::classic);
        routed_editors(VisualStyle::winui);
        clear_affordance_contract();
        layout_contract();
        split_and_callback_contract();
        outside_capture_contract();
        busy_and_unsupported();
        overlapping_host_contract();
        retirement_contract();
        std::cout << "Content inspection: native routes, editors, ancestry/clips, z-order, Busy/refusals, 100 replacements passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
