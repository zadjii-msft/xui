#pragma once

#include "xui/core.hpp"
#include <windows.h>
#include <string>

namespace xui {

// Replaceable text control boundary. Windows EDIT owns text, IME, undo, and UIA.
class NativeEditBridge final : public Element {
public:
    ~NativeEditBridge() override;
    void attach(HWND parent, int control_id);
    void set_dpi(UINT dpi);
    void set_placeholder_color(COLORREF color);
    void set_placeholder(std::wstring text);
    void set_insets(Insets insets);
    void arrange(Rect bounds) override;
    HWND window() const { return window_; }
    bool composing() const { return composing_; }
    std::wstring text() const;
    void focus(bool select_all = false);
private:
    static LRESULT CALLBACK subclass(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR) noexcept;
    HWND window_{};
    HFONT font_{};
    UINT dpi_{96};
    bool composing_{};
    COLORREF placeholder_color_{RGB(128, 128, 128)};
    std::wstring placeholder_{L"Filter this folder"};
    std::optional<Insets> insets_;
};

}
