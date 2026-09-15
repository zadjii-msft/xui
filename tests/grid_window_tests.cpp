#include "xui/application.hpp"
#include "xui/data_grid.hpp"
#include "../src/drawing.hpp"
#undef small
#include <windows.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <limits>

namespace {
int assertions{};
void require(bool value, const char* message) { ++assertions; if (!value) throw std::runtime_error(message); }
HWND owned(const wchar_t* title) {
    const auto h = FindWindowW(L"Xui.Window.1", title);
    DWORD pid{}; if (h) GetWindowThreadProcessId(h, &pid);
    return pid == GetCurrentProcessId() ? h : nullptr;
}
void delayed_close() {
    using namespace xui;
    const auto title = L"XUI delayed sampler shutdown";
    Window window({title, {420, 240}});
    auto root = std::make_shared<Stack>(Axis::vertical);
    root->add(std::make_shared<Label>(L"Cancellation test")); window.set_content(root);
    auto started = std::make_shared<std::atomic<bool>>();
    auto finished = std::make_shared<std::atomic<bool>>();
    int deliveries{};
    auto task = window.create_sample_task([started, finished](std::stop_token, bool) -> SampleTask::Payload {
        *started = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        *finished = true;
        return std::make_shared<int>(7);
    }, [&](auto, const auto&) { ++deliveries; });
    std::jthread closer([&] {
        for (int i = 0; i < 200; ++i) {
            if (const auto h = owned(title); h && started->load()) { PostMessageW(h, WM_CLOSE, 0, 0); return; }
            Sleep(10);
        }
    });
    const auto begin = std::chrono::steady_clock::now();
    require(Application::run(window) == 0, "Delayed-loader window closes normally");
    const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
    require(elapsed < 1000 && !finished->load(), "Closing revokes callback without joining blocked loader on UI thread");
    require(deliveries == 0, "No callback after closed window");
    for (int i = 0; i < 200 && !finished->load(); ++i) Sleep(10);
    require(finished->load(), "Deferred owner completes and releases worker");
    std::cout << "delayed_close_ms=" << elapsed << '\n';
}
void delivery() {
    using namespace xui;
    const auto title = L"XUI sample lifecycle";
    Window window({title, {460, 300}});
    auto content = std::make_shared<Stack>(Axis::vertical);
    auto chart = std::make_shared<HistoryChart>(L"Test chart");
    content->add(chart, 1); window.set_content(content);
    const auto ui_thread = GetCurrentThreadId();
    std::atomic<int> loaded{}, delivered{}, baselines{}, failures{}, successes{};
    std::atomic<bool> wrong_thread{}, paused{}, close_sent{};
    auto task = window.create_sample_task([&](std::stop_token, bool reset) -> SampleTask::Payload {
        if (GetCurrentThreadId() == ui_thread) wrong_thread = true;
        if (reset) ++baselines;
        const auto count = ++loaded;
        // A latest-result mailbox can replace results before the first window paint.
        // Keep the error pending until a receiver observes it, then recover.
        if (delivered > 0 && failures == 0) throw std::runtime_error("Controlled sample error");
        return std::make_shared<int>(count);
    }, [&](auto payload, const auto& error) {
        if (GetCurrentThreadId() != ui_thread) wrong_thread = true;
        if (!error.empty()) ++failures;
        else if (payload) { ++successes; chart->append(double(*std::static_pointer_cast<const int>(payload))); }
        ++delivered;
    }, 100);
    window.on_key([&](const KeyEvent& e) {
        if (e.key == Key::p) { paused = !paused.load(); task->pause(paused); return true; }
        return false;
    });
    std::atomic<bool> stable{};
    std::jthread driver([&] {
        HWND h{};
        for (int i = 0; i < 500; ++i) {
            h = owned(title);
            if (h && successes >= 2 && failures >= 1) break;
            Sleep(10);
        }
        if (!h) return;
        PostMessageW(h, WM_KEYDOWN, 'P', 0);
        for (int i = 0; i < 100 && !paused; ++i) Sleep(10);
        Sleep(100);
        const auto before = delivered.load();
        const auto paints = SendMessageW(h, WM_APP + 60, 0, 0);
        Sleep(350);
        stable = delivered == before && SendMessageW(h, WM_APP + 60, 0, 0) == paints;
        PostMessageW(h, WM_KEYDOWN, 'P', 0);
        for (int i = 0; i < 100 && baselines < 2; ++i) Sleep(10);
        close_sent = true;
        PostMessageW(h, WM_CLOSE, 0, 0);
    });
    require(Application::run(window) == 0, "Sample lifecycle window runs");
    driver.join();
    require(close_sent && stable, "Pause cancels scheduling and produces no idle paint");
    require(baselines >= 2, "Resume requests a new baseline");
    require(!wrong_thread, "Loader is off UI; delivery is on UI thread");
    require(failures >= 1 && successes >= 2, "Loader error is visible and subsequent samples recover");
}
class VirtualSource final : public xui::GridSource {
public:
    const std::size_t count;
    mutable std::atomic<std::uint64_t> text_requests{};
    explicit VirtualSource(std::size_t size) : count(size) {}
    std::size_t size() const override { return count; }
    xui::RowKey key(std::size_t row) const override { return {row + 1, 1}; }
    std::optional<std::size_t> find(xui::RowKey value) const override {
        return value.id > 0 && value.id <= count && value.version == 1 ? std::optional<std::size_t>(value.id - 1) : std::nullopt;
    }
    std::wstring text(std::size_t row, std::size_t column) const override {
        ++text_requests;
        return column ? std::to_wstring(row) : L"Synthetic row";
    }
};
class HoverSource final : public xui::GridSource {
public:
    std::size_t count{1000000};
    mutable std::size_t hit_queries{};
    std::size_t size() const override { return count; }
    xui::RowKey key(std::size_t row) const override { return {row + 1, 1}; }
    std::optional<std::size_t> find(xui::RowKey key) const override {
        return key.id && key.id <= count && key.version == 1 ? std::optional<std::size_t>{key.id - 1} : std::nullopt;
    }
    bool selectable(std::size_t row) const override { ++hit_queries; return row != 2; }
    std::wstring text(std::size_t, std::size_t) const override { return L"Hover fixture"; }
};
void hover_model() {
    using namespace xui;
    DataGrid grid;
    auto source = std::make_shared<HoverSource>();
    grid.set_columns({{L"Name", 100}});
    grid.set_source(source); grid.arrange({0, 0, 300, 180});
    grid.select({7, 1}, false); grid.set_focused(true);
    unsigned paints{}, selections{};
    grid.set_invalidator([&](Invalidation) { ++paints; });
    grid.on_select([&] { ++selections; });
    grid.hover_pointer(Point{10, 39});
    require(grid.hovered_row() == 0 && paints == 1, "Grid hover hits the first row in local DIPs");
    grid.hover_pointer(Point{280, 60});
    require(grid.hovered_row() == 0 && paints == 1, "Full-row hover ignores column whitespace and same-row movement does not repaint");
    grid.hover_pointer(Point{10, 75});
    require(grid.hovered_row() == 1 && paints == 2, "Crossing a row boundary repaints once");
    grid.hover_pointer(Point{10, 103});
    require(!grid.hovered_row() && paints == 3, "Nonselectable rows have no hover");
    for (Point point : {Point{-1, 40}, Point{288, 40}, Point{10, 37}, Point{10, 168}, Point{10, -1}})
        { grid.hover_pointer(point); require(!grid.hovered_row(), "Headers, scrollbars and outside coordinates do not hover rows"); }
    grid.hover_pointer(Point{10, 39}); grid.set_offset(32, 0);
    require(grid.hovered_row() == 1, "A stationary pointer follows the current vertical offset");
    grid.set_offset(64, 0);
    require(!grid.hovered_row(), "Scrolling a disabled row under the pointer clears hover");
    grid.set_offset(96, 0);
    require(grid.hovered_row() == 3, "Scrolling away from a disabled row restores hover without movement");
    grid.arrange({0, 0, 15, 180});
    require(!grid.hovered_row(), "Resize rechecks the current horizontal viewport");
    grid.arrange({0, 0, 300, 45});
    require(!grid.hovered_row(), "Collapsed row viewport has no hover");
    grid.arrange({0, 0, 300, 180});
    require(grid.selected() == RowKey{7, 1} && grid.focused() && !selections, "Hover never changes selection or keyboard focus");
    grid.hover_pointer(Point{10, 135});
    grid.clear_selection();
    auto small = std::make_shared<HoverSource>(); small->count = 1;
    std::weak_ptr<HoverSource> old = source;
    require(source->hit_queries < 50, "Hover queries are constant-time even with a million-row source");
    source.reset(); grid.set_source(small);
    require(old.expired() && !grid.hovered_row(), "Source replacement releases old rows and rechecks blank space");
    grid.hover_pointer(Point{10, 40});
    grid.set_enabled(false);
    require(!grid.hovered_row(), "Disabled controls have no hover");
    grid.set_enabled(true); grid.hover_pointer(Point{10, 40}); grid.cancel();
    require(!grid.hovered_row(), "Capture cancellation clears the hover pointer");
    grid.hover_pointer(Point{10, 40}); grid.hover_pointer({});
    require(!grid.hovered_row(), "Mouse leave clears hover");
    bool rejected{};
    try { grid.hover_pointer(Point{0, std::numeric_limits<float>::quiet_NaN()}); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "Nonfinite hover coordinates fail explicitly");
}
void hover_window() {
    using namespace xui;
    for (const auto mode : {ThemeMode::dark, ThemeMode::light, ThemeMode::high_contrast}) {
        const auto title = L"XUI grid hover pixels";
        Window window({title, {500, 300}}); window.set_theme(mode);
        auto grid = std::make_shared<DataGrid>(L"Hover rows");
        auto source = std::make_shared<HoverSource>();
        grid->set_columns({{L"Name", 160}});
        grid->set_source(source); grid->select({2, 1}, false);
        auto root = std::make_shared<Stack>(Axis::vertical);
        root->add(grid, 1); window.set_content(root);
        unsigned selections{};
        grid->on_select([&] { ++selections; });
        require(window.post([&] {
            const auto hwnd = owned(title);
            const auto peer = FindWindowExW(hwnd, nullptr, L"Xui.Control.1", L"Hover rows");
            require(hwnd && peer, "Owned hover grid exists");
            const auto dpi = GetDpiForWindow(hwnd);
            const auto repaint = [&] {
                SendMessageW(hwnd, WM_APP + 12, 0, 0);
                RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
            };
            const auto move = [&](int x, int y, WPARAM buttons = 0) {
                SendMessageW(peer, WM_MOUSEMOVE, buttons, MAKELPARAM(MulDiv(x, dpi, 96), MulDiv(y, dpi, 96)));
                repaint();
            };
            const auto sample = [&](float x, float y) {
                POINT point{static_cast<LONG>(std::lround(x * dpi / 96)), static_cast<LONG>(std::lround(y * dpi / 96))};
                MapWindowPoints(peer, hwnd, &point, 1);
                const auto dc = GetDC(hwnd);
                const auto value = GetPixel(dc, point.x, point.y);
                ReleaseDC(hwnd, dc); require(value != CLR_INVALID, "Read a rendered grid pixel");
                return value;
            };
            const auto color = [](D2D1_COLOR_F value) {
                return RGB(std::lround(value.r * 255), std::lround(value.g * 255), std::lround(value.b * 255));
            };
            const auto palette = Palette::system(mode);
            repaint();
            const auto focus = GetFocus();
            const auto x = grid->viewport_width() - 8;
            const auto header = sample(x, 18);
            const auto bar = sample(grid->viewport_width() + 6, 52);
            std::vector<COLORREF> edge;
            for (int px = 0; px < 5; ++px) edge.push_back(sample(static_cast<float>(px), 54));
            move(50, 54);
            require(grid->hovered_row() == 0 && sample(x, 54) == color(palette.hover) &&
                sample(4, 54) == color(palette.hover), "Themed hover fills the visible row, including space beyond the last column");
            if (palette.high_contrast) {
                bool outlined{};
                for (int px = 0; px < 5; ++px) outlined |= edge[px] != sample(static_cast<float>(px), 54);
                require(outlined, "High contrast adds a visible row outline without changing text or background contrast");
            }
            require(header == sample(x, 18) && bar == sample(grid->viewport_width() + 6, 52),
                "Row hover does not paint headers or scrollbars");
            move(50, 86);
            require(sample(x, 86) == color(palette.selection), "Selection color takes priority over hover");
            move(50, 118);
            require(!grid->hovered_row() && sample(x, 118) == color(palette.background), "Disabled rows never paint hover");
            move(50, 18);
            require(!grid->hovered_row(), "Native header movement clears row hover");
            move(50, 54);
            SendMessageW(peer, WM_MOUSELEAVE, 0, 0); repaint();
            require(!grid->hovered_row() && sample(x, 54) == color(palette.background), "Mouse leave restores row background");
            move(50, 54);
            grid->set_offset(96, 0); repaint();
            require(grid->hovered_row() == 3 && sample(x, 54) == color(palette.hover),
                "Stationary native pointer re-hits the scrolled source during painting");
            move(50, 54, MK_LBUTTON);
            require(!grid->hovered_row(), "Dragging across rows suppresses hover");
            move(50, 54); SendMessageW(peer, WM_CANCELMODE, 0, 0); repaint();
            require(!grid->hovered_row(), "Native input cancellation clears hover");
            move(50, 54); SetCapture(peer); ReleaseCapture(); repaint();
            require(!grid->hovered_row(), "Capture loss clears hover");
            require(grid->selected() == RowKey{2, 1} && !selections && GetFocus() == focus,
                "Hover and scrolling leave native focus and selection unchanged");
            move(50, 54); grid->set_enabled(false); repaint();
            require(!grid->hovered_row() && sample(x, 54) == color(palette.surface), "Disabled grid retains striping without hover");
            grid->set_enabled(true);
            auto small = std::make_shared<HoverSource>(); small->count = 1;
            grid->set_source(small); move(50, 150);
            require(!grid->hovered_row() && sample(x, 150) == color(palette.background), "Empty area below the source never highlights");
            require(grid->selected() == RowKey{2, 1} && !selections, "Hover never emits selection events");
            window.close();
        }), "Queue hover checks on the UI thread");
        require(Application::run(window) == 0, "Hover window completes");
    }
}
void virtual_rendering() {
    using namespace xui;
    const auto title = L"XUI million-row rendering";
    Window window({title, {600, 380}});
    auto content = std::make_shared<Stack>(Axis::vertical);
    auto grid = std::make_shared<DataGrid>(L"Synthetic data");
    auto small = std::make_shared<VirtualSource>(100000);
    auto large = std::make_shared<VirtualSource>(1000000);
    grid->set_columns({{L"Name", 300}, {L"Number", 160, true}});
    grid->set_source(small); content->add(grid, 1); window.set_content(content);
    std::atomic<bool> replaced{};
    window.on_key([&](const KeyEvent& e) {
        if (e.key != Key::s) return false;
        grid->select({99999, 1}); grid->set_source(large); grid->edge(true);
        replaced = true; return true;
    });
    std::atomic<bool> bounded{}, one_peer{}, end_visible{};
    std::jthread driver([&] {
        HWND h{};
        for (int i = 0; i < 300; ++i) {
            h = owned(title);
            if (h && SendMessageW(h, WM_APP + 60, 0, 0) > 0) break;
            Sleep(10);
        }
        if (!h) return;
        PostMessageW(h, WM_KEYDOWN, 'S', 0);
        for (int i = 0; i < 100 && (!replaced || large->text_requests == 0); ++i) Sleep(10);
        const auto paints = SendMessageW(h, WM_APP + 60, 0, 0);
        one_peer = SendMessageW(h, WM_APP + 60, 14, 0) == 1;
        end_visible = SendMessageW(h, WM_APP + 60, 19, 0) == 1000000 && SendMessageW(h, WM_APP + 60, 18, 0) <= 12;
        // Two columns and at most 12 visible/boundary rows. One in-progress frame is permitted.
        bounded = small->text_requests + large->text_requests <= static_cast<std::uint64_t>((paints + 1) * 24);
        PostMessageW(h, WM_CLOSE, 0, 0);
    });
    require(Application::run(window) == 0, "Synthetic grid window renders");
    driver.join();
    require(one_peer && end_visible && grid->selected() == RowKey{1000000, 1}, "Dynamic million-row source keeps one peer and reveals final row");
    require(bounded && small->text_requests > 0 && large->text_requests > 0, "Actual renderer requests only visible cells for 100k and 1m sources");
}
void column_dpi_input() {
    using namespace xui;
    const auto title = L"XUI column input at multiple DPI scales";
    Window window({title, {600, 380}});
    auto content = std::make_shared<Stack>(Axis::vertical);
    auto grid = std::make_shared<DataGrid>(L"DPI columns");
    grid->set_columns({{L"Name", 180}, {L"PID", 80, true}, {L"CPU", 92, true}, {L"Memory", 142, true}});
    grid->set_source(std::make_shared<VirtualSource>(1000000));
    grid->select({1234, 1});
    grid->set_sort(2, true);
    content->add(grid, 1); window.set_content(content);
    std::atomic<int> verified{};
    window.on_key([&](const KeyEvent& event) {
        if (event.key == Key::s) {
            const auto h = owned(title);
            const auto peer = FindWindowExW(h, nullptr, L"Xui.Control.1", L"DPI columns");
            require(h && peer, "Owned DPI grid exists");
            // Keep each synthetic gesture on the UI thread. Otherwise queued
            // hardware moves at the physical cursor can interrupt its capture.
            for (UINT dpi : {96u, 144u, 192u}) {
                RECT bounds{}; GetWindowRect(h, &bounds);
                bounds.right = bounds.left + MulDiv(540, dpi, 96);
                bounds.bottom = bounds.top + MulDiv(380, dpi, 96);
                SendMessageW(h, WM_DPICHANGED, MAKELONG(dpi, dpi), reinterpret_cast<LPARAM>(&bounds));
                SendMessageW(h, WM_APP + 12, 0, 0);
                const auto mouse = [&](UINT message, int x) {
                    SendMessageW(peer, message, message == WM_LBUTTONUP ? 0 : MK_LBUTTON,
                        MAKELPARAM(MulDiv(x, dpi, 96), MulDiv(18, dpi, 96)));
                };
                mouse(WM_LBUTTONDOWN, 180); mouse(WM_MOUSEMOVE, 220); mouse(WM_LBUTTONUP, 220);
                mouse(WM_LBUTTONDOWN, 40); mouse(WM_MOUSEMOVE, 430); mouse(WM_LBUTTONUP, 430);
                if (grid->column_order() == std::vector<std::size_t>{1, 2, 0, 3} &&
                    std::abs(grid->columns()[2].width - 220) < 1 &&
                    grid->selected() == RowKey{1234, 1} && grid->sort_column() == 2) ++verified;
                grid->set_columns({{L"Name", 180}, {L"PID", 80, true}, {L"CPU", 92, true}, {L"Memory", 142, true}});
                grid->set_offset(grid->offset(), 0);
            }
            window.close();
            return true;
        }
        return false;
    });
    std::jthread driver([&] {
        HWND h{};
        for (int i = 0; i < 300; ++i) {
            h = owned(title);
            if (h && SendMessageW(h, WM_APP + 60, 0, 0) > 0) break;
            Sleep(10);
        }
        if (!h) return;
        PostMessageW(h, WM_KEYDOWN, 'S', 0);
    });
    require(Application::run(window) == 0, "Column input window runs at simulated DPI scales");
    driver.join();
    require(verified == 3, "Native header resize and reorder preserve DIP widths and source identities at 96, 144 and 192 DPI");
}
}
int main(int argc, char** argv) {
    try {
        hover_model();
        hover_window();
        if (argc == 2 && std::string_view(argv[1]) == "--hover-only") {
            std::cout << assertions << " grid hover assertions passed\n"; return 0;
        }
        delayed_close(); delivery(); virtual_rendering(); column_dpi_input(); std::cout << assertions << " grid and sample lifecycle assertions passed\n"; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
