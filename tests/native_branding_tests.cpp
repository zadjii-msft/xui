#include "../demo/branding.hpp"
#include <windows.h>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

struct Module {
    HMODULE value{};
    ~Module() { if (value) FreeLibrary(value); }
};

unsigned word(const unsigned char* bytes) {
    return bytes[0] | (static_cast<unsigned>(bytes[1]) << 8);
}

std::size_t dword(const unsigned char* bytes) {
    return word(bytes) | (static_cast<std::size_t>(word(bytes + 2)) << 16);
}

void resources(const wchar_t* path, bool branded, const std::vector<unsigned char>& canonical) {
    Module module{LoadLibraryExW(path, nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE)};
    require(module.value != nullptr, "Open executable resources");
    require(FindResource(module.value, MAKEINTRESOURCE(1), RT_MANIFEST) != nullptr,
        "Preserve the existing application manifest resource");
    const auto icon = FindResource(module.value, MAKEINTRESOURCE(IDI_ZOEY), RT_GROUP_ICON);
    require((icon != nullptr) == branded, "Only sample executables embed Zoey");
    if (!branded) return;
    const auto group = static_cast<const unsigned char*>(LockResource(LoadResource(module.value, icon)));
    require(group && SizeofResource(module.value, icon) == 6 + 10 * 14,
        "Embedded icon group contains exactly ten frames");
    require(word(group) == 0 && word(group + 2) == 1 && word(group + 4) == 10,
        "Embedded resource has a valid ten-frame icon header");
    unsigned index{};
    for (int size : {16, 20, 24, 32, 40, 48, 64, 96, 128, 256}) {
        const auto entry = group + 6 + index * 14;
        const auto expected = canonical.data() + 6 + index * 16;
        require((entry[0] ? entry[0] : 256) == size && (entry[1] ? entry[1] : 256) == size &&
            word(entry + 6) == 32, "Each embedded icon frame has the expected size and depth");
        require(std::memcmp(entry, expected, 12) == 0, "Embedded frame metadata matches the canonical ICO");
        const auto resource = FindResource(module.value, MAKEINTRESOURCE(word(entry + 12)), RT_ICON);
        require(resource != nullptr, "Find embedded PNG frame");
        const auto bytes = static_cast<const unsigned char*>(LockResource(LoadResource(module.value, resource)));
        const auto offset = dword(expected + 12), length = dword(expected + 8);
        require(offset <= canonical.size() && length <= canonical.size() - offset,
            "Canonical ICO frame is in bounds");
        require(bytes && SizeofResource(module.value, resource) == length &&
            std::memcmp(bytes, canonical.data() + offset, length) == 0,
            "Embedded PNG frame is byte-identical to the canonical ICO");
        const auto loaded = static_cast<HICON>(LoadImageW(module.value, MAKEINTRESOURCEW(IDI_ZOEY),
            IMAGE_ICON, size, size, 0));
        require(loaded != nullptr, "Load an embedded application icon size");
        DestroyIcon(loaded);
        ++index;
    }
}

struct Child {
    PROCESS_INFORMATION process{};
    ~Child() {
        if (process.hProcess) {
            if (WaitForSingleObject(process.hProcess, 0) == WAIT_TIMEOUT) {
                TerminateProcess(process.hProcess, 1);
                WaitForSingleObject(process.hProcess, 5000);
            }
            CloseHandle(process.hProcess);
        }
        if (process.hThread) CloseHandle(process.hThread);
    }
};

struct Search {
    DWORD process{};
    HWND window{};
};

BOOL CALLBACK find_window(HWND window, LPARAM context) {
    auto& search = *reinterpret_cast<Search*>(context);
    DWORD process{};
    GetWindowThreadProcessId(window, &process);
    if (process != search.process) return TRUE;
    wchar_t name[64]{};
    GetClassNameW(window, name, 64);
    if (std::wstring_view(name) != L"Xui.Window.1") return TRUE;
    search.window = window;
    return FALSE;
}

bool has_icon(HWND window, WPARAM slot) {
    DWORD_PTR result{};
    return SendMessageTimeoutW(window, WM_GETICON, slot, 0, SMTO_ABORTIFHUNG, 500, &result) && result;
}

void runtime_icons(const wchar_t* path, const wchar_t* arguments = L"") {
    Child child;
    STARTUPINFOW startup{sizeof(startup)};
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_SHOWNOACTIVATE;
    std::wstring command = L"\"" + std::wstring(path) + L"\" " + arguments;
    require(CreateProcessW(path, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
        nullptr, nullptr, &startup, &child.process) != FALSE, "Start native sample");
    Search search{child.process.dwProcessId};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    bool ready{};
    while (std::chrono::steady_clock::now() < deadline) {
        require(WaitForSingleObject(child.process.hProcess, 0) == WAIT_TIMEOUT,
            "Sample must remain running until icons arrive");
        EnumWindows(find_window, reinterpret_cast<LPARAM>(&search));
        if (search.window && has_icon(search.window, ICON_SMALL) && has_icon(search.window, ICON_BIG)) {
            ready = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    require(ready, "Sample must apply both native HWND icon slots");
    require(PostMessageW(search.window, WM_CLOSE, 0, 0) != FALSE, "Close native sample");
    require(WaitForSingleObject(child.process.hProcess, 10000) == WAIT_OBJECT_0, "Sample must close cleanly");
    DWORD exit{};
    require(GetExitCodeProcess(child.process.hProcess, &exit) && !exit, "Sample must return success");
}
}

int wmain(int argc, wchar_t** argv) {
    try {
        require(argc == 11, "Expected eight samples, one unbranded test host, and the canonical ICO");
        std::ifstream stream(std::filesystem::path(argv[10]), std::ios::binary);
        require(stream.is_open(), "Open canonical application ICO");
        const std::vector<unsigned char> canonical{std::istreambuf_iterator<char>(stream), {}};
        require(canonical.size() >= 6 + 10 * 16 && word(canonical.data()) == 0 &&
            word(canonical.data() + 2) == 1 && word(canonical.data() + 4) == 10,
            "Canonical ICO contains ten frames");
        bool missing{};
        try { xui::demo::application_icon_source(); }
        catch (const std::runtime_error&) { missing = true; }
        require(missing, "Branding must reject a host without the application resource");
        require(xui::demo::utf8(L"Zoey — 日本語") == "Zoey — 日本語", "Preserve Unicode icon paths and errors");
        for (int i = 1; i <= 8; ++i) {
            std::wcout << L"Checking " << argv[i] << L'\n';
            resources(argv[i], true, canonical);
            runtime_icons(argv[i]);
            if (std::wstring_view(argv[i]).ends_with(L"xui_gallery.exe"))
                runtime_icons(argv[i], L"--system-titlebar");
        }
        resources(argv[9], false, canonical);
        std::cout << "Native branding tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
