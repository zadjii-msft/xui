#pragma once

#include "suggestion_worker.hpp"
#include <commctrl.h>

namespace xui {
class NativeEditBridge;
class TextInput;

class SuggestionPeer {
public:
    SuggestionPeer(NativeEditBridge& edit, TextInput& input);
    ~SuggestionPeer();
    void sync();
    void changed();
    void dismiss();
    bool key(WPARAM key);
    void timer();
    void deliver();
    void set_colors(COLORREF background, COLORREF text, COLORREF secondary);
    bool replacing() const { return replacing_; }
    static constexpr UINT_PTR timer_id = 0x585549;
private:
    static LRESULT CALLBACK procedure(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR) noexcept;
    bool eligible() const;
    void show(SuggestionResult result);
    void accept(bool submit);
    NativeEditBridge& edit_;
    TextInput& input_;
    std::uint64_t revision_{};
    HWND popup_{};
    HWND list_{}, status_{};
    HFONT font_{};
    UINT font_dpi_{};
    HBRUSH background_{};
    COLORREF background_color_{CLR_INVALID}, text_color_{}, secondary_color_{};
    std::shared_ptr<detail::SuggestionDelivery> delivery_;
    std::shared_ptr<detail::SuggestionWorker> worker_;
    std::vector<std::wstring> items_;
    bool wanted_{}, explicit_{}, replacing_{}, pending_{};
    int clicked_{-1};
};
}
