#include "xui/application.hpp"
#include "xui/data_grid.hpp"
#include <windows.h>
#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

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
}
int main() {
    try { delayed_close(); delivery(); virtual_rendering(); std::cout << assertions << " grid and sample lifecycle assertions passed\n"; return 0; }
    catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
