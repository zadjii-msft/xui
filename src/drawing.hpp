#pragma once

#include "xui/core.hpp"
#include "xui/theme.hpp"
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <string_view>

namespace xui {

struct Palette {
    D2D1_COLOR_F background, text, secondary, selection, selection_text, border;
    D2D1_COLOR_F surface, field, hover, accent, folder, file, error;
    bool high_contrast{};
    static Palette system(ThemeMode mode = ThemeMode::dark);
};

class Drawing {
public:
    void initialize();
    void initialize(const Drawing& resources);
    bool begin(HWND window, float dpi, D2D1_COLOR_F background);
    bool end();
    void discard();
    void fill(Rect bounds, D2D1_COLOR_F color);
    void outline(Rect bounds, D2D1_COLOR_F color);
    void rounded(Rect bounds, D2D1_COLOR_F color, float radius = VisualMetrics::radius, bool stroke = false);
    void line(float x1, float y1, float x2, float y2, D2D1_COLOR_F color, float thickness = 1);
    void icon(Rect bounds, D2D1_COLOR_F color, bool folder);
    void search_icon(Rect bounds, D2D1_COLOR_F color);
    void heading(std::wstring_view value, Rect bounds, D2D1_COLOR_F color);
    void text(std::wstring_view value, Rect bounds, D2D1_COLOR_F color, bool small_text = false);
    void push_clip(Rect bounds);
    void pop_clip();
private:
    Microsoft::WRL::ComPtr<ID2D1Factory> factory_;
    Microsoft::WRL::ComPtr<IDWriteFactory> text_factory_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> format_, small_format_, heading_format_;
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> target_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush_;
};

}
