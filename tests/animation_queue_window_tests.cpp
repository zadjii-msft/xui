#include "xui/application.hpp"
#include "xui/foundation.hpp"
#include "xui/reveal.hpp"
#include <windows.h>
#include <commctrl.h>
#include <chrono>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {
using namespace xui;
using Clock = std::chrono::steady_clock;
constexpr UINT flood_message = WM_APP + 74, metrics = WM_APP + 60;
constexpr UINT_PTR idle_timer = 99;
constexpr UINT_PTR quit_guard_timer = 100;
constexpr UINT_PTR child_timer = 101;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
struct Fixture {
    Window& window;
    std::shared_ptr<Button> anchor = std::make_shared<Button>(L"Stationary focus");
    std::shared_ptr<Reveal> reveal = std::make_shared<Reveal>(std::make_shared<TextInput>(L"Retained input"));
    std::shared_ptr<Progress> progress = std::make_shared<Progress>(L"Finite progress");
    HWND host{};
    HWND anchor_peer{};
    Clock::time_point started{}, idle_started{};
    LRESULT start_ticks{}, last_paints{}, idle_paints{}, idle_layouts{};
    std::size_t delivered{};
    std::size_t child_ticks{};
    bool motion{}, middle{}, painted_middle{}, idle_baseline{}, done{};
    bool retire_early{};
    bool quit_on_frame{}, quit_requested{};
    unsigned duration{};
    std::exception_ptr error;
    explicit Fixture(Window& target, unsigned milliseconds = 600, bool retire = false, bool quit = false)
        : window(target), retire_early(retire), quit_on_frame(quit), duration(milliseconds) {
        auto root = std::make_shared<Stack>(Axis::vertical);
        root->add(anchor);
        reveal->set_open(true);
        reveal->set_duration(duration);
        root->add(reveal);
        progress->set_duration(duration);
        root->add(progress);
        window.set_content(root);
    }
    LRESULT metric(WPARAM id) const { return SendMessageW(host, metrics, id, 0); }
    void begin() {
        require(window.focus(*anchor), "Focus the queue fixture");
        anchor_peer = GetFocus();
        host = GetAncestor(anchor_peer, GA_ROOT);
        BOOL enabled{};
        require(SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0) != FALSE, "Read motion policy");
        motion = enabled != FALSE && duration != 0;
        require(SetWindowSubclass(host, dispatch, 1, reinterpret_cast<DWORD_PTR>(this)) != FALSE, "Install the owned queue driver");
        SendMessageW(host, WM_APP + 12, 0, 0);
        UpdateWindow(host);
        reveal->set_open(false);
        progress->set_value(80);
        SendMessageW(host, WM_APP + 12, 0, 0);
        started = Clock::now();
        start_ticks = metric(34);
        last_paints = metric(0);
        if (quit_on_frame) {
            require(SetTimer(host, quit_guard_timer, 2000, nullptr) != 0, "Start bounded quit observation");
            if (!motion) { quit_requested = true; PostQuitMessage(37); }
            return;
        }
        if (motion) {
            require(SetWindowSubclass(anchor_peer, child_dispatch, 1, reinterpret_cast<DWORD_PTR>(this)) != FALSE,
                "Install the owned child-timer observer");
            require(SetTimer(anchor_peer, child_timer, 16, nullptr) != 0, "Start the owned child timer");
        }
        require(PostMessageW(host, flood_message, 0, 0) != FALSE, "Start bounded posted-message traffic");
    }
    void flood() {
        ++delivered;
        if (retire_early && Clock::now() - started >= std::chrono::milliseconds(100)) {
            done = true;
            window.close();
            return;
        }
        const bool intermediate = reveal->progress() > 0 && reveal->progress() < 1;
        const auto paints = metric(0);
        middle |= intermediate;
        painted_middle |= intermediate && paints > last_paints;
        last_paints = paints;
        if (Clock::now() - started < std::chrono::milliseconds(1500)) {
            anchor->set_name(delivered % 2 ? L"Stationary focus" : L"Active updates");
            require(PostMessageW(host, flood_message, 0, 0) != FALSE, "Continue bounded posted-message traffic");
            return;
        }
        std::cout << "queued_messages=" << delivered << " ticks=" << metric(34) - start_ticks
            << " middle=" << middle << " painted_middle=" << painted_middle << " child_ticks=" << child_ticks << '\n';
        require(delivered > 1, "The application processed posted traffic");
        require(!motion || (middle && painted_middle && metric(34) > start_ticks),
            "Posted traffic must not starve real animation timers and intermediate painting");
        require(motion || metric(34) == start_ticks, "Immediate mode has no animation timer work during posted traffic");
        require(!motion || child_ticks > 0, "Animation frame service must dispatch retrieved native child timers");
        KillTimer(anchor_peer, child_timer);
        require(!reveal->animating() && !progress->animating() && metric(33) == 0,
            "Animation completes and stops its clock while posted traffic continues");
        require(window.focus(*anchor), "Queue service preserves usable native input");
        require(SetTimer(host, idle_timer, 50, nullptr) != 0, "Start bounded idle observation");
    }
    void idle() {
        if (!idle_baseline) {
            idle_baseline = true;
            idle_started = Clock::now();
            idle_paints = metric(0);
            idle_layouts = metric(2);
            return;
        }
        if (Clock::now() - idle_started < std::chrono::milliseconds(200)) return;
        require(metric(33) == 0 && metric(0) == idle_paints && metric(2) == idle_layouts,
            "Queue fairness adds no idle animation, paint, or layout work");
        KillTimer(host, idle_timer);
        done = true;
        window.close();
    }
    static LRESULT CALLBACK child_dispatch(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam,
        UINT_PTR, DWORD_PTR data) noexcept {
        auto& self = *reinterpret_cast<Fixture*>(data);
        if (message == WM_TIMER && wparam == child_timer) { ++self.child_ticks; return 0; }
        if (message == WM_NCDESTROY) RemoveWindowSubclass(hwnd, child_dispatch, 1);
        return DefSubclassProc(hwnd, message, wparam, lparam);
    }
    static LRESULT CALLBACK dispatch(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam,
        UINT_PTR, DWORD_PTR data) noexcept {
        auto& self = *reinterpret_cast<Fixture*>(data);
        try {
            if (message == WM_TIMER && wparam == quit_guard_timer)
                throw std::runtime_error("Animation presentation must preserve the queued quit message");
            if (message == WM_TIMER && wparam == 43 && self.quit_on_frame && !self.quit_requested) {
                const auto result = DefSubclassProc(hwnd, message, wparam, lparam);
                MSG update{};
                while (PeekMessageW(&update, hwnd, WM_APP + 12, WM_APP + 12, PM_REMOVE))
                    DispatchMessageW(&update);
                self.quit_requested = true;
                PostQuitMessage(37);
                return result;
            }
            if (message == flood_message) { self.flood(); return 0; }
            if (message == WM_TIMER && wparam == idle_timer) { self.idle(); return 0; }
        } catch (...) {
            self.error = std::current_exception();
            KillTimer(hwnd, idle_timer);
            self.window.close();
            return 0;
        }
        if (message == WM_NCDESTROY) RemoveWindowSubclass(hwnd, dispatch, 1);
        return DefSubclassProc(hwnd, message, wparam, lparam);
    }
};
}
int main() {
    try {
        for (const unsigned duration : {600u, 0u}) {
            for (const bool managed : {false, true}) {
                std::unique_ptr<Application> app;
                if (managed) app = std::make_unique<Application>();
                const WindowOptions options{L"XUI animation queue fairness", {640, 420}};
                auto window = app ? app->create_window(options) : std::make_shared<Window>(options);
                Fixture fixture(*window, duration);
                fixture.window.post([&] { fixture.begin(); });
                if (app) app->show(fixture.window);
                const auto result = app ? app->run() : Application::run(fixture.window);
                if (fixture.error) std::rethrow_exception(fixture.error);
                require(result == 0 && fixture.done, "Queue fairness completes through both application entry points");
            }
        }
        for (const bool managed : {false, true}) {
            std::unique_ptr<Application> app;
            if (managed) app = std::make_unique<Application>();
            const WindowOptions options{L"XUI animation queued quit", {640, 420}};
            auto window = app ? app->create_window(options) : std::make_shared<Window>(options);
            Fixture fixture(*window, 600, false, true);
            window->post([&] { fixture.begin(); });
            if (app) app->show(*window);
            const auto result = app ? app->run() : Application::run(*window);
            if (fixture.error) std::rethrow_exception(fixture.error);
            std::cout << "quit_managed=" << managed << " result=" << result
                << " requested=" << fixture.quit_requested << " live_host=" << IsWindow(fixture.host) << '\n';
            require(result == 37 && fixture.quit_requested && !IsWindow(fixture.host),
                "Both animation entry points preserve the quit code and retire native peers");
            // The single-window host posts its normal quit while explicit-quit teardown destroys it.
            MSG teardown_quit{};
            if (!managed && PeekMessageW(&teardown_quit, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE))
                require(teardown_quit.wParam == 0, "The retired fixture owns only its normal teardown quit");
        }
        Application app;
        auto retiring = app.create_window({L"Retiring animation queue", {640, 420}});
        auto surviving = app.create_window({L"Surviving animation queue", {640, 420}});
        Fixture first(*retiring, 600, true), second(*surviving);
        retiring->post([&] { first.begin(); });
        surviving->post([&] { second.begin(); });
        app.show(*retiring);
        app.show(*surviving);
        const auto result = app.run();
        if (first.error) std::rethrow_exception(first.error);
        if (second.error) std::rethrow_exception(second.error);
        require(result == 0 && first.done && second.done && !IsWindow(first.host) && !IsWindow(second.host),
            "Retiring one queued owner leaves the other animation window independent");
        require(!first.reveal->animating() && !first.progress->animating(),
            "Retired animation participants remain settled after peer destruction");
        std::cout << "Animation queue fairness and idle checks passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
