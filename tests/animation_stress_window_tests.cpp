#include "xui/application.hpp"
#include "xui/adaptive_layout.hpp"
#include "xui/foundation.hpp"
#include "xui/reveal.hpp"
#include <windows.h>
#include <algorithm>
#include <chrono>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using namespace xui;
using Clock = std::chrono::steady_clock;
constexpr UINT metrics = WM_APP + 60, update = WM_APP + 12;
constexpr UINT_PTR driver = 98;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
double cpu_milliseconds() {
    FILETIME created{}, exited{}, kernel{}, user{};
    require(GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user) != FALSE, "Read fixture CPU time");
    const auto ticks = [](FILETIME value) {
        return (static_cast<unsigned long long>(value.dwHighDateTime) << 32) | value.dwLowDateTime;
    };
    return static_cast<double>(ticks(kernel) + ticks(user)) / 10000.0;
}
std::vector<HWND> editors(HWND host) {
    std::vector<HWND> result;
    EnumChildWindows(host, [](HWND child, LPARAM data) -> BOOL {
        wchar_t name[32]{};
        if (GetClassNameW(child, name, 32) && _wcsicmp(name, L"EDIT") == 0)
            reinterpret_cast<std::vector<HWND>*>(data)->push_back(child);
        return TRUE;
    }, reinterpret_cast<LPARAM>(&result));
    std::sort(result.begin(), result.end(), std::less<HWND>{});
    return result;
}
class MeasuredRoot final : public Stack {
public:
    MeasuredRoot() : Stack(Axis::vertical) {}
    std::function<void()> on_measure;
    Size measure(Size available) override {
        if (on_measure) on_measure();
        return Stack::measure(available);
    }
};
struct Fixture {
    Window window{{L"XUI bounded concurrent animation", {1040, 700}}};
    std::shared_ptr<MeasuredRoot> root = std::make_shared<MeasuredRoot>();
    std::shared_ptr<Button> anchor = std::make_shared<Button>(L"Stationary focus target");
    std::vector<std::shared_ptr<Reveal>> reveals;
    std::vector<std::shared_ptr<Progress>> progress;
    std::vector<HWND> original_editors;
    HWND host{};
    bool expanding{}, motion{}, done{}, middle{}, reversed{}, changing_target{};
    std::vector<bool> closed_at_layout;
    unsigned leg{};
    LRESULT peers{}, start_paints{}, start_layouts{}, target_layouts{}, settlement_layouts{}, frame_layouts{};
    LRESULT idle_paints{}, idle_layouts{}, peak_buffers{};
    DWORD start_gdi{}, peak_gdi{};
    double start_cpu{};
    Clock::time_point started{}, measurement_start{}, leg_start{}, idle_start{};
    std::exception_ptr error;
    explicit Fixture(std::size_t count, bool expand) : expanding(expand) {
        root->set_padding({8, 8, 8, 8});
        root->add(anchor);
        auto grid = std::make_shared<Grid>();
        const std::size_t columns = count == 64 ? 8 : count == 16 ? 4 : 1;
        std::vector<GridTrack> rows(count / columns, {TrackSizing::fixed, 64});
        std::vector<GridTrack> widths(columns, {TrackSizing::fixed, 120});
        grid->set_tracks(std::move(rows), std::move(widths));
        for (std::size_t i = 0; i < count; ++i) {
            auto cell = std::make_shared<Stack>(Axis::vertical);
            auto input = std::make_shared<TextInput>(L"Retained editor " + std::to_wstring(i));
            input->set_caption_visible(false);
            input->set_preferred_size({110, 28});
            input->set_text(L"Native text");
            auto reveal = std::make_shared<Reveal>(input);
            reveal->set_layout(expand ? RevealLayout::expand : RevealLayout::fixed);
            reveal->set_open(true);
            reveal->set_duration(600);
            auto value = std::make_shared<Progress>(L"Measured progress " + std::to_wstring(i));
            value->set_preferred_size({110, 24});
            value->set_value(90);
            value->set_duration(600);
            cell->add(reveal);
            cell->add(value);
            grid->add(cell, i / columns, i % columns);
            reveals.push_back(std::move(reveal));
            progress.push_back(std::move(value));
        }
        root->add(grid);
        closed_at_layout.resize(count);
        root->on_measure = [this] {
            if (leg < 2 || leg >= 6) return;
            bool closed{};
            for (std::size_t i = 0; i < reveals.size(); ++i) {
                const bool settled = !reveals[i]->open() && !reveals[i]->animating();
                closed |= settled && !closed_at_layout[i];
                closed_at_layout[i] = settled;
            }
            if (changing_target) ++target_layouts;
            else if (closed) ++settlement_layouts;
            else ++frame_layouts;
        };
        window.set_content(root);
    }
    LRESULT metric(WPARAM id) const { return SendMessageW(host, metrics, id, 0); }
    void flush() {
        SendMessageW(host, update, 0, 0);
        UpdateWindow(host);
    }
    bool active() const {
        return std::any_of(reveals.begin(), reveals.end(), [](const auto& item) { return item->animating(); }) ||
            std::any_of(progress.begin(), progress.end(), [](const auto& item) { return item->animating(); });
    }
    void target(bool open) {
        changing_target = true;
        for (std::size_t i = 0; i < reveals.size(); ++i) {
            reveals[i]->set_open(open);
            progress[i]->set_value(open ? 90 : 10);
        }
        flush();
        changing_target = false;
        leg_start = Clock::now();
    }
    void begin() {
        require(window.focus(*anchor), "Stress fixture receives focus");
        host = GetAncestor(GetFocus(), GA_ROOT);
        BOOL enabled{};
        require(SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0) != FALSE, "Read system motion policy");
        motion = enabled != FALSE;
        flush();
        original_editors = editors(host);
        require(original_editors.size() == reveals.size(), "Each retained cell has exactly one native editor");
        peers = metric(14);
        started = Clock::now();
        target(false);
        require(SetTimer(host, driver, 15, tick) != 0, "Start bounded stress observation");
    }
    void step() {
        const auto now = Clock::now();
        require(now - started < std::chrono::seconds(30), "Concurrent animation fixture timed out");
        require(metric(14) == peers && editors(host) == original_editors, "Concurrent motion retains all native identities and peers");
        if (leg >= 2 && leg < 6) {
            peak_buffers = std::max(peak_buffers, metric(30) + metric(31));
            peak_gdi = std::max(peak_gdi, GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS));
            middle |= std::any_of(reveals.begin(), reveals.end(),
                [](const auto& item) { return item->progress() > 0 && item->progress() < 1; });
            if (motion && leg == 2 && !reversed && now - leg_start >= std::chrono::milliseconds(120) && active()) {
                target(true);
                reversed = true;
            }
        }
        if (active()) {
            require(metric(33) != 0, "Active controls share the window clock");
            return;
        }
        require(metric(33) == 0, "All settled controls stop the shared clock");
        if (leg == 0) { ++leg; target(true); return; }
        if (leg == 1) {
            ++leg;
            start_paints = metric(0); start_layouts = metric(2);
            start_cpu = cpu_milliseconds(); measurement_start = now;
            start_gdi = peak_gdi = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
            target(false);
            return;
        }
        if (leg < 5) { ++leg; target(!reveals.front()->open()); return; }
        if (leg == 5) {
            require(!motion || middle, "Concurrent controls expose intermediate presentation");
            const auto layouts = metric(2) - start_layouts;
            const auto elapsed = std::chrono::duration<double, std::milli>(now - measurement_start).count();
            std::cout << "count=" << reveals.size() << " layout=" << (expanding ? "expand" : "fixed")
                << " system_motion=" << motion << " elapsed_ms=" << elapsed
                << " cpu_ms=" << cpu_milliseconds() - start_cpu << " paints=" << metric(0) - start_paints
                << " layouts=" << layouts << " target_layouts=" << target_layouts
                << " settlement_layouts=" << settlement_layouts << " frame_layouts=" << frame_layouts
                << " peers=" << peers << " peak_native_bytes=" << peak_buffers
                << " gdi_start=" << start_gdi << " gdi_peak=" << peak_gdi << " reversal=" << reversed << '\n';
            require(layouts == target_layouts + settlement_layouts + frame_layouts, "Root measurements account for each recorded layout");
            require(expanding || frame_layouts == 0, "Fixed Reveal and Progress intermediate frames do not perform root layout");
            require(!expanding || !motion || frame_layouts > 0, "Expanding Reveal performs coordinated frame layout");
            ++leg;
            return;
        }
        if (leg == 6) {
            // Give native visibility messages one pump turn after the final clip changes.
            ++leg; idle_start = now; idle_paints = metric(0); idle_layouts = metric(2);
            return;
        }
        if (now - idle_start < std::chrono::milliseconds(250)) return;
        if (metric(0) != idle_paints || metric(2) != idle_layouts || metric(33) != 0)
            std::cerr << "Idle deltas: paints=" << metric(0) - idle_paints
                << " layouts=" << metric(2) - idle_layouts << " animation_timer=" << metric(33) << '\n';
        require(metric(0) == idle_paints && metric(2) == idle_layouts && metric(33) == 0,
            "Settled concurrent controls produce no idle paints, layouts, or animation wakeups");
        target(true);
        ShowWindow(host, SW_HIDE);
        flush();
        require(!active() && metric(33) == 0, "Hiding the host settles the whole animation set");
        KillTimer(host, driver);
        done = true;
        window.close();
    }
    static Fixture* current;
    static void CALLBACK tick(HWND, UINT, UINT_PTR, DWORD) noexcept {
        if (!current || current->done) return;
        try { current->step(); }
        catch (...) {
            current->error = std::current_exception();
            KillTimer(current->host, driver);
            current->window.close();
        }
    }
};
Fixture* Fixture::current{};
}
int main() {
    try {
        std::cout << std::unitbuf;
        for (const bool expand : {false, true}) {
            for (const std::size_t count : {1, 16, 64}) {
                Fixture fixture(count, expand);
                Fixture::current = &fixture;
                fixture.window.post([&] { fixture.begin(); });
                const auto result = Application::run(fixture.window);
                Fixture::current = nullptr;
                if (fixture.error) std::rethrow_exception(fixture.error);
                if (result) std::wcerr << fixture.window.error() << '\n';
                require(result == 0 && fixture.done, "Concurrent animation acceptance completed");
            }
        }
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
