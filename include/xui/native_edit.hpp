#pragma once

#include "xui/core.hpp"
#include "xui/controls.hpp"
#include <windows.h>
#include <string>

namespace xui {

class TextInput;
class SuggestionPeer;

// Replaceable text control boundary. Windows EDIT owns text, IME, undo, and UIA.
class NativeEditBridge final : public Element {
public:
    NativeEditBridge();
    ~NativeEditBridge() override;
    void attach(HWND parent, int control_id);
    void set_dpi(UINT dpi);
    void set_font_family(std::wstring family);
    void set_placeholder_color(COLORREF color);
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
    void require_live_thread() const;
    void delete_previous_word();
    void update_font(UINT dpi);
    std::wstring font_family_{L"Segoe UI"};
    static LRESULT CALLBACK subclass(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR) noexcept;
    HWND window_{};
    HFONT font_{};
    int font_height_{};
    UINT dpi_{96};
    bool composing_{};
    COLORREF placeholder_color_{RGB(128, 128, 128)};
    std::wstring placeholder_{L"Filter this folder"};
    std::optional<Insets> insets_;
    std::unique_ptr<SuggestionPeer> suggestions_;
    bool setting_text_{};
    TextInput* input_{};
    std::shared_ptr<int> lifetime_{std::make_shared<int>(0)};
    std::function<void()> failure_;
};

}
