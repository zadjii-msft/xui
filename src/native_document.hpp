#pragma once
#include "xui/documents.hpp"
#include "xui/theme.hpp"
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
    std::wstring text() const;
    std::shared_ptr<Control> model_;
    HWND window_{};
    HFONT font_{};
    UINT dpi_{};
    std::uint64_t revision_{}, selection_revision_{};
    bool composing_{}, setting_{}, readonly_{}, colors_set_{};
    COLORREF text_color_{}, background_{};
    std::size_t maximum_{};
    std::wstring accessible_name_;
    Palette palette_{};
    std::function<void()> failure_;
};
}
