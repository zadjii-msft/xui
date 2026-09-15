#pragma once

#include "xui/core.hpp"
#include "xui/theme.hpp"
#include "xui/controls.hpp"
#include "xui/collections.hpp"
#include "xui/vector_canvas.hpp"
#include "symbols.hpp"
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
    VisualStyle style{};
    ThemeMode mode{};
    static Palette system(ThemeMode mode = ThemeMode::dark, VisualStyle style = VisualStyle::classic);
    D2D1_COLOR_F input_fill(bool enabled, bool focused, bool hovered, bool on_surface) const;
};

class Drawing {
public:
    Drawing() = default;
    ~Drawing() { discard(); }
    Drawing(const Drawing&) = delete;
    Drawing& operator=(const Drawing&) = delete;
    static std::size_t live_targets();
    static std::size_t created_text_layouts() { return created_text_layouts_; }
    static std::size_t created_field_brushes() { return created_field_brushes_; }
    static std::size_t created_symbol_faces() { return created_symbol_faces_; }
    void initialize(VisualStyle style = VisualStyle::classic);
    void set_visual_style(VisualStyle style);
    const wchar_t* edit_font_family() const { return variable_font_ ? L"Segoe UI Variable Text" : L"Segoe UI"; }
    const wchar_t* symbol_font_family() const { return symbol_family_; }
    bool has_symbol(Symbol symbol) const;
    void symbol(Symbol symbol, Rect bounds, D2D1_COLOR_F color, float size = 16);
    bool begin(HWND window, float dpi, D2D1_COLOR_F background);
    struct NativeWindow { HWND window; RECT clip; };
    bool native_windows(std::span<const NativeWindow> windows);
    void present_native(std::span<const HWND> windows);
    bool end();
    void discard();
    void release();
    void fill(Rect bounds, D2D1_COLOR_F color);
    void outline(Rect bounds, D2D1_COLOR_F color);
    void rounded(Rect bounds, D2D1_COLOR_F color, float radius = VisualMetrics::radius, bool stroke = false);
    void focus_ring(Rect bounds, const Palette& palette, float radius = 4);
    void field_frame(Rect bounds, const Palette& palette, bool focused, bool enabled, bool invalid = false,
        std::optional<D2D1_COLOR_F> fill = {});
    void surface_frame(Rect bounds, const Palette& palette);
    D2D1_COLOR_F check_indicator(Rect bounds, const Palette& palette, bool checked, bool enabled, bool mixed = false,
        bool hovered = false, bool pressed = false);
    D2D1_COLOR_F radio_indicator(Rect bounds, const Palette& palette, bool checked, bool enabled, bool hovered = false, bool pressed = false);
    void chevron(Rect bounds, D2D1_COLOR_F color, bool expanded);
    void scrollbar_thumb(Rect bounds, const Palette& palette, bool active, bool enabled = true);
    D2D1_COLOR_F button_face(Rect bounds, const Palette& palette, ButtonAppearance appearance,
        bool enabled, bool hovered, bool pressed, bool checked);
    void line(float x1, float y1, float x2, float y2, D2D1_COLOR_F color, float thickness = 1);
    void icon(Rect bounds, D2D1_COLOR_F color, bool folder);
    void search_icon(Rect bounds, D2D1_COLOR_F color);
    void button_icon(Rect bounds, D2D1_COLOR_F color, ButtonIcon icon);
    void caption_button(Rect bounds, ButtonIcon icon, const Palette& palette,
        bool active, bool enabled, bool hovered, bool pressed, bool focused);
    void heading(std::wstring_view value, Rect bounds, D2D1_COLOR_F color);
    void text(std::wstring_view value, Rect bounds, D2D1_COLOR_F color, bool small_text = false);
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout(std::wstring_view value, TextStyle style, Size& measured,
        float wrap_width = 0, std::size_t maximum_lines = 0);
    void text_layout(IDWriteTextLayout* layout, Rect bounds, D2D1_COLOR_F color);
    void cell_text(std::wstring_view value, Rect bounds, D2D1_COLOR_F color, bool numeric);
    void item_visual(const ItemVisual& visual, const std::shared_ptr<const ImagePixels>& pixels, Rect bounds, D2D1_COLOR_F ink);
    void collection_row(const CollectionRow& row, bool selected, bool focused, bool enabled, const Palette& palette, bool hovered = false,
        const std::shared_ptr<const ImagePixels>& pixels = {}, bool trailing_shortcut_badges = false, bool command_menu = false);
    void push_clip(Rect bounds);
    void pop_clip();
    void origin(float x, float y);
    bool image(const std::shared_ptr<const ImagePixels>& pixels, Rect bounds);
    void keep_images(std::span<const std::uint64_t> ids);
    void scene(const std::shared_ptr<const VectorScene>& scene, std::optional<ShapeId> selected, D2D1_COLOR_F highlight);
    std::size_t native_buffer_bytes() const { return std::size_t(native_size_.width) * native_size_.height * 4; }
    std::size_t native_bitmap_bytes() const;
    std::size_t scene_paths() const;
private:
    VisualStyle visual_style_{VisualStyle::classic};
    bool variable_font_{};
    friend struct DrawingTestAccess;
    static thread_local HRESULT end_result_override_;
    static thread_local HRESULT native_result_override_;
    static thread_local void (*present_observer_)(HWND);
    static thread_local std::size_t live_targets_;
    static thread_local std::size_t created_text_layouts_;
    static thread_local std::size_t created_field_brushes_;
    inline static thread_local std::size_t created_symbol_faces_{};
    Microsoft::WRL::ComPtr<ID2D1Factory> factory_;
    Microsoft::WRL::ComPtr<IDWriteFactory> text_factory_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> format_, small_format_, heading_format_, subtitle_format_, strong_format_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> numeric_format_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> caption_format_;
    Microsoft::WRL::ComPtr<IDWriteFontFace> symbol_face_;
    const wchar_t* symbol_family_{L""};
    DWRITE_FONT_METRICS symbol_font_metrics_{};
    std::array<UINT16, symbol_codepoints.size()> symbol_indices_{};
    std::array<DWRITE_GLYPH_METRICS, symbol_codepoints.size()> symbol_metrics_{};
    void prepare_symbols();
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> target_;
    struct NativeBitmap {
        HWND window{};
        RECT bounds{};
        Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap;
        bool used{};
    };
    std::vector<NativeBitmap> native_bitmaps_;
    HDC native_dc_{};
    HBITMAP native_buffer_{};
    void* native_pixels_{};
    D2D1_SIZE_U native_size_{};
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush_;
    struct FieldBorderBrush {
        ThemeMode mode{};
        COLORREF accent{};
        Microsoft::WRL::ComPtr<ID2D1LinearGradientBrush> value;
    };
    FieldBorderBrush field_borders_[2];
    FieldBorderBrush button_borders_[2];
    struct Bitmap {
        std::uint64_t id{};
        std::size_t bytes{};
        Microsoft::WRL::ComPtr<ID2D1Bitmap> value;
    };
    std::vector<Bitmap> bitmaps_;
    struct SceneCache {
        std::weak_ptr<const VectorScene> source;
        std::vector<Microsoft::WRL::ComPtr<ID2D1PathGeometry>> paths;
        bool used{};
    };
    std::vector<SceneCache> scenes_;
    Microsoft::WRL::ComPtr<ID2D1StrokeStyle> scene_stroke_;
    void erase_bitmap(std::size_t index);
};

}
