#include "drawing.hpp"
#include "platform.hpp"
#include "images.hpp"
#include <algorithm>
#include <limits>
#include <cmath>
#include <utility>
#include <chrono>

namespace xui {
thread_local std::size_t Drawing::live_targets_{};
thread_local std::size_t Drawing::created_text_layouts_{};
thread_local HRESULT Drawing::end_result_override_{S_OK};
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
    return true;
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
    return true;
}

void Drawing::discard() {
    while (!bitmaps_.empty()) erase_bitmap(bitmaps_.size() - 1);
    brush_.Reset();
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
