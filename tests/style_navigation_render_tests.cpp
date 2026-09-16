#include "../src/drawing.hpp"
#include "xui/application.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace xui {
struct DrawingTestAccess {
    static void software_target(Drawing& drawing, HWND window) {
        const auto properties = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
            96, 96, D2D1_RENDER_TARGET_USAGE_GDI_COMPATIBLE);
        if (FAILED(drawing.factory_->CreateHwndRenderTarget(properties,
            D2D1::HwndRenderTargetProperties(window, D2D1::SizeU(240, 80)), &drawing.target_)))
            throw std::runtime_error("Create navigation render target");
        ++Drawing::live_targets_;
        if (FAILED(drawing.target_->CreateSolidColorBrush(D2D1::ColorF(0), &drawing.brush_)))
            throw std::runtime_error("Create navigation render brush");
    }
    static std::vector<uint32_t> pixels(Drawing& drawing) {
        Microsoft::WRL::ComPtr<ID2D1GdiInteropRenderTarget> interop;
        if (FAILED(drawing.target_.As(&interop))) throw std::runtime_error("Read navigation render target");
        HDC dc{};
        if (FAILED(interop->GetDC(D2D1_DC_INITIALIZE_MODE_COPY, &dc))) throw std::runtime_error("Read navigation pixels");
        std::vector<uint32_t> result(240 * 80);
        for (int y = 0; y < 80; ++y) for (int x = 0; x < 240; ++x) {
            const auto color = GetPixel(dc, x, y);
            result[y * 240 + x] = (uint32_t(GetRValue(color)) << 16) | (uint32_t(GetGValue(color)) << 8) | GetBValue(color);
        }
        const RECT unchanged{};
        if (FAILED(interop->ReleaseDC(&unchanged))) throw std::runtime_error("Release navigation pixel DC");
        return result;
    }
};
}
namespace {
using namespace xui;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
struct Fixture {
    HWND window{};
    Drawing drawing;
    Fixture() {
        window = CreateWindowExW(WS_EX_NOACTIVATE, L"STATIC", L"Navigation style fixture",
            WS_POPUP, 0, 0, 240, 80, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        require(window != nullptr, "Create hidden navigation fixture");
        drawing.initialize();
        DrawingTestAccess::software_target(drawing, window);
    }
    ~Fixture() { drawing.release(); if (window) DestroyWindow(window); }
    template<class F> std::vector<uint32_t> render(F paint) {
        require(drawing.begin(window, 96, D2D1::ColorF(0x101010)), "Begin navigation paint");
        paint();
        auto result = DrawingTestAccess::pixels(drawing);
        require(drawing.end(), "Finish navigation paint");
        return result;
    }
};
std::size_t count(const std::vector<uint32_t>& pixels, uint32_t color) {
    return std::count(pixels.begin(), pixels.end(), color);
}
void tabs(Fixture& fixture) {
    TabStrip tabs;
    tabs.set_tabs({{1, L"First"}, {2, L"Second"}}, 1);
    tabs.on_close([](uint64_t) {});
    tabs.arrange({0, 0, 240, 60});
    auto palette = Palette::system(ThemeMode::dark);
    palette.high_contrast = false;
    const auto baseline = fixture.render([&] { fixture.drawing.tab_strip(tabs, tabs.bounds(), palette, true, false, false); });
    PartStyleValues tab, selected, hovered, close, marker;
    tab.background = ThemeColor{0x223344}; tab.corner_radius = 0; tab.border_thickness = Insets{};
    selected.background = ThemeColor{0x345678};
    hovered.background = ThemeColor{0x456789};
    close.background = ThemeColor{0xabcdef}; close.corner_radius = 0;
    marker.background = ThemeColor{0x12ab34}; marker.thickness = 4;
    tabs.set_control_style(ControlStyle::create(StyleTarget::tab_strip,
        {{StylePart::tab, tab}, {StylePart::selection, marker}},
        {{StylePart::tab, style_states::selected, selected}, {StylePart::tab, style_states::hovered, hovered},
         {StylePart::close_action, style_states::hovered, close}}));
    const auto second = tabs.close_bounds(1);
    const Point pointer{second.x + second.width / 2, second.y + second.height / 2};
    const auto painted = fixture.render([&] { fixture.drawing.tab_strip(tabs, tabs.bounds(), palette, true, false, false, pointer); });
    require(count(painted, 0x345678) > 100 && count(painted, 0x456789) > 100,
        "Selected and hovered tabs paint distinct model states");
    require(count(painted, 0xabcdef) > 50 && count(painted, 0x12ab34) > 100,
        "Close-hover and selection parts paint independently");
    palette.high_contrast = true;
    const auto contrast = fixture.render([&] { fixture.drawing.tab_strip(tabs, tabs.bounds(), palette, true, false, true, pointer); });
    require(count(contrast, 0x345678) == 0 && count(contrast, 0xabcdef) == 0,
        "High contrast suppresses authored tab and close colors");
    palette.high_contrast = false;
    tabs.set_control_style(nullptr);
    const auto restored = fixture.render([&] { fixture.drawing.tab_strip(tabs, tabs.bounds(), palette, true, false, false); });
    require(baseline == restored, "Removing TabStrip styling restores default pixels");
}
void captions(Fixture& fixture) {
    auto palette = Palette::system(ThemeMode::dark);
    palette.high_contrast = false;
    PartStyleValues parent;
    parent.background = ThemeColor{0x123456}; parent.foreground = ThemeColor{0xeeeeee};
    parent.corner_radius = 0;
    ButtonStyleValues child;
    child.background = ThemeColor{0xabcdef};
    const auto painted = fixture.render([&] {
        fixture.drawing.caption_button({10, 10, 100, 40}, ButtonIcon::maximize, palette,
            false, true, true, false, true, &parent, &child);
    });
    require(count(painted, 0xabcdef) > 500 && count(painted, 0x123456) == 0,
        "Caption-local Button style overrides its parent part");
    Button caption(L"Caption");
    caption.set_style_values(child);
    PartStyleValues own;
    own.background = ThemeColor{0x56789a};
    caption.set_control_style_values(StylePart::root, own);
    const auto composed = merge_part_values(parent, caption.surface_style_values());
    const auto own_painted = fixture.render([&] {
        fixture.drawing.caption_button({10, 10, 100, 40}, ButtonIcon::maximize, palette,
            false, true, true, false, true, &composed);
    });
    require(count(own_painted, 0x56789a) > 500 && count(own_painted, 0xabcdef) == 0,
        "Caption composition keeps generic child locals above legacy Button values");
    palette.high_contrast = true;
    const auto contrast = fixture.render([&] {
        fixture.drawing.caption_button({10, 10, 100, 40}, ButtonIcon::close, palette,
            true, true, true, false, true, &parent, &child);
    });
    require(count(contrast, 0xabcdef) == 0, "High contrast keeps caption system colors and focus");
}
void tooltip_attachment() {
    Window window;
    require(!window.tooltip_style() && !window.effective_tooltip_style_values(StylePart::root),
        "An unstyled Window has no tooltip attachment");
    PartStyleValues root, shown, local;
    root.foreground = ThemeColor{0x112233};
    root.padding = Insets{5, 6, 7, 8};
    shown.foreground = ThemeColor{0xff0000};
    const auto style = ControlStyle::create(StyleTarget::tooltip, {{StylePart::root, root}},
        {{StylePart::text, style_states::open, shown}});
    window.set_tooltip_style(style);
    require(window.tooltip_style() == style &&
        window.effective_tooltip_style_values(StylePart::text)->foreground == root.foreground,
        "Assigning a tooltip style preserves hidden state and inherits root text color");
    local.foreground = ThemeColor{0xabcdef};
    window.set_tooltip_style_values(StylePart::text, local);
    require(window.tooltip_style_values(StylePart::text).foreground == local.foreground &&
        window.effective_tooltip_style_values(StylePart::text)->foreground == local.foreground,
        "Tooltip locals override the shared definition");
    bool rejected{};
    try { window.set_tooltip_style(ControlStyle::create(StyleTarget::title_bar, {}, {})); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected && window.tooltip_style() == style, "Wrong tooltip target fails without replacing the style");
    rejected = false;
    try { window.set_tooltip_style_values(StylePart::tab, {}); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "Clearing an unsupported tooltip part is still an error");
    window.set_tooltip_style(nullptr);
    require(!window.tooltip_style() &&
        window.effective_tooltip_style_values(StylePart::text)->foreground == local.foreground,
        "Removing a tooltip definition preserves local values");
    window.set_tooltip_style_values(StylePart::text, {});
    require(!window.effective_tooltip_style_values(StylePart::root) &&
        window.tooltip_style_values(StylePart::text).empty(), "Clearing the final local releases the tooltip attachment");
    window.set_tooltip_style_values(StylePart::root, {});
    require(!window.effective_tooltip_style_values(StylePart::root), "Clearing an unstyled tooltip does not allocate an attachment");
}
}
int main() {
    try { Fixture fixture; tabs(fixture); captions(fixture); tooltip_attachment(); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    std::cout << "Navigation styling software rendering tests passed\n";
}
