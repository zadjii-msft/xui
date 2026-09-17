#include "image_fixtures.hpp"
#include "../src/images.hpp"
#include "../src/async.hpp"
#include "../src/drawing.hpp"
#include "xui/navigation.hpp"
#include <iostream>
#include <chrono>
#include <thread>
#include <array>

namespace xui {
struct DrawingTestAccess {
    static void lose() { Drawing::end_result_override_ = D2DERR_RECREATE_TARGET; }
};
struct RowImagesTestAccess {
    static std::shared_ptr<ImageRequest> request(const RowImages& images, ItemKey key) {
        for (const auto& slot : images.slots_) if (slot->key == key) return slot->request;
        return {};
    }
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
void row_image_tests() {
    struct Source final : ItemsSource {
        size_t size() const override { return 100; }
        ItemKey key(size_t index) const override { return {index + 1, 1}; }
        std::optional<size_t> find(ItemKey key) const override { return key.id && key.id <= 100 ? std::optional<size_t>{key.id - 1} : std::nullopt; }
        ItemContent item(size_t) const override { return {}; }
    };
    auto source = std::make_shared<Source>();
    check(source->visual(0).image_path.empty() && source->visual(0).icon == ButtonIcon::none, "Old sources have no visuals");
    auto wake = std::make_shared<TaskWake>();
    RowImages first, second, third;
    std::vector<RowVisual> rows;
    std::vector<uint64_t> retained;
    size_t remaining = 48;
    for (const auto icon : {ButtonIcon::save, ButtonIcon::save_as, ButtonIcon::undo, ButtonIcon::redo,
        ButtonIcon::chevron_up, ButtonIcon::chevron_down}) {
        first.sync(source, {{source->key(0), {icon, {}}}}, 96, wake, retained, remaining);
        check(first.visual(source->key(0)).icon == icon && remaining == 48,
            "Document row icons retain their value without image requests");
    }
    for (const auto invalid : {static_cast<ButtonIcon>(-1), static_cast<ButtonIcon>(28)}) {
        bool rejected{};
        try { first.sync(source, {{source->key(0), {invalid, {}}}}, 96, wake, retained, remaining); }
        catch (const std::invalid_argument&) { rejected = true; }
        check(rejected, "Invalid row icons fail before I/O");
    }
    for (size_t i = 0; i < 100; ++i) rows.push_back({source->key(i), {ButtonIcon::none, path(i)}});
    first.sync(source, rows, 96, wake, retained, remaining);
    second.sync(source, rows, 96, wake, retained, remaining);
    third.sync(source, rows, 96, wake, retained, remaining);
    check(first.count() == 24 && second.count() == 24 && third.count() == 0 && !remaining, "24 per control and 48 shared image slots");
    wait([] { const auto s = ImageResources::statistics(); return !s.active && !s.queued; });
    retained.clear(); remaining = 48;
    first.sync(source, rows, 96, wake, retained, remaining);
    check(first.pixels({1, 1}) && retained.size() == 24, "Visible row completion retains actual pixels");
    const auto id = first.pixels({1, 1})->id;
    remaining = 48; retained.clear(); first.sync(source, rows, 192, wake, retained, remaining);
    check(!first.pixels({1, 1}), "DPI change releases old pixel slots before completion");
    wait([] { const auto s = ImageResources::statistics(); return !s.active && !s.queued; });
    remaining = 48; first.sync(source, rows, 192, wake, retained, remaining);
    check(first.pixels({1, 1}) && first.pixels({1, 1})->id != id, "DPI change uses newly sized pixels");
    rows = {{{1, 2}, {ButtonIcon::none, (directory / L"missing.png").wstring()}}};
    remaining = 48; first.sync(source, rows, 96, wake, retained, remaining);
    check(!first.pixels({1, 1}), "Key/version replacement drops stale visuals");
    wait([] { const auto s = ImageResources::statistics(); return !s.active && !s.queued; });
    remaining = 48; first.sync(source, rows, 96, wake, retained, remaining);
    const auto failed = ImageResources::statistics();
    for (int i = 0; i < 10; ++i) { remaining = 48; first.sync(source, rows, 96, wake, retained, remaining); }
    const auto after = ImageResources::statistics();
    check(first.count() == 1 && !first.pixels({1, 2}) && !after.queued && !after.active &&
        after.rejected == failed.rejected, "Failed row images do not retry on each paint");
    for (auto invalid : {std::wstring(32768, L'x'), std::wstring(L"a\0b", 3)}) {
        bool rejected{};
        try { remaining = 48; first.sync(source, {{{1, 1}, {ButtonIcon::none, invalid}}}, 96, wake, retained, remaining); }
        catch (const std::invalid_argument&) { rejected = true; }
        check(rejected, "Invalid row image paths fail before I/O");
    }
    remaining = 48; first.sync(source, {}, 96, wake, retained, remaining);
    check(!first.count(), "Hidden or empty rows release image requests");
    first.clear(); second.clear(); third.clear(); source.reset(); empty();
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
    ImageKind kind;
    Gate(ImageDecodeStage stage, ImageKind worker = ImageKind::wic) : kind(worker) {
        entered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        release_gate = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        gated_stage = stage;
        (kind == ImageKind::shell ? ImageDecodeTestAccess::shell_hook : ImageDecodeTestAccess::hook) = hook;
    }
    void await() { check(WaitForSingleObject(entered, 10000) == WAIT_OBJECT_0, "Decode reaches gated boundary"); }
    void release() {
        (kind == ImageKind::shell ? ImageDecodeTestAccess::shell_hook : ImageDecodeTestAccess::hook) = nullptr;
        SetEvent(release_gate);
    }
    ~Gate() {
        release();
        wait([] { return !ImageResources::statistics().active; });
        CloseHandle(entered); CloseHandle(release_gate);
    }
};
void tab_image_tests() {
    TabStrip tabs;
    tabs.set_tabs({{1, L"Folder", ButtonIcon::folder, directory.wstring()},
        {2, L"Image", ButtonIcon::none, path(0)}}, 1);
    RowImages images;
    auto wake = std::make_shared<TaskWake>();
    std::vector<std::uint64_t> retained;
    const auto sync = [&](UINT dpi = 96) {
        std::vector<RowVisual> rows;
        for (const auto& tab : tabs.tabs()) rows.push_back({{tab.id, 0}, {tab.icon, tab.image_path}});
        size_t remaining = 48;
        retained.clear();
        return images.sync_visuals(std::move(rows), dpi, wake, retained, remaining, 16);
    };
    {
        Gate gate(ImageDecodeStage::before_delivery, ImageKind::shell);
        sync(); gate.await();
        const auto pending = RowImagesTestAccess::request(images, {1, 0});
        check(pending && pending->kind == ImageKind::shell && pending->size.width == 16,
            "Folder tab requests use the shared Shell worker at 16 physical pixels");
        auto reordered = tabs.tabs();
        std::reverse(reordered.begin(), reordered.end());
        reordered[1].title = L"Renamed";
        tabs.set_tabs(reordered, 2); sync();
        check(RowImagesTestAccess::request(images, {1, 0}) == pending && !pending->cancelled,
            "Tab selection, title changes, and reorder preserve pending image identity");
        reordered[1].image_path = (directory / L"missing-folder").wstring();
        tabs.set_tabs(reordered, 2); sync();
        check(pending->cancelled && !images.pixels({1, 0}),
            "Changed tab paths cancel old requests before stale delivery");
        tabs.set_tabs({}, {}); sync();
        check(images.count() == 0, "Closed or hidden tabs release requests");
        gate.release();
    }
    tabs.set_tabs({{1, L"Folder", ButtonIcon::folder, directory.wstring()}}, 1);
    sync();
    wait([] { const auto s = ImageResources::statistics(); return !s.active && !s.queued; });
    sync();
    check(images.pixels({1, 0}) && !retained.empty(), "Folder tab completion retains Shell pixels");
    const auto original = images.pixels({1, 0})->id;
    sync(192);
    check(!images.pixels({1, 0}), "Tab DPI changes release the previous image");
    wait([] { const auto s = ImageResources::statistics(); return !s.active && !s.queued; });
    sync(192);
    check(images.pixels({1, 0}) && images.pixels({1, 0})->id != original &&
        images.pixels({1, 0})->size.width == 32, "High-DPI tabs receive newly sized Shell pixels");
    const auto settled = ImageResources::statistics();
    for (int frame = 0; frame < 20; ++frame)
        check(!sync(192), "Settled tab images do not request another paint");
    const auto idle = ImageResources::statistics();
    check(idle.decoded == settled.decoded && idle.cache_hits == settled.cache_hits &&
        !idle.active && !idle.queued, "Settled tabs do not queue image work on repeated reconciliation");
    images.clear(); empty();
}
void ordinary_row_image_refresh_test() {
    struct Source final : ItemsSource {
        size_t size() const override { return 1; }
        ItemKey key(size_t) const override { return {1, 0}; }
        std::optional<size_t> find(ItemKey key) const override {
            return key == ItemKey{1, 0} ? std::optional<size_t>{0} : std::nullopt;
        }
        ItemContent item(size_t) const override { return {}; }
    };
    const auto file = directory / L"ordinary-refresh.png";
    image_fixture::png(file, 32, 32, 0xffff0000);
    const auto written = std::filesystem::last_write_time(file);
    auto source = std::make_shared<Source>();
    RowImages images;
    auto wake = std::make_shared<TaskWake>();
    std::vector<std::uint64_t> retained;
    const auto sync = [&] {
        size_t remaining = 48;
        retained.clear();
        return images.sync(source, {{{1, 0}, {ButtonIcon::none, file.wstring()}}}, 96, wake, retained, remaining);
    };
    const auto deliver = [&] {
        wait([] { const auto s = ImageResources::statistics(); return !s.active && !s.queued; });
        sync();
    };
    sync(); deliver();
    check(images.pixels({1, 0}) != nullptr, "Ordinary source loads its initial image");
    const auto original = images.pixels({1, 0})->id;
    image_fixture::png(file, 32, 32, 0xff00ff00);
    std::filesystem::last_write_time(file, written + 2s);
    {
        Gate gate(ImageDecodeStage::before_delivery);
        source = std::make_shared<Source>();
        check(sync(), "Ordinary source replacement invalidates a loaded row with the same key and path");
        gate.await();
        const auto stale = RowImagesTestAccess::request(images, {1, 0});
        check(stale && !images.pixels({1, 0}) && retained.empty(),
            "Ordinary source refresh does not retain stale file pixels");
        source = std::make_shared<Source>();
        sync();
        check(stale->cancelled && RowImagesTestAccess::request(images, {1, 0}) != stale,
            "Ordinary source replacement also cancels an in-flight request with the same key and path");
        gate.release(); deliver();
        const auto pixels = images.pixels({1, 0});
        check(pixels && pixels->id != original && std::to_integer<unsigned>(pixels->pixels[1]) == 255 &&
            std::to_integer<unsigned>(pixels->pixels[2]) == 0,
            "Ordinary source refresh reloads changed file contents even when key version remains zero");
    }
    images.clear(); empty();
}
void navigation_row_image_tests() {
    NavigationView nav;
    RowImages images;
    auto wake = std::make_shared<TaskWake>();
    std::vector<std::uint64_t> retained;
    const auto entry = [](std::uint64_t id, size_t file) {
        NavigationItem item;
        item.key = {id, 1}; item.label = L"Folder"; item.image_path = path(file);
        return item;
    };
    std::vector<NavigationItem> entries{entry(1, 0), entry(2, 1), entry(3, 2), entry(4, 3)};
    const auto refresh = [&] {
        nav.set_items(entries);
        nav.arrange({0, 0, 280, 600});
    };
    const auto sync = [&](UINT dpi = 96, size_t budget = 48) {
        const auto available = budget;
        std::vector<RowVisual> rows;
        const auto source = nav.items()->source();
        for (const auto& row : nav.items()->visible_content()) {
            auto visual = source->visual(row.index);
            if (visual.icon == ButtonIcon::none) visual.icon = row.content.icon;
            if (visual.image_path.empty()) visual.image_path = row.content.image_path;
            if (row.navigation && !visual.image_path.empty() && visual.icon == ButtonIcon::none)
                visual.icon = ButtonIcon::folder;
            rows.push_back({row.key, std::move(visual), row.navigation});
        }
        retained.clear();
        const auto changed = images.sync(source, std::move(rows), dpi, wake, retained, budget, true);
        check(images.count() <= RowImages::maximum_images && images.count() + budget == available,
            "Navigation refresh respects row and shared image budgets");
        return changed;
    };
    const auto deliver = [&] {
        wait([] { const auto s = ImageResources::statistics(); return !s.active && !s.queued; });
        sync();
    };
    refresh(); sync(); deliver();
    check(images.pixels({1, 1}) && images.pixels({2, 1}) && images.pixels({3, 1}) && images.pixels({4, 1}),
        "Actual navigation rows load Shell image paths");
    const auto loaded = images.pixels({1, 1})->id;
    {
        Gate gate(ImageDecodeStage::before_delivery, ImageKind::shell);
        entries.push_back(entry(5, 4)); entries.push_back(entry(6, 5)); entries.push_back(entry(7, 6));
        entries.push_back(entry(9, 12));
        refresh(); sync(); gate.await();
        const auto pending = RowImagesTestAccess::request(images, {5, 1});
        const auto changed = RowImagesTestAccess::request(images, {6, 1});
        const auto removed = RowImagesTestAccess::request(images, {7, 1});
        const auto queued = RowImagesTestAccess::request(images, {9, 1});
        check(pending && changed && removed && queued, "Navigation has active and queued image requests");
        std::weak_ptr<const ItemsSource> previous = nav.items()->source();
        entries = {entries[4], entries[0], entry(2, 7), entries[3], entry(6, 8), entry(8, 9), entries[7]};
        entries[1].icon = ButtonIcon::drive;
        entries[3].image_path.clear();
        refresh();
        check(previous.expired(), "Navigation refresh replaces and releases its old snapshot");
        check(sync(), "Changed and removed row images request a repaint");
        check(images.pixels({1, 1}) && images.pixels({1, 1})->id == loaded &&
            retained == std::vector<std::uint64_t>{loaded} && images.visual({1, 1}).icon == ButtonIcon::drive,
            "Reordered surviving keys retain loaded pixels and update fallback vectors without a blank frame");
        check(RowImagesTestAccess::request(images, {5, 1}) == pending && !pending->cancelled &&
            RowImagesTestAccess::request(images, {9, 1}) == queued && !queued->cancelled,
            "Snapshot replacement retains active and queued decodes for unchanged visuals");
        check(changed->cancelled && removed->cancelled &&
            RowImagesTestAccess::request(images, {6, 1}) != changed && !images.pixels({2, 1}) &&
            !images.pixels({3, 1}) && !images.pixels({4, 1}) && images.count() == 6,
            "Changed paths, empty paths and removed keys drop old pixels and cancel obsolete queued work");
        for (int i = 0; i < 3; ++i) {
            refresh();
            check(!sync() && images.pixels({1, 1})->id == loaded &&
                RowImagesTestAccess::request(images, {5, 1}) == pending &&
                RowImagesTestAccess::request(images, {9, 1}) == queued,
                "Repeated navigation refreshes do not restart work or discard decoded icons");
        }
        gate.release(); deliver();
        check(images.pixels({5, 1}) && images.pixels({6, 1}) && images.pixels({2, 1}) &&
            images.pixels({8, 1}) && images.pixels({9, 1}) && !images.pixels({7, 1}) && images.count() == 6,
            "Retained and replacement requests complete only into current navigation rows");
    }
    {
        Gate gate(ImageDecodeStage::before_delivery, ImageKind::shell);
        entries[1].image_path = path(10);
        refresh(); sync(); gate.await();
        const auto stale = RowImagesTestAccess::request(images, {1, 1});
        entries[1].image_path = path(11);
        refresh(); sync();
        check(stale && stale->cancelled && !images.pixels({1, 1}) &&
            RowImagesTestAccess::request(images, {1, 1}) != stale,
            "A path change cancels an active completion before it can replace the current icon");
        gate.release(); deliver();
        check(images.pixels({1, 1}) && images.visual({1, 1}).image_path == path(11),
            "Only the replacement path reaches the current navigation row");
    }
    {
        Gate gate(ImageDecodeStage::before_decode, ImageKind::shell);
        sync(192); gate.await();
        const auto stale = RowImagesTestAccess::request(images, {5, 1});
        refresh(); sync(144);
        const auto replacement = RowImagesTestAccess::request(images, {5, 1});
        check(stale && stale->cancelled && replacement && replacement != stale &&
            replacement->size == ImageSize{36, 36} && retained.empty(),
            "DPI changes cancel old work even across snapshot replacements");
        check(sync(144, 1) && images.count() == 1 && !replacement->cancelled,
            "A reduced shared budget evicts excess slots but preserves the retained pending row");
        sync(144, 0);
        check(!images.count() && replacement->cancelled, "A zero budget releases all row image work");
        gate.release();
    }
    images.clear(); empty();
}
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
        decode_tests(); cancellation_tests(); row_image_tests(); ordinary_row_image_refresh_test();
        navigation_row_image_tests(); tab_image_tests(); gpu_tests();
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
