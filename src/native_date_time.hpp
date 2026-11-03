#pragma once

#include "xui/documents.hpp"
#include "xui/native_edit.hpp"
#include "drawing.hpp"
#include <commctrl.h>

namespace xui {

class NativeDateTimeBridge final {
public:
    explicit NativeDateTimeBridge(std::shared_ptr<DateTimePicker>);
    ~NativeDateTimeBridge();
    NativeDateTimeBridge(const NativeDateTimeBridge&) = delete;
    NativeDateTimeBridge& operator=(const NativeDateTimeBridge&) = delete;
    void attach(HWND parent, int id);
    HWND window() const { return window_; }
    bool composing() const { return composing_; }
    void update(UINT dpi, const Palette&);
    LRESULT notify(const NMHDR&);
    void on_failure(std::function<void()> callback) { failure_ = std::move(callback); }
private:
    static LRESULT CALLBACK subclass(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR) noexcept;
    std::shared_ptr<DateTimePicker> model_;
    std::shared_ptr<bool> lifetime_ = std::make_shared<bool>();
    HWND window_{};
    HFONT font_{};
    NativeFieldFont styled_font_;
    UINT dpi_{};
    std::uint64_t revision_{};
    bool composing_{}, setting_{};
    std::function<void()> failure_;
};

}
