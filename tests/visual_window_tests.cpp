#include "image_fixtures.hpp"
#include "xui/application.hpp"
#include "xui/data_grid.hpp"
#include "xui/navigation.hpp"
#include "xui/miller_columns.hpp"
#include "../src/images.hpp"
#include <chrono>
#include <iostream>
#include <thread>

namespace {
using namespace xui;
using namespace std::chrono_literals;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
struct Rows final : GridSource {
    std::wstring path;
    DataGrid* grid{};
    mutable std::size_t visuals{};
    std::size_t count{1000000};
    uint64_t generation{1};
    size_t size() const override { return count; }
    RowKey key(size_t row) const override { return {row + 1, generation}; }
    std::optional<size_t> find(RowKey key) const override {
        return key.version == generation && key.id && key.id <= count ? std::optional<size_t>{key.id - 1} : std::nullopt;
    }
    std::wstring text(size_t row, size_t) const override { return L"Row " + std::to_wstring(row); }
    ItemVisual visual(size_t row, size_t column) const override {
        check(column == 0, "Visual metadata follows source column zero after reorder");
        const auto [first, end] = grid->visible_rows();
        check(row >= first && row < end && grid->visible(), "Only visible grid rows request visual metadata");
        ++visuals;
        return {ButtonIcon::none, path};
    }
};
struct Items final : ItemsSource {
    std::wstring path;
    mutable std::size_t visuals{};
    size_t size() const override { return 1000000; }
    ItemKey key(size_t row) const override { return {row + 1, 1}; }
    std::optional<size_t> find(ItemKey key) const override {
        return key.version == 1 && key.id && key.id <= size() ? std::optional<size_t>{key.id - 1} : std::nullopt;
    }
    ItemContent item(size_t) const override { return {L"Palette result"}; }
    ItemVisual visual(size_t row) const override {
        check(row < 10, "Only visible palette rows request visual metadata");
        ++visuals; return {ButtonIcon::none, path};
    }
};
std::function<void(HWND)> tick;
std::exception_ptr failure;
void CALLBACK timer(HWND hwnd, UINT, UINT_PTR, DWORD) noexcept {
    try { tick(hwnd); }
    catch (...) { failure = std::current_exception(); PostMessageW(hwnd, WM_CLOSE, 0, 0); }
}
void context_selection() {
    DataGrid grid;
    auto source = std::make_shared<Rows>(); source->grid = &grid; source->count = 3;
    grid.set_columns({{L"Name", 240}}); grid.set_source(source); grid.arrange({0, 0, 400, 250});
    grid.select({1, 1});
    grid.prepare_context_menu(Point{50, DataGrid::header_height + DataGrid::row_height + 4});
    check(grid.selected() == RowKey{2, 1}, "Pointer context selects the clicked row");
    grid.prepare_context_menu(Point{50, 230});
    check(!grid.selected() && grid.selection().empty(), "Empty context clears stale selection");
    grid.select({2, 1}); grid.prepare_context_menu({});
    check(grid.selected() == RowKey{2, 1}, "Keyboard context preserves current selection");
    grid.prepare_context_menu(Point{-1, 50});
    check(!grid.selected(), "Negative pointer coordinates cannot select a row");
    grid.select({2, 1});
    auto replacement = std::make_shared<Rows>(); replacement->grid = &grid; replacement->generation = 2;
    grid.set_source(replacement); grid.prepare_context_menu({});
    check(!grid.selected(), "Filtered identity replacement cannot use stale context selection");
}
void miller_images(const std::filesystem::path& directory) {
    struct ColumnItems final : ItemsSource {
        std::wstring path;
        std::size_t count{100};
        std::size_t size() const override { return count; }
        ItemKey key(std::size_t row) const override { return {row + 1, 1}; }
        std::optional<std::size_t> find(ItemKey key) const override {
            return key.version == 1 && key.id && key.id <= count ? std::optional<std::size_t>{key.id - 1} : std::nullopt;
        }
        ItemContent item(std::size_t) const override {
            ItemContent result{L"File", {}, ButtonIcon::library};
            result.image_path = path; return result;
        }
    };
    Window window({L"XUI Miller row images", {980, 850}});
    auto source = std::make_shared<ColumnItems>();
    source->path = (directory / L"blue.png").wstring();
    auto sidebar = std::make_shared<FileList>();
    auto files = std::make_shared<std::vector<FileItem>>();
    for (std::size_t i = 0; i < 100; ++i) files->push_back({i + 1, L"File", source->path, false});
    sidebar->set_items(files); sidebar->set_thumbnails(true); sidebar->set_preferred_size({180, 850});
    auto columns = std::make_shared<MillerColumns>();
    columns->set_column_width(180);
    std::vector<MillerColumn> path{{L"Root", source, {}}};
    columns->set_columns(path);
    auto root = std::make_shared<Stack>(Axis::horizontal);
    root->add(sidebar); root->add(columns, 1); window.set_content(root);
    const auto before = ImageResources::statistics();
    auto changed_at = std::chrono::steady_clock::now();
    int phase{}, settled{};
    LRESULT idle_paints{};
    tick = [&](HWND hwnd) {
        const auto stats = ImageResources::statistics();
        check(std::chrono::steady_clock::now() - changed_at < 8s,
            "Every visible Miller column loads image pixels after navigation");
        check(stats.rejected == before.rejected, "Column navigation does not reject image requests");
        check(stats.cpu_bytes + stats.cpu_reserved <= ImageLimits::cpu_bytes &&
            stats.gpu_bytes + stats.gpu_reserved <= ImageLimits::gpu_bytes &&
            stats.queued <= ImageLimits::queue && stats.active <= ImageLimits::workers,
            "Miller navigation retains bounded image resources");
        if (phase == 5) {
            if (std::chrono::steady_clock::now() - changed_at < 300ms) return;
            check(!stats.active && !stats.queued && SendMessageW(hwnd, WM_APP + 60, 0, 0) == idle_paints,
                "Completed row images have no idle requests or repaints");
            KillTimer(hwnd, 98); window.close(); ++phase; return;
        }
        if (stats.active || stats.queued || ++settled < 4) return;
        RECT bounds{}; GetWindowRect(hwnd, &bounds);
        POINT client{}; ClientToScreen(hwnd, &client);
        const auto dc = GetDC(hwnd), memory = CreateCompatibleDC(dc);
        const auto bitmap = CreateCompatibleBitmap(dc, bounds.right - bounds.left, bounds.bottom - bounds.top);
        const auto previous = SelectObject(memory, bitmap);
        const bool captured = PrintWindow(hwnd, memory, 2) != FALSE;
        const auto scale = GetDpiForWindow(hwnd) / 96.0f;
        std::size_t visible{}, ready{};
        for (std::size_t i = 0; i < path.size(); ++i) {
            const auto list = columns->column_list(i);
            const auto b = list->bounds();
            const auto viewport = columns->bounds();
            if (b.x + 22 < viewport.x || b.x + 22 >= viewport.x + viewport.width) continue;
            for (const auto& row : list->visible_content()) {
                const auto y = row.bounds.y + row.bounds.height / 2;
                if (y < 0 || y >= b.height) continue;
                ++visible;
                const auto color = GetPixel(memory, client.x - bounds.left + int((b.x + 22) * scale),
                    client.y - bounds.top + int((b.y + y) * scale));
                ready += color == RGB(0x20, 0x40, 0xc0);
            }
        }
        SelectObject(memory, previous); DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(hwnd, dc);
        check(captured && visible, "Capture owned visible column images");
        if (ready != visible) return;
        if (phase == 0 && path.size() < 8) {
            if (path.size() == 1) check(window.focus(*columns->column_list(0)), "Keep ancestor focus during appended reveal");
            path.push_back({L"Child", source, {}});
            columns->set_columns(path);
        } else if (phase == 0) {
            columns->set_horizontal_offset(0); ++phase;
        } else if (phase == 1) {
            auto empty = std::make_shared<ColumnItems>(); empty->count = 0;
            path[1].source = empty; columns->set_columns(path); ++phase;
        } else if (phase == 2) {
            path[1].source = source; columns->set_columns(path);
            columns->set_horizontal_offset(columns->maximum_horizontal()); ++phase;
        } else if (phase == 3) {
            for (std::size_t i = 0; i < path.size(); ++i)
                columns->column_list(i)->set_offset(columns->column_list(i)->maximum_offset());
            ++phase;
        } else {
            idle_paints = SendMessageW(hwnd, WM_APP + 60, 0, 0); ++phase;
        }
        changed_at = std::chrono::steady_clock::now(); settled = 0;
    };
    window.post([&] {
        const auto hwnd = FindWindowW(L"Xui.Window.1", L"XUI Miller row images");
        check(hwnd && SetTimer(hwnd, 98, 25, timer), "Start Miller image navigation checks");
    });
    check(Application::run(window) == 0, "Miller image window succeeds");
    tick = {};
    if (failure) std::rethrow_exception(failure);
    check(phase == 6, "All row images survive deep navigation, horizontal and vertical scrolling, ancestor filtering, and idle");
    ImageResources::clear_unused();
    check(!ImageResources::statistics().gpu_bytes, "Closing columns releases image bitmaps");
}
void workload(const std::filesystem::path& directory) {
    Window window({L"XUI visible row visuals", {900, 650}});
    auto grid = std::make_shared<DataGrid>(L"Visual grid");
    grid->set_columns({{L"Name", 200}, {L"Other", 180}});
    grid->set_column_order({1, 0});
    auto source = std::make_shared<Rows>(); source->path = (directory / L"blue.png").wstring(); source->grid = grid.get();
    grid->set_source(source);
    auto items = std::make_shared<ItemsView>(L"Visual palette");
    auto item_source = std::make_shared<Items>(); item_source->path = source->path; items->set_items(item_source);
    items->set_maximum_size({10000, 160});
    auto navigation = std::make_shared<NavigationView>();
    NavigationItem header{{1, 1}, {}, L"Recents", ButtonIcon::history}; header.selectable = false;
    auto bookmarks = header; bookmarks.key = {3, 1}; bookmarks.label = L"Bookmarks"; bookmarks.icon = ButtonIcon::bookmark;
    auto storage = header; storage.key = {4, 1}; storage.label = L"Storage"; storage.icon = ButtonIcon::drive;
    NavigationItem folder{{2, 1}, {}, L"Folder", ButtonIcon::folder}; folder.image_path = directory.wstring();
    navigation->set_items({header, bookmarks, storage, folder});
    auto content = std::make_shared<Stack>(Axis::vertical); content->add(grid, 1); content->add(items);
    auto root = std::make_shared<Stack>(Axis::horizontal); root->add(navigation); root->add(content, 1); window.set_content(root);
    std::weak_ptr<Rows> old = source;
    const auto before = ImageResources::statistics();
    const auto started = std::chrono::steady_clock::now();
    int phase{};
    size_t hidden_queries{}, hidden_item_queries{};
    const auto sample = [&](HWND hwnd) {
        const auto bounds = grid->bounds();
        const auto scale = GetDpiForWindow(hwnd) / 96.0f;
        const auto dc = GetDC(hwnd);
        const auto color = GetPixel(dc, int((bounds.x + grid->columns()[0].width - grid->horizontal_offset() + 24) * scale),
            int((bounds.y + DataGrid::header_height + 16) * scale));
        ReleaseDC(hwnd, dc); return color;
    };
    tick = [&](HWND hwnd) {
        check(std::chrono::steady_clock::now() - started < 20s, "Visible visual workload deadline");
        const auto stats = ImageResources::statistics();
        check(stats.cpu_bytes + stats.cpu_reserved <= ImageLimits::cpu_bytes &&
            stats.gpu_bytes + stats.gpu_reserved <= ImageLimits::gpu_bytes &&
            stats.queued <= ImageLimits::queue && stats.active <= ImageLimits::workers, "Shared image budgets hold");
        if (stats.active || stats.queued) return;
        if (phase == 0) {
            if (sample(hwnd) != RGB(0x20, 0x40, 0xc0)) return;
            const auto dc = GetDC(hwnd);
            const auto scale = GetDpiForWindow(hwnd) / 96.0f;
            const auto list_bounds = navigation->items()->bounds();
            std::set<uint64_t> glyphs;
            const auto nav_rows = navigation->items()->visible_content();
            for (size_t i = 0; i < 3; ++i) {
                const auto bounds = nav_rows.at(i).bounds;
                const auto background = GetPixel(dc, int((list_bounds.x + bounds.x + 1) * scale),
                    int((list_bounds.y + bounds.y + 1) * scale));
                uint64_t hash = 14695981039346656037ull;
                unsigned ink{};
                for (int y = 0; y < 20; ++y) for (int x = 0; x < 20; ++x) {
                    const auto pixel = GetPixel(dc, int((list_bounds.x + bounds.x + 12 + x) * scale),
                        int((list_bounds.y + bounds.y + (bounds.height - 20) / 2 + y) * scale));
                    const bool drawn = pixel != background;
                    ink += drawn; hash = (hash ^ static_cast<uint64_t>(drawn)) * 1099511628211ull;
                }
                check(ink > 12, "Each new sidebar vector glyph draws visible strokes");
                glyphs.insert(hash);
            }
            ReleaseDC(hwnd, dc);
            check(glyphs.size() == 3, "History, Bookmark, and Drive draw distinct glyphs");
            check(source->visuals && source->visuals < 1000 && item_source->visuals && item_source->visuals < 1000,
                "Million-row visuals use bounded visible metadata");
            auto replacement = std::make_shared<Rows>(); replacement->path = (directory / L"red.png").wstring();
            replacement->grid = grid.get(); replacement->generation = 2;
            grid->set_source(replacement); source = replacement; phase = 1;
        } else if (phase == 1) {
            if (sample(hwnd) != RGB(0xc0, 0x40, 0x20)) return;
            check(old.expired(), "Thumbnail slots do not retain stale immutable sources");
            ShowWindow(hwnd, SW_HIDE);
            hidden_queries = source->visuals; hidden_item_queries = item_source->visuals; phase = 2;
        } else if (phase == 2 || phase == 4 || phase == 8) {
            check(source->visuals == hidden_queries && item_source->visuals == hidden_item_queries, "Hidden controls request no visuals");
            check(!stats.gpu_bytes, "Hiding rows releases uploaded bitmaps");
            check(stats.decoded - before.decoded <= 48, "Only bounded visible thumbnails decode");
            if (phase == 8) window.close();
            else { ShowWindow(hwnd, phase == 2 ? SW_SHOWNOACTIVATE : SW_RESTORE); ++phase; }
        } else if (phase == 3 || phase == 5 || phase == 7) {
            if (sample(hwnd) != RGB(0xc0, 0x40, 0x20)) return;
            if (phase == 3) ShowWindow(hwnd, SW_MINIMIZE);
            else if (phase == 5) grid->set_column_width(0, 1200);
            else { grid->set_visible(false); items->set_visible(false); navigation->set_visible(false); }
            hidden_queries = source->visuals; hidden_item_queries = item_source->visuals; ++phase;
        } else if (phase == 6) {
            check(source->visuals == hidden_queries, "A clipped source column zero requests no image metadata");
            grid->set_offset(0, grid->maximum_horizontal()); ++phase;
        }
    };
    window.post([&] {
        auto hwnd = FindWindowW(L"Xui.Window.1", L"XUI visible row visuals");
        check(hwnd && SetTimer(hwnd, 97, 25, timer), "Start owned-window visual checks");
    });
    check(Application::run(window) == 0, "Visual window succeeds");
    tick = {};
    if (failure) std::rethrow_exception(failure);
    check(phase == 8, "Visual workload completes");
}
}
int wmain(int argc, wchar_t** argv) {
    try {
        check(argc > 1, "Pass a project-local fixture directory");
        const auto directory = std::filesystem::absolute(argv[1]);
        std::filesystem::create_directories(directory);
        image_fixture::hr(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
        image_fixture::png(directory / L"blue.png", 24, 24, 0xff2040c0);
        image_fixture::png(directory / L"red.png", 24, 24, 0xffc04020);
        context_selection(); miller_images(directory); workload(directory);
        CoUninitialize();
        std::cout << "Visible row visuals, identity replacement, budgets, context selection passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
