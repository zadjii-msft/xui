#include "../src/drawing.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace xui {
struct DrawingTestAccess {
    static void software_target(Drawing& drawing, HWND window) {
        const auto properties = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
            96, 96, D2D1_RENDER_TARGET_USAGE_GDI_COMPATIBLE);
        if (FAILED(drawing.factory_->CreateHwndRenderTarget(properties,
            D2D1::HwndRenderTargetProperties(window, D2D1::SizeU(200, 100)), &drawing.target_)))
            throw std::runtime_error("Create basic style software target");
        ++Drawing::live_targets_;
        if (FAILED(drawing.target_->CreateSolidColorBrush(D2D1::ColorF(0), &drawing.brush_)))
            throw std::runtime_error("Create basic style brush");
        drawing.target_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_ALIASED);
    }
    static std::vector<uint32_t> pixels(Drawing& drawing) {
        Microsoft::WRL::ComPtr<ID2D1GdiInteropRenderTarget> interop;
        if (FAILED(drawing.target_.As(&interop))) throw std::runtime_error("Read basic style target");
        HDC dc{};
        if (FAILED(interop->GetDC(D2D1_DC_INITIALIZE_MODE_COPY, &dc))) throw std::runtime_error("Read basic style pixels");
        std::vector<uint32_t> result(200 * 100);
        for (int y = 0; y < 100; ++y) for (int x = 0; x < 200; ++x) {
            const auto color = GetPixel(dc, x, y);
            result[y * 200 + x] = (uint32_t(GetRValue(color)) << 16) | (uint32_t(GetGValue(color)) << 8) | GetBValue(color);
        }
        const RECT unchanged{};
        if (FAILED(interop->ReleaseDC(&unchanged))) throw std::runtime_error("Release basic style pixel DC");
        return result;
    }
    static std::size_t formats(const Drawing& drawing) { return drawing.styled_formats_.size(); }
    static std::size_t layouts(const Drawing& drawing) { return drawing.styled_layouts_.size(); }
};
}
namespace {
using namespace xui;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
struct Fixture {
    HWND window{};
    Drawing drawing;
    Fixture() {
        window = CreateWindowExW(WS_EX_NOACTIVATE, L"STATIC", L"Basic text style software fixture",
            WS_POPUP, 0, 0, 200, 100, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        require(window != nullptr, "Create hidden basic text fixture");
        drawing.initialize();
        DrawingTestAccess::software_target(drawing, window);
    }
    ~Fixture() { drawing.release(); if (window) DestroyWindow(window); }
    template<class F> std::vector<uint32_t> render(F paint) {
        require(drawing.begin(window, 96, D2D1::ColorF(0x101010)), "Begin basic style paint");
        paint();
        auto pixels = DrawingTestAccess::pixels(drawing);
        require(drawing.end(), "Finish basic style paint");
        return pixels;
    }
};
std::size_t count(const std::vector<uint32_t>& pixels, uint32_t value) {
    return std::count(pixels.begin(), pixels.end(), value);
}
void typography_cache(Fixture& fixture) {
    auto& drawing = fixture.drawing;
    const auto baseline = fixture.render([&] { drawing.text(L"Default path", {10, 10, 180, 50}, D2D1::ColorF(0xffffff)); });
    PartStyleValues colors;
    colors.foreground = ThemeColor{0x123456};
    const auto initial_formats = DrawingTestAccess::formats(drawing);
    Size color_metrics;
    const auto first_color = drawing.styled_layout(L"Color only", TextStyle::body, colors, color_metrics);
    colors.foreground = ThemeColor{0x654321}; colors.background = ThemeColor{0x112233};
    const auto second_color = drawing.styled_layout(L"Color only", TextStyle::body, colors, color_metrics);
    require(first_color == second_color && initial_formats == DrawingTestAccess::formats(drawing),
        "Color-only styles retain built-in fonts and do not change typography cache keys");
    PartStyleValues values;
    const auto native_default = Drawing::font_descriptor(values, L"Native default", 17, 500, StyleFontStyle::italic);
    require(std::wstring_view(native_default.family_name()) == L"Native default" && native_default.size == 17 &&
        native_default.weight == 500 && native_default.style == StyleFontStyle::italic,
        "Shared font descriptor preserves caller-owned native defaults");
    values.font_family = make_style_font_family("Segoe UI");
    values.font_size = 16.0f; values.font_weight = 400; values.font_style = StyleFontStyle::normal;
    const auto descriptor = Drawing::font_descriptor(values, L"Native default", 17);
    require(descriptor.authored_family == values.font_family && descriptor.family_name() == values.font_family->name.c_str(),
        "Font descriptor retains immutable family ownership without copying its name");
    Size small_size, large_size;
    const auto first = drawing.styled_layout(L"Metrics", TextStyle::body, values, small_size);
    const auto created = Drawing::created_text_layouts();
    const auto second = drawing.styled_layout(L"Metrics", TextStyle::body, values, large_size);
    require(first == second && Drawing::created_text_layouts() == created, "Identical typography reuses the layout");
    values.font_weight = 700; values.font_style = StyleFontStyle::italic;
    values.horizontal_alignment = StyleAlignment::end; values.vertical_alignment = StyleAlignment::start;
    const auto authored = drawing.styled_layout(L"Metrics", TextStyle::body, values, large_size);
    DWRITE_FONT_WEIGHT weight{};
    DWRITE_FONT_STYLE slant{};
    require(SUCCEEDED(authored->GetFontWeight(0, &weight)) && weight == DWRITE_FONT_WEIGHT_BOLD &&
        SUCCEEDED(authored->GetFontStyle(0, &slant)) && slant == DWRITE_FONT_STYLE_ITALIC,
        "DirectWrite receives authored font weight and style");
    require(authored->GetTextAlignment() == DWRITE_TEXT_ALIGNMENT_TRAILING &&
        authored->GetParagraphAlignment() == DWRITE_PARAGRAPH_ALIGNMENT_NEAR, "DirectWrite receives authored alignment");
    fixture.render([&] { drawing.styled_text(L"Warm row", {0, 0, 180, 50}, D2D1::ColorF(0xffffff), values); });
    const auto formats = DrawingTestAccess::formats(drawing);
    const auto layouts = Drawing::created_text_layouts();
    fixture.render([&] {
        for (unsigned i = 0; i < 100; ++i)
            drawing.styled_text(std::to_wstring(i), {0, 0, 180, 50}, D2D1::ColorF(0xffffff), values);
    });
    require(formats == DrawingTestAccess::formats(drawing) && layouts == Drawing::created_text_layouts(),
        "Virtual row text reuses fonts without allocating a layout per row");
    const auto cached_layouts = DrawingTestAccess::layouts(drawing);
    fixture.render([&] {
        drawing.private_text(L"Synthetic private text", {0, 0, 180, 50}, D2D1::ColorF(0xffffff), values);
    });
    require(cached_layouts == DrawingTestAccess::layouts(drawing) && layouts == Drawing::created_text_layouts(),
        "Private text does not create or retain text layouts");
    values.font_size = 32.0f;
    drawing.styled_layout(L"Metrics", TextStyle::body, values, large_size);
    require(large_size.width > small_size.width && large_size.height > small_size.height, "Authored typography changes actual glyph metrics");
    values.wrapping = true; values.maximum_lines = 1; values.font_size = 16.0f;
    Size one, many;
    drawing.styled_layout(L"one two three four five six", TextStyle::body, values, one, 45);
    values.maximum_lines = 0;
    drawing.styled_layout(L"one two three four five six", TextStyle::body, values, many, 45);
    require(many.height > one.height, "Line limits constrain actual wrapped line metrics");
    for (unsigned i = 0; i < 180; ++i) {
        values.font_size = 8.0f + static_cast<float>(i);
        drawing.styled_layout(std::to_wstring(i), TextStyle::body, values, many);
    }
    require(DrawingTestAccess::formats(drawing) <= 64 && DrawingTestAccess::layouts(drawing) <= 128,
        "Typography format and layout caches remain bounded");
    drawing.set_visual_style(VisualStyle::winui);
    require(DrawingTestAccess::formats(drawing) == 0 && DrawingTestAccess::layouts(drawing) == 0,
        "Visual style change clears stale font and text layouts");
    drawing.set_visual_style(VisualStyle::classic);
    const auto restored = fixture.render([&] { drawing.text(L"Default path", {10, 10, 180, 50}, D2D1::ColorF(0xffffff)); });
    require(baseline == restored, "Authored typography does not mutate built-in unstyled formats or output");
}
void open_icon(Fixture& fixture) {
    Button button(L"Open selected file");
    button.set_icon(ButtonIcon::open);
    for (const auto style : {VisualStyle::classic, VisualStyle::winui}) {
        button.set_name(L"Open selected file");
        fixture.drawing.set_visual_style(style);
        const auto palette = Palette::system(ThemeMode::light, style);
        const auto before = fixture.render([&] {
            fixture.drawing.styled_button(button, {10, 10, 160, 70}, palette, true, false);
        });
        button.set_name(L"Another accessible description");
        const auto after = fixture.render([&] {
            fixture.drawing.styled_button(button, {10, 10, 160, 70}, palette, true, false);
        });
        require(before == after && button.name() == L"Another accessible description",
            "Open remains icon-only while retaining the accessible label");
        const auto glyph = fixture.render([&] {
            fixture.drawing.button_icon({20, 20, 32, 32}, D2D1::ColorF(0xffffff), ButtonIcon::open);
        });
        require(count(glyph, 0xffffff) > 10, "Open paints visible Fluent and Classic fallback strokes");
    }
    fixture.drawing.set_visual_style(VisualStyle::classic);
}
void surfaces_and_text(Fixture& fixture) {
    auto palette = Palette::system(ThemeMode::dark);
    palette.high_contrast = false;
    PartStyleValues values;
    values.background = ThemeColor{0x335577}; values.border_brush = ThemeColor{0xee2244};
    values.border_thickness = Insets{0, 4, 8, 0}; values.corner_radius = 0.0f;
    const auto unequal = fixture.render([&] {
        fixture.drawing.styled_surface({10, 10, 100, 70}, palette, values, palette.surface, palette.border, 0, {});
    });
    require(unequal[11 * 200 + 20] == 0xee2244 && unequal[40 * 200 + 105] == 0xee2244 &&
        unequal[40 * 200 + 11] == 0x335577 && unequal[78 * 200 + 20] == 0x335577,
        "Unequal and zero borders paint their actual edges");
    Label label(L"MMMM");
    label.set_tone(TextTone::error);
    PartStyleValues text; text.foreground = ThemeColor{0x00ff00}; text.font_size = 22.0f;
    text.horizontal_alignment = StyleAlignment::end; text.vertical_alignment = StyleAlignment::end;
    label.set_control_style_values(StylePart::root, text);
    const auto painted = fixture.render([&] { fixture.drawing.styled_label(label, {10, 10, 160, 75}, palette, true); });
    require(count(painted, 0x00ff00) > 20, "Authored Label foreground overrides semantic error tone");
    const auto expected = fixture.render([&] {
        fixture.drawing.styled_text(label.text(), {10, 10, 160, 75}, D2D1::ColorF(0x00ff00), text, label.text_style());
    });
    const auto glyph_bounds = [](const auto& pixels) {
        std::array<std::size_t, 5> result{200, 100, 0, 0, 0};
        for (std::size_t i = 0; i < pixels.size(); ++i) if (pixels[i] == 0x00ff00) {
            result[0] = std::min(result[0], i % 200); result[1] = std::min(result[1], i / 200);
            result[2] = std::max(result[2], i % 200); result[3] = std::max(result[3], i / 200);
            ++result[4];
        }
        return result;
    };
    const auto actual_bounds = glyph_bounds(painted), expected_bounds = glyph_bounds(expected);
    const bool aligned = actual_bounds[0] > 60 && actual_bounds[1] > 45 && actual_bounds == expected_bounds;
    if (!aligned) {
        const auto* effective = label.effective_control_style_values(label.text_part());
        std::cerr << "Label alignment actual=[";
        for (const auto value : actual_bounds) std::cerr << value << ' ';
        std::cerr << "] expected=[";
        for (const auto value : expected_bounds) std::cerr << value << ' ';
        std::cerr << "] (left top right bottom pixel_count), content=[10 10 160 75], "
            << "expected left>60 top>45, resolved font_size=" << (effective ? effective->font_size.value_or(-1.0f) : -1.0f)
            << " horizontal=" << (effective && effective->horizontal_alignment ? static_cast<int>(*effective->horizontal_alignment) : -1)
            << " vertical=" << (effective && effective->vertical_alignment ? static_cast<int>(*effective->vertical_alignment) : -1) << '\n';
    }
    require(aligned, "Authored alignment moves glyphs within the actual content area");
    palette.high_contrast = true;
    const auto system_ink = Drawing::style_foreground(text, palette, palette.text);
    require(system_ink.r == palette.text.r && system_ink.g == palette.text.g && system_ink.b == palette.text.b,
        "Shared foreground helper preserves the caller's system-color fallback in high contrast");
    const auto contrast = fixture.render([&] { fixture.drawing.styled_label(label, {10, 10, 160, 75}, palette, true); });
    require(count(contrast, 0x00ff00) == 0, "High contrast suppresses authored text colors");
    label.set_control_style_values(StylePart::root, {});
    require(!label.has_control_styling(), "Clearing Label styling restores the default branch");
}
void button_variants(Fixture& fixture) {
    auto palette = Palette::system(ThemeMode::dark);
    palette.high_contrast = false;
    Button button(L"Action");
    PartStyleValues root; root.background = ThemeColor{0x223344}; root.foreground = ThemeColor{0xffcc00};
    root.border_thickness = Insets{0, 0, 0, 0}; root.corner_radius = 0.0f;
    PartStyleValues icon; icon.foreground = ThemeColor{0x00ff00}; icon.size = 24.0f;
    PartStyleValues arrow; arrow.foreground = ThemeColor{0xff0000}; arrow.size = 28.0f;
    button.set_control_style(ControlStyle::create(StyleTarget::button,
        {{StylePart::root, root}, {StylePart::icon, icon}, {StylePart::arrow, arrow}}, {}));
    for (auto appearance : {ButtonAppearance::standard, ButtonAppearance::accent, ButtonAppearance::subtle}) {
        button.set_appearance(appearance);
        for (auto behavior : {ButtonBehavior::momentary, ButtonBehavior::repeat, ButtonBehavior::toggle, ButtonBehavior::dropdown}) {
            button.set_behavior(behavior); button.set_checked(behavior == ButtonBehavior::toggle);
            const auto text = fixture.render([&] { fixture.drawing.styled_button(button, {10, 10, 160, 70}, palette, true, true); });
            require(count(text, 0x223344) > 500, "Every Button appearance and behavior paints the authored surface");
        }
    }
    button.set_icon(ButtonIcon::add); button.set_behavior(ButtonBehavior::dropdown);
    const auto parts = fixture.render([&] { fixture.drawing.styled_button(button, {10, 10, 160, 70}, palette, true, false); });
    const auto changed_in_region = [](const auto& first, const auto& second, Rect area) {
        std::array<std::size_t, 2> result{};
        for (std::size_t i = 0; i < first.size(); ++i) if (first[i] != second[i]) {
            const float x = static_cast<float>(i % 200) + 0.5f;
            const float y = static_cast<float>(i / 200) + 0.5f;
            const bool inside = x >= area.x && x < area.x + area.width && y >= area.y && y < area.y + area.height;
            ++result[inside ? 0 : 1];
        }
        return result;
    };
    PartStyleValues alternate_icon, alternate_arrow;
    alternate_icon.foreground = ThemeColor{0x0000ff};
    alternate_arrow.foreground = ThemeColor{0xffff00};
    button.set_control_style_values(StylePart::icon, alternate_icon);
    const auto recolored_icon = fixture.render([&] {
        fixture.drawing.styled_button(button, {10, 10, 160, 70}, palette, true, false);
    });
    button.set_control_style_values(StylePart::icon, {});
    button.set_control_style_values(StylePart::arrow, alternate_arrow);
    const auto recolored_arrow = fixture.render([&] {
        fixture.drawing.styled_button(button, {10, 10, 160, 70}, palette, true, false);
    });
    button.set_control_style_values(StylePart::arrow, {});
    const auto icon_area = button.icon_bounds({10, 10, 160, 70});
    const auto arrow_area = button.dropdown_bounds({10, 10, 160, 70});
    const auto icon_changes = changed_in_region(parts, recolored_icon, icon_area);
    const auto arrow_changes = changed_in_region(parts, recolored_arrow, arrow_area);
    const bool independent_colors = icon_changes[0] > 0 && !icon_changes[1] && arrow_changes[0] > 0 && !arrow_changes[1];
    if (!independent_colors) {
        std::cerr << "Button part colors exact green=" << count(parts, 0x00ff00) << " red=" << count(parts, 0xff0000)
            << "; icon changed inside/outside=" << icon_changes[0] << '/' << icon_changes[1]
            << " bounds=" << icon_area.x << ',' << icon_area.y << ',' << icon_area.width << ',' << icon_area.height
            << "; arrow changed inside/outside=" << arrow_changes[0] << '/' << arrow_changes[1]
            << " bounds=" << arrow_area.x << ',' << arrow_area.y << ',' << arrow_area.width << ',' << arrow_area.height << '\n';
    }
    require(independent_colors, "Button icon and dropdown indicator paint independent styles");
    button.set_icon(ButtonIcon::none); button.set_behavior(ButtonBehavior::momentary);
    const auto step = fixture.render([&] {
        fixture.drawing.styled_button(button, {10, 10, 32, 30}, palette, true, false, {}, 0, true);
    });
    button.set_name(L"Long accessible name for the increment action");
    const auto renamed_step = fixture.render([&] {
        fixture.drawing.styled_button(button, {10, 10, 32, 30}, palette, true, false, {}, 0, true);
    });
    require(step == renamed_step && count(step, 0xffcc00) > 0,
        "Styled numeric child keeps its visible glyph independent of its accessible name");
    Button clear(L"Clear private input");
    clear.set_icon(ButtonIcon::close);
    PartStyleValues inherited;
    inherited.background = ThemeColor{0x557799}; inherited.foreground = ThemeColor{0x00ff00};
    inherited.border_thickness = Insets{}; inherited.corner_radius = 0.0f;
    const auto parent_pixels = fixture.render([&] {
        fixture.drawing.styled_button(clear, {10, 10, 32, 30}, palette, true, false, {}, 0, {}, &inherited);
    });
    require(count(parent_pixels, 0x557799) > 200 && count(parent_pixels, 0x00ff00) > 0 && !clear.has_control_styling(),
        "Private child consumes projected parent surface without a new style attachment");
    ButtonStyleValues local;
    local.background = ThemeColor{0x112255};
    clear.set_style_values(local);
    const auto local_pixels = fixture.render([&] {
        fixture.drawing.styled_button(clear, {10, 10, 32, 30}, palette, true, false, {}, 0, {}, &inherited);
    });
    require(count(local_pixels, 0x112255) > 200 && count(local_pixels, 0x557799) == 0 &&
        clear.style_values().background->resolve(ThemeMode::dark) == 0x112255,
        "Projected parent defaults never overwrite legacy child locals");
    palette.high_contrast = true;
    const auto contrast = fixture.render([&] { fixture.drawing.styled_button(button, {10, 10, 160, 70}, palette, true, true); });
    require(count(contrast, 0x223344) == 0 && count(contrast, 0xff0000) == 0,
        "High contrast retains system Button surfaces, indicators and focus");
}
void clear_glyph_and_state(Fixture& fixture) {
    fixture.drawing.set_visual_style(VisualStyle::winui);
    auto palette = Palette::system(ThemeMode::dark, VisualStyle::winui);
    palette.high_contrast = false;
    TextInput input(L"Owner");
    input.set_text(L"Retained text");
    PartStyleValues normal, hovered, pressed, disabled;
    normal.background = ThemeColor{0x223344}; normal.foreground = ThemeColor{0x00ff00};
    hovered.background = ThemeColor{0x334455};
    pressed.background = ThemeColor{0x445566};
    disabled.background = ThemeColor{0x556677};
    input.set_control_style(ControlStyle::create(StyleTarget::text_input, {{StylePart::clear_action, normal}},
        {{StylePart::clear_action, style_states::hovered, hovered}, {StylePart::clear_action, style_states::pressed, pressed},
            {StylePart::clear_action, style_states::disabled, disabled}}));
    Button clear(L"Clear Owner");
    clear.set_icon(ButtonIcon::close);
    clear.pointer_move(true);
    require(clear.pointer_down(), "Real clear child supplies pressed interaction");
    auto defaults = input.resolve_control_style_part(StylePart::clear_action,
        (clear.hovered() ? style_states::hovered : 0) | (clear.pressed() ? style_states::pressed : 0));
    require(defaults.background == pressed.background, "Projected clear style uses real child interaction");
    PartStyleValues local_root, local_icon;
    local_root.background = ThemeColor{0x112255};
    local_icon.foreground = ThemeColor{0xff8800};
    clear.set_control_style_values(StylePart::root, local_root);
    clear.set_control_style_values(StylePart::icon, local_icon);
    constexpr Rect bounds{10, 10, 32, 30};
    const auto actual = fixture.render([&] {
        fixture.drawing.styled_button(clear, bounds, palette, true, false, {}, 0, {}, &defaults, Symbol::clear);
    });
    const auto expected_glyph = fixture.render([&] {
        fixture.drawing.symbol(Symbol::clear, bounds, D2D1::ColorF(0xff8800), 12);
    });
    require(count(actual, 0x112255) > 200 && count(actual, 0xff8800) > 0,
        "Clear projection preserves explicit generic root and icon locals");
    for (std::size_t i = 0; i < actual.size(); ++i)
        require((actual[i] == 0xff8800) == (expected_glyph[i] == 0xff8800),
            "Styled private clear draws the exact existing WinUI clear symbol");
    input.set_enabled(false);
    defaults = input.resolve_control_style_part(StylePart::clear_action, style_states::pressed);
    require(defaults.background == disabled.background && input.text() == L"Retained text" &&
        clear.icon() == ButtonIcon::close && clear.control_style_values(StylePart::icon).foreground == local_icon.foreground,
        "Owner disabled wins without mutating text, child model icon, or child locals");
    fixture.drawing.set_visual_style(VisualStyle::classic);
}
}
int main() {
    try { Fixture fixture; typography_cache(fixture); surfaces_and_text(fixture); button_variants(fixture); clear_glyph_and_state(fixture); open_icon(fixture); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    std::cout << "Basic style DirectWrite and software rendering contracts passed\n";
}
