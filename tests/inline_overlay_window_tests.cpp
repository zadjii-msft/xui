#include "xui/application.hpp"
#include "xui/adaptive_layout.hpp"
#include "xui/collections.hpp"
#include "xui/reveal.hpp"
#include "owned_window_capture.hpp"
#include <windowsx.h>
#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>

namespace {
using namespace xui;
constexpr COLORREF panel_color = 0xb04070;
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void pump() {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
    do {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    } while (std::chrono::steady_clock::now() < end);
}
HWND owner_window() {
    HWND result{};
    EnumWindows([](HWND hwnd, LPARAM data) -> BOOL {
        DWORD process{};
        GetWindowThreadProcessId(hwnd, &process);
        wchar_t title[128]{};
        GetWindowTextW(hwnd, title, 128);
        if (process == GetCurrentProcessId() && std::wstring_view(title) == L"Inline overlay regression") {
            *reinterpret_cast<HWND*>(data) = hwnd;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&result));
    require(result != nullptr, "Find the owned overlay window");
    return result;
}
HWND named(HWND owner, const wchar_t* name) {
    struct Search { const wchar_t* name; HWND result{}; } search{name};
    EnumChildWindows(owner, [](HWND hwnd, LPARAM data) -> BOOL {
        auto& search = *reinterpret_cast<Search*>(data);
        wchar_t text[128]{};
        GetWindowTextW(hwnd, text, 128);
        if (std::wstring_view(text) == search.name) { search.result = hwnd; return FALSE; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    require(search.result != nullptr, "Find the named native peer");
    return search.result;
}
POINT screen_point(HWND owner, Point point) {
    const auto dpi = GetDpiForWindow(owner);
    POINT result{static_cast<LONG>(std::lround(point.x * dpi / 96)),
        static_cast<LONG>(std::lround(point.y * dpi / 96))};
    ClientToScreen(owner, &result);
    return result;
}
HWND hit(HWND owner, Point point) {
    const auto screen = screen_point(owner, point);
    HWND target = owner;
    for (;;) {
        auto client = screen;
        ScreenToClient(target, &client);
        const auto child = ChildWindowFromPointEx(target, client, CWP_SKIPINVISIBLE | CWP_SKIPDISABLED);
        if (!child || child == target) return target;
        target = child;
    }
}
void click(HWND owner, Point point) {
    const auto target = hit(owner, point);
    auto client = screen_point(owner, point);
    ScreenToClient(target, &client);
    const auto position = MAKELPARAM(client.x, client.y);
    SendMessageW(target, WM_MOUSEMOVE, 0, position);
    SendMessageW(target, WM_LBUTTONDOWN, MK_LBUTTON, position);
    SendMessageW(target, WM_LBUTTONUP, 0, position);
    pump();
}
struct Rows final : ItemsSource {
    std::size_t size() const override { return 60; }
    ItemKey key(std::size_t index) const override { return {index + 1, 1}; }
    std::optional<std::size_t> find(ItemKey key) const override {
        if (key.version == 1 && key.id && key.id <= size()) return key.id - 1;
        return {};
    }
    ItemContent item(std::size_t) const override {
        ItemContent item;
        item.primary = L"Underlying playlist row";
        item.action = L"More";
        return item;
    }
};
void run() {
    Application app;
    WindowOptions options;
    options.title = L"Inline overlay regression";
    options.size = {600, 280};
    options.visual_style = VisualStyle::winui;
    options.show_activated = false;
    auto owned = app.create_window(options);
    auto& window = *owned;
    auto grid = std::make_shared<Grid>();
    grid->set_tracks({{TrackSizing::fixed, 80}, {TrackSizing::fixed, 120}, {}},
        {{TrackSizing::fixed, 160}, {TrackSizing::fixed, 400}, {}});
    auto list = std::make_shared<ItemsView>(L"Underlying rows");
    list->set_items(std::make_shared<Rows>());
    unsigned actions{}, dismissals{};
    list->on_action([&](ItemKey) { ++actions; });
    grid->add(list, 0, 0, 3, 3);
    auto editor = std::make_shared<TextInput>(L"Underlying editor");
    editor->set_caption_visible(false);
    editor->set_text(L"Native selection and draft");
    grid->add(editor, 1, 1);
    auto transparent_label = std::make_shared<Label>(L"Transparent annotation");
    PartStyleValues annotation;
    annotation.foreground = ThemeColor{0x12ff34};
    transparent_label->set_control_style_values(StylePart::label, annotation);
    transparent_label->set_visible(false);
    grid->add(transparent_label, 1, 1);
    auto panel = std::make_shared<Stack>(Axis::vertical);
    panel->set_padding({16, 16, 16, 16});
    PartStyleValues face;
    face.background = ThemeColor{panel_color};
    face.border_brush = ThemeColor{0xffffff};
    face.border_thickness = Insets{1, 1, 1, 1};
    face.corner_radius = 8.0f;
    panel->set_control_style_values(StylePart::root, face);
    panel->add(std::make_shared<Label>(L"Notification above the playlist"));
    auto dismiss = std::make_shared<Button>(L"Dismiss notification");
    panel->add(dismiss);
    auto reveal = std::make_shared<Reveal>(panel, L"Inline notification");
    reveal->set_duration(0);
    reveal->set_open(true);
    dismiss->on_click([&] { ++dismissals; reveal->set_open(false); });
    grid->add(reveal, 1, 1);
    window.set_content(grid);
    const auto foreground = GetForegroundWindow();
    app.show(window);
    pump();
    const auto owner = owner_window();
    require(GetForegroundWindow() == foreground, "Show the notification without activation");
    const auto list_hwnd = named(owner, L"Underlying rows");
    const auto dismiss_hwnd = named(owner, L"Dismiss notification");
    owned_window_capture::Device device;
    const auto image = [&] {
        InvalidateRect(owner, nullptr, FALSE);
        UpdateWindow(owner);
        pump();
        return owned_window_capture::capture(owner, device);
    };
    const auto pixel = [&](const owned_window_capture::Pixels& pixels, Point point) {
        const auto dpi = GetDpiForWindow(owner);
        const auto x = static_cast<int>(std::lround(point.x * dpi / 96));
        const auto y = static_cast<int>(std::lround(point.y * dpi / 96));
        require(x >= 0 && y >= 0 && x < pixels.width && y < pixels.height, "Read only owned client pixels");
        return pixels.data[static_cast<std::size_t>(y) * pixels.width + x] & 0xffffff;
    };
    const auto sample = [&] {
        const auto bounds = panel->bounds();
        return Point{bounds.x + bounds.width - 12, bounds.y + bounds.height - 16};
    };
    const auto button_point = [&] {
        const auto bounds = dismiss->bounds();
        return Point{bounds.x + bounds.width / 2, bounds.y + bounds.height / 2};
    };
    HWND retained_editor{};
    for (const auto theme : {ThemeMode::dark, ThemeMode::light}) {
        window.set_theme(theme);
        for (unsigned cycle = 0; cycle < 3; ++cycle) {
            reveal->set_open(true);
            pump();
            const auto position = sample();
            const Point corner{panel->bounds().x + 1, panel->bounds().y + 1};
            reveal->set_open(false);
            editor->set_visible(false);
            pump();
            const auto clear = image();
            require(pixel(clear, position) != panel_color, "Closing restores the underlying collection pixels");
            editor->set_visible(true);
            pump();
            require(window.focus(*editor), "Focus the native editor before opening the overlay");
            const auto focus = GetFocus();
            if (!retained_editor) retained_editor = focus;
            require(focus == retained_editor, "Show and hide preserve native editor identity");
            const auto active = GetActiveWindow();
            SendMessageW(focus, EM_SETSEL, 1, 6);
            const auto selection = SendMessageW(focus, EM_GETSEL, 0, 0);
            SendMessageW(focus, WM_IME_STARTCOMPOSITION, 0, 0);
            reveal->set_open(true);
            pump();
            require(GetFocus() == focus && GetActiveWindow() == active && GetForegroundWindow() == foreground,
                "Opening changes neither focus nor activation");
            require(SendMessageW(focus, EM_GETSEL, 0, 0) == selection, "Opening preserves native selection");
            const auto opaque = image();
            require(pixel(opaque, sample()) == panel_color, "The complete opaque subtree covers native and collection pixels");
            require(GetFocus() == focus && editor->text() == L"Native selection and draft",
                "Overlay painting preserves the native editor and composition draft");
            SendMessageW(focus, WM_IME_ENDCOMPOSITION, 0, 0);
            require(hit(owner, button_point()) == dismiss_hwnd, "Native sibling hit testing finds the declared overlay button");
            editor->set_visible(false);
            pump();
            const auto no_editor = image();
            require(pixel(no_editor, sample()) == panel_color, "The collection cannot overwrite the notification surface");
            require(pixel(no_editor, button_point()) != panel_color, "The notification surface does not cover its own button");
            const auto panel_bounds = panel->bounds();
            for (float y = panel_bounds.height - 16; y < panel_bounds.height - 8; ++y)
                for (float x = panel_bounds.width - 64; x < panel_bounds.width - 16; ++x)
                    require(pixel(no_editor, {panel_bounds.x + x, panel_bounds.y + y}) == panel_color,
                        "Underlying More glyphs cannot bleed through the opaque panel");
            require(pixel(no_editor, corner) == pixel(clear, corner), "Rounded corners preserve the underlying pixels");
            const auto viewport = list->content_viewport();
            const auto bounds = list->bounds();
            const Point action{bounds.x + viewport.x + viewport.width - 35, bounds.y + viewport.y + 20};
            require(hit(owner, action) == list_hwnd, "An outside row action remains a native input target");
            const auto before = actions;
            click(owner, action);
            require(actions == before + 1, "The outside More action runs while the overlay is open");
            const auto old_offset = list->offset();
            const auto scroll_focus = GetFocus();
            list->set_offset(old_offset + 48);
            pump();
            require(list->offset() > old_offset && pixel(image(), sample()) == panel_color,
                "Scrolling repaints the list beneath the complete overlay");
            require(GetFocus() == scroll_focus && GetForegroundWindow() == foreground, "Scroll preserves focus and activation");
            const auto width = MulDiv(cycle & 1 ? 640 : 600, GetDpiForWindow(owner), 96);
            require(SetWindowPos(owner, nullptr, 0, 0, width, MulDiv(320, GetDpiForWindow(owner), 96),
                SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE) != FALSE, "Resize the owned window without activation");
            const auto resize_focus = GetFocus();
            pump();
            require(GetFocus() == resize_focus && GetForegroundWindow() == foreground, "Resize preserves focus and activation");
            require(pixel(image(), sample()) == panel_color, "Resize preserves overlay paint order");
            const auto before_dismiss = dismissals;
            click(owner, button_point());
            require(dismissals == before_dismiss + 1 && !reveal->open(), "Ordinary native input dismisses the overlay");
        }
    }
    // A fixed Reveal translates its child through an unchanged viewport.
    reveal->set_open(true);
    pump();
    const auto bounds = reveal->bounds();
    const Point outside{bounds.x + bounds.width - 12, bounds.y + bounds.height + 5};
    const auto complete = image();
    reveal->set_duration(600);
    reveal->set_open(false);
    reveal->advance(Animation::Clock::now() + std::chrono::milliseconds(250));
    pump();
    require(pixel(image(), outside) == pixel(complete, outside), "Reveal motion cannot paint outside its ancestor clip");
    reveal->settle();
    reveal->set_duration(0);
    pump();
    editor->set_visible(true);
    pump();
    require(window.focus(*editor), "Restore focus to the same native editor");
    const auto editor_hwnd = GetFocus();
    require(editor_hwnd == retained_editor, "The same editor survives overlay cycles, scrolling, and resize");
    editor->set_text(L"");
    pump();
    for (const auto theme : {ThemeMode::dark, ThemeMode::light}) {
        window.set_theme(theme);
        transparent_label->set_visible(false);
        pump();
        const auto bare = image();
        const auto field = editor->bounds();
        const Point blank{field.x + field.width - 24, field.y + field.height - 24};
        const auto native_background = pixel(bare, blank);
        const auto native_selection = SendMessageW(editor_hwnd, EM_GETSEL, 0, 0);
        transparent_label->set_visible(true);
        pump();
        const auto overlaid = image();
        require(pixel(overlaid, blank) == native_background, "Transparent annotation preserves the native editor background");
        unsigned green{};
        const auto label = transparent_label->bounds();
        for (float y = 12; y < label.height - 12; ++y)
            for (float x = 12; x < label.width - 12; ++x) {
                const auto color = pixel(overlaid, {label.x + x, label.y + y});
                const auto red = (color >> 16) & 255, blue = color & 255, ink = (color >> 8) & 255;
                if (ink > red + 30 && ink > blue + 30) ++green;
            }
        require(green > 4, "Transparent annotation text paints above the captured native editor");
        require(GetFocus() == editor_hwnd && SendMessageW(editor_hwnd, EM_GETSEL, 0, 0) == native_selection,
            "Transparent annotation preserves editor focus and selection");
        transparent_label->set_visible(false);
        pump();
        require(pixel(image(), blank) == native_background, "Hiding transparent annotation restores the live native editor");
    }
    SendMessageW(editor_hwnd, WM_CHAR, L'X', 0);
    pump();
    require(editor->text().find(L'X') != std::wstring::npos, "The original editor retains native text input");
    require(GetForegroundWindow() == foreground, "Overlay operations never activate another application");
    window.close();
}
}
int main() {
    try {
        run();
        std::cout << "Inline overlay pixels and native input passed in dark and light themes.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
