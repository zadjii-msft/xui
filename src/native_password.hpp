#pragma once

#include "xui/documents.hpp"
#include "xui/native_edit.hpp"
#include "drawing.hpp"
#include <commctrl.h>

namespace xui {

class NativePasswordBridge final {
public:
    explicit NativePasswordBridge(std::shared_ptr<PasswordInput>);
    ~NativePasswordBridge();
    NativePasswordBridge(const NativePasswordBridge&) = delete;
    NativePasswordBridge& operator=(const NativePasswordBridge&) = delete;
    void attach(HWND parent, int id);
    HWND window() const { return window_; }
    bool composing() const { return composing_; }
    void update(UINT dpi, const Palette&);
    void changed();
    void on_failure(std::function<void()> callback) { failure_ = std::move(callback); }
private:
    static LRESULT CALLBACK subclass(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR) noexcept;
    std::wstring text() const;
    std::shared_ptr<PasswordInput> model_;
    std::shared_ptr<bool> lifetime_ = std::make_shared<bool>();
    HWND window_{};
    HFONT font_{};
    NativeFieldFont styled_font_;
    UINT dpi_{};
    std::uint64_t revision_{};
    std::size_t maximum_{};
    bool composing_{}, setting_{}, colors_set_{};
    COLORREF text_color_{}, background_{};
    std::function<void()> failure_;
};

}
