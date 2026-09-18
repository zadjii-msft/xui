#include "xui/application.hpp"
#include "xui/documents.hpp"
#include "xui/navigation.hpp"
#include "xui/menu_bar.hpp"
#include "../src/drawing.hpp"
#include "owned_window_capture.hpp"
#include <commctrl.h>
#include <UIAutomation.h>
#include <atomic>
#include <thread>
#include <psapi.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <exception>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace xui {
struct DrawingTestAccess {
    static void software_target(Drawing& drawing, HWND hwnd) {
        RECT client{};
        if (!GetClientRect(hwnd, &client)) throw std::runtime_error("Read software target size");
        const auto result = drawing.factory_->CreateHwndRenderTarget(
            D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
                96, 96, D2D1_RENDER_TARGET_USAGE_GDI_COMPATIBLE),
            D2D1::HwndRenderTargetProperties(hwnd, D2D1::SizeU(client.right, client.bottom)),
            &drawing.target_);
        if (FAILED(result)) throw std::runtime_error("Create readable Direct2D software HWND target");
        ++Drawing::live_targets_;
        if (FAILED(drawing.target_->CreateSolidColorBrush(D2D1::ColorF(0), &drawing.brush_)))
            throw std::runtime_error("Create software target brush");
        drawing.target_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_ALIASED);
    }
    static Microsoft::WRL::ComPtr<ID2D1GdiInteropRenderTarget> interop(Drawing& drawing) {
        Microsoft::WRL::ComPtr<ID2D1GdiInteropRenderTarget> result;
        if (FAILED(drawing.target_.As(&result))) throw std::runtime_error("Query Direct2D pixel readback");
        return result;
    }
    static void observe(void (*callback)(HWND)) { Drawing::present_observer_ = callback; }
    static void lose_target() { Drawing::end_result_override_ = D2DERR_RECREATE_TARGET; }
    static void flush(Drawing& drawing) {
        if (FAILED(drawing.target_->Flush())) throw std::runtime_error("Flush software raster work");
    }
    static void stroke(Drawing& drawing, Rect bounds, float radius, D2D1_COLOR_F color, float thickness) {
        drawing.brush_->SetColor(color);
        drawing.target_->DrawRoundedRectangle(D2D1::RoundedRect(
            D2D1::RectF(bounds.x, bounds.y, bounds.x + bounds.width, bounds.y + bounds.height), radius, radius),
            drawing.brush_.Get(), thickness);
    }
};
}

namespace {
using namespace xui;
constexpr wchar_t window_title[] = L"XUI authored Button style contracts";
constexpr uint32_t sentinel = 0x132537;
constexpr Rect face{12, 12, 120, 56};
bool trace_resources_enabled{};
bool benchmark_styled_first{};
constexpr int benchmark_frames = 600;
enum class FocusFixture { all, buttons, choices, selectors, expanders };

void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void success(HRESULT result, const char* message) {
    if (FAILED(result)) throw std::runtime_error(std::string(message) + ": " + std::to_string(result));
}
uint32_t rgb(COLORREF value) {
    return (uint32_t(GetRValue(value)) << 16) | (uint32_t(GetGValue(value)) << 8) | GetBValue(value);
}
int color_distance(uint32_t first, uint32_t second) {
    return std::max({std::abs(int(first & 255) - int(second & 255)),
        std::abs(int((first >> 8) & 255) - int((second >> 8) & 255)),
        std::abs(int((first >> 16) & 255) - int((second >> 16) & 255))});
}
#ifndef XUI_STYLING_BASELINE
struct Pixels {
    int width{}, height{};
    std::vector<DWORD> data;
    uint32_t at(int x, int y) const {
        require(x >= 0 && y >= 0 && x < width && y < height, "Pixel sample lies inside captured target");
        return data[static_cast<std::size_t>(y) * width + x] & 0xffffff;
    }
    void expect(int x, int y, uint32_t expected, const char* message) const {
        const auto actual = at(x, y);
        if (color_distance(actual, expected) > 2) {
            std::cerr << message << " at " << x << ',' << y << ": actual=0x" << std::hex << actual
                << " expected=0x" << expected << std::dec << '\n';
            throw std::runtime_error(message);
        }
    }
    std::size_t matches(RECT rect, uint32_t expected) const {
        std::size_t count{};
        for (auto y = rect.top; y < rect.bottom; ++y)
            for (auto x = rect.left; x < rect.right; ++x)
                count += color_distance(at(x, y), expected) <= 3;
        return count;
    }
};

// Read the actual Direct2D backing surface before EndDraw, not the desktop or a simulated rasterizer.
Pixels readback(Drawing& drawing) {
    auto interop = DrawingTestAccess::interop(drawing);
    HDC source{};
    success(interop->GetDC(D2D1_DC_INITIALIZE_MODE_COPY, &source), "Read painted Direct2D pixels");
    struct Release {
        ID2D1GdiInteropRenderTarget* target;
        ~Release() { const RECT unchanged{}; target->ReleaseDC(&unchanged); }
    } release{interop.Get()};
    Pixels pixels{160, 96};
    pixels.data.resize(static_cast<std::size_t>(pixels.width) * pixels.height);
    HDC dc = CreateCompatibleDC(source);
    require(dc != nullptr, "Create pixel readback DC");
    struct DeleteDCGuard { HDC dc; ~DeleteDCGuard() { DeleteDC(dc); } } delete_dc{dc};
    BITMAPINFO info{};
    info.bmiHeader = {sizeof(BITMAPINFOHEADER), pixels.width, -pixels.height, 1, 32, BI_RGB};
    void* data{};
    const auto bitmap = CreateDIBSection(source, &info, DIB_RGB_COLORS, &data, nullptr, 0);
    require(bitmap && data, "Create pixel readback bitmap");
    struct DeleteBitmap { HBITMAP bitmap; ~DeleteBitmap() { DeleteObject(bitmap); } } delete_bitmap{bitmap};
    const auto previous = SelectObject(dc, bitmap);
    require(previous && previous != HGDI_ERROR, "Select pixel readback bitmap");
    const auto copied = BitBlt(dc, 0, 0, pixels.width, pixels.height, source, 0, 0, SRCCOPY);
    const auto flushed = GdiFlush();
    if (copied && flushed) memcpy(pixels.data.data(), data, pixels.data.size() * sizeof(DWORD));
    SelectObject(dc, previous);
    require(copied && flushed, "Copy actual Direct2D pixel data");
    return pixels;
}

struct SoftwareFixture {
    HWND hwnd{};
    Drawing drawing;
    SoftwareFixture() {
        hwnd = CreateWindowExW(WS_EX_NOACTIVATE, L"STATIC", L"XUI software style pixels",
            WS_POPUP, 0, 0, 160, 96, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        require(hwnd != nullptr, "Create hidden software drawing fixture");
        try {
            drawing.initialize();
            DrawingTestAccess::software_target(drawing, hwnd);
        } catch (...) {
            DestroyWindow(hwnd);
            throw;
        }
    }
    ~SoftwareFixture() { drawing.release(); DestroyWindow(hwnd); }
    Pixels render(const Palette& palette, const ButtonStyleValues* values, bool enabled = true,
        bool hovered = false, bool pressed = false, bool checked = false,
        ButtonAppearance appearance = ButtonAppearance::standard) {
        require(drawing.begin(hwnd, 96, D2D1::ColorF(sentinel)), "Begin software button frame");
        D2D1_COLOR_F ink{};
        if (values)
            ink = drawing.styled_button_face(face, palette, appearance, enabled, hovered, pressed, checked, *values);
        else if (palette.style == VisualStyle::winui)
            ink = drawing.button_face(face, palette, appearance, enabled, hovered, pressed, checked);
        else {
            drawing.rounded(face, pressed || checked ? palette.selection : hovered ? palette.hover : palette.surface);
            drawing.rounded(face, palette.border, 6, true);
            ink = !enabled ? palette.disabled : pressed || checked ? palette.selection_text : palette.text;
        }
        drawing.text(L"MMMM", {36, 22, 80, 32}, ink);
        auto pixels = readback(drawing);
        require(drawing.end(), "Complete actual software button paint");
        return pixels;
    }
};

ButtonStyleValues authored_values() {
    ButtonStyleValues values;
    values.background = ThemeColor{0xe13457, 0x2496c3};
    values.foreground = ThemeColor{0x17420b, 0xffed34};
    values.border_brush = ThemeColor{0x2468ed, 0xda42ef};
    values.border_thickness = Insets{3, 0, 0, 0};
    values.corner_radius = 0.0f;
    return values;
}
Palette regular_palette(ThemeMode mode, VisualStyle style) {
    auto palette = Palette::system(mode, style);
    // Do not modify the user's system settings. Explicit regular palettes isolate non-HC paint tests.
    if (palette.high_contrast) {
        const auto colors = theme_colors(mode, style);
        palette.background = D2D1::ColorF(colors.background);
        palette.surface = D2D1::ColorF(colors.surface);
        palette.border = D2D1::ColorF(colors.border);
        palette.hover = D2D1::ColorF(colors.hover);
        palette.text = D2D1::ColorF(colors.text);
        palette.selection = D2D1::ColorF(colors.selection);
        palette.selection_text = D2D1::ColorF(colors.selection_text);
        palette.high_contrast = false;
    }
    return palette;
}
void toggle_pixel_contracts() {
    SoftwareFixture fixture;
    Toggle toggle(L"MMMM");
    PartStyleValues root; root.background = ThemeColor{0x213141, 0x415161};
    root.foreground = ThemeColor{0xf1e2d3, 0xabcdef}; root.corner_radius = 0.0f;
    PartStyleValues indicator; indicator.size = 20.0f; indicator.background = ThemeColor{0x236745, 0x896745};
    indicator.border_brush = ThemeColor{0xfc1234}; indicator.border_thickness = Insets{2, 2, 2, 2}; indicator.corner_radius = 0.0f;
    PartStyleValues checked; checked.background = ThemeColor{0x765432};
    PartStyleValues mark; mark.foreground = ThemeColor{0x12fe34};
    auto style = ControlStyle::create(StyleTarget::toggle,
        {{StylePart::root, root}, {StylePart::indicator, indicator}, {StylePart::mark, mark}},
        {{StylePart::indicator, style_states::checked, checked}});
    toggle.set_style(style);
    const auto render = [&](Palette palette, bool focused = false) {
        require(fixture.drawing.begin(fixture.hwnd, 96, D2D1::ColorF(sentinel)), "Begin styled Toggle pixel frame");
        Size size{};
        auto label = fixture.drawing.layout(toggle.name(), toggle.text_style(), size);
        fixture.drawing.styled_toggle(toggle, face, palette, true, label.Get(), focused);
        auto pixels = readback(fixture.drawing);
        require(fixture.drawing.end(), "End styled Toggle pixel frame");
        return pixels;
    };
    for (const auto visual : {VisualStyle::classic, VisualStyle::winui}) {
        toggle.set_visual_style(visual);
        auto palette = regular_palette(ThemeMode::light, visual);
        auto pixels = render(palette);
        const auto b = toggle.indicator_bounds(face);
        pixels.expect(120, 60, root.background->light, "Toggle root background reaches pixels");
        pixels.expect(static_cast<int>(b.x + 5), static_cast<int>(b.y + 5), indicator.background->light, "Indicator background reaches pixels");
        pixels.expect(static_cast<int>(b.x), static_cast<int>(b.y + 10), indicator.border_brush->light, "Indicator border reaches pixels");
        require(pixels.matches({static_cast<LONG>(b.x + b.width + toggle.layout_metrics().gap), 12, 132, 68},
            root.foreground->light) > 10, "Inherited root foreground reaches the label");
        toggle.set_checked(true);
        pixels = render(palette);
        pixels.expect(static_cast<int>(b.x + 5), static_cast<int>(b.y + 5), checked.background->light, "Checked rule reaches indicator pixels");
        require(pixels.matches({static_cast<LONG>(b.x), static_cast<LONG>(b.y),
            static_cast<LONG>(b.x + b.width), static_cast<LONG>(b.y + b.height)}, mark.foreground->light) > 5,
            "Authored mark foreground reaches check pixels");
        toggle.set_checked(false);
        palette = regular_palette(ThemeMode::dark, visual);
        pixels = render(palette);
        pixels.expect(120, 60, root.background->dark, "Dark root color remains distinct");
        palette.high_contrast = true;
        const auto high_contrast = render(palette, true);
        require(high_contrast.matches({12, 12, 132, 68}, root.background->dark) == 0 &&
            high_contrast.matches({12, 12, 132, 68}, root.foreground->dark) == 0,
            "High contrast suppresses authored colors and preserves a separate focus outline");
    }
    std::cout << "PASS Toggle software pixels: parts, inheritance, checked, themes, high contrast\n" << std::flush;
}
void switch_state_pixel_contracts() {
    SoftwareFixture fixture;
    constexpr Rect bounds{10, 10, 60, 32};
    for (const auto dpi : {96.0f, 144.0f, 192.0f})
        for (const auto mode : {ThemeMode::light, ThemeMode::dark})
            for (const bool enabled : {false, true})
                for (const bool checked : {false, true})
                    for (const int interaction : {0, 1, 2}) {
                        ToggleSwitch toggle(L"");
                        toggle.set_visual_style(VisualStyle::winui);
                        toggle.set_checked(checked);
                        toggle.pointer_move(interaction != 0);
                        if (interaction == 2) toggle.pointer_down();
                        const auto palette = regular_palette(mode, VisualStyle::winui);
                        const bool light = mode == ThemeMode::light;
                        const auto color = [](uint32_t argb) {
                            return D2D1::ColorF(argb & 0xffffff, float(argb >> 24) / 255);
                        };
                        const uint32_t off_fill = !enabled ? 0 : interaction == 2 ? (light ? 0x18000000 : 0x12ffffff) :
                            interaction == 1 ? (light ? 0x0f000000 : 0x0bffffff) : light ? 0x06000000 : 0x19000000;
                        const uint32_t on_fill = !enabled ? (light ? 0x37000000 : 0x28ffffff) :
                            (interaction == 2 ? 0xcc000000 : interaction == 1 ? 0xe6000000 : 0xff000000) |
                            winui_control_colors(mode).accent;
                        const uint32_t stroke = !enabled ? (light ? 0x37000000 : 0x28ffffff) :
                            light ? 0x72000000 : 0x8bffffff;
                        const uint32_t thumb = checked ? (!enabled ? (light ? 0xffffffff : 0x87ffffff) :
                            light ? 0xffffffff : 0xff000000) : !enabled ? (light ? 0x5c000000 : 0x5dffffff) :
                            light ? 0x9e000000 : 0xc5ffffff;
                        const auto render = [&](bool reference) {
                            require(fixture.drawing.begin(fixture.hwnd, dpi, D2D1::ColorF(sentinel)), "Begin switch state pixels");
                            if (reference) {
                                const auto track = toggle.indicator_bounds(bounds);
                                const float thumb_width = !enabled || interaction == 0 ? 12.0f : interaction == 1 ? 14.0f : 17.0f;
                                const float thumb_height = !enabled || interaction == 0 ? 12.0f : 14.0f;
                                const float left = enabled && interaction == 2 ? (checked ? 20.0f : 3.0f) :
                                    (checked ? 29.5f : 9.5f) - thumb_width / 2;
                                const Rect mark{bounds.x + left,
                                    bounds.y + (bounds.height - thumb_height) / 2, thumb_width, thumb_height};
                                fixture.drawing.styled_surface(track, palette, {}, color(checked ? on_fill : off_fill),
                                    color(stroke), track.height / 2, checked ? Insets{} : Insets{1, 1, 1, 1});
                                fixture.drawing.rounded(mark, color(thumb), mark.height / 2);
                            } else fixture.drawing.styled_toggle(toggle, bounds, palette, enabled, nullptr, false);
                            auto pixels = readback(fixture.drawing);
                            require(fixture.drawing.end(), "End switch state pixels");
                            return pixels;
                        };
                        require(render(false).data == render(true).data,
                            "WinUI switches match template thumb geometry and state brushes");
                    }
}
void switch_focus_pixel_contracts() {
    SoftwareFixture fixture;
    constexpr Rect bounds{10, 4, 60, 40};
    for (const auto dpi : {96.0f, 144.0f, 192.0f})
        for (const auto mode : {ThemeMode::light, ThemeMode::dark, ThemeMode::high_contrast})
            for (const auto radius : {0.0f, 4.0f, 16.0f, 100.0f})
                for (const int variant : {0, 1, 2, 3, 4, 5}) {
                    ToggleSwitch toggle(L"");
                    toggle.set_visual_style(VisualStyle::winui);
                    PartStyleValues root;
                    root.corner_radius = radius;
                    const bool padded = variant == 1;
                    if (padded) root.padding = Insets{3, 4, 5, 6};
                    toggle.set_control_style_values(StylePart::root, root);
                    PartStyleValues label;
                    if (variant == 2) label.horizontal_alignment = StyleAlignment::center;
                    if (variant == 3) label.vertical_alignment = StyleAlignment::start;
                    if (variant == 4) label.font_size = 24.0f;
                    if (variant == 5) label.font_size = 48.0f;
                    toggle.set_control_style_values(StylePart::label, label);
                    const auto palette = mode == ThemeMode::high_contrast ? Palette::system(mode, VisualStyle::winui) :
                        regular_palette(mode, VisualStyle::winui);
                    const auto render = [&](bool reference) {
                        require(fixture.drawing.begin(fixture.hwnd, dpi, D2D1::ColorF(sentinel)), "Begin switch focus pixels");
                        fixture.drawing.styled_toggle(toggle, bounds, palette, true, nullptr, !reference);
                        if (reference) {
                            const float width = variant == 2 ? 60.0f : 52.0f;
                            const float height = variant == 3 || variant == 5 ? 40.0f : 30.0f;
                            const float x = padded ? 13.0f : 10.0f, y = padded ? 8.0f : height == 40 ? 4.0f : 9.0f;
                            const float r = palette.high_contrast ? 4.0f : std::min(radius, height / 2);
                            const auto outer = palette.high_contrast ? palette.text :
                                mode == ThemeMode::light ? D2D1::ColorF(0, 228.0f / 255) : D2D1::ColorF(0xffffff);
                            const auto inner = palette.high_contrast ? palette.background :
                                mode == ThemeMode::light ? D2D1::ColorF(0xffffff, 179.0f / 255) : D2D1::ColorF(0, 179.0f / 255);
                            DrawingTestAccess::stroke(fixture.drawing, {x - 6, y - 2, width + 12, height + 4}, r > 0 ? r + 4 : 0, outer, 2);
                            DrawingTestAccess::stroke(fixture.drawing, {x - 4.5f, y - 0.5f, width + 9, height + 1}, r > 0 ? r + 2.5f : 0, inner, 1);
                        }
                        auto pixels = readback(fixture.drawing);
                        require(fixture.drawing.end(), "End switch focus pixels");
                        return pixels;
                    };
                    require(render(false).data == render(true).data,
                        "Switch focus follows its content-sized inner target, native margins, and resolved rounded corners");
                    const auto layouts = Drawing::created_text_layouts();
                    render(false);
                    require(Drawing::created_text_layouts() == layouts, "Repeated switch focus reuses cached text geometry");
                }
}
void switch_ring_pixel_contracts() {
    switch_state_pixel_contracts();
    switch_focus_pixel_contracts();
    SoftwareFixture fixture;
    ToggleSwitch toggle(L"Switch");
    PartStyleValues indicator; indicator.background = ThemeColor{0x2468ac};
    PartStyleValues mark; mark.foreground = ThemeColor{0xfedcba};
    toggle.set_style(ControlStyle::create(StyleTarget::toggle,
        {{StylePart::indicator, indicator}, {StylePart::mark, mark}}, {}));
    for (const auto visual : {VisualStyle::classic, VisualStyle::winui}) {
        toggle.set_visual_style(visual);
        for (const auto mode : {ThemeMode::light, ThemeMode::dark}) {
            auto palette = regular_palette(mode, visual);
            const auto render = [&] {
                require(fixture.drawing.begin(fixture.hwnd, 96, D2D1::ColorF(sentinel)), "Begin switch pixel frame");
                Size size{}; auto label = fixture.drawing.layout(toggle.name(), toggle.text_style(), size);
                fixture.drawing.styled_toggle(toggle, face, palette, true, label.Get(), true);
                auto pixels = readback(fixture.drawing);
                require(fixture.drawing.end(), "End switch pixel frame");
                return pixels;
            };
            toggle.set_checked(false); const auto off = render();
            const auto off_thumb = toggle.mark_bounds(face);
            off.expect(static_cast<int>(off_thumb.x + off_thumb.width / 2),
                static_cast<int>(off_thumb.y + off_thumb.height / 2), mark.foreground->light, "Switch off thumb is painted");
            toggle.set_checked(true); const auto on = render();
            const auto on_thumb = toggle.mark_bounds(face);
            on.expect(static_cast<int>(on_thumb.x + on_thumb.width / 2),
                static_cast<int>(on_thumb.y + on_thumb.height / 2), mark.foreground->light, "Switch on thumb is painted");
            require(on.data != off.data, "Switch checked state moves visible pixels");
            palette.high_contrast = true;
            const auto hc = render();
            require(hc.matches({12, 12, 132, 68}, indicator.background->light) == 0 &&
                hc.matches({12, 12, 132, 68}, mark.foreground->light) == 0, "Switch high contrast suppresses authored colors");
        }
    }
    const auto render_arc = [&](float phase, float sweep) {
        require(fixture.drawing.begin(fixture.hwnd, 96, D2D1::ColorF(sentinel)), "Begin circular progress frame");
        fixture.drawing.arc({20, 20, 48, 48}, 0, 1, D2D1::ColorF(0x345678), 4);
        fixture.drawing.arc({20, 20, 48, 48}, phase, sweep, D2D1::ColorF(0xabcdef), 4);
        auto pixels = readback(fixture.drawing);
        require(fixture.drawing.end(), "End circular progress frame");
        return pixels;
    };
    const auto first = render_arc(0, 0.25f), next = render_arc(0.25f, 0.25f);
    require(first.data != next.data, "Progress ring phase changes actual rendered pixels");
    first.expect(44, 44, sentinel, "Ring leaves its center open");
    const auto empty = render_arc(0, 0), full = render_arc(0, 1);
    require(empty.matches({20, 20, 68, 68}, 0xabcdef) == 0 &&
        full.matches({20, 20, 68, 68}, 0xabcdef) > first.matches({20, 20, 68, 68}, 0xabcdef),
        "Determinate ring zero and full ranges render distinct coverage");
}
void hyperlink_pixel_contracts() {
    SoftwareFixture fixture;
    constexpr Rect bounds{10, 10, 60, 32};
    for (const auto dpi : {96.0f, 144.0f, 192.0f}) {
        for (const auto mode : {ThemeMode::light, ThemeMode::dark, ThemeMode::high_contrast}) {
            const auto palette = mode == ThemeMode::high_contrast ? Palette::system(mode, VisualStyle::winui) :
                regular_palette(mode, VisualStyle::winui);
            for (const bool enabled : {true, false}) for (const int state : {0, 1, 2}) {
                HyperlinkButton link(L"Link");
                link.set_visual_style(VisualStyle::winui);
                link.pointer_move(state != 0);
                if (state == 2) link.pointer_down();
                const auto render = [&](bool reference) {
                    require(fixture.drawing.begin(fixture.hwnd, dpi, D2D1::ColorF(sentinel)), "Begin hyperlink state pixels");
                    if (reference) {
                        const uint32_t fill = !enabled || palette.high_contrast || state == 0 ? 0 :
                            mode == ThemeMode::light ? (state == 2 ? 0x06000000u : 0x09000000u) :
                            state == 2 ? 0x0affffffu : 0x0fffffffu;
                        if (fill) fixture.drawing.rounded(bounds,
                            D2D1::ColorF(fill & 0xffffff, float(fill >> 24) / 255), 4);
                        const auto ink = !enabled ? palette.high_contrast ? palette.disabled :
                            D2D1::ColorF(mode == ThemeMode::light ? 0 : 0xffffff,
                                (mode == ThemeMode::light ? 92.0f : 93.0f) / 255) :
                            palette.high_contrast ? palette.text : palette.accent;
                        PartStyleValues text;
                        text.horizontal_alignment = text.vertical_alignment = StyleAlignment::center;
                        const auto content = link.content_bounds(bounds);
                        fixture.drawing.push_clip(content);
                        fixture.drawing.styled_text(link.name(), content, ink, text);
                        fixture.drawing.pop_clip();
                    } else fixture.drawing.hyperlink(link, bounds, palette, enabled, false);
                    auto pixels = readback(fixture.drawing);
                    require(fixture.drawing.end(), "End hyperlink state pixels");
                    return pixels;
                };
                require(render(false).data == render(true).data,
                    "WinUI hyperlink uses plain accent text, subtle interaction fills and disabled alpha without an underline");
            }
        }
    }
}
void checkbox_state_pixel_contracts() {
    SoftwareFixture fixture;
    fixture.drawing.set_visual_style(VisualStyle::winui);
    constexpr Rect bounds{10, 10, 120, 32};
    for (const auto dpi : {96.0f, 144.0f, 192.0f}) {
        for (const auto mode : {ThemeMode::light, ThemeMode::dark, ThemeMode::high_contrast}) {
            const auto palette = mode == ThemeMode::high_contrast ? Palette::system(mode, VisualStyle::winui) :
                regular_palette(mode, VisualStyle::winui);
            for (const bool rounded : {false, true}) for (const bool enabled : {true, false})
                for (const int interaction : {0, 1, 2})
                    for (const auto state : {CheckState::unchecked, CheckState::checked, CheckState::indeterminate}) {
                        CheckBox check(L"Include");
                        check.set_visual_style(VisualStyle::winui);
                        if (rounded) {
                            PartStyleValues root;
                            root.corner_radius = 16.0f;
                            check.set_control_style_values(StylePart::root, root);
                        }
                        check.pointer_move(interaction != 0);
                        if (interaction == 2) check.pointer_down();
                        check.set_state(state);
                        Size size{};
                        const auto label = fixture.drawing.layout(check.name(), check.text_style(), size);
                        const auto render = [&](bool reference) {
                            require(fixture.drawing.begin(fixture.hwnd, dpi, D2D1::ColorF(sentinel)), "Begin checkbox state pixels");
                            if (reference) {
                                const auto ink = fixture.drawing.check_indicator({10.5f, 16.5f, 19, 19}, palette,
                                    check.checked(), enabled, check.indeterminate(), check.hovered(), check.pressed());
                                const auto content = check.label_bounds(bounds);
                                fixture.drawing.push_clip(content);
                                fixture.drawing.text_layout(label.Get(), content, ink);
                                fixture.drawing.pop_clip();
                            } else fixture.drawing.styled_toggle(check, bounds, palette, enabled, label.Get(), false);
                            auto pixels = readback(fixture.drawing);
                            require(fixture.drawing.end(), "End checkbox state pixels");
                            return pixels;
                        };
                        require(render(false).data == render(true).data,
                            "Default and rounded-root CheckBox share Toggle brushes, font marks and indicator alignment");
                    }
        }
    }
}
void checkbox_focus_pixel_contracts() {
    SoftwareFixture fixture;
    constexpr Rect bounds{10, 10, 60, 32};
    for (const auto dpi : {96.0f, 144.0f, 192.0f}) {
        for (const auto mode : {ThemeMode::light, ThemeMode::dark, ThemeMode::high_contrast}) {
            const auto palette = mode == ThemeMode::high_contrast ? Palette::system(mode, VisualStyle::winui) :
                regular_palette(mode, VisualStyle::winui);
            for (const auto radius : {0.0f, 4.0f, 16.0f}) {
                CheckBox check(L"");
                check.set_visual_style(VisualStyle::winui);
                PartStyleValues root;
                root.corner_radius = radius;
                check.set_control_style_values(StylePart::root, root);
                const auto render = [&](bool reference) {
                    require(fixture.drawing.begin(fixture.hwnd, dpi, D2D1::ColorF(sentinel)), "Begin checkbox focus pixels");
                    fixture.drawing.styled_toggle(check, bounds, palette, true, nullptr, !reference);
                    if (reference) {
                        const float r = palette.high_contrast ? 4.0f : radius;
                        const auto outer = palette.high_contrast ? palette.text :
                            mode == ThemeMode::light ? D2D1::ColorF(0, 228.0f / 255) : D2D1::ColorF(0xffffff);
                        const auto inner = palette.high_contrast ? palette.background :
                            mode == ThemeMode::light ? D2D1::ColorF(0xffffff, 179.0f / 255) : D2D1::ColorF(0, 179.0f / 255);
                        DrawingTestAccess::stroke(fixture.drawing, {4, 8, 72, 36}, r > 0 ? r + 4 : 0, outer, 2);
                        DrawingTestAccess::stroke(fixture.drawing, {5.5f, 9.5f, 69, 33}, r > 0 ? r + 2.5f : 0, inner, 1);
                    }
                    auto pixels = readback(fixture.drawing);
                    require(fixture.drawing.end(), "End checkbox focus pixels");
                    return pixels;
                };
                require(render(false).data == render(true).data,
                    "Checkbox focus uses seven horizontal DIPs, three vertical DIPs and the native averaged corner adjustment");
            }
        }
    }
}
void rounded_surface_pixel_contracts() {
    SoftwareFixture fixture;
    constexpr Rect bounds{10, 10, 60, 32};
    for (const auto dpi : {96.0f, 144.0f, 192.0f})
        for (const auto mode : {ThemeMode::light, ThemeMode::dark, ThemeMode::high_contrast}) {
            const auto palette = mode == ThemeMode::high_contrast ? Palette::system(mode, VisualStyle::winui) :
                regular_palette(mode, VisualStyle::winui);
            for (const auto radius : {0.0f, 4.0f, 16.0f, 100.0f})
                for (const auto width : {0.0f, 1.0f, 3.0f, 30.0f})
                    for (const bool transparent : {false, true}) {
                        PartStyleValues values;
                        if (!transparent) values.background = ThemeColor{0x236745, 0x896745};
                        values.border_brush = ThemeColor{0xfc1234, 0x24dcfe};
                        values.corner_radius = radius;
                        values.border_thickness = Insets{width, width, width, width};
                        const auto background = transparent ? D2D1::ColorF(0, 0.0f) : palette.surface;
                        const auto render = [&](bool reference) {
                            require(fixture.drawing.begin(fixture.hwnd, dpi, D2D1::ColorF(sentinel)), "Begin rounded surface pixels");
                            if (reference) {
                                const float r = std::min(palette.high_contrast ? 6.0f : radius, bounds.height / 2);
                                const float edge = std::min(palette.high_contrast ? 2.0f : width, bounds.height / 2);
                                const auto fill = !palette.high_contrast && values.background ?
                                    D2D1::ColorF(values.background->resolve(mode)) : background;
                                const auto border = palette.high_contrast ? palette.border :
                                    D2D1::ColorF(values.border_brush->resolve(mode));
                                if (fill.a > 0) fixture.drawing.rounded(bounds, fill, r);
                                if (edge > 0) DrawingTestAccess::stroke(fixture.drawing,
                                    {bounds.x + edge / 2, bounds.y + edge / 2, bounds.width - edge, bounds.height - edge},
                                    std::max(0.0f, r - edge / 2), border, edge);
                            } else fixture.drawing.styled_surface(bounds, palette, values,
                                background, palette.border, 6, {2, 2, 2, 2});
                            auto pixels = readback(fixture.drawing);
                            require(fixture.drawing.end(), "End rounded surface pixels");
                            return pixels;
                        };
                        require(render(false).data == render(true).data,
                            "Uniform surface borders retain continuous curved corners and stay inside their bounds");
                    }
        }
}
void joined_surface_pixel_contracts() {
    SoftwareFixture fixture;
    constexpr Rect bounds{10, 10, 60, 32};
    const auto palette = regular_palette(ThemeMode::light, VisualStyle::winui);
    for (const auto dpi : {96.0f, 144.0f, 192.0f})
        for (const auto radius : {0.0f, 4.0f, 16.0f, 100.0f})
            for (const auto width : {0.0f, 1.0f, 3.0f})
                for (const bool transparent : {false, true}) {
                    const auto background = D2D1::ColorF(0x2468ac, transparent ? 0.0f : 1.0f);
                    const auto border = D2D1::ColorF(0xfedcba, 0.4f);
                    const auto render = [&](Drawing::SurfaceCorners corners, bool reference, float reference_radius) {
                        require(fixture.drawing.begin(fixture.hwnd, dpi, D2D1::ColorF(sentinel)), "Begin joined surface pixels");
                        if (reference) {
                            if (!transparent) fixture.drawing.rounded(bounds, background, reference_radius);
                            if (width > 0) DrawingTestAccess::stroke(fixture.drawing,
                                {bounds.x + width / 2, bounds.y + width / 2, bounds.width - width, bounds.height - width},
                                std::max(0.0f, reference_radius - width / 2), border, width);
                        } else fixture.drawing.styled_surface(bounds, palette, {}, background, border, radius,
                            {width, width, width, width}, corners);
                        auto pixels = readback(fixture.drawing);
                        require(fixture.drawing.end(), "End joined surface pixels");
                        return pixels;
                    };
                    const auto rounded = render(Drawing::SurfaceCorners::all, true, std::min(radius, bounds.height / 2));
                    const auto square = render(Drawing::SurfaceCorners::all, true, 0);
                    for (const auto corners : {Drawing::SurfaceCorners::top, Drawing::SurfaceCorners::bottom}) {
                        const auto actual = render(corners, false, 0);
                        for (int y = 0; y < actual.height; ++y)
                            for (int x = 0; x < actual.width; ++x) {
                                const bool upper = y < std::lround((bounds.y + bounds.height / 2) * dpi / 96);
                                const auto& expected = upper == (corners == Drawing::SurfaceCorners::top) ? rounded : square;
                                actual.expect(x, y, expected.at(x, y),
                                    "Joined surfaces retain curved outer corners and square connecting corners");
                            }
                    }
                }
}
void joined_focus_pixel_contracts() {
    SoftwareFixture fixture;
    constexpr Rect bounds{10, 10, 60, 32};
    for (const auto dpi : {96.0f, 144.0f, 192.0f})
        for (const auto mode : {ThemeMode::light, ThemeMode::dark, ThemeMode::high_contrast})
            for (const auto radius : {0.0f, 4.0f, 16.0f}) for (const bool external : {false, true}) {
                const auto palette = mode == ThemeMode::high_contrast ? Palette::system(mode, VisualStyle::winui) :
                    regular_palette(mode, VisualStyle::winui);
                const auto render = [&](Drawing::SurfaceCorners corners, bool reference, float reference_radius) {
                    require(fixture.drawing.begin(fixture.hwnd, dpi, D2D1::ColorF(sentinel)), "Begin joined focus pixels");
                    if (reference && external) {
                        const auto outer = palette.high_contrast ? palette.text :
                            mode == ThemeMode::light ? D2D1::ColorF(0, 228.0f / 255) : D2D1::ColorF(0xffffff);
                        const auto inner = palette.high_contrast ? palette.background :
                            mode == ThemeMode::light ? D2D1::ColorF(0xffffff, 179.0f / 255) : D2D1::ColorF(0, 179.0f / 255);
                        DrawingTestAccess::stroke(fixture.drawing,
                            {bounds.x - 2, bounds.y - 2, bounds.width + 4, bounds.height + 4},
                            reference_radius > 0 ? reference_radius + 2 : 0, outer, 2);
                        DrawingTestAccess::stroke(fixture.drawing,
                            {bounds.x - 0.5f, bounds.y - 0.5f, bounds.width + 1, bounds.height + 1},
                            reference_radius > 0 ? reference_radius + 0.5f : 0, inner, 1);
                    } else if (reference) {
                        DrawingTestAccess::stroke(fixture.drawing, bounds, reference_radius, palette.text, 1);
                        DrawingTestAccess::stroke(fixture.drawing,
                            {bounds.x + 1, bounds.y + 1, bounds.width - 2, bounds.height - 2},
                            std::max(0.0f, reference_radius - 1), palette.background, 1);
                    } else if (external) fixture.drawing.winui_focus_ring(bounds, palette, radius, 3, corners);
                    else fixture.drawing.focus_ring(bounds, palette, radius, corners);
                    auto pixels = readback(fixture.drawing);
                    require(fixture.drawing.end(), "End joined focus pixels");
                    return pixels;
                };
                const auto rounded = render(Drawing::SurfaceCorners::all, true, external && palette.high_contrast ? 4 : radius);
                const auto square = render(Drawing::SurfaceCorners::all, true, 0);
                for (const auto corners : {Drawing::SurfaceCorners::all, Drawing::SurfaceCorners::top, Drawing::SurfaceCorners::bottom}) {
                    const auto actual = render(corners, false, 0);
                    for (int y = 0; y < actual.height; ++y)
                        for (int x = 0; x < actual.width; ++x) {
                            const bool upper = y < std::lround((bounds.y + bounds.height / 2) * dpi / 96);
                            const auto& expected = corners == Drawing::SurfaceCorners::all ||
                                upper == (corners == Drawing::SurfaceCorners::top) ? rounded : square;
                            actual.expect(x, y, expected.at(x, y),
                                "Joined focus follows the surface corners without gaps or a dividing stroke");
                        }
                }
            }
}
void combo_focus_pixel_contracts() {
    SoftwareFixture fixture;
    constexpr Rect bounds{10, 10, 60, 32};
    for (const auto dpi : {96.0f, 144.0f, 192.0f})
        for (const auto mode : {ThemeMode::light, ThemeMode::dark})
            for (const float field_radius : {0.0f, 4.0f, 16.0f}) {
                const auto palette = regular_palette(mode, VisualStyle::winui);
                const auto render = [&](bool reference) {
                    require(fixture.drawing.begin(fixture.hwnd, dpi, D2D1::ColorF(sentinel)), "Begin ComboBox focus pixels");
                    if (reference) {
                        fixture.drawing.rounded({6, 6, 68, 40},
                            D2D1::ColorF(0xffffff, (mode == ThemeMode::light ? 179.0f : 15.0f) / 255), 7);
                        DrawingTestAccess::stroke(fixture.drawing, {7, 7, 66, 38}, 6,
                            mode == ThemeMode::light ? D2D1::ColorF(0, 228.0f / 255) : D2D1::ColorF(0xffffff), 2);
                    } else fixture.drawing.winui_combo_focus_background(bounds, palette);
                    fixture.drawing.rounded(bounds, D2D1::ColorF(0x234567), field_radius);
                    if (reference) fixture.drawing.rounded({11, 18, 3, 16}, palette.accent, 1.5f);
                    else fixture.drawing.winui_combo_focus_marker(bounds, palette);
                    auto pixels = readback(fixture.drawing);
                    require(fixture.drawing.end(), "End ComboBox focus pixels");
                    return pixels;
                };
                require(render(false).data == render(true).data,
                    "ComboBox focus uses its fixed seven-DIP highlight radius, external border, and foreground accent marker");
            }
}
void range_focus_pixel_contracts() {
    SoftwareFixture fixture;
    for (const auto dpi : {96.0f, 144.0f, 192.0f})
        for (const auto mode : {ThemeMode::light, ThemeMode::dark, ThemeMode::high_contrast})
            for (const auto bounds : {Rect{20, 8, 48, 24}, Rect{20, 8, 24, 36}, Rect{20, 8, 24, 4}, Rect{20, 8, 24, 1}}) {
                const auto palette = mode == ThemeMode::high_contrast ? Palette::system(mode, VisualStyle::winui) :
                    regular_palette(mode, VisualStyle::winui);
                const auto render = [&](bool reference) {
                    require(fixture.drawing.begin(fixture.hwnd, dpi, D2D1::ColorF(sentinel)), "Begin range focus pixels");
                    if (reference) {
                        const auto outer = palette.high_contrast ? palette.text :
                            mode == ThemeMode::light ? D2D1::ColorF(0, 228.0f / 255) : D2D1::ColorF(0xffffff);
                        const auto inner = palette.high_contrast ? palette.background :
                            mode == ThemeMode::light ? D2D1::ColorF(0xffffff, 179.0f / 255) : D2D1::ColorF(0, 179.0f / 255);
                        const float radius = palette.high_contrast ? 4.0f : std::min(4.0f, bounds.height / 2);
                        if (bounds.height > 2)
                            DrawingTestAccess::stroke(fixture.drawing,
                                {bounds.x - 6, bounds.y + 1, bounds.width + 12, bounds.height - 2}, radius + 2.5f, outer, 2);
                        if (bounds.height > 5)
                            DrawingTestAccess::stroke(fixture.drawing,
                                {bounds.x - 4.5f, bounds.y + 2.5f, bounds.width + 9, bounds.height - 5}, radius + 1, inner, 1);
                    } else fixture.drawing.winui_focus_ring(bounds, palette, 4, 7, Drawing::SurfaceCorners::all, 0);
                    auto pixels = readback(fixture.drawing);
                    require(fixture.drawing.end(), "End range focus pixels");
                    return pixels;
                };
                require(render(false).data == render(true).data,
                    "Range focus uses seven horizontal DIPs, no vertical outset, and native corner adjustment without inverted strokes");
            }
}
void styled_field_focus_pixel_contracts() {
    SoftwareFixture fixture;
    const std::array<std::optional<float>, 5> radii{std::nullopt, 0.0f, 4.0f, 16.0f, 80.0f};
    for (const auto dpi : {96.0f, 144.0f, 192.0f})
        for (const auto visual : {VisualStyle::classic, VisualStyle::winui})
            for (const auto mode : {ThemeMode::light, ThemeMode::dark, ThemeMode::high_contrast})
                for (const auto authored_radius : radii)
                    for (const auto bounds : {Rect{12, 12, 48, 32}, Rect{12, 12, 48, 3}}) {
                        const auto palette = mode == ThemeMode::high_contrast ? Palette::system(mode, visual) :
                            regular_palette(mode, visual);
                        PartStyleValues values; values.corner_radius = authored_radius;
                        const auto render = [&](bool reference) {
                            require(fixture.drawing.begin(fixture.hwnd, dpi, D2D1::ColorF(sentinel)),
                                "Begin authored field focus pixels");
                            if (reference) {
                                const float radius = visual == VisualStyle::winui && !palette.high_contrast ?
                                    std::min(authored_radius.value_or(4.0f), bounds.height / 2) : 4.0f;
                                DrawingTestAccess::stroke(fixture.drawing, bounds, radius, palette.text, 1);
                                DrawingTestAccess::stroke(fixture.drawing,
                                    {bounds.x + 1, bounds.y + 1, bounds.width - 2, bounds.height - 2},
                                    std::max(0.0f, radius - 1), palette.background, 1);
                            } else fixture.drawing.styled_field_focus(bounds, palette, values);
                            auto pixels = readback(fixture.drawing);
                            require(fixture.drawing.end(), "End authored field focus pixels");
                            return pixels;
                        };
                        require(render(false).data == render(true).data,
                            "Authored WinUI field focus follows clamped square/rounded corners; Classic and high contrast retain their outline");
                    }
}
void checked_disabled_button_pixel_contracts() {
    SoftwareFixture fixture;
    for (const auto mode : {ThemeMode::light, ThemeMode::dark}) {
        const auto palette = regular_palette(mode, VisualStyle::winui);
        const auto fill = composite_argb_on_rgb(mode == ThemeMode::light ? 0x37000000u : 0x28ffffffu, sentinel);
        const auto ink = composite_argb_on_rgb(mode == ThemeMode::light ? 0xffffffffu : 0x87ffffffu, fill);
        const auto verify = [&](const Pixels& pixels) {
            pixels.expect(22, 40, fill, "Checked disabled button retains its accent-disabled fill");
            pixels.expect(72, 13, fill, "Checked disabled button has no contrasting top border");
            require(pixels.matches({36, 22, 116, 54}, ink) > 5,
                "Checked disabled button uses the text-on-accent disabled foreground");
        };
        ButtonStyleValues legacy; legacy.corner_radius = 16.0f;
        for (const auto* values : std::array<const ButtonStyleValues*, 2>{nullptr, &legacy})
            for (const bool hovered : {false, true})
                for (const bool pressed : {false, true})
                    verify(fixture.render(palette, values, false, hovered, pressed, true));
        ToggleButton button(L"MMMM");
        button.set_checked(true); button.set_enabled(false); button.set_visual_style(VisualStyle::winui);
        PartStyleValues root; root.corner_radius = 16.0f;
        button.set_control_style_values(StylePart::root, root);
        require(fixture.drawing.begin(fixture.hwnd, 96, D2D1::ColorF(sentinel)), "Begin named-part disabled button pixels");
        fixture.drawing.styled_button(button, face, palette, false, false);
        const auto pixels = readback(fixture.drawing);
        require(fixture.drawing.end(), "End named-part disabled button pixels");
        verify(pixels);
    }
}
void navigation_focus_pixel_contracts() {
    SoftwareFixture fixture;
    NavigationView navigation;
    auto owner = navigation.items();
    CollectionRow row;
    row.bounds = {10, 10, 56, 26}; row.navigation = true;
    const Rect face{12, 12, 52, 22};
    for (const auto visual : {VisualStyle::classic, VisualStyle::winui})
        for (const auto mode : {ThemeMode::light, ThemeMode::dark, ThemeMode::high_contrast})
            for (const auto dpi : {96.0f, 144.0f, 192.0f})
                for (const bool styled : {false, true})
                    for (const bool group : {false, true})
                        for (const auto radius : {std::optional<float>{}, std::optional{0.0f},
                            std::optional{4.0f}, std::optional{16.0f}, std::optional{100.0f}}) {
                            if (!styled && radius) continue;
                            row.group = group;
                            owner->set_control_style_values(StylePart::row, {});
                            owner->set_control_style_values(StylePart::group_header, {});
                            PartStyleValues values; values.corner_radius = radius;
                            if (styled) {
                                values.foreground = ThemeColor{0xabcdef};
                                owner->set_control_style_values(group ? StylePart::group_header : StylePart::row, values);
                            }
                            const auto palette = mode == ThemeMode::high_contrast ? Palette::system(mode, visual) :
                                regular_palette(mode, visual);
                            for (const bool selected : {false, true})
                            for (const bool clipped : {false, true}) {
                                if (group && selected) continue;
                                const auto render = [&](bool reference) {
                                    require(fixture.drawing.begin(fixture.hwnd, dpi, D2D1::ColorF(sentinel)),
                                        "Begin navigation focus pixels");
                                    if (clipped) fixture.drawing.push_clip({15, 14, 51, 22});
                                    fixture.drawing.collection_row(row, selected, !reference, true, palette,
                                        false, {}, false, false, owner.get());
                                    if (reference) {
                                        if (visual == VisualStyle::winui && !palette.high_contrast) {
                                            const auto primary = mode == ThemeMode::light ? 0xe4000000u : 0xffffffffu;
                                            const auto secondary = mode == ThemeMode::light ? 0xb3ffffffu : 0xb3000000u;
                                            const float r = std::min(radius.value_or(5.0f), 11.0f);
                                            DrawingTestAccess::stroke(fixture.drawing, {11, 11, 54, 24}, r > 0 ? r + 1 : 0,
                                                D2D1::ColorF(primary & 0xffffff, float(primary >> 24) / 255), 2);
                                            DrawingTestAccess::stroke(fixture.drawing, {12.5f, 12.5f, 51, 21}, r > 0 ? r - 0.5f : 0,
                                                D2D1::ColorF(secondary & 0xffffff, float(secondary >> 24) / 255), 1);
                                        } else if (styled || visual == VisualStyle::winui) {
                                            DrawingTestAccess::stroke(fixture.drawing, face, 4, palette.text, 1);
                                            DrawingTestAccess::stroke(fixture.drawing, {13, 13, 50, 20}, 3, palette.background, 1);
                                        } else DrawingTestAccess::stroke(fixture.drawing, face, 5, palette.accent, 1);
                                    }
                                    if (clipped) fixture.drawing.pop_clip();
                                    auto pixels = readback(fixture.drawing);
                                    require(fixture.drawing.end(), "End navigation focus pixels");
                                    return pixels;
                                };
                                require(render(false).data == render(true).data,
                                    "WinUI navigation focus covers the complete row with native strokes and resolved corners; other modes retain their outline");
                            }
                        }
}
void next_controls_pixel_contracts() {
    navigation_focus_pixel_contracts();
    checked_disabled_button_pixel_contracts();
    styled_field_focus_pixel_contracts();
    rounded_surface_pixel_contracts();
    joined_surface_pixel_contracts();
    joined_focus_pixel_contracts();
    combo_focus_pixel_contracts();
    range_focus_pixel_contracts();
    hyperlink_pixel_contracts();
    checkbox_state_pixel_contracts();
    checkbox_focus_pixel_contracts();
    SoftwareFixture fixture;
    for (const auto visual : {VisualStyle::classic, VisualStyle::winui}) {
        for (const auto theme : {ThemeMode::light, ThemeMode::dark, ThemeMode::high_contrast}) {
            auto palette = theme == ThemeMode::high_contrast ? Palette::system(theme, visual) : regular_palette(theme, visual);
            CheckBox check(L"Include"); check.set_visual_style(visual);
            const auto render_check = [&](CheckState state) {
                check.set_state(state);
                require(fixture.drawing.begin(fixture.hwnd, 96, D2D1::ColorF(sentinel)), "Begin checkbox pixels");
                Size size{}; auto label = fixture.drawing.layout(check.name(), check.text_style(), size);
                fixture.drawing.styled_toggle(check, face, palette, true, label.Get(), true);
                auto result = readback(fixture.drawing); require(fixture.drawing.end(), "End checkbox pixels"); return result;
            };
            const auto off = render_check(CheckState::unchecked), on = render_check(CheckState::checked),
                mixed = render_check(CheckState::indeterminate);
            require(off.data != on.data && on.data != mixed.data && off.data != mixed.data,
                "Unchecked, checked and mixed checkbox states have distinct pixels");
            InfoBadge badge(L"Unread");
            const auto render_badge = [&] {
                require(fixture.drawing.begin(fixture.hwnd, 96, D2D1::ColorF(sentinel)), "Begin badge pixels");
                const auto size = badge.measure({100, 100});
                fixture.drawing.info_badge(badge, {20, 20, size.width, size.height}, palette, true);
                auto result = readback(fixture.drawing); require(fixture.drawing.end(), "End badge pixels"); return result;
            };
            const auto dot = render_badge(); badge.set_count(100); const auto count = render_badge();
            badge.set_icon(ButtonIcon::bookmark); const auto icon = render_badge();
            require(dot.data != count.data && count.data != icon.data && dot.data != icon.data,
                "Badge dot, bounded count and icon presentations render distinctly");
            HyperlinkButton link(L"Documentation"); link.set_visual_style(visual);
            link.set_text_measurer([&fixture](std::wstring_view text, TextStyle style) -> Size {
                Size size{}; fixture.drawing.layout(text, style, size); return size;
            });
            const auto render_link = [&](bool enabled) {
                require(fixture.drawing.begin(fixture.hwnd, 96, D2D1::ColorF(sentinel)), "Begin hyperlink pixels");
                fixture.drawing.hyperlink(link, face, palette, enabled, true);
                auto result = readback(fixture.drawing); require(fixture.drawing.end(), "End hyperlink pixels"); return result;
            };
            const auto active = render_link(true), disabled = render_link(false);
            require(active.data != disabled.data, "Hyperlink disabled state changes visible ink");
            active.expect(70, 16, sentinel, "Unstyled hyperlink has no button face");
        }
    }
}
void button_focus_pixel_contracts() {
    SoftwareFixture fixture;
    constexpr Rect bounds{10, 10, 60, 32};
    enum class RadiusSource { local, focused_rule, inherited, default_style };
    for (const auto dpi : {96.0f, 144.0f, 192.0f}) {
        for (const auto visual : {VisualStyle::classic, VisualStyle::winui}) {
            for (const auto mode : {ThemeMode::light, ThemeMode::dark, ThemeMode::high_contrast}) {
                const auto palette = mode == ThemeMode::high_contrast ?
                    Palette::system(mode, visual) : regular_palette(mode, visual);
                for (const auto radius : {0.0f, 4.0f, 16.0f, 1000.0f}) {
                    for (const auto source : {RadiusSource::local, RadiusSource::focused_rule,
                        RadiusSource::inherited, RadiusSource::default_style}) {
                        Button button(L"");
                        button.set_visual_style(visual);
                        button.set_focused(true);
                        PartStyleValues root;
                        root.corner_radius = radius;
                        PartStyleValues inherited;
                        if (source == RadiusSource::local) {
                            ButtonStyleValues local;
                            local.corner_radius = radius;
                            button.set_style_values(local);
                        } else if (source == RadiusSource::focused_rule) {
                            button.set_control_style(ControlStyle::create(StyleTarget::button, {},
                                {{StylePart::root, style_states::focused, root}}));
                        } else if (source == RadiusSource::inherited) {
                            inherited = root;
                        }
                        const auto render = [&](bool reference) {
                            require(fixture.drawing.begin(fixture.hwnd, dpi, D2D1::ColorF(sentinel)),
                                "Begin button focus pixels");
                            fixture.drawing.styled_button(button, bounds, palette, true, !reference,
                                {}, 0, {}, &inherited);
                            if (reference) {
                                const bool winui = visual == VisualStyle::winui;
                                const float inset = winui ? 0.5f : 2.0f;
                                const Rect outline{bounds.x + inset, bounds.y + inset,
                                    bounds.width - 2 * inset, bounds.height - 2 * inset};
                                if (winui) {
                                    const float expected_radius = palette.high_contrast || source == RadiusSource::default_style ? 4.0f :
                                        std::min(radius, 16.0f);
                                    const auto outer = mode == ThemeMode::light ? D2D1::ColorF(0, 228.0f / 255) :
                                        D2D1::ColorF(0xffffff);
                                    const auto inner = mode == ThemeMode::light ? D2D1::ColorF(0xffffff, 179.0f / 255) :
                                        D2D1::ColorF(0, 179.0f / 255);
                                    DrawingTestAccess::stroke(fixture.drawing, {8, 8, 64, 36},
                                        expected_radius > 0 ? expected_radius + 2 : 0,
                                        palette.high_contrast ? palette.text : outer, 2);
                                    DrawingTestAccess::stroke(fixture.drawing, {9.5f, 9.5f, 61, 33},
                                        expected_radius > 0 ? expected_radius + 0.5f : 0,
                                        palette.high_contrast ? palette.background : inner, 1);
                                } else {
                                    fixture.drawing.rounded(outline, palette.accent, 6, true);
                                }
                            }
                            auto pixels = readback(fixture.drawing);
                            require(fixture.drawing.end(), "End button focus pixels");
                            return pixels;
                        };
                        const auto actual = render(false);
                        const auto expected = render(true);
                        require(actual.data == expected.data,
                            "Button focus uses the external margin, stroke resources and resolved radius without changing its interior");
                        if (visual == VisualStyle::winui && !palette.high_contrast && dpi == 96) {
                            const auto primary = mode == ThemeMode::light ? 0xe4000000u : 0xffffffffu;
                            const auto secondary = mode == ThemeMode::light ? 0xb3ffffffu : 0xb3000000u;
                            actual.expect(40, 7, composite_argb_on_rgb(primary, sentinel), "The first outer DIP uses the primary brush");
                            actual.expect(40, 8, composite_argb_on_rgb(primary, sentinel), "The second outer DIP uses the primary brush");
                            actual.expect(40, 9, composite_argb_on_rgb(secondary, sentinel), "The inner DIP composites over the parent");
                            actual.expect(40, 6, sentinel, "Button focus never exceeds the three-DIP margin");
                        }
                        if (visual == VisualStyle::winui && !palette.high_contrast &&
                            source != RadiusSource::default_style && radius >= 16)
                            actual.expect(static_cast<int>(11 * dpi / 96), static_cast<int>(11 * dpi / 96),
                                sentinel, "Pill focus leaves the rounded corner outside the control untouched");
                    }
                }
            }
        }
    }
}
void pixel_contracts() {
    const auto before = Drawing::live_targets();
    {
        SoftwareFixture fixture;
        require(Drawing::live_targets() == before + 1, "Software fixture owns one real target");
        const auto values = authored_values();
        for (const auto dpi : {96.0f, 120.0f, 144.0f}) {
            for (const auto widths : {Insets{3, 0, 0, 0}, Insets{3.5f, 0, 0, 0}, Insets{2, 3, 4, 5}}) {
                auto square = values;
                square.border_thickness = widths;
                const auto palette = regular_palette(ThemeMode::light, VisualStyle::classic);
                const auto draw = [&](bool reference) {
                    require(fixture.drawing.begin(fixture.hwnd, dpi, D2D1::ColorF(sentinel)), "Begin square-edge comparison");
                    if (reference) {
                        fixture.drawing.rounded(face, D2D1::ColorF(square.background->light), 0);
                        const Rect edges[]{{12, 12, widths.left, 56},
                            {12 + widths.left, 12, 120 - widths.left - widths.right, widths.top},
                            {132 - widths.right, 12, widths.right, 56},
                            {12 + widths.left, 68 - widths.bottom, 120 - widths.left - widths.right, widths.bottom}};
                        for (const auto& edge : edges) if (edge.width > 0 && edge.height > 0) {
                            fixture.drawing.push_clip(edge);
                            fixture.drawing.rounded(face, D2D1::ColorF(square.border_brush->light), 0);
                            fixture.drawing.pop_clip();
                        }
                    } else fixture.drawing.styled_button_face(face, palette, ButtonAppearance::standard,
                        true, false, false, false, square);
                    auto pixels = readback(fixture.drawing);
                    require(fixture.drawing.end(), "Square-edge comparison completes");
                    return pixels;
                };
                const auto reference = draw(true);
                const auto optimized = draw(false);
                require(reference.data == optimized.data,
                    "Square-edge fast path preserves every pixel, including fractional edges and scaled DPI");
            }
        }
        for (const auto style : {VisualStyle::classic, VisualStyle::winui}) {
            for (const auto mode : {ThemeMode::light, ThemeMode::dark}) {
                const auto palette = regular_palette(mode, style);
                const auto pixels = fixture.render(palette, &values);
                const auto fill = values.background->resolve(mode), edge = values.border_brush->resolve(mode);
                for (int x = 12; x < 15; ++x)
                    pixels.expect(x, 40, edge, "The left border covers exactly three DIPs at 96 DPI");
                pixels.expect(15, 40, fill, "The fourth DIP is background, not border");
                for (const auto point : {POINT{20, 12}, POINT{131, 12}, POINT{131, 67}, POINT{20, 67}, POINT{131, 40}})
                    pixels.expect(point.x, point.y, fill, "Square corners and absent top/right/bottom borders");
                pixels.expect(12, 12, edge, "A zero corner radius keeps the border corner square");
                pixels.expect(11, 40, sentinel, "Border never paints outside its authored bounds");
                require(pixels.matches({36, 22, 116, 54}, values.foreground->resolve(mode)) > 12,
                    "Actual DirectWrite glyph pixels use the authored foreground");

                const auto baseline = fixture.render(palette, nullptr);
                ButtonStyleValues padding;
                padding.padding = Insets{17, 8, 23, 9};
                const auto padded = fixture.render(palette, &padding);
                require(baseline.data == padded.data, "Padding-only styles retain the exact default face and glyph paint");
                ButtonStyleValues foreground;
                foreground.foreground = values.foreground;
                const auto recolored = fixture.render(palette, &foreground);
                for (int y = 10; y < 70; ++y)
                    for (int x = 10; x < 134; ++x)
                        if (x < 34 || x >= 118 || y < 20 || y >= 56)
                            require(baseline.at(x, y) == recolored.at(x, y),
                                "Foreground-only styles retain default rounded/elevated face pixels");
                require(recolored.matches({36, 22, 116, 54}, values.foreground->resolve(mode)) > 12,
                    "Foreground-only styles recolor actual glyph pixels");
            }

            const auto high_contrast = Palette::system(ThemeMode::high_contrast, style);
            for (const bool enabled : {true, false}) {
                const auto pixels = fixture.render(high_contrast, &values, enabled);
                pixels.expect(22, 40, rgb(GetSysColor(COLOR_WINDOW)), "High contrast ignores authored background");
                require(pixels.matches({36, 22, 116, 54}, rgb(GetSysColor(enabled ? COLOR_WINDOWTEXT : COLOR_GRAYTEXT))) > 12,
                    "High contrast uses native enabled/disabled system text colors");
                require(pixels.matches({10, 10, 134, 70}, rgb(GetSysColor(enabled ? COLOR_WINDOWTEXT : COLOR_GRAYTEXT))) > 30,
                    "High contrast retains a visible system-colored face outline");
            }
            const auto selected = fixture.render(high_contrast, &values, true, false, true, true);
            selected.expect(22, 40, rgb(GetSysColor(COLOR_HIGHLIGHT)), "High contrast selected fill uses system highlight");
            require(selected.matches({36, 22, 116, 54}, rgb(GetSysColor(COLOR_HIGHLIGHTTEXT))) > 12,
                "High contrast selected glyphs use system highlight text");
        }

        auto base = authored_values();
        constexpr std::array<uint32_t, 5> colors{0xc52525, 0x27b837, 0x314de2, 0x8f2bba, 0xb28317};
        std::vector<ButtonStyleRule> rules;
        for (std::size_t i = 0; i < colors.size(); ++i) {
            ButtonStyleValues value;
            value.background = ThemeColor{colors[i]};
            rules.push_back({static_cast<ButtonStyleState>(i), value});
        }
        Button button(L"State pixels");
        button.set_behavior(ButtonBehavior::toggle);
        button.set_style(ButtonStyle::create(base, rules));
        const auto palette = regular_palette(ThemeMode::light, VisualStyle::classic);
        const auto sample = [&](uint32_t expected) {
            fixture.render(palette, button.effective_style_values(), button.enabled(), button.hovered(),
                button.pressed(), button.checked()).expect(22, 40, expected, "Resolved Button state reaches actual face pixels");
        };
        sample(base.background->light);
        button.set_focused(true); sample(colors[0]);
        button.set_checked(true); sample(colors[1]);
        button.pointer_move(true); sample(colors[2]);
        require(button.pointer_down(), "Begin model pointer press without requiring OS keyboard focus");
        sample(colors[3]);
        button.set_enabled(false); sample(colors[4]);
        ButtonStyleValues local;
        local.background = ThemeColor{0x1dcae1};
        button.set_style_values(local); sample(0x1dcae1);
        button.set_style_values({}); sample(colors[4]);
        button.set_enabled(true); button.cancel(); button.pointer_move(false);
        button.set_checked(false); button.set_focused(false); sample(base.background->light);
    }
    require(Drawing::live_targets() == before, "Software fixture releases its target");
    std::cout << "PASS actual Direct2D pixels: geometry, default face, theme colors, high contrast, state precedence\n";
}
#endif

std::wstring native_text(HWND hwnd) {
    std::wstring text(static_cast<std::size_t>(GetWindowTextLengthW(hwnd)) + 1, L'\0');
    text.resize(GetWindowTextW(hwnd, text.data(), static_cast<int>(text.size())));
    return text;
}
std::vector<HWND> children(HWND host) {
    std::vector<HWND> result;
    EnumChildWindows(host, [](HWND hwnd, LPARAM data) -> BOOL {
        reinterpret_cast<std::vector<HWND>*>(data)->push_back(hwnd);
        return TRUE;
    }, reinterpret_cast<LPARAM>(&result));
    return result;
}
HWND find_host() {
    HWND result{};
    EnumWindows([](HWND hwnd, LPARAM data) -> BOOL {
        DWORD process{};
        GetWindowThreadProcessId(hwnd, &process);
        if (process == GetCurrentProcessId() && native_text(hwnd) == window_title && IsWindowVisible(hwnd)) {
            *reinterpret_cast<HWND*>(data) = hwnd;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&result));
    return result;
}
void trace_module(LPARAM address) {
    HMODULE module{};
    if (address && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(address), &module)) {
        wchar_t path[MAX_PATH]{};
        GetModuleFileNameW(module, path, MAX_PATH);
        std::wcerr << L" module=" << path << L" offset=0x" << std::hex
            << (address - reinterpret_cast<LPARAM>(module)) << std::dec << L'\n';
    }
}
void pump_for(unsigned milliseconds) {
    const auto deadline = GetTickCount64() + milliseconds;
    do {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            require(message.message != WM_QUIT, "The window must remain alive during fixture message delivery");
            const auto before = trace_resources_enabled ? GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS) : 0;
            TranslateMessage(&message);
            DispatchMessageW(&message);
            const auto after = trace_resources_enabled ? GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS) : 0;
            if (before != after) {
                std::cerr << "TRACE dispatch hwnd=" << message.hwnd << " message=" << std::hex << message.message
                    << " wp=" << message.wParam << " lp=" << message.lParam << std::dec
                    << " USER " << before << " -> " << after << '\n';
                // USER totals include timers. Identify native callbacks without guessing from the count.
                if (message.message == WM_TIMER)
                    trace_module(message.lParam ? message.lParam : GetWindowLongPtrW(message.hwnd, GWLP_WNDPROC));
            }
        }
        Sleep(1);
    } while (GetTickCount64() < deadline);
}
void flush(HWND host) {
    SendMessageW(host, WM_APP + 12, 0, 0);
    require(RedrawWindow(host, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW) != FALSE,
        "Paint pending native style presentation");
}
std::size_t presents{}, paints{};
void presented(HWND) { ++presents; }
LRESULT CALLBACK count_paints(HWND hwnd, UINT message, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    if (message == WM_PAINT) ++paints;
    const auto before = trace_resources_enabled ? GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS) : 0;
    const auto result = DefSubclassProc(hwnd, message, wp, lp);
    const auto after = trace_resources_enabled ? GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS) : 0;
    if (before != after)
        std::cerr << "TRACE native hwnd=" << hwnd << " message=" << std::hex << message
            << " wp=" << wp << " lp=" << lp << std::dec << " USER " << before << " -> " << after << '\n';
    return result;
}
struct ObservePaints {
    std::vector<HWND> windows;
    explicit ObservePaints(HWND host) : windows(children(host)) {
        windows.push_back(host);
        DrawingTestAccess::observe(presented);
        for (const auto hwnd : windows)
            require(SetWindowSubclass(hwnd, count_paints, 71, 0) != FALSE, "Observe native paint messages");
    }
    ~ObservePaints() {
        DrawingTestAccess::observe(nullptr);
        for (const auto hwnd : windows) if (IsWindow(hwnd)) RemoveWindowSubclass(hwnd, count_paints, 71);
    }
};
struct Resources {
    SIZE_T private_bytes{};
    DWORD handles{}, gdi{}, user{};
    static Resources read() {
        PROCESS_MEMORY_COUNTERS_EX memory{};
        memory.cb = sizeof(memory);
        require(GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),
            sizeof(memory)) != FALSE, "Read process private bytes");
        Resources result{memory.PrivateUsage};
        require(GetProcessHandleCount(GetCurrentProcess(), &result.handles) != FALSE, "Read process handle count");
        result.gdi = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
        result.user = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
        return result;
    }
    void retained_from(const Resources& baseline) const {
        if (trace_resources_enabled || user != baseline.user || gdi > baseline.gdi + 2 || handles > baseline.handles + 8 ||
            private_bytes > baseline.private_bytes + 8 * 1024 * 1024)
            std::cerr << "Resource retention: USER " << baseline.user << " -> " << user
                << ", GDI " << baseline.gdi << " -> " << gdi << ", handles " << baseline.handles << " -> " << handles
                << ", private bytes " << baseline.private_bytes << " -> " << private_bytes << '\n';
        require(user == baseline.user, "Style/theme cycles retain no additional USER objects");
        require(gdi <= baseline.gdi + 2, "Style/theme cycles retain no growing GDI object set");
        require(handles <= baseline.handles + 8, "Style/theme cycles retain no growing kernel handle set");
        require(private_bytes <= baseline.private_bytes + 8 * 1024 * 1024,
            "Warmed style/theme cycles retain no more than 8 MiB of private allocator/cache growth");
    }
};
void trace_resources(const char* phase, int index) {
    if (!trace_resources_enabled) return;
    const auto resources = Resources::read();
    std::cerr << "TRACE " << phase << ' ' << index << " USER=" << resources.user << '\n';
    const auto print = [](HWND hwnd, LPARAM) -> BOOL {
        DWORD process{};
        const auto thread = GetWindowThreadProcessId(hwnd, &process);
        if (process != GetCurrentProcessId()) return TRUE;
        wchar_t cls[256]{};
        GetClassNameW(hwnd, cls, 256);
        std::wcerr << L" HWND=" << hwnd << L" class=" << cls << L" thread=" << thread
            << L" parent=" << GetAncestor(hwnd, GA_PARENT) << L'\n';
        return TRUE;
    };
    EnumWindows(print, 0);
    EnumWindows([](HWND hwnd, LPARAM data) -> BOOL {
        DWORD process{};
        GetWindowThreadProcessId(hwnd, &process);
        if (process == GetCurrentProcessId()) EnumChildWindows(hwnd, reinterpret_cast<WNDENUMPROC>(data), 0);
        return TRUE;
    }, reinterpret_cast<LPARAM>(static_cast<WNDENUMPROC>(print)));
    for (auto hwnd = FindWindowExW(HWND_MESSAGE, nullptr, nullptr, nullptr); hwnd;
        hwnd = FindWindowExW(HWND_MESSAGE, hwnd, nullptr, nullptr))
        print(hwnd, 0);
}
unsigned long long ticks(FILETIME value) {
    return (static_cast<unsigned long long>(value.dwHighDateTime) << 32) | value.dwLowDateTime;
}
unsigned long long cpu_ticks() {
    FILETIME created{}, exited{}, kernel{}, user{};
    require(GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user) != FALSE, "Read UI thread CPU time");
    return ticks(kernel) + ticks(user);
}
unsigned long long cpu_cycles() {
    ULONG64 value{};
    require(QueryThreadCycleTime(GetCurrentThread(), &value) != FALSE, "Read UI thread cycle count");
    return value;
}
#ifndef XUI_STYLING_BASELINE
void lower_level_benchmarks() {
    const auto palette = regular_palette(ThemeMode::light, VisualStyle::classic);
    const auto colors = theme_colors(ThemeMode::light, VisualStyle::classic);
    auto equivalent = ButtonStyleValues{};
    equivalent.background = ThemeColor{colors.surface};
    equivalent.foreground = ThemeColor{colors.text};
    equivalent.border_brush = ThemeColor{colors.border};
    equivalent.corner_radius = 6.0f;
    ButtonStyleValues hovered;
    hovered.background = ThemeColor{colors.hover};
    const std::array<std::shared_ptr<const ButtonStyle>, 3> styles{
        nullptr, ButtonStyle::create(equivalent, {{ButtonStyleState::hovered, hovered}}),
        ButtonStyle::create(authored_values(), {{ButtonStyleState::hovered, hovered}})};
    SoftwareFixture fixture;
    Size text_size{};
    auto text = fixture.drawing.layout(L"Style pixels MMMM", TextStyle::body, text_size);
    const std::array<const char*, 3> names{"default", "equivalent", "authored"};
    std::uint64_t checksum{};
    for (unsigned repetition = 0; repetition < 6; ++repetition) {
        for (unsigned order = 0; order < 3; ++order) {
            const unsigned index = repetition % 2 ? 2 - order : order;
            Button button(L"Style pixels MMMM");
            button.set_style(styles[index]);
            for (unsigned i = 0; i < 10000; ++i) {
                button.pointer_move(i % 2 != 0);
                (void)button.effective_style_values();
            }
            constexpr unsigned state_iterations = 1000000;
            auto cycles = cpu_cycles();
            auto cpu = cpu_ticks();
            auto start = std::chrono::steady_clock::now();
            for (unsigned i = 0; i < state_iterations; ++i) {
                button.pointer_move(i % 2 != 0);
                const auto* resolved = button.effective_style_values();
                checksum += resolved && resolved->background ? resolved->background->light : unsigned(button.hovered());
            }
            auto wall = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
            auto used_cpu = (cpu_ticks() - cpu) / 10.0;
            auto used_cycles = cpu_cycles() - cycles;
            std::cout << "LOWER state repetition=" << repetition << " order=" << order << " variant=" << names[index]
                << " iterations=" << state_iterations << " wall_ns=" << wall * 1000 / state_iterations
                << " cpu_ns=" << used_cpu * 1000 / state_iterations << " cycles=" << double(used_cycles) / state_iterations << '\n';

            const auto draw = [&](unsigned i) {
                fixture.drawing.fill({0, 0, 160, 96}, D2D1::ColorF(sentinel));
                const bool hot = i % 2 != 0;
                auto ink = palette.text;
                if (styles[index]) {
                    ink = fixture.drawing.styled_button_face(face, palette, ButtonAppearance::standard,
                        true, hot, false, false, styles[index]->values(hot ? 4 : 0));
                } else {
                    fixture.drawing.rounded(face, hot ? palette.hover : palette.surface);
                    fixture.drawing.rounded(face, palette.border, 6, true);
                }
                fixture.drawing.text_layout(text.Get(), {24, 24, 108, 32}, ink);
            };
            require(fixture.drawing.begin(fixture.hwnd, 96, D2D1::ColorF(sentinel)), "Begin isolated software raster workload");
            for (unsigned i = 0; i < 100; ++i) draw(i);
            DrawingTestAccess::flush(fixture.drawing);
            constexpr unsigned raster_iterations = 4000;
            cycles = cpu_cycles(); cpu = cpu_ticks(); start = std::chrono::steady_clock::now();
            for (unsigned i = 0; i < raster_iterations; ++i) {
                draw(i);
                if (i % 32 == 31) DrawingTestAccess::flush(fixture.drawing);
            }
            DrawingTestAccess::flush(fixture.drawing);
            wall = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
            used_cpu = (cpu_ticks() - cpu) / 10.0; used_cycles = cpu_cycles() - cycles;
            require(fixture.drawing.end(), "End isolated software raster workload");
            std::cout << "LOWER raster repetition=" << repetition << " order=" << order << " variant=" << names[index]
                << " iterations=" << raster_iterations << " surface=160x96 face=120x56 text=identical"
                << " wall_us=" << wall / raster_iterations << " cpu_us=" << used_cpu / raster_iterations
                << " cycles=" << double(used_cycles) / raster_iterations << '\n';
        }
    }
    Size fitting_size{};
    const auto fitting_text = fixture.drawing.layout(L"MMMM", TextStyle::body, fitting_size);
    std::optional<Pixels> fitting_pixels;
    for (unsigned repetition = 0; repetition < 6; ++repetition) {
        for (const bool unclipped : {repetition % 2 != 0, repetition % 2 == 0}) {
            const auto draw = [&] {
                fixture.drawing.fill({0, 0, 160, 96}, D2D1::ColorF(sentinel));
                constexpr Rect bounds{24, 24, 108, 32}, clip{12, 12, 140, 72};
                if (unclipped) fixture.drawing.text_layout(fitting_text.Get(), bounds, palette.text);
                else {
                    fixture.drawing.push_clip(clip);
                    fixture.drawing.text_layout(fitting_text.Get(), bounds, palette.text);
                    fixture.drawing.pop_clip();
                }
            };
            require(fixture.drawing.begin(fixture.hwnd, 96, D2D1::ColorF(sentinel)), "Begin isolated content-clip workload");
            for (unsigned i = 0; i < 100; ++i) draw();
            const auto pixels = readback(fixture.drawing);
            if (fitting_pixels) require(pixels.data == fitting_pixels->data, "Text-clip profiling variants paint identical glyph pixels");
            else fitting_pixels = pixels;
            DrawingTestAccess::flush(fixture.drawing);
            constexpr unsigned iterations = 4000;
            const auto cycles = cpu_cycles(), cpu = cpu_ticks();
            const auto start = std::chrono::steady_clock::now();
            for (unsigned i = 0; i < iterations; ++i) {
                draw();
                if (i % 32 == 31) DrawingTestAccess::flush(fixture.drawing);
            }
            DrawingTestAccess::flush(fixture.drawing);
            const auto wall = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
            const auto used_cpu = (cpu_ticks() - cpu) / 10.0, used_cycles = double(cpu_cycles() - cycles);
            require(fixture.drawing.end(), "End isolated content-clip workload");
            std::cout << "LOWER textclip repetition=" << repetition << " variant=" << (unclipped ? "unclipped" : "reference")
                << " iterations=" << iterations << " surface=160x96 text=identical"
                << " wall_us=" << wall / iterations << " cpu_us=" << used_cpu / iterations
                << " cycles=" << used_cycles / iterations << '\n';
        }
    }
    std::cout << "LOWER checksum=" << checksum << '\n';
}
void benchmark(HWND host, Button& button, const std::shared_ptr<const ButtonStyle>& style) {
    const auto expected_bounds = button.bounds();
    const auto expected_name = button.name();
    const auto expected_children = children(host);
    DWORD process{};
    require(GetWindowThreadProcessId(host, &process) == GetCurrentThreadId() && process == GetCurrentProcessId(),
        "Benchmark foreground target belongs to this UI thread");
    if (GetForegroundWindow() != host) {
        const auto foreground_thread = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
        const auto current_thread = GetCurrentThreadId();
        const bool attach = foreground_thread && foreground_thread != current_thread;
        if (attach) require(AttachThreadInput(current_thread, foreground_thread, TRUE) != FALSE,
            "Attach the owned benchmark thread for foreground transfer");
        SetForegroundWindow(host);
        if (attach) require(AttachThreadInput(current_thread, foreground_thread, FALSE) != FALSE,
            "Detach the owned benchmark thread after foreground transfer");
    }
    require(GetForegroundWindow() == host, "Benchmark owns foreground before presentation timing");
    for (const bool styled : {benchmark_styled_first, !benchmark_styled_first}) {
        button.set_style(styled ? style : nullptr);
        for (int i = 0; i < 60; ++i) { button.pointer_move(i % 2 != 0); flush(host); }
        const auto bounds = button.bounds();
        require(bounds.x == expected_bounds.x && bounds.y == expected_bounds.y && bounds.width == expected_bounds.width &&
            bounds.height == expected_bounds.height && button.name() == expected_name && children(host) == expected_children,
            "Styled and default benchmarks use identical content, bounds and native peers");
        const auto before = Resources::read();
        const auto frames = presents, native_paints = paints;
        std::vector<double> latency;
        constexpr int iterations = benchmark_frames;
        latency.reserve(iterations);
        const auto cpu = cpu_ticks(), cycles = cpu_cycles();
        for (int i = 0; i < iterations; ++i) {
            const auto foreground = GetForegroundWindow();
            if (foreground != host) {
                DWORD foreground_process{};
                GetWindowThreadProcessId(foreground, &foreground_process);
                std::cerr << "Foreground guard: styled=" << styled << " frame=" << i
                    << " foreground_pid=" << foreground_process << " owned_pid=" << GetCurrentProcessId() << '\n';
            }
            require(foreground == host, "Benchmark keeps foreground during presentation timing");
            const auto start = std::chrono::steady_clock::now();
            button.pointer_move(i % 2 != 0);
            flush(host);
            latency.push_back(std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count());
        }
        const auto cpu_us = (cpu_ticks() - cpu) / 10.0;
        const auto used_cycles = cpu_cycles() - cycles;
        const auto after = Resources::read();
        std::sort(latency.begin(), latency.end());
        require(presents - frames == iterations && paints - native_paints == 3 * iterations,
            "Both benchmark variants perform exactly the same completed root and native paints");
        require(Drawing::live_targets() == 1, "Benchmark uses only the root render target");
        after.retained_from(before);
        std::cout << "BENCH styled=" << styled << " styled_first=" << benchmark_styled_first << " iterations=" << iterations
            << " ui_thread_cpu_us_per_state_paint=" << cpu_us / iterations
            << " ui_thread_cycles_per_state_paint=" << double(used_cycles) / iterations
            << " button_width=" << bounds.width << " button_height=" << bounds.height
            << " latency_p50_us=" << latency[iterations / 2] << " latency_p95_us=" << latency[iterations * 95 / 100]
            << " presents=" << presents - frames << " wm_paint=" << paints - native_paints
            << " private_before=" << before.private_bytes << " private_after=" << after.private_bytes
            << " handles=" << after.handles << " gdi=" << after.gdi << " user=" << after.user
            << " targets=" << Drawing::live_targets() << '\n';
    }
    button.pointer_move(false);
    button.set_style(nullptr);
}
#endif

struct StartFixture {
    static inline thread_local StartFixture* active{};
    Window& window;
    std::function<void(HWND)> body;
    UINT_PTR timer{};
    ULONGLONG deadline{GetTickCount64() + 10000};
    bool ran{};
    std::exception_ptr error;
    StartFixture(Window& value, std::function<void(HWND)> callback) : window(value), body(std::move(callback)) {
        active = this;
        timer = SetTimer(nullptr, 0, 10, [](HWND, UINT, UINT_PTR id, DWORD) {
            auto& fixture = *active;
            const auto hwnd = find_host();
            if (!hwnd && GetTickCount64() < fixture.deadline) return;
            KillTimer(nullptr, id);
            fixture.timer = 0;
            try {
                require(hwnd != nullptr, "Native styling fixture starts within ten seconds");
                fixture.ran = true;
                fixture.body(hwnd);
            } catch (...) { fixture.error = std::current_exception(); }
            fixture.window.close();
        });
        require(timer != 0, "Schedule same-thread native style fixture");
    }
    ~StartFixture() { if (timer) KillTimer(nullptr, timer); active = nullptr; }
};
#ifndef XUI_STYLING_BASELINE
void button_focus_native_contracts(FocusFixture fixture = FocusFixture::all) {
    const bool include_buttons = fixture == FocusFixture::all || fixture == FocusFixture::buttons;
    const bool include_choices = fixture == FocusFixture::all || fixture == FocusFixture::choices;
    const bool include_selectors = fixture == FocusFixture::all || fixture == FocusFixture::selectors;
    const bool include_expanders = fixture == FocusFixture::all || fixture == FocusFixture::expanders;
    WindowOptions options{window_title, {740, 490}, ThemeMode::dark};
    options.visual_style = VisualStyle::winui;
    options.show_activated = false;
    Application application;
    auto owned_window = application.create_window(options);
    auto& window = *owned_window;
    auto root = std::make_shared<Stack>(Axis::vertical);
    root->set_padding({20, 20, 20, 20});
    root->set_spacing(12);
    if (include_selectors) {
        PartStyleValues surface; surface.background = ThemeColor{0x363636};
        root->set_control_style_values(StylePart::root, surface);
    }
    auto row = std::make_shared<Stack>(Axis::horizontal);
    row->set_spacing(8);
    std::array<std::shared_ptr<Button>, 4> buttons;
    for (std::size_t i = 0; i < buttons.size(); ++i) {
        if (i == 3) buttons[i] = std::make_shared<HyperlinkButton>(L"Focus link");
        else buttons[i] = std::make_shared<Button>(L"Focus " + std::to_wstring(i));
        buttons[i]->set_fixed_size({80, 32});
        row->add(buttons[i]);
    }
    ButtonStyleValues legacy;
    legacy.corner_radius = 16.0f;
    buttons[0]->set_style_values(legacy);
    PartStyleValues named;
    named.corner_radius = 16.0f;
    buttons[1]->set_control_style_values(StylePart::root, named);
    buttons[3]->set_control_style_values(StylePart::root, named);
    auto menu_bar = std::make_shared<MenuBar>(L"Menu focus");
    std::vector<CommandRecord> headings{{901, 0, L"File"}, {902, 0, L"Edit"}, {903, 0, L"View"}};
    for (auto& heading : headings) heading.kind = CommandKind::submenu;
    menu_bar->set_commands(std::make_shared<CommandSet>(std::move(headings)));
    menu_bar->set_fixed_size({240, 40});
    PartStyleValues square; square.corner_radius = 0;
    menu_bar->heading(902)->set_control_style_values(StylePart::root, square);
    menu_bar->heading(903)->set_control_style_values(StylePart::root, named);
    row->add(menu_bar);
    root->add(row);
    auto input = std::make_shared<TextInput>(L"Native input");
    input->set_caption_visible(false);
    input->set_text(L"Keep native text");
    root->add(input);
    auto content = std::make_shared<Stack>(Axis::vertical);
    content->set_spacing(0);
    auto clipped = std::make_shared<Button>(L"Clipped focus");
    clipped->set_fixed_size({80, 32});
    content->add(clipped);
    auto spacer = std::make_shared<Label>(L"");
    spacer->set_fixed_size({80, 80});
    content->add(spacer);
    auto scroll = std::make_shared<ScrollView>(content, L"Focus viewport");
    scroll->set_fixed_size({180, 48});
    root->add(scroll);
    auto choices = std::make_shared<Stack>(Axis::horizontal);
    choices->set_spacing(16);
    std::array<std::shared_ptr<Toggle>, 3> checks{
        std::make_shared<Toggle>(L"Binary"), std::make_shared<CheckBox>(L"Three-state"),
        std::make_shared<CheckBox>(L"Rounded")};
    for (const auto& check : checks) {
        check->set_fixed_size({80, 32});
        choices->add(check);
    }
    checks.back()->set_control_style_values(StylePart::root, named);
    root->add(choices);
    auto radio_row = std::make_shared<Stack>(Axis::horizontal);
    radio_row->set_spacing(16);
    std::array<std::shared_ptr<RadioGroup>, 2> radios{
        std::make_shared<RadioGroup>(L"Default radios"), std::make_shared<RadioGroup>(L"Rounded radios")};
    for (const auto& radio : radios) {
        radio->set_items({{1, L"First"}, {2, L"Second"}}, 1);
        radio->set_fixed_size({160, 64});
        radio_row->add(radio);
    }
    radios.back()->set_control_style(ControlStyle::create(StyleTarget::radio_group, {},
        {{StylePart::item, style_states::focused, named}}));
    root->add(radio_row);
    auto selector_row = std::make_shared<Stack>(Axis::horizontal);
    selector_row->set_spacing(8);
    std::array<std::shared_ptr<SelectorBar>, 4> selectors;
    for (std::size_t i = 0; i < selectors.size(); ++i) {
        selectors[i] = std::make_shared<SelectorBar>(L"Selector focus " + std::to_wstring(i));
        selectors[i]->set_items({{1, L"One"}, {2, L"Two"}}, 1);
        selectors[i]->set_fixed_size({160, 42});
        if (i > 0) {
            PartStyleValues root_style; root_style.padding = Insets{};
            if (i == 1) root_style.background = ThemeColor{0x777777};
            selectors[i]->set_control_style_values(StylePart::root, root_style);
            PartStyleValues item_style; item_style.corner_radius = i == 2 ? 100.0f : 0.0f;
            if (i == 1) item_style.background = ThemeColor{0x545454};
            selectors[i]->set_control_style_values(StylePart::item, item_style);
        }
        if (i == 3) {
            auto viewport = std::make_shared<ScrollView>(selectors[i], L"Selector focus viewport");
            viewport->set_fixed_size({160, 42});
            selector_row->add(viewport);
        } else selector_row->add(selectors[i]);
    }
    root->add(selector_row);
    auto expander_row = std::make_shared<Stack>(Axis::horizontal);
    expander_row->set_spacing(16);
    std::array<std::shared_ptr<Expander>, 2> expanders{
        std::make_shared<Expander>(L"Default details", std::make_shared<Label>(L"Body")),
        std::make_shared<Expander>(L"Rounded details", std::make_shared<Label>(L"Body"))};
    for (const auto& expander : expanders) {
        expander->set_fixed_size({160, 48});
        expander->set_expanded(false);
        expander_row->add(expander);
    }
    expanders.back()->set_control_style_values(StylePart::header, named);
    root->add(expander_row);
    window.set_content(root);
    application.show(window);
    const auto host = find_host();
    require(host != nullptr, "Nonactivating focus fixture has its own window");
    const auto peers = children(host);
    HWND edit{}, button_peer{};
    for (const auto hwnd : peers) {
        if (native_text(hwnd) == buttons[0]->name()) button_peer = hwnd;
        wchar_t cls[80]{};
        GetClassNameW(hwnd, cls, 80);
        if (_wcsicmp(cls, L"EDIT") == 0) edit = hwnd;
    }
    require(button_peer && edit, "Focus fixture owns button and native editor peers");
    SendMessageW(edit, EM_SETSEL, 2, 7);
    const auto area = [&](const Control& control) {
        for (const auto hwnd : peers) if (native_text(hwnd) == control.name()) {
            RECT bounds{};
            require(GetWindowRect(hwnd, &bounds) != FALSE, "Read focus specimen bounds");
            MapWindowPoints(nullptr, host, reinterpret_cast<POINT*>(&bounds), 2);
            return bounds;
        }
        throw std::runtime_error("Focus specimen peer is missing");
    };
    const owned_window_capture::Device capture_device;
    const auto capture = [&] {
        flush(host);
        auto image = owned_window_capture::capture(host, capture_device);
        return Pixels{image.width, image.height, std::move(image.data)};
    };
    bool ran{};
    std::exception_ptr error;
    window.on_key([&](const KeyEvent&) {
        try {
            ran = true;
            for (const auto mode : {ThemeMode::light, ThemeMode::dark, ThemeMode::high_contrast}) {
                std::cout << "Focus adapter theme " << static_cast<int>(mode) << std::endl;
                window.set_theme(mode);
                const auto baseline = capture();
                const auto plain_radio = area(*radios.front());
                const auto styled_radio = area(*radios.back());
                const auto disclosure_area = [&](const Expander& expander) {
                    const auto bounds = area(expander);
                    const auto box = expander.disclosure_bounds();
                    const float scale = GetDpiForWindow(host) / 96.0f;
                    const int left = bounds.left + static_cast<int>(std::lround((box.x - expander.bounds().x) * scale));
                    const int top = bounds.top + static_cast<int>(std::lround((box.y - expander.bounds().y) * scale));
                    return RECT{left, top, left + static_cast<int>(std::lround(box.width * scale)),
                        top + static_cast<int>(std::lround(box.height * scale))};
                };
                const auto compare_disclosures = [&](const Pixels& pixels) {
                    const auto plain = disclosure_area(*expanders.front()), styled = disclosure_area(*expanders.back());
                    for (int y = 0; y < plain.bottom - plain.top; ++y)
                        for (int x = 0; x < plain.right - plain.left; ++x)
                            pixels.expect(styled.left + x, styled.top + y, pixels.at(plain.left + x, plain.top + y),
                                "Styled and default Expanders use the same disclosure direction");
                };
                if (include_expanders) compare_disclosures(baseline);
                if (include_choices) for (int y = 0; y < plain_radio.bottom - plain_radio.top; ++y)
                    for (int x = 0; x < plain_radio.right - plain_radio.left; ++x)
                        baseline.expect(styled_radio.left + x, styled_radio.top + y,
                            baseline.at(plain_radio.left + x, plain_radio.top + y),
                            "A radio focus-only style preserves the default unfocused appearance");
                if (include_buttons) for (const auto& button : buttons) {
                    button->set_focused(true);
                    const auto focused = capture();
                    const auto bounds = area(*button);
                    const int x = (bounds.left + bounds.right) / 2;
                    const int y = bounds.top - MulDiv(2, GetDpiForWindow(host), 96);
                    require(focused.at(x, y) != baseline.at(x, y),
                        "Both Button style adapters and unstyled buttons paint focus outside the native peer");
                    const int corner = MulDiv(1, GetDpiForWindow(host), 96);
                    if (button != buttons[2] && !Palette::system(mode, VisualStyle::winui).high_contrast)
                        focused.expect(bounds.left + corner, bounds.top + corner,
                            baseline.at(bounds.left + corner, bounds.top + corner),
                            "The Window adapter preserves pill focus corners for legacy and named styles");
                    button->set_focused(false);
                    const auto cleared = capture();
                    cleared.expect(x, y, baseline.at(x, y), "Focus removal repaints the former external border");
                }
                if (include_buttons) {
                    for (const auto& child : menu_bar->retained_children()) {
                        const auto heading = std::static_pointer_cast<MenuBar::Heading>(child);
                        const auto bounds = area(*heading);
                        const int outset = MulDiv(2, GetDpiForWindow(host), 96);
                        const int x = (bounds.left + bounds.right) / 2, y = (bounds.top + bounds.bottom) / 2;
                        const std::array<POINT, 4> edges{{{x, bounds.top - outset}, {x, bounds.bottom + outset - 1},
                            {bounds.left - outset, y}, {bounds.right + outset - 1, y}}};
                        heading->set_focused(true);
                        const auto focused = capture();
                        for (const auto point : edges)
                            require(focused.at(point.x, point.y) != baseline.at(point.x, point.y),
                                "Menu heading focus retains all four edges within the bar");
                        heading->set_focused(false);
                        const auto cleared = capture();
                        for (const auto point : edges)
                            cleared.expect(point.x, point.y, baseline.at(point.x, point.y),
                                "Menu heading blur removes every focus edge");
                    }
                    clipped->set_focused(true);
                    const auto focused = capture();
                    const auto bounds = area(*clipped);
                    const int x = (bounds.left + bounds.right) / 2;
                    const int outside = MulDiv(2, GetDpiForWindow(host), 96);
                    focused.expect(x, bounds.top - outside, baseline.at(x, bounds.top - outside),
                        "External button focus remains clipped at the scroll viewport");
                    require(focused.at(x, bounds.bottom + outside - 1) != baseline.at(x, bounds.bottom + outside - 1),
                        "The same clipped button retains its visible lower focus border");
                    clipped->set_focused(false);
                }
                if (include_choices) for (const auto& check : checks) {
                    check->set_focused(true);
                    const auto focused = capture();
                    const auto bounds = area(*check);
                    const int x = bounds.left - MulDiv(6, GetDpiForWindow(host), 96);
                    const int y = (bounds.top + bounds.bottom) / 2;
                    require(focused.at(x, y) != baseline.at(x, y),
                        "Binary Toggle, CheckBox and authored CheckBox retain their wider horizontal focus margin");
                    const int outside = bounds.left - MulDiv(8, GetDpiForWindow(host), 96);
                    focused.expect(outside, y, baseline.at(outside, y),
                        "Checkbox focus never exceeds its seven-DIP horizontal margin");
                    check->set_focused(false);
                    capture().expect(x, y, baseline.at(x, y), "Checkbox blur removes the external outline");
                }
                if (include_choices) for (const auto& radio : radios) {
                    radio->set_focused(true);
                    auto focused = capture();
                    const auto bounds = area(*radio);
                    const auto scale = [&](float value) { return static_cast<int>(std::lround(value * GetDpiForWindow(host) / 96)); };
                    const auto first_row = radio->item_bounds(0);
                    const auto second_row = radio->item_bounds(1);
                    const int x = bounds.left + scale(first_row.x - 6);
                    const int first_y = bounds.top + scale(first_row.y + first_row.height / 2);
                    const int second_y = bounds.top + scale(second_row.y + second_row.height / 2);
                    require(focused.at(x, first_y) != baseline.at(x, first_y),
                        "Radio focus extends outside the group peer around the selected row");
                    focused.expect(x, second_y, baseline.at(x, second_y), "Unselected radio rows have no focus outline");
                    if (radio == radios.back() && !Palette::system(mode, VisualStyle::winui).high_contrast)
                        focused.expect(x, bounds.top + scale(first_row.y + 6),
                            baseline.at(x, bounds.top + scale(first_row.y + 6)),
                            "Radio focus follows the selected item's focused-state corner radius");
                    require(radio->step(1) && radio->selected() == 2, "Radio selection moves to the next enabled row");
                    focused = capture();
                    focused.expect(x, first_y, baseline.at(x, first_y), "Moving selection removes the previous radio outline");
                    require(focused.at(x, second_y) != baseline.at(x, second_y),
                        "Radio focus follows selection instead of surrounding the entire group");
                    radio->set_focused(false);
                    capture().expect(x, second_y, baseline.at(x, second_y), "Radio blur removes the external outline");
                    radio->set_selected(1);
                }
                if (include_selectors) for (std::size_t i = 0; i < selectors.size(); ++i) {
                    const auto& selector = selectors[i];
                    selector->set_focused(true);
                    auto focused = capture();
                    const auto bounds = area(*selector);
                    const auto scale = [&](float value) { return static_cast<int>(std::lround(value * GetDpiForWindow(host) / 96)); };
                    const auto first = selector->item_bounds(0), second = selector->item_bounds(1);
                    require(first.width > 0 && second.width > 0, "SelectorBar fixture displays both selection slots");
                    if (!Palette::system(mode, VisualStyle::winui).high_contrast) {
                        if (i < 3) {
                            const auto fill = i == 1 ? 0x545454u : 0x363636u;
                            baseline.expect(bounds.left + scale(first.x + first.width / 2), bounds.top + scale(first.y + 4), fill,
                                "WinUI SelectorBar selected faces stay transparent unless the author supplies a fill");
                            baseline.expect(bounds.left + scale(second.x + second.width / 2), bounds.top + scale(second.y + 4), fill,
                                "WinUI SelectorBar unselected faces preserve the parent or authored fill");
                            baseline.expect(bounds.left + scale(first.x + first.width + 2), bounds.top + scale(first.y + 4),
                                i == 1 ? 0x777777u : 0x363636u, "WinUI SelectorBar root stays transparent unless authored");
                        }
                        const int marker_y = bounds.top + scale(first.y + first.height - 2);
                        int marker_left = -1, marker_right = -1;
                        for (int px = bounds.left + scale(first.x); px < bounds.left + scale(first.x + first.width); ++px) {
                            const auto color = baseline.at(px, marker_y);
                            if (int(color & 0xff) - int((color >> 16) & 0xff) > 4) {
                                if (marker_left < 0) marker_left = px;
                                marker_right = px + 1;
                            }
                        }
                        const int center = bounds.left + scale(first.x + first.width / 2);
                        require(marker_left == center - scale(8) && marker_right == center + scale(8),
                            "The default WinUI SelectorBar selection marker is centered and sixteen DIPs wide");
                    }
                    const int x = bounds.left + scale(first.x + first.width / 2);
                    const int next_x = bounds.left + scale(second.x + second.width / 2);
                    const int y = bounds.top + scale(first.y - 1);
                    const bool external = !Palette::system(mode, VisualStyle::winui).high_contrast && i != 3;
                    if (external) {
                        focused.expect(x, y, composite_argb_on_rgb(mode == ThemeMode::light ? 0xe4000000u : 0xffffffffu,
                            baseline.at(x, y)), "SelectorBar paints its opaque-width native primary focus stroke");
                        focused.expect(x, bounds.top + scale(first.y - 3), baseline.at(x, bounds.top + scale(first.y - 3)),
                            "SelectorBar focus does not exceed the native margin");
                        if (i == 2)
                            focused.expect(bounds.left + scale(first.x), y, baseline.at(bounds.left + scale(first.x), y),
                                "SelectorBar focus preserves authored pill corners");
                        if (i == 1)
                            require(focused.at(bounds.left + scale(first.x), y) != baseline.at(bounds.left + scale(first.x), y),
                                "SelectorBar focus preserves authored square corners");
                    } else if (i == 3) focused.expect(x, y, baseline.at(x, y),
                        "SelectorBar respects ancestor viewport clips");
                    else focused.expect(x, bounds.top + scale(first.y - 3), baseline.at(x, bounds.top + scale(first.y - 3)),
                        "High-contrast SelectorBar retains its existing focus extent");
                    require(selector->step(1) && selector->selected() == 2,
                        "SelectorBar keeps its existing selection movement");
                    focused = capture();
                    focused.expect(x, y, baseline.at(x, y), "Moving selection removes the old SelectorBar outline");
                    if (external) require(focused.at(next_x, y) != baseline.at(next_x, y),
                        "SelectorBar focus follows the selected item");
                    selector->set_focused(false);
                    capture().expect(next_x, y, baseline.at(next_x, y), "SelectorBar blur removes external focus");
                    selector->set_selected(1);
                }
                if (include_selectors && !Palette::system(mode, VisualStyle::winui).high_contrast) {
                    const auto& selector = selectors.front();
                    const auto bounds = area(*selector);
                    const auto item = selector->item_bounds(1);
                    const auto scale = [&](float value) { return static_cast<int>(std::lround(value * GetDpiForWindow(host) / 96)); };
                    const int x = scale(item.x + item.width / 2), y = scale(item.y + 4);
                    const auto peer = std::find_if(peers.begin(), peers.end(),
                        [&](HWND hwnd) { return native_text(hwnd) == selector->name(); });
                    require(peer != peers.end(), "SelectorBar hover fixture owns its peer");
                    SendMessageW(*peer, WM_MOUSEMOVE, 0, MAKELPARAM(x, y));
                    capture().expect(bounds.left + x, bounds.top + y, 0x363636,
                        "WinUI SelectorBar hover preserves its transparent item background");
                    SendMessageW(*peer, WM_MOUSELEAVE, 0, 0);
                }
                if (include_expanders && !Palette::system(mode, VisualStyle::winui).high_contrast) {
                    for (const auto& expander : expanders) {
                        const auto bounds = area(*expander);
                        const auto scale = [&](float value) { return static_cast<int>(std::lround(value * GetDpiForWindow(host) / 96)); };
                        const auto disclosure = expander->disclosure_bounds();
                        const int x = bounds.left + scale(24), y = bounds.top + scale(6);
                        const int glyph_x = bounds.left + scale(disclosure.x - expander->bounds().x + 4);
                        const int glyph_y = bounds.top + scale(disclosure.y - expander->bounds().y + disclosure.height / 2);
                        expander->pointer_move(true);
                        auto interaction = capture();
                        interaction.expect(x, y, baseline.at(x, y), "Expander hover does not replace the header background");
                        require(interaction.at(glyph_x, glyph_y) != baseline.at(glyph_x, glyph_y),
                            "Expander hover paints the disclosure background");
                        expander->pointer_down();
                        interaction = capture();
                        interaction.expect(x, y, baseline.at(x, y), "Expander press does not paint a selected header");
                        require(interaction.at(glyph_x, glyph_y) != baseline.at(glyph_x, glyph_y),
                            "Expander press retains a disclosure-only interaction fill");
                        expander->pointer_up(false);
                        expander->pointer_move(false);
                        capture().expect(glyph_x, glyph_y, baseline.at(glyph_x, glyph_y),
                            "Cancelled Expander press removes the disclosure fill");
                    }
                }
                if (include_expanders) {
                    for (const auto& expander : expanders) expander->set_expanded(true);
                    const auto expanded_pixels = capture();
                    compare_disclosures(expanded_pixels);
                    const auto glyph = disclosure_area(*expanders.front());
                    bool glyph_changed{};
                    for (int y = glyph.top; y < glyph.bottom; ++y)
                        for (int x = glyph.left; x < glyph.right; ++x)
                            glyph_changed |= expanded_pixels.at(x, y) != baseline.at(x, y);
                    require(glyph_changed, "Expanding changes the disclosure direction");
                    for (const auto& expander : expanders) expander->set_expanded(false);
                }
            }
            DWORD first{}, last{};
            SendMessageW(edit, EM_GETSEL, reinterpret_cast<WPARAM>(&first), reinterpret_cast<LPARAM>(&last));
            require(children(host) == peers && native_text(edit) == L"Keep native text" && first == 2 && last == 7,
                "Focus paint and theme changes preserve native peers, text and selection");
        } catch (...) { error = std::current_exception(); }
        window.close();
        return true;
    });
    require(SetTimer(host, 1, fixture == FocusFixture::all ? 90000 : 45000,
        [](HWND hwnd, UINT, UINT_PTR, DWORD) { PostMessageW(hwnd, WM_CLOSE, 0, 0); }) != 0,
        "Bound the nonactivating focus fixture");
    require(PostMessageW(button_peer, WM_KEYDOWN, VK_SHIFT, 0) != FALSE, "Queue fixture-local keyboard modality");
    const auto result = application.run();
    if (error) std::rethrow_exception(error);
    require(ran && result == 0 && window.error().empty(), "Nonactivating focus adapter checks complete");
    std::cout << "PASS selected focus adapter checks and native peer retention\n";
}

void button_alignment_contracts() {
    if (Palette::system(ThemeMode::light).high_contrast) {
        std::cout << "SKIP authored-color Button alignment pixels under system high contrast\n";
        return;
    }
    Window window({window_title, {360, 180}, ThemeMode::light});
    auto root = std::make_shared<Stack>(Axis::horizontal);
    auto label = std::make_shared<Label>(L"8");
    PartStyleValues text;
    text.foreground = ThemeColor{0x00ff00};
    text.horizontal_alignment = StyleAlignment::center;
    text.vertical_alignment = StyleAlignment::center;
    label->set_control_style_values(StylePart::root, text);
    label->set_auto_size(false);
    label->set_preferred_size({36, 36});
    label->set_maximum_size({36, 36});
    root->add(label);
    std::array<std::shared_ptr<Button>, 2> buttons;
    for (std::size_t i = 0; i < buttons.size(); ++i) {
        auto button = std::make_shared<Button>(L"8");
        button->set_auto_size(false);
        button->set_preferred_size({36, 36});
        button->set_maximum_size({36, 36});
        ButtonStyleValues values;
        values.background = ThemeColor{0x242424};
        values.foreground = text.foreground;
        values.border_brush = ThemeColor{0xa0a0a0};
        values.border_thickness = i == 0 ? Insets{1, 1, 1, 1} : Insets{};
        values.padding = i == 0 ? Insets{} : Insets{2, 2, 2, 2};
        values.corner_radius = 0.0f;
        button->set_style(ButtonStyle::create(values));
        buttons[i] = button;
        root->add(button);
    }
    window.set_content(root);
    StartFixture start(window, [&](HWND host) {
        for (const auto visual : {VisualStyle::classic, VisualStyle::winui}) {
            window.set_visual_style(visual);
            for (const auto mode : {ThemeMode::light, ThemeMode::dark}) {
                window.set_theme(mode);
                for (const bool enabled : {true, false}) {
                    for (const auto& button : buttons) button->set_enabled(enabled);
                    flush(host);
                    std::vector<RECT> areas;
                    for (const auto peer : children(host)) if (native_text(peer) == L"8") {
                        RECT bounds{};
                        require(GetWindowRect(peer, &bounds) != FALSE, "Read alignment peer bounds");
                        MapWindowPoints(nullptr, host, reinterpret_cast<POINT*>(&bounds), 2);
                        areas.push_back(bounds);
                    }
                    require(areas.size() == 3, "Alignment fixture retains one label and two buttons");
                    std::sort(areas.begin(), areas.end(), [](const RECT& a, const RECT& b) { return a.left < b.left; });
                    const auto image = owned_window_capture::capture(host);
                    std::array<double, 2> reference{};
                    for (std::size_t i = 0; i < areas.size(); ++i) {
                        const auto area = areas[i];
                        require(area.left >= 0 && area.top >= 0 && area.right <= image.width && area.bottom <= image.height,
                            "Alignment peer lies inside the captured client");
                        const auto extent = MulDiv(36, GetDpiForWindow(host), 96);
                        require(area.right - area.left == extent && area.bottom - area.top == extent,
                            "Alignment fixture uses actual 36-DIP square cells");
                        RECT ink{area.right, area.bottom, area.left, area.top};
                        unsigned count{};
                        for (auto y = area.top; y < area.bottom; ++y) for (auto x = area.left; x < area.right; ++x) {
                            const auto pixel = image.data[static_cast<std::size_t>(y) * image.width + x] & 0xffffff;
                            if (((pixel >> 8) & 255) < 160 || (pixel & 255) > 80 || (pixel >> 16) > 80) continue;
                            ink.left = std::min(ink.left, x); ink.right = std::max(ink.right, x);
                            ink.top = std::min(ink.top, y); ink.bottom = std::max(ink.bottom, y);
                            ++count;
                        }
                        require(count > 5, "Every alignment peer paints visible number glyphs");
                        const std::array<double, 2> offset{
                            (ink.left + ink.right + 1 - area.left - area.right) / 2.0,
                            (ink.top + ink.bottom + 1 - area.top - area.bottom) / 2.0};
                        if (i == 0) reference = offset;
                        else {
                            const auto tolerance = GetDpiForWindow(host) / 96.0;
                            if (std::abs(offset[0] - reference[0]) > tolerance ||
                                std::abs(offset[1] - reference[1]) > tolerance) {
                                std::cerr << "Button alignment offset: " << offset[0] << ',' << offset[1]
                                    << "; centered label: " << reference[0] << ',' << reference[1] << '\n';
                                throw std::runtime_error("Covered and cleared button glyphs align with centered coordinate text");
                            }
                        }
                    }
                }
            }
        }
    });
    const auto result = Application::run(window);
    if (start.error) std::rethrow_exception(start.error);
    require(start.ran && result == 0 && window.error().empty(), "Native button alignment completes");
    std::cout << "PASS native Button and coordinate text alignment in both themes and visual styles\n";
}

void toggle_native_contracts() {
    Window window({window_title, {420, 280}, ThemeMode::light});
    auto root = std::make_shared<Stack>(Axis::vertical);
    auto toggle = std::make_shared<Toggle>(L"Styled Toggle");
    auto content = std::make_shared<Stack>(Axis::vertical); content->add(toggle);
    auto ancestor = std::make_shared<Expander>(L"Disabled ancestor", content);
    ancestor->set_expanded(true);
    auto input = std::make_shared<TextInput>(L"Retained input");
    input->set_text(L"Keep native input");
    root->add(ancestor); root->add(input);
    PartStyleValues indicator; indicator.size = 80.0f;
    PartStyleValues surface; surface.background = ThemeColor{0x134679, 0x975431};
    PartStyleValues disabled; disabled.foreground = ThemeColor{0x987654};
    auto style = ControlStyle::create(StyleTarget::toggle, {{StylePart::indicator, indicator}, {StylePart::root, surface}},
        {{StylePart::root, style_states::disabled, disabled}});
    toggle->set_style(style);
    window.set_content(root);
    StartFixture start(window, [&](HWND host) {
        flush(host);
        const auto peers = children(host);
        HWND toggle_peer{}, edit{};
        for (const auto hwnd : peers) {
            if (native_text(hwnd) == toggle->name()) toggle_peer = hwnd;
            wchar_t cls[80]{}; GetClassNameW(hwnd, cls, 80);
            if (_wcsicmp(cls, L"EDIT") == 0) edit = hwnd;
        }
        require(toggle_peer && edit, "Toggle and native editor retain actual peers");
        require(toggle->bounds().height >= 80, "Native layout reserves the authored outer indicator size");
        const auto verify_pixels = [&](ThemeMode mode) {
            window.set_theme(mode); flush(host);
            auto image = owned_window_capture::capture(host);
            Pixels pixels{image.width, image.height, std::move(image.data)};
            RECT bounds{}; require(GetWindowRect(toggle_peer, &bounds) != FALSE, "Read live Toggle bounds");
            MapWindowPoints(nullptr, host, reinterpret_cast<POINT*>(&bounds), 2);
            const int margin = MulDiv(6, GetDpiForWindow(host), 96);
            if (!Palette::system(mode).high_contrast)
                pixels.expect(bounds.right - margin, bounds.bottom - margin, surface.background->resolve(mode),
                    "The actual window adapter paints the Toggle's authored root surface");
        };
        verify_pixels(ThemeMode::light); verify_pixels(ThemeMode::dark);
        window.set_theme(ThemeMode::light); flush(host);
        SendMessageW(edit, EM_SETSEL, 2, 8);
        toggle->set_style(nullptr); flush(host);
        require(children(host) == peers, "Removing a style does not replace peers or create decorative peers");
        toggle->set_style(style); flush(host);
        require(children(host) == peers, "Applying named parts keeps one native Toggle peer");
        SendMessageW(toggle_peer, WM_SETFOCUS, 0, 0);
        SendMessageW(toggle_peer, WM_KEYDOWN, VK_SPACE, 0);
        SendMessageW(toggle_peer, WM_KEYUP, VK_SPACE, 0);
        require(toggle->checked(), "Native Space key toggles the whole styled control");
        flush(host);
        std::atomic<bool> done{};
        std::exception_ptr automation_error;
        std::thread automation([&] {
            const auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            try {
                success(initialized, "Initialize Toggle UIA client");
                Microsoft::WRL::ComPtr<IUIAutomation> client;
                success(CoCreateInstance(CLSID_CUIAutomation8, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&client)), "Create Toggle UIA client");
                require(client != nullptr, "UIA client exists");
                Microsoft::WRL::ComPtr<IUIAutomation2> bounded_client;
                success(client.As(&bounded_client), "Configure bounded UIA calls");
                success(bounded_client->put_ConnectionTimeout(5000), "Bound UIA connection time");
                success(bounded_client->put_TransactionTimeout(5000), "Bound UIA transaction time");
                Microsoft::WRL::ComPtr<IUIAutomationElement> element;
                success(client->ElementFromHandle(toggle_peer, &element), "Find styled Toggle UIA element");
                require(element != nullptr, "Toggle HWND host exists in UIA");
                VARIANT type{}; type.vt = VT_I4; type.lVal = UIA_CheckBoxControlTypeId;
                Microsoft::WRL::ComPtr<IUIAutomationCondition> condition;
                success(client->CreatePropertyCondition(UIA_ControlTypePropertyId, type, &condition), "Find Toggle semantic role");
                Microsoft::WRL::ComPtr<IUIAutomationElement> semantic;
                success(element->FindFirst(TreeScope_Subtree, condition.Get(), &semantic), "Find semantic Toggle beneath its HWND host");
                require(semantic != nullptr, "Styled Toggle exposes its semantic checkbox element");
                Microsoft::WRL::ComPtr<IUIAutomationTogglePattern> pattern;
                success(semantic->GetCurrentPatternAs(UIA_TogglePatternId, IID_PPV_ARGS(&pattern)), "Styled Toggle keeps the Toggle pattern");
                require(pattern != nullptr, "Semantic Toggle exposes a non-null Toggle pattern");
                RECT accessible{};
                success(semantic->get_CurrentBoundingRectangle(&accessible), "Read styled Toggle accessible bounds");
                require(accessible.bottom - accessible.top >= 80, "UIA bounds include the styled indicator metric");
                ToggleState state{};
                success(pattern->get_CurrentToggleState(&state), "Read styled Toggle state");
                require(state == ToggleState_On, "UIA exposes the model checked state");
                success(pattern->Toggle(), "UIA activates the existing styled Toggle");
            } catch (...) { automation_error = std::current_exception(); }
            if (SUCCEEDED(initialized)) CoUninitialize();
            done = true;
        });
        while (!done) pump_for(10);
        automation.join();
        if (automation_error) std::rethrow_exception(automation_error);
        pump_for(20);
        require(!toggle->checked(), "UIA toggles the original model");
        ancestor->set_enabled(false); flush(host); flush(host);
        require(toggle->enabled() && !IsWindowEnabled(toggle_peer) &&
            toggle->effective_style_values(StylePart::label)->foreground == disabled.foreground,
            "Disabled ancestor selects generic disabled style without changing local enabled");
        ancestor->set_enabled(true); flush(host);
        auto dialog = std::make_shared<ContentDialog>(L"Modal", std::make_shared<Label>(L"Modal content"));
        window.show_dialog(dialog, *toggle); flush(host); flush(host);
        require(toggle->enabled() && !IsWindowEnabled(toggle_peer) &&
            toggle->effective_style_values(StylePart::label)->foreground == disabled.foreground,
            "Modal context selects generic disabled style");
        dialog->cancel(); flush(host); flush(host);
        require(IsWindowEnabled(toggle_peer), "Closing the modal restores enabled presentation");
        toggle->set_style(nullptr); flush(host);
        require(!toggle->effective_style_values(StylePart::root), "Clearing style restores the default path");
        require(std::find(peers.begin(), peers.end(), toggle_peer) != peers.end() && IsWindow(toggle_peer),
            "Style changes preserve the native Toggle identity");
        DWORD first{}, last{};
        SendMessageW(edit, EM_GETSEL, reinterpret_cast<WPARAM>(&first), reinterpret_cast<LPARAM>(&last));
        require(first == 2 && last == 8 && native_text(edit) == L"Keep native input", "Toggle styles preserve native editor selection and text");
    });
    const auto result = Application::run(window);
    if (start.error) std::rethrow_exception(start.error);
    require(start.ran && result == 0 && window.error().empty(), "Native Toggle integration completes");
    std::cout << "PASS Toggle native metrics, keyboard, UIA, ancestor/modal context, editor retention\n";
}
#endif
void native_contracts(bool measure, int repetition) {
    require(Drawing::live_targets() == 0, "Native fixture begins without retained render targets");
#ifndef XUI_STYLING_BASELINE
    std::weak_ptr<const ButtonStyle> released_style;
#endif
    HWND closed_host{}, closed_edit{}, closed_button{};
    {
        Window window({window_title, {420, 220}, ThemeMode::light});
        auto root = std::make_shared<Stack>(Axis::vertical);
        auto button = std::make_shared<Button>(L"Style pixels MMMM");
        button->set_auto_size(false);
        button->set_preferred_size({280, 56});
        auto input = std::make_shared<TextInput>(L"Retained native input");
        input->set_text(L"Stable native text and selection");
        root->add(button); root->add(input);
        window.set_content(root);
        StartFixture start(window, [&](HWND host) {
            closed_host = host;
            flush(host);
            const auto peers = children(host);
            HWND edit{}, button_peer{};
            for (const auto hwnd : peers) {
                wchar_t cls[80]{};
                require(GetClassNameW(hwnd, cls, 80) != 0, "Read native peer class");
                if (_wcsicmp(cls, L"EDIT") == 0) edit = hwnd;
                if (native_text(hwnd) == button->name()) button_peer = hwnd;
            }
            require(edit && button_peer && edit != button_peer, "Fixture contains actual EDIT and Button peers");
            closed_edit = edit; closed_button = button_peer;
            SendMessageW(edit, EM_SETSEL, 2, 12);
            const auto verify = [&] {
                require(children(host) == peers, "Live style application/clear preserves every native HWND");
                require(Drawing::live_targets() == 1, "Buttons share exactly one root render target");
                require(IsWindow(edit) && native_text(edit) == input->text() &&
                    input->text() == L"Stable native text and selection", "Style/theme changes preserve real EDIT text");
                DWORD first{}, last{};
                SendMessageW(edit, EM_GETSEL, reinterpret_cast<WPARAM>(&first), reinterpret_cast<LPARAM>(&last));
                require(first == 2 && last == 12 && input->selection() == TextInput::Selection{2, 12},
                    "Style/theme changes preserve native and model selection without keyboard focus");
            };
#ifndef XUI_STYLING_BASELINE
            auto scope = ResourceScope::create({{"Fill", ThemeColor{0xe13457, 0x2496c3}},
                {"Alias", std::string{"Fill"}}});
            std::weak_ptr<const ResourceScope> released_scope = scope;
            auto values = authored_values();
            values.background = scope->color("Alias");
            auto style = ButtonStyle::create(values);
            released_style = style;
            scope.reset();
            require(released_scope.expired(), "Resolved color values do not retain their source ResourceScope");
            ObservePaints observed(host);
            ButtonStyleValues local;
            local.background = ThemeColor{0x2abb57};
#else
            ObservePaints observed(host);
#endif
            // Capture service startup is not part of the repeated Window lifetime resource contract.
            if (repetition == 0) {
#ifdef XUI_STYLING_BASELINE
                for (int i = 0; i < 5; ++i) {
                    window.set_theme(i == 3 ? ThemeMode::dark : ThemeMode::light);
                    flush(host);
                    (void)owned_window_capture::capture(host);
                    verify();
                }
#else
                const auto capture = [&] {
                    flush(host);
                    trace_resources("before-capture", repetition);
                    auto image = owned_window_capture::capture(host);
                    trace_resources("after-capture", repetition);
                    return Pixels{image.width, image.height, std::move(image.data)};
                };
                RECT bounds{};
                require(GetWindowRect(button_peer, &bounds) != FALSE, "Read live Button bounds");
                MapWindowPoints(nullptr, host, reinterpret_cast<POINT*>(&bounds), 2);
                const auto dpi = GetDpiForWindow(host) / 96.0f;
                const int x = bounds.left + static_cast<int>(std::lround(10 * dpi));
                const int y = bounds.top + static_cast<int>(std::lround(10 * dpi));
                const auto baseline = capture();
                button->set_style(style);
                auto styled = capture();
                if (!Palette::system(ThemeMode::light).high_contrast)
                    styled.expect(x, y, values.background->light, "A live application Button paints its shared style");
                verify();
                button->set_style_values(local);
                auto overridden = capture();
                if (!Palette::system(ThemeMode::light).high_contrast)
                    overridden.expect(x, y, 0x2abb57, "A live application Button paints local overrides");
                button->set_style_values({});
                window.set_theme(ThemeMode::dark);
                auto dark = capture();
                if (!Palette::system(ThemeMode::dark).high_contrast)
                    dark.expect(x, y, values.background->dark, "Theme changes repaint a retained style with its dark resource color");
                verify();
                window.set_theme(ThemeMode::light);
                button->set_style(nullptr);
                const auto cleared = capture();
                cleared.expect(x, y, baseline.at(x, y), "Clearing a live Button style restores default face paint");
                verify();
#endif
            }

            const auto cycle = [&](int index) {
#ifndef XUI_STYLING_BASELINE
                auto cycle_style = ButtonStyle::create(values);
                std::weak_ptr<const ButtonStyle> retired = cycle_style;
                button->set_style(cycle_style);
#endif
                window.set_visual_style(index % 2 ? VisualStyle::winui : VisualStyle::classic);
                window.set_theme(ThemeMode::dark);
                flush(host); verify();
#ifndef XUI_STYLING_BASELINE
                local.padding = Insets{16, 7, 18, 8};
                button->set_style_values(local);
#endif
                window.set_theme(ThemeMode::high_contrast);
                flush(host); verify();
#ifndef XUI_STYLING_BASELINE
                button->set_style_values({});
                button->set_style(nullptr);
#endif
                window.set_theme(ThemeMode::light);
                window.set_visual_style(VisualStyle::classic);
                flush(host); verify();
#ifndef XUI_STYLING_BASELINE
                cycle_style.reset();
                require(retired.expired(), "Apply/clear cycles do not retain retired immutable style definitions");
#endif
            };
            for (int i = 0; i < 16; ++i) cycle(i);
            pump_for(100);
            const auto warmed = Resources::read();
            trace_resources("warmed", repetition);
            auto last_user = warmed.user;
            for (int i = 0; i < 128; ++i) {
                cycle(i);
                if (trace_resources_enabled) {
                    const auto user = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
                    if (user != last_user) trace_resources("cycles", i + 1);
                    last_user = user;
                }
            }
            pump_for(100);
            const auto after = Resources::read();
            trace_resources("after", repetition);
            after.retained_from(warmed);
            require(presents > 0 && paints > 0, "Paint observers measured real root and native WM_PAINT work");
            pump_for(100);
            const auto idle_presents = presents, idle_paints = paints;
            pump_for(180);
            require(presents == idle_presents && paints == idle_paints, "Settled style/theme state produces no idle paints");
#ifndef XUI_STYLING_BASELINE
            if (measure) benchmark(host, *button, style);
#else
            require(!measure, "The pristine baseline supports lifetime checks, not authored-style benchmarks");
#endif
            DrawingTestAccess::lose_target();
            flush(host); flush(host);
            verify();
#ifndef XUI_STYLING_BASELINE
            button->set_style(style);
            flush(host);
            button->set_style(nullptr);
            style.reset();
            require(released_style.expired(), "Clearing style releases its last shared definition");
#endif
            verify();
            std::cout << "PASS native styling cycle=" << repetition << " retained_hwnds=" << peers.size()
                << " targets=" << Drawing::live_targets() << " style_theme_cycles=144 idle_paints=0\n";
        });
        const auto result = Application::run(window);
        if (start.error) std::rethrow_exception(start.error);
        if (!start.ran || result != 0 || !window.error().empty())
            std::wcerr << L"Native fixture ran=" << start.ran << L" exit=" << result << L" error=" << window.error() << L'\n';
        require(start.ran && result == 0 && window.error().empty(), "Native styling fixture completes without application errors");
    }
    require(!IsWindow(closed_host) && !IsWindow(closed_edit) && !IsWindow(closed_button),
        "Closing destroys the owner, EDIT and Button HWNDs");
#ifndef XUI_STYLING_BASELINE
    require(released_style.expired(), "Window teardown releases styles");
#endif
    require(Drawing::live_targets() == 0,
        "Window teardown releases styles and all render targets");
}
}

int main(int argc, char** argv) {
    try {
        bool measure{}, lower{}, toggle_only{}, alignment_only{};
        std::optional<FocusFixture> focus_fixture;
        for (int i = 1; i < argc; ++i) {
            const std::string_view argument{argv[i]};
            if (argument == "--benchmark" && !measure) measure = true;
            else if (argument == "--trace-resources" && !trace_resources_enabled) trace_resources_enabled = true;
            else if (argument == "--styled-first" && !benchmark_styled_first) benchmark_styled_first = true;
            else if (argument == "--lower-level" && !lower) lower = true;
            else if (argument == "--switch-ring-only" || argument == "--next-controls-only" || argument == "--button-focus-only") {
#ifndef XUI_STYLING_BASELINE
                SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
                success(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED), "Initialize control pixel fixture COM");
                struct Com { ~Com() { CoUninitialize(); } } com;
                if (argument == "--button-focus-only") button_focus_pixel_contracts();
                else if (argument == "--next-controls-only") next_controls_pixel_contracts();
                else switch_ring_pixel_contracts();
                return 0;
#else
                throw std::runtime_error("Control pixel checks are not available in the pristine baseline");
#endif
            }
            else if (argument == "--toggle-only" && !toggle_only) toggle_only = true;
            else if (argument == "--alignment-only" && !alignment_only) alignment_only = true;
            else if (argument == "--button-focus-window-only" && !focus_fixture) focus_fixture = FocusFixture::all;
            else if (argument == "--focus-buttons-only" && !focus_fixture) focus_fixture = FocusFixture::buttons;
            else if (argument == "--focus-choices-only" && !focus_fixture) focus_fixture = FocusFixture::choices;
            else if (argument == "--focus-selectors-only" && !focus_fixture) focus_fixture = FocusFixture::selectors;
            else if (argument == "--expander-interaction-only" && !focus_fixture) focus_fixture = FocusFixture::expanders;
            else throw std::runtime_error("Usage: xui_styling_window_tests [--button-focus-only] [--button-focus-window-only | --focus-buttons-only | --focus-choices-only | --expander-interaction-only] [--switch-ring-only] [--next-controls-only] [--toggle-only] [--alignment-only] [--benchmark] [--styled-first] [--lower-level] [--trace-resources]");
        }
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        success(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED), "Initialize styling fixture COM");
        struct Com { ~Com() { CoUninitialize(); } } com;
#ifndef XUI_STYLING_BASELINE
        if (focus_fixture) {
            button_focus_native_contracts(*focus_fixture);
            return 0;
        }
        if (alignment_only) {
            button_alignment_contracts();
            return 0;
        }
        if (toggle_only) {
            toggle_pixel_contracts();
            switch_ring_pixel_contracts();
            toggle_native_contracts();
            return 0;
        }
        pixel_contracts();
        button_focus_pixel_contracts();
        toggle_pixel_contracts();
        switch_ring_pixel_contracts();
        if (lower) { lower_level_benchmarks(); return 0; }
        button_alignment_contracts();
        button_focus_native_contracts();
        toggle_native_contracts();
#else
        require(!lower && !measure && !focus_fixture, "Pristine fixture only measures unchanged Window/editor/theme lifetime");
        std::cout << "BASELINE pristine native library; authored style operations omitted; capture/theme/state checks retained.\n";
#endif
        native_contracts(measure, 0);
        const auto closed_resources = Resources::read();
        for (int i = 1; i < 3; ++i) {
            native_contracts(false, i);
            Resources::read().retained_from(closed_resources);
        }
        std::cout << "PASS styling_window_tests\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL styling_window_tests: " << error.what() << '\n';
        return 1;
    }
}
