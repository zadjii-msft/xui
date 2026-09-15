#pragma once
#include "drawing.hpp"
#include <atomic>

namespace xui {
inline constexpr wchar_t active_menu_property[] = L"Xui.ContextMenu.Active.1";
struct ContextMenuTestAccess {
    using Track = UINT (*)(HMENU, HWND, Point);
    static std::atomic<Track> track;
    using Paint = void (*)(HMENU, HWND);
    static std::atomic<Paint> paint;
};
// Palette and DPI belong to the invoking window, never to a process-wide menu theme.
void show_control_menu(Control& control, HWND window, LPARAM position, const Palette& palette, UINT dpi);
void cancel_control_menu(HWND root);
}
