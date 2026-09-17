#include "xui/application.hpp"
#include "../demo/swap_chain_renderer.hpp"
#include "owned_window_capture.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <limits>
#include <thread>

namespace {
using namespace xui;
using swap_chain_sample::Renderer;
using swap_chain_sample::check;
using Microsoft::WRL::ComPtr;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<class F> void rejects(F action, const char* message) {
    try { action(); } catch (const std::exception&) { return; }
    throw std::runtime_error(message);
}
void flush(HWND window) {
    SendMessageW(window, WM_APP + 12, 0, 0);
    RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
}
RECT client_bounds(HWND child, HWND root) {
    RECT bounds{};
    require(GetClientRect(child, &bounds) != FALSE, "Read owned native client bounds");
    MapWindowPoints(child, root, reinterpret_cast<POINT*>(&bounds), 2);
    return bounds;
}
HWND find_child(HWND root, const wchar_t* class_name, const wchar_t* title = nullptr) {
    struct Search { const wchar_t* class_name; const wchar_t* title; HWND result{}; } search{class_name, title};
    EnumChildWindows(root, [](HWND child, LPARAM value) -> BOOL {
        auto& search = *reinterpret_cast<Search*>(value);
        wchar_t cls[128]{}, name[256]{};
        GetClassNameW(child, cls, 128); GetWindowTextW(child, name, 256);
        if (_wcsicmp(cls, search.class_name) == 0 &&
            (!search.title || std::wstring_view(name) == search.title)) {
            search.result = child;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    require(search.result != nullptr, "Find owned native child");
    return search.result;
}
bool matches(DWORD pixel, std::uint32_t rgb) {
    for (int shift : {0, 8, 16})
        if (std::abs(int((pixel >> shift) & 255) - int((rgb >> shift) & 255)) > 5) return false;
    return true;
}
bool authored(DWORD pixel) {
    return matches(pixel, Renderer::blue) || matches(pixel, Renderer::gold) ||
        matches(pixel, Renderer::pink) || matches(pixel, Renderer::cyan);
}
std::size_t count(const owned_window_capture::Pixels& pixels, RECT bounds, std::uint32_t color) {
    const auto left = std::clamp<int>(bounds.left, 0, pixels.width);
    const auto right = std::clamp<int>(bounds.right, left, pixels.width);
    const auto top = std::clamp<int>(bounds.top, 0, pixels.height);
    const auto bottom = std::clamp<int>(bounds.bottom, top, pixels.height);
    std::size_t result{};
    for (auto y = top; y < bottom; ++y) for (auto x = left; x < right; ++x)
        result += matches(pixels.data[static_cast<std::size_t>(y) * pixels.width + x], color);
    return result;
}

// DXGI holds this private interface until the actual chain is destroyed.
// This tests ownership without depending on implementation-specific AddRef counts.
class Lifetime final : public IUnknown {
public:
    explicit Lifetime(std::shared_ptr<std::atomic<bool>> destroyed) : destroyed_(std::move(destroyed)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        if (iid != __uuidof(IUnknown)) return E_NOINTERFACE;
        *value = static_cast<IUnknown*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const auto remaining = --references_;
        if (!remaining) { destroyed_->store(true); delete this; }
        return remaining;
    }
private:
    std::atomic<ULONG> references_{1};
    std::shared_ptr<std::atomic<bool>> destroyed_;
};
std::shared_ptr<std::atomic<bool>> track(IDXGISwapChain1* chain) {
    static constexpr GUID key{0xb389c857, 0x50e2, 0x4f63, {0x87, 0x21, 0x13, 0xdc, 0x99, 0x21, 0xaf, 0x19}};
    auto destroyed = std::make_shared<std::atomic<bool>>(false);
    ComPtr<IUnknown> witness;
    witness.Attach(new Lifetime(destroyed));
    check(chain->SetPrivateDataInterface(key, witness.Get()), "Track producer resource lifetime");
    return destroyed;
}
void released(const std::shared_ptr<std::atomic<bool>>& destroyed, const char* message) {
    for (int i = 0; i < 150 && !destroyed->load(); ++i) {
        DwmFlush();
        Sleep(20);
    }
    require(destroyed->load(), message);
}

void callback_lifetime_case(unsigned mode) {
    WindowOptions options;
    options.title = L"XUI owned metrics callback lifetime";
    options.size = {360, 240};
    options.show_activated = false;
    Window window(options);
    auto root = std::make_shared<Stack>(Axis::vertical);
    auto panel = std::make_shared<SwapChainPanel>();
    root->add(panel, 1);
    window.set_content(root);
    std::unique_ptr<Renderer> producer;
    std::shared_ptr<std::atomic<bool>> lifetime;
    bool invoked{};
    unsigned callbacks{}, callbacks_at_teardown{};
    std::atomic<bool> closed{};
    panel->on_metrics_changed([&](const SwapChainPanelMetrics& metrics) {
        ++callbacks;
        if (!metrics.visible || invoked) return;
        invoked = true;
        producer = std::make_unique<Renderer>(false, true);
        lifetime = track(producer->chain());
        panel->set_swap_chain(producer->chain());
        producer->render(metrics);
        callbacks_at_teardown = callbacks;
        if (mode != 1) window.close();
        if (mode != 0) throw std::runtime_error("Expected metrics callback failure");
    });
    window.on_closed([&] {
        panel->on_metrics_changed({});
        producer.reset();
        closed = true;
    });
    std::jthread watchdog([&](std::stop_token stop) {
        for (int i = 0; i < 200 && !closed && !stop.stop_requested(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (!closed && !stop.stop_requested()) {
            std::cerr << "Metrics callback lifetime fixture exceeded its timeout\n" << std::flush;
            std::_Exit(2);
        }
    });
    const auto result = Application::run(window);
    require(invoked && closed && result == (mode ? 1 : 0), "Metrics callback can close, throw, or close then throw safely");
    if (mode) require(window.error().find(L"Expected metrics callback failure") != std::wstring::npos,
        "Thrown metrics callback reports its explicit application error");
    require(callbacks == callbacks_at_teardown, "Reentrant close does not emit teardown metrics");
    require(!panel->native_window() && !panel->has_content() && panel->metrics() == SwapChainPanelMetrics{} && !producer,
        "Callback teardown releases native state and producer before retained panel destruction");
    released(lifetime, "Callback close or failure releases actual producer resources");
}

void run_case(bool surface_handle, bool capture) {
    WindowOptions options;
    options.title = surface_handle ? L"XUI owned surface-handle regression" : L"XUI owned swap-chain regression";
    options.size = {720, 580};
    options.theme = ThemeMode::light;
    options.show_activated = false;
    Window window(options);
    auto root = std::make_shared<Stack>(Axis::vertical);
    root->set_spacing(10); root->set_padding({12, 12, 12, 12});
    auto anchor = std::make_shared<Button>(L"Neighbor above graphics");
    auto input = std::make_shared<TextInput>(L"Native input beside graphics");
    input->set_text(L"Owned native text");
    root->add(anchor); root->add(input);
    auto row = std::make_shared<Stack>(Axis::horizontal);
    row->set_spacing(12);
    auto panel = std::make_shared<SwapChainPanel>(L"Owned authored graphic");
    panel->set_preferred_size({420, 280});
    panel->set_minimum_size({0, 280});
    panel->set_maximum_size({10000, 280});
    auto host = std::make_shared<ContentHost>(panel);
    auto content = std::make_shared<Stack>(Axis::vertical);
    content->add(host);
    auto filler = std::make_shared<Label>(L"Scroll past the graphic");
    filler->set_fixed_size({300, 700});
    content->add(filler);
    auto scroll = std::make_shared<ScrollView>(content, L"Owned graphics viewport");
    row->add(scroll, 1);
    auto neighbor = std::make_shared<Button>(L"Right neighbor");
    neighbor->set_fixed_size({150, 160});
    row->add(neighbor);
    root->add(row, 1);
    root->add(std::make_shared<Label>(L"Neighbor below graphics"));
    auto popup = std::make_shared<Popup>(std::make_shared<Label>(L"Owned retained popup"));
    require(!panel->tab_stop() && panel->name() == L"Owned authored graphic", "Panel defaults preserve name and skip Tab");
    require(!panel->native_window() && !panel->has_content() && panel->metrics() == SwapChainPanelMetrics{},
        "Unattached panel has no native resources or metrics");
    window.set_content(root);

    std::unique_ptr<Renderer> producer;
    std::shared_ptr<std::atomic<bool>> lifetime;
    std::shared_ptr<SwapChainPanel> retired;
    std::exception_ptr callback_error;
    std::vector<SwapChainPanelMetrics> notifications;
    bool alternate{};
    bool auto_render = true;
    unsigned callbacks_at_close{};
    std::atomic<bool> done{};
    bool ran{};
    std::string error;
    auto changed = [&](const SwapChainPanelMetrics& metrics) {
        notifications.push_back(metrics);
        try { if (producer && auto_render) producer->render(metrics, alternate); }
        catch (...) { callback_error = std::current_exception(); }
    };
    panel->on_metrics_changed(changed);
    window.on_closed([&] {
        panel->on_metrics_changed({});
        producer.reset();
        // Each case owns a separate Application COM lifetime.
        if (capture) winrt::clear_factory_cache();
        done = true;
    });

    std::jthread worker([&](std::stop_token stop) {
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        if (!window.post([&] {
            ran = true;
            try {
                const auto hwnd = GetAncestor(panel->native_window(), GA_ROOT);
                DWORD process{};
                GetWindowThreadProcessId(hwnd, &process);
                require(hwnd && process == GetCurrentProcessId(), "Fixture uses only its owned window");
                const auto foreground = GetForegroundWindow();
                const auto viewport = find_child(hwnd, L"Xui.Control.1", L"Owned graphics viewport");
                const auto button = find_child(hwnd, L"Xui.Control.1", L"Right neighbor");
                const auto edit = find_child(hwnd, L"EDIT");
                auto sync = [&] {
                    flush(hwnd);
                    if (callback_error) std::rethrow_exception(callback_error);
                };
                auto stage = [&](const char* name) {
                    std::cout << (surface_handle ? "Handle: " : "Pointer: ") << name << '\n' << std::flush;
                };
                auto dimensions = [&] {
                    const auto metrics = panel->metrics();
                    RECT native{};
                    require(GetClientRect(panel->native_window(), &native) != FALSE, "Panel exposes a live borrowed HWND");
                    require(metrics.pixel_width == static_cast<unsigned>(native.right) &&
                        metrics.pixel_height == static_cast<unsigned>(native.bottom),
                        "Metrics match physical native client dimensions");
                    require(metrics.visible && metrics.pixel_width && metrics.pixel_height &&
                        std::isfinite(metrics.rasterization_scale) && metrics.rasterization_scale > 0,
                        "Shown panel publishes usable physical metrics");
                    DXGI_SWAP_CHAIN_DESC1 description{};
                    check(producer->chain()->GetDesc1(&description), "Read resized chain");
                    require(description.Width == metrics.pixel_width && description.Height == metrics.pixel_height,
                        "Producer ResizeBuffers follows published metrics");
                };
                auto attach = [&] {
                    producer = std::make_unique<Renderer>(surface_handle, true);
                    lifetime = track(producer->chain());
                    if (surface_handle) {
                        panel->set_swap_chain_handle(producer->surface_handle());
                        producer->close_surface_handle();
                        require(!producer->surface_handle(), "Caller closes imported surface handle immediately");
                    } else {
                        panel->set_swap_chain(producer->chain());
                    }
                    producer->render(panel->metrics(), alternate);
                    stage("attach and authored pixels");
                    sync();
                    require(panel->has_content(), "Panel retains attached content");
                    dimensions();
                };
                auto pixels = [&](bool shown, bool accent = false) {
                    if (!capture) return;
                    RECT allowed{};
                    auto panel_bounds = client_bounds(panel->native_window(), hwnd);
                    auto viewport_bounds = client_bounds(viewport, hwnd);
                    IntersectRect(&allowed, &panel_bounds, &viewport_bounds);
                    const auto neighbor_bounds = client_bounds(button, hwnd);
                    std::string reason;
                    for (int attempt = 0; attempt < 6; ++attempt) {
                        sync();
                        DwmFlush();
                        auto frame = owned_window_capture::capture(hwnd);
                        auto tolerance = allowed;
                        InflateRect(&tolerance, 2, 2);
                        std::size_t total{}, leaked{};
                        for (int y = 0; y < frame.height; ++y) for (int x = 0; x < frame.width; ++x) {
                            if (!authored(frame.data[static_cast<std::size_t>(y) * frame.width + x])) continue;
                            ++total;
                            if (!PtInRect(&tolerance, POINT{x, y})) ++leaked;
                        }
                        if (!shown) {
                            if (!total) return;
                            reason = "Detached or hidden visual still contributes compositor pixels";
                        } else if (leaked) {
                            reason = "Authored compositor pixels escape the panel or scroll viewport";
                        } else if (count(frame, allowed, alternate ? Renderer::pink : Renderer::blue) < 500) {
                            reason = "Actual compositor capture has no expected producer background";
                        } else if (accent && count(frame, allowed, alternate ? Renderer::cyan : Renderer::gold) < 100) {
                            reason = "Actual compositor capture has no authored inner rectangle";
                        } else {
                            require(count(frame, neighbor_bounds, Renderer::blue) == 0 &&
                                count(frame, neighbor_bounds, Renderer::pink) == 0, "Native neighbor is not covered by graphics");
                            return;
                        }
                        Sleep(80);
                    }
                    throw std::runtime_error(reason);
                };

                sync();
                require(!notifications.empty(), "Initial native layout emits metrics");
                attach();
                pixels(true, true);
                const auto initial_presents = producer->presents();
                alternate = true;
                producer->render(panel->metrics(), alternate);
                pixels(true, true);
                require(producer->presents() == initial_presents + 1, "Authored frame changes only through producer Present");

                stage("rainbow triangle Y-axis rotation");
                auto_render = false;
                producer->render_triangle(panel->metrics(), 0);
                sync();
                if (capture) {
                    const auto area = client_bounds(panel->native_window(), hwnd);
                    const auto triangle_frame = [&](bool edge_on = false) {
                        DwmFlush();
                        auto frame = owned_window_capture::capture(hwnd);
                        std::size_t red{}, green{}, blue{}, mixed{};
                        for (LONG y = area.top; y < area.bottom; ++y) for (LONG x = area.left; x < area.right; ++x) {
                            const auto pixel = frame.data[static_cast<std::size_t>(y) * frame.width + x];
                            const auto r = (pixel >> 16) & 255, g = (pixel >> 8) & 255, b = pixel & 255;
                            red += r > 150 && g < 80 && b < 80;
                            green += g > 150 && r < 80 && b < 80;
                            blue += b > 150 && r < 80 && g < 80;
                            mixed += r > 45 && g > 45 && b > 45;
                        }
                        if (edge_on)
                            require(red + green + blue + mixed < 10, "Y-axis quarter turn makes the triangle edge-on");
                        else
                            require(red > 100 && green > 100 && blue > 100 && mixed > 100,
                                "Real triangle frame contains RGB vertices and interpolated rainbow colors");
                        return frame;
                    };
                    auto first = triangle_frame();
                    producer->render_triangle(panel->metrics(), 0.7f);
                    auto second = triangle_frame();
                    require(first.width == second.width && first.height == second.height, "Rotation preserves output dimensions");
                    std::size_t changed_pixels{};
                    for (LONG y = area.top; y < area.bottom; ++y) for (LONG x = area.left; x < area.right; ++x) {
                        const auto index = static_cast<std::size_t>(y) * first.width + x;
                        changed_pixels += first.data[index] != second.data[index];
                    }
                    require(changed_pixels > 1000, "Rotation changes actual triangle pixels without layout changes");
                    producer->render_triangle(panel->metrics(), 1.5707963f);
                    triangle_frame(true);
                    producer->render_triangle(panel->metrics(), 3.1415927f);
                    triangle_frame();
                }
                const auto triangle_presents = producer->presents();
                auto hidden_metrics = panel->metrics();
                hidden_metrics.visible = false;
                producer->render_triangle(hidden_metrics, 1);
                require(producer->presents() == triangle_presents, "Hidden triangle does not render or present");
                rejects([&] { producer->render_triangle(panel->metrics(), std::numeric_limits<float>::infinity()); },
                    "Non-finite triangle angle is rejected");
                auto_render = true;
                producer->render(panel->metrics(), alternate);
                pixels(true, true);

                stage("invalid content");
                rejects([&] { panel->set_swap_chain_handle(INVALID_HANDLE_VALUE); }, "Invalid surface handle must fail");
                swap_chain_sample::SurfaceHandle event;
                *event.put() = CreateEventW(nullptr, FALSE, FALSE, nullptr);
                require(event.get() != nullptr, "Create owned wrong-kind handle");
                rejects([&] { panel->set_swap_chain_handle(event.get()); }, "Event handle is not a composition surface");
                require(panel->has_content(), "Invalid handle preserves existing content");
                HWND wrong_window = CreateWindowExW(WS_EX_NOACTIVATE, L"STATIC", L"Owned wrong-chain fixture",
                    WS_POPUP, 0, 0, 64, 64, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
                require(wrong_window != nullptr, "Create owned hidden HWND for incompatible chain");
                struct Destroy { HWND window; ~Destroy() { DestroyWindow(window); } } destroy{wrong_window};
                DXGI_SWAP_CHAIN_DESC1 wrong{};
                wrong.Width = wrong.Height = 64; wrong.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
                wrong.SampleDesc.Count = 1; wrong.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
                wrong.BufferCount = 2; wrong.Scaling = DXGI_SCALING_STRETCH;
                wrong.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
                ComPtr<IDXGISwapChain1> hwnd_chain;
                check(producer->factory()->CreateSwapChainForHwnd(producer->device(), wrong_window, &wrong,
                    nullptr, nullptr, &hwnd_chain), "Create deliberately incompatible HWND swap chain");
                rejects([&] { panel->set_swap_chain(hwnd_chain.Get()); }, "Non-composition discard chain must fail");
                hwnd_chain.Reset();
                wrong.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
                check(producer->factory()->CreateSwapChainForHwnd(producer->device(), wrong_window, &wrong,
                    nullptr, nullptr, &hwnd_chain), "Create flip-sequential HWND swap chain");
                rejects([&] { panel->set_swap_chain(hwnd_chain.Get()); },
                    "An HWND chain is not a composition chain even when its descriptor matches");
                hwnd_chain.Reset();
                require(panel->has_content(), "Rejected chain preserves existing content");
                pixels(true, true);

                stage("popup and native editor");
                bool blocked{};
                try { window.show_popup(popup, *anchor); } catch (const std::logic_error&) { blocked = true; }
                require(blocked && !popup->is_open(), "Active native graphics reject retained popup");
                // Native editor messages target only this owned, non-activated window.
                const auto focus = GetFocus();
                const auto text_end = GetWindowTextLengthW(edit);
                SendMessageW(edit, EM_SETSEL, text_end, text_end);
                SendMessageW(edit, WM_CHAR, L'!', 0);
                sync();
                require(input->text() == L"Owned native text!", "Native text editing coexists with presented graphics");
                require(GetFocus() == focus, "Targeted native edit messages preserve existing keyboard focus");

                stage("resize and DPI");
                const auto before_resize = panel->metrics();
                RECT outer{}; require(GetWindowRect(hwnd, &outer) != FALSE, "Read owned outer size");
                require(SetWindowPos(hwnd, nullptr, 0, 0, outer.right - outer.left + 80, outer.bottom - outer.top + 40,
                    SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE) != FALSE, "Resize owned window without activation");
                sync(); dimensions();
                require(panel->metrics().pixel_width != before_resize.pixel_width, "Window resize changes native panel width");
                auto_render = false;
                const auto presentations = producer->presents();
                DXGI_SWAP_CHAIN_DESC1 old_buffers{};
                check(producer->chain()->GetDesc1(&old_buffers), "Read buffers before DPI notification");
                const UINT synthetic_dpi = panel->metrics().rasterization_scale == 1.5f ? 192 : 144;
                GetWindowRect(hwnd, &outer);
                SendMessageW(hwnd, WM_DPICHANGED, MAKEWPARAM(synthetic_dpi, synthetic_dpi), reinterpret_cast<LPARAM>(&outer));
                sync();
                require(std::abs(panel->metrics().rasterization_scale - synthetic_dpi / 96.0f) < 0.001f,
                    "DPI change publishes rasterization scale");
                DXGI_SWAP_CHAIN_DESC1 untouched{};
                check(producer->chain()->GetDesc1(&untouched), "Read buffers with producer paused");
                require(untouched.Width == old_buffers.Width && untouched.Height == old_buffers.Height &&
                    producer->presents() == presentations, "XUI does not resize or present producer buffers");
                auto_render = true;
                producer->render(panel->metrics(), alternate);
                sync(); dimensions(); pixels(true, true);
                require(GetFocus() == focus, "Layout and DPI preserve native editor focus");
                require(GetForegroundWindow() != hwnd || foreground == hwnd,
                    "Graphics and native editing do not activate the owned window");

                stage("scroll and visibility");
                const auto before_scroll = producer->resizes();
                scroll->set_offset(90); sync();
                require(panel->metrics().visible, "Partially scrolled panel stays visible");
                require(producer->resizes() == before_scroll, "Clipping does not resize producer buffers");
                pixels(true);
                scroll->set_offset(scroll->maximum_offset()); sync();
                require(!panel->metrics().visible && panel->has_content(), "Fully scrolled-out panel retains content but suspends visuals");
                const auto suspended_presents = producer->presents();
                pixels(false);
                require(producer->presents() == suspended_presents, "Fully clipped producer stops presenting");
                scroll->set_offset(0); sync(); pixels(true, true);
                panel->set_visible(false); sync();
                require(!panel->metrics().visible && panel->has_content(), "Hide retains producer content");
                pixels(false);
                panel->set_visible(true); sync(); dimensions(); pixels(true, true);
                ShowWindow(hwnd, SW_MINIMIZE); sync();
                require(!panel->metrics().visible && panel->has_content(), "Minimize suspends without dropping content");
                ShowWindow(hwnd, SW_SHOWNOACTIVATE); sync();
                require(panel->metrics().visible, "Restore resumes native graphics");
                pixels(true, true);
                require(std::any_of(notifications.begin(), notifications.end(),
                    [](const auto& m) { return !m.visible; }), "Visibility transitions reach producer callback");

                stage("null detach and producer ownership");
                panel->set_swap_chain_handle(nullptr); sync();
                require(!panel->has_content(), "Null handle clears either content path");
                pixels(false);
                window.show_popup(popup, *anchor); sync();
                require(popup->is_open(), "Detached graphics no longer exclude retained popup");
                window.dismiss_popup(*popup); sync();
                producer.reset();
                released(lifetime, "Null handle releases retired producer resources");
                attach(); pixels(true, true);
                panel->set_swap_chain(nullptr); sync();
                require(!panel->has_content(), "Null pointer clears either content path");
                pixels(false);
                producer.reset();
                released(lifetime, "Null pointer releases retired producer resources");
                attach();
                if (!surface_handle) {
                    producer.reset();
                    require(!lifetime->load() && panel->has_content(), "Panel owns pointer content after producer drops its reference");
                    pixels(true, true);
                    panel->set_swap_chain(nullptr); sync();
                    released(lifetime, "Clearing content releases the panel's last producer reference");
                    attach();
                }

                stage("content replacement and close");
                retired = panel;
                const auto retired_window = retired->native_window();
                auto replacement = std::make_shared<SwapChainPanel>(L"Replacement authored graphic");
                replacement->set_preferred_size({420, 280});
                replacement->set_minimum_size({0, 280}); replacement->set_maximum_size({10000, 280});
                const auto before_retire = notifications.size();
                window.replace_content(*host, replacement);
                require(!IsWindow(retired_window) && !retired->native_window() && !retired->has_content() &&
                    retired->metrics() == SwapChainPanelMetrics{}, "Retained retired panel releases HWND, content, and metrics");
                require(notifications.size() == before_retire, "Content replacement emits no teardown metrics callback");
                retired->on_metrics_changed({});
                producer.reset();
                released(lifetime, "Content replacement retires actual producer resources");
                panel = std::move(replacement);
                panel->on_metrics_changed(changed);
                attach(); pixels(true, true);
                callbacks_at_close = static_cast<unsigned>(notifications.size());
            } catch (const winrt::hresult_error& failure) {
                error = "Owned Graphics Capture failed (HRESULT " +
                    std::to_string(static_cast<std::int32_t>(failure.code())) +
                    "). Use --no-capture only for explicit non-pixel coverage.";
            } catch (const std::exception& failure) { error = failure.what(); }
            window.close();
        })) done = true;
        for (int i = 0; i < 1200 && !done && !stop.stop_requested(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (!done && !stop.stop_requested()) {
            std::cerr << "Swap-chain fixture exceeded its 120-second timeout\n" << std::flush;
            std::_Exit(2);
        }
    });
    const auto result = Application::run(window);
    done = true;
    require(ran && result == 0 && error.empty(), error.empty() ? "Native swap-chain run completes" : error.c_str());
    require(!panel->native_window() && !panel->has_content() && panel->metrics() == SwapChainPanelMetrics{},
        "Closed retained panel owns no HWND, metrics, or composition content");
    require(notifications.size() == callbacks_at_close, "Close does not call producer during resource teardown");
    released(lifetime, "Window close releases producer before retained control destruction");
    require(retired && !retired->native_window() && !retired->has_content(), "Earlier retired control stays detached after COM shutdown");
    std::cout << (surface_handle ? "Surface handle" : "Swap-chain pointer") <<
        ": native layout, DPI, clipping, visibility, popup, editor, replacement, and lifetime passed" <<
        (capture ? " with real compositor pixels\n" : " (pixel capture explicitly disabled)\n");
}
}

int main(int argc, char** argv) {
    try {
        bool capture = true;
        for (int i = 1; i < argc; ++i) {
            if (std::string_view(argv[i]) == "--no-capture") capture = false;
            else throw std::invalid_argument("Usage: xui_swap_chain_panel_tests [--no-capture]");
        }
        run_case(false, capture);
        run_case(true, capture);
        for (unsigned mode = 0; mode != 3; ++mode) callback_lifetime_case(mode);
        std::cout << "Metrics callbacks safely close, throw, and close then throw\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
