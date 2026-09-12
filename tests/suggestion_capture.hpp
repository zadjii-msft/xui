#pragma once
#include <windows.h>
#include <psapi.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

namespace suggestion_capture {
inline void memory(HANDLE process, const char* phase) {
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (!GetProcessMemoryInfo(process, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters)))
        throw std::runtime_error("Read suggestion memory counters");
    std::vector<std::byte> buffer(1024 * 1024);
    if (!QueryWorkingSet(process, buffer.data(), static_cast<DWORD>(buffer.size())))
        throw std::runtime_error("Read suggestion working set");
    const auto* info = reinterpret_cast<const PSAPI_WORKING_SET_INFORMATION*>(buffer.data());
    std::size_t pages{};
    for (std::size_t i = 0; i < info->NumberOfEntries; ++i) if (!info->WorkingSetInfo[i].Shared) ++pages;
    SYSTEM_INFO system{}; GetSystemInfo(&system);
    std::cout << phase << " private_bytes=" << counters.PrivateUsage << " private_working_set=" << pages * system.dwPageSize <<
        " handles_gdi=" << GetGuiResources(process, GR_GDIOBJECTS) << " handles_user=" << GetGuiResources(process, GR_USEROBJECTS) << '\n';
}
inline void bitmap(HWND host, HWND popup, const std::filesystem::path& path) {
    RECT rect{}, drop{}; GetWindowRect(host, &rect); GetWindowRect(popup, &drop);
    const int width = rect.right - rect.left, height = rect.bottom - rect.top;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    HDC dc = CreateCompatibleDC(nullptr);
    void* pixels{};
    HBITMAP image = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (!dc || !image) {
        if (image) DeleteObject(image);
        if (dc) DeleteDC(dc);
        throw std::runtime_error("Create suggestion capture bitmap");
    }
    const auto previous = SelectObject(dc, image);
    // Print only our windows. Never capture other desktop applications.
    const bool printed = PrintWindow(host, dc, 2) != FALSE;
    HDC drop_dc = CreateCompatibleDC(dc);
    HBITMAP drop_image = CreateCompatibleBitmap(dc, drop.right - drop.left, drop.bottom - drop.top);
    const auto drop_previous = SelectObject(drop_dc, drop_image);
    const bool printed_popup = PrintWindow(popup, drop_dc, 2) != FALSE;
    BitBlt(dc, drop.left - rect.left, drop.top - rect.top, drop.right - drop.left, drop.bottom - drop.top,
        drop_dc, 0, 0, SRCCOPY);
    SelectObject(drop_dc, drop_previous); DeleteObject(drop_image); DeleteDC(drop_dc);
    BITMAPFILEHEADER header{};
    header.bfType = 0x4d42;
    header.bfOffBits = sizeof(header) + sizeof(BITMAPINFOHEADER);
    header.bfSize = header.bfOffBits + width * height * 4;
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(&header), sizeof(header));
    file.write(reinterpret_cast<const char*>(&info.bmiHeader), sizeof(info.bmiHeader));
    file.write(static_cast<const char*>(pixels), static_cast<std::streamsize>(width) * height * 4);
    const bool written = bool(file);
    SelectObject(dc, previous); DeleteObject(image); DeleteDC(dc);
    if (!printed || !printed_popup || !written) throw std::runtime_error("Capture suggestion windows");
}
}
