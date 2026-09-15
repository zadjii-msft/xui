#include "drawing.hpp"
#include "platform.hpp"
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
void Drawing::collection_row(const CollectionRow& row, bool selected, bool focused, bool enabled, const Palette& palette, bool hovered,
    const std::shared_ptr<const ImagePixels>& pixels, bool trailing_shortcut_badges) {
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
                text(row.content.secondary, {right - badge_width + 7, b.y, badge_width - 14, b.height}, palette.secondary, true);
                right -= badge_width + 6;
            }
            const float text_left = left + (visual ? 28 : 0);
            text(row.content.primary, {text_left, b.y, std::max(0.0f, right - text_left), b.height}, ink);
            if (row.expandable) {
                const float x = b.x + b.width - 21, y = b.y + b.height / 2;
                if (row.expanded) { line(x - 4, y - 2, x, y + 2, ink, 1.5f); line(x, y + 2, x + 4, y - 2, ink, 1.5f); }
                else { line(x - 2, y - 4, x + 2, y, ink, 1.5f); line(x + 2, y, x - 2, y + 4, ink, 1.5f); }
            }
        }
        if (focused) rounded(face, palette.accent, 5, true);
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
    if (selected || hovered || row.group) fill({b.x + 1, b.y + 1, std::max(0.0f, b.width - 2), b.height - 2},
        selected ? palette.selection : hovered ? palette.hover : palette.surface);
    float left = b.x + 10 + std::min(static_cast<float>(row.depth) * 20, b.width / 3);
    if (row.content.checked) {
        text(*row.content.checked ? L"✓" : L"○", {left, b.y, 22, b.height}, ink); left += 24;
    }
    if (row.expandable && !row.content.submenu) {
        text(row.expanded ? L"\u25be" : L"\u25b8", {left, b.y, 22, b.height}, ink); left += 24;
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
        !enabled ? palette.disabled : selected ? palette.selection_text : palette.secondary, true);
    if (row.content.progress && std::isfinite(*row.content.progress)) {
        const Rect track{left, b.y + b.height - 4, width, 2}; fill(track, palette.border);
        fill({track.x, track.y, track.width * static_cast<float>(std::clamp(*row.content.progress, 0.0, 1.0)), track.height}, ink);
    }
    if (action_visible) {
        const Rect action{b.x + b.width - 70, b.y + 8, 64, std::max(0.0f, b.height - 16)};
        rounded(action, palette.border, 4, true); text(row.content.action, {action.x + 5, action.y, action.width - 10, action.height}, ink, true);
    }
    if (row.content.submenu) text(L"›", {b.x + b.width - 28, b.y, 20, b.height}, ink);
    if (focused || (hovered && palette.high_contrast))
        outline({b.x + 1, b.y + 1, std::max(0.0f, b.width - 2), b.height - 2}, palette.accent);
}
thread_local std::size_t Drawing::live_targets_{};
thread_local std::size_t Drawing::created_text_layouts_{};
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
}

Palette Palette::system(ThemeMode mode) {
    HIGHCONTRASTW contrast{sizeof(contrast)};
    win32_require(SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) != 0,
                  "Read high contrast settings");
    if (mode == ThemeMode::high_contrast || (contrast.dwFlags & HCF_HIGHCONTRASTON))
        return {color(COLOR_WINDOW), color(COLOR_WINDOWTEXT), color(COLOR_WINDOWTEXT),
                color(COLOR_HIGHLIGHT), color(COLOR_HIGHLIGHTTEXT), color(COLOR_WINDOWTEXT),
                color(COLOR_WINDOW), color(COLOR_WINDOW), color(COLOR_WINDOW),
                color(COLOR_HIGHLIGHT), color(COLOR_WINDOWTEXT), color(COLOR_WINDOWTEXT),
                color(COLOR_WINDOWTEXT), true, color(COLOR_GRAYTEXT)};
    const auto theme = theme_colors(mode);
    return {D2D1::ColorF(theme.background), D2D1::ColorF(theme.text), D2D1::ColorF(theme.secondary),
            D2D1::ColorF(theme.selection), D2D1::ColorF(theme.selection_text), D2D1::ColorF(theme.border),
            D2D1::ColorF(theme.surface), D2D1::ColorF(theme.field), D2D1::ColorF(theme.hover),
            D2D1::ColorF(theme.accent), D2D1::ColorF(theme.folder), D2D1::ColorF(theme.file),
            D2D1::ColorF(theme.error), false, D2D1::ColorF(theme.secondary)};
}

void Drawing::initialize() {
    hr_require(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory_.GetAddressOf()),
               "Create Direct2D factory");
    hr_require(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
               reinterpret_cast<IUnknown**>(text_factory_.GetAddressOf())), "Create DirectWrite factory");
    for (auto entry : {std::pair{std::addressof(format_), VisualMetrics::body_size},
                       std::pair{std::addressof(small_format_), VisualMetrics::caption_size},
                       std::pair{std::addressof(heading_format_), VisualMetrics::heading_size}}) {
        hr_require(text_factory_->CreateTextFormat(L"Segoe UI", nullptr,
            entry.first == std::addressof(heading_format_) ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, entry.second, L"",
            entry.first->GetAddressOf()), "Create text format");
        hr_require((*entry.first)->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP), "Set text wrapping");
        hr_require((*entry.first)->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER),
                   "Set text alignment");
        Microsoft::WRL::ComPtr<IDWriteInlineObject> ellipsis;
        hr_require(text_factory_->CreateEllipsisTrimmingSign(entry.first->Get(), &ellipsis),
                   "Create text ellipsis");
        DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
        hr_require((*entry.first)->SetTrimming(&trimming, ellipsis.Get()), "Set text trimming");
    }
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
    caption_format_.Reset();
    numeric_format_.Reset();
    heading_format_.Reset();
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

void Drawing::line(float x1, float y1, float x2, float y2, D2D1_COLOR_F value, float thickness) {
    brush_->SetColor(value);
    target_->DrawLine(D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2), brush_.Get(), thickness);
}

void Drawing::icon(Rect box, D2D1_COLOR_F value, bool folder) {
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
    brush_->SetColor(value);
    target_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(box.x + 7, box.y + 7), 5, 5), brush_.Get(), 1.5f);
    line(box.x + 11, box.y + 11, box.x + 16, box.y + 16, value, 1.5f);
}

void Drawing::caption_button(Rect bounds, ButtonIcon icon, const Palette& palette,
    bool active, bool enabled, bool hovered, bool pressed, bool focused) {
    if (!caption_format_) {
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
    brush_->SetColor(ink);
    target_->DrawText(&glyph, 1, caption_format_.Get(), rectangle(bounds), brush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    if (focused) outline({bounds.x + 2.5f, bounds.y + 2.5f, std::max(0.0f, bounds.width - 5),
        std::max(0.0f, bounds.height - 5)}, ink);
}

void Drawing::button_icon(Rect box, D2D1_COLOR_F color, ButtonIcon icon) {
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
        hr_require(text_factory_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
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

Microsoft::WRL::ComPtr<IDWriteTextLayout> Drawing::layout(std::wstring_view value, TextStyle style, Size& measured) {
    Microsoft::WRL::ComPtr<IDWriteTextLayout> result;
    auto* format = style == TextStyle::heading ? heading_format_.Get() :
        style == TextStyle::caption ? small_format_.Get() : format_.Get();
    hr_require(text_factory_->CreateTextLayout(value.data(), static_cast<UINT32>(std::min(value.size(),
        static_cast<size_t>(std::numeric_limits<UINT32>::max()))), format, 10000000, 10000000, &result),
        "Measure control text");
    ++created_text_layouts_;
    DWRITE_TEXT_METRICS metrics{};
    hr_require(result->GetMetrics(&metrics), "Read control text metrics");
    measured = {std::ceil(metrics.widthIncludingTrailingWhitespace), std::ceil(metrics.height)};
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
