#pragma once

#include "xui/core.hpp"
#include "xui/controls.hpp"
#include <windows.h>
#include <string>

namespace xui {

class TextInput;
class SuggestionPeer;

// One opt-in HFONT per native text part; never a process-wide font cache.
class NativeFieldFont final {
public:
    ~NativeFieldFont();
    NativeFieldFont() = default;
    NativeFieldFont(const NativeFieldFont&) = delete;
    NativeFieldFont& operator=(const NativeFieldFont&) = delete;
    void update(HWND window, const PartStyleValues* values, UINT dpi, HFONT fallback,
        const wchar_t* family, float size);
private:
    HFONT font_{};
    LOGFONTW descriptor_{};
};

// Replaceable text control boundary. Windows EDIT owns text, IME, undo, and UIA.
class NativeEditBridge final : public Element {
public:
    NativeEditBridge();
    ~NativeEditBridge() override;
    void attach(HWND parent, int control_id, TextInputPurpose purpose = TextInputPurpose::normal);
    void set_dpi(UINT dpi);
    void set_font_family(std::wstring family);
    void set_font(std::wstring_view family, float size, int weight, bool italic);
    HFONT font() const { return font_; }
    void set_caption_font(HWND caption, const PartStyleValues* values, HFONT fallback,
        const wchar_t* family, float size);
    void set_placeholder_color(COLORREF color);
    void set_colors(COLORREF text, COLORREF background);
    void set_caption_color(HWND caption, COLORREF color);
    void set_placeholder(std::wstring text);
    void set_insets(Insets insets);
    void arrange(Rect bounds) override;
    HWND window() const { return window_; }
    bool composing() const { return composing_; }
    std::wstring text() const;
    void focus(bool select_all = false);
    TextInput::Selection selection() const;
    void set_selection(TextInput::Selection value);
    void sync_suggestions(TextInput& input);
    void text_changed();
    void dismiss_suggestions();
    bool suggestion_key(WPARAM key);
    void set_model_text(const std::wstring& text);
    void set_suggestion_colors(COLORREF background, COLORREF text, COLORREF secondary);
    RECT suggestion_anchor() const;
    void on_failure(std::function<void()> callback) { failure_ = std::move(callback); }
    void report_failure() noexcept { if (failure_) { auto callback = failure_; callback(); } }
private:
    void update_font(UINT dpi);
    std::wstring font_family_{L"Segoe UI"};
    float font_size_{14.0f};
    int font_weight_{FW_NORMAL};
    bool font_italic_{};
    NativeFieldFont caption_font_;
    void require_live_thread() const;
    void delete_previous_word();
    static LRESULT CALLBACK subclass(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR) noexcept;
    HWND window_{};
    HFONT font_{};
    int font_height_{};
    UINT dpi_{96};
    bool composing_{};
    bool input_scope_set_{};
    COLORREF placeholder_color_{RGB(128, 128, 128)};
    COLORREF text_color_{}, background_color_{};
    bool colors_set_{};
    COLORREF caption_color_{};
    bool caption_color_set_{};
    std::wstring placeholder_{L"Filter this folder"};
    std::optional<Insets> insets_;
    std::unique_ptr<SuggestionPeer> suggestions_;
    bool setting_text_{};
    TextInput* input_{};
    std::shared_ptr<int> lifetime_{std::make_shared<int>(0)};
    std::function<void()> failure_;
};

}
