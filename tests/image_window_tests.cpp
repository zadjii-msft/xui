#include "image_fixtures.hpp"
#include "../demo/thumbnail_grid.hpp"
#include "../src/drawing.hpp"
#include "../src/images.hpp"
#include <chrono>
#include <iostream>
#include <thread>

namespace xui {
struct DrawingTestAccess {
    static void lose() { Drawing::end_result_override_ = D2DERR_RECREATE_TARGET; }
    static void fail_image_upload() { Drawing::image_result_override_ = E_OUTOFMEMORY; }
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
BOOL CALLBACK find_window(HWND hwnd, LPARAM result) {
    DWORD process{};
    GetWindowThreadProcessId(hwnd, &process);
    wchar_t title[100]{};
    GetWindowTextW(hwnd, title, 100);
    if (process == GetCurrentProcessId() && std::wstring_view(title) == L"XUI image workload" && IsWindowVisible(hwnd)) {
        *reinterpret_cast<HWND*>(result) = hwnd;
        return FALSE;
    }
    return TRUE;
}
std::jthread start_driver() {
    return std::jthread([] {
        const auto deadline = Clock::now() + 10s;
        HWND hwnd{};
        while (!hwnd && Clock::now() < deadline) {
            EnumWindows(find_window, reinterpret_cast<LPARAM>(&hwnd));
            std::this_thread::sleep_for(10ms);
        }
        if (hwnd) {
            SetWindowPos(hwnd, HWND_TOPMOST, 40, 40, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
            SetTimer(hwnd, 81, 20, timer);
        }
    });
}
void screenshot(HWND hwnd, const std::filesystem::path& path) {
    RECT rect{}; GetClientRect(hwnd, &rect);
    const auto dc = GetDC(hwnd);
    auto memory = CreateCompatibleDC(dc);
    auto bitmap = CreateCompatibleBitmap(dc, rect.right, rect.bottom);
    auto old = SelectObject(memory, bitmap);
    check(BitBlt(memory, 0, 0, rect.right, rect.bottom, dc, 0, 0, SRCCOPY) != 0, "Capture image workload");
    SelectObject(memory, old);
    BITMAPINFO info{};
    info.bmiHeader = {sizeof(BITMAPINFOHEADER), rect.right, -rect.bottom, 1, 32, BI_RGB};
    std::vector<char> pixels(std::size_t(rect.right) * rect.bottom * 4);
    check(GetDIBits(memory, bitmap, 0, rect.bottom, pixels.data(), &info, DIB_RGB_COLORS) != 0, "Read image screenshot");
    BITMAPFILEHEADER file{0x4d42, static_cast<DWORD>(54 + pixels.size()), 0, 0, 54};
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(&file), sizeof(file));
    output.write(reinterpret_cast<const char*>(&info.bmiHeader), sizeof(info.bmiHeader));
    output.write(pixels.data(), pixels.size());
    DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(hwnd, dc);
}
void workload(const std::filesystem::path& directory) {
    Window window({L"XUI image workload", {860, 660}});
    auto root = std::make_shared<Stack>(Axis::vertical);
    root->set_padding({16, 16, 16, 16}); root->set_spacing(8);
    auto header = std::make_shared<Label>(L"Bounded image workload");
    root->add(header);
    auto grid = std::make_shared<sample::ThumbnailGrid>();
    auto scroll = std::make_shared<ScrollView>(grid, L"Thumbnails");
    scroll->set_maximum_size({10000, 960});
    grid->on_offset([weak = std::weak_ptr(scroll)] { auto value = weak.lock(); return value ? value->offset() : 0; });
    root->add(scroll, 1);
    auto footer = std::make_shared<Label>(L"Original generated image fixtures");
    root->add(footer);
    window.set_content(root);
    std::atomic<std::size_t> folder{};
    auto task = window.create_view_task([&](const CancelCheck& cancel) {
        const auto number = folder.load();
        auto items = std::make_shared<std::vector<FileItem>>();
        for (std::size_t i = 0; i < 20000; ++i) {
            if (cancel()) return SourceResult{};
            items->push_back({i + 1, L"Image " + std::to_wstring(i),
                (directory / (L"image-" + std::to_wstring((i + number * 53) % 160) + L".png")).wstring(), false});
        }
        return SourceResult{FileSnapshot::build(items, cancel), {}};
    }, [&](ViewResult result) {
        check(result.error.empty() && result.view, "Asynchronous image source succeeds");
        grid->set_view(std::move(result.view)); scroll->set_offset(0);
    });
    task->request(L"", true);
    int phase{}, page{}, switches{}, idle_ticks{};
    std::uint64_t idle_paints{}, before_recreate{};
    Clock::time_point start = Clock::now(), last = start, operation = start;
    double scroll_ms{}, scroll_max{}, ready_ms{};
    std::size_t interactions{}, ready_pages{};
    const auto statistics_before = ImageResources::statistics();
    tick = [&](HWND hwnd) {
        check(Clock::now() - start < 100s, "Image workload completes before its deadline");
        auto s = ImageResources::statistics();
        check(s.cpu_bytes + s.cpu_reserved <= ImageLimits::cpu_bytes && s.gpu_bytes + s.gpu_reserved <= ImageLimits::gpu_bytes,
            "Actual window retains bounded pixel and bitmap bytes");
        check(s.queued <= ImageLimits::queue && s.active <= 1 && Drawing::live_targets() == 1, "One target and one bounded worker");
        check(grid->child_count() == sample::ThumbnailGrid::tile_count, "20,000 items retain only 40 tile controls");
        if (phase < 5 && task->generation() == task->applied_generation())
            check(grid->count() == 20000, "The complete immutable source remains available for scrolling");
        bool loading = task->busy() || task->generation() != task->applied_generation();
        std::size_t ready{};
        for (const auto& image : grid->images) {
            check(image->status() != ImageStatus::error, "Visible thumbnails must not fail to decode or upload");
            loading = loading || image->status() == ImageStatus::loading;
            ready += image->status() == ImageStatus::ready;
        }
        if (phase == 0) {
            if (loading || !ready || s.active || s.queued) return;
            if (Clock::now() - last < 80ms) return;
            check(ready <= 20, "Only visible tiles decode, not the entire collection");
            auto image = grid->images[0];
            const auto rect = image->bounds();
            const float dpi = GetDpiForWindow(hwnd) / 96.0f;
            auto native_scroll = FindWindowExW(hwnd, nullptr, L"Xui.Control.1", L"Thumbnails");
            const auto native_content = FindWindowExW(native_scroll, nullptr, L"Xui.ScrollContent.1", nullptr);
            auto native_image = FindWindowExW(native_content, nullptr, L"Xui.Control.1", L"Image");
            check(native_image != nullptr, "Find the first recycled native image peer");
            RECT actual{};
            GetWindowRect(native_image, &actual);
            MapWindowPoints(nullptr, hwnd, reinterpret_cast<POINT*>(&actual), 2);
            check(std::abs(actual.left - rect.x * dpi) < 1 && std::abs(actual.top - rect.y * dpi) < 1,
                "Batched image geometry remains relative to the correct native viewport parent");
            auto dc = GetDC(hwnd);
            const auto color = GetPixel(dc, static_cast<int>((rect.x + rect.width / 2) * dpi),
                static_cast<int>((rect.y + rect.height / 2) * dpi));
            ReleaseDC(hwnd, dc);
            check(color == RGB(0x20, 0x40, 0xc0), "Actual WIC pixels appear in the shared D2D window");
            dc = GetDC(hwnd);
            const auto lower = GetPixel(dc, static_cast<int>((rect.x + rect.width / 2) * dpi),
                static_cast<int>((rect.y + rect.height - 5) * dpi));
            ReleaseDC(hwnd, dc);
            check(lower == RGB(0x20, 0x40, 0xc0), "The complete first image occupies its arranged rectangle");
            screenshot(hwnd, directory.parent_path() / L"images-workload.bmp");
            phase = 1;
            last = Clock::now();
        } else if (phase == 1) {
            if (loading || s.active || s.queued) return;
            if (Clock::now() - last < 40ms) return;
            if (interactions) { ready_ms += std::chrono::duration<double, std::milli>(Clock::now() - operation).count(); ++ready_pages; }
            auto native = FindWindowExW(hwnd, nullptr, L"Xui.Control.1", L"Thumbnails");
            check(native != nullptr, "Public ScrollView has a native input peer");
            operation = Clock::now();
            if (page < 30) {
                const auto old = scroll->offset();
                SendMessageW(native, WM_KEYDOWN, VK_NEXT, 0);
                check(scroll->offset() > old, "Page Down scrolls the virtual thumbnail collection");
                SendMessageW(native, WM_MOUSEWHEEL, static_cast<WPARAM>(static_cast<unsigned>(-120) << 16), 0);
                const auto duration = std::chrono::duration<double, std::milli>(Clock::now() - operation).count();
                scroll_ms += duration; scroll_max = std::max(scroll_max, duration); ++interactions;
                ++page;
            } else if (switches < 3) {
                ++switches; folder = switches;
                grid->set_view({});
                task->request(L"", true);
                std::cout << "folder switch " << switches << ": cpu=" << s.cpu_bytes << " gpu=" << s.gpu_bytes
                    << " cache=" << s.cache_entries << " evicted=" << s.evicted << '\n';
                page = 0;
            } else {
                check(s.evicted > 128, "The actual image workload must reach cache eviction");
                before_recreate = s.uploaded;
                DrawingTestAccess::lose();
                InvalidateRect(hwnd, nullptr, FALSE);
                phase = 2;
            }
            last = Clock::now();
        } else if (phase == 2) {
            if (loading || Clock::now() - last < 150ms) return;
            check(s.uploaded > before_recreate, "A live image window recreates its bitmaps after device loss");
            scroll->set_offset(scroll->offset() + 80);
            phase = 3; last = Clock::now();
        } else if (phase == 3) {
            if (loading || s.active || s.queued || Clock::now() - last < 150ms) return;
            const auto dpi = GetDpiForWindow(hwnd) / 96.0f;
            auto dc = GetDC(hwnd);
            const auto color = GetPixel(dc, static_cast<int>(24 * dpi), static_cast<int>(8 * dpi));
            ReleaseDC(hwnd, dc);
            check(color == RGB(0x15, 0x18, 0x1b), "Scrolled image pixels stay clipped out of the header margin");
            idle_paints = SendMessageW(hwnd, WM_APP + 60, 0, 0);
            phase = 4;
        } else if (phase == 4) {
            if (++idle_ticks < 50) return;
            check(static_cast<std::uint64_t>(SendMessageW(hwnd, WM_APP + 60, 0, 0)) == idle_paints, "Idle images do not request paints");
            std::cout << "window latency: input_mean_ms=" << scroll_ms / interactions << " input_max_ms=" << scroll_max
                << " page_ready_mean_ms=" << ready_ms / ready_pages << " interactions=" << interactions
                << " cpu=" << s.cpu_bytes << " gpu=" << s.gpu_bytes << " cpu_peak=" << s.cpu_peak << " gpu_peak=" << s.gpu_peak
                << " decoded=" << s.decoded - statistics_before.decoded << " hits=" << s.cache_hits - statistics_before.cache_hits
                << " decode_ms=" << s.decode_ms - statistics_before.decode_ms << " decode_max_ms=" << s.decode_max_ms
                << " upload_ms=" << s.upload_ms - statistics_before.upload_ms << " upload_max_ms=" << s.upload_max_ms << '\n';
            grid->set_view({});
            phase = 5;
        } else if (phase == 5) {
            if (s.active || s.queued) return;
            ImageResources::clear_unused();
            s = ImageResources::statistics();
            check(!s.cpu_bytes && !s.cpu_reserved && !s.gpu_bytes, "Explicit unload and cache eviction return all owned bytes");
            KillTimer(hwnd, 81);
            window.close();
        }
    };
    auto driver = start_driver();
    const auto result = Application::run(window);
    driver.join();
    tick = {};
    if (failure) std::rethrow_exception(failure);
    check(result == 0, "Public image workload exits successfully");
    const auto s = ImageResources::statistics();
    check(!s.cpu_bytes && !s.gpu_bytes && !s.cpu_reserved && !s.gpu_reserved, "Window close releases all image resources");
    std::cout << "window close: cpu=0 gpu=0 reserved=0\n";
}
HANDLE gate_entered{}, gate_release{};
void blocked_decode(ImageDecodeStage stage) {
    if (stage == ImageDecodeStage::reserved) {
        SetEvent(gate_entered);
        WaitForSingleObject(gate_release, 10000);
    }
}
void close_inflight(const std::filesystem::path& directory) {
    Clock::time_point close_start;
    gate_entered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    gate_release = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ImageDecodeTestAccess::hook = blocked_decode;
    Window window({L"XUI image workload", {400, 300}});
    auto root = std::make_shared<Stack>(Axis::vertical);
    auto image = std::make_shared<Image>(L"Close in flight");
    image->set_source((directory / L"image-0.png").wstring(), {1024, 1024});
    root->add(image, 1); window.set_content(root);
    tick = [&](HWND hwnd) {
        if (WaitForSingleObject(gate_entered, 0) != WAIT_OBJECT_0) return;
        check(ImageResources::statistics().cpu_reserved == 4 * 1024 * 1024, "Close occurs during an in-flight reservation");
        KillTimer(hwnd, 81); close_start = Clock::now(); window.close();
    };
    auto driver = start_driver();
    check(Application::run(window) == 0, "Close does not wait for a blocked decoder");
    driver.join(); tick = {};
    check(WaitForSingleObject(gate_release, 0) == WAIT_TIMEOUT, "The worker is still blocked after Application::run returns");
    std::cout << "close_inflight_ms=" << std::chrono::duration<double, std::milli>(Clock::now() - close_start).count()
        << " worker_still_blocked=1\n";
    ImageDecodeTestAccess::hook = nullptr; SetEvent(gate_release);
    const auto deadline = Clock::now() + 10s;
    while (ImageResources::statistics().active) {
        check(Clock::now() < deadline, "Cancelled worker releases its reservation");
        std::this_thread::sleep_for(1ms);
    }
    const auto s = ImageResources::statistics();
    check(!s.cpu_bytes && !s.cpu_reserved && !s.gpu_bytes, "Close-in-flight returns owned bytes after the codec boundary");
    check(image->status() == ImageStatus::empty, "Retained image stays detached after stale completion");
    CloseHandle(gate_entered); CloseHandle(gate_release);
    if (failure) std::rethrow_exception(failure);
}
}
#include "image_memory_window.inc"
int wmain(int argc, wchar_t** argv) {
    try {
        if (argc == 2 && std::wstring_view(argv[1]) == L"--memory-only") {
            image_fixture::hr(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
            image_memory_window::run(); CoUninitialize(); return 0;
        }
        check(argc > 1, "Pass a project-local fixture directory");
        const auto directory = std::filesystem::absolute(argv[1]);
        image_fixture::hr(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
        image_fixture::create(directory);
        CoUninitialize();
        workload(directory); close_inflight(directory);
        return 0;
    } catch (const std::exception& error) {
        ImageDecodeTestAccess::hook = nullptr;
        if (gate_release) SetEvent(gate_release);
        std::cerr << error.what() << '\n'; return 1;
    }
}
