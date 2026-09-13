#pragma once
#include "drawing.hpp"

namespace xui {
// Palette and DPI belong to the invoking window, never to a process-wide menu theme.
void show_control_menu(Control& control, HWND window, LPARAM position, const Palette& palette, UINT dpi);
void cancel_control_menu(HWND root);
}
