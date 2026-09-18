#include "xui/application.hpp"
#include "owned_window_capture.hpp"
#include <UIAutomation.h>
#include <commctrl.h>
#include <atomic>
#include <cmath>
#include <exception>
#include <iostream>
#include <thread>

namespace {
using namespace xui;
using namespace std::chrono_literals;
constexpr UINT update = WM_APP + 12, metrics = WM_APP + 60;
constexpr UINT_PTR driver_timer = 199;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
HWND named_peer(HWND host, const wchar_t* name) {
    struct Search { const wchar_t* name; HWND result{}; } search{name};
    EnumChildWindows(host, [](HWND hwnd, LPARAM context) -> BOOL {
        auto& state = *reinterpret_cast<Search*>(context);
        wchar_t text[128]{}; GetWindowTextW(hwnd, text, 128);
        if (std::wstring_view(text) == state.name) { state.result = hwnd; return FALSE; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    require(search.result != nullptr, "Find the retained Progress native peer");
    return search.result;
}
void logical_uia(HWND host, HWND peer, double expected) {
    std::atomic<bool> done{};
    std::exception_ptr failure;
    std::thread client([&] {
        const auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        try {
            winrt::check_hresult(initialized);
            using Microsoft::WRL::ComPtr;
            ComPtr<IUIAutomation> automation;
            winrt::check_hresult(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation)));
            ComPtr<IUIAutomationElement> element;
            winrt::check_hresult(automation->ElementFromHandle(peer, &element));
            CONTROLTYPEID type{};
            winrt::check_hresult(element->get_CurrentControlType(&type));
            require(type == UIA_ProgressBarControlTypeId, "Interpolation retains the ProgressBar UIA role");
            ComPtr<IUIAutomationRangeValuePattern> range;
            winrt::check_hresult(element->GetCurrentPatternAs(UIA_RangeValuePatternId, IID_PPV_ARGS(&range)));
            double value{}, minimum{}, maximum{};
            BOOL read_only{};
            winrt::check_hresult(range->get_CurrentValue(&value));
            winrt::check_hresult(range->get_CurrentMinimum(&minimum));
            winrt::check_hresult(range->get_CurrentMaximum(&maximum));
            winrt::check_hresult(range->get_CurrentIsReadOnly(&read_only));
            require(value == expected && minimum == 0 && maximum == 100 && read_only,
                "UIA exposes the immediate logical value and read-only range, not an interpolated value");
        } catch (...) { failure = std::current_exception(); }
        if (SUCCEEDED(initialized)) CoUninitialize();
        done = true;
        PostMessageW(host, WM_NULL, 0, 0);
    });
    while (!done) {
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 50, QS_ALLINPUT);
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message); DispatchMessageW(&message);
        }
    }
    client.join();
    if (failure) std::rethrow_exception(failure);
}
struct Fixture {
    Window window;
    std::shared_ptr<Stack> root = std::make_shared<Stack>(Axis::vertical);
    std::shared_ptr<Progress> progress = std::make_shared<Progress>(L"Animated work");
    std::shared_ptr<ContentHost> content = std::make_shared<ContentHost>(progress);
    std::shared_ptr<Button> outside = std::make_shared<Button>(L"Outside progress");
    HWND host{}, peer{}, focus{};
    bool motion{}, watching{}, middle{}, frame_layout{}, done{}, stepping{};
    unsigned phase{}, frames{};
    LRESULT entry_layouts{}, idle_layouts{}, idle_paints{};
    Animation::Clock::time_point started{}, idle_since{};
    std::exception_ptr error;
    static Fixture* current;

    explicit Fixture(VisualStyle style)
        : window{{L"XUI animated Progress acceptance", {480, 240}, ThemeMode::light, {}, false, style}} {
        root->set_padding({12, 12, 12, 12});
        progress->set_value(20);
        progress->set_duration(2000);
        PartStyleValues track; track.background = ThemeColor{0x182838}; track.thickness = 12.0f; track.corner_radius = 0.0f;
        PartStyleValues fill; fill.background = ThemeColor{0x12B456}; fill.corner_radius = 0.0f;
        progress->set_control_style(ControlStyle::create(StyleTarget::progress,
            {{StylePart::track, track}, {StylePart::fill, fill}}, {}));
        content->set_preferred_size({320, 42});
        root->add(content);
        root->add(outside, 1);
        window.set_content(root);
    }
    ~Fixture() { if (IsWindow(host)) RemoveWindowSubclass(host, observe, 1); }
    void flush() { SendMessageW(host, update, 0, 0); UpdateWindow(host); }
    static LRESULT CALLBACK observe(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR, DWORD_PTR context) noexcept {
        auto& self = *reinterpret_cast<Fixture*>(context);
        const auto result = DefSubclassProc(hwnd, message, wparam, lparam);
        // Observe the actual shared timer before a slower independent driver can miss its frames.
        if (message == WM_TIMER && self.watching && !self.done) {
            const auto value = self.progress->presented_value();
            if (self.progress->animating() && value > 20 && value < 80) {
                self.middle = true;
                ++self.frames;
                self.frame_layout = self.frame_layout || SendMessageW(hwnd, metrics, 2, 0) != self.entry_layouts;
            }
        }
        return result;
    }
    void begin() {
        require(window.focus(*outside), "The owned progress fixture receives native focus");
        focus = GetFocus();
        host = GetAncestor(focus, GA_ROOT);
        peer = named_peer(host, L"Animated work");
        BOOL enabled{};
        require(SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0) != FALSE,
            "Read system motion preference without changing it");
        motion = enabled != FALSE;
        require(!progress->animating() && SendMessageW(host, metrics, 33, 0) == 0,
            "Initial progress creates no animation timer");
        require(SetWindowSubclass(host, observe, 1, reinterpret_cast<DWORD_PTR>(this)) != FALSE,
            "Observe only the owned window's timer dispatch");
        entry_layouts = SendMessageW(host, metrics, 2, 0);
        watching = true;
        started = Animation::Clock::now();
        progress->set_value(80);
        require(progress->value() == 80 && progress->presented_value() == 20,
            "Logical value changes before the first presented frame");
        flush();
        require(progress->animating() == motion && (SendMessageW(host, metrics, 33, 0) != 0) == motion,
            "The shared timer respects the system reduced-motion setting");
        require(SetTimer(host, driver_timer, 15, tick) != 0, "Start the bounded progress acceptance driver");
    }
    void pixels_and_uia() {
        progress->set_duration(0); progress->set_value(20);
        progress->set_duration(10000); progress->set_value(80); flush();
        const auto layouts = SendMessageW(host, metrics, 2, 0);
        if (motion) progress->advance(Animation::Clock::now() + 2500ms);
        flush();
        if (motion) require(progress->presented_value() > 20 && progress->presented_value() < 80 && progress->animating(),
            "The pixel fixture retains an actual intermediate value");
        logical_uia(host, peer, 80);
        flush();
        require(SendMessageW(host, metrics, 2, 0) == layouts, "Presented value and UIA sampling require no root layout");
        require(RedrawWindow(host, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW) != FALSE,
            "Paint the owned progress frame");
        winrt::check_hresult(DwmFlush());
        HIGHCONTRASTW contrast{sizeof(contrast)};
        require(SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) != FALSE,
            "Read high contrast without changing it");
        if (!(contrast.dwFlags & HCF_HIGHCONTRASTON)) {
            const auto fraction = progress->presented_fraction();
            if (motion) require(fraction > 0.2 && fraction < 0.8 && progress->animating(),
                "Intermediate pixels must not be replaced by a completed frame");
            const auto bounds = progress->content_bounds();
            const float track_x = bounds.x + 2, track_width = bounds.width - 4;
            const float track_y = bounds.y + std::max(2.0f, bounds.height - 14) + 6;
            POINT origin{};
            MapWindowPoints(peer, host, &origin, 1);
            const float scale = GetDpiForWindow(host) / 96.0f;
            const auto pixels = owned_window_capture::capture(host);
            const auto pixel = [&](double portion) {
                const auto x = origin.x + static_cast<int>(std::lround((track_x + track_width * portion) * scale));
                const auto y = origin.y + static_cast<int>(std::lround(track_y * scale));
                require(x >= 0 && x < pixels.width && y >= 0 && y < pixels.height, "Progress pixels remain inside the owned capture");
                return pixels.data[static_cast<std::size_t>(y) * pixels.width + x] & 0xffffff;
            };
            require(pixel(fraction / 2) == 0x12B456, "The presented portion paints the authored fill");
            require(pixel(motion ? (fraction + 0.8) / 2 : 0.9) == 0x182838,
                "Pixels after the presented edge remain track, rather than showing the logical target early");
        } else std::cout << "High contrast: custom-color pixels omitted; timer, identity, and UIA still checked\n";
        progress->set_value(40);
        const auto displayed = progress->presented_value();
        progress->set_value(90);
        require(progress->presented_value() == displayed && progress->value() == 90, "A rapid retarget has no presentation jump");
        flush();
        const auto prior_width = progress->bounds().width;
        const auto prior_value = progress->presented_value();
        RECT outer{};
        require(GetWindowRect(host, &outer) != FALSE, "Read the owned window before resizing");
        require(SetWindowPos(host, nullptr, 0, 0, outer.right - outer.left + 100, outer.bottom - outer.top,
            SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE) != FALSE, "Resize only the owned progress window");
        flush();
        require(progress->bounds().width != prior_width && progress->presented_value() == prior_value,
            "Resize changes track geometry without changing the presented value");
        require(named_peer(host, L"Animated work") == peer && GetFocus() == focus,
            "Interpolation and resize preserve native progress identity and unrelated focus");
        progress->set_visible(false); flush();
        require(!progress->animating() && progress->presented_value() == 90 && SendMessageW(host, metrics, 33, 0) == 0,
            "Hidden progress settles its value and stops the shared timer");
        progress->set_visible(true); flush();
        progress->set_value(20); flush();
        ShowWindow(host, SW_HIDE); flush();
        require(!progress->animating() && progress->presented_value() == 20 && SendMessageW(host, metrics, 33, 0) == 0,
            "Hiding the owner settles retained progress");
        ShowWindow(host, SW_SHOWNOACTIVATE); flush();
        require(window.focus(*outside), "Retirement retains focus outside progress");
        focus = GetFocus();
        progress->set_value(80); flush();
        window.replace_content(*content, {}); flush();
        require(!IsWindow(peer) && !progress->animating() && progress->presented_value() == 80 &&
            SendMessageW(host, metrics, 33, 0) == 0 && GetFocus() == focus,
            "Retirement settles the retained model, releases its peer and timer, and preserves outside focus");
    }
    void step() {
        require(Animation::Clock::now() - started < 30s, "Progress acceptance exceeded its deadline");
        if (phase == 0) {
            require(progress->value() == 80 && GetFocus() == focus && named_peer(host, L"Animated work") == peer,
                "Timer delivery preserves logical state, native identity, and focus");
            if (progress->animating()) return;
            watching = false;
            if (motion && !middle) std::cerr << "No intermediate Progress timer frame; duration=2000ms, observed=" << frames << '\n';
            require(!motion || middle, "Actual timer delivery produces an intermediate Progress frame");
            require(!frame_layout && SendMessageW(host, metrics, 2, 0) == entry_layouts,
                "Progress timer frames perform zero root layouts");
            flush();
            require(progress->presented_value() == 80 && SendMessageW(host, metrics, 33, 0) == 0,
                "Completion settles exactly and stops the shared timer");
            idle_layouts = SendMessageW(host, metrics, 2, 0);
            idle_paints = SendMessageW(host, metrics, 0, 0);
            idle_since = Animation::Clock::now();
            phase = 1;
        } else {
            if (Animation::Clock::now() - idle_since < 150ms) return;
            require(SendMessageW(host, metrics, 2, 0) == idle_layouts && SendMessageW(host, metrics, 0, 0) == idle_paints,
                "Settled progress has no periodic layout or paint");
            pixels_and_uia();
            done = true;
            KillTimer(host, driver_timer);
            RemoveWindowSubclass(host, observe, 1);
            window.close();
        }
    }
    static void CALLBACK tick(HWND hwnd, UINT, UINT_PTR id, DWORD) noexcept {
        if (!current || current->done || current->stepping || current->host != hwnd || id != driver_timer) return;
        current->stepping = true;
        try { current->step(); }
        catch (...) {
            current->error = std::current_exception();
            current->done = true;
            KillTimer(hwnd, driver_timer);
            RemoveWindowSubclass(hwnd, observe, 1);
            current->window.close();
        }
        current->stepping = false;
    }
};
Fixture* Fixture::current{};
}
int main() {
    try {
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        struct Apartment { ~Apartment() { winrt::clear_factory_cache(); winrt::uninit_apartment(); } } apartment;
        for (auto style : {VisualStyle::classic, VisualStyle::winui}) {
            Fixture fixture(style);
            Fixture::current = &fixture;
            fixture.window.post([&] { fixture.begin(); });
            const auto result = Application::run(fixture.window);
            Fixture::current = nullptr;
            if (fixture.error) std::rethrow_exception(fixture.error);
            if (result) std::wcerr << fixture.window.error() << L'\n';
            require(result == 0 && fixture.done, "Progress desktop acceptance completed");
        }
        std::cout << "Progress timer, pixels, logical UIA, lifecycle, and idle contracts passed.\n";
    } catch (const std::exception& error) {
        Fixture::current = nullptr;
        std::cerr << error.what() << '\n';
        return 1;
    }
}
