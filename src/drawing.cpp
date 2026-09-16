#include "drawing.hpp"
#include <dwrite_3.h>
#include "platform.hpp"
#include "window_host.hpp"
#include "images.hpp"
#include <algorithm>
#include <limits>
#include <cmath>
#include <utility>
#include <chrono>

namespace xui {
std::size_t Drawing::native_bitmap_bytes() const {
    std::size_t result{};
    for (const auto& entry : native_bitmaps_) if (entry.bitmap) {
        const auto size = entry.bitmap->GetPixelSize();
        result += std::size_t(size.width) * size.height * 4;
    }
    return result;
}
std::size_t Drawing::scene_paths() const {
    std::size_t result{};
    for (const auto& entry : scenes_) result += entry.paths.size();
    return result;
}
void Drawing::scene(const std::shared_ptr<const VectorScene>& source, std::optional<ShapeId> selected, D2D1_COLOR_F highlight) {
    if (!source) return;
    std::erase_if(scenes_, [](const auto& cache) { return cache.source.expired(); });
    auto found = std::find_if(scenes_.begin(), scenes_.end(), [&](const auto& cache) { return cache.source.lock() == source; });
    if (found == scenes_.end()) {
        if (scenes_.size() >= 8) scenes_.erase(scenes_.begin());
        SceneCache cache; cache.source = source;
        for (const auto& shape : source->shapes()) {
            Microsoft::WRL::ComPtr<ID2D1PathGeometry> geometry;
            hr_require(factory_->CreatePathGeometry(&geometry), "Create retained scene path");
            Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
            hr_require(geometry->Open(&sink), "Open retained scene path");
            sink->SetFillMode(D2D1_FILL_MODE_ALTERNATE);
            sink->BeginFigure({shape.points.front().x, shape.points.front().y}, D2D1_FIGURE_BEGIN_FILLED);
            for (std::size_t i = 1; i < shape.points.size(); ++i) sink->AddLine({shape.points[i].x, shape.points[i].y});
            sink->EndFigure(shape.closed ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
            hr_require(sink->Close(), "Close retained scene path"); cache.paths.push_back(std::move(geometry));
        }
        scenes_.push_back(std::move(cache)); found = scenes_.end() - 1;
    }
    found->used = true;
    if (!scene_stroke_) {
        auto style = D2D1::StrokeStyleProperties();
        style.startCap = style.endCap = D2D1_CAP_STYLE_ROUND; style.lineJoin = D2D1_LINE_JOIN_ROUND;
        hr_require(factory_->CreateStrokeStyle(style, nullptr, 0, &scene_stroke_), "Create scene stroke");
    }
    const auto color = [](SceneColor c) { return D2D1::ColorF(c.red, c.green, c.blue, c.alpha); };
    for (std::size_t i = 0; i < source->shapes().size(); ++i) {
        const auto& s = source->shapes()[i]; auto* path = found->paths[i].Get();
        if (s.clip) push_clip(*s.clip);
        if (selected == s.id) { brush_->SetColor(highlight); target_->DrawGeometry(path, brush_.Get(), s.stroke_width + 4, scene_stroke_.Get()); }
        if (s.closed && s.fill.alpha > 0) { brush_->SetColor(color(s.fill)); target_->FillGeometry(path, brush_.Get()); }
        if (s.stroke.alpha > 0 && s.stroke_width > 0) { brush_->SetColor(color(s.stroke)); target_->DrawGeometry(path, brush_.Get(), s.stroke_width, scene_stroke_.Get()); }
        if (s.clip) pop_clip();
    }
}
void Drawing::item_visual(const ItemVisual& visual, const std::shared_ptr<const ImagePixels>& pixels, Rect bounds, D2D1_COLOR_F ink) {
    if (pixels) {
        if (image(pixels, bounds)) return;
        OutputDebugStringW(L"XUI thumbnail: Bitmap upload failed. Drawing the fallback icon.\n");
    }
    if (visual.icon != ButtonIcon::none) button_icon(bounds, ink, visual.icon);
    else if (!visual.image_path.empty()) icon(bounds, ink, false);
}
void Drawing::tab_strip(const TabStrip& strip, Rect bounds, const Palette& palette, bool enabled, bool on_surface,
    bool focus_visible, std::optional<Point> pointer) {
    if (bounds.width <= 0 || bounds.height <= 0) return;
    const bool winui = palette.style == VisualStyle::winui;
    const auto& colors = strip.colors();
    const auto resolve = [&](std::optional<std::uint32_t> value, D2D1_COLOR_F fallback) {
        return value && !palette.high_contrast ? D2D1::ColorF(*value) : fallback;
    };
    const auto parent = on_surface ? palette.surface : palette.background;
    const auto content = resolve(colors.selected_background, parent);
    const auto rail = resolve(colors.row_background, parent);
    const auto inactive = resolve(colors.inactive_background, rail);
    const auto hover = resolve(colors.hover_background, palette.hover);
    const auto border = resolve(colors.border, palette.border);
    fill({0, 0, bounds.width, bounds.height}, rail);
    const auto hovered = enabled && pointer && pointer->y >= 0 && pointer->y < bounds.height ?
        strip.hit_test(pointer->x) : std::nullopt;
    const auto is_selected = [&](std::size_t index) { return strip.selected() == strip.tabs()[index].id; };
    const auto paint = [&](std::size_t index, bool selected) {
        auto b = strip.tab_bounds(index);
        if (b.width <= 0 || b.height <= 0) return;
        b.height = bounds.height;
        const bool hot = hovered == index;
        const float top = std::min(selected ? 2.0f : 6.0f, b.height);
        const float radius = std::min({winui ? 4.0f : 5.0f, b.width / 2, (b.height - top) / 2});
        push_clip(b);
        if (selected || hot || (colors.inactive_background && !palette.high_contrast)) {
            // Clip the lower corners and stroke below the rail: the active tab opens into its content.
            const Rect face{b.x + 0.5f, top + 0.5f, std::max(0.0f, b.width - 1), b.height - top + radius + 1};
            rounded(face, selected ? content : hot ? hover : inactive, radius);
            if (selected) rounded(face, border, radius, true);
        }
        if (!selected && index + 1 < strip.tabs().size() && !is_selected(index + 1) && b.height > 18)
            fill({b.x + b.width - 1, 10, 1, b.height - 18}, border);
        const auto ink = !enabled ? palette.disabled : selected ?
            resolve(colors.selected_text, palette.text) : resolve(colors.inactive_text, palette.secondary);
        const auto close = strip.close_bounds(index);
        const float right = close.width > 0 ? close.x - 4 : b.x + b.width - 10;
        text(strip.tabs()[index].title, {b.x + 12, top, std::max(0.0f, right - b.x - 12), b.height - top}, ink, !winui);
        if (close.width > 0) {
            const bool close_hot = hot && pointer->x >= close.x && pointer->x < close.x + close.width &&
                pointer->y >= close.y && pointer->y < close.y + close.height;
            if (close_hot) rounded(close, palette.high_contrast ? palette.selection : hover, 3);
            const auto close_ink = close_hot && palette.high_contrast ? palette.selection_text : ink;
            if (winui) symbol(Symbol::close, close, close_ink, 12);
            else {
                const float x = close.x + close.width / 2, y = close.y + close.height / 2;
                line(x - 4, y - 4, x + 4, y + 4, close_ink);
                line(x - 4, y + 4, x + 4, y - 4, close_ink);
            }
        }
        if (selected && focus_visible && enabled && b.width > 10 && b.height > 12) {
            const Rect focus{b.x + 4, top + 3, b.width - 8, b.height - top - 7};
            if (winui) focus_ring(focus, palette, 2);
            else rounded(focus, palette.accent, 2, true);
        }
        pop_clip();
    };
    for (std::size_t i = 0; i < strip.tabs().size(); ++i)
        if (!is_selected(i)) paint(i, false);
    if (!strip.tabs().empty()) {
        float left{}, right{};
        for (std::size_t i = 0; i < strip.tabs().size(); ++i)
            if (is_selected(i)) {
                const auto selected = strip.tab_bounds(i);
                left = selected.x; right = selected.x + selected.width;
                break;
            }
        // Never paint a baseline under the selected tab: fractional-DPI clips can expose it.
        const float bottom = std::max(0.0f, bounds.height - 1);
        if (left > 0) fill({0, bottom, left, 1}, border);
        if (right < bounds.width) fill({right, bottom, bounds.width - right, 1}, border);
    }
    for (std::size_t i = 0; i < strip.tabs().size(); ++i)
        if (is_selected(i)) paint(i, true);
}
void Drawing::collection_row(const CollectionRow& row, bool selected, bool focused, bool enabled, const Palette& palette, bool hovered,
    const std::shared_ptr<const ImagePixels>& pixels, bool trailing_shortcut_badges, bool command_menu) {
    const auto b = row.bounds;
    if (row.navigation) {
        selected = selected || row.selected_descendant;
        const auto ink = !enabled || !row.content.enabled ? palette.disabled : selected ? palette.selection_text : palette.text;
        const Rect face{b.x + 2, b.y + 2, std::max(0.0f, b.width - 4), std::max(0.0f, b.height - 4)};
        if (selected || (row.hovered && enabled && row.content.enabled))
            rounded(face, selected ? palette.selection : palette.hover, 5);
        if (selected) rounded({b.x + 2, b.y + 10, 3, std::max(0.0f, b.height - 20)}, palette.accent, 1.5f);
        const float left = row.compact ? b.x + std::max(0.0f, (b.width - 20) / 2) :
            b.x + 12 + std::min(static_cast<float>(row.depth) * 16, b.width / 3);
        const bool visual = row.content.icon != ButtonIcon::none || !row.content.image_path.empty();
        if (visual)
            item_visual({row.content.icon, row.content.image_path}, pixels, {left, b.y + (b.height - 20) / 2, 20, 20}, ink);
        else if (row.compact)
            text(row.content.primary.substr(0, 1), {left, b.y, 20, b.height}, ink);
        if (!row.compact) {
            float right = b.x + b.width - (row.expandable ? 32.0f : 10.0f);
            if (!row.content.secondary.empty() && right - left > 120) {
                const float badge_width = std::min(64.0f, 16 + static_cast<float>(row.content.secondary.size()) * 7);
                rounded({right - badge_width, b.y + 9, badge_width, b.height - 18}, palette.surface, 8);
                text(row.content.secondary, {right - badge_width + 7, b.y, badge_width - 14, b.height},
                    palette.style == VisualStyle::winui && (!enabled || !row.content.enabled) ? palette.disabled : palette.secondary, true);
                right -= badge_width + 6;
            }
            const float text_left = left + (visual ? 28 : 0);
            text(row.content.primary, {text_left, b.y, std::max(0.0f, right - text_left), b.height}, ink);
            if (row.expandable) {
                chevron({b.x + b.width - 33, b.y, 24, b.height}, ink, row.expanded);
            }
        }
        if (focused) {
            if (palette.style == VisualStyle::winui) focus_ring(face, palette);
            else rounded(face, palette.accent, 5, true);
        }
        return;
    }
    if (row.content.separator) {
        fill({b.x + 10, b.y + b.height / 2, std::max(0.0f, b.width - 20), 1}, palette.border);
        return;
    }
    if (row.group && !row.expandable) {
        const bool visual = row.content.icon != ButtonIcon::none || !row.content.image_path.empty();
        if (visual) item_visual({row.content.icon, row.content.image_path}, pixels, {b.x + 10, b.y + (b.height - 20) / 2, 20, 20}, palette.secondary);
        text(row.content.primary, {b.x + (visual ? 38 : 10), b.y, std::max(0.0f, b.width - (visual ? 48 : 20)), b.height}, palette.secondary, true);
        return;
    }
    const auto ink = !enabled || !row.content.enabled ? palette.disabled : selected ? palette.selection_text : palette.text;
    const bool winui = palette.style == VisualStyle::winui;
    const Rect face{b.x + 2, b.y + 2, std::max(0.0f, b.width - 4), std::max(0.0f, b.height - 4)};
    if (selected || hovered || row.group) {
        const auto background = selected ? palette.selection : hovered ? palette.hover : palette.surface;
        if (palette.style == VisualStyle::winui) rounded(face, background, 4);
        else fill({b.x + 1, b.y + 1, std::max(0.0f, b.width - 2), b.height - 2}, background);
    }
    float left = b.x + 10 + std::min(static_cast<float>(row.depth) * 20, b.width / 3);
    if (row.content.checked) {
        if (winui && command_menu) {
            if (*row.content.checked) symbol(Symbol::check, {left, b.y, 16, b.height}, ink, 12);
        } else if (winui) check_indicator({left, b.y + (b.height - 16) / 2, 16, 16},
            palette, *row.content.checked, enabled && row.content.enabled);
        else text(*row.content.checked ? L"✓" : L"○", {left, b.y, 22, b.height}, ink);
        left += 24;
    }
    if (row.expandable && !row.content.submenu) {
        if (winui) chevron({left, b.y, 22, b.height}, ink, row.expanded);
        else text(row.expanded ? L"\u25be" : L"\u25b8", {left, b.y, 22, b.height}, ink);
        left += 24;
    }
    if (row.content.icon != ButtonIcon::none || !row.content.image_path.empty()) {
        const float size = row.content.image_path.empty() ? 20.0f : 24.0f;
        item_visual({row.content.icon, row.content.image_path}, pixels, {left, b.y + (b.height - size) / 2, size, size}, ink); left += size + 8;
    }
    const bool action_visible = !row.content.action.empty() && b.width >= 160;
    const bool secondary_visible = !trailing_shortcut_badges && !row.content.secondary.empty() && b.height >= 48;
    float right = b.x + b.width - (action_visible ? 74 : row.content.submenu ? 34 : 10);
    if (trailing_shortcut_badges) {
        const float gap = std::min(12.0f, std::max(0.0f, right - left));
        const float lane = std::min(144.0f, std::max(0.0f, (right - left - gap) / 2));
        const float height = std::min(24.0f, std::max(0.0f, b.height - 8));
        const auto badge_ink = enabled && row.content.enabled ? palette.text : palette.disabled;
        const std::wstring_view shortcut = row.content.secondary;
        struct Keycap {
            Microsoft::WRL::ComPtr<IDWriteTextLayout> label;
            float width{}, text_width{};
        };
        std::vector<Keycap> keys;
        float total_width{};
        for (std::size_t start = 0; start < shortcut.size() && total_width + (keys.empty() ? 0 : 4) + 12 < lane;) {
            auto end = shortcut.find(L'+', start);
            // A final '+' is the key in shortcuts such as Ctrl++.
            if (end == std::wstring_view::npos || end == start) end = shortcut.size();
            auto key = shortcut.substr(start, end - start);
            while (!key.empty() && key.front() == L' ') key.remove_prefix(1);
            while (!key.empty() && key.back() == L' ') key.remove_suffix(1);
            if (!key.empty()) {
                Size measured{};
                auto label = layout(key, TextStyle::caption, measured);
                if (!keys.empty()) total_width += 4;
                const float key_width = std::min(std::max(24.0f, measured.width + 12), lane - total_width);
                keys.push_back({std::move(label), key_width, measured.width});
                total_width += key_width;
            }
            start = end == shortcut.size() ? end : end + 1;
        }
        float x = right - total_width;
        for (const auto& key : keys) {
            const Rect badge{x, b.y + (b.height - height) / 2, key.width, height};
            rounded(badge, palette.field, 4);
            rounded(badge, palette.high_contrast ? badge_ink : palette.border, 4, true);
            const float inset = std::max(6.0f, (key.width - key.text_width) / 2);
            text_layout(key.label.Get(), {x + inset, badge.y, std::max(0.0f, key.width - 2 * inset), height}, badge_ink);
            x += key.width + 4;
        }
        if (!keys.empty()) right -= total_width + gap;
    }
    const float width = std::max(0.0f, right - left);
    text(row.content.primary, {left, b.y + 3, width, secondary_visible ? 26 : b.height - 6}, ink);
    if (secondary_visible) text(row.content.secondary, {left, b.y + 27, width, std::max(0.0f, b.height - 30)},
        !enabled || (winui && !row.content.enabled) ? palette.disabled : selected ? palette.selection_text : palette.secondary, true);
    if (row.content.progress && std::isfinite(*row.content.progress)) {
        const Rect track{left, b.y + b.height - 4, width, 2}; fill(track, palette.border);
        fill({track.x, track.y, track.width * static_cast<float>(std::clamp(*row.content.progress, 0.0, 1.0)), track.height}, ink);
    }
    if (action_visible) {
        const Rect action{b.x + b.width - 70, b.y + 8, 64, std::max(0.0f, b.height - 16)};
        const auto action_ink = winui ? button_face(action, palette, ButtonAppearance::standard,
            enabled && row.content.enabled, false, false, false) : ink;
        if (!winui) rounded(action, palette.border, 4, true);
        text(row.content.action, {action.x + 5, action.y, action.width - 10, action.height}, action_ink, true);
    }
    if (row.content.submenu) {
        if (winui) chevron({b.x + b.width - 28, b.y, 20, b.height}, ink, false);
        else text(L"›", {b.x + b.width - 28, b.y, 20, b.height}, ink);
    }
    if (focused || (hovered && palette.high_contrast)) {
        if (palette.style == VisualStyle::winui) focus_ring(face, palette);
        else outline({b.x + 1, b.y + 1, std::max(0.0f, b.width - 2), b.height - 2}, palette.accent);
    }
}
thread_local std::size_t Drawing::live_targets_{};
thread_local std::size_t Drawing::created_text_layouts_{};
thread_local std::size_t Drawing::created_field_brushes_{};
thread_local HRESULT Drawing::end_result_override_{S_OK};
thread_local HRESULT Drawing::native_result_override_{S_OK};
thread_local void (*Drawing::present_observer_)(HWND){};
std::size_t Drawing::live_targets() { return live_targets_; }
namespace {
D2D1_COLOR_F color(int system_color) {
    const COLORREF value = GetSysColor(system_color);
    return D2D1::ColorF(GetRValue(value) / 255.0f, GetGValue(value) / 255.0f,
                       GetBValue(value) / 255.0f);
}
D2D1_RECT_F rectangle(Rect bounds) {
    return D2D1::RectF(bounds.x, bounds.y, bounds.x + bounds.width, bounds.y + bounds.height);
}
D2D1_COLOR_F argb_color(uint32_t value) {
    return D2D1::ColorF(value & 0xffffff, static_cast<float>(value >> 24) / 255);
}
}

Palette Palette::system(ThemeMode mode, VisualStyle style) {
    HIGHCONTRASTW contrast{sizeof(contrast)};
    win32_require(SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) != 0,
                  "Read high contrast settings");
    if (mode == ThemeMode::high_contrast || (contrast.dwFlags & HCF_HIGHCONTRASTON))
        return {color(COLOR_WINDOW), color(COLOR_WINDOWTEXT), color(COLOR_WINDOWTEXT),
                color(COLOR_HIGHLIGHT), color(COLOR_HIGHLIGHTTEXT), color(COLOR_WINDOWTEXT),
                color(COLOR_WINDOW), color(COLOR_WINDOW), color(COLOR_WINDOW),
                color(COLOR_HIGHLIGHT), color(COLOR_WINDOWTEXT), color(COLOR_WINDOWTEXT),
                color(COLOR_WINDOWTEXT), true, color(COLOR_GRAYTEXT), style, mode};
    const auto theme = theme_colors(mode, style);
    return {D2D1::ColorF(theme.background), D2D1::ColorF(theme.text), D2D1::ColorF(theme.secondary),
            D2D1::ColorF(theme.selection), D2D1::ColorF(theme.selection_text), D2D1::ColorF(theme.border),
            D2D1::ColorF(theme.surface), D2D1::ColorF(theme.field), D2D1::ColorF(theme.hover),
            D2D1::ColorF(theme.accent), D2D1::ColorF(theme.folder), D2D1::ColorF(theme.file),
            D2D1::ColorF(theme.error), false,
            D2D1::ColorF(style == VisualStyle::winui ? winui_control_colors(mode).disabled_text : theme.secondary), style, mode};
}

D2D1_COLOR_F Palette::input_fill(bool enabled, bool focused, bool hovered, bool on_surface) const {
    if (style != VisualStyle::winui || high_contrast) return field;
    const auto parent = platform::native_color(on_surface ? surface : background);
    const auto rgb = (uint32_t(GetRValue(parent)) << 16) | (uint32_t(GetGValue(parent)) << 8) | GetBValue(parent);
    return D2D1::ColorF(composite_argb_on_rgb(winui_input_background(mode, enabled, focused, hovered), rgb));
}

void Drawing::initialize(VisualStyle style) {
    hr_require(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory_.GetAddressOf()),
               "Create Direct2D factory");
    hr_require(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
               reinterpret_cast<IUnknown**>(text_factory_.GetAddressOf())), "Create DirectWrite factory");
    set_visual_style(style);
}

void Drawing::set_visual_style(VisualStyle style) {
    if (format_ && visual_style_ == style) return;
    Microsoft::WRL::ComPtr<IDWriteFactory6> variable_factory;
    Microsoft::WRL::ComPtr<IDWriteFontCollection2> variable_fonts;
    variable_font_ = false;
    if (style == VisualStyle::winui) {
        wchar_t scripts[128]{};
        win32_require(GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_SSCRIPTS, scripts, 128) > 0, "Read UI font locale");
        if (std::wstring_view(scripts) == L"Latn;") {
            const auto supported = text_factory_.As(&variable_factory);
            if (supported != E_NOINTERFACE) hr_require(supported, "Read variable font support");
            if (variable_factory) {
                hr_require(variable_factory->GetSystemFontCollection(FALSE, DWRITE_FONT_FAMILY_MODEL_TYPOGRAPHIC,
                    &variable_fonts), "Read typographic font collection");
                UINT32 index{};
                BOOL exists{};
                hr_require(variable_fonts->FindFamilyName(L"Segoe UI Variable", &index, &exists), "Find WinUI variable font");
                variable_font_ = exists != FALSE;
            }
        }
    }
    for (auto entry : {std::pair{std::addressof(format_), VisualMetrics::body_size},
                       std::pair{std::addressof(small_format_), VisualMetrics::caption_size},
                       std::pair{std::addressof(heading_format_), VisualMetrics::heading_size},
                       std::pair{std::addressof(subtitle_format_), 20.0f},
                       std::pair{std::addressof(strong_format_), VisualMetrics::body_size}}) {
        const auto weight = entry.first == std::addressof(heading_format_) || entry.first == std::addressof(subtitle_format_) ||
            entry.first == std::addressof(strong_format_) ?
            DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL;
        if (variable_font_) {
            const DWRITE_FONT_AXIS_VALUE axis{DWRITE_FONT_AXIS_TAG_WEIGHT, static_cast<float>(weight)};
            Microsoft::WRL::ComPtr<IDWriteTextFormat3> format;
            hr_require(variable_factory->CreateTextFormat(L"Segoe UI Variable", variable_fonts.Get(), &axis, 1,
                entry.second, L"", &format), "Create WinUI variable text format");
            hr_require(format->SetAutomaticFontAxes(DWRITE_AUTOMATIC_FONT_AXES_OPTICAL_SIZE), "Set automatic optical sizing");
            *entry.first = format;
        } else {
            hr_require(text_factory_->CreateTextFormat(L"Segoe UI", nullptr, weight,
                DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, entry.second, L"",
                entry.first->ReleaseAndGetAddressOf()), "Create text format");
        }
        hr_require((*entry.first)->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP), "Set text wrapping");
        hr_require((*entry.first)->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER),
                   "Set text alignment");
        Microsoft::WRL::ComPtr<IDWriteInlineObject> ellipsis;
        hr_require(text_factory_->CreateEllipsisTrimmingSign(entry.first->Get(), &ellipsis),
                   "Create text ellipsis");
        DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
        hr_require((*entry.first)->SetTrimming(&trimming, ellipsis.Get()), "Set text trimming");
    }
    numeric_format_.Reset();
    if (style == VisualStyle::winui) prepare_symbols();
    visual_style_ = style;
}

void Drawing::prepare_symbols() {
    if (symbol_face_) return;
    Microsoft::WRL::ComPtr<IDWriteFontCollection> fonts;
    hr_require(text_factory_->GetSystemFontCollection(&fonts), "Read symbol font collection");
    UINT32 index{};
    BOOL exists{};
    const wchar_t* family = L"Segoe Fluent Icons";
    hr_require(fonts->FindFamilyName(family, &index, &exists), "Find Segoe Fluent Icons");
    if (!exists) {
        family = L"Segoe MDL2 Assets";
        hr_require(fonts->FindFamilyName(family, &index, &exists), "Find compatible symbol font");
        if (!exists) throw std::runtime_error("WinUI icons require Segoe Fluent Icons or Segoe MDL2 Assets");
        OutputDebugStringW(L"XUI: Segoe Fluent Icons is not installed; using Segoe MDL2 Assets.\n");
    }
    Microsoft::WRL::ComPtr<IDWriteFontFamily> font_family;
    Microsoft::WRL::ComPtr<IDWriteFont> font;
    Microsoft::WRL::ComPtr<IDWriteFontFace> face;
    hr_require(fonts->GetFontFamily(index, &font_family), "Read symbol font family");
    hr_require(font_family->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        DWRITE_FONT_STYLE_NORMAL, &font), "Read symbol font");
    hr_require(font->CreateFontFace(&face), "Create symbol font face");
    hr_require(face->GetGlyphIndices(symbol_codepoints.data(), static_cast<UINT32>(symbol_codepoints.size()),
        symbol_indices_.data()), "Resolve symbol glyphs");
    for (std::size_t i = 1; i < symbol_indices_.size(); ++i)
        if (!symbol_indices_[i]) throw std::runtime_error("The installed symbol font is missing a required XUI icon");
    hr_require(face->GetDesignGlyphMetrics(symbol_indices_.data(), static_cast<UINT32>(symbol_indices_.size()),
        symbol_metrics_.data()), "Measure symbol glyphs");
    face->GetMetrics(&symbol_font_metrics_);
    symbol_face_ = std::move(face);
    symbol_family_ = family;
    ++created_symbol_faces_;
}

bool Drawing::has_symbol(Symbol value) const {
    const auto index = static_cast<std::size_t>(value);
    return symbol_face_ && index > 0 && index < symbol_indices_.size() && symbol_indices_[index] != 0;
}

void Drawing::symbol(Symbol value, Rect bounds, D2D1_COLOR_F color, float size) {
    if (value == Symbol::none || bounds.width <= 0 || bounds.height <= 0) return;
    if (!std::isfinite(size) || size <= 0) throw std::invalid_argument("Invalid symbol size");
    prepare_symbols();
    if (!has_symbol(value)) throw std::invalid_argument("Invalid symbol");
    const auto index = static_cast<std::size_t>(value);
    const float em = std::min({size, bounds.width, bounds.height});
    const float scale = em / symbol_font_metrics_.designUnitsPerEm;
    const float advance = symbol_metrics_[index].advanceWidth * scale;
    const float height = (symbol_font_metrics_.ascent + symbol_font_metrics_.descent) * scale;
    const DWRITE_GLYPH_RUN run{symbol_face_.Get(), em, 1, &symbol_indices_[index], &advance, nullptr, FALSE, 0};
    brush_->SetColor(color);
    const auto antialias = target_->GetTextAntialiasMode();
    target_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    target_->DrawGlyphRun(D2D1::Point2F(bounds.x + (bounds.width - advance) / 2,
        bounds.y + (bounds.height - height) / 2 + symbol_font_metrics_.ascent * scale),
        &run, brush_.Get(), DWRITE_MEASURING_MODE_NATURAL);
    target_->SetTextAntialiasMode(antialias);
}

bool Drawing::begin(HWND window, float dpi, D2D1_COLOR_F background) {
    RECT client{};
    win32_require(GetClientRect(window, &client) != 0, "Read window size");
    const auto size = D2D1::SizeU(static_cast<UINT32>(client.right), static_cast<UINT32>(client.bottom));
    if (!size.width || !size.height) return false;
    if (!target_) {
        hr_require(factory_->CreateHwndRenderTarget(D2D1::RenderTargetProperties(),
            D2D1::HwndRenderTargetProperties(window, size), &target_), "Create graphics target");
        ++live_targets_;
        hr_require(target_->CreateSolidColorBrush(D2D1::ColorF(0, 0.0f), &brush_), "Create paint brush");
    } else if (target_->GetPixelSize().width != size.width || target_->GetPixelSize().height != size.height) {
        const HRESULT result = target_->Resize(size);
        if (result == D2DERR_RECREATE_TARGET) {
            discard();
            return begin(window, dpi, background);
        }
        hr_require(result, "Resize graphics target");
    }
    target_->SetDpi(dpi, dpi);
    target_->BeginDraw();
    target_->SetTransform(D2D1::Matrix3x2F::Identity());
    target_->Clear(background);
    for (auto& scene : scenes_) scene.used = false;
    return true;
}

bool Drawing::native_windows(std::span<const NativeWindow> windows) {
    auto result = std::exchange(native_result_override_, S_OK);
    if (result == D2DERR_RECREATE_TARGET) { discard(); return false; }
    hr_require(result, "Begin native window drawing");
    D2D1_SIZE_U required{};
    for (const auto& entry : windows) {
        RECT bounds{}, clip{};
        win32_require(GetClientRect(entry.window, &bounds) != FALSE, "Read native buffer bounds");
        MapWindowPoints(entry.window, target_->GetHwnd(), reinterpret_cast<POINT*>(&bounds), 2);
        if (IntersectRect(&clip, &bounds, &entry.clip)) {
            required.width = std::max(required.width, static_cast<UINT32>(clip.right - clip.left));
            required.height = std::max(required.height, static_cast<UINT32>(clip.bottom - clip.top));
        }
    }
    if (native_size_.width != required.width || native_size_.height != required.height) {
        if (native_buffer_) DeleteObject(std::exchange(native_buffer_, nullptr));
        native_pixels_ = nullptr;
        native_size_ = {};
    }
    for (auto& bitmap : native_bitmaps_) bitmap.used = false;
    for (const auto& entry : windows) {
        RECT bounds{};
        win32_require(GetClientRect(entry.window, &bounds) != FALSE, "Read native drawing bounds");
        MapWindowPoints(entry.window, target_->GetHwnd(), reinterpret_cast<POINT*>(&bounds), 2);
        RECT clip{};
        if (!IntersectRect(&clip, &bounds, &entry.clip)) continue;
        const auto width = static_cast<UINT32>(clip.right - clip.left);
        const auto height = static_cast<UINT32>(clip.bottom - clip.top);
        if (!native_dc_) {
            native_dc_ = CreateCompatibleDC(nullptr);
            win32_require(native_dc_ != nullptr, "Create native drawing context");
        }
        if (native_size_.width < width || native_size_.height < height) {
            if (native_buffer_) DeleteObject(std::exchange(native_buffer_, nullptr));
            native_size_ = required;
            BITMAPINFO info{};
            info.bmiHeader = {sizeof(BITMAPINFOHEADER), static_cast<LONG>(native_size_.width),
                -static_cast<LONG>(native_size_.height), 1, 32, BI_RGB};
            native_buffer_ = CreateDIBSection(native_dc_, &info, DIB_RGB_COLORS, &native_pixels_, nullptr, 0);
            win32_require(native_buffer_ != nullptr, "Create native pixel buffer");
        }
        const auto saved = SaveDC(native_dc_);
        win32_require(saved != 0, "Save native drawing context");
        const auto selected = SelectObject(native_dc_, native_buffer_);
        const auto clipped = IntersectClipRect(native_dc_, 0, 0, width, height);
        const auto positioned = SetViewportOrgEx(native_dc_, bounds.left - clip.left, bounds.top - clip.top, nullptr);
        // Refresh every visible native region. EDIT owns selection, composition and text.
        if (selected && selected != HGDI_ERROR && clipped != ERROR && positioned)
            SendMessageW(entry.window, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(native_dc_), PRF_CLIENT | PRF_ERASEBKGND);
        const auto restored = RestoreDC(native_dc_, saved);
        win32_require(selected && selected != HGDI_ERROR && clipped != ERROR && positioned && restored,
            "Draw native window pixels");
        win32_require(GdiFlush() != FALSE, "Finish native window drawing");
        auto found = std::find_if(native_bitmaps_.begin(), native_bitmaps_.end(),
            [&](const auto& bitmap) { return bitmap.window == entry.window; });
        if (found == native_bitmaps_.end()) {
            native_bitmaps_.push_back({entry.window});
            found = std::prev(native_bitmaps_.end());
        }
        if (found->bitmap && (found->bitmap->GetPixelSize().width != width ||
            found->bitmap->GetPixelSize().height != height)) found->bitmap.Reset();
        if (!found->bitmap) {
            result = target_->CreateBitmap(D2D1::SizeU(width, height), native_pixels_, native_size_.width * 4,
                D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE)),
                &found->bitmap);
        } else result = found->bitmap->CopyFromMemory(nullptr, native_pixels_, native_size_.width * 4);
        if (result == D2DERR_RECREATE_TARGET) { discard(); return false; }
        hr_require(result, "Copy native window pixels");
        found->bounds = clip;
        found->used = true;
    }
    std::erase_if(native_bitmaps_, [](const auto& bitmap) { return !bitmap.used; });
    if (native_bitmaps_.empty()) {
        if (native_dc_) DeleteDC(std::exchange(native_dc_, nullptr));
        if (native_buffer_) DeleteObject(std::exchange(native_buffer_, nullptr));
        native_pixels_ = nullptr;
        native_size_ = {};
    }
    float dpi_x{}, dpi_y{};
    target_->GetDpi(&dpi_x, &dpi_y);
    for (const auto& bitmap : native_bitmaps_) {
        const auto& bounds = bitmap.bounds;
        target_->DrawBitmap(bitmap.bitmap.Get(), D2D1::RectF(bounds.left * 96.0f / dpi_x,
            bounds.top * 96.0f / dpi_y, bounds.right * 96.0f / dpi_x, bounds.bottom * 96.0f / dpi_y),
            1, D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
    }
    return true;
}

void Drawing::present_native(std::span<const HWND> windows) {
    if (!target_) return;
    float dpi_x{}, dpi_y{};
    target_->GetDpi(&dpi_x, &dpi_y);
    for (const auto& bitmap : native_bitmaps_) {
        if (std::find(windows.begin(), windows.end(), bitmap.window) == windows.end()) continue;
        const auto& bounds = bitmap.bounds;
        target_->DrawBitmap(bitmap.bitmap.Get(), D2D1::RectF(bounds.left * 96.0f / dpi_x,
            bounds.top * 96.0f / dpi_y, bounds.right * 96.0f / dpi_x, bounds.bottom * 96.0f / dpi_y),
            1, D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
    }
}
bool Drawing::end() {
    HRESULT result = target_->EndDraw();
    const auto injected = std::exchange(end_result_override_, S_OK);
    if (SUCCEEDED(result) && FAILED(injected)) result = injected;
    if (result == D2DERR_RECREATE_TARGET) {
        discard();
        return false;
    }
    hr_require(result, "Draw window");
    std::erase_if(scenes_, [](const auto& scene) { return !scene.used; });
    if (scenes_.empty()) scene_stroke_.Reset();
    if (present_observer_) present_observer_(target_->GetHwnd());
    return true;
}

void Drawing::discard() {
    scenes_.clear();
    scene_stroke_.Reset();
    while (!bitmaps_.empty()) erase_bitmap(bitmaps_.size() - 1);
    brush_.Reset();
    for (auto& border : field_borders_) border.value.Reset();
    for (auto& border : button_borders_) border.value.Reset();
    native_bitmaps_.clear();
    if (native_dc_) DeleteDC(std::exchange(native_dc_, nullptr));
    if (native_buffer_) DeleteObject(std::exchange(native_buffer_, nullptr));
    native_pixels_ = nullptr;
    native_size_ = {};
    if (target_) --live_targets_;
    target_.Reset();
}
void Drawing::erase_bitmap(std::size_t index) {
    const auto bytes = bitmaps_[index].bytes;
    bitmaps_[index].value.Reset();
    bitmaps_.erase(bitmaps_.begin() + index);
    release_bitmap(bytes);
}
void Drawing::keep_images(std::span<const std::uint64_t> ids) {
    for (std::size_t i = bitmaps_.size(); i; --i)
        if (std::find(ids.begin(), ids.end(), bitmaps_[i - 1].id) == ids.end()) erase_bitmap(i - 1);
}
bool Drawing::image(const std::shared_ptr<const ImagePixels>& pixels, Rect bounds) {
    if (!target_ || bounds.width <= 0 || bounds.height <= 0) return false;
    auto it = std::find_if(bitmaps_.begin(), bitmaps_.end(), [&](const auto& entry) { return entry.id == pixels->id; });
    if (it == bitmaps_.end()) {
        const auto bytes = pixels->accounted;
        // Allocate the cache slot before reserving GPU bytes. No allocation can leak a reservation.
        if (bitmaps_.size() >= ImageLimits::cache_entries) return false;
        bitmaps_.reserve(bitmaps_.size() + 1);
        if (!reserve_bitmap(bytes)) return false;
        const auto start = std::chrono::steady_clock::now();
        Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap;
        const auto result = target_->CreateBitmap(D2D1::SizeU(pixels->size.width, pixels->size.height),
            pixels->pixels.data(), pixels->size.width * 4,
            D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
                96, 96), &bitmap);
        finish_bitmap(bytes, SUCCEEDED(result),
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
        if (FAILED(result)) return false;
        bitmaps_.push_back({pixels->id, bytes, std::move(bitmap)});
        it = std::prev(bitmaps_.end());
    }
    const float scale = std::min(bounds.width / pixels->size.width, bounds.height / pixels->size.height);
    const float width = pixels->size.width * scale, height = pixels->size.height * scale;
    target_->DrawBitmap(it->value.Get(), rectangle({bounds.x + (bounds.width - width) / 2,
        bounds.y + (bounds.height - height) / 2, width, height}), 1, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    return true;
}
void Drawing::release() {
    discard();
    symbol_face_.Reset();
    symbol_family_ = L"";
    caption_format_.Reset();
    numeric_format_.Reset();
    heading_format_.Reset();
    subtitle_format_.Reset();
    strong_format_.Reset();
    small_format_.Reset();
    format_.Reset();
    text_factory_.Reset();
    factory_.Reset();
}

void Drawing::fill(Rect bounds, D2D1_COLOR_F value) {
    brush_->SetColor(value);
    target_->FillRectangle(rectangle(bounds), brush_.Get());
}

void Drawing::outline(Rect bounds, D2D1_COLOR_F value) {
    brush_->SetColor(value);
    target_->DrawRectangle(rectangle(bounds), brush_.Get(), 1.0f);
}

void Drawing::rounded(Rect bounds, D2D1_COLOR_F value, float radius, bool stroke) {
    brush_->SetColor(value);
    const auto shape = D2D1::RoundedRect(rectangle(bounds), radius, radius);
    if (stroke) target_->DrawRoundedRectangle(shape, brush_.Get(), 1);
    else target_->FillRoundedRectangle(shape, brush_.Get());
}

void Drawing::focus_ring(Rect bounds, const Palette& palette, float radius) {
    rounded(bounds, palette.text, radius, true);
    if (bounds.width > 2 && bounds.height > 2)
        rounded({bounds.x + 1, bounds.y + 1, bounds.width - 2, bounds.height - 2},
            palette.background, std::max(0.0f, radius - 1), true);
}

void Drawing::field_frame(Rect bounds, const Palette& palette, bool focused, bool enabled, bool invalid,
    std::optional<D2D1_COLOR_F> fill) {
    const bool winui = palette.style == VisualStyle::winui && !palette.high_contrast;
    const auto radius = style_metrics(palette.style).control_radius;
    rounded(bounds, fill.value_or(palette.field), radius);
    if (!winui || invalid) {
        const auto stroke = invalid ? palette.error : focused ? palette.accent : palette.border;
        rounded(bounds, stroke, radius, true);
        if (palette.style == VisualStyle::winui && palette.high_contrast && bounds.width > 2 && bounds.height > 2)
            rounded({bounds.x + 1, bounds.y + 1, bounds.width - 2, bounds.height - 2}, stroke, radius - 1, true);
        if (winui && invalid && bounds.width > 8 && bounds.height > 2)
            line(bounds.x + 4, bounds.y + bounds.height - 0.5f,
                bounds.x + bounds.width - 4, bounds.y + bounds.height - 0.5f, stroke, 2);
        return;
    }
    const auto tokens = winui_input_strokes(palette.mode);
    const auto outline = argb_color(tokens.outline);
    if (!enabled) {
        rounded(bounds, outline, radius, true);
        return;
    }
    auto& cached = field_borders_[focused ? 1 : 0];
    const auto accent = platform::native_color(palette.accent);
    if (!cached.value || cached.mode != palette.mode || (focused && cached.accent != accent)) {
        const auto elevation = focused ? palette.accent : argb_color(tokens.elevation);
        const D2D1_GRADIENT_STOP stops[]{{0, elevation}, {focused ? 1.0f : 0.5f, elevation}, {1, outline}};
        Microsoft::WRL::ComPtr<ID2D1GradientStopCollection> collection;
        hr_require(target_->CreateGradientStopCollection(stops, 3, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, &collection),
            "Create input elevation stops");
        Microsoft::WRL::ComPtr<ID2D1LinearGradientBrush> next;
        hr_require(target_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(
            D2D1::Point2F(0, 0), D2D1::Point2F(0, 2)), collection.Get(), &next), "Create input elevation brush");
        cached.value = std::move(next);
        cached.mode = palette.mode;
        cached.accent = accent;
        ++created_field_brushes_;
    }
    const float bottom = bounds.y + bounds.height + 0.5f;
    cached.value->SetStartPoint(D2D1::Point2F(0, bottom));
    cached.value->SetEndPoint(D2D1::Point2F(0, bottom - 2));
    target_->DrawRoundedRectangle(D2D1::RoundedRect(rectangle(bounds), radius, radius), cached.value.Get(), 1);
    if (focused && bounds.width > 2 && bounds.height > 2) {
        push_clip({bounds.x - 0.5f, bottom - 2, bounds.width + 1, 2});
        target_->DrawRoundedRectangle(D2D1::RoundedRect(rectangle(
            {bounds.x + 1, bounds.y + 1, bounds.width - 2, bounds.height - 2}), radius - 1, radius - 1),
            cached.value.Get(), 1);
        pop_clip();
    }
}

void Drawing::surface_frame(Rect bounds, const Palette& palette) {
    const auto radius = style_metrics(palette.style).surface_radius;
    rounded(bounds, palette.surface, radius);
    rounded(bounds, palette.border, radius, true);
}

D2D1_COLOR_F Drawing::check_indicator(Rect bounds, const Palette& palette, bool checked, bool enabled, bool mixed,
    bool hovered, bool pressed) {
    const bool marked = checked || mixed;
    const auto mark = !enabled ? palette.disabled : palette.high_contrast ? palette.selection : palette.accent;
    const bool fluent = palette.style == VisualStyle::winui && !palette.high_contrast;
    const auto visual = winui_indicator_brushes(palette.mode, marked, enabled, hovered, pressed);
    const auto label = fluent ? argb_color(visual.text) : enabled ? palette.text : palette.disabled;
    rounded(bounds, fluent ? argb_color(visual.fill) : marked ? mark : palette.field, 4);
    rounded(bounds, fluent ? argb_color(visual.stroke) : !enabled ? palette.disabled : marked ? mark : palette.secondary, 4, true);
    if (!marked) return label;
    const auto ink = fluent ? argb_color(visual.mark) : palette.high_contrast ? palette.selection_text :
        D2D1::ColorF(winui_control_colors(palette.mode).accent_text);
    if (visual_style_ == VisualStyle::winui) {
        symbol(mixed ? Symbol::indeterminate : Symbol::check,
            {bounds.x, bounds.y + (mixed ? 0 : 1), bounds.width, bounds.height}, ink, 12);
    } else if (mixed) {
        line(bounds.x + bounds.width * 0.25f, bounds.y + bounds.height / 2,
            bounds.x + bounds.width * 0.75f, bounds.y + bounds.height / 2, ink, 2);
    } else {
        line(bounds.x + bounds.width * 0.22f, bounds.y + bounds.height * 0.5f,
            bounds.x + bounds.width * 0.44f, bounds.y + bounds.height * 0.72f, ink, 2);
        line(bounds.x + bounds.width * 0.44f, bounds.y + bounds.height * 0.72f,
            bounds.x + bounds.width * 0.78f, bounds.y + bounds.height * 0.28f, ink, 2);
    }
    return label;
}

D2D1_COLOR_F Drawing::radio_indicator(Rect bounds, const Palette& palette, bool checked, bool enabled, bool hovered, bool pressed) {
    const auto radius = std::min(bounds.width, bounds.height) / 2;
    const auto ink = !enabled ? palette.disabled : checked ? palette.accent : palette.secondary;
    const bool fluent = palette.style == VisualStyle::winui && !palette.high_contrast;
    const auto visual = winui_indicator_brushes(palette.mode, checked, enabled, hovered, pressed, true);
    rounded(bounds, fluent ? argb_color(visual.fill) : checked ? ink : palette.field, radius);
    rounded(bounds, fluent ? argb_color(visual.stroke) : ink, radius, true);
    const float diameter = std::min(std::min(bounds.width, bounds.height), winui_radio_dot(enabled, hovered, pressed));
    if (checked && diameter > 0)
        rounded({bounds.x + (bounds.width - diameter) / 2, bounds.y + (bounds.height - diameter) / 2, diameter, diameter},
            fluent ? argb_color(visual.mark) : palette.high_contrast ? palette.selection_text :
                D2D1::ColorF(winui_control_colors(palette.mode).accent_text),
            diameter / 2);
    return fluent ? argb_color(visual.text) : enabled ? palette.text : palette.disabled;
}

void Drawing::chevron(Rect bounds, D2D1_COLOR_F color, bool expanded) {
    if (visual_style_ == VisualStyle::winui) {
        symbol(expanded ? Symbol::chevron_down : Symbol::chevron_right, bounds, color, 12);
        return;
    }
    const auto x = bounds.x + bounds.width / 2, y = bounds.y + bounds.height / 2;
    if (expanded) {
        line(x - 4, y - 2, x, y + 2, color, 1.5f);
        line(x, y + 2, x + 4, y - 2, color, 1.5f);
    } else {
        line(x - 2, y - 4, x + 2, y, color, 1.5f);
        line(x + 2, y, x - 2, y + 4, color, 1.5f);
    }
}

void Drawing::scrollbar_thumb(Rect bounds, const Palette& palette, bool active, bool enabled) {
    if (bounds.width <= 0 || bounds.height <= 0) return;
    rounded(bounds, !enabled ? palette.disabled : active ? palette.text : palette.secondary,
        std::min(bounds.width, bounds.height) / 2);
}

D2D1_COLOR_F Drawing::button_face(Rect bounds, const Palette& palette, ButtonAppearance appearance,
    bool enabled, bool hovered, bool pressed, bool checked) {
    if (palette.high_contrast) {
        const bool selected = enabled && (pressed || checked || appearance == ButtonAppearance::accent);
        rounded(bounds, selected ? palette.selection : palette.surface, 4);
        rounded(bounds, enabled ? palette.text : palette.disabled, 4, true);
        return !enabled ? palette.disabled : selected ? palette.selection_text : palette.text;
    }
    const auto visual = winui_button_brushes(palette.mode, appearance, enabled, hovered, pressed, checked);
    if (visual.fill >> 24) {
        const float inset = visual.accent ? -0.5f : 0.5f;
        rounded({bounds.x + inset, bounds.y + inset, std::max(0.0f, bounds.width - 2 * inset),
            std::max(0.0f, bounds.height - 2 * inset)}, argb_color(visual.fill), visual.accent ? 4.0f : 3.0f);
    }
    if (visual.elevated) {
        auto& cached = button_borders_[visual.accent ? 1 : 0];
        if (!cached.value || cached.mode != palette.mode) {
            const D2D1_GRADIENT_STOP stops[]{{0.33f, argb_color(visual.elevation)}, {1, argb_color(visual.stroke)}};
            Microsoft::WRL::ComPtr<ID2D1GradientStopCollection> collection;
            hr_require(target_->CreateGradientStopCollection(stops, 2, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP,
                &collection), "Create button elevation stops");
            Microsoft::WRL::ComPtr<ID2D1LinearGradientBrush> next;
            hr_require(target_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(
                D2D1::Point2F(0, 0), D2D1::Point2F(0, 3)), collection.Get(), &next), "Create button elevation brush");
            cached.value = std::move(next);
            cached.mode = palette.mode;
        }
        const bool bottom = visual.accent || palette.mode == ThemeMode::light;
        const float edge = bottom ? bounds.y + bounds.height + 0.5f : bounds.y - 0.5f;
        cached.value->SetStartPoint(D2D1::Point2F(0, edge));
        cached.value->SetEndPoint(D2D1::Point2F(0, edge + (bottom ? -3 : 3)));
        target_->DrawRoundedRectangle(D2D1::RoundedRect(rectangle(bounds), 4, 4), cached.value.Get(), 1);
    } else if (visual.stroke >> 24) {
        rounded(bounds, argb_color(visual.stroke), 4, true);
    }
    return argb_color(visual.text);
}

D2D1_COLOR_F Drawing::styled_button_face(Rect bounds, const Palette& palette, ButtonAppearance appearance,
    bool enabled, bool hovered, bool pressed, bool checked, const ButtonStyleValues& values) {
    const bool winui = palette.style == VisualStyle::winui;
    const bool selected = pressed || checked;
    auto ink = !enabled ? palette.disabled : selected ? palette.selection_text : palette.text;
    const bool authored_face = values.background || values.border_brush || values.border_thickness || values.corner_radius;
    if (palette.high_contrast || !authored_face) {
        if (winui) ink = button_face(bounds, palette, appearance, enabled, hovered, pressed, checked);
        else {
            rounded(bounds, selected ? palette.selection : hovered ? palette.hover : palette.surface);
            rounded(bounds, palette.high_contrast ? enabled ? palette.text : palette.disabled : palette.border, 6, true);
        }
    } else {
        auto background = selected ? palette.selection : hovered ? palette.hover : palette.surface;
        auto border = palette.border;
        if (winui) {
            const auto defaults = winui_button_brushes(palette.mode, appearance, enabled, hovered, pressed, checked);
            background = argb_color(defaults.fill);
            border = argb_color(defaults.stroke);
            ink = argb_color(defaults.text);
        }
        if (values.background) background = D2D1::ColorF(values.background->resolve(palette.mode));
        if (values.border_brush) border = D2D1::ColorF(values.border_brush->resolve(palette.mode));
        const float radius = std::min(values.corner_radius.value_or(winui ? 4.0f : 6.0f),
            std::min(bounds.width, bounds.height) / 2);
        const auto edge = values.border_thickness.value_or(Insets{1, 1, 1, 1});
        if (radius == 0) fill(bounds, background);
        else rounded(bounds, background, radius);
        if (edge.left == edge.top && edge.left == edge.right && edge.left == edge.bottom) {
            const float width = std::min(edge.left, std::min(bounds.width, bounds.height) / 2);
            if (width > 0) {
                brush_->SetColor(border);
                const Rect stroke{bounds.x + width / 2, bounds.y + width / 2,
                    std::max(0.0f, bounds.width - width), std::max(0.0f, bounds.height - width)};
                const float inner_radius = std::max(0.0f, radius - width / 2);
                target_->DrawRoundedRectangle(D2D1::RoundedRect(rectangle(stroke), inner_radius, inner_radius), brush_.Get(), width);
            }
        } else {
            // Only aligned square edges can omit the aliased clip without changing boundary pixels.
            const float left = std::min(edge.left, bounds.width), right = std::min(edge.right, bounds.width);
            const float top = std::min(edge.top, bounds.height), bottom = std::min(edge.bottom, bounds.height);
            const Rect edges[]{{bounds.x, bounds.y, left, bounds.height},
                {bounds.x + left, bounds.y, std::max(0.0f, bounds.width - left - right), top},
                {bounds.x + bounds.width - right, bounds.y, right, bounds.height},
                {bounds.x + left, bounds.y + bounds.height - bottom, std::max(0.0f, bounds.width - left - right), bottom}};
            D2D1_MATRIX_3X2_F transform{};
            float dpi_x{}, dpi_y{};
            if (radius == 0) {
                target_->GetTransform(&transform);
                target_->GetDpi(&dpi_x, &dpi_y);
            }
            const auto aligned = [](float value) { return value == std::round(value); };
            const auto rectangular = [&](Rect edge) {
                return radius == 0 && transform._11 == 1 && transform._22 == 1 && transform._12 == 0 && transform._21 == 0 &&
                    aligned((edge.x + transform._31) * dpi_x / 96) &&
                    aligned((edge.y + transform._32) * dpi_y / 96) &&
                    aligned((edge.x + edge.width + transform._31) * dpi_x / 96) &&
                    aligned((edge.y + edge.height + transform._32) * dpi_y / 96);
            };
            for (const auto& clip : edges) if (clip.width > 0 && clip.height > 0) {
                if (rectangular(clip)) fill(clip, border);
                else {
                    push_clip(clip);
                    rounded(bounds, border, radius);
                    pop_clip();
                }
            }
        }
    }
    if (!palette.high_contrast && values.foreground) ink = D2D1::ColorF(values.foreground->resolve(palette.mode));
    return ink;
}

void Drawing::styled_surface(Rect bounds, const Palette& palette, const PartStyleValues& values,
    D2D1_COLOR_F background, D2D1_COLOR_F border, float radius, Insets thickness) {
    if (bounds.width <= 0 || bounds.height <= 0) return;
    if (!palette.high_contrast) {
        if (values.background) background = D2D1::ColorF(values.background->resolve(palette.mode));
        if (values.border_brush) border = D2D1::ColorF(values.border_brush->resolve(palette.mode));
        radius = values.corner_radius.value_or(radius);
        thickness = values.border_thickness.value_or(thickness);
    }
    radius = std::min(radius, std::min(bounds.width, bounds.height) / 2);
    if (background.a > 0) rounded(bounds, background, radius);
    const float left = std::min(thickness.left, bounds.width), right = std::min(thickness.right, bounds.width);
    const float top = std::min(thickness.top, bounds.height), bottom = std::min(thickness.bottom, bounds.height);
    const Rect edges[]{{bounds.x, bounds.y, left, bounds.height},
        {bounds.x + left, bounds.y, std::max(0.0f, bounds.width - left - right), top},
        {bounds.x + bounds.width - right, bounds.y, right, bounds.height},
        {bounds.x + left, bounds.y + bounds.height - bottom, std::max(0.0f, bounds.width - left - right), bottom}};
    for (const auto& edge : edges) if (edge.width > 0 && edge.height > 0) {
        push_clip(edge);
        rounded(bounds, border, radius);
        pop_clip();
    }
}

void Drawing::styled_toggle(const Toggle& toggle, Rect bounds, const Palette& palette, bool enabled,
    IDWriteTextLayout* label, bool focus_visible) {
    const PartStyleValues empty;
    const auto* root = toggle.effective_style_values(StylePart::root);
    const auto* indicator = toggle.effective_style_values(StylePart::indicator);
    const auto* mark = toggle.effective_style_values(StylePart::mark);
    const auto* text = toggle.effective_style_values(StylePart::label);
    const bool winui = palette.style == VisualStyle::winui;
    const auto indicator_box = toggle.indicator_bounds(bounds);
    auto ink = !enabled ? palette.disabled : toggle.pressed() ? palette.selection_text : palette.text;
    const auto root_fill = !winui && (toggle.hovered() || toggle.pressed()) ?
        toggle.pressed() ? palette.selection : palette.hover : D2D1::ColorF(0, 0.0f);
    styled_surface(bounds, palette, root ? *root : empty, root_fill,
        ink, 0, {});
    const auto fill = toggle.checked() ? (enabled ? palette.high_contrast ? palette.selection : palette.accent : palette.disabled) : palette.field;
    const auto border = enabled ? palette.high_contrast ? palette.text : palette.accent : palette.disabled;
    styled_surface(indicator_box, palette, indicator ? *indicator : empty, fill, border, winui ? 4.0f : 3.0f, {1, 1, 1, 1});
    if (toggle.checked()) {
        auto mark_ink = palette.high_contrast && enabled ? palette.selection_text : palette.background;
        if (!palette.high_contrast && mark && mark->foreground)
            mark_ink = D2D1::ColorF(mark->foreground->resolve(palette.mode));
        const auto b = toggle.mark_bounds(bounds);
        if (b.width > 0 && b.height > 0) {
            push_clip(indicator_box);
            line(b.x + b.width * 2 / 9, b.y + b.height / 2,
                b.x + b.width * 4 / 9, b.y + b.height * 13 / 18, mark_ink, 2);
            line(b.x + b.width * 4 / 9, b.y + b.height * 13 / 18,
                b.x + b.width * 7 / 9, b.y + b.height * 5 / 18, mark_ink, 2);
            pop_clip();
        }
    }
    if (!palette.high_contrast && text && text->foreground)
        ink = D2D1::ColorF(text->foreground->resolve(palette.mode));
    const auto content = toggle.label_bounds(bounds);
    push_clip(content);
    text_layout(label, content, ink);
    pop_clip();
    if (focus_visible) {
        const Rect face{bounds.x + 1, bounds.y + 1, std::max(0.0f, bounds.width - 2), std::max(0.0f, bounds.height - 2)};
        if (winui) focus_ring(face, palette);
        else outline(face, palette.high_contrast ? palette.text : palette.accent);
    }
}

void Drawing::line(float x1, float y1, float x2, float y2, D2D1_COLOR_F value, float thickness) {
    brush_->SetColor(value);
    target_->DrawLine(D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2), brush_.Get(), thickness);
}

void Drawing::icon(Rect box, D2D1_COLOR_F value, bool folder) {
    if (visual_style_ == VisualStyle::winui) {
        symbol(folder ? Symbol::folder : Symbol::document, box, value, std::min(box.width, box.height));
        return;
    }
    if (folder) {
        rounded({box.x, box.y + 2, box.width * 0.5f, 6}, value, 1.5f);
        rounded({box.x, box.y + 5, box.width, box.height - 6}, value, 2);
    } else {
        rounded({box.x + 2, box.y + 1, box.width - 4, box.height - 2}, value, 2, true);
        line(box.x + 6, box.y + 7, box.x + box.width - 5, box.y + 7, value);
        line(box.x + 6, box.y + 11, box.x + box.width - 5, box.y + 11, value);
    }
}

void Drawing::search_icon(Rect box, D2D1_COLOR_F value) {
    if (visual_style_ == VisualStyle::winui) {
        symbol(Symbol::search, box, value);
        return;
    }
    brush_->SetColor(value);
    target_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(box.x + 7, box.y + 7), 5, 5), brush_.Get(), 1.5f);
    line(box.x + 11, box.y + 11, box.x + 16, box.y + 16, value, 1.5f);
}

void Drawing::caption_button(Rect bounds, ButtonIcon icon, const Palette& palette,
    bool active, bool enabled, bool hovered, bool pressed, bool focused) {
    if (!caption_format_ && palette.style != VisualStyle::winui) {
        hr_require(text_factory_->CreateTextFormat(L"Segoe MDL2 Assets", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 10, L"", &caption_format_),
            "Create caption glyph format");
        hr_require(caption_format_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER), "Center caption glyph");
        hr_require(caption_format_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER), "Align caption glyph");
        hr_require(caption_format_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP), "Set caption glyph wrapping");
    }
    const bool hot = enabled && hovered;
    const bool down = hot && pressed;
    const bool dark = palette.background.r + palette.background.g + palette.background.b < 1.5f;
    auto background = palette.background;
    auto ink = palette.high_contrast ? palette.text : D2D1::ColorF(dark ? 0xffffff : 0x000000);
    if (!enabled || (!active && !hot)) ink = palette.high_contrast ? palette.disabled :
        D2D1::ColorF(dark ? 0x999999 : 0x777777);
    if (hot) {
        if (palette.high_contrast) {
            background = palette.selection;
            ink = palette.selection_text;
        } else if (icon == ButtonIcon::close) {
            background = D2D1::ColorF(down ? 0xc50f1f : 0xe81123);
            ink = D2D1::ColorF(0xffffff, down ? 0.7f : 1.0f);
        } else {
            const float alpha = down ? 0.06f : 0.10f;
            const float overlay = dark ? 1.0f : 0.0f;
            background = D2D1::ColorF(background.r * (1 - alpha) + overlay * alpha,
                background.g * (1 - alpha) + overlay * alpha, background.b * (1 - alpha) + overlay * alpha);
            if (down) ink.a = 0.7f;
        }
    }
    fill(bounds, background);
    const wchar_t glyph = icon == ButtonIcon::minimize ? L'\ue921' : icon == ButtonIcon::maximize ? L'\ue922' :
        icon == ButtonIcon::restore ? L'\ue923' : L'\ue8bb';
    if (palette.style == VisualStyle::winui) {
        symbol(icon == ButtonIcon::close ? Symbol::caption_close : button_symbol(icon), bounds, ink, 10);
    } else {
        brush_->SetColor(ink);
        target_->DrawText(&glyph, 1, caption_format_.Get(), rectangle(bounds), brush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
    if (focused) outline({bounds.x + 2.5f, bounds.y + 2.5f, std::max(0.0f, bounds.width - 5),
        std::max(0.0f, bounds.height - 5)}, ink);
}

void Drawing::button_icon(Rect box, D2D1_COLOR_F color, ButtonIcon icon) {
    if (visual_style_ == VisualStyle::winui) {
        symbol(button_symbol(icon), box, color, std::min(box.width, box.height));
        return;
    }
    const auto stroke = [&](float x1, float y1, float x2, float y2) {
        line(box.x + x1 * box.width / 16, box.y + y1 * box.height / 16,
            box.x + x2 * box.width / 16, box.y + y2 * box.height / 16, color, 1.5f);
    };
    if (icon == ButtonIcon::menu) {
        stroke(2, 4, 14, 4); stroke(2, 8, 14, 8); stroke(2, 12, 14, 12);
    } else if (icon == ButtonIcon::home) {
        stroke(1, 7, 8, 1); stroke(8, 1, 15, 7); stroke(3, 6, 3, 14);
        stroke(3, 14, 13, 14); stroke(13, 14, 13, 6); stroke(6, 14, 6, 9); stroke(6, 9, 10, 9); stroke(10, 9, 10, 14);
    } else if (icon == ButtonIcon::folder) {
        stroke(1, 3, 6, 3); stroke(6, 3, 8, 5); stroke(8, 5, 15, 5);
        stroke(15, 5, 15, 13); stroke(15, 13, 1, 13); stroke(1, 13, 1, 3);
    } else if (icon == ButtonIcon::library) {
        stroke(2, 2, 2, 14); stroke(6, 2, 6, 14); stroke(10, 2, 14, 14);
        stroke(1, 14, 15, 14);
    } else if (icon == ButtonIcon::history) {
        brush_->SetColor(color);
        target_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(box.x + box.width / 2, box.y + box.height / 2),
            box.width * 0.375f, box.height * 0.375f), brush_.Get(), 1.5f);
        stroke(8, 4, 8, 8); stroke(8, 8, 11, 10);
    } else if (icon == ButtonIcon::bookmark) {
        stroke(4, 2, 12, 2); stroke(12, 2, 12, 14); stroke(12, 14, 8, 11);
        stroke(8, 11, 4, 14); stroke(4, 14, 4, 2);
    } else if (icon == ButtonIcon::drive) {
        stroke(4, 3, 12, 3); stroke(12, 3, 14, 9); stroke(14, 9, 14, 13);
        stroke(14, 13, 2, 13); stroke(2, 13, 2, 9); stroke(2, 9, 4, 3);
        stroke(2, 9, 14, 9); stroke(10, 11, 12, 11);
    } else if (icon == ButtonIcon::settings) {
        stroke(1, 4, 15, 4); stroke(1, 12, 15, 12);
        stroke(5, 1, 5, 7); stroke(11, 9, 11, 15);
    } else if (icon == ButtonIcon::search) {
        brush_->SetColor(color);
        target_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(box.x + box.width * 0.4f, box.y + box.height * 0.4f),
            box.width * 0.3f, box.height * 0.3f), brush_.Get(), 1.5f);
        stroke(10, 10, 15, 15);
    } else if (icon == ButtonIcon::minimize) {
        stroke(3, 8, 13, 8);
    } else if (icon == ButtonIcon::close) {
        stroke(3, 3, 13, 13); stroke(3, 13, 13, 3);
    } else if (icon == ButtonIcon::maximize || icon == ButtonIcon::restore) {
        stroke(3, 3, 13, 3); stroke(13, 3, 13, 13); stroke(13, 13, 3, 13); stroke(3, 13, 3, 3);
        if (icon == ButtonIcon::restore) { stroke(5, 1, 15, 1); stroke(15, 1, 15, 11); }
    } else if (icon == ButtonIcon::more) {
        stroke(3, 8, 4, 8); stroke(7, 8, 8, 8); stroke(11, 8, 12, 8);
    } else if (icon == ButtonIcon::back || icon == ButtonIcon::forward) {
        const float tip = icon == ButtonIcon::back ? 2.0f : 14.0f;
        const float tail = 16 - tip;
        const float shoulder = icon == ButtonIcon::back ? 7.0f : 9.0f;
        stroke(tip, 8, tail, 8); stroke(tip, 8, shoulder, 3); stroke(tip, 8, shoulder, 13);
    } else if (icon == ButtonIcon::add) {
        stroke(8, 3, 8, 13); stroke(3, 8, 13, 8);
    } else if (icon == ButtonIcon::up) {
        stroke(8, 2, 8, 14); stroke(8, 2, 3, 7); stroke(8, 2, 13, 7);
    } else if (icon == ButtonIcon::split) {
        rounded({box.x + 1, box.y + 2, box.width - 2, box.height - 4}, color, 1, true);
        stroke(8, 2, 8, 14);
    } else if (icon == ButtonIcon::theme) {
        brush_->SetColor(color);
        target_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(box.x + box.width / 2, box.y + box.height / 2),
            box.width / 4, box.height / 4), brush_.Get(), 1.5f);
        stroke(8, 0, 8, 2); stroke(8, 14, 8, 16);
        stroke(0, 8, 2, 8); stroke(14, 8, 16, 8);
        stroke(2, 2, 3, 3); stroke(13, 13, 14, 14);
        stroke(2, 14, 3, 13); stroke(13, 3, 14, 2);
    } else if (icon == ButtonIcon::refresh) {
        // An open circular arrow avoids a font-dependent symbol.
        constexpr float points[][2]{{13, 5}, {11, 2}, {7, 1}, {3, 3}, {1, 7},
            {2, 11}, {5, 14}, {9, 14}, {12, 12}};
        for (std::size_t i = 1; i < std::size(points); ++i)
            stroke(points[i - 1][0], points[i - 1][1], points[i][0], points[i][1]);
        stroke(13, 1, 13, 5); stroke(9, 5, 13, 5);
    }
}

void Drawing::heading(std::wstring_view value, Rect bounds, D2D1_COLOR_F value_color) {
    brush_->SetColor(value_color);
    target_->DrawText(value.data(), static_cast<UINT32>(std::min(value.size(),
        static_cast<size_t>(std::numeric_limits<UINT32>::max()))), heading_format_.Get(),
        rectangle(bounds), brush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void Drawing::text(std::wstring_view value, Rect bounds, D2D1_COLOR_F value_color, bool small_text) {
    brush_->SetColor(value_color);
    target_->DrawText(value.data(), static_cast<UINT32>(std::min(value.size(),
        static_cast<size_t>(std::numeric_limits<UINT32>::max()))),
        small_text ? small_format_.Get() : format_.Get(), rectangle(bounds), brush_.Get(),
        D2D1_DRAW_TEXT_OPTIONS_CLIP);
}
void Drawing::cell_text(std::wstring_view value, Rect bounds, D2D1_COLOR_F color, bool numeric) {
    if (!numeric) { text(value, bounds, color, true); return; }
    if (!numeric_format_) {
        hr_require(text_factory_->CreateTextFormat(edit_font_family(), nullptr, DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, VisualMetrics::caption_size, L"", &numeric_format_), "Create numeric text format");
        numeric_format_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        numeric_format_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        numeric_format_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
        Microsoft::WRL::ComPtr<IDWriteInlineObject> ellipsis;
        hr_require(text_factory_->CreateEllipsisTrimmingSign(numeric_format_.Get(), &ellipsis), "Create numeric ellipsis");
        DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
        hr_require(numeric_format_->SetTrimming(&trimming, ellipsis.Get()), "Set numeric trimming");
    }
    brush_->SetColor(color);
    target_->DrawText(value.data(), static_cast<UINT32>(value.size()), numeric_format_.Get(),
        rectangle(bounds), brush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

Microsoft::WRL::ComPtr<IDWriteTextLayout> Drawing::layout(std::wstring_view value, TextStyle style, Size& measured,
    float wrap_width, std::size_t maximum_lines) {
    Microsoft::WRL::ComPtr<IDWriteTextLayout> result;
    auto* format = style == TextStyle::body_strong ? strong_format_.Get() :
        style == TextStyle::subtitle ? subtitle_format_.Get() : style == TextStyle::heading ? heading_format_.Get() :
        style == TextStyle::caption ? small_format_.Get() : format_.Get();
    hr_require(text_factory_->CreateTextLayout(value.data(), static_cast<UINT32>(std::min(value.size(),
        static_cast<size_t>(std::numeric_limits<UINT32>::max()))), format, 10000000, 10000000, &result),
        "Measure control text");
    ++created_text_layouts_;
    if (wrap_width > 0) {
        hr_require(result->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP), "Wrap control text");
        hr_require(result->SetMaxWidth(wrap_width), "Constrain wrapped control text");
        hr_require(result->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR), "Align wrapped control text");
    }
    DWRITE_TEXT_METRICS metrics{};
    hr_require(result->GetMetrics(&metrics), "Read control text metrics");
    measured = {std::ceil(metrics.widthIncludingTrailingWhitespace), std::ceil(metrics.height)};
    if (wrap_width > 0 && maximum_lines) {
        UINT32 count{};
        const HRESULT query = result->GetLineMetrics(nullptr, 0, &count);
        if (query != E_NOT_SUFFICIENT_BUFFER) hr_require(query, "Count wrapped text lines");
        std::vector<DWRITE_LINE_METRICS> lines(count);
        if (count) hr_require(result->GetLineMetrics(lines.data(), count, &count), "Measure wrapped text lines");
        float height{};
        for (std::size_t i = 0; i < std::min(maximum_lines, lines.size()); ++i) height += lines[i].height;
        measured.height = std::min(measured.height, std::ceil(height));
    }
    return result;
}
void Drawing::text_layout(IDWriteTextLayout* layout, Rect bounds, D2D1_COLOR_F color) {
    if (!layout || bounds.width <= 0 || bounds.height <= 0) return;
    if (layout->GetMaxWidth() != bounds.width)
        hr_require(layout->SetMaxWidth(bounds.width), "Constrain control text width");
    if (layout->GetMaxHeight() != bounds.height)
        hr_require(layout->SetMaxHeight(bounds.height), "Constrain control text height");
    brush_->SetColor(color);
    target_->DrawTextLayout(D2D1::Point2F(bounds.x, bounds.y), layout, brush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void Drawing::push_clip(Rect bounds) {
    target_->PushAxisAlignedClip(rectangle(bounds), D2D1_ANTIALIAS_MODE_ALIASED);
}
void Drawing::pop_clip() { target_->PopAxisAlignedClip(); }
void Drawing::origin(float x, float y) {
    target_->SetTransform(D2D1::Matrix3x2F::Translation(x, y));
}

}
