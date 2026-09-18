#include "xui/application.hpp"
#include "owned_window_capture.hpp"
#include <UIAutomation.h>
#include <atomic>
#include <cmath>
#include <exception>
#include <functional>
#include <iostream>
#include <thread>

namespace {
using namespace xui;
constexpr UINT update = WM_APP + 12, metrics = WM_APP + 60;
constexpr UINT_PTR driver_timer = 198;
constexpr unsigned entry_duration = 2000;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
struct ObservedBody final : Stack {
    ObservedBody() : Stack(Axis::vertical) {}
    std::function<void()> arranged;
    void arrange(Rect bounds) override {
        Stack::arrange(bounds);
        if (arranged) arranged();
    }
};
HWND named_peer(HWND host, const wchar_t* name) {
    struct Search { const wchar_t* name; HWND result{}; } search{name};
    EnumChildWindows(host, [](HWND hwnd, LPARAM context) -> BOOL {
        auto& state = *reinterpret_cast<Search*>(context);
        wchar_t text[128]{}; GetWindowTextW(hwnd, text, 128);
        if (std::wstring_view(text) == state.name) { state.result = hwnd; return FALSE; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    require(search.result != nullptr, "Find the original Expander native peer");
    return search.result;
}
void uia_collapsed(HWND host, HWND expander, HWND editor) {
    std::atomic<bool> done{};
    std::exception_ptr failure;
    std::thread client([&] {
        const auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        try {
            winrt::check_hresult(initialized);
            using Microsoft::WRL::ComPtr;
            ComPtr<IUIAutomation> automation;
            winrt::check_hresult(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation)));
            ComPtr<IUIAutomationElement> root;
            winrt::check_hresult(automation->ElementFromHandle(expander, &root));
            ComPtr<IUIAutomationExpandCollapsePattern> disclosure;
            winrt::check_hresult(root->GetCurrentPatternAs(UIA_ExpandCollapsePatternId, IID_PPV_ARGS(&disclosure)));
            ExpandCollapseState state{};
            winrt::check_hresult(disclosure->get_CurrentExpandCollapseState(&state));
            require(state == ExpandCollapseState_Collapsed, "UIA reports logical collapse while the body exits");
            ComPtr<IUIAutomationElement> input;
            winrt::check_hresult(automation->ElementFromHandle(editor, &input));
            BOOL enabled{};
            winrt::check_hresult(input->get_CurrentIsEnabled(&enabled));
            require(!enabled, "Outgoing native input is disabled in UIA");
            ComPtr<IUIAutomationTreeWalker> walker;
            winrt::check_hresult(automation->get_RawViewWalker(&walker));
            ComPtr<IUIAutomationElement> parent;
            winrt::check_hresult(walker->GetParentElement(input.Get(), &parent));
            require(parent != nullptr, "The outgoing editor remains in the UIA hierarchy");
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
    std::shared_ptr<ObservedBody> body = std::make_shared<ObservedBody>();
    std::shared_ptr<TextInput> input = std::make_shared<TextInput>(L"Body input");
    std::shared_ptr<Expander> expander = std::make_shared<Expander>(L"Animated details", body);
    std::shared_ptr<Button> files = std::make_shared<Button>(L"Neighbor");
    HWND host{}, header{}, editor{};
    bool motion{}, middle{}, done{}, stepping{}, watching_entry{};
    unsigned phase{}, style_variant{}, entry_frames{};
    float full_height{};
    double entry_setup_ms{}, first_tick_ms{-1};
    LRESULT idle_layouts{}, idle_paints{};
    Animation::Clock::time_point started{}, idle_since{};
    std::exception_ptr error;
    static Fixture* current;

    explicit Fixture(unsigned variant)
        : window{{L"XUI animated Expander acceptance", {500, 400}, ThemeMode::light, {}, false,
            variant == 1 ? VisualStyle::winui : VisualStyle::classic}} {
        style_variant = variant;
        // Backend layout can present frames before the separate driver timer is dispatched.
        body->arranged = [this] {
            if (watching_entry && expander->animating() && expander->progress() > 0 && expander->progress() < 1) {
                middle = true;
                ++entry_frames;
            }
        };
        root->set_padding({12, 12, 12, 12});
        body->set_preferred_size({320, 120});
        body->set_padding({12, 12, 12, 12});
        PartStyleValues background; background.background = ThemeColor{0x12B456};
        body->set_control_style_values(StylePart::root, background);
        input->set_caption_visible(false);
        input->set_preferred_size({240, 44});
        body->add(input);
        if (variant == 2) {
            PartStyleValues face; face.height = 54.0f;
            PartStyleValues content; content.padding = Insets{8, 8, 8, 8};
            expander->set_control_style(ControlStyle::create(StyleTarget::expander,
                {{StylePart::header, face}, {StylePart::content, content}}, {}));
        }
        expander->set_expanded(false);
        expander->set_duration(entry_duration);
        root->add(expander);
        root->add(files, 1);
        window.set_content(root);
    }
    void flush() { SendMessageW(host, update, 0, 0); UpdateWindow(host); }
    void geometry() {
        require(expander->content() == body && expander->retained_children().size() == 1,
            "The original Expander owns its original body without another host");
        require(GetParent(editor) == header && named_peer(host, L"Animated details") == header,
            "The editor keeps its original native Expander parent");
        require(std::abs(body->bounds().height - full_height) < 0.01f && input->bounds().height == 44,
            "The retained body and native editor do not shrink during motion");
        require(std::abs(expander->bounds().y + expander->bounds().height - files->bounds().y) < 0.01f,
            "The neighbor and animated body share their moving edge");
    }
    void begin() {
        require(window.focus(*files), "The neighbor receives initial native focus");
        host = GetAncestor(GetFocus(), GA_ROOT);
        header = named_peer(host, L"Animated details");
        BOOL enabled{};
        require(SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0) != FALSE,
            "Read the system motion preference without changing it");
        motion = enabled != FALSE;
        const auto neighbor_height = files->bounds().height;
        started = Animation::Clock::now();
        watching_entry = true;
        expander->set_expanded(true);
        require(window.focus(*input), "Opening input receives focus before its body clip grows");
        editor = GetFocus();
        wchar_t kind[32]{}; GetClassNameW(editor, kind, 32);
        require(_wcsicmp(kind, L"EDIT") == 0, "Expander body remains native text input");
        SendMessageW(editor, WM_CHAR, L'e', 0);
        require(input->text() == L"e", "The first character survives zero-clip entry");
        flush();
        full_height = body->bounds().height;
        require(expander->animating() == motion && (SendMessageW(host, metrics, 33, 0) != 0) == motion,
            "Expander uses the shared timer and system reduced-motion policy");
        if (motion) require(files->bounds().height == neighbor_height, "Neighbors do not snap at entry");
        geometry();
        entry_setup_ms = std::chrono::duration<double, std::milli>(Animation::Clock::now() - started).count();
        require(SetTimer(host, driver_timer, 15, tick) != 0, "Start the bounded Expander driver");
    }
    void outgoing_pixels() {
        expander->set_duration(10000);
        expander->set_expanded(false);
        flush();
        require(GetFocus() == header && !IsWindowEnabled(editor) && !window.focus(*input),
            "Collapse repairs native focus to the original header and immediately disables descendants");
        if (motion) {
            expander->advance(Animation::Clock::now() + std::chrono::milliseconds(2000));
            flush();
            require(expander->animating() && expander->body_presented(), "Outgoing body stays presented");
            geometry();
            require(RedrawWindow(host, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW) != FALSE,
                "Paint the owned outgoing Expander frame");
            winrt::check_hresult(DwmFlush());
            HIGHCONTRASTW contrast{sizeof(contrast)};
            require(SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) != FALSE,
                "Read high contrast without changing it");
            if (!(contrast.dwFlags & HCF_HIGHCONTRASTON)) {
                const auto pixels = owned_window_capture::capture(host);
                const float scale = GetDpiForWindow(host) / 96.0f;
                const auto pixel = [&](float x, float y) {
                    const auto column = static_cast<int>(std::lround(x * scale));
                    const auto row = static_cast<int>(std::lround(y * scale));
                    require(column >= 0 && column < pixels.width && row >= 0 && row < pixels.height,
                        "Expander pixel samples stay inside the owned window");
                    return pixels.data[static_cast<std::size_t>(row) * pixels.width + column] & 0xffffff;
                };
                const auto clip = expander->content_bounds();
                require(pixel(clip.x + 4, clip.y + 4) == 0x12B456, "Outgoing body pixels remain visible after logical collapse");
                const auto surface = expander->content_surface_bounds();
                require(pixel(clip.x + 4, surface.y + surface.height + 4) != 0x12B456,
                    "Retained body pixels cannot escape the animated clip");
            }
        }
        uia_collapsed(host, header, editor);
        expander->set_expanded(true);
        require(window.focus(*input) && GetFocus() == editor, "Reversal retains the native editor identity");
        DWORD first{}, last{};
        SendMessageW(editor, EM_GETSEL, reinterpret_cast<WPARAM>(&first), reinterpret_cast<LPARAM>(&last));
        require(input->text() == L"e" && first == 1 && last == 1 && SendMessageW(editor, EM_CANUNDO, 0, 0),
            "Collapse and reversal preserve native text, selection, and undo");
        expander->set_duration(0);
        expander->set_duration(240);
        expander->set_expanded(false);
        flush();
    }
    void step() {
        require(Animation::Clock::now() - started < std::chrono::seconds(20), "Expander acceptance exceeded its deadline");
        if (phase == 0) {
            if (first_tick_ms < 0)
                first_tick_ms = std::chrono::duration<double, std::milli>(Animation::Clock::now() - started).count();
            geometry();
            require(GetFocus() == editor && input->text() == L"e", "Entry retains native focus and text");
            if (expander->animating()) return;
            watching_entry = false;
            if (motion && !middle)
                std::cerr << "Expander entry variant=" << style_variant << " duration_ms=" << entry_duration
                    << " setup_ms=" << entry_setup_ms << " first_driver_ms=" << first_tick_ms
                    << " completion_ms=" << std::chrono::duration<double, std::milli>(Animation::Clock::now() - started).count()
                    << " progress=" << expander->progress() << " intermediate_layouts=" << entry_frames << '\n';
            require(!motion || middle, "Actual timer delivery produces an intermediate body frame");
            require(SendMessageW(host, metrics, 33, 0) == 0, "Completed entry stops its timer");
            outgoing_pixels();
            phase = 1;
        } else if (phase == 1) {
            if (expander->animating()) return;
            flush();
            require(!expander->body_presented() && body->bounds().height == 0 && SendMessageW(host, metrics, 33, 0) == 0,
                "Completed exit releases body geometry and stops the timer");
            idle_layouts = SendMessageW(host, metrics, 2, 0);
            idle_paints = SendMessageW(host, metrics, 0, 0);
            idle_since = Animation::Clock::now();
            phase = 2;
        } else {
            if (Animation::Clock::now() - idle_since < std::chrono::milliseconds(150)) return;
            require(SendMessageW(host, metrics, 2, 0) == idle_layouts && SendMessageW(host, metrics, 0, 0) == idle_paints,
                "A settled Expander has no periodic layout or paint");
            expander->set_expanded(true); flush();
            ShowWindow(host, SW_HIDE); flush();
            require(!expander->animating() && SendMessageW(host, metrics, 33, 0) == 0,
                "Hiding the owner settles the retained Expander and timer");
            done = true;
            KillTimer(host, driver_timer);
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
            current->window.close();
        }
        current->stepping = false;
    }
};
Fixture* Fixture::current{};
void retirement() {
    Window window{{L"Expander retirement", {500, 400}}};
    auto root = std::make_shared<Stack>(Axis::vertical);
    auto outside = std::make_shared<Button>(L"Outside");
    root->add(outside);
    auto input = std::make_shared<TextInput>(L"Retained input");
    auto expander = std::make_shared<Expander>(L"Retained details", input);
    expander->set_duration(10000);
    auto content = std::make_shared<ContentHost>(expander);
    root->add(content, 1);
    window.set_content(root);
    bool complete{};
    window.post([&] {
        require(window.focus(*outside), "Retirement fixture receives native focus");
        const auto focused = GetFocus(), host = GetAncestor(focused, GA_ROOT);
        const auto header = named_peer(host, L"Retained details");
        expander->set_expanded(false);
        SendMessageW(host, update, 0, 0);
        window.replace_content(*content, {});
        SendMessageW(host, update, 0, 0);
        require(!expander->animating() && !IsWindow(header) && GetFocus() == focused,
            "Retirement settles the retained model, removes its peer, and preserves outside focus");
        require(SendMessageW(host, metrics, 33, 0) == 0 && expander->content() == input,
            "Retirement stops the timer without changing body ownership");
        complete = true;
        window.close();
    });
    const auto result = Application::run(window);
    if (result) std::wcerr << window.error() << L'\n';
    require(result == 0 && complete, "Expander retirement acceptance completed");
}
}
int main() {
    try {
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        struct Apartment { ~Apartment() { winrt::clear_factory_cache(); winrt::uninit_apartment(); } } apartment;
        for (unsigned variant : {0u, 1u, 2u}) {
            Fixture fixture(variant);
            Fixture::current = &fixture;
            fixture.window.post([&] { fixture.begin(); });
            const auto result = Application::run(fixture.window);
            Fixture::current = nullptr;
            if (fixture.error) std::rethrow_exception(fixture.error);
            if (result) std::wcerr << fixture.window.error() << L'\n';
            require(result == 0 && fixture.done && !fixture.expander->animating(), "Expander desktop acceptance completed");
        }
        retirement();
        std::cout << "Expander native input, timer, pixels, UIA, reversal, and idle contracts passed\n";
    } catch (const std::exception& error) {
        Fixture::current = nullptr;
        std::cerr << error.what() << '\n';
        return 1;
    }
}
