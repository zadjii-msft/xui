#pragma once
#include "xui/documents.hpp"
#include "xui/theme.hpp"
#include "xui/native_edit.hpp"
#include "drawing.hpp"
#include <windows.h>
#include <commctrl.h>

namespace xui {
class NativeDocumentBridge final {
public:
    explicit NativeDocumentBridge(std::shared_ptr<Control> model);
    ~NativeDocumentBridge();
    void attach(HWND parent, int id);
    HWND window() const { return window_; }
    bool composing() const { return composing_; }
    void update(UINT dpi, const Palette& palette);
    void changed();
    LRESULT notify(const NMHDR& notification);
    void on_failure(std::function<void()> callback) { failure_ = std::move(callback); }
private:
    static LRESULT CALLBACK subclass(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR) noexcept;
    bool command(TextCommand command);
    TextSelection replace_range(TextSelection range, const std::wstring& expected, const std::wstring& replacement);
    std::wstring text() const;
    std::shared_ptr<Control> model_;
    HWND window_{};
    HFONT font_{};
    NativeFieldFont styled_font_;
    LOGFONTW document_font_{};
    bool document_font_set_{};
    UINT dpi_{};
    std::uint64_t revision_{}, selection_revision_{};
    std::uint64_t syntax_revision_{};
    std::array<COLORREF, 8> syntax_colors_{};
    bool syntax_dirty_{true};
    bool composing_{}, setting_{}, readonly_{}, monospace_{}, colors_set_{};
    COLORREF text_color_{}, background_{};
    std::size_t maximum_{};
    std::wstring accessible_name_;
    Palette palette_{};
    std::function<void()> failure_;
};
}
