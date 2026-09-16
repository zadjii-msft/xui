#include "../src/window_icon.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <thread>

namespace xui {
struct WindowIconTestAccess {
    static std::shared_ptr<ImageRequest> pending(WindowIcon& icon) {
        icon.cancel();
        icon.request_ = std::make_shared<ImageRequest>();
        return icon.request_;
    }
};
}
namespace {
using namespace xui;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
HICON current(HWND window, int kind = ICON_BIG) {
    return reinterpret_cast<HICON>(SendMessageW(window, WM_GETICON, kind, 0));
}
bool valid(HICON icon) {
    ICONINFO info{};
    const bool result = GetIconInfo(icon, &info) != 0;
    if (info.hbmColor) DeleteObject(info.hbmColor);
    if (info.hbmMask) DeleteObject(info.hbmMask);
    return result;
}
void complete(const std::shared_ptr<ImageRequest>& request) {
    auto pixels = std::make_shared<ImagePixels>();
    pixels->size = {8, 4};
    pixels->pixels.resize(8 * 4 * 4, std::byte{0x80});
    std::lock_guard lock(request->mutex);
    request->pixels = std::move(pixels);
    request->done = true;
}
void run() {
    HWND window = CreateWindowExW(0, L"STATIC", L"XUI hidden icon test", 0,
        0, 0, 100, 100, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    check(window != nullptr, "Create hidden test window");
    struct Close { HWND window; ~Close() { DestroyWindow(window); } } close{window};
    auto wake = std::make_shared<TaskWake>();
    WindowIcon icon;
    unsigned errors{};
    std::wstring last_error;
    icon.on_error = [&](const std::wstring& error) { ++errors; last_error = error; };

    bool rejected{};
    try { icon.set_source(std::wstring(L"a\0b", 3), window, 96, wake); }
    catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "Reject embedded NUL");
    rejected = false;
    try { icon.set_source(std::wstring(32768, L'a'), window, 96, wake); }
    catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "Reject long path");

    auto first = WindowIconTestAccess::pending(icon);
    complete(first);
    icon.deliver(window);
    auto first_icon = current(window);
    check(first_icon && first_icon == current(window, ICON_SMALL) && valid(first_icon), "Owned icon fills both HWND slots");
    ICONINFO info{};
    check(GetIconInfo(first_icon, &info) != 0, "Read icon bitmaps");
    BITMAP bitmap{};
    GetObjectW(info.hbmColor, sizeof(bitmap), &bitmap);
    DeleteObject(info.hbmColor); DeleteObject(info.hbmMask);
    check(bitmap.bmWidth == 8 && bitmap.bmHeight == 8, "Pad rectangular pixels without stretching");
    icon.set_source(L"", window, 96, wake);
    check(!current(window) && !current(window, ICON_SMALL) && !valid(first_icon), "Clear slots and destroy owned handle");

    auto stale = WindowIconTestAccess::pending(icon);
    icon.set_source(L"", window, 96, wake);
    check(stale->cancelled, "Replacement cancels old mailbox");
    complete(stale);
    icon.deliver(window);
    check(!current(window) && errors == 0, "Stale completion cannot replace cleared icon");
    stale->pixels.reset();

    auto failed = WindowIconTestAccess::pending(icon);
    failed->error = L"Fixture error"; failed->done = true;
    icon.deliver(window); icon.deliver(window);
    check(errors == 1 && last_error == L"Fixture error", "Report failure exactly once");

    auto pending = WindowIconTestAccess::pending(icon);
    icon.close(window);
    check(pending->cancelled, "Closure cancels delivery");
    complete(pending); icon.deliver(window);
    check(!current(window) && errors == 1, "Closure rejects stale completion");
    pending->pixels.reset();

    const DWORD before = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
    for (int i = 0; i < 128; ++i) {
        complete(WindowIconTestAccess::pending(icon));
        icon.deliver(window);
        check(current(window) != nullptr, "Replace icon repeatedly");
    }
    icon.close(window);
    check(GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS) <= before, "Repeated replacement does not leak icon handles");

    icon.set_source(std::filesystem::current_path().wstring(), window, 96, wake);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (!current(window) && errors == 1 && std::chrono::steady_clock::now() < deadline) {
        WaitForSingleObject(wake->event, 20); icon.deliver(window);
    }
    check(current(window) && errors == 1, "Shared Shell pipeline supplies a real folder icon");
    icon.refresh(window, 144, wake);
    check(!current(window), "DPI refresh clears old-size icon");
    icon.set_source((std::filesystem::current_path() / L"xui-window-icon-missing-folder-7fc39e42").wstring(), window, 96, wake);
    deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (errors == 1 && std::chrono::steady_clock::now() < deadline) {
        WaitForSingleObject(wake->event, 20); icon.deliver(window);
    }
    check(errors == 2 && !last_error.empty() && !current(window), "Missing path reports an explicit asynchronous error");
    icon.close(window);
}
}
int main() {
    try { run(); std::cout << "Window icon tests passed\n"; return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
