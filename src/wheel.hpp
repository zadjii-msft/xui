#pragma once

#include "platform.hpp"
#include <windowsx.h>

namespace xui {

inline double vertical_wheel_dips(WPARAM wparam, double page_height) {
    UINT lines{};
    win32_require(SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0) != FALSE,
        "Read Windows wheel scroll lines");
    const double notch = lines == WHEEL_PAGESCROLL ? page_height : 16.0 * lines;
    return static_cast<double>(GET_WHEEL_DELTA_WPARAM(wparam)) / WHEEL_DELTA * notch;
}

}
