#include "xui/application.hpp"
#include "xui/documents.hpp"
#include "xui/reveal.hpp"
#include "owned_window_capture.hpp"
#include <windows.h>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {
using namespace xui;
constexpr UINT metrics = WM_APP + 60, update = WM_APP + 12;
constexpr UINT_PTR driver_timer = 97;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
struct Fixture {
    Window window{{L"XUI bottom reveal", {600, 400}}};
    std::shared_ptr<Stack> root = std::make_shared<Stack>(Axis::vertical);
    std::shared_ptr<Button> files = std::make_shared<Button>(L"Files");
    std::shared_ptr<Stack> bar = std::make_shared<Stack>(Axis::horizontal);
    std::shared_ptr<TextInput> input = std::make_shared<TextInput>(L"Find");
    std::shared_ptr<Button> close = std::make_shared<Button>(L"Close Find");
    std::shared_ptr<Reveal> reveal;
    HWND host{}, editor{};
    std::exception_ptr error;
    bool system_motion{}, saw_middle{}, done{}, expand{};
    unsigned phase{};
    LRESULT opening_layouts{}, idle_layouts{}, idle_paints{};
    float first_y{};
    Reveal::Clock::time_point started{}, phase_start{};
    Fixture(bool expanding = false) : expand(expanding) {
        root->add(files, 1);
        bar->set_padding({6, 6, 6, 6});
        bar->set_spacing(8);
        input->set_caption_visible(false);
        input->set_preferred_size({320, 44});
        bar->add(input, 1);
        close->set_fixed_size({44, 44});
        bar->add(close);
        reveal = std::make_shared<Reveal>(bar);
        if (expand) reveal->set_layout(RevealLayout::expand);
        reveal->set_duration(1200);
        root->add(reveal);
        window.set_content(root);
    }
    void flush() {
        SendMessageW(host, update, 0, 0);
        UpdateWindow(host);
    }
    void geometry() {
        RECT rect{};
        require(GetWindowRect(editor, &rect) != FALSE, "Read live editor geometry");
        MapWindowPoints(nullptr, host, reinterpret_cast<POINT*>(&rect), 2);
        const float scale = GetDpiForWindow(editor) / 96.0f;
        const auto bounds = input->bounds();
        const float center = (rect.top + rect.bottom) / (2 * scale);
        require(std::abs(center - (bounds.y + bounds.height / 2)) < 2,
            "Native editor follows the animated retained bounds");
        require(std::abs(bounds.height - 44) < 0.01f, "Native input keeps its full layout height");
        if (expand) {
            require(std::abs(reveal->bounds().height - 56 * reveal->progress()) < 0.01f,
                "The native clip and content use the same progress");
            require(std::abs(files->bounds().height + reveal->bounds().height - root->bounds().height) < 0.01f,
                "The sibling and Find share the available height");
            require(std::abs(files->bounds().y + files->bounds().height - bar->bounds().y) < 0.01f,
                "The sibling and content share one moving edge");
        }
    }
    void begin() {
        require(window.focus(*files), "Files initially receive focus");
        host = GetAncestor(GetFocus(), GA_ROOT);
        BOOL enabled{};
        require(SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0) != FALSE,
            "Read system motion preference without changing it");
        system_motion = enabled != FALSE;
        const auto original_height = files->bounds().height;
        reveal->set_open(true);
        require(window.focus(*input), "Find receives focus immediately, before entry completes");
        editor = GetFocus();
        wchar_t kind[32]{};
        GetClassNameW(editor, kind, 32);
        require(_wcsicmp(kind, L"EDIT") == 0, "Find remains a native EDIT");
        SendMessageW(editor, WM_CHAR, L's', 0);
        require(input->text() == L"s", "First type-to-find character is retained during entry");
        flush();
        if (expand && system_motion)
            require(files->bounds().height == original_height && reveal->bounds().height == 0,
                "Opening and focusing Find do not shrink its sibling before the first timer frame");
        first_y = input->bounds().y;
        opening_layouts = SendMessageW(host, metrics, 2, 0);
        require((SendMessageW(host, metrics, 33, 0) != 0) == system_motion,
            "Scheduler respects the system motion preference");
        if (!system_motion) require(!reveal->animating() && reveal->progress() == 1, "Reduced motion opens immediately");
        started = phase_start = Reveal::Clock::now();
        require(SetTimer(host, driver_timer, 15, tick) != 0, "Start bounded reveal fixture");
    }
    void step() {
        const auto now = Reveal::Clock::now();
        require(now - started < std::chrono::seconds(10), "Reveal fixture timed out");
        if (phase == 0) {
            geometry();
            if (GetFocus() != editor)
                std::cerr << "Focus changed: current=" << GetFocus() << " editor=" << editor
                    << " foreground=" << GetForegroundWindow() << " host=" << host << '\n';
            require(GetFocus() == editor && input->text() == L"s", "Motion preserves input identity, text, and focus");
            if (system_motion && reveal->progress() > 0 && reveal->progress() < 1) {
                saw_middle = true;
                require(input->bounds().y < first_y, "Entry moves the live editor upward");
                const auto layouts = SendMessageW(host, metrics, 2, 0);
                require(expand ? layouts > opening_layouts : layouts == opening_layouts,
                    "Only expanding entry frames perform root layout");
            }
            if (reveal->animating()) return;
            require(!system_motion || saw_middle, "Actual timer delivery produced an intermediate frame");
            require(SendMessageW(host, metrics, 33, 0) == 0, "Completed entry stops its timer");
            reveal->set_duration(1000);
            reveal->set_open(false);
            require(window.focus(*files), "Exit returns focus immediately");
            flush();
            require(!IsWindowEnabled(editor) && !window.focus(*input), "Exiting input cannot receive interaction");
            if (system_motion) {
                require(reveal->bounds().height > 0, "Exit keeps its reserved slot");
                phase = 1;
            } else phase = 2;
            phase_start = now;
        } else if (phase == 1) {
            if (now - phase_start < std::chrono::milliseconds(45)) return;
            if (reveal->animating() && reveal->progress() == 1) return;
            require(reveal->animating() && reveal->progress() < 1, "Exit makes visible progress");
            const float before = reveal->progress();
            reveal->set_open(true);
            require(reveal->progress() <= before && reveal->progress() > 0, "Reopen retargets from the current position");
            require(window.focus(*input) && GetFocus() == editor, "Reversal preserves the native editor identity");
            DWORD selection_start{}, selection_end{};
            SendMessageW(editor, EM_GETSEL, reinterpret_cast<WPARAM>(&selection_start), reinterpret_cast<LPARAM>(&selection_end));
            require(selection_start == 1 && selection_end == 1 && SendMessageW(editor, EM_CANUNDO, 0, 0),
                "Reversal preserves the native caret selection and undo history");
            phase = 2;
        } else if (phase == 2) {
            if (reveal->animating()) return;
            reveal->set_open(false);
            window.focus(*files);
            phase = 3;
        } else if (phase == 3) {
            if (reveal->animating()) return;
            flush();
            require(reveal->bounds().height == 0 && bar->bounds().height == (expand ? 56 : 0),
                "Completed exit returns the reserved height without shrinking expanding content");
            require(SendMessageW(host, metrics, 33, 0) == 0, "Completed exit stops its timer");
            idle_layouts = SendMessageW(host, metrics, 2, 0);
            idle_paints = SendMessageW(host, metrics, 0, 0);
            phase_start = now;
            phase = 4;
        } else if (phase == 4) {
            if (now - phase_start < std::chrono::milliseconds(120)) return;
            require(SendMessageW(host, metrics, 2, 0) == idle_layouts, "Settled Find has no periodic layout");
            if (SendMessageW(host, metrics, 0, 0) != idle_paints)
                std::cerr << "Idle reveal paints=" << idle_paints << "->" << SendMessageW(host, metrics, 0, 0)
                    << " timer=" << SendMessageW(host, metrics, 33, 0) << " focus=" << GetFocus()
                    << " foreground=" << GetForegroundWindow() << " owner=" << host << '\n';
            require(SendMessageW(host, metrics, 0, 0) == idle_paints, "Settled Find has no periodic repaint");
            reveal->set_duration(0);
            reveal->set_open(true);
            require(window.focus(*input) && GetFocus() == editor, "Disabled motion retains native identity");
            require(!reveal->animating() && reveal->progress() == 1, "Zero duration opens immediately");
            require(SendMessageW(host, metrics, 33, 0) == 0, "Zero duration starts no timer");
            reveal->set_duration(240);
            reveal->set_open(false);
            flush();
            ShowWindow(host, SW_HIDE);
            flush();
            require(!reveal->animating() && reveal->progress() == 0, "Hiding the owner settles motion");
            require(SendMessageW(host, metrics, 33, 0) == 0, "Hidden owner has no animation timer");
            ShowWindow(host, SW_SHOWNOACTIVATE);
            reveal->set_open(true);
            flush();
            KillTimer(host, driver_timer);
            done = true;
            window.close();
        }
    }
    static Fixture* current;
    static void CALLBACK tick(HWND, UINT, UINT_PTR, DWORD) noexcept {
        if (!current || current->done) return;
        try { current->step(); }
        catch (...) {
            current->error = std::current_exception();
            KillTimer(current->host, driver_timer);
            current->window.close();
        }
    }
};
Fixture* Fixture::current{};

void retirement_contract(bool expand = false) {
    Window window({L"Reveal retirement", {600, 400}});
    auto root = std::make_shared<Stack>(Axis::vertical);
    auto files = std::make_shared<Button>(L"Files");
    root->add(files, 1);
    auto reveal = std::make_shared<Reveal>(std::make_shared<TextInput>(L"Retiring Find"));
    if (expand) reveal->set_layout(RevealLayout::expand);
    reveal->set_duration(1000);
    auto host = std::make_shared<ContentHost>(reveal);
    root->add(host, 1);
    window.set_content(root);
    bool ran{};
    window.post([&] {
        require(window.focus(*files), "Retirement fixture receives focus");
        const auto hwnd = GetAncestor(GetFocus(), GA_ROOT);
        reveal->set_open(true);
        SendMessageW(hwnd, update, 0, 0);
        window.replace_content(*host, {});
        SendMessageW(hwnd, update, 0, 0);
        require(!reveal->animating(), "Content retirement settles the retained animation model");
        require(SendMessageW(hwnd, metrics, 33, 0) == 0, "Content retirement stops the shared timer");
        require(GetFocus() && GetAncestor(GetFocus(), GA_ROOT) == hwnd, "Content retirement preserves unrelated focus");
        ran = true;
        window.close();
    });
    const auto result = Application::run(window);
    if (result) std::wcerr << window.error() << L'\n';
    require(result == 0 && ran, "Reveal content retirement completed");
}

void concurrent_contract(bool expand = false) {
    Window window({L"Concurrent reveals", {600, 400}});
    auto root = std::make_shared<Stack>(Axis::vertical);
    auto files = std::make_shared<Button>(L"Files");
    root->add(files, 1);
    auto first = std::make_shared<Reveal>(std::make_shared<TextInput>(L"First"));
    auto second = std::make_shared<Reveal>(std::make_shared<TextInput>(L"Second"));
    if (expand) {
        first->set_layout(RevealLayout::expand);
        second->set_layout(RevealLayout::expand);
    }
    first->set_duration(10000);
    second->set_duration(10000);
    root->add(first);
    root->add(second);
    window.set_content(root);
    bool ran{};
    window.post([&] {
        require(window.focus(*files), "Concurrent fixture receives focus");
        const auto hwnd = GetAncestor(GetFocus(), GA_ROOT);
        first->set_open(true);
        second->set_open(true);
        SendMessageW(hwnd, update, 0, 0);
        const bool motion = second->animating();
        first->settle();
        SendMessageW(hwnd, update, 0, 0);
        require((SendMessageW(hwnd, metrics, 33, 0) != 0) == motion,
            "One completed reveal does not stop another reveal's timer");
        second->set_visible(false);
        SendMessageW(hwnd, update, 0, 0);
        require(!second->animating() && second->progress() == 1, "Hidden reveal settles to its open target");
        require(SendMessageW(hwnd, metrics, 33, 0) == 0, "Hiding the last active reveal stops the timer");
        second->set_visible(true);
        second->set_open(false);
        SendMessageW(hwnd, update, 0, 0);
        ShowWindow(hwnd, SW_MINIMIZE);
        require(!second->animating() && second->progress() == 0, "Minimizing settles an active exit");
        require(SendMessageW(hwnd, metrics, 33, 0) == 0, "Minimized window has no animation timer");
        ran = true;
        window.close();
    });
    const auto result = Application::run(window);
    if (result) std::wcerr << window.error() << L'\n';
    require(result == 0 && ran, "Concurrent reveal contract completed");
}

void pixel_contract(bool expand, RevealDirection direction) {
    Window window({L"Reveal pixels", {400, 240}, ThemeMode::light});
    const bool vertical = direction == RevealDirection::bottom || direction == RevealDirection::top;
    auto root = std::make_shared<Stack>(vertical ? Axis::vertical : Axis::horizontal);
    auto files = std::make_shared<Button>(L"Files");
    root->add(files, 1);
    auto bar = std::make_shared<Stack>(Axis::horizontal);
    bar->set_preferred_size({56, 56});
    PartStyleValues style;
    style.background = ThemeColor{0x12B456};
    bar->set_control_style_values(StylePart::root, style);
    auto reveal = std::make_shared<Reveal>(bar);
    if (expand) reveal->set_layout(RevealLayout::expand);
    reveal->set_direction(direction);
    reveal->set_duration(10000);
    root->add(reveal);
    auto footer = std::make_shared<Label>(L"Footer");
    footer->set_preferred_size({32, 32});
    root->add(footer);
    window.set_content(root);
    bool ran{};
    window.post([&] {
        require(window.focus(*files), "Pixel fixture receives focus");
        const auto hwnd = GetAncestor(GetFocus(), GA_ROOT);
        reveal->set_open(true);
        SendMessageW(hwnd, update, 0, 0);
        if (reveal->animating()) reveal->advance(Reveal::Clock::now() + std::chrono::milliseconds(1000));
        SendMessageW(hwnd, update, 0, 0);
        require(RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW) != FALSE,
            "Paint the intermediate reveal frame");
        winrt::check_hresult(DwmFlush());
        const auto pixels = owned_window_capture::capture(hwnd);
        const auto pixel = [&](float x, float y) {
            const auto dpi = GetDpiForWindow(hwnd);
            const int column = static_cast<int>(std::lround(x * dpi / 96));
            const int row = static_cast<int>(std::lround(y * dpi / 96));
            require(column >= 0 && column < pixels.width && row >= 0 && row < pixels.height, "Reveal pixel is in the owned window");
            return pixels.data[static_cast<std::size_t>(row) * pixels.width + column] & 0xffffff;
        };
        HIGHCONTRASTW contrast{sizeof(contrast)};
        require(SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) != FALSE, "Read contrast preference");
        if (!(contrast.dwFlags & HCF_HIGHCONTRASTON)) {
            const auto bounds = reveal->bounds(), moving = bar->bounds();
            const auto left = std::max(bounds.x, moving.x), top = std::max(bounds.y, moving.y);
            const auto right = std::min(bounds.x + bounds.width, moving.x + moving.width);
            const auto bottom = std::min(bounds.y + bounds.height, moving.y + moving.height);
            require(pixel((left + right) / 2, (top + bottom) / 2) == 0x12B456,
                "Actual intermediate pixels follow the selected entry edge");
            if (reveal->animating() && !expand) {
                const auto x = direction == RevealDirection::left ? bounds.x + bounds.width - 3 : bounds.x + 3;
                const auto y = direction == RevealDirection::top ? bounds.y + bounds.height - 3 : bounds.y + 3;
                require(pixel(x, y) != 0x12B456, "Unrevealed space does not contain final-position pixels");
            }
            require(pixel(vertical ? bounds.x + 4 : bounds.x + bounds.width + 3,
                vertical ? bounds.y + bounds.height + 3 : bounds.y + 4) != 0x12B456,
                "Moving content remains clipped before the stationary footer");
        }
        ran = true;
        window.close();
    });
    const auto result = Application::run(window);
    if (result) std::wcerr << window.error() << L'\n';
    require(result == 0 && ran, "Reveal pixel contract completed");
}

void frame_work_contract(bool expand) {
    Fixture fixture(expand);
    fixture.reveal->set_duration(10000);
    bool ran{};
    fixture.window.post([&] {
        require(fixture.window.focus(*fixture.files), "Frame fixture receives focus");
        fixture.host = GetAncestor(GetFocus(), GA_ROOT);
        fixture.reveal->set_open(true);
        require(fixture.window.focus(*fixture.input), "Frame fixture retains immediate input");
        fixture.editor = GetFocus();
        fixture.flush();
        const auto before = SendMessageW(fixture.host, metrics, 2, 0);
        const auto start = Reveal::Clock::now();
        std::vector<double> durations;
        const bool motion = fixture.reveal->animating();
        if (motion) {
            for (int frame = 1; frame <= 20; ++frame) {
                const auto sampled = Reveal::Clock::now();
                fixture.reveal->advance(start + std::chrono::milliseconds(frame * 400));
                fixture.flush();
                durations.push_back(std::chrono::duration<double, std::milli>(Reveal::Clock::now() - sampled).count());
                fixture.geometry();
            }
            const auto layouts = SendMessageW(fixture.host, metrics, 2, 0) - before;
            require(layouts == (expand ? 20 : 0), "Each sampled expansion frame performs exactly one root layout");
            std::sort(durations.begin(), durations.end());
            std::cout << "Reveal frame work layout=" << (expand ? "expand" : "fixed")
                << " samples=20 root_layouts=" << layouts << " update_and_paint_ms median=" << durations[10]
                << " p95=" << durations[18] << " max=" << durations[19] << '\n';
        }
        if (expand) {
            const auto progress = fixture.reveal->progress();
            RECT rectangle{};
            require(GetWindowRect(fixture.host, &rectangle) != FALSE, "Read fixture window bounds");
            require(SetWindowPos(fixture.host, nullptr, 0, 0, 740, 520, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE) != FALSE,
                "Resize the active fixture window");
            fixture.flush();
            fixture.geometry();
            require(fixture.reveal->progress() == progress, "Resize does not restart the animation");
            const auto original_dpi = GetDpiForWindow(fixture.host);
            for (const UINT dpi : {144u, 192u, original_dpi}) {
                SendMessageW(fixture.host, WM_DPICHANGED, MAKELONG(dpi, dpi), reinterpret_cast<LPARAM>(&rectangle));
                fixture.flush();
                const auto bounds = fixture.input->bounds();
                RECT native{};
                require(GetWindowRect(fixture.editor, &native) != FALSE, "Read DPI-adjusted editor bounds");
                MapWindowPoints(nullptr, fixture.host, reinterpret_cast<POINT*>(&native), 2);
                require(std::abs((native.top + native.bottom) * 48.0f / dpi - (bounds.y + bounds.height / 2)) < 2,
                    "DPI message aligns native editor geometry with the retained layout");
                require(std::abs(bounds.height - 44) < 0.01f && fixture.reveal->progress() == progress,
                    "DPI message preserves full editor extent and transition progress");
            }
        }
        fixture.reveal->settle();
        fixture.flush();
        require(SendMessageW(fixture.host, metrics, 33, 0) == 0, "Sampled frame work leaves no animation timer");
        ran = true;
        fixture.window.close();
    });
    const auto result = Application::run(fixture.window);
    if (result) std::wcerr << fixture.window.error() << L'\n';
    require(result == 0 && ran, "Frame work and DPI fixture completed");
}

void nested_window_contract() {
    Window window({L"Nested expanding scroll content", {600, 400}});
    auto input = std::make_shared<TextInput>(L"Find");
    input->set_caption_visible(false);
    input->set_preferred_size({320, 44});
    auto inner = std::make_shared<Reveal>(input);
    auto outer = std::make_shared<Reveal>(inner);
    for (const auto& reveal : {inner, outer}) {
        reveal->set_layout(RevealLayout::expand);
        reveal->set_duration(10000);
    }
    auto content = std::make_shared<Stack>(Axis::vertical);
    auto files = std::make_shared<Button>(L"Files");
    files->set_preferred_size({320, 100});
    content->add(files);
    content->add(outer);
    auto footer = std::make_shared<Label>(L"Remaining content");
    footer->set_preferred_size({320, 600});
    content->add(footer);
    auto scroll = std::make_shared<ScrollView>(content);
    auto root = std::make_shared<Stack>(Axis::vertical);
    root->add(scroll, 1);
    window.set_content(root);
    bool ran{};
    window.post([&] {
        require(window.focus(*files), "Nested fixture receives focus");
        const auto hwnd = GetAncestor(GetFocus(), GA_ROOT);
        scroll->set_offset(30);
        SendMessageW(hwnd, update, 0, 0);
        outer->set_open(true);
        inner->set_open(true);
        require(window.focus(*input), "Nested zero-extent opening clips permit immediate input");
        const auto editor = GetFocus();
        SendMessageW(editor, WM_CHAR, L'n', 0);
        const auto now = Reveal::Clock::now() + std::chrono::milliseconds(3000);
        outer->advance(now);
        inner->advance(now);
        SendMessageW(hwnd, update, 0, 0);
        require(std::abs(outer->bounds().height - 44 * inner->progress() * outer->progress()) < 0.01f,
            "Native nested layout uses each transition progress once");
        require(GetFocus() == editor && input->text() == L"n" && scroll->offset() == 30,
            "Nested expansion retains native input and a valid scroll offset");
        require(input->bounds().height == 44, "Nested clips do not shrink the editor");
        outer->set_open(false);
        window.focus(*files);
        SendMessageW(hwnd, update, 0, 0);
        require(!inner->animating() && inner->open(), "Closing an ancestor settles but preserves the nested logical target");
        outer->settle();
        SendMessageW(hwnd, update, 0, 0);
        require(SendMessageW(hwnd, metrics, 33, 0) == 0, "Nested closure leaves no active timer");
        ran = true;
        window.close();
    });
    const auto result = Application::run(window);
    if (result) std::wcerr << window.error() << L'\n';
    require(result == 0 && ran, "Nested scroll window fixture completed");
}

void popup_entry_contract() {
    Window window({L"Popup content entry", {600, 400}});
    auto anchor = std::make_shared<Button>(L"Popup anchor");
    auto root = std::make_shared<Stack>(Axis::vertical);
    root->add(anchor);
    window.set_content(root);
    auto input = std::make_shared<TextInput>(L"Popup animated editor");
    input->set_caption_visible(false);
    input->set_preferred_size({320, 44});
    auto content = std::make_shared<Stack>(Axis::vertical);
    content->add(input);
    content->add(std::make_shared<Button>(L"Retained popup action"));
    auto reveal = std::make_shared<Reveal>(content);
    reveal->set_direction(RevealDirection::top);
    reveal->set_duration(1000);
    auto popup = std::make_shared<Popup>(reveal);
    popup->set_preferred_size({360, 160});
    bool reopen{}, ran{};
    HWND owner{};
    popup->on_dismiss([&](auto) {
        reveal->set_open(false);
        reveal->settle();
        if (reopen) {
            reopen = false;
            reveal->set_open(true);
            const auto foreground = GetForegroundWindow();
            window.show_popup(popup, *anchor, input.get());
            if (foreground != owner) {
                require(GetForegroundWindow() == foreground, "Reentrant popup entry does not steal foreground activation");
                require(window.focus(*input), "Background reentrant popup accepts explicit local editor focus");
            }
        }
    });
    window.post([&] {
        require(window.focus(*anchor), "Popup entry fixture receives focus");
        owner = GetAncestor(GetFocus(), GA_ROOT);
        reveal->set_open(true);
        const auto foreground = GetForegroundWindow();
        window.show_popup(popup, *anchor, input.get());
        if (foreground != owner) {
            require(GetForegroundWindow() == foreground, "Popup entry does not steal foreground activation");
            require(window.focus(*input), "Background popup accepts explicit local editor focus");
        }
        const auto editor = GetFocus();
        wchar_t kind[32]{};
        GetClassNameW(editor, kind, 32);
        require(_wcsicmp(kind, L"EDIT") == 0, "Popup entry focuses its native editor");
        SendMessageW(editor, WM_CHAR, L'p', 0);
        require(input->text() == L"p", "Popup accepts native typing before the first animation frame");
        const auto frame = popup->bounds();
        const auto generation = popup->generation();
        const auto full_height = input->bounds().height;
        const auto initial_y = input->bounds().y;
        BOOL motion{};
        require(SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &motion, 0) != FALSE, "Read popup motion policy");
        require(reveal->animating() == (motion != FALSE), "Popup content follows the system motion preference");
        if (motion) reveal->advance(Animation::Clock::now() + std::chrono::milliseconds(200));
        SendMessageW(owner, update, 0, 0);
        if (motion) require(reveal->progress() > 0 && reveal->progress() < 1 && input->bounds().y > initial_y,
            "Popup entry presents an intermediate translated content frame");
        const auto presented = popup->bounds();
        require(presented.x == frame.x && presented.y == frame.y &&
            presented.width == frame.width && presented.height == frame.height,
            "Popup entry keeps the final edge-aware frame stationary");
        require(input->bounds().height == full_height && GetFocus() == editor && input->text() == L"p",
            "Popup content motion retains full native height, text, and focus");
        reopen = true;
        window.dismiss_popup(*popup);
        require(popup->is_open() && popup->generation() != generation && !popup->current(generation),
            "Reentrant popup entry revokes the old generation immediately");
        require(GetFocus() == editor && input->text() == L"p",
            "Reentrant popup entry retains its native editor without restoring stale focus");
        window.dismiss_popup(*popup);
        SendMessageW(owner, update, 0, 0);
        require(!popup->is_open() && !reveal->animating() && GetFocus() != editor &&
            SendMessageW(owner, metrics, 33, 0) == 0,
            "Popup dismissal remains immediate and leaves no animation clock");
        ran = true;
        window.close();
    });
    const auto result = Application::run(window);
    if (result) std::wcerr << window.error() << L'\n';
    require(result == 0 && ran, "Popup entry fixture completed");
}

std::vector<HWND> native_editors(HWND owner) {
    std::vector<HWND> result;
    EnumChildWindows(owner, [](HWND child, LPARAM context) -> BOOL {
        wchar_t kind[32]{};
        GetClassNameW(child, kind, 32);
        if (_wcsicmp(kind, L"EDIT") == 0)
            reinterpret_cast<std::vector<HWND>*>(context)->push_back(child);
        return TRUE;
    }, reinterpret_cast<LPARAM>(&result));
    return result;
}

void deferred_popup_peers_contract(bool expand) {
    Window window({L"Deferred popup reveal peers", {640, 480}});
    window.set_presentation("Consolas", 18, true, false);
    auto anchor = std::make_shared<Button>(L"Settings");
    auto root = std::make_shared<Stack>(Axis::vertical);
    root->add(anchor);
    window.set_content(root);
    auto content = std::make_shared<Stack>(Axis::vertical);
    auto action = std::make_shared<Button>(L"Popup focus target");
    content->add(action);
    constexpr unsigned row_count = 64;
    std::vector<std::shared_ptr<Reveal>> reveals;
    std::vector<std::shared_ptr<TextInput>> inputs;
    for (unsigned row = 0; row < row_count; ++row) {
        auto children = std::make_shared<Stack>(Axis::vertical);
        auto input = std::make_shared<TextInput>(L"Setting " + std::to_wstring(row));
        input->set_caption_visible(false);
        input->set_preferred_size({320, 44});
        children->add(input);
        children->add(std::make_shared<Label>(L"Setting description"));
        children->add(std::make_shared<Button>(L"Reset setting"));
        auto reveal = std::make_shared<Reveal>(children);
        reveal->set_duration(0);
        if (expand) reveal->set_layout(RevealLayout::expand);
        content->add(reveal);
        reveals.push_back(reveal);
        inputs.push_back(input);
    }
    auto popup = std::make_shared<Popup>(content);
    popup->set_preferred_size({400, 280});
    bool ran{};
    window.post([&] {
        require(window.focus(*anchor), "Deferred popup fixture receives focus");
        const auto owner = GetAncestor(GetFocus(), GA_ROOT);
        const auto peers = [&] { return SendMessageW(owner, metrics, 14, 0); };
        const auto baseline = peers();
        require(native_editors(owner).empty(), "Popup fixture starts without native editors");
        window.show_popup(popup, *anchor, action.get());
        const auto hidden_peers = peers();
        require(hidden_peers == baseline + row_count + 2 && native_editors(owner).empty(),
            "Closed reveals retain 192 model controls without creating their native peers");
        require(!window.focus(*inputs.front()) && peers() == hidden_peers,
            "Focusing a closed reveal cannot realize its hidden editor");
        auto& reveal = reveals.front();
        auto& input = inputs.front();
        reveal->set_open(true);
        require(window.focus(*input), "Opening synchronously realizes the editor before assigning focus");
        const auto editor = GetFocus();
        require(native_editors(owner) == std::vector<HWND>{editor} && peers() == hidden_peers + 3,
            "Opening one row realizes only that row's three controls");
        LOGFONTW font{};
        require(GetObjectW(reinterpret_cast<HFONT>(SendMessageW(editor, WM_GETFONT, 0, 0)), sizeof(font), &font) != 0 &&
            std::wstring(font.lfFaceName) == L"Consolas" &&
            std::abs(font.lfHeight + std::lround(18 * GetDpiForWindow(editor) / 96.0f)) <= 1,
            "Deferred native editors inherit the current window typography");
        SendMessageW(editor, WM_CHAR, L'a', 0);
        SendMessageW(editor, WM_CHAR, L'b', 0);
        SendMessageW(editor, WM_CHAR, L'c', 0);
        require(input->text() == L"abc" && SendMessageW(editor, EM_CANUNDO, 0, 0),
            "The newly realized editor accepts native typing and records undo");
        SendMessageW(editor, EM_SETSEL, 1, 2);
        const auto opened_peers = peers();
        reveal->set_open(false);
        require(window.focus(*action), "Closing the row returns focus within the same popup");
        SendMessageW(owner, update, 0, 0);
        require(popup->is_open() && IsWindow(editor) && !IsWindowVisible(editor) &&
            !window.focus(*input) && peers() == opened_peers,
            "Closing a realized reveal retains its peers without permitting hidden input");
        reveal->set_open(true);
        require(window.focus(*input) && GetFocus() == editor && peers() == opened_peers,
            "Reopening an attached reveal preserves native editor identity");
        DWORD start{}, end{};
        SendMessageW(editor, EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));
        POINT caret{};
        require(start == 1 && end == 2 && GetCaretPos(&caret) && input->text() == L"abc" &&
            SendMessageW(editor, EM_CANUNDO, 0, 0),
            "Reopening preserves native selection, caret, model text, and undo history");
        require(SendMessageW(editor, EM_UNDO, 0, 0) && input->text().empty(),
            "Preserved undo reverses the original native typing");
        SendMessageW(editor, WM_CHAR, L'z', 0);
        reveal->set_open(false);
        require(window.focus(*action), "Return focus before dismissing the settings popup");
        window.dismiss_popup(*popup);
        SendMessageW(owner, update, 0, 0);
        require(!popup->is_open() && !IsWindow(editor) && native_editors(owner).empty() && peers() == baseline,
            "Dismissal destroys even closed reveal peers and restores the baseline");
        require(input->text() == L"z", "Popup dismissal retains edited model text");
        window.show_popup(popup, *anchor, action.get());
        require(peers() == hidden_peers && native_editors(owner).empty(),
            "Fresh popup opening defers previously realized closed rows instead of caching peers");
        reveal->set_open(true);
        require(window.focus(*input), "A fresh popup can realize its retained editor again");
        const auto fresh_editor = GetFocus();
        wchar_t text[8]{};
        GetWindowTextW(fresh_editor, text, 8);
        require(std::wstring(text) == L"z" && input->text() == L"z" &&
            !SendMessageW(fresh_editor, EM_CANUNDO, 0, 0),
            "A fresh native editor receives model text, not the dismissed peer's undo history");
        window.dismiss_popup(*popup);
        SendMessageW(owner, update, 0, 0);
        require(peers() == baseline && native_editors(owner).empty() &&
            SendMessageW(owner, metrics, 33, 0) == 0,
            "Repeated popup dismissal leaves no peers or animation timer");
        ran = true;
        window.close();
    });
    const auto result = Application::run(window);
    if (result) std::wcerr << window.error() << L'\n';
    require(result == 0 && ran, "Deferred popup peer contract completed");
}

void deferred_hidden_replacement_contract() {
    Window window({L"Deferred nested content replacement", {600, 400}});
    auto anchor = std::make_shared<Button>(L"Outside focus");
    auto old_content = std::make_shared<Stack>(Axis::vertical);
    old_content->add(std::make_shared<TextInput>(L"Never realized"));
    std::weak_ptr<Element> retired = old_content;
    auto host = std::make_shared<ContentHost>(old_content);
    old_content.reset();
    auto inner = std::make_shared<Reveal>(host);
    auto outer = std::make_shared<Reveal>(inner);
    inner->set_duration(0);
    outer->set_duration(0);
    auto slot = std::make_shared<ContentHost>(outer);
    auto root = std::make_shared<Stack>(Axis::vertical);
    root->add(anchor);
    root->add(slot);
    window.set_content(root);
    bool ran{};
    window.post([&] {
        require(window.focus(*anchor), "Hidden replacement fixture receives focus");
        const auto owner = GetAncestor(GetFocus(), GA_ROOT);
        const auto baseline = SendMessageW(owner, metrics, 14, 0);
        require(native_editors(owner).empty(), "Nested closed reveals have no editor peers");
        Window other;
        other.set_content(host);
        bool rejected{};
        try { other.replace_content(*host, {}); }
        catch (const std::invalid_argument&) { rejected = true; }
        require(rejected && host->content() && !retired.expired(),
            "Hidden descendants remain claimed by their live window without native peers");
        other.set_content(std::make_shared<Stack>(Axis::vertical));
        auto replacement = std::make_shared<TextInput>(L"Replacement editor");
        replacement->set_caption_visible(false);
        replacement->set_text(L"Retained replacement");
        window.replace_content(*host, replacement);
        require(host->content() == replacement && retired.expired() &&
            !outer->open() && !inner->open() && !window.focus(*replacement),
            "Replacing nested hidden content retires the old model without opening its ancestors");
        outer->set_open(true);
        inner->set_open(true);
        require(window.focus(*replacement), "Nested replacement realizes synchronously when opened");
        const auto editor = GetFocus();
        wchar_t text[64]{};
        GetWindowTextW(editor, text, 64);
        require(native_editors(owner) == std::vector<HWND>{editor} && std::wstring(text) == L"Retained replacement",
            "Opening nested reveals realizes only the replacement model");
        outer->set_open(false);
        require(window.focus(*anchor), "Retirement leaves focus outside the hidden subtree");
        SendMessageW(owner, update, 0, 0);
        require(IsWindow(editor), "Closing the nested ancestor retains its realized editor");
        window.replace_content(*slot, {});
        require(!IsWindow(editor) && native_editors(owner).empty() &&
            SendMessageW(owner, metrics, 14, 0) == baseline - 1,
            "Retiring a closed subtree destroys its retained peers");
        other.set_content(host);
        other.replace_content(*host, {});
        require(!host->content(),
            "Retired hidden descendants release their live-window claims for another window");
        ran = true;
        window.close();
    });
    const auto result = Application::run(window);
    if (result) std::wcerr << window.error() << L'\n';
    require(result == 0 && ran, "Deferred hidden replacement contract completed");
}

void modal_entry_contract(VisualStyle style) {
    WindowOptions options{L"Modal content entry", {600, 440}};
    options.visual_style = style;
    Window window(options);
    auto anchor = std::make_shared<Button>(L"Open modal");
    auto root = std::make_shared<Stack>(Axis::vertical);
    root->add(anchor);
    window.set_content(root);
    auto input = std::make_shared<TextInput>(L"Modal native editor");
    input->set_caption_visible(false);
    input->set_preferred_size({320, 44});
    auto reveal = std::make_shared<Reveal>(input);
    reveal->set_duration(1000);
    auto dialog = std::make_shared<ContentDialog>(L"Animated modal", reveal);
    dialog->on_validate([&] { return input->text().empty() ? L"A value is required." : L""; });
    int results{};
    bool ran{};
    dialog->on_result([&](DialogResult result) {
        ++results;
        require(result == DialogResult::primary && !dialog->popup()->is_open(),
            "Modal result follows logical closure immediately");
        reveal->set_open(false);
        reveal->settle();
    });
    window.post([&] {
        require(window.focus(*anchor), "Modal entry fixture receives focus");
        const auto anchor_hwnd = GetFocus();
        const auto owner = GetAncestor(anchor_hwnd, GA_ROOT);
        reveal->set_open(true);
        const auto foreground = GetForegroundWindow();
        window.show_dialog(dialog, *anchor, input.get());
        if (foreground != owner) {
            require(GetForegroundWindow() == foreground, "Modal entry does not steal foreground activation");
            require(window.focus(*input), "Background modal accepts explicit local editor focus");
        }
        const auto editor = GetFocus();
        require(editor != anchor_hwnd && !IsWindowEnabled(anchor_hwnd) && !window.focus(*anchor),
            "Modal exclusion applies before the first animation frame");
        const auto generation = dialog->popup()->generation();
        const auto frame = dialog->popup()->bounds();
        const auto initial_y = input->bounds().y;
        const auto height = input->bounds().height;
        BOOL motion{};
        require(SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &motion, 0) != FALSE, "Read modal motion policy");
        require(reveal->animating() == (motion != FALSE), "Modal content follows the system motion preference");
        if (motion) reveal->advance(Animation::Clock::now() + std::chrono::milliseconds(200));
        SendMessageW(owner, update, 0, 0);
        if (motion) require(reveal->progress() > 0 && reveal->progress() < 1 && input->bounds().y < initial_y,
            "Modal form presents an intermediate entry frame");
        const auto presented = dialog->popup()->bounds();
        require(presented.x == frame.x && presented.y == frame.y &&
            presented.width == frame.width && presented.height == frame.height,
            "Modal frame stays at its final position");
        dialog->accept();
        require(dialog->popup()->current(generation) && results == 0 && dialog->validation()->visible(),
            "Validation failure preserves the open modal generation during entry");
        SendMessageW(editor, WM_CHAR, L'm', 0);
        require(input->text() == L"m" && GetFocus() == editor && input->bounds().height == height,
            "Modal entry preserves immediate native input, focus, and full field height");
        dialog->accept();
        SendMessageW(owner, update, 0, 0);
        require(results == 1 && !dialog->popup()->current(generation) && IsWindowEnabled(anchor_hwnd) &&
            GetFocus() == anchor_hwnd && SendMessageW(owner, metrics, 33, 0) == 0,
            "Modal acceptance immediately restores owner input and stops entry motion");
        ran = true;
        window.close();
    });
    const auto result = Application::run(window);
    if (result) std::wcerr << window.error() << L'\n';
    require(result == 0 && ran, "Modal entry fixture completed");
}
void pane_pixel_contract() {
    Window window({L"Animated pane pixels", {1000, 400}, ThemeMode::light});
    auto first = std::make_shared<Button>(L"First pane");
    auto second = std::make_shared<Stack>(Axis::vertical);
    auto input = std::make_shared<TextInput>(L"Incoming pane editor");
    input->set_caption_visible(false);
    input->set_preferred_size({320, 44});
    second->add(input);
    PartStyleValues style;
    style.background = ThemeColor{0x12B456};
    second->set_control_style_values(StylePart::root, style);
    auto split = std::make_shared<SplitView>(first, second);
    split->set_secondary_visible(false);
    split->set_transition_duration(10000);
    auto root = std::make_shared<Stack>(Axis::horizontal);
    root->add(split, 1);
    auto neighbor = std::make_shared<Label>(L"Neighbor");
    neighbor->set_fixed_size({100, 400});
    root->add(neighbor);
    window.set_content(root);
    bool ran{};
    window.post([&] {
        require(window.focus(*first), "Pane fixture receives focus");
        const auto hwnd = GetAncestor(GetFocus(), GA_ROOT);
        split->set_secondary_visible(true);
        require(window.focus(*input), "The incoming offscreen pane accepts focus immediately");
        const auto editor = GetFocus();
        SendMessageW(editor, WM_CHAR, L'p', 0);
        const auto full_width = input->bounds().width;
        if (split->animating()) split->advance(Animation::Clock::now() + std::chrono::milliseconds(1500));
        SendMessageW(hwnd, update, 0, 0);
        require(input->text() == L"p" && GetFocus() == editor && input->bounds().width == full_width,
            "Pane movement preserves native text, focus, and full editor width");
        require(RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW) != FALSE,
            "Paint the intermediate pane frame");
        winrt::check_hresult(DwmFlush());
        auto pixels = owned_window_capture::capture(hwnd);
        const auto pixel = [&](float x, float y) {
            const float scale = GetDpiForWindow(hwnd) / 96.0f;
            const int column = static_cast<int>(std::lround(x * scale)), row = static_cast<int>(std::lround(y * scale));
            require(column >= 0 && column < pixels.width && row >= 0 && row < pixels.height, "Pane pixel is inside the owned window");
            return pixels.data[static_cast<std::size_t>(row) * pixels.width + column] & 0xffffff;
        };
        HIGHCONTRASTW contrast{sizeof(contrast)};
        require(SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) != FALSE, "Read contrast preference");
        const auto area = split->bounds();
        if (!(contrast.dwFlags & HCF_HIGHCONTRASTON)) {
            require(pixel(area.x + area.width - 20, area.y + area.height - 40) == 0x12B456,
                "The incoming pane paints at its intermediate position");
            require(pixel(area.x + area.width + 20, area.y + area.height - 40) != 0x12B456,
                "Pane backgrounds and native content cannot escape the split viewport");
        }
        split->settle();
        SendMessageW(hwnd, update, 0, 0);
        const auto initial_width = first->bounds().width;
        split->set_ratio(0.65f);
        SendMessageW(hwnd, update, 0, 0);
        const bool ratio_motion = split->animating();
        if (ratio_motion) split->advance(Animation::Clock::now() + std::chrono::milliseconds(2500));
        SendMessageW(hwnd, update, 0, 0);
        const auto target_width = (split->pane_area().width - split->effective_divider_width()) * 0.65f;
        require(split->ratio() == 0.65f && split->progress() == 1,
            "Ratio targets are immediate while visibility remains fully expanded");
        require(ratio_motion ? first->bounds().width > initial_width && first->bounds().width < target_width :
            std::abs(first->bounds().width - target_width) < 0.01f,
            "Ratio presets present intermediate widths or settle for reduced motion");
        require(GetFocus() == editor && input->text() == L"p" &&
            SendMessageW(editor, EM_GETSEL, 0, 0) == MAKELONG(1, 1) && SendMessageW(editor, EM_CANUNDO, 0, 0),
            "Ratio layout preserves native focus, text, caret, and undo");
        RECT editor_rect{};
        require(GetWindowRect(editor, &editor_rect) != FALSE, "Read resized native pane editor");
        MapWindowPoints(nullptr, hwnd, reinterpret_cast<POINT*>(&editor_rect), 2);
        const float scale = GetDpiForWindow(hwnd) / 96.0f;
        require(std::abs((editor_rect.left + editor_rect.right) / (2 * scale) -
            (input->bounds().x + input->bounds().width / 2)) < 2,
            "The native editor follows the presented ratio geometry");
        require(RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW) != FALSE,
            "Paint the intermediate ratio frame");
        winrt::check_hresult(DwmFlush());
        pixels = owned_window_capture::capture(hwnd);
        if (!(contrast.dwFlags & HCF_HIGHCONTRASTON)) {
            const auto divider = split->divider();
            require(pixel(divider.x + divider.width + 15, area.y + area.height - 40) == 0x12B456 &&
                pixel(divider.x - 15, area.y + area.height - 40) != 0x12B456,
                "Both pane backgrounds follow the presented ratio divider");
        }
        split->set_secondary_visible(false);
        window.focus(*first);
        SendMessageW(hwnd, update, 0, 0);
        require(!IsWindowEnabled(editor) && !window.focus(*input), "Closing pane input is disabled before exit completion");
        split->settle();
        SendMessageW(hwnd, update, 0, 0);
        require(second->bounds().width == 0 && SendMessageW(hwnd, metrics, 33, 0) == 0,
            "Closed pane releases its geometry and timer");
        ran = true;
        window.close();
    });
    const auto result = Application::run(window);
    if (result) std::wcerr << window.error() << L'\n';
    require(result == 0 && ran, "Pane pixel fixture completed");
}
}
#include "portable_reveal_window.inc"
int main(int argc, char** argv) {
    try {
        // Keep cached Graphics Capture factories in one apartment across fixture windows.
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        struct Apartment {
            ~Apartment() { winrt::clear_factory_cache(); winrt::uninit_apartment(); }
        } apartment;
        if (argc == 2 && std::string_view(argv[1]) == "--portable-only") {
            portable_reveal_window::run();
            return 0;
        }
        if (argc == 2 && std::string_view(argv[1]) == "--popup-only") {
            popup_entry_contract();
            std::cout << "Reveal popup entry passed\n";
            return 0;
        }
        deferred_popup_peers_contract(false);
        deferred_popup_peers_contract(true);
        deferred_hidden_replacement_contract();
        std::cerr << "Reveal deferred peers, popup cleanup, and hidden replacement passed\n";
        if (argc == 2 && std::string_view(argv[1]) == "--deferred-only") return 0;
        for (const bool expand : {false, true}) {
            std::cerr << "Reveal window layout=" << (expand ? "expand" : "fixed") << '\n';
            Fixture fixture(expand);
            Fixture::current = &fixture;
            fixture.window.post([&] { fixture.begin(); });
            const auto result = Application::run(fixture.window);
            Fixture::current = nullptr;
            if (fixture.error) std::rethrow_exception(fixture.error);
            if (result) std::wcerr << fixture.window.error() << L'\n';
            require(result == 0 && fixture.done, "Reveal window fixture completed");
            require(!fixture.reveal->animating(), "Window closure cancels motion even when controls remain retained");
        }
        retirement_contract();
        retirement_contract(true);
        std::cerr << "Reveal retirement passed\n";
        concurrent_contract();
        concurrent_contract(true);
        std::cerr << "Reveal concurrent passed\n";
        for (const bool expand : {false, true})
            for (const auto direction : {RevealDirection::bottom, RevealDirection::top, RevealDirection::left, RevealDirection::right})
                pixel_contract(expand, direction);
        frame_work_contract(false);
        frame_work_contract(true);
        nested_window_contract();
        popup_entry_contract();
        modal_entry_contract(VisualStyle::classic);
        modal_entry_contract(VisualStyle::winui);
        pane_pixel_contract();
        std::cout << "Reveal window: native input, motion, reversal, focus, system policy, idle work, and closure passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
