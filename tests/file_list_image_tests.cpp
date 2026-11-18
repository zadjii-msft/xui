#include "image_fixtures.hpp"
#include "list_peer.hpp"
#include "async.hpp"
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <new>
#include <thread>

namespace probe {
thread_local bool active{};
thread_local std::size_t allocations{}, bytes{};
}
void* operator new(std::size_t size) {
    if (auto* memory = std::malloc(size ? size : 1)) {
        if (probe::active) { ++probe::allocations; probe::bytes += size; }
        return memory;
    }
    throw std::bad_alloc{};
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* value) noexcept { std::free(value); }
void operator delete[](void* value) noexcept { std::free(value); }
void operator delete(void* value, std::size_t) noexcept { std::free(value); }
void operator delete[](void* value, std::size_t) noexcept { std::free(value); }

namespace {
using namespace xui;
using Clock = std::chrono::steady_clock;
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<class F> void await(F action) {
    const auto deadline = Clock::now() + std::chrono::seconds(10);
    while (!action()) {
        require(Clock::now() < deadline, "Legacy image completion timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
std::uint64_t ticks(FILETIME value) { return (std::uint64_t(value.dwHighDateTime) << 32) | value.dwLowDateTime; }
void run(const std::filesystem::path& directory, bool measured) {
    auto list = std::make_shared<FileList>();
    auto data = std::make_shared<std::vector<FileItem>>();
    for (unsigned i = 0; i != 10000; ++i)
        data->push_back({i + 1, L"Row " + std::to_wstring(i), (directory / L"ready.png").wstring(), false});
    list->set_items(data);
    list->set_viewport_height(1024);
    list->set_thumbnails(true);
    ListPeer peer(list, [] { throw std::runtime_error("Unexpected peer failure"); }, [](HWND) {});
    auto wake = std::make_shared<TaskWake>();
    std::vector<std::uint64_t> retained;
    retained.reserve(24);
    auto sync = [&](std::size_t budget = 48) {
        retained.clear();
        return peer.sync_thumbnails(true, {0, 0, 500, 1024}, wake, retained, budget);
    };
    auto ready = [&] {
        sync();
        return peer.thumbnail_count() == 24 && peer.thumbnail_ready() == 24;
    };
    await(ready);
    require(retained.size() == 24, "Legacy retained pixel pins match admitted rows");
    const auto retained_id = retained.front();
    const auto resources = ImageResources::statistics();
    if (measured) {
        constexpr unsigned iterations = 1048576;
        FILETIME creation{}, exit{}, k0{}, u0{}, k1{}, u1{};
        require(GetThreadTimes(GetCurrentThread(), &creation, &exit, &k0, &u0) != FALSE, "Read owner CPU");
        probe::allocations = probe::bytes = 0;
        probe::active = true;
        const auto start = Clock::now();
        for (unsigned i = 0; i != iterations; ++i)
            require(!sync() && retained.size() == 24 && retained.front() == retained_id, "Settled legacy identities stay unchanged");
        const auto wall = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        probe::active = false;
        require(GetThreadTimes(GetCurrentThread(), &creation, &exit, &k1, &u1) != FALSE, "Read owner CPU after");
        std::cout << "{\"phase\":\"legacy-image-reconciliation\",\"iterations\":" << iterations << ",\"wall_ms\":" << wall
            << ",\"cpu_ms\":" << (ticks(k1) + ticks(u1) - ticks(k0) - ticks(u0)) / 10000.0
            << ",\"cpp_allocations\":" << probe::allocations << ",\"cpp_requested_bytes\":" << probe::bytes
            << ",\"retained_slots\":" << peer.thumbnail_count() << ",\"retained_pins\":" << retained.size()
            << ",\"kernel_before100ns\":" << ticks(k0) << ",\"kernel_after100ns\":" << ticks(k1)
            << ",\"user_before100ns\":" << ticks(u0) << ",\"user_after100ns\":" << ticks(u1) << "}\n";
    } else {
        probe::allocations = 0; probe::active = true;
        for (unsigned i = 0; i != 64; ++i) require(!sync(), "Settled legacy sync does not repaint");
        probe::active = false;
        require(!probe::allocations, "Settled legacy reconciliation must allocate no temporary storage");
    }
    require(ImageResources::statistics().decoded == resources.decoded, "Settled reconciliation does not decode again");
    sync(3);
    require(peer.thumbnail_count() == 3 && retained.front() == retained_id, "Window admission budget remains enforced");
    list->set_filter(L"Row 9999");
    sync();
    await([&] { sync(); return peer.thumbnail_count() == 1 && peer.thumbnail_ready() == 1; });
    list->set_filter(L"");
    list->scroll_to(200000);
    await(ready);
    list->set_thumbnails(false);
    sync();
    require(peer.thumbnail_count() == 0, "Disabled thumbnails cancel and release slots");
    unsigned errors{};
    list->on_thumbnail_error([&](ItemId id, const std::wstring& message) {
        require(id == 1 && !message.empty(), "Legacy errors retain item identity and explicit diagnostics");
        ++errors;
    });
    list->set_items(std::make_shared<const std::vector<FileItem>>(std::vector<FileItem>{
        {1, L"Missing", (directory / L"missing.png").wstring(), false}}));
    list->set_thumbnails(true);
    await([&] { sync(); return errors == 1; });
    for (unsigned i = 0; i != 16; ++i) sync();
    require(errors == 1 && peer.thumbnail_ready() == 0, "Failure reports once and keeps fallback artwork");
    // Error callbacks can replace the source. Remaining errors must not target old rows.
    list->on_thumbnail_error([&](ItemId, const std::wstring&) { ++errors; list->set_items(data); });
    list->reload_thumbnails();
    await([&] { sync(); return errors == 2; });
    await(ready);
    peer.detach_thumbnails();
    require(peer.thumbnail_count() == 0, "Peer retirement releases all image slots");
}
}
int main(int argc, char** argv) {
    const bool measured = argc == 2 && std::string_view(argv[1]) == "--measure";
    const auto directory = std::filesystem::temp_directory_path() /
        (L"xui-legacy-images-" + std::to_wstring(GetCurrentProcessId()));
    try {
        require(argc == 1 || measured, "Usage: file_list_image_tests [--measure]");
        require(std::filesystem::create_directory(directory), "Create unique test image directory");
        image_fixture::hr(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
        image_fixture::png(directory / L"ready.png", 32, 32, 0xff2040c0);
        run(directory, measured);
        await([] { const auto stats = ImageResources::statistics(); return !stats.active && !stats.queued; });
        clear_image_cache();
        require(ImageResources::statistics().cpu_bytes == 0, "Legacy image resources retire after closure");
        std::filesystem::remove(directory / L"ready.png");
        std::filesystem::remove(directory);
        CoUninitialize();
        std::cout << "Legacy image admission, replacement, failures and retirement passed.\n";
    } catch (const std::exception& error) {
        probe::active = false;
        std::cerr << error.what() << '\n';
        return 1;
    }
}
