#pragma once

#include "xui/core.hpp"
#include "xui/theme.hpp"
#include "xui/controls.hpp"
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <string_view>

namespace xui {
struct ImagePixels;

struct Palette {
    D2D1_COLOR_F background, text, secondary, selection, selection_text, border;
    D2D1_COLOR_F surface, field, hover, accent, folder, file, error;
    bool high_contrast{};
    D2D1_COLOR_F disabled{};
    static Palette system(ThemeMode mode = ThemeMode::dark);
};

class Drawing {
public:
    Drawing() = default;
    ~Drawing() { discard(); }
    Drawing(const Drawing&) = delete;
    Drawing& operator=(const Drawing&) = delete;
    static std::size_t live_targets();
    static std::size_t created_text_layouts() { return created_text_layouts_; }
    void initialize();
    bool begin(HWND window, float dpi, D2D1_COLOR_F background);
    bool end();
    void discard();
    void release();
    void fill(Rect bounds, D2D1_COLOR_F color);
    void outline(Rect bounds, D2D1_COLOR_F color);
    void rounded(Rect bounds, D2D1_COLOR_F color, float radius = VisualMetrics::radius, bool stroke = false);
    void line(float x1, float y1, float x2, float y2, D2D1_COLOR_F color, float thickness = 1);
    void icon(Rect bounds, D2D1_COLOR_F color, bool folder);
    void search_icon(Rect bounds, D2D1_COLOR_F color);
    void heading(std::wstring_view value, Rect bounds, D2D1_COLOR_F color);
    void text(std::wstring_view value, Rect bounds, D2D1_COLOR_F color, bool small_text = false);
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout(std::wstring_view value, TextStyle style, Size& measured);
    void text_layout(IDWriteTextLayout* layout, Rect bounds, D2D1_COLOR_F color);
    void cell_text(std::wstring_view value, Rect bounds, D2D1_COLOR_F color, bool numeric);
    void push_clip(Rect bounds);
    void pop_clip();
    void origin(float x, float y);
    bool image(const std::shared_ptr<const ImagePixels>& pixels, Rect bounds);
    void keep_images(std::span<const std::uint64_t> ids);
private:
    friend struct DrawingTestAccess;
    static thread_local HRESULT end_result_override_;
    static thread_local std::size_t live_targets_;
    static thread_local std::size_t created_text_layouts_;
    Microsoft::WRL::ComPtr<ID2D1Factory> factory_;
    Microsoft::WRL::ComPtr<IDWriteFactory> text_factory_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> format_, small_format_, heading_format_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> numeric_format_;
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> target_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush_;
    struct Bitmap {
        std::uint64_t id{};
        std::size_t bytes{};
        Microsoft::WRL::ComPtr<ID2D1Bitmap> value;
    };
    std::vector<Bitmap> bitmaps_;
    void erase_bitmap(std::size_t index);
};

}
