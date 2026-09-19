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
class RowImages;

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

D2D1_COLOR_F style_foreground(const PartStyleValues& values, const Palette& palette, D2D1_COLOR_F fallback);

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
    struct FontDescriptor {
        const std::shared_ptr<const StyleFontFamily> authored_family;
        const wchar_t* const fallback_family;
        const float size;
        const uint32_t weight;
        const StyleFontStyle style;
        const wchar_t* family_name() const { return authored_family ? authored_family->name.c_str() : fallback_family; }
    };
    // The fallback family must outlive the descriptor; built-in family names have static storage.
    static FontDescriptor font_descriptor(const PartStyleValues& values, const wchar_t* fallback_family,
        float fallback_size, uint32_t fallback_weight = 400, StyleFontStyle fallback_style = StyleFontStyle::normal) {
        return {values.font_family, fallback_family, values.font_size.value_or(fallback_size),
            values.font_weight.value_or(fallback_weight), values.font_style.value_or(fallback_style)};
    }
    static D2D1_COLOR_F style_foreground(const PartStyleValues& values, const Palette& palette, D2D1_COLOR_F fallback) {
        return xui::style_foreground(values, palette, fallback);
    }
    const wchar_t* symbol_font_family() const { return symbol_family_; }
    bool has_symbol(Symbol symbol) const;
    void symbol(Symbol symbol, Rect bounds, D2D1_COLOR_F color, float size = 16);
    bool begin(HWND window, float dpi, D2D1_COLOR_F background, Point offset = {});
    struct NativeWindow { HWND window; RECT clip; };
    bool native_windows(std::span<const NativeWindow> windows);
    void present_native(std::span<const HWND> windows);
    bool end();
    void discard();
    void release();
    void fill(Rect bounds, D2D1_COLOR_F color);
    void outline(Rect bounds, D2D1_COLOR_F color);
    void rounded(Rect bounds, D2D1_COLOR_F color, float radius = VisualMetrics::radius, bool stroke = false);
    void arc(Rect bounds, float start_turn, float sweep_turns, D2D1_COLOR_F color, float thickness);
    enum class SurfaceCorners { all, top, bottom };
    void focus_ring(Rect bounds, const Palette& palette, float radius = 4, SurfaceCorners corners = SurfaceCorners::all);
    void winui_focus_ring(Rect bounds, const Palette& palette, float radius = 4, float horizontal_outset = 3,
        SurfaceCorners corners = SurfaceCorners::all, float vertical_outset = 3);
    void winui_toggle_focus(const Toggle& toggle, Rect bounds, const Palette& palette, IDWriteTextLayout* label);
    void winui_combo_focus_background(Rect bounds, const Palette& palette);
    void winui_combo_focus_marker(Rect bounds, const Palette& palette);
    void field_frame(Rect bounds, const Palette& palette, bool focused, bool enabled, bool invalid = false,
        std::optional<D2D1_COLOR_F> fill = {});
    void surface_frame(Rect bounds, const Palette& palette);
    void styled_surface(Rect bounds, const Palette& palette, const PartStyleValues& values,
        D2D1_COLOR_F background, D2D1_COLOR_F border, float radius, Insets thickness,
        SurfaceCorners corners = SurfaceCorners::all);
    void styled_field_focus(Rect bounds, const Palette& palette, const PartStyleValues& values);
    void styled_toggle(const Toggle& toggle, Rect bounds, const Palette& palette, bool enabled,
        IDWriteTextLayout* label, bool focus_visible);
    void hyperlink(const HyperlinkButton& link, Rect bounds, const Palette& palette, bool enabled, bool focus_visible);
    void info_badge(const InfoBadge& badge, Rect bounds, const Palette& palette, bool enabled);
    void styled_button(const Button& button, Rect bounds, const Palette& palette, bool enabled, bool focus_visible,
        std::wstring_view label_override = {}, float trailing_space = 0, std::optional<bool> step_increment = {},
        const PartStyleValues* inherited_defaults = nullptr, std::optional<Symbol> glyph_override = {});
    void styled_label(const Label& label, Rect bounds, const Palette& palette, bool enabled);
    D2D1_COLOR_F check_indicator(Rect bounds, const Palette& palette, bool checked, bool enabled, bool mixed = false,
        bool hovered = false, bool pressed = false);
    D2D1_COLOR_F radio_indicator(Rect bounds, const Palette& palette, bool checked, bool enabled, bool hovered = false, bool pressed = false);
    void chevron(Rect bounds, D2D1_COLOR_F color, bool expanded);
    void scrollbar_thumb(Rect bounds, const Palette& palette, bool active, bool enabled = true);
    D2D1_COLOR_F button_face(Rect bounds, const Palette& palette, ButtonAppearance appearance,
        bool enabled, bool hovered, bool pressed, bool checked);
    D2D1_COLOR_F styled_button_face(Rect bounds, const Palette& palette, ButtonAppearance appearance,
        bool enabled, bool hovered, bool pressed, bool checked, const ButtonStyleValues& values);
    void line(float x1, float y1, float x2, float y2, D2D1_COLOR_F color, float thickness = 1);
    void icon(Rect bounds, D2D1_COLOR_F color, bool folder);
    void search_icon(Rect bounds, D2D1_COLOR_F color);
    void button_icon(Rect bounds, D2D1_COLOR_F color, ButtonIcon icon);
    void caption_button(Rect bounds, ButtonIcon icon, const Palette& palette,
        bool active, bool enabled, bool hovered, bool pressed, bool focused,
        const PartStyleValues* style = nullptr, const ButtonStyleValues* local = nullptr,
        const PartStyleValues* icon_style = nullptr);
    void tab_strip(const TabStrip& strip, Rect bounds, const Palette& palette, bool enabled, bool on_surface,
        bool focus_visible, std::optional<Point> pointer = {}, const RowImages* images = nullptr);
    void heading(std::wstring_view value, Rect bounds, D2D1_COLOR_F color);
    void text(std::wstring_view value, Rect bounds, D2D1_COLOR_F color, bool small_text = false);
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout(std::wstring_view value, TextStyle style, Size& measured,
        float wrap_width = 0, std::size_t maximum_lines = 0);
    Microsoft::WRL::ComPtr<IDWriteTextLayout> styled_layout(std::wstring_view value, TextStyle fallback,
        const PartStyleValues& values, Size& measured, float width = 0, std::size_t maximum_lines = 0);
    void styled_text(std::wstring_view value, Rect bounds, D2D1_COLOR_F color,
        const PartStyleValues& values, TextStyle fallback = TextStyle::body);
    void private_text(std::wstring_view value, Rect bounds, D2D1_COLOR_F color,
        const PartStyleValues& values, TextStyle fallback = TextStyle::body);
    void text_layout(IDWriteTextLayout* layout, Rect bounds, D2D1_COLOR_F color);
    void cell_text(std::wstring_view value, Rect bounds, D2D1_COLOR_F color, bool numeric);
    void item_visual(const ItemVisual& visual, const std::shared_ptr<const ImagePixels>& pixels, Rect bounds, D2D1_COLOR_F ink);
    void collection_row(const CollectionRow& row, bool selected, bool focused, bool enabled, const Palette& palette, bool hovered = false,
        const std::shared_ptr<const ImagePixels>& pixels = {}, bool trailing_shortcut_badges = false, bool command_menu = false,
        const VirtualCollection* owner = nullptr);
    void styled_collection_row(const VirtualCollection& owner, const CollectionRow& row, bool selected, bool focused,
        bool enabled, const Palette& palette, bool hovered, const std::shared_ptr<const ImagePixels>& pixels,
        bool trailing_shortcut_badges, bool command_menu);
    void push_clip(Rect bounds);
    void pop_clip();
    bool push_rounded_clip(Rect bounds, float radius);
    void pop_rounded_clip();
    void origin(float x, float y);
    bool image(const std::shared_ptr<const ImagePixels>& pixels, Rect bounds);
    void keep_images(std::span<const std::uint64_t> ids);
    void scene(const std::shared_ptr<const VectorScene>& scene, std::optional<ShapeId> selected, D2D1_COLOR_F highlight);
    std::size_t native_buffer_bytes() const { return std::size_t(native_size_.width) * native_size_.height * 4; }
    std::size_t native_bitmap_bytes() const;
    std::size_t scene_paths() const;
private:
    void rounded_border(Rect bounds, D2D1_COLOR_F color, float radius, float thickness);
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
    struct TextFormatKey {
        std::shared_ptr<const StyleFontFamily> font_family;
        std::optional<float> font_size;
        std::optional<uint32_t> font_weight;
        std::optional<StyleFontStyle> font_style;
        std::optional<StyleAlignment> horizontal_alignment, vertical_alignment;
        bool wrapping{};
        TextFormatKey() = default;
        TextFormatKey(const PartStyleValues& values, bool wrap);
        bool matches(const PartStyleValues& values, bool wrap) const;
    };
    struct StyledFormat {
        TextStyle fallback{};
        TextFormatKey typography;
        Microsoft::WRL::ComPtr<IDWriteTextFormat> format;
    };
    struct StyledLayout {
        std::wstring text;
        TextStyle fallback{};
        TextFormatKey typography;
        float width{};
        std::size_t maximum_lines{};
        Size measured{};
        Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    };
    std::vector<StyledFormat> styled_formats_;
    std::vector<StyledLayout> styled_layouts_;
    std::size_t next_styled_format_{}, next_styled_layout_{};
    IDWriteTextFormat* styled_format(TextStyle fallback, const PartStyleValues& values);
    IDWriteTextFormat* styled_format(TextStyle fallback, const PartStyleValues& values, bool wrap);
    Microsoft::WRL::ComPtr<IDWriteFontFace> symbol_face_;
    const wchar_t* symbol_family_{L""};
    DWRITE_FONT_METRICS symbol_font_metrics_{};
    std::array<UINT16, symbol_codepoints.size()> symbol_indices_{};
    std::array<DWRITE_GLYPH_METRICS, symbol_codepoints.size()> symbol_metrics_{};
    void prepare_symbols();
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> target_;
    Point offset_{};
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
    struct RoundedClip {
        Rect bounds;
        float radius{};
        Microsoft::WRL::ComPtr<ID2D1RoundedRectangleGeometry> geometry;
    };
    std::vector<RoundedClip> rounded_clips_;
    Microsoft::WRL::ComPtr<ID2D1StrokeStyle> scene_stroke_;
    void erase_bitmap(std::size_t index);
};

}
