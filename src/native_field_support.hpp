#pragma once

#include "platform.hpp"
#include <string_view>

namespace xui {

inline bool complete_native_utf16(std::wstring_view text) {
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto value = static_cast<unsigned>(text[i]);
        if (value >= 0xd800 && value <= 0xdbff) {
            if (++i == text.size() || text[i] < 0xdc00 || text[i] > 0xdfff) return false;
        } else if (value >= 0xdc00 && value <= 0xdfff) return false;
    }
    return true;
}

inline void update_native_field_font(HWND window, HFONT& font, UINT& previous_dpi, UINT dpi) {
    if (previous_dpi == dpi) return;
    const auto replacement = CreateFontW(-MulDiv(14, dpi, 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    win32_require(replacement != nullptr, "Create native document font");
    SendMessageW(window, WM_SETFONT, reinterpret_cast<WPARAM>(replacement), TRUE);
    if (font) DeleteObject(font);
    font = replacement;
    previous_dpi = dpi;
}

}
