#include "../src/drawing.hpp"
#include "xui/navigation.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>

namespace allocation_probe {
thread_local bool active{};
thread_local std::size_t calls{};
}
void* operator new(std::size_t size) {
    if (auto* value = std::malloc(size ? size : 1)) {
        if (allocation_probe::active) ++allocation_probe::calls;
        return value;
    }
    throw std::bad_alloc{};
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* value) noexcept { std::free(value); }
void operator delete[](void* value) noexcept { std::free(value); }
void operator delete(void* value, std::size_t) noexcept { std::free(value); }
void operator delete[](void* value, std::size_t) noexcept { std::free(value); }

namespace xui {
struct DrawingTestAccess {
    static_assert(sizeof(Drawing::TextFormatKey) < sizeof(PartStyleValues) / 2);
    static IDWriteTextFormat* format(Drawing& drawing, const PartStyleValues& values, TextStyle fallback = TextStyle::body) {
        return drawing.styled_format(fallback, values);
    }
    static std::size_t format_entry_bytes() { return sizeof(Drawing::StyledFormat); }
    static std::size_t layout_entry_bytes() { return sizeof(Drawing::StyledLayout); }
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
void document_icons(Fixture& fixture) {
    constexpr std::array icons{ButtonIcon::save, ButtonIcon::save_as, ButtonIcon::undo, ButtonIcon::redo,
        ButtonIcon::chevron_up, ButtonIcon::chevron_down, ButtonIcon::chevron_right};
    for (const auto style : {VisualStyle::classic, VisualStyle::winui}) {
        fixture.drawing.set_visual_style(style);
        for (const auto size : {16.0f, 20.0f, 32.0f}) {
            std::array<std::vector<uint32_t>, icons.size()> images;
            for (std::size_t i = 0; i < icons.size(); ++i) {
                images[i] = fixture.render([&] {
                    fixture.drawing.button_icon({20, 20, size, size}, D2D1::ColorF(0xffffff), icons[i]);
                });
                require(count(images[i], 0x101010) < images[i].size(),
                    "Every document icon paints visible pixels in both visual styles");
                for (std::size_t previous = 0; previous < i; ++previous)
                    require(images[i] != images[previous], "Document command icons have distinct shapes");
                if (icons[i] == ButtonIcon::chevron_up || icons[i] == ButtonIcon::chevron_down || icons[i] == ButtonIcon::chevron_right) {
                    const bool up = icons[i] == ButtonIcon::chevron_up;
                    const bool right = icons[i] == ButtonIcon::chevron_right;
                    const auto expected = fixture.render([&] {
                        if (style == VisualStyle::winui) {
                            fixture.drawing.symbol(right ? Symbol::chevron_right : up ? Symbol::chevron_up : Symbol::chevron_down,
                                {20, 20, size, size}, D2D1::ColorF(0xffffff), size);
                        } else if (right) {
                            fixture.drawing.line(20 + 5 * size / 16, 20 + 3 * size / 16,
                                20 + 11 * size / 16, 20 + 8 * size / 16, D2D1::ColorF(0xffffff), 1.5f);
                            fixture.drawing.line(20 + 11 * size / 16, 20 + 8 * size / 16,
                                20 + 5 * size / 16, 20 + 13 * size / 16, D2D1::ColorF(0xffffff), 1.5f);
                        } else {
                            const float tip = 20 + (up ? 5 : 11) * size / 16;
                            const float tail = 20 + (up ? 11 : 5) * size / 16;
                            fixture.drawing.line(20 + 3 * size / 16, tail, 20 + 8 * size / 16, tip,
                                D2D1::ColorF(0xffffff), 1.5f);
                            fixture.drawing.line(20 + 8 * size / 16, tip, 20 + 13 * size / 16, tail,
                                D2D1::ColorF(0xffffff), 1.5f);
                        }
                    });
                    require(images[i] == expected, "Chevron buttons use the existing symbol or exactly two directional strokes");
                }
            }
        }
    }
    fixture.drawing.set_visual_style(VisualStyle::classic);
}
void partition_icons(Fixture& fixture) {
    constexpr std::array icons{ButtonIcon::folders_first, ButtonIcon::files_first, ButtonIcon::mixed};
    for (const auto size : {16.0f, 20.0f, 24.0f, 32.0f}) {
        for (const auto color : {0x202020u, 0xffffffu, 0xffff00u}) {
            std::array<std::vector<uint32_t>, icons.size()> classic;
            for (const auto style : {VisualStyle::classic, VisualStyle::winui}) {
                fixture.drawing.set_visual_style(style);
                for (std::size_t i = 0; i < icons.size(); ++i) {
                    const auto pixels = fixture.render([&] {
                        fixture.drawing.button_icon({20, 20, size, size}, D2D1::ColorF(color), icons[i]);
                    });
                    require(count(pixels, 0x101010) < pixels.size(), "Partition icons paint visible pixels at toolbar sizes");
                    if (style == VisualStyle::classic) classic[i] = pixels;
                    else require(pixels == classic[i], "Partition SVG geometry is identical in both visual styles");
                    for (std::size_t previous = 0; previous < i; ++previous)
                        require(pixels != classic[previous], "Each partition has a distinct vector icon");
                    for (int y = 0; y < 100; ++y) for (int x = 0; x < 200; ++x)
                        if (x < 20 || y < 20 || x >= 20 + size || y >= 20 + size)
                            require(pixels[y * 200 + x] == 0x101010, "Partition strokes stay inside their icon bounds");
                }
            }
        }
    }
    fixture.drawing.set_visual_style(VisualStyle::classic);
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
void typography_cache_keys() {
    Drawing drawing;
    drawing.initialize();
    PartStyleValues values;
    values.font_family = make_style_font_family("Segoe UI");
    values.font_size = 16.0f;
    const std::weak_ptr<const StyleFontFamily> family = values.font_family;
    Size measured;
    const auto first = drawing.styled_layout(L"Borrowed cache key", TextStyle::body, values, measured);
    const auto original_size = measured;
    auto* format = DrawingTestAccess::format(drawing, values);
    values.font_family = make_style_font_family("Segoe UI");
    require(!family.expired(), "Retained typography owns the original family after the caller replaces it");
    values.foreground = ThemeColor{0x123456};
    values.padding = Insets{1, 2, 3, 4};
    values.wrapping = false;
    require(DrawingTestAccess::format(drawing, values) == format &&
        drawing.styled_layout(L"Borrowed cache key", TextStyle::body, values, measured) == first,
        "Equal family names, non-text properties and explicit no-wrap reuse cached typography");
    const auto created = Drawing::created_text_layouts();
    allocation_probe::calls = 0;
    allocation_probe::active = true;
    for (unsigned i = 0; i < 1024; ++i) {
        DrawingTestAccess::format(drawing, values);
        drawing.styled_layout(L"Borrowed cache key", TextStyle::body, values, measured);
    }
    allocation_probe::active = false;
    require(allocation_probe::calls == 0 && Drawing::created_text_layouts() == created,
        "Warm format and layout lookups allocate no C++ storage or DirectWrite layouts");
    require(measured.width == original_size.width && measured.height == original_size.height,
        "Cache hits preserve measured glyph metrics");
    require(SUCCEEDED(first->SetMaxWidth(12)) && SUCCEEDED(first->SetMaxHeight(8)), "Mutate returned layout bounds");
    require(drawing.styled_layout(L"Borrowed cache key", TextStyle::body, values, measured) == first &&
        first->GetMaxWidth() == 10000000 && first->GetMaxHeight() == 10000000,
        "Cache hits restore mutable layout bounds");
    const auto differs = [&](PartStyleValues changed) {
        require(DrawingTestAccess::format(drawing, changed) != format &&
            drawing.styled_layout(L"Borrowed cache key", TextStyle::body, changed, measured) != first,
            "Each typography field participates in format and layout identity");
    };
    auto changed = values; changed.font_family = make_style_font_family("Consolas"); differs(changed);
    changed = values; changed.font_size = 17.0f; differs(changed);
    changed = values; changed.font_weight = 700; differs(changed);
    changed = values; changed.font_style = StyleFontStyle::italic; differs(changed);
    changed = values; changed.horizontal_alignment = StyleAlignment::end; differs(changed);
    changed = values; changed.vertical_alignment = StyleAlignment::start; differs(changed);
    changed = values; changed.wrapping = true; differs(changed);
    require(DrawingTestAccess::format(drawing, values, TextStyle::caption) != format &&
        drawing.styled_layout(L"Borrowed cache key", TextStyle::caption, values, measured) != first,
        "Fallback style remains part of cache identity even with authored fonts");
    changed = values; changed.wrapping.reset();
    const auto implicit_wrap = drawing.styled_layout(L"Borrowed cache key", TextStyle::body, changed, measured, 50);
    changed.wrapping = true;
    require(drawing.styled_layout(L"Borrowed cache key", TextStyle::body, changed, measured, 50) == implicit_wrap &&
        implicit_wrap->GetWordWrapping() == DWRITE_WORD_WRAPPING_WRAP,
        "Positive width and explicit wrap share the normalized layout key");
    changed.wrapping = false;
    require(drawing.styled_layout(L"Borrowed cache key", TextStyle::body, changed, measured, 50) != implicit_wrap,
        "Explicit no-wrap overrides the positive-width default");
    require(drawing.styled_layout(L"Borrowed cache key", TextStyle::body, values, measured, 51) != first &&
        drawing.styled_layout(L"Borrowed cache key", TextStyle::body, values, measured, 0, 1) != first,
        "Width and maximum lines remain part of layout identity");
    changed = values; changed.vertical_alignment = StyleAlignment::stretch;
    bool rejected{};
    try { DrawingTestAccess::format(drawing, changed); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "Warm caches still reject unsupported vertical stretch");
    const auto count = DrawingTestAccess::layouts(drawing);
    const std::wstring long_text(4097, L'a');
    drawing.styled_layout(long_text, TextStyle::body, values, measured);
    drawing.styled_layout(long_text, TextStyle::body, values, measured);
    require(DrawingTestAccess::layouts(drawing) == count, "Oversized text does not enter the bounded layout cache");
    drawing.release();
    require(DrawingTestAccess::layouts(drawing) == 0 && DrawingTestAccess::formats(drawing) == 0 && family.expired(),
        "Release drops typography cache ownership");
}
void typography_benchmark() {
    constexpr unsigned iterations = 200000;
    std::cout << "Typography entry bytes: format=" << DrawingTestAccess::format_entry_bytes()
        << " layout=" << DrawingTestAccess::layout_entry_bytes() << '\n';
    for (const unsigned count : {1u, 64u}) {
        Drawing drawing;
        drawing.initialize();
        std::array<PartStyleValues, 64> values;
        for (unsigned i = 0; i < count; ++i) {
            values[i].font_family = make_style_font_family("Segoe UI");
            values[i].font_size = 10.0f + i * 0.25f;
            DrawingTestAccess::format(drawing, values[i]);
        }
        std::array<double, 7> samples;
        for (auto& sample : samples) {
            const auto start = std::chrono::steady_clock::now();
            for (unsigned i = 0; i < iterations; ++i)
                DrawingTestAccess::format(drawing, values[i % count]);
            sample = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count() / iterations;
        }
        std::sort(samples.begin(), samples.end());
        std::cout << "Cached format lookup entries=" << count << " median_ns=" << samples[3] << '\n';
    }
    for (const unsigned count : {1u, 128u}) {
        Drawing drawing;
        drawing.initialize();
        PartStyleValues values;
        values.font_family = make_style_font_family("Segoe UI");
        values.font_size = 16.0f;
        std::array<std::wstring, 128> text;
        Size measured;
        for (unsigned i = 0; i < count; ++i) {
            text[i] = L"Retained row " + std::to_wstring(i);
            drawing.styled_layout(text[i], TextStyle::body, values, measured);
        }
        std::array<double, 7> samples;
        for (auto& sample : samples) {
            const auto start = std::chrono::steady_clock::now();
            for (unsigned i = 0; i < iterations; ++i)
                drawing.styled_layout(text[i % count], TextStyle::body, values, measured);
            sample = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count() / iterations;
        }
        std::sort(samples.begin(), samples.end());
        std::cout << "Cached layout lookup entries=" << count << " median_ns=" << samples[3] << '\n';
    }
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
        Button space(L"Go to folder");
        space.set_icon(ButtonIcon::folder);
        PartStyleValues hidden_icon;
        hidden_icon.size = 0.0f;
        space.set_control_style_values(StylePart::icon, hidden_icon);
        const auto named_space = fixture.render([&] {
            fixture.drawing.styled_button(space, {10, 10, 160, 70}, palette, true, false);
        });
        require(space.name() == L"Go to folder", "An empty address surface retains its accessible name");
        space.set_icon(ButtonIcon::none);
        space.set_name(L"");
        const auto empty_space = fixture.render([&] {
            fixture.drawing.styled_button(space, {10, 10, 160, 70}, palette, true, false);
        });
        require(named_space == empty_space, "Zero-size icons leave the trailing address surface visually empty");
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
void semantic_theme_brushes() {
    for (auto mode : {ThemeMode::light, ThemeMode::dark}) {
        auto palette = Palette::system(mode, VisualStyle::winui);
        palette.high_contrast = false;
        for (bool enabled : {false, true}) for (bool hovered : {false, true}) for (bool pressed : {false, true}) {
            for (auto appearance : {ButtonAppearance::standard, ButtonAppearance::accent, ButtonAppearance::subtle}) {
                const auto native = winui_button_brushes(mode, appearance, enabled, hovered, pressed, false);
                auto result = palette.button_brushes(appearance, enabled, hovered, pressed, false);
                require(result.fill == native.fill && result.text == native.text && result.stroke == native.stroke &&
                    result.elevation == native.elevation && result.elevated == native.elevated,
                    "An unset semantic theme preserves native Button brushes");
                palette.semantic_foreground = 0x993355; palette.semantic_accent = 0xffff00;
                result = palette.button_brushes(appearance, enabled, hovered, pressed, false);
                require((result.text >> 24) == (native.text >> 24) && result.stroke == native.stroke &&
                    result.elevation == native.elevation && result.elevated == native.elevated,
                    "Semantic resources preserve native state alpha and elevation feedback");
                if (enabled) {
                    require((result.text & 0xffffff) == (native.accent ? 0u : 0x993355u),
                        "Accent ink contrasts with yellow, not the ordinary foreground or light scheme");
                    if (native.accent) require((result.fill & 0xffffff) == 0xffff00 &&
                        (result.fill >> 24) == (native.fill >> 24), "Custom accent retains hover and press opacity");
                } else require(result.text == native.text && result.fill == native.fill, "Disabled brushes remain native");
                palette.semantic_foreground.reset(); palette.semantic_accent.reset();
            }
            palette.semantic_foreground = 0; palette.semantic_accent = 0x000033;
            const auto check = palette.indicator_brushes(true, enabled, hovered, pressed);
            const auto toggle = palette.switch_brushes(true, enabled, hovered, pressed);
            if (enabled) require((check.mark & 0xffffff) == 0xffffff && (toggle.mark & 0xffffff) == 0xffffff &&
                (check.text & 0xffffff) == 0 && (toggle.text & 0xffffff) == 0,
                "Dark accents use white marks while explicit black foreground remains distinct from unset");
            else {
                const auto native_check = winui_indicator_brushes(mode, true, false, hovered, pressed);
                const auto native_toggle = winui_switch_brushes(mode, true, false, hovered, pressed);
                require(check.fill == native_check.fill && check.mark == native_check.mark &&
                    check.text == native_check.text && toggle.fill == native_toggle.fill &&
                    toggle.mark == native_toggle.mark && toggle.text == native_toggle.text,
                    "Semantic roles do not recolor disabled checks or switches");
            }
            palette.high_contrast = true;
            require(palette.text_brush(true) == winui_text_brush(mode, true) &&
                palette.accent_brush(true, hovered, pressed) == winui_accent_brush(mode, true, hovered, pressed),
                "High contrast bypasses semantic RGB brushes");
            palette.high_contrast = false; palette.semantic_foreground.reset(); palette.semantic_accent.reset();
        }
    }
}
void semantic_theme_pixels(Fixture& fixture) {
    for (auto mode : {ThemeMode::light, ThemeMode::dark}) for (auto style : {VisualStyle::classic, VisualStyle::winui}) {
        fixture.drawing.set_visual_style(style);
        auto native = Palette::system(mode, style); native.high_contrast = false;
        auto themed = native;
        themed.semantic_foreground = 0x00ff00; themed.text = D2D1::ColorF(0x00ff00);
        themed.semantic_accent = 0xffff00; themed.accent = D2D1::ColorF(0xffff00);
        Label label(L"MMMM");
        Button button(L"MMMM");
        Toggle toggle(L"MMMM");
        CheckBox check(L"MMMM"); check.set_checked(true);
        ToggleSwitch switches(L"MMMM"); switches.set_checked(true);
        const std::array<const Toggle*, 3> choices{&toggle, &check, &switches};
        Size measured{};
        auto layout = fixture.drawing.layout(L"MMMM", TextStyle::body, measured);
        for (unsigned role = 0; role < 5; ++role) {
            auto paint = [&](const Palette& palette, bool enabled, bool focused = false) {
                return fixture.render([&] {
                    constexpr Rect bounds{10, 10, 180, 60};
                    if (role == 0) fixture.drawing.styled_label(label, bounds, palette, enabled);
                    else if (role == 1) fixture.drawing.styled_button(button, bounds, palette, enabled, focused);
                    else fixture.drawing.styled_toggle(*choices[role - 2],
                        bounds, palette, enabled, layout.Get(), focused);
                });
            };
            const auto ordinary = paint(native, true), custom = paint(themed, true);
            require(ordinary != custom, "Semantic foreground reaches each ordinary Label/Button/Toggle/CheckBox/Switch painter");
            require(paint(native, false) == paint(themed, false), "Semantic resources preserve disabled pixels");
            if (role != 0) require(paint(themed, true, true) != custom, "Semantic colors preserve focus feedback");
            PartStyleValues local; local.foreground = ThemeColor{0xff00ff};
            if (role == 0) label.set_control_style_values(StylePart::root, local);
            if (role == 1) button.set_control_style_values(StylePart::label, local);
            if (role == 2) toggle.set_control_style_values(StylePart::label, local);
            if (role == 3) check.set_control_style_values(StylePart::label, local);
            if (role == 4) switches.set_control_style_values(StylePart::label, local);
            require(count(paint(themed, true), 0xff00ff) > 5, "Authored local foreground wins over semantic roles");
            auto hc = Palette::system(ThemeMode::high_contrast, style);
            const auto normal_hc = paint(hc, true);
            hc.semantic_foreground = 0x00ff00; hc.semantic_accent = 0xffff00;
            require(normal_hc == paint(hc, true) && count(normal_hc, 0xff00ff) == 0,
                "High contrast ignores both semantic and authored RGB in actual pixels");
        }
        if (style == VisualStyle::winui) {
            const auto pixels = fixture.render([&] {
                fixture.drawing.button_face({10, 10, 180, 60}, themed, ButtonAppearance::accent, true, false, false, false);
                fixture.drawing.text(L"MMMM", {30, 10, 140, 60}, themed.accent_ink(themed.text));
            });
            require(count(pixels, 0xffff00) > 500 && count(pixels, 0) > 20,
                "Custom yellow accent paints a real yellow face and contrasting black ink in both schemes");
        }
    }
    fixture.drawing.set_visual_style(VisualStyle::classic);
}
void semantic_page_selectors(Fixture& fixture) {
    for (auto mode : {ThemeMode::light, ThemeMode::dark}) for (auto style : {VisualStyle::classic, VisualStyle::winui}) {
        fixture.drawing.set_visual_style(style);
        auto palette = Palette::system(mode, style); palette.high_contrast = false;
        palette.semantic_foreground = 0x00ff00; palette.text = D2D1::ColorF(0x00ff00);
        palette.semantic_accent = 0xffff00; palette.accent = D2D1::ColorF(0xffff00);
        TabStrip tabs(L"Pages"); tabs.set_tabs({{11, L"MMMM"}, {22, L"NNNN"}}, 11);
        tabs.set_visual_style(style); tabs.arrange({0, 0, 190, 60});
        PartStyleValues marker; marker.thickness = 3.0f;
        tabs.set_control_style_values(StylePart::selection, marker);
        const auto draw_tabs = [&](const Palette& colors, bool enabled, bool focus) {
            return fixture.render([&] { fixture.drawing.tab_strip(tabs, {0, 0, 190, 60}, colors, enabled, false, focus); });
        };
        const auto plain = draw_tabs(palette, true, false);
        require(count(plain, 0x00ff00) > 5 && count(plain, 0xffff00) > 30,
            "Native tab selected text and selection marker consume semantic foreground/accent");
        require(draw_tabs(palette, true, true) != plain, "Themed tabs retain native focus feedback");
        PartStyleValues local; local.foreground = ThemeColor{0xff00ff};
        tabs.set_control_style_values(StylePart::label, local);
        require(count(draw_tabs(palette, true, false), 0xff00ff) > 5, "Tab label locals override semantic foreground");
        NavigationView navigation(L"Pages");
        CollectionRow row{}; row.bounds = {10, 10, 180, 60}; row.navigation = true; row.content.primary = L"MMMM";
        row.content.enabled = true;
        const auto draw_row = [&](const Palette& colors, bool selected, bool enabled, bool focus) {
            return fixture.render([&] {
                fixture.drawing.collection_row(row, selected, focus, enabled, colors, false, {}, false, false, navigation.items().get());
            });
        };
        require(count(draw_row(palette, false, true, false), 0x00ff00) > 5,
            "Ordinary navigation text consumes semantic foreground");
        const auto selected = draw_row(palette, true, true, false);
        const auto selection_ink = theme_colors(mode, style).selection_text;
        require(count(selected, 0xffff00) > 15 && count(selected, selection_ink) > 5,
            "Navigation uses custom accent markers while retaining native selected-surface contrast ink");
        require(draw_row(palette, true, true, true) != selected, "Themed navigation retains focus feedback");
        const auto disabled = style == VisualStyle::winui ? winui_control_colors(mode).disabled_text : theme_colors(mode, style).secondary;
        require(count(draw_row(palette, true, false, false), disabled) > 5, "Disabled navigation retains native disabled text");
        navigation.items()->set_control_style_values(StylePart::row, local);
        require(count(draw_row(palette, true, true, false), 0xff00ff) > 5,
            "Navigation row locals override semantic and native selection ink");
        auto hc = Palette::system(ThemeMode::high_contrast, style);
        const auto hc_tabs = draw_tabs(hc, true, true), hc_rows = draw_row(hc, true, true, true);
        hc.semantic_foreground = 0x00ff00; hc.semantic_accent = 0xffff00;
        require(hc_tabs == draw_tabs(hc, true, true) && hc_rows == draw_row(hc, true, true, true),
            "High contrast ignores semantic colors in both selector painters");
    }
    fixture.drawing.set_visual_style(VisualStyle::classic);
}
void label_text_layout(Fixture& fixture) {
        Label label(L"MMMMMMMMMMMMMMMMMMMM");
        PartStyleValues font; font.font_size = 22.0f; font.vertical_alignment = StyleAlignment::start;
        label.set_control_style_values(StylePart::label, font);
        label.set_wrapping(true, 5);
        label.set_text_layout(LabelTextLayout{false, LabelOverflow::clip});
        require(!label.wrapping() && label.maximum_lines() == 1, "Label descriptor masks legacy wrapping without changing it");
        label.set_wrapping(true, 4);
        label.set_text_layout(std::nullopt);
        require(label.wrapping() && label.maximum_lines() == 4, "Null restores the latest legacy wrapping fields");
        Label styled(L"Styled wrapping");
        auto wrapped_style = font; wrapped_style.wrapping = true; wrapped_style.maximum_lines = 3;
        styled.set_control_style_values(StylePart::label, wrapped_style);
        styled.set_text_layout(LabelTextLayout{false, LabelOverflow::clip});
        styled.set_text_layout(std::nullopt);
        require(styled.wrapping() && styled.maximum_lines() == 3,
            "Null restores inherited style wrapping, not a newly explicit legacy flag");
        const auto palette = Palette::system(ThemeMode::dark);
        const auto draw = [&](float width) {
            return fixture.render([&] { fixture.drawing.styled_label(label, {10, 5, width, 90}, palette, true); });
        };
        label.set_text_layout(LabelTextLayout{false, LabelOverflow::clip});
        const auto clipped = draw(65);
        const auto independently_cropped = fixture.render([&] {
            fixture.drawing.push_clip({10, 5, 65, 90});
            fixture.drawing.styled_label(label, {10, 5, 180, 90}, palette, true);
            fixture.drawing.pop_clip();
        });
        require(clipped == independently_cropped, "SingleLine Clip is literal full-text clipping, not ellipsis");
        label.set_text_layout(LabelTextLayout{false, LabelOverflow::character_ellipsis});
        const auto ellipsis = draw(65);
        require(ellipsis != clipped, "CharacterEllipsis produces different overflow pixels from Clip");
        label.set_text_layout(LabelTextLayout{false, LabelOverflow::clip});
        require(draw(65) == clipped, "Alternating overflow modes cannot contaminate cached format/layout objects");
        Size clip_size{}, ellipsis_size{};
        auto values = font; values.wrapping = false;
        const auto clip_layout = fixture.drawing.styled_layout(label.text(), TextStyle::body, values, clip_size, 65, 0, true);
        const auto ellipse_layout = fixture.drawing.styled_layout(label.text(), TextStyle::body, values, ellipsis_size, 65);
        DWRITE_TRIMMING clip_trimming{}, ellipse_trimming{};
        Microsoft::WRL::ComPtr<IDWriteInlineObject> clip_sign, ellipse_sign;
        require(SUCCEEDED(clip_layout->GetTrimming(&clip_trimming, &clip_sign)) &&
            SUCCEEDED(ellipse_layout->GetTrimming(&ellipse_trimming, &ellipse_sign)) &&
            clip_trimming.granularity == DWRITE_TRIMMING_GRANULARITY_NONE && !clip_sign &&
            ellipse_trimming.granularity == DWRITE_TRIMMING_GRANULARITY_CHARACTER && ellipse_sign &&
            clip_layout != ellipse_layout,
            "Clipping and ellipsis use distinct cached format/layout objects and trimming signs");
        fixture.drawing.styled_layout(label.text(), TextStyle::body, values, clip_size, 0, 0, true);
        fixture.drawing.styled_layout(label.text(), TextStyle::body, values, ellipsis_size);
        require(clip_size.width == ellipsis_size.width && clip_size.width > 65,
            "Unbounded intrinsic measurement preserves complete text for both overflow modes");
        label.set_text_layout(LabelTextLayout{true, LabelOverflow::clip, 2});
        label.set_text(L"First\nSecond\nThird");
        const auto capped = draw(180);
        label.set_text_layout(LabelTextLayout{true, LabelOverflow::clip});
        const auto uncapped = draw(180);
        require(capped != uncapped && label.text() == L"First\nSecond\nThird", "Wrap maxLines clips presentation only, not source text");
        values.wrapping = true; values.maximum_lines = 2;
        const auto wrapped = fixture.drawing.styled_layout(label.text(), TextStyle::body, values, clip_size, 180, 2, true);
        require(SUCCEEDED(wrapped->GetTrimming(&clip_trimming, &clip_sign)) &&
            clip_trimming.granularity == DWRITE_TRIMMING_GRANULARITY_NONE && !clip_sign,
            "Capped Wrap explicitly retains no trimming sign");
        for (const auto text : {L"\r", L"\n", L"\u0085", L"\u2028", L"\u2029"}) {
            label.set_text(text);
            bool rejected{};
            try { label.set_text_layout(LabelTextLayout{}); } catch (const std::invalid_argument&) { rejected = true; }
            require(rejected && label.wrapping(), "Descriptor rejection preserves old wrapping and full hard-break text");
        }
}
}
int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string_view(argv[1]) == "--benchmark") { typography_benchmark(); return 0; }
        typography_cache_keys();
        semantic_theme_brushes();
        Fixture fixture; label_text_layout(fixture); semantic_theme_pixels(fixture); semantic_page_selectors(fixture); document_icons(fixture); partition_icons(fixture); typography_cache(fixture); surfaces_and_text(fixture); button_variants(fixture); clear_glyph_and_state(fixture); open_icon(fixture);
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    std::cout << "Basic style DirectWrite and software rendering contracts passed\n";
}
