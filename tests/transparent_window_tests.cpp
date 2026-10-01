#include "../src/drawing.hpp"
#include <stdexcept>
#include <cstdint>
#include <iostream>

namespace xui {
struct DrawingTestAccess {
    static std::uint32_t pixel(const Drawing& drawing, unsigned x, unsigned y) {
        DIBSECTION section{};
        if (GetObjectW(drawing.layered_bitmap_, sizeof(section), &section) != sizeof(section))
            throw std::runtime_error("Read layered bitmap");
        auto* pixels = static_cast<const std::uint32_t*>(section.dsBm.bmBits);
        return pixels[y * drawing.layered_size_.width + x];
    }
};
}

int main() {
    try {
        const int x = (GetSystemMetrics(SM_CXSCREEN) - 128) / 2;
        const int y = (GetSystemMetrics(SM_CYSCREEN) - 96) / 2;
        WNDCLASSW cls{};
        cls.hInstance = GetModuleHandleW(nullptr);
        cls.lpfnWndProc = DefWindowProcW;
        cls.lpszClassName = L"Xui.TransparentTest";
        if (!RegisterClassW(&cls)) throw std::runtime_error("Register layered test window");
        HWND hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_CONTROLPARENT, cls.lpszClassName, L"XUI alpha test",
            WS_POPUP | WS_CLIPCHILDREN, x, y, 128, 96, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!hwnd) throw std::runtime_error("Create layered test window");
        {
            xui::Drawing drawing;
            drawing.initialize();
            if (!drawing.begin(hwnd, 96, D2D1::ColorF(0, 0.0f), {}, true))
                throw std::runtime_error("Begin transparent frame");
            drawing.rounded({24, 16, 80, 64}, D2D1::ColorF(0x3875ab), 12);
            if (!drawing.end()) throw std::runtime_error("Present transparent frame");
            if (xui::DrawingTestAccess::pixel(drawing, 2, 2) != 0)
                throw std::runtime_error("Transparent corner has nonzero alpha");
            if ((xui::DrawingTestAccess::pixel(drawing, 64, 48) >> 24) != 255)
                throw std::runtime_error("The card is not opaque");
            if ((xui::DrawingTestAccess::pixel(drawing, 24, 16) >> 24) == 255)
                throw std::runtime_error("Rounded corner is still square");
            ShowWindow(hwnd, SW_SHOWNA);
            if (!drawing.begin(hwnd, 96, D2D1::ColorF(0, 0.0f), {}, true))
                throw std::runtime_error("Begin visible frame");
            drawing.rounded({24, 16, 80, 64}, D2D1::ColorF(0x3875ab), 12);
            if (!drawing.end()) throw std::runtime_error("Present visible frame");
            if (WindowFromPoint({x + 2, y + 2}) == hwnd)
                throw std::runtime_error("Transparent corner intercepts pointer input");
            if (WindowFromPoint({x + 64, y + 48}) != hwnd) {
                std::cerr << "center=" << WindowFromPoint({x + 64, y + 48}) << " hwnd=" << hwnd << '\n';
                throw std::runtime_error("Opaque card is not hit-testable");
            }
            if (!SetWindowPos(hwnd, nullptr, 0, 0, 160, 120, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE))
                throw std::runtime_error("Resize transparent window");
            if (!drawing.begin(hwnd, 96, D2D1::ColorF(0, 0.0f), {}, true))
                throw std::runtime_error("Begin resized frame");
            drawing.rounded({24, 16, 96, 80}, D2D1::ColorF(0x3875ab), 12);
            if (!drawing.end()) throw std::runtime_error("Present resized frame");
            if (xui::DrawingTestAccess::pixel(drawing, 150, 110) != 0 ||
                (xui::DrawingTestAccess::pixel(drawing, 64, 48) >> 24) != 255)
                throw std::runtime_error("Resizing retained stale opaque pixels");
            ShowWindow(hwnd, SW_HIDE);
            if (!SetWindowPos(hwnd, nullptr, 0, 0, 4097, 120, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE))
                throw std::runtime_error("Size transparent window beyond its buffer limit");
            bool rejected = false;
            try { drawing.begin(hwnd, 96, D2D1::ColorF(0, 0.0f), {}, true); }
            catch (const std::invalid_argument&) { rejected = true; }
            if (!rejected) throw std::runtime_error("Oversized layered buffer was accepted");
        }
        DestroyWindow(hwnd);
        std::cout << "Transparent pixels, resize, rounded card, and pointer hit testing passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
