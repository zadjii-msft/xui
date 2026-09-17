#pragma once

#include "images.hpp"
#include "async.hpp"
#include <algorithm>
#include <cstring>

namespace xui {
// UI-thread state only. The image service never receives this object or its HWND.
class WindowIcon final {
public:
    ~WindowIcon() { cancel(); if (icon_) DestroyIcon(icon_); }
    std::function<void(const std::wstring&)> on_error;

    void set_source(std::wstring path, HWND window, UINT dpi, const std::shared_ptr<TaskWake>& wake) {
        if (path.size() > 32767 || path.find(L'\0') != std::wstring::npos)
            throw std::invalid_argument("Invalid window icon path");
        source_ = std::move(path);
        refresh(window, dpi, wake);
    }
    void refresh(HWND window, UINT dpi, const std::shared_ptr<TaskWake>& wake) {
        cancel();
        replace(window, nullptr);
        if (!window || source_.empty()) return;
        const auto extent = static_cast<std::uint32_t>(std::clamp(
            std::max(GetSystemMetricsForDpi(SM_CXICON, dpi), GetSystemMetricsForDpi(SM_CYICON, dpi)), 1, 1024));
        request_ = request_image(source_, {extent, extent}, wake, ImageKind::shell);
        // Queue rejection is already completed and need not signal the worker event.
        SetEvent(wake->event);
    }
    void close(HWND window) {
        cancel();
        replace(window, nullptr);
        if (!source_.empty()) clear_image_cache();
    }
    void deliver(HWND window) {
        if (!window || !request_) return;
        auto completed = request_;
        std::shared_ptr<const ImagePixels> pixels;
        std::wstring error;
        {
            std::lock_guard lock(completed->mutex);
            if (!completed->done || completed->cancelled) return;
            pixels = std::move(completed->pixels);
            error = std::move(completed->error);
        }
        request_.reset();
        if (pixels) {
            try {
                auto next = create(*pixels);
                if (next) replace(window, next);
                else error = L"Cannot create the window icon (Win32 error " + std::to_wstring(GetLastError()) + L").";
            } catch (const std::exception& failure) {
                error = L"Cannot create the window icon: " + exception_message(failure);
            }
        } else if (error.empty()) error = L"The Shell returned no window icon pixels.";
        // A callback can replace its own subscription or start another request.
        if (!error.empty()) if (auto callback = on_error) callback(error);
    }
private:
    friend struct WindowIconTestAccess;
    struct Bitmap {
        HBITMAP value{};
        ~Bitmap() {
            const auto error = GetLastError();
            if (value) DeleteObject(value);
            SetLastError(error);
        }
    };
    static HICON create(const ImagePixels& image) {
        const auto side = std::max(image.size.width, image.size.height);
        if (!side || side > 1024 || image.pixels.size() !=
            static_cast<std::size_t>(image.size.width) * image.size.height * 4) {
            SetLastError(ERROR_INVALID_DATA); return nullptr;
        }
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = static_cast<LONG>(side);
        info.bmiHeader.biHeight = -static_cast<LONG>(side);
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void* bits{};
        Bitmap color{CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0)};
        if (!color.value) return nullptr;
        const auto stride = static_cast<std::size_t>(side) * 4;
        std::memset(bits, 0, stride * side);
        const auto x = (side - image.size.width) / 2, y = (side - image.size.height) / 2;
        for (std::uint32_t row = 0; row < image.size.height; ++row)
            std::memcpy(static_cast<std::byte*>(bits) + (row + y) * stride + x * 4,
                image.pixels.data() + static_cast<std::size_t>(row) * image.size.width * 4, image.size.width * 4);
        const auto mask_stride = ((side + 15) / 16) * 2;
        std::vector<unsigned char> mask_bits(static_cast<std::size_t>(mask_stride) * side);
        for (std::uint32_t row = 0; row < side; ++row)
            for (std::uint32_t column = 0; column < side; ++column)
                if (static_cast<unsigned char*>(bits)[row * stride + column * 4 + 3] == 0)
                    mask_bits[row * mask_stride + column / 8] |= static_cast<unsigned char>(0x80 >> (column % 8));
        Bitmap mask{CreateBitmap(side, side, 1, 1, mask_bits.data())};
        if (!mask.value) return nullptr;
        ICONINFO icon{};
        icon.fIcon = TRUE; icon.hbmColor = color.value; icon.hbmMask = mask.value;
        return CreateIconIndirect(&icon);
    }
    void cancel() {
        if (request_) request_->cancel();
        request_.reset();
    }
    void replace(HWND window, HICON next) {
        // Both slots borrow the same owned handle. Never destroy a handle returned by WM_SETICON.
        if (window) {
            SendMessageW(window, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(next));
            SendMessageW(window, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(next));
        }
        if (icon_) DestroyIcon(icon_);
        icon_ = next;
    }
    std::wstring source_;
    std::shared_ptr<ImageRequest> request_;
    HICON icon_{};
};
}
