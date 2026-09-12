#include "image_fixtures.hpp"
#include "../src/images.hpp"
#include "../src/async.hpp"
#include "../src/drawing.hpp"
#include <iostream>
#include <chrono>
#include <thread>
#include <array>

namespace xui {
struct DrawingTestAccess {
    static void lose() { Drawing::end_result_override_ = D2DERR_RECREATE_TARGET; }
};
}
namespace {
using namespace xui;
using namespace std::chrono_literals;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
std::filesystem::path directory;
std::wstring path(std::size_t index) { return (directory / (L"image-" + std::to_wstring(index) + L".png")).wstring(); }
template<class F> void wait(F predicate) {
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    while (!predicate()) {
        check(std::chrono::steady_clock::now() < deadline, "Image wait deadline");
        std::this_thread::sleep_for(1ms);
    }
}
std::shared_ptr<ImageRequest> load(const std::wstring& file, ImageSize size = {}) {
    auto request = request_image(file, size, std::make_shared<TaskWake>());
    wait([&] { std::lock_guard lock(request->mutex); return request->done; });
    return request;
}
void empty() {
    wait([] { auto s = ImageResources::statistics(); return !s.active && !s.queued; });
    clear_image_cache();
    auto s = ImageResources::statistics();
    check(!s.cpu_bytes && !s.cpu_reserved && !s.gpu_bytes && !s.gpu_reserved, "All image ownership must return to zero");
}
void bounds() {
    auto s = ImageResources::statistics();
    check(s.cpu_bytes + s.cpu_reserved <= ImageLimits::cpu_bytes && s.cpu_peak <= ImageLimits::cpu_bytes, "CPU budget and peak");
    check(s.gpu_bytes + s.gpu_reserved <= ImageLimits::gpu_bytes && s.gpu_peak <= ImageLimits::gpu_bytes, "GPU budget and peak");
    check(s.queued <= ImageLimits::queue && s.active <= 1 && s.cache_entries <= ImageLimits::cache_entries, "Bounded metadata and worker count");
}
void decode_tests() {
    Image image(L"Preview");
    check(!image.focusable(), "An image does not add a false keyboard action");
    for (const auto size : {ImageSize{0, 4}, ImageSize{1025, 1}, ImageSize{0xffffffff, 0xffffffff}}) {
        bool rejected{};
        try { image.set_source(path(0), size); } catch (const std::invalid_argument&) { rejected = true; }
        check(rejected, "Invalid output dimensions are explicit errors");
    }
    for (const auto& invalid : {std::wstring(32768, L'x'), std::wstring(L"a\0b", 3)}) {
        bool rejected{};
        try { image.set_source(invalid); } catch (const std::invalid_argument&) { rejected = true; }
        check(rejected, "Invalid path strings fail before file I/O");
    }
    {
        image_fixture::png(directory / L"alpha.png", 32, 16, 0x80804020);
        auto alpha = load((directory / L"alpha.png").wstring(), {128, 128});
        check(alpha->pixels && alpha->pixels->size == ImageSize{32, 16}, "A small image preserves aspect ratio without decode upscaling");
        const auto& pixel = alpha->pixels->pixels;
        check(std::to_integer<unsigned>(pixel[0]) == 0x10 && std::to_integer<unsigned>(pixel[1]) == 0x20 &&
            std::to_integer<unsigned>(pixel[2]) == 0x40 && std::to_integer<unsigned>(pixel[3]) == 0x80, "WIC converts straight alpha to premultiplied BGRA");
    }
    {
        auto a = load(path(0));
        check(a->pixels && a->pixels->size == ImageSize{144, 144}, "Aspect-fit requested decode size");
        check(a->pixels->pixels.size() == 144 * 144 * 4, "Exact owned pixel bytes");
        auto b = load((directory / L"." / L"image-0.png").wstring());
        check(a->pixels == b->pixels, "Canonical aliases share a decoded resource");
        auto c = load(path(0), {64, 64});
        check(c->pixels && c->pixels != a->pixels && c->pixels->size == ImageSize{64, 64}, "Display size forms part of the sharing key");
        const auto version = a->pixels->id;
        image_fixture::png(path(0), 1024, 1024, 0xffd04020);
        auto d = load(path(0));
        check(d->pixels && d->pixels->id != version, "File version change invalidates the cache");
        check(std::to_integer<unsigned>(d->pixels->pixels[2]) == 0xd0, "Changed pixels reach the resource");
    }
    for (const auto file : {L"missing.png", L"corrupt.png", L"huge.bmp"}) {
        auto request = load((directory / file).wstring());
        check(!request->pixels && !request->error.empty(), "Missing, corrupt and huge-header files return errors");
    }
    for (const auto width : {0L, 1024L, 16385L}) {
        BITMAPFILEHEADER file{0x4d42, 58, 0, 0, 54};
        BITMAPINFOHEADER info{sizeof(info), width, 1024, 1, 32, BI_RGB};
        const auto truncated = directory / L"truncated.bmp";
        {
            std::ofstream stream(truncated, std::ios::binary);
            stream.write(reinterpret_cast<const char*>(&file), sizeof(file));
            stream.write(reinterpret_cast<const char*>(&info), sizeof(info));
            stream << "xxxx";
        }
        auto request = load(truncated.wstring());
        check(!request->pixels && !request->error.empty() && !ImageResources::statistics().cpu_reserved,
            "Zero, truncated and oversize dimensions reject with no stranded reservation");
    }
    {
        const auto oversized = directory / L"oversized.png";
        { std::ofstream stream(oversized, std::ios::binary); stream.seekp(ImageLimits::file_bytes); stream.put('x'); }
        auto request = load(oversized.wstring());
        check(!request->pixels && request->error.find(L"32 MiB") != std::wstring::npos, "Oversize encoded files fail before codec access");
        std::filesystem::remove(oversized);
    }
    {
        auto request = load(directory.wstring());
        check(!request->pixels && !request->error.empty(), "A directory is not an image");
    }
    empty();
    {
        auto a = load(path(0), {1024, 1024});
        auto b = load(path(1), {1024, 1024});
        check(a->pixels && b->pixels, "Two large images fill the CPU budget");
        check(ImageResources::statistics().cpu_bytes == ImageLimits::cpu_bytes, "Exact 8 MiB CPU ownership");
        auto c = load(path(2), {1024, 1024});
        check(!c->pixels && !c->error.empty(), "Pinned CPU budget exhaustion is an error, not a fallback");
        a.reset();
        c = load(path(2), {1024, 1024});
        check(c->pixels && ImageResources::statistics().cpu_bytes == ImageLimits::cpu_bytes, "LRU eviction makes space after unload");
        bounds();
    }
    empty();
    for (int cycle = 0; cycle < 4; ++cycle) {
        for (std::size_t i = 0; i < 160; ++i) {
            auto request = load(path(i), {192, 128});
            check(request->pixels != nullptr, "Sustained navigation decodes each requested image");
            bounds();
        }
        const auto s = ImageResources::statistics();
        std::cout << "navigation " << cycle << ": cpu=" << s.cpu_bytes << " gpu=" << s.gpu_bytes
            << " cache=" << s.cache_entries << " evicted=" << s.evicted << '\n';
    }
    check(ImageResources::statistics().evicted > 400, "Sustained navigation must exercise cache eviction");
    empty();
}
HANDLE entered{}, release_gate{};
ImageDecodeStage gated_stage;
void hook(ImageDecodeStage stage) {
    if (stage != gated_stage) return;
    SetEvent(entered);
    WaitForSingleObject(release_gate, 10000);
}
struct Gate {
    Gate(ImageDecodeStage stage) {
        entered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        release_gate = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        gated_stage = stage; ImageDecodeTestAccess::hook = hook;
    }
    void await() { check(WaitForSingleObject(entered, 10000) == WAIT_OBJECT_0, "Decode reaches gated boundary"); }
    void release() { ImageDecodeTestAccess::hook = nullptr; SetEvent(release_gate); }
    ~Gate() {
        release();
        wait([] { return !ImageResources::statistics().active; });
        CloseHandle(entered); CloseHandle(release_gate);
    }
};
void cancellation_tests() {
    {
        Gate gate(ImageDecodeStage::before_decode);
        const auto decoded = ImageResources::statistics().decoded;
        auto a = request_image(path(0), {64, 64}, {});
        gate.await();
        auto b = request_image(path(0), {64, 64}, {});
        gate.release();
        wait([&] { std::lock_guard lock(b->mutex); return b->done; });
        check(a->pixels && a->pixels == b->pixels && ImageResources::statistics().decoded == decoded + 1,
            "Concurrent identical requests share one decode and one allocation");
        a.reset(); b.reset();
    }
    empty();
    for (const auto stage : {ImageDecodeStage::before_decode, ImageDecodeStage::reserved, ImageDecodeStage::before_delivery}) {
        Gate gate(stage);
        Image image(L"Recycled");
        ImagePeer peer(image);
        image.set_source(path(1), {1024, 1024});
        peer.sync(true, std::make_shared<TaskWake>());
        gate.await();
        if (stage == ImageDecodeStage::reserved) {
            auto s = ImageResources::statistics();
            check(s.cpu_reserved == 4 * 1024 * 1024, "In-flight allocation has an exact reservation");
        }
        image.set_source(path(2), {64, 64});
        peer.sync(true, std::make_shared<TaskWake>());
        gate.release();
        wait([&] { peer.deliver(); return image.status() == ImageStatus::ready; });
        check(peer.pixels->size == ImageSize{64, 64}, "A stale recycled completion cannot update its replacement");
        peer.detach();
        empty();
    }
    {
        Gate gate(ImageDecodeStage::before_decode);
        auto active = request_image(path(0), {}, std::make_shared<TaskWake>());
        gate.await();
        std::vector<std::shared_ptr<ImageRequest>> queued;
        for (std::size_t i = 0; i < ImageLimits::queue; ++i) queued.push_back(request_image(path(1), {}, {}));
        auto rejected = request_image(path(2), {}, {});
        check(rejected->done && !rejected->error.empty(), "A full queue rejects instead of growing");
        for (auto& request : queued) request->cancel();
        active->cancel();
        auto replacement = request_image(path(3), {}, {});
        check(ImageResources::statistics().queued == 1, "Cancelled queued work is removed before accepting a replacement");
        bounds();
        gate.release();
        wait([&] { std::lock_guard lock(replacement->mutex); return replacement->done; });
        replacement->cancel();
        queued.clear(); active.reset(); replacement.reset(); rejected.reset();
    }
    empty();
}
void gpu_tests() {
    auto window = CreateWindowExW(0, L"STATIC", L"Image render regression", WS_OVERLAPPEDWINDOW,
        50, 50, 360, 300, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    check(window != nullptr, "Create render regression window");
    ShowWindow(window, SW_SHOW);
    Drawing drawing;
    drawing.initialize();
    auto a = load(path(0), {1024, 1024});
    auto b = load(path(1), {1024, 1024});
    check(a->pixels && b->pixels, "GPU fixtures decoded");
    check(drawing.begin(window, 96, D2D1::ColorF(0)), "Begin image frame");
    check(drawing.image(a->pixels, {0, 0, 128, 128}), "Upload first bitmap");
    const auto uploads = ImageResources::statistics().uploaded;
    check(drawing.image(a->pixels, {128, 0, 128, 128}), "Draw a shared bitmap twice");
    check(ImageResources::statistics().uploaded == uploads, "Deduplication includes the GPU upload");
    check(drawing.image(b->pixels, {0, 128, 128, 128}), "Upload second bitmap");
    check(ImageResources::statistics().gpu_bytes == ImageLimits::gpu_bytes, "Exact 8 MiB bitmap ownership");
    check(!reserve_bitmap(4), "An extra bitmap reservation fails at the GPU limit");
    a.reset();
    ImageResources::clear_unused();
    auto c = load(path(2), {1024, 1024});
    check(c->pixels && !drawing.image(c->pixels, {128, 128, 128, 128}), "A real upload rejects when unrelated retained bitmaps fill the GPU budget");
    const std::array keep{b->pixels->id, c->pixels->id};
    drawing.keep_images(keep);
    check(drawing.image(c->pixels, {128, 128, 128, 128}), "Bitmap eviction permits a later explicit upload");
    check(drawing.end(), "End image frame");
    DrawingTestAccess::lose();
    drawing.begin(window, 96, D2D1::ColorF(0));
    check(!drawing.end(), "Injected device loss requests repaint");
    check(ImageResources::statistics().gpu_bytes == 0 && Drawing::live_targets() == 0, "Device loss releases all target-owned bitmaps");
    drawing.begin(window, 96, D2D1::ColorF(0));
    check(drawing.image(b->pixels, {0, 0, 128, 128}), "Device recreation uploads retained pixels");
    drawing.end();
    drawing.keep_images({});
    check(ImageResources::statistics().gpu_bytes == 0, "Unload drops the bitmap cache");
    bounds();
    drawing.release();
    DestroyWindow(window);
    b.reset(); c.reset();
    empty();
}
}
int wmain(int argc, wchar_t** argv) {
    try {
        check(argc > 1, "Pass a project-local fixture directory");
        directory = std::filesystem::absolute(argv[1]);
        image_fixture::hr(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
        image_fixture::create(directory);
        if (argc > 2 && std::wstring(argv[2]) == L"--fixtures") { CoUninitialize(); return 0; }
        decode_tests(); cancellation_tests(); gpu_tests();
        const auto s = ImageResources::statistics();
        std::cout << "image resources: decoded=" << s.decoded << " hits=" << s.cache_hits << " evicted=" << s.evicted
            << " rejected=" << s.rejected << " cancelled=" << s.cancelled << " cpu_peak=" << s.cpu_peak
            << " gpu_peak=" << s.gpu_peak << " uploaded=" << s.uploaded << " decode_ms=" << s.decode_ms
            << " decode_max_ms=" << s.decode_max_ms << " upload_ms=" << s.upload_ms
            << " upload_max_ms=" << s.upload_max_ms << '\n';
        CoUninitialize();
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
