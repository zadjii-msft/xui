#include "../src/content_inspection.hpp"
#include "../src/control_accessibility.hpp"
#include "../src/drawing.hpp"
#include "xui/map_view.hpp"
#include "owned_window_capture.hpp"
#include <richedit.h>
#include <deque>
#include <iostream>
#include <thread>

namespace xui {
struct DrawingTestAccess {
    static void lose_target() { Drawing::end_result_override_ = D2DERR_RECREATE_TARGET; }
};
}
namespace {
using namespace xui;
using Result = ContentHighlightResult;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class Error, class F> void rejects(F&& operation, const char* message) {
    try { operation(); } catch (const Error&) { return; }
    throw std::runtime_error(message);
}
void geometry() {
    for (const float scale : {1.0f, 1.5f, 2.0f}) {
        const auto ring = inspection::outline({10.25f, 10.25f, 80.5f, 60.5f}, {0, 0, 200, 200}, scale);
        check(ring.visible(), "Original outline has a visible perimeter");
        for (const auto rect : ring.segments) {
            for (const float value : {rect.x, rect.y, rect.width, rect.height})
                check(std::abs(value * scale - std::round(value * scale)) < 0.0001f, "All outline edges snap to pixels");
            check(std::abs(std::min(rect.width, rect.height) * scale - 2) < 0.0001f, "Stroke is two physical pixels");
            check(rect.x >= 10.25f && rect.y >= 10.25f && rect.x + rect.width <= 90.75f &&
                rect.y + rect.height <= 70.75f, "Stroke remains inside original bounds");
        }
        check(!ring.overlaps({30, 30, 10, 10}), "An interior native surface does not touch the perimeter");
        const auto clipped = inspection::outline({0, 0, 100, 100}, {20, 0, 80, 100}, scale);
        check(clipped.visible() && !clipped.overlaps({20, 40, 1, 10}), "Clipping does not invent a left edge");
        check(clipped.overlaps({99, 40, 1, 10}), "The original right edge survives clipping");
        check(!inspection::outline({0, 0, 100, 100}, {20, 20, 60, 60}, scale).visible(),
            "Interior-only clipping has no visible perimeter");
        check(!inspection::outline({0, 0, 100, 100}, {110, 0, 20, 20}, scale).visible(), "Disjoint clipping hides outline");
    }
    auto pages = std::make_shared<PageView>();
    auto active = std::make_shared<Label>(L"Active"), inactive = std::make_shared<Label>(L"Inactive");
    pages->add_page(active); pages->add_page(inactive);
    pages->measure({300, 200}); pages->arrange({0, 0, 300, 200});
    check(inspection::element_clip(pages, *active, pages->bounds()).has_value(), "Active page participates in geometry");
    check(!inspection::element_clip(pages, *inactive, pages->bounds()), "Inactive page has no outline geometry");
    active->set_visible(false);
    check(!inspection::element_clip(pages, *active, pages->bounds()), "Hidden target has no outline geometry");
    std::cout << "PASS outline geometry: 96/144/192 DPI, original clipped edges, hidden/inactive pages\n";
}
std::vector<HWND> children(HWND parent) {
    std::vector<HWND> result;
    EnumChildWindows(parent, [](HWND hwnd, LPARAM data) -> BOOL {
        reinterpret_cast<std::vector<HWND>*>(data)->push_back(hwnd); return TRUE;
    }, reinterpret_cast<LPARAM>(&result));
    return result;
}
HWND klass(HWND parent, const wchar_t* expected, int index = 0) {
    for (const auto hwnd : children(parent)) {
        wchar_t value[128]{}; GetClassNameW(hwnd, value, 128);
        if (_wcsicmp(value, expected) == 0 && index-- == 0) return hwnd;
    }
    throw std::runtime_error("Native editor not found");
}
HWND editor_named(HWND parent, const std::wstring& expected) {
    for (const auto hwnd : children(parent)) {
        wchar_t name[128]{}, value[128]{};
        GetClassNameW(hwnd, name, 128); GetWindowTextW(hwnd, value, 128);
        if (_wcsicmp(name, L"EDIT") == 0 && value == expected) return hwnd;
    }
    throw std::runtime_error("Exact native editor text not found");
}
void flush(HWND hwnd, bool layout = true) {
    if (layout) SendMessageW(hwnd, WM_APP + 12, 0, 0);
    check(RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW) != FALSE,
        "Present actual root drawing");
    DwmFlush();
}
struct Regions {
    struct Entry { HWND hwnd; HRGN region; int type; LONG_PTR style, extended; RECT bounds; HWND hit; };
    std::vector<Entry> entries;
    explicit Regions(HWND root) {
        auto windows = children(root);
        windows.push_back(root);
        for (const auto hwnd : windows) {
            const auto region = CreateRectRgn(0, 0, 0, 0);
            check(region != nullptr, "Allocate region observation");
            RECT rect{}; check(GetWindowRect(hwnd, &rect) != FALSE, "Read native bounds");
            entries.push_back({hwnd, region, GetWindowRgn(hwnd, region), GetWindowLongPtrW(hwnd, GWL_STYLE),
                GetWindowLongPtrW(hwnd, GWL_EXSTYLE), rect, WindowFromPoint({(rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2})});
        }
    }
    ~Regions() { for (const auto& entry : entries) DeleteObject(entry.region); }
    void unchanged(HWND root) const {
        check(children(root).size() + 1 == entries.size(), "Highlight creates no native windows");
        for (const auto& entry : entries) {
            const auto region = CreateRectRgn(0, 0, 0, 0);
            check(region != nullptr, "Allocate comparison region");
            const auto type = GetWindowRgn(entry.hwnd, region);
            const bool same = type == entry.type && (type == ERROR || EqualRgn(region, entry.region));
            DeleteObject(region);
            check(same, "Highlight never changes HWND regions");
            RECT rect{}; check(GetWindowRect(entry.hwnd, &rect) != FALSE, "Read retained native bounds");
            check(EqualRect(&rect, &entry.bounds) && GetWindowLongPtrW(entry.hwnd, GWL_STYLE) == entry.style &&
                GetWindowLongPtrW(entry.hwnd, GWL_EXSTYLE) == entry.extended, "Highlight preserves HWND bounds and styles");
            check(WindowFromPoint({(rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2}) == entry.hit,
                "Actual native WindowFromPoint identities stay unchanged");
        }
    }
};
struct Fixture {
    WindowOptions options;
    Window window;
    std::shared_ptr<ContentHost> host = std::make_shared<ContentHost>();
    std::shared_ptr<TextInput> outside = std::make_shared<TextInput>(L"Outside");
    std::deque<std::function<void()>> steps;
    HWND hwnd{}, editor{}, foreground{};
    unsigned picks{};
    explicit Fixture(VisualStyle style = VisualStyle::classic) :
        options{L"XUI non-occluding highlight", {960, 760}, ThemeMode::light, {}, false, style, false}, window(options) {
        auto root = std::make_shared<Stack>(Axis::horizontal);
        outside->set_text(L"Outside editor text"); outside->set_fixed_size({260, 68});
        root->add(outside); root->add(host, 1); window.set_content(root);
    }
    void replace(const std::shared_ptr<Element>& root, std::vector<ContentInspectionTarget> targets) {
        window.replace_content(*host, root, std::move(targets), [this](std::uint32_t) { ++picks; });
    }
    Result select(std::optional<std::uint32_t> key) { return window.highlight_content(*host, key); }
    void run() {
        foreground = GetForegroundWindow();
        std::function<void()> advance = [&] {
            auto action = std::move(steps.front()); steps.pop_front(); action();
            if (steps.empty()) window.close();
            else check(window.post(advance), "Queue highlight test step");
        };
        check(window.post([&] {
            hwnd = FindWindowW(L"Xui.Window.1", options.title.c_str());
            check(hwnd && GetForegroundWindow() == foreground, "Test window does not activate");
            MONITORINFO monitor{sizeof(monitor)};
            check(GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor) != FALSE,
                "Read test monitor work area");
            struct Placement { RECT foreground; MONITORINFO monitor; } placement{{}, monitor};
            GetWindowRect(foreground, &placement.foreground);
            EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR handle, HDC, LPRECT, LPARAM data) -> BOOL {
                auto& placement = *reinterpret_cast<Placement*>(data);
                MONITORINFO candidate{sizeof(candidate)};
                RECT overlap{};
                if (GetMonitorInfoW(handle, &candidate) &&
                    !IntersectRect(&overlap, &candidate.rcWork, &placement.foreground)) {
                    placement.monitor = candidate; return FALSE;
                }
                return TRUE;
            }, reinterpret_cast<LPARAM>(&placement));
            monitor = placement.monitor;
            check(SetWindowPos(hwnd, HWND_TOPMOST, monitor.rcWork.left + 8, monitor.rcWork.top + 8, 0, 0,
                SWP_NOSIZE | SWP_NOACTIVATE) != FALSE, "Position owned physical-hit fixture without activation");
            check(GetForegroundWindow() == foreground, "Physical-hit fixture positioning does not activate");
            editor = editor_named(hwnd, outside->text());
            SetFocus(editor);
            check(window.post(advance), "Start highlight test steps");
        }), "Queue highlight initialization");
        const auto result = Application::run(window);
        std::cout << "Native highlight run returned " << result << '\n';
        if (result) std::wcerr << window.error() << L'\n';
        check(result == 0 && steps.empty(), "Highlight fixture completed");
        check(!IsWindow(hwnd) && !IsWindow(editor) && Drawing::live_targets() == 0, "Close releases native windows and drawing target");
        check(GetForegroundWindow() == foreground, "Highlight never changes foreground window");
    }
};
std::vector<DWORD> pixels(const owned_window_capture::Pixels& image, const inspection::Outline& ring, float scale) {
    std::vector<DWORD> result;
    for (const auto rect : ring.segments)
        for (int y = static_cast<int>(std::lround(rect.y * scale)); y < std::lround((rect.y + rect.height) * scale); ++y)
            for (int x = static_cast<int>(std::lround(rect.x * scale)); x < std::lround((rect.x + rect.width) * scale); ++x)
                if (x >= 0 && y >= 0 && x < image.width && y < image.height)
                    result.push_back(image.data[static_cast<std::size_t>(y) * image.width + x] & 0xffffff);
    return result;
}
void renderer_and_native_state(VisualStyle style) {
    Fixture f(style);
    auto root = std::make_shared<Stack>(Axis::vertical);
    root->set_padding({18, 18, 18, 18}); root->set_spacing(18);
    auto label = std::make_shared<Label>(L"Outlined label");
    auto button = std::make_shared<Button>(L"Ordinary button");
    auto input = std::make_shared<TextInput>(L"Opaque caption");
    auto document = std::make_shared<MultilineText>(L"Opaque document");
    label->set_fixed_size({500, 56}); button->set_fixed_size({500, 52});
    input->set_fixed_size({500, 72}); document->set_fixed_size({500, 136});
    PartStyleValues opaque_document;
    opaque_document.padding = Insets{};
    opaque_document.border_thickness = Insets{};
    document->set_control_style_values(StylePart::root, opaque_document);
    input->set_text(L"Native input"); document->set_text(L"Native document");
    root->add(label); root->add(button); root->add(input); root->add(document);
    f.replace(root, {{4, document}, {2, button}, {0, root}, {3, input}, {1, label}});
    unsigned clicks{}, input_changes{}, document_changes{};
    button->on_click([&] {
        ++clicks;
        rejects<std::logic_error>([&] { f.select(1); }, "Highlight rejects native input callback");
    });
    input->on_change([&](const auto&) { ++input_changes; });
    document->on_change([&](const auto&) { ++document_changes; });
    f.steps.push_back([&] {
        flush(f.hwnd);
        const auto native_input = editor_named(f.hwnd, input->text()), native_document = klass(f.hwnd, L"RICHEDIT50W");
        for (const auto hwnd : {native_input, native_document}) {
            SendMessageW(hwnd, EM_SETSEL, 0, 0);
            SendMessageW(hwnd, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"Edited "));
            SendMessageW(hwnd, EM_SETSEL, 2, 7);
        }
        input_changes = document_changes = 0;
        const auto input_selection = input->selection();
        const auto document_selection = document->selection();
        Regions regions(f.hwnd);
        RECT edit_bounds{}; check(GetWindowRect(native_input, &edit_bounds) != FALSE, "Read physical native EDIT bounds");
        const POINT probe{(edit_bounds.left + edit_bounds.right) / 2, (edit_bounds.top + edit_bounds.bottom) / 2};
        const auto actual_hit = WindowFromPoint(probe);
        if (actual_hit != native_input) {
            RECT frame{}; GetWindowRect(f.hwnd, &frame);
            DWORD hit_process{}; GetWindowThreadProcessId(actual_hit, &hit_process);
            const auto blocking_root = GetAncestor(actual_hit, GA_ROOT);
            RECT blocking_bounds{}; GetWindowRect(blocking_root, &blocking_bounds);
            DWORD cloaked{};
            const auto cloak_result = DwmGetWindowAttribute(f.hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
            wchar_t blocking_class[128]{}; GetClassNameW(blocking_root, blocking_class, 128);
            std::cerr << "Physical EDIT probe expected=" << native_input << " actual=" << actual_hit
                << " hit_process=" << hit_process << " own_process=" << GetCurrentProcessId()
                << " frame=" << frame.left << ',' << frame.top << ',' << frame.right << ',' << frame.bottom
                << " edit=" << edit_bounds.left << ',' << edit_bounds.top << ',' << edit_bounds.right << ',' << edit_bounds.bottom
                << " point=" << probe.x << ',' << probe.y << " visible=" << IsWindowVisible(native_input)
                << " enabled=" << IsWindowEnabled(native_input)
                << " root_exstyle=" << GetWindowLongPtrW(f.hwnd, GWL_EXSTYLE)
                << " cloak_hr=" << cloak_result << " cloaked=" << cloaked
                << " blocker_bounds=" << blocking_bounds.left << ',' << blocking_bounds.top << ','
                << blocking_bounds.right << ',' << blocking_bounds.bottom
                << " blocker_exstyle=" << GetWindowLongPtrW(blocking_root, GWL_EXSTYLE) << '\n';
            std::wcerr << L"Blocking native class=" << blocking_class << L'\n';
        }
        check(actual_hit == native_input,
            "Physical hit probe reaches the actual native EDIT");
        GUITHREADINFO caret{sizeof(caret)};
        check(GetGUIThreadInfo(GetCurrentThreadId(), &caret) != FALSE, "Read original native caret");
        SetCapture(f.editor);
        const auto scale = GetDpiForWindow(f.hwnd) / 96.0f;
        const auto ring = inspection::outline(label->bounds(), f.host->bounds(), scale);
        bool process_layout{};
        const auto capture = [&] { flush(f.hwnd, process_layout); return owned_window_capture::capture(f.hwnd); };
        const auto baseline = pixels(capture(), ring, scale);
        check(baseline.size() > 20 && f.select(1) == Result::applied, "Label outline applied with picking OFF");
        const auto outlined = pixels(capture(), ring, scale);
        check(outlined.size() == baseline.size() && outlined != baseline, "Presented renderer pixels contain label outline");
        regions.unchanged(f.hwnd);
        DrawingTestAccess::lose_target(); flush(f.hwnd, false); flush(f.hwnd, false);
        check(pixels(capture(), ring, scale) == outlined, "Recreated drawing target derives outline from registered key");
        check(f.select(3) == Result::occluded_native, "Opaque EDIT/caption refuses outline");
        check(pixels(capture(), ring, scale) == baseline, "Refusal clears the previous rendered outline");
        RECT document_bounds{}; GetWindowRect(native_document, &document_bounds);
        MapWindowPoints(nullptr, f.hwnd, reinterpret_cast<POINT*>(&document_bounds), 2);
        check(inspection::outline(document->bounds(), f.host->bounds(), scale).overlaps({
            document_bounds.left / scale, document_bounds.top / scale,
            (document_bounds.right - document_bounds.left) / scale, (document_bounds.bottom - document_bounds.top) / scale}),
            "Configured RichEdit HWND intersects the original perimeter");
        check(f.select(1) == Result::applied && f.select(4) == Result::occluded_native, "Opaque RichEdit/caption refuses outline");
        check(pixels(capture(), ring, scale) == baseline, "RichEdit refusal clears the previous outline");
        check(f.select(2) == Result::applied, "Button outline applied");
        const auto button_ring = inspection::outline(button->bounds(), f.host->bounds(), scale);
        const auto button_outlined = pixels(capture(), button_ring, scale);
        check(f.select(std::nullopt) == Result::cleared, "Null clears highlight");
        check(pixels(capture(), button_ring, scale) != button_outlined, "Button outline is visible in actual renderer");
        regions.unchanged(f.hwnd);
        GUITHREADINFO after{sizeof(after)};
        GetGUIThreadInfo(GetCurrentThreadId(), &after);
        if (GetFocus() != f.editor || GetCapture() != f.editor || after.hwndCaret != caret.hwndCaret ||
            !EqualRect(&after.rcCaret, &caret.rcCaret))
            std::cerr << "Editor state expected=" << f.editor << " focus=" << GetFocus() << " capture=" << GetCapture()
                << " caret=" << caret.hwndCaret << "->" << after.hwndCaret
                << " rect=" << caret.rcCaret.left << ',' << caret.rcCaret.top << ',' << caret.rcCaret.right << ',' << caret.rcCaret.bottom
                << " -> " << after.rcCaret.left << ',' << after.rcCaret.top << ',' << after.rcCaret.right << ',' << after.rcCaret.bottom << '\n';
        check(GetFocus() == f.editor && GetCapture() == f.editor && after.hwndCaret == caret.hwndCaret &&
            EqualRect(&after.rcCaret, &caret.rcCaret), "Highlight preserves focus, capture and native caret");
        check(input->selection() == input_selection && document->selection() == document_selection &&
            input_changes == 0 && document_changes == 0 &&
            SendMessageW(native_input, EM_CANUNDO, 0, 0) && SendMessageW(native_document, EM_CANUNDO, 0, 0),
            "Native text selections, undo buffers and callbacks stay unchanged");
        ReleaseCapture();
        process_layout = true;
        std::cout << "PASS native pixels, regions, physical hits, focus/capture/caret/selection/undo\n";
        check(f.select(1) == Result::applied, "Select before hidden request");
        label->set_visible(false);
        check(f.select(1) == Result::not_visible, "Hidden request clears old selection");
        label->set_visible(true);
        check(pixels(capture(), ring, scale) == baseline, "Failed hidden request retains no new key");
        check(f.select(1) == Result::applied, "Select before temporary layout hiding");
        label->set_visible(false); flush(f.hwnd); label->set_visible(true);
        check(pixels(capture(), ring, scale) == outlined, "Previously applied key survives temporary layout hiding");
        const auto bounds = label->bounds();
        const auto foreign_surface = CreateWindowExW(0, L"STATIC", L"Unregistered surface", WS_CHILD | WS_VISIBLE,
            static_cast<int>(bounds.x * scale), static_cast<int>(bounds.y * scale), 40, 20,
            f.hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        check(foreign_surface != nullptr, "Create owned unproven native surface");
        const auto foreign_result = f.select(1);
        check(DestroyWindow(foreign_surface) != FALSE, "Remove test-owned unproven surface");
        check(foreign_result == Result::unsupported_surface &&
            pixels(capture(), ring, scale) == baseline, "Unproven surface refusal clears and retains no new key");
        check(f.select(1) == Result::applied, "Select before modal owner disable");
        EnableWindow(f.hwnd, FALSE);
        const auto disabled_result = f.select(1);
        check(f.select({}) == Result::cleared, "Clear works while modal owner is disabled");
        EnableWindow(f.hwnd, TRUE);
        check(disabled_result == Result::unsupported_surface, "Disabled modal owner explicitly refuses highlight");
        std::cout << "PASS hidden/foreign/modal refusal retirement\n";
        label->set_enabled(false);
        check(f.select(1) == Result::applied, "Disabled retained target can be outlined");
        label->set_enabled(true);
        HWND button_hwnd{};
        for (const auto hwnd : children(f.hwnd)) {
            wchar_t name[128]{}; GetWindowTextW(hwnd, name, 128);
            if (std::wstring_view(name) == button->name()) button_hwnd = hwnd;
        }
        check(button_hwnd && SendMessageW(button_hwnd, control_action_message, button->id(), 0) == S_OK && clicks == 1,
            "UIA native invoke endpoint remains ordinary during highlight");
        check(f.window.focus(*button), "Keyboard focus remains ordinary");
        SendMessageW(button_hwnd, WM_KEYDOWN, VK_RETURN, 0); SendMessageW(button_hwnd, WM_KEYUP, VK_RETURN, 0);
        check(clicks == 2, "Keyboard activation remains ordinary during highlight");
        SetFocus(native_input); SendMessageW(native_input, WM_CHAR, L'Z', 0);
        check(input_changes == 1 && SendMessageW(native_input, WM_UNDO, 0, 0), "Native typing and undo remain functional");
        SetFocus(f.editor);
        rejects<std::invalid_argument>([&] { f.select(99); }, "Unregistered highlight key rejected");
        ContentHost foreign;
        rejects<std::invalid_argument>([&] { f.window.highlight_content(foreign, {}); }, "Foreign highlight host rejected");
        std::thread wrong([&] {
            rejects<std::logic_error>([&] { f.select(1); }, "Wrong-thread highlight rejected");
        }); wrong.join();
        check(f.picks == 0, "Highlight never enables pointer picking or sends pick callbacks");
        std::cout << "PASS presented Label/Button outlines, native regions/hits/editor state/UIA/keyboard/refusals\n";
    });
    f.steps.push_back([&] {
        auto current = std::make_shared<Label>(L"Repeated target");
        f.replace(current, {{0, current}});
        const auto cycle = [&] {
            auto next = std::make_shared<Label>(L"Repeated target");
            std::weak_ptr<Element> retired = current;
            f.replace(next, {{0, next}}); current = next;
            check(retired.expired(), "Replacement releases retired registered target");
            check(f.select(0) == Result::applied, "Repeated outline applied");
            flush(f.hwnd);
            check(f.select({}) == Result::cleared, "Repeated outline cleared");
            flush(f.hwnd);
        };
        for (int i = 0; i < 12; ++i) cycle();
        const auto count = children(f.hwnd).size();
        const auto gdi = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
        const auto user = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
        for (int i = 0; i < 100; ++i) cycle();
        check(children(f.hwnd).size() == count && GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) == gdi &&
            GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS) == user, "Repeated replacements keep exact native/GDI ownership baseline");
        check(f.select(0) == Result::applied, "Select before metadata retirement");
        f.replace(current, {{0, current}});
        const auto scale = GetDpiForWindow(f.hwnd) / 96.0f;
        const auto ring = inspection::outline(current->bounds(), f.host->bounds(), scale);
        flush(f.hwnd);
        const auto retired = pixels(owned_window_capture::capture(f.hwnd), ring, scale);
        f.select({}); flush(f.hwnd);
        check(pixels(owned_window_capture::capture(f.hwnd), ring, scale) == retired, "Same-root registration replacement retires highlight");
        f.window.replace_content(*f.host, {});
        check(f.select({}) == Result::cleared, "Empty host can clear highlight");
        f.replace(current, {{0, current}});
        check(f.select(0) == Result::applied, "Close with pending selected highlight");
        std::cout << "PASS 100 highlight/clear/replacement cycles with exact HWND/USER/GDI baseline\n";
    });
    f.run();
}
void clipped_layout() {
    Fixture f;
    auto root = std::make_shared<Stack>(Axis::vertical);
    root->set_padding({16, 16, 16, 16});
    auto pages = std::make_shared<PageView>();
    auto first = std::make_shared<Label>(L"First"), second = std::make_shared<Label>(L"Second");
    pages->add_page(first); pages->add_page(second); pages->set_fixed_size({500, 140});
    auto tall = std::make_shared<Label>(L"Clipped original perimeter");
    tall->set_fixed_size({480, 300});
    auto contents = std::make_shared<Stack>(Axis::vertical);
    contents->add(tall); contents->add(std::make_shared<Label>(L"Tail")); contents->set_fixed_size({500, 800});
    auto scroll = std::make_shared<ScrollView>(contents); scroll->set_fixed_size({520, 180});
    root->add(pages); root->add(scroll);
    f.replace(root, {{0, root}, {1, first}, {2, second}, {3, tall}});
    f.steps.push_back([&] {
        check(f.select(2) == Result::not_visible, "Inactive page refuses highlight");
        pages->select(1);
        check(f.select(2) == Result::applied, "Newly active page is visible");
        check(f.select(0) == Result::applied, "Layout root perimeter applied");
        scroll->set_offset(100); flush(f.hwnd);
        const auto scale = GetDpiForWindow(f.hwnd) / 96.0f;
        f.select({}); flush(f.hwnd);
        const auto baseline = owned_window_capture::capture(f.hwnd);
        check(f.select(3) == Result::applied, "Partially clipped original perimeter applied");
        flush(f.hwnd);
        const auto outlined = owned_window_capture::capture(f.hwnd);
        const auto ring = inspection::outline(tall->bounds(), scroll->viewport(), scale);
        check(pixels(baseline, ring, scale) != pixels(outlined, ring, scale), "Clipped original side edges render");
        const int x = static_cast<int>((scroll->viewport().x + scroll->viewport().width / 2) * scale);
        const int y = static_cast<int>(std::ceil(scroll->viewport().y * scale));
        check(baseline.data[static_cast<std::size_t>(y) * baseline.width + x] ==
            outlined.data[static_cast<std::size_t>(y) * outlined.width + x], "Renderer adds no fabricated viewport-top border");
        scroll->set_offset(scroll->maximum_offset()); flush(f.hwnd);
        check(f.select(3) == Result::not_visible, "Fully clipped target refuses highlight");
        scroll->set_offset(100); flush(f.hwnd);
        check(f.select(3) == Result::applied, "Scrolled target can be selected again");
        RECT rect{}; GetWindowRect(f.hwnd, &rect);
        SetWindowPos(f.hwnd, nullptr, 0, 0, rect.right - rect.left - 80, rect.bottom - rect.top - 60,
            SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOZORDER);
        flush(f.hwnd);
        check(f.select(3) == Result::applied, "Resized retained viewport recomputes original perimeter");
        auto unsupported = std::make_shared<MapView>();
        f.replace(unsupported, {{0, unsupported}});
        check(f.select(0) == Result::unsupported_surface, "Unsupported native/runtime source explicitly refused with picking OFF");
        check(f.select({}) == Result::cleared, "Unsupported geometry does not prevent clearing");
        std::cout << "PASS retained layout/page/scroll/resize geometry and unsupported source refusal\n";
    });
    f.run();
}
}
int main(int argc, char** argv) {
    std::cout << std::unitbuf;
    try {
        geometry();
        if (argc == 2 && std::string_view(argv[1]) == "--geometry") return 0;
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        struct Apartment {
            ~Apartment() { winrt::clear_factory_cache(); winrt::uninit_apartment(); }
        } apartment;
        std::cout << "Starting Classic fixture\n";
        renderer_and_native_state(xui::VisualStyle::classic);
        std::cout << "Starting WinUI fixture\n";
        renderer_and_native_state(xui::VisualStyle::winui);
        std::cout << "Starting clipped layout fixture\n";
        clipped_layout();
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
