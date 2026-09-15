#include "image_fixtures.hpp"
#include "suggestion_capture.hpp"
#include "../demo/browser.hpp"
#include "../src/images.hpp"
#include "../src/drawing.hpp"
#include "../src/list_peer.hpp"
#include "xui/application.hpp"
#include <chrono>
#include <iostream>
#include <thread>
#include <shobjidl.h>
#include <shellapi.h>
#include <shlobj.h>

namespace xui {
struct DrawingTestAccess {
    static void lose() { Drawing::end_result_override_ = D2DERR_RECREATE_TARGET; }
};
}
namespace {
using namespace xui;
using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
std::function<void(HWND)> tick;
std::exception_ptr failure;
void CALLBACK timer(HWND hwnd, UINT, UINT_PTR, DWORD) noexcept {
    try { tick(hwnd); }
    catch (...) { failure = std::current_exception(); PostMessageW(hwnd, WM_CLOSE, 0, 0); }
}
std::jthread driver() {
    return std::jthread([] {
        const auto deadline = Clock::now() + 10s;
        HWND hwnd{};
        while (!hwnd && Clock::now() < deadline) {
            EnumWindows([](HWND window, LPARAM data) -> BOOL {
                DWORD process{}; GetWindowThreadProcessId(window, &process);
                wchar_t cls[64]{}; GetClassNameW(window, cls, 64);
                if (process == GetCurrentProcessId() && IsWindowVisible(window) &&
                    std::wstring_view(cls) == L"Xui.Window.1") {
                    *reinterpret_cast<HWND*>(data) = window; return FALSE;
                }
                return TRUE;
            }, reinterpret_cast<LPARAM>(&hwnd));
            std::this_thread::sleep_for(10ms);
        }
        if (hwnd) {
            SetWindowPos(hwnd, HWND_TOPMOST, 40, 40, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
            SetTimer(hwnd, 83, 20, timer);
        }
    });
}
HWND child(HWND root, const wchar_t* cls, const wchar_t* title = nullptr, int ordinal = 0) {
    struct Search { const wchar_t* cls; const wchar_t* title; int ordinal; HWND found{}; } search{cls, title, ordinal};
    EnumChildWindows(root, [](HWND hwnd, LPARAM data) -> BOOL {
        auto& s = *reinterpret_cast<Search*>(data);
        wchar_t cls[128]{}, title[128]{};
        GetClassNameW(hwnd, cls, 128); GetWindowTextW(hwnd, title, 128);
        if (_wcsicmp(cls, s.cls) == 0 && (!s.title || std::wstring_view(title) == s.title) && s.ordinal-- == 0) {
            s.found = hwnd; return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    return search.found;
}
void command(HWND root, const wchar_t* title) {
    const auto button = child(root, L"Xui.Control.1", title);
    check(button != nullptr, "Find browser command");
    SendMessageW(button, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(10, 10));
    SendMessageW(button, WM_LBUTTONUP, 0, MAKELPARAM(10, 10));
}
LRESULT metric(HWND hwnd, int key) { return SendMessageW(hwnd, WM_APP + 60, key, 0); }
COLORREF pixel(HWND hwnd, HWND list, float x, float y, UINT dpi = 0) {
    POINT point{};
    MapWindowPoints(list, hwnd, &point, 1);
    const float scale = (dpi ? dpi : GetDpiForWindow(hwnd)) / 96.0f;
    const auto dc = GetDC(hwnd);
    const auto color = GetPixel(dc, point.x + static_cast<int>(x * scale), point.y + static_cast<int>(y * scale));
    ReleaseDC(hwnd, dc); return color;
}
bool near_color(COLORREF actual, COLORREF expected, int tolerance = 5) {
    return std::abs(int(GetRValue(actual)) - int(GetRValue(expected))) <= tolerance &&
        std::abs(int(GetGValue(actual)) - int(GetGValue(expected))) <= tolerance &&
        std::abs(int(GetBValue(actual)) - int(GetBValue(expected))) <= tolerance;
}
void capture(HWND hwnd, const std::filesystem::path& path) {
    RECT rect{}; GetClientRect(hwnd, &rect);
    const auto dc = GetDC(hwnd), memory = CreateCompatibleDC(dc);
    auto bitmap = CreateCompatibleBitmap(dc, rect.right, rect.bottom);
    auto old = SelectObject(memory, bitmap);
    check(BitBlt(memory, 0, 0, rect.right, rect.bottom, dc, 0, 0, SRCCOPY) != FALSE, "Capture owned client");
    SelectObject(memory, old);
    BITMAPINFO info{};
    info.bmiHeader = {sizeof(BITMAPINFOHEADER), rect.right, -rect.bottom, 1, 32, BI_RGB};
    std::vector<char> pixels(std::size_t(rect.right) * rect.bottom * 4);
    check(GetDIBits(memory, bitmap, 0, rect.bottom, pixels.data(), &info, DIB_RGB_COLORS) != 0, "Read capture");
    BITMAPFILEHEADER file{0x4d42, static_cast<DWORD>(54 + pixels.size()), 0, 0, 54};
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(&file), sizeof(file));
    out.write(reinterpret_cast<const char*>(&info.bmiHeader), sizeof(info.bmiHeader));
    out.write(pixels.data(), pixels.size());
    DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(hwnd, dc);
}
bool quiet() { const auto s = ImageResources::statistics(); return !s.active && !s.queued; }
std::uint64_t process_cpu() {
    FILETIME created{}, exited{}, kernel{}, user{};
    check(GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user) != FALSE, "Read process CPU time");
    return (std::uint64_t(kernel.dwHighDateTime) << 32) + kernel.dwLowDateTime +
        (std::uint64_t(user.dwHighDateTime) << 32) + user.dwLowDateTime;
}
void finish(int result, std::jthread& thread) {
    thread.join(); tick = {};
    if (failure) std::rethrow_exception(failure);
    check(result == 0, "Window exits successfully");
    ImageResources::clear_unused();
    check(ImageResources::statistics().gpu_bytes == 0, "Window close releases thumbnail bitmaps");
}
std::shared_ptr<const std::vector<FileItem>> items(const std::filesystem::path& directory) {
    auto rows = std::make_shared<std::vector<FileItem>>();
    rows->push_back({1, L"Opaque", (directory / L"00-opaque.PNG").wstring(), false});
    rows->push_back({2, L"JPEG", (directory / L"01-photo.jpg").wstring(), false});
    rows->push_back({3, L"Alpha", (directory / L"02-alpha.png").wstring(), false});
    rows->push_back({4, L"Corrupt", (directory / L"03-corrupt.png").wstring(), false});
    rows->push_back({5, L"Document", (directory / L"readme.txt").wstring(), false});
    rows->push_back({6, L"Folder", (directory / L"folder").wstring(), true});
    for (std::size_t i = 0; i < 20000; ++i)
        rows->push_back({i + 7, L"image-" + std::to_wstring(i), (directory / L"00-opaque.PNG").wstring(), false});
    return rows;
}
void public_list(const std::filesystem::path& directory) {
    Window window({L"Thumbnail control test", {700, 480}});
    auto root = std::make_shared<Stack>(Axis::vertical);
    auto list = std::make_shared<FileList>();
    auto image = std::make_shared<Image>();
    image->set_preferred_size({100, 40});
    list->set_items(items(directory));
    root->add(image); root->add(list, 1); window.set_content(root);
    std::size_t errors{};
    list->on_thumbnail_error([&](ItemId id, const std::wstring& error) {
        check((id == 4 || id == 1) && !error.empty(), "Failure callback identifies the visible item");
        ++errors;
    });
    const auto before = ImageResources::statistics();
    int phase{}, waits{};
    std::uint64_t uploaded{}, decoded{};
    LRESULT idle{};
    UINT original_dpi{};
    const auto start = Clock::now();
    tick = [&](HWND hwnd) {
        check(Clock::now() - start < 30s, "Public list test completes");
        const auto s = ImageResources::statistics();
        check(metric(hwnd, 22) <= 24 && s.queued <= 64, "20,006 rows keep bounded slots and requests");
        const auto native = child(hwnd, L"Xui.FileList.1");
        if (!quiet() || ++waits < 5) return;
        waits = 0;
        if (phase == 0) {
            check(s.decoded == before.decoded && !metric(hwnd, 22), "Default FileList does not load images");
            list->set_thumbnails(true);
            image->set_source((directory / L"standalone.png").wstring(), {24, 24});
        } else if (phase == 1) {
            check(metric(hwnd, 23) > 3 && errors == 1, "Visible previews load and corrupt files report one failure");
            check(pixel(hwnd, native, 23, 16) == RGB(0x20, 0x40, 0xc0), "Actual opaque PNG pixels render");
            const auto jpeg = pixel(hwnd, native, 23, 48);
            check(GetRValue(jpeg) > 180 && GetGValue(jpeg) < 60, "Actual JPEG pixels render");
            check(pixel(hwnd, native, 23, 6) == pixel(hwnd, native, 9, 6), "Wide PNG keeps its aspect ratio");
            check(pixel(hwnd, native, 23, 80) != pixel(hwnd, native, 9, 80), "Alpha PNG blends over row background");
            check(image->status() == ImageStatus::ready, "Image control and list share the root bitmap owner");
            list->select(2, false);
            uploaded = s.uploaded;
        } else if (phase == 2) {
            check(pixel(hwnd, native, 23, 80) != pixel(hwnd, native, 9, 80), "Alpha PNG also blends over selection");
            window.set_theme(ThemeMode::light);
        } else if (phase == 3) {
            check(s.uploaded == uploaded, "Selection and theme reuse bitmaps across Image and FileList");
            DrawingTestAccess::lose();
            InvalidateRect(hwnd, nullptr, FALSE);
        } else if (phase == 4) {
            check(s.uploaded > uploaded && pixel(hwnd, native, 23, 16) == RGB(0x20, 0x40, 0xc0),
                "Device recreation restores actual pixels");
            original_dpi = GetDpiForWindow(hwnd);
            RECT rect{}; GetWindowRect(hwnd, &rect);
            SendMessageW(hwnd, WM_DPICHANGED, MAKELONG(144, 144), reinterpret_cast<LPARAM>(&rect));
        } else if (phase == 5) {
            check(pixel(hwnd, native, 23, 16, 144) == RGB(0x20, 0x40, 0xc0), "DPI-sized thumbnail renders at 144 DPI");
            RECT rect{}; GetWindowRect(hwnd, &rect);
            SendMessageW(hwnd, WM_DPICHANGED, MAKELONG(original_dpi, original_dpi), reinterpret_cast<LPARAM>(&rect));
            list->scroll_to(32000);
        } else if (phase == 6) {
            check(metric(hwnd, 23) > 0 && s.decoded < before.decoded + 12,
                "Invisible thousands never decode and repeated paths share pixels");
            list->set_filter(L"JPEG");
        } else if (phase == 7) {
            check(metric(hwnd, 22) == 1 && GetRValue(pixel(hwnd, native, 23, 16)) > 180,
                "Filtering reuses stable file identity, not the old row index");
            list->set_filter(L"Opaque");
        } else if (phase == 8) {
            image_fixture::hr(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
            image_fixture::png(directory / L"00-opaque.PNG", 80, 40, 0xff20c040);
            CoUninitialize();
            list->reload_thumbnails();
        } else if (phase == 9) {
            check(pixel(hwnd, native, 23, 16) == RGB(0x20, 0xc0, 0x40), "Reload checks changed file version off the UI thread");
            std::filesystem::remove(directory / L"00-opaque.PNG");
            list->set_items(items(directory));
            list->set_filter(L"Opaque");
        } else if (phase == 10) {
            check(metric(hwnd, 23) == 0 && errors >= 2, "Source refresh detects deletion and keeps fallback icons");
            list->set_thumbnails(false); image->unload();
        } else if (phase == 11) {
            check(!metric(hwnd, 22) && !s.gpu_bytes, "Disabling thumbnails releases all visible GPU pins");
            decoded = s.decoded;
            list->set_filter(L"Document"); list->set_thumbnails(true);
        } else if (phase == 12) {
            check(s.decoded >= decoded && metric(hwnd, 23) == 1, "Text-only views now request Shell file icons");
            idle = metric(hwnd, 0);
        } else {
            check(metric(hwnd, 0) == idle, "Decoded/disabled lists do not repaint while idle");
            window.close();
        }
        ++phase;
    };
    auto thread = driver(); finish(Application::run(window), thread);
}
void browser(const std::filesystem::path& directory, std::size_t count) {
    const auto before = ImageResources::statistics();
    int phase{}, waits{}, pages{};
    std::uint64_t hits{};
    LRESULT idle{};
    std::size_t peak_slots{};
    std::size_t sampled_queue_peak{};
    double scroll_ms{}, cold_ms{}, warm_ms{}, scroll_ready_ms{};
    auto operation = Clock::now();
    const auto start = Clock::now();
    tick = [&](HWND hwnd) {
        check(Clock::now() - start < 45s, "Explorer thumbnail workload completes");
        const auto s = ImageResources::statistics();
        peak_slots = std::max(peak_slots, static_cast<std::size_t>(metric(hwnd, 22)));
        sampled_queue_peak = std::max(sampled_queue_peak, s.queued);
        check(peak_slots <= 48 && s.queued <= 64 && s.active <= 2, "Explorer keeps bounded slots, queue and two workers");
        check(s.cpu_bytes + s.cpu_reserved <= ImageLimits::cpu_bytes && s.gpu_bytes + s.gpu_reserved <= ImageLimits::gpu_bytes,
            "Global image accounting includes thumbnails");
        check(s.cpu_peak <= 128 * 36 * 36 * 4 && s.gpu_peak <= 48 * 36 * 36 * 4,
            "Thumbnail output fits small physical icon boxes, not full-resolution images");
        if (metric(hwnd, 4) || !quiet() || ++waits < 6) return;
        waits = 0;
        const auto list = child(hwnd, L"Xui.FileList.1", L"Files");
        if (phase == 0) {
            check(metric(hwnd, 9) == static_cast<LRESULT>(count + 6), "Browser reads the complete fixture folder");
            check(metric(hwnd, 23) > 0 && s.decoded - before.decoded < 24, "Only visible Explorer rows decode");
            check(pixel(hwnd, list, 23, 48) == RGB(0x20, 0x40, 0xc0), "Explorer renders image pixels beside filenames");
            check(pixel(hwnd, list, 23, 16) != RGB(0x20, 0x40, 0xc0), "Folder does not display another file's preview");
            check(GetRValue(pixel(hwnd, list, 23, 80)) > 180, "Explorer renders JPEG pixels");
            cold_ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
            capture(hwnd, directory.parent_path() / (L"explorer-" + std::to_wstring(count) + L".bmp"));
            suggestion_capture::memory(GetCurrentProcess(), "explorer-cold");
            hits = s.cache_hits;
            operation = Clock::now();
            command(hwnd, L"Split panes");
        } else if (phase == 1) {
            const auto right = child(hwnd, L"Xui.FileList.1", L"Right files");
            check(right && IsWindowVisible(right) && s.cache_hits > hits, "Both panes enable and share cached previews");
            check(pixel(hwnd, right, 23, 48) == RGB(0x20, 0x40, 0xc0), "Right pane renders the same cached PNG");
            warm_ms = std::chrono::duration<double, std::milli>(Clock::now() - operation).count();
            command(hwnd, L"Theme");
        } else if (phase == 2) {
            capture(hwnd, directory.parent_path() / (L"explorer-split-" + std::to_wstring(count) + L".bmp"));
            command(hwnd, L"Split panes");
        } else if (phase == 3) {
            check(metric(hwnd, 22) <= 24, "Hidden pane releases thumbnail slots");
            if (pages) scroll_ready_ms += std::chrono::duration<double, std::milli>(Clock::now() - operation).count();
            operation = Clock::now();
            for (int i = 0; i < 5; ++i) SendMessageW(list, WM_KEYDOWN, VK_NEXT, 0);
            scroll_ms += std::chrono::duration<double, std::milli>(Clock::now() - operation).count();
            if (++pages < 8) return;
        } else if (phase == 4) {
            scroll_ready_ms += std::chrono::duration<double, std::milli>(Clock::now() - operation).count();
            const auto edit = child(hwnd, L"EDIT", nullptr, 1);
            check(edit != nullptr, "Find browser search");
            SetWindowTextW(edit, L"01-photo");
        } else if (phase == 5) {
            check(metric(hwnd, 8) == 1 && GetRValue(pixel(hwnd, list, 23, 16)) > 180,
                "Browser filtering cannot display a previous row's thumbnail");
            SetWindowTextW(child(hwnd, L"EDIT", nullptr, 1), L"");
            command(hwnd, L"+");
        } else if (phase == 6) {
            check(metric(hwnd, 9) == static_cast<LRESULT>(count + 6), "New tab uses the same thumbnail-enabled list");
            command(hwnd, L"Refresh");
        } else if (phase == 7) {
            check(metric(hwnd, 23) > 0, "Directory refresh restores visible previews");
            SendMessageW(list, WM_KEYDOWN, VK_HOME, 0);
            SendMessageW(list, WM_KEYDOWN, VK_RETURN, 0);
        } else if (phase == 8) {
            check(metric(hwnd, 9) == 0 && metric(hwnd, 22) == 0 && !s.gpu_bytes,
                "Navigation to an empty folder releases slots and bitmaps");
            command(hwnd, L"Back");
        } else if (phase == 9) {
            check(metric(hwnd, 23) > 0 && pixel(hwnd, list, 23, 48) == RGB(0x20, 0x40, 0xc0),
                "Back navigation restores correct image identity");
            idle = metric(hwnd, 0);
        } else {
            check(metric(hwnd, 0) == idle, "Explorer thumbnails have no idle paint loop");
            suggestion_capture::memory(GetCurrentProcess(), "explorer-warm");
            std::cout << "explorer images=" << count << " cold_ready_ms=" << cold_ms << " scroll_input_ms=" << scroll_ms / 40
                << " warm_pane_ready_ms=" << warm_ms << " scroll_ready_ms=" << scroll_ready_ms / 8
                << " peak_slots=" << peak_slots << " sampled_queue_peak=" << sampled_queue_peak
                << " decoded=" << s.decoded - before.decoded << " cache_hits=" << s.cache_hits - before.cache_hits
                << " cpu_peak=" << s.cpu_peak << " gpu_peak=" << s.gpu_peak << '\n';
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
        }
        ++phase;
    };
    auto thread = driver(); finish(run_file_browser({directory}), thread);
}
HANDLE entered{}, released{};
std::atomic<bool> shell_sta{};
void block(ImageDecodeStage stage) {
    if (stage == ImageDecodeStage::reserved) { SetEvent(entered); WaitForSingleObject(released, 10000); }
}
void block_shell(ImageDecodeStage stage) {
    APTTYPE apartment{};
    APTTYPEQUALIFIER qualifier{};
    shell_sta = SUCCEEDED(CoGetApartmentType(&apartment, &qualifier)) && apartment == APTTYPE_STA;
    block(stage);
}
void close_blocked(const std::filesystem::path& directory) {
    entered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    released = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ImageDecodeTestAccess::hook = block;
    Window window({L"Close thumbnail test", {400, 300}});
    auto root = std::make_shared<Stack>(Axis::vertical);
    auto list = std::make_shared<FileList>();
    list->set_thumbnails(true); list->set_items(items(directory));
    list->on_thumbnail_error([](ItemId, const std::wstring&) { check(false, "Cancelled errors cannot reach a closed or filtered list"); });
    root->add(list, 1); window.set_content(root);
    auto close_start = Clock::now();
    const auto start = Clock::now();
    tick = [&](HWND hwnd) {
        check(Clock::now() - start < 10s, "Thumbnail decode reaches blocked boundary");
        if (WaitForSingleObject(entered, 0) != WAIT_OBJECT_0) return;
        list->set_filter(L"no-matching-row");
        RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
        check(metric(hwnd, 22) == 0, "Filtering cancels a blocked visible slot");
        list->set_filter(L"");
        RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
        check(metric(hwnd, 22) > 0, "Window closes with replacement thumbnail requests still queued");
        close_start = Clock::now(); window.close();
    };
    auto thread = driver(); finish(Application::run(window), thread);
    check(WaitForSingleObject(released, 0) == WAIT_TIMEOUT, "Window close does not join the blocked decoder");
    std::cout << "thumbnail_close_blocked_ms=" << std::chrono::duration<double, std::milli>(Clock::now() - close_start).count() << '\n';
    ImageDecodeTestAccess::hook = nullptr; SetEvent(released);
    const auto deadline = Clock::now() + 10s;
    while (!quiet()) { check(Clock::now() < deadline, "Cancelled thumbnail worker drains"); std::this_thread::sleep_for(1ms); }
    ImageResources::clear_unused();
    const auto s = ImageResources::statistics();
    check(!s.cpu_bytes && !s.cpu_reserved && !s.gpu_bytes, "Stale completion releases all owned pixels");
    CloseHandle(entered); CloseHandle(released); entered = released = nullptr;
}
void tall_list(const std::filesystem::path& directory) {
    auto list = std::make_shared<FileList>();
    list->set_items(items(directory)); list->set_thumbnails(true); list->set_viewport_height(32000);
    ListPeer peer(list, [] {}, [](HWND) {});
    std::vector<std::uint64_t> retained;
    std::size_t remaining = 48;
    peer.sync_thumbnails(true, {0, 0, 320, 32000}, {}, retained, remaining);
    check(peer.thumbnail_count() == 24 && remaining == 24, "Giant viewport obeys the per-list hard cap");
    peer.sync_thumbnails(false, {}, {}, retained, remaining);
    check(!peer.thumbnail_count(), "Hidden giant viewport cancels all slots");
    const auto deadline = Clock::now() + 10s;
    while (!quiet()) { check(Clock::now() < deadline, "Cancelled giant viewport drains"); std::this_thread::sleep_for(1ms); }
    ImageResources::clear_unused();
}
void fixtures(const std::filesystem::path& directory, std::size_t count) {
    std::filesystem::create_directories(directory / L"folder");
    image_fixture::png(directory / L"00-opaque.PNG", 64, 32, 0xff2040c0);
    image_fixture::jpeg(directory / L"01-photo.jpg", 64, 64, 0xffd02020);
    image_fixture::png(directory / L"02-alpha.png", 64, 64, 0x8020c040);
    std::ofstream(directory / L"03-corrupt.png") << "Not an image";
    std::ofstream(directory / L"readme.txt") << "Ordinary file";
    for (std::size_t i = 0; i < count; ++i) {
        wchar_t name[40]{}; swprintf_s(name, L"image-%04zu.png", i);
        image_fixture::png(directory / name, 64, 64, 0xff000000u | (static_cast<unsigned>(i) * 193u + 0x2040c0u));
    }
}
std::shared_ptr<ImageRequest> completed(const std::filesystem::path& path, UINT size = 24,
    ImageKind kind = ImageKind::shell) {
    auto request = request_image(path.wstring(), {size, size}, {}, kind);
    const auto deadline = Clock::now() + 20s;
    for (;;) {
        {
            std::lock_guard lock(request->mutex);
            if (request->done) return request;
        }
        check(Clock::now() < deadline, "Shell request completes");
        std::this_thread::sleep_for(1ms);
    }
}
std::shared_ptr<const ImagePixels> loaded(const std::filesystem::path& path, UINT size = 24) {
    auto result = completed(path, size);
    if (!result->pixels) {
        std::wcerr << path << L": " << result->error << '\n';
        throw std::runtime_error("Shell pixels load");
    }
    return result->pixels;
}
void write_icon(const std::filesystem::path& exe, unsigned color, bool legacy = false) {
    static unsigned revision{};
    const auto suffix = L"." + std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(++revision);
    const std::filesystem::path replacement = exe.wstring() + suffix + L".replacement";
    const std::filesystem::path retired = exe.wstring() + suffix + L".retired";
    std::filesystem::copy_file(exe, replacement);
    constexpr unsigned side = 32;
    BITMAPINFOHEADER header{sizeof(header), side, side * 2, 1, 32, BI_RGB};
    std::vector<BYTE> data(sizeof(header) + side * side * 4 + side * 4);
    memcpy(data.data(), &header, sizeof(header));
    for (unsigned y = 0; y < side; ++y) for (unsigned x = 0; x < side; ++x) {
        const bool inside = x >= 4 && x < 28 && y >= 8 && y < 24;
        const unsigned value = inside ? (legacy ? color & 0xffffff : color) : 0;
        memcpy(data.data() + sizeof(header) + (y * side + x) * 4, &value, 4);
        if (!inside) data[sizeof(header) + side * side * 4 + y * 4 + x / 8] |= BYTE(0x80 >> (x % 8));
    }
    // GRPICONDIR + one GRPICONDIRENTRY, serialized without host structure padding.
    BYTE group[20]{0, 0, 1, 0, 1, 0, 32, 32, 0, 0, 1, 0, 32, 0};
    const DWORD bytes = static_cast<DWORD>(data.size());
    memcpy(group + 14, &bytes, 4); group[18] = 1;
    const auto deadline = Clock::now() + 5s;
    for (;;) {
        const auto update = BeginUpdateResourceW(replacement.c_str(), FALSE);
        DWORD error = update ? ERROR_SUCCESS : GetLastError();
        if (update) {
            const bool success = UpdateResourceW(update, RT_ICON, MAKEINTRESOURCEW(1), 0, data.data(), bytes) &&
                UpdateResourceW(update, RT_GROUP_ICON, MAKEINTRESOURCEW(1), 0, group, sizeof(group));
            if (!success) error = GetLastError();
            if (!EndUpdateResourceW(update, !success) && !error) error = GetLastError();
        }
        if (!error) break;
        if ((error != ERROR_SHARING_VIOLATION && error != ERROR_LOCK_VIOLATION && error != ERROR_ACCESS_DENIED) || Clock::now() >= deadline)
            throw std::runtime_error("Write owned fixture resources: Win32 error " + std::to_string(error));
        std::this_thread::sleep_for(50ms);
    }
    // Shell can map the old PE resources beyond GetImage. Replace the file at the
    // same canonical path rather than editing those mapped pages in place.
    std::filesystem::rename(exe, retired);
    try { std::filesystem::rename(replacement, exe); }
    catch (...) { std::filesystem::rename(retired, exe); throw; }
    std::error_code cleanup;
    std::filesystem::remove(retired, cleanup);
    SHChangeNotify(SHCNE_UPDATEITEM, SHCNF_PATHW | SHCNF_FLUSH, exe.c_str(), nullptr);
}
void shell_fixtures(const std::filesystem::path& directory, const std::filesystem::path& executable) {
    std::filesystem::create_directories(directory / L"folder");
    for (const auto name : {L"fixture.exe", L"legacy.exe", L"alpha.exe"}) {
        const auto path = directory / name;
        std::filesystem::copy_file(executable, path, std::filesystem::copy_options::overwrite_existing);
        write_icon(path, std::wstring_view(name) == L"alpha.exe" ? 0x8020b8e0 : 0xff20b8e0,
            std::wstring_view(name) == L"legacy.exe");
    }
    std::ofstream(directory / L"readme.txt") << "Shell file association";
    std::ofstream(directory / L"document.pdf") << "No PDF thumbnail";
    std::ofstream(directory / L"document.docx") << "No document thumbnail";
    std::ofstream(directory / L"unknown.xui-unknown-type") << "No registered handler";
    image_fixture::png(directory / L"preview.png", 64, 32, 0xff2040c0);
    image_fixture::png(directory / L"alpha-preview.png", 64, 32, 0x8020b8e0);
    Microsoft::WRL::ComPtr<IShellLinkW> link;
    image_fixture::hr(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link)));
    image_fixture::hr(link->SetPath((directory / L"fixture.exe").c_str()));
    Microsoft::WRL::ComPtr<IPersistFile> persist;
    image_fixture::hr(link.As(&persist));
    image_fixture::hr(persist->Save((directory / L"fixture.lnk").c_str(), TRUE));
}
void shell_pixels(const std::filesystem::path& directory) {
    wchar_t system[MAX_PATH]{};
    check(GetSystemDirectoryW(system, MAX_PATH) != 0, "Find native System32");
    const auto cmd = std::filesystem::path(system) / L"cmd.exe";
    const auto before = ImageResources::statistics();
    for (const auto size : {20u, 24u, 30u, 36u, 40u, 48u}) {
        const auto pixels = loaded(directory / L"fixture.exe", size);
        check(pixels->size.width <= size && pixels->size.height <= size, "Physical DPI size bounds Shell pixels");
        const auto center = (pixels->size.height / 2 * pixels->size.width + pixels->size.width / 2) * 4;
        std::cout << "fixture size=" << size << " actual=" << pixels->size.width << "x" << pixels->size.height
            << " center_bgra=" << std::to_integer<int>(pixels->pixels[center]) << ","
            << std::to_integer<int>(pixels->pixels[center + 1]) << "," << std::to_integer<int>(pixels->pixels[center + 2])
            << "," << std::to_integer<int>(pixels->pixels[center + 3]) << '\n';
        check(near_color(RGB(std::to_integer<int>(pixels->pixels[center + 2]),
            std::to_integer<int>(pixels->pixels[center + 1]), std::to_integer<int>(pixels->pixels[center])), RGB(0x20, 0xb8, 0xe0)) &&
            std::to_integer<int>(pixels->pixels[center + 3]) >= 250,
            "Shell extracts original executable resource color");
        check(pixels->pixels[3] == std::byte{}, "Shell icon retains transparent margins");
        const auto alpha = loaded(directory / L"alpha.exe", size);
        check(alpha->size == ImageSize{size, size}, "Shell returns the requested physical icon size");
        const auto middle = (size / 2 * size + size / 2) * 4;
        for (unsigned channel = 0; channel < 4; ++channel) {
            constexpr unsigned expected[]{0x70, 0x5c, 0x10, 0x80};
            check(std::abs(std::to_integer<int>(alpha->pixels[middle + channel]) - static_cast<int>(expected[channel])) <= 1,
                "Shell straight alpha converts once to premultiplied BGRA");
        }
        for (const auto& image : {pixels, alpha, loaded(directory / L"folder", size)}) {
            for (size_t i = 0; i < image->pixels.size(); i += 4) {
                check(image->pixels[i] <= image->pixels[i + 3] &&
                    image->pixels[i + 1] <= image->pixels[i + 3] &&
                    image->pixels[i + 2] <= image->pixels[i + 3],
                    "Shell edge colors never exceed premultiplied alpha");
            }
        }
    }
    const auto legacy = loaded(directory / L"legacy.exe");
    check(legacy->pixels[3] == std::byte{}, "Legacy icon AND mask retains transparent margins");
    const auto command = loaded(cmd);
    const auto generic = loaded(directory / L"unknown.xui-unknown-type");
    check(command->pixels != generic->pixels, "Native cmd.exe has a non-generic Shell icon");
    for (const auto name : {L"readme.txt", L"folder", L"fixture.lnk", L"document.pdf", L"document.docx"}) loaded(directory / name);
    loaded(std::filesystem::path(system) / L"kernel32.dll");
    const auto preview = loaded(directory / L"preview.png");
    check(preview->size.width > preview->size.height, "Shell thumbnail handler returns a wide image preview");
    auto wic = completed(directory / L"preview.png", 24, ImageKind::wic);
    check(wic->pixels && wic->pixels->id != preview->id, "WIC and Shell cache keys do not collide");
    const auto alpha_preview = loaded(directory / L"alpha-preview.png");
    const auto alpha_wic = completed(directory / L"alpha-preview.png", 24, ImageKind::wic);
    check(alpha_wic->pixels && alpha_preview->size == alpha_wic->pixels->size &&
        alpha_preview->pixels == alpha_wic->pixels->pixels,
        "Shell thumbnails and direct WIC decoding preserve the same translucent pixels");
    auto absent = completed(directory / L"absent.exe");
    check(!absent->pixels && !absent->error.empty(), "Missing Shell item has an explicit nonfatal error");
    {
        auto list = std::make_shared<FileList>();
        auto rows = std::make_shared<std::vector<FileItem>>();
        rows->push_back({1, L"absent.exe", (directory / L"absent.exe").wstring(), false});
        list->set_items(rows); list->set_thumbnails(true);
        unsigned errors{};
        list->on_thumbnail_error([&](ItemId id, const std::wstring& error) {
            check(id == 1 && !error.empty(), "Shell failure identifies the original row");
            ++errors;
        });
        ListPeer peer(list, [] {}, [](HWND) {});
        std::vector<std::uint64_t> retained;
        std::size_t remaining = 48;
        peer.sync_thumbnails(true, {0, 0, 320, 320}, {}, retained, remaining);
        const auto deadline = Clock::now() + 10s;
        while (!quiet()) { check(Clock::now() < deadline, "Missing item request completes"); std::this_thread::sleep_for(1ms); }
        for (int i = 0; i < 10; ++i) {
            remaining = 48;
            peer.sync_thumbnails(true, {0, 0, 320, 320}, {}, retained, remaining);
        }
        check(errors == 1 && retained.empty() && quiet(), "Failed Shell slot keeps vector fallback and reports once, without frame retries");
    }
    auto cached = loaded(directory / L"fixture.exe");
    check(loaded(std::filesystem::relative(directory / L"fixture.exe"))->id == cached->id,
        "Relative and absolute paths identify the same Shell item");
    const auto old_id = cached->id;
    write_icon(directory / L"fixture.exe", 0xffd03070);
    auto changed = loaded(directory / L"fixture.exe");
    std::cout << "fixture_version_before=" << old_id << " after=" << changed->id
        << " pixels_changed=" << (changed->pixels != cached->pixels) << '\n';
    check(changed->id != old_id, "Executable resource changes invalidate XUI's versioned pixel cache");
    // Windows can retain an old per-path icon after SHChangeNotify. A new path bypasses that OS cache.
    const auto updated = directory / (L"updated-" + std::to_wstring(GetTickCount64()) + L".exe");
    std::filesystem::copy_file(directory / L"fixture.exe", updated);
    check(loaded(updated)->pixels != cached->pixels, "Updated executable resources produce different Shell pixels at a new path");
    std::filesystem::remove(updated);
    write_icon(directory / L"fixture.exe", 0xff20b8e0);
    const auto gdi = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    for (int i = 0; i < 50; ++i) {
        clear_image_cache();
        loaded(directory / L"fixture.exe"); loaded(directory / L"legacy.exe"); loaded(cmd);
    }
    const auto after_gdi = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    check(after_gdi <= gdi + 2, "Repeated Shell extraction releases HBITMAP and HICON handles");
    const auto after = ImageResources::statistics();
    check(after.rejected == before.rejected + 2, "Missing thumbnails fall through to icons without errors");
    std::wcout << L"native_shell_path=" << cmd << L" gdi_before=" << gdi << L" gdi_after=" << after_gdi << '\n';
}
void shell_window(const std::filesystem::path& directory) {
    Window window({L"Shell icon pixel test", {700, 480}});
    auto root = std::make_shared<Stack>(Axis::vertical);
    auto list = std::make_shared<FileList>();
    auto rows = std::make_shared<std::vector<FileItem>>();
    for (std::size_t i = 0; i < 1000; ++i)
        rows->push_back({i + 1, L"fixture-" + std::to_wstring(i) + L".exe",
            (directory / (i == 0 ? L"alpha.exe" : L"fixture.exe")).wstring(), false});
    list->set_items(rows); list->set_thumbnails(true); root->add(list, 1); window.set_content(root);
    list->on_thumbnail_error([](ItemId, const std::wstring&) { check(false, "Fixture icons do not fail"); });
    int phase{}, waits{};
    UINT dpi{};
    LRESULT idle{};
    auto start = Clock::now();
    tick = [&](HWND hwnd) {
        check(Clock::now() - start < 25s, "Shell pixel window completes");
        check(metric(hwnd, 22) <= 24 && ImageResources::statistics().queued <= 64, "1000 executable rows retain visible slots only");
        if (!quiet() || ++waits < 5) return;
        waits = 0;
        const auto native = child(hwnd, L"Xui.FileList.1");
        const auto scale = dpi ? dpi : GetDpiForWindow(hwnd);
        auto expected = RGB(0x20, 0xb8, 0xe0);
        if (phase <= 4) {
            const auto background = pixel(hwnd, native, 9, 16, scale);
            const auto blend = [](unsigned foreground, unsigned background) {
                return (foreground * 128 + background * 127 + 127) / 255;
            };
            expected = RGB(blend(0x20, GetRValue(background)), blend(0xb8, GetGValue(background)),
                blend(0xe0, GetBValue(background)));
        }
        check(near_color(pixel(hwnd, native, 23, 16, scale), expected),
            "Shell icon alpha blends over dark, selected, and light rows at physical DPI");
        check(pixel(hwnd, native, 23, 6, scale) == pixel(hwnd, native, 9, 6, scale), "Transparent icon margin blends with row background");
        if (phase == 0) { list->select(0, false); }
        else if (phase == 1) { window.set_theme(ThemeMode::light); }
        else if (phase == 2 || phase == 3) {
            dpi = phase == 2 ? 144 : 192;
            RECT rect{}; GetWindowRect(hwnd, &rect);
            SendMessageW(hwnd, WM_DPICHANGED, MAKELONG(dpi, dpi), reinterpret_cast<LPARAM>(&rect));
        } else if (phase == 4) {
            capture(hwnd, directory / L"fixture-200dpi.bmp");
            dpi = GetDpiForWindow(hwnd);
            RECT rect{}; GetWindowRect(hwnd, &rect);
            SendMessageW(hwnd, WM_DPICHANGED, MAKELONG(dpi, dpi), reinterpret_cast<LPARAM>(&rect));
            list->scroll_to(16000);
        } else if (phase == 5) { list->set_filter(L"fixture-900.exe"); }
        else if (phase == 6) { check(metric(hwnd, 22) == 1, "Filtered executable row keeps its icon"); idle = metric(hwnd, 0); }
        else {
            check(metric(hwnd, 0) == idle, "Shell icons do not repaint while idle");
            capture(hwnd, directory / L"fixture-filtered.bmp");
            check(metric(hwnd, 11) == 1, "Shell icons reuse one root render target");
            window.close();
        }
        ++phase;
    };
    auto thread = driver(); finish(Application::run(window), thread);
}
void shell_blocked(const std::filesystem::path& directory) {
    entered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    released = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    clear_image_cache();
    ImageDecodeTestAccess::shell_hook = block_shell;
    auto stale = request_image((directory / L"fixture.exe").wstring(), {24, 24}, {}, ImageKind::shell);
    check(WaitForSingleObject(entered, 10000) == WAIT_OBJECT_0, "Shell extraction reaches the deterministic gate");
    check(shell_sta, "Shell extraction runs in a dedicated COM STA");
    auto wic = completed(directory / L"preview.png", 24, ImageKind::wic);
    check(wic->pixels != nullptr, "Blocked Shell worker does not block WIC");
    std::vector<std::shared_ptr<ImageRequest>> requests;
    for (int i = 0; i < 1000; ++i)
        requests.push_back(request_image((directory / (std::to_wstring(i) + L".exe")).wstring(), {24, 24}, {}, ImageKind::shell));
    check(ImageResources::statistics().queued == 64, "1000 Shell submissions cannot exceed the shared 64-request queue");
    for (auto& request : requests) request->cancel();
    stale->cancel(); wic->cancel(); requests.clear();
    ImageResources::clear_unused();
    // Exercise real list cancellation while the Shell worker is still blocked.
    Window window({L"Blocked Shell close test", {500, 400}});
    auto root = std::make_shared<PageView>();
    auto list = std::make_shared<FileList>();
    auto rows = std::make_shared<std::vector<FileItem>>();
    for (std::size_t i = 0; i < 1000; ++i)
        rows->push_back({i + 1, L"fixture.exe", (directory / (std::to_wstring(i) + L".exe")).wstring(), false});
    {
        auto other = std::make_shared<FileList>();
        other->set_items(rows); other->set_thumbnails(true);
        list->set_items(rows); list->set_thumbnails(true);
        ListPeer first(list, [] {}, [](HWND) {}), second(other, [] {}, [](HWND) {});
        std::vector<std::uint64_t> retained;
        std::size_t remaining = 48;
        first.sync_thumbnails(true, {0, 0, 320, 32000}, {}, retained, remaining);
        second.sync_thumbnails(true, {0, 0, 320, 32000}, {}, retained, remaining);
        check(first.thumbnail_count() == 24 && second.thumbnail_count() == 24 && remaining == 0 &&
            ImageResources::statistics().queued == 48, "Two tall panes request only 48 of 1000 distinct Shell paths");
    }
    ImageResources::clear_unused();
    list->set_items(rows); list->set_thumbnails(true);
    root->add_page(list); root->add_page(std::make_shared<Stack>(Axis::vertical)); window.set_content(root);
    list->on_thumbnail_error([](ItemId, const std::wstring&) { check(false, "Cancelled Shell results do not reach the list"); });
    int phase{};
    tick = [&](HWND hwnd) {
        if (phase == 1) {
            check(metric(hwnd, 22) == 0, "Hidden pane revokes blocked Shell slots");
            root->select(0); ++phase; return;
        }
        if (phase == 2) {
            check(metric(hwnd, 22) > 0, "Returning to the page creates replacement requests");
            window.close(); return;
        }
        const auto start = Clock::now();
        for (int i = 0; i < 10; ++i) {
            list->scroll_to(static_cast<float>(i * 320));
            list->set_filter(i % 2 ? L"fixture" : L"");
            RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
        }
        root->select(1);
        std::cout << "blocked_shell_scroll_filter_ms=" << std::chrono::duration<double, std::milli>(Clock::now() - start).count() << '\n';
        ++phase;
    };
    const auto start = Clock::now();
    auto thread = driver(); finish(Application::run(window), thread);
    check(WaitForSingleObject(released, 0) == WAIT_TIMEOUT, "Close never joins a blocked Shell handler");
    std::cout << "blocked_shell_window_lifetime_ms=" << std::chrono::duration<double, std::milli>(Clock::now() - start).count() << '\n';
    ImageDecodeTestAccess::shell_hook = nullptr; SetEvent(released);
    const auto deadline = Clock::now() + 10s;
    while (!quiet()) { check(Clock::now() < deadline, "Cancelled Shell worker drains"); std::this_thread::sleep_for(1ms); }
    check(!stale->pixels && !stale->done, "Cancelled Shell completion cannot publish stale pixels");
    ImageResources::clear_unused();
    const auto s = ImageResources::statistics();
    check(!s.cpu_bytes && !s.cpu_reserved && !s.gpu_bytes, "Cancelled Shell work releases all accounted pixels");
    CloseHandle(entered); CloseHandle(released); entered = released = nullptr;
}
void shell_browser(const std::filesystem::path& directory) {
    int phase{}, waits{};
    LRESULT idle{};
    std::uint64_t idle_cpu{};
    const auto start = Clock::now();
    auto operation = start;
    tick = [&](HWND hwnd) {
        check(Clock::now() - start < 45s, "Native System32 browser completes");
        check(metric(hwnd, 22) <= 48 && ImageResources::statistics().queued <= 64, "System32 has bounded visible requests");
        if (metric(hwnd, 4) || !quiet() || ++waits < 5) return;
        waits = 0;
        if (phase == 0) {
            check(metric(hwnd, 9) > 1000 && metric(hwnd, 23) > 0, "System32 loads thousands of files and visible Shell icons");
            std::cout << "system32_first_visible_ready_ms=" << std::chrono::duration<double, std::milli>(Clock::now() - start).count() << '\n';
            operation = Clock::now();
            SetWindowTextW(child(hwnd, L"EDIT", nullptr, 1), L"cmd.exe");
            std::cout << "system32_filter_input_ms=" << std::chrono::duration<double, std::milli>(Clock::now() - operation).count() << '\n';
        } else if (phase == 1) {
            std::cout << "system32_cmd_filtered_rows=" << metric(hwnd, 8) << " ready=" << metric(hwnd, 23)
                << " slots=" << metric(hwnd, 22) << '\n';
            check(metric(hwnd, 8) >= 1 && metric(hwnd, 23) >= 1, "Filtered command executable rows load Shell icons");
            const auto native = child(hwnd, L"Xui.FileList.1");
            wchar_t system[MAX_PATH]{}; GetSystemDirectoryW(system, MAX_PATH);
            const auto size = static_cast<UINT>(std::lround(24.0 * GetDpiForWindow(hwnd) / 96.0));
            const auto expected = loaded(std::filesystem::path(system) / L"cmd.exe", size);
            unsigned matches{}, opaque{};
            for (UINT y = 0; y < expected->size.height; ++y) for (UINT x = 0; x < expected->size.width; ++x) {
                const auto at = (y * expected->size.width + x) * 4;
                if (expected->pixels[at + 3] != std::byte{255}) continue;
                ++opaque;
                const auto actual = pixel(hwnd, native, 11 + (x + 0.5f) * 24 / size,
                    4 + (y + 0.5f) * 24 / size);
                const auto byte = [&](unsigned offset) { return std::to_integer<int>(expected->pixels[at + offset]); };
                if (std::abs(int(GetRValue(actual)) - byte(2)) < 8 &&
                    std::abs(int(GetGValue(actual)) - byte(1)) < 8 &&
                    std::abs(int(GetBValue(actual)) - byte(0)) < 8) ++matches;
            }
            std::cout << "cmd_rendered_opaque_pixels=" << opaque << " matches=" << matches << '\n';
            check(opaque > 20 && matches > opaque * 8 / 10, "Actual System32 cmd.exe row matches extracted Shell pixels");
            capture(hwnd, directory / L"system32-cmd.bmp");
            command(hwnd, L"Split panes");
        } else if (phase == 2) {
            check(metric(hwnd, 23) > 1, "Two browser panes retain Shell visuals");
            command(hwnd, L"Theme"); command(hwnd, L"Split panes");
            SetWindowTextW(child(hwnd, L"EDIT", nullptr, 1), L"");
        } else if (phase == 3) {
            operation = Clock::now();
            const auto native = child(hwnd, L"Xui.FileList.1");
            for (int i = 0; i < 20; ++i) SendMessageW(native, WM_KEYDOWN, VK_NEXT, 0);
            std::cout << "system32_scroll_input_ms=" << std::chrono::duration<double, std::milli>(Clock::now() - operation).count() / 20 << '\n';
        } else if (phase == 4) {
            capture(hwnd, directory / L"system32-scrolled.bmp");
            suggestion_capture::memory(GetCurrentProcess(), "system32-settled");
            idle = metric(hwnd, 0); idle_cpu = process_cpu(); operation = Clock::now();
        } else {
            if (Clock::now() - operation < 2s) return;
            check(metric(hwnd, 0) == idle, "System32 Shell icons have zero settled idle paints");
            std::cout << "system32_idle_cpu_ms=" << (process_cpu() - idle_cpu) / 10000.0 << " idle_paints=0\n";
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
        }
        ++phase;
    };
    wchar_t system[MAX_PATH]{}; check(GetSystemDirectoryW(system, MAX_PATH) != 0, "Read native System32 path");
    auto thread = driver(); finish(run_file_browser({system}), thread);
}
}
int wmain(int argc, wchar_t** argv) {
    try {
        check(argc >= 2 && argc <= 4, "Pass a project-local artifact directory and optional fixture count or Shell mode");
        const auto directory = std::filesystem::absolute(argv[1]);
        if (argc == 4) {
            check(std::wstring_view(argv[2]) == L"--shell", "Use --shell with the owned fixture executable");
            image_fixture::hr(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
            shell_fixtures(directory, argv[3]);
            shell_pixels(directory);
            CoUninitialize();
            ImageResources::clear_unused();
            shell_window(directory);
            shell_blocked(directory);
            shell_browser(directory);
            const auto s = ImageResources::statistics();
            std::cout << "shell_cpu_peak=" << s.cpu_peak << " shell_gpu_peak=" << s.gpu_peak << '\n';
            return 0;
        }
        if (argc == 3) {
            const auto count = std::stoul(argv[2]);
            check(count == 100 || count == 1000, "Use the generated 100 or 1000 image folder");
            browser(directory / std::to_wstring(count), count);
            return 0;
        }
        image_fixture::hr(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
        fixtures(directory / L"100", 100); fixtures(directory / L"1000", 1000);
        fixtures(directory / L"\u753b\u50cf", 0);
        image_fixture::png(directory / L"\u753b\u50cf" / L"standalone.png", 64, 64, 0xffa040a0);
        CoUninitialize();
        public_list(directory / L"\u753b\u50cf");
        browser(directory / L"100", 100);
        browser(directory / L"1000", 1000);
        tall_list(directory / L"100");
        close_blocked(directory / L"100");
        return 0;
    } catch (const std::exception& error) {
        ImageDecodeTestAccess::hook = nullptr;
        ImageDecodeTestAccess::shell_hook = nullptr;
        if (released) SetEvent(released);
        std::cerr << error.what() << '\n'; return 1;
    }
}
