#pragma once

#include "drawing.hpp"
#include <functional>
#include <span>

namespace xui::platform {

// Shared by the browser adapter and the application-facing control host.
class Runtime final {
public:
    Runtime();
    ~Runtime();
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;
    int run(const std::function<bool(MSG&)>& translate, HANDLE ready = nullptr,
        const std::function<void()>& accept = {}, const std::function<void()>& present = {});
};

COLORREF native_color(D2D1_COLOR_F color);
void place(HWND window, Rect bounds, UINT dpi);
void appearance(HWND window, ThemeMode theme, const Palette& palette, bool backdrop = false);
// DWM_SYSTEMBACKDROP_TYPE value: 0 disables the material. Returns false when this
// Windows version or the current DWM state cannot present the material.
bool system_backdrop(HWND window, DWORD type, bool custom_frame = false);
bool system_backdrop_supported();
// Requests Windows 11 rounded corners for a borderless window. Earlier systems ignore it.
void round_corners(HWND window);
bool transparency_effects_enabled();
// GDI writes zero alpha. Over a system backdrop, repaint the native child through an
// opaque buffer after input that can draw outside WM_PAINT.
void opaque_native_paint(HWND window, bool enabled);
bool traverse_focus(std::span<const HWND> windows, bool reverse, HWND current = nullptr);
inline constexpr UINT restore_focus_message = WM_APP + 13;
void request_focus_restore(HWND window);
void restore_focus(HWND window, HWND preferred, std::span<const HWND> targets);

}
