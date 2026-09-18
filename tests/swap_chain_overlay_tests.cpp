#include "xui/application.hpp"
#include "../demo/swap_chain_renderer.hpp"
#include "owned_window_capture.hpp"
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <thread>

namespace {
using namespace xui;
using swap_chain_sample::Renderer;
using swap_chain_sample::check;

constexpr std::uint32_t overlay_color = 0x3d7841;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void flush(HWND window) {
    SendMessageW(window, WM_APP + 12, 0, 0);
    RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    check(DwmFlush(), "Flush owned overlay composition");
}

RECT client_bounds(HWND child, HWND root) {
    RECT result{};
    require(GetClientRect(child, &result) != FALSE, "Read owned client bounds");
    MapWindowPoints(child, root, reinterpret_cast<POINT*>(&result), 2);
    return result;
}

HWND find_popup(HWND root) {
    HWND result{};
    EnumChildWindows(root, [](HWND child, LPARAM value) -> BOOL {
        wchar_t name[128]{}, cls[128]{};
        GetWindowTextW(child, name, 128);
        GetClassNameW(child, cls, 128);
        if (std::wstring_view(name) == L"Owned live overlay" &&
            std::wstring_view(cls) == L"Xui.Control.1") {
            *reinterpret_cast<HWND*>(value) = child;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&result));
    require(result != nullptr, "Find owned popup peer");
    return result;
}

bool matches(DWORD pixel, std::uint32_t color) {
    for (int shift : {0, 8, 16})
        if (std::abs(int((pixel >> shift) & 255) - int((color >> shift) & 255)) > 5) return false;
    return true;
}

template<class Predicate>
std::size_t count(const owned_window_capture::Pixels& frame, RECT area, Predicate predicate) {
    require(area.left >= 0 && area.top >= 0 && area.right <= frame.width &&
        area.bottom <= frame.height && area.right > area.left && area.bottom > area.top,
        "Pixel assertions use a nonempty area inside the owned capture");
    std::size_t result{};
    for (LONG y = area.top; y < area.bottom; ++y)
        for (LONG x = area.left; x < area.right; ++x)
            result += predicate(frame.data[static_cast<std::size_t>(y) * frame.width + x]);
    return result;
}

bool producer_pixel(DWORD pixel) {
    return matches(pixel, Renderer::blue) || matches(pixel, Renderer::gold) ||
        matches(pixel, Renderer::pink) || matches(pixel, Renderer::cyan);
}

void run_case(bool nested) {
    std::cout << (nested ? "Nested native hosts\n" : "Direct native hosts\n") << std::flush;
    const auto original_foreground = GetForegroundWindow();
    WindowOptions options;
    options.title = L"XUI owned live swap-chain overlay regression";
    options.size = {800, 520};
    options.theme = ThemeMode::light;
    options.show_activated = false;
    Window window(options);
    auto root = std::make_shared<Stack>(Axis::vertical);
    root->set_padding({12, 12, 12, 12});
    auto anchor = std::make_shared<Button>(L"Open owned overlay");
    root->add(anchor);
    auto row = std::make_shared<Stack>(Axis::horizontal);
    row->set_spacing(12);
    std::array panels{
        std::make_shared<SwapChainPanel>(L"Pointer producer"),
        std::make_shared<SwapChainPanel>(L"Handle producer")};
    for (const auto& panel : panels) {
        if (nested) row->add(std::make_shared<ContentHost>(panel), 1);
        else row->add(panel, 1);
    }
    root->add(row, 1);
    window.set_content(root);

    auto content = std::make_shared<Stack>(Axis::vertical);
    content->set_padding({16, 16, 16, 16});
    content->add(std::make_shared<Label>(L"Retained content above live producers"));
    auto popup = std::make_shared<Popup>(content, L"Owned live overlay");
    popup->set_fixed_size({360, 180});
    popup->set_placement(PopupPlacement::center);
    PartStyleValues style;
    style.background = ThemeColor{overlay_color};
    style.corner_radius = 12.0f;
    popup->set_control_style_values(StylePart::root, style);

    std::array<std::unique_ptr<Renderer>, 2> producers;
    std::atomic<bool> finished{};
    bool ran{};
    std::string failure;
    window.on_closed([&] {
        for (auto& producer : producers) producer.reset();
        winrt::clear_factory_cache();
        finished = true;
    });

    std::jthread worker([&](std::stop_token stop) {
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        if (!window.post([&] {
            ran = true;
            try {
                const auto root_hwnd = GetAncestor(panels[0]->native_window(), GA_ROOT);
                DWORD process{};
                GetWindowThreadProcessId(root_hwnd, &process);
                require(root_hwnd && process == GetCurrentProcessId(), "Only owned windows are inspected");
                require(GetForegroundWindow() == original_foreground, "Showing fixture does not change foreground");
                flush(root_hwnd);
                std::array<HWND, 2> hosts{};
                std::array<RECT, 2> areas{};
                std::array<SwapChainPanelMetrics, 2> metrics{};
                std::array<unsigned, 2> resizes{};
                std::array<std::vector<SwapChainPanelMetrics>, 2> changes;
                for (std::size_t i = 0; i < panels.size(); ++i) {
                    hosts[i] = panels[i]->native_window();
                    areas[i] = client_bounds(hosts[i], root_hwnd);
                    metrics[i] = panels[i]->metrics();
                    require(metrics[i].visible, "Both producers start visible");
                    producers[i] = std::make_unique<Renderer>(i == 1, true);
                    if (i == 1) {
                        panels[i]->set_swap_chain_handle(producers[i]->surface_handle());
                        producers[i]->close_surface_handle();
                    } else {
                        panels[i]->set_swap_chain(producers[i]->chain());
                    }
                    producers[i]->render(metrics[i], i == 1);
                    resizes[i] = producers[i]->resizes();
                    panels[i]->on_metrics_changed([&, i](const SwapChainPanelMetrics& value) {
                        changes[i].push_back(value);
                    });
                }
                auto unchanged = [&] {
                    for (std::size_t i = 0; i < panels.size(); ++i) {
                        const auto area = client_bounds(hosts[i], root_hwnd);
                        require(panels[i]->native_window() == hosts[i] && EqualRect(&area, &areas[i]),
                            "Opening and closing the overlay preserves producer HWND and physical bounds");
                        require(panels[i]->metrics() == metrics[i] && panels[i]->has_content(),
                            "Overlay does not resize, hide, or detach either producer");
                        for (const auto& value : changes[i])
                            require(value == metrics[i], "Overlay never transiently suspends or resizes a producer");
                        DXGI_SWAP_CHAIN_DESC1 desc{};
                        check(producers[i]->chain()->GetDesc1(&desc), "Read live producer buffers");
                        require(desc.Width == metrics[i].pixel_width && desc.Height == metrics[i].pixel_height &&
                            producers[i]->resizes() == resizes[i], "Producer buffers remain unchanged");
                    }
                    require(GetForegroundWindow() == original_foreground,
                        "Overlay operations preserve the foreground window");
                };
                auto capture = [&] {
                    flush(root_hwnd);
                    unchanged();
                    return owned_window_capture::capture(root_hwnd);
                };
                auto visible_producers = [&](const owned_window_capture::Pixels& frame, bool alternate) {
                    for (std::size_t i = 0; i < panels.size(); ++i) {
                        const auto color = (alternate != (i == 1)) ? Renderer::pink : Renderer::blue;
                        require(count(frame, areas[i], [&](DWORD pixel) { return matches(pixel, color); }) > 1000,
                            "Both live producers contribute their expected current pixels");
                    }
                };
                visible_producers(capture(), false);
                std::cout << "Two live producers captured before overlay\n" << std::flush;

                window.show_popup(popup, *anchor);
                require(popup->is_open(), "Overlay opens above active swap-chain content");
                flush(root_hwnd);
                const auto overlay = client_bounds(find_popup(root_hwnd), root_hwnd);
                std::array<RECT, 2> covered{};
                for (std::size_t i = 0; i < panels.size(); ++i) {
                    require(IntersectRect(&covered[i], &areas[i], &overlay) != FALSE,
                        "Overlay actually overlaps both producer surfaces");
                    InflateRect(&covered[i], -16, -16);
                }
                auto occludes = [&](const owned_window_capture::Pixels& frame) {
                    for (const auto& area : covered) {
                        require(count(frame, area, producer_pixel) == 0,
                            "No producer pixels show through the opaque overlay interior");
                        require(count(frame, area, [](DWORD pixel) { return matches(pixel, overlay_color); }) > 500,
                            "Actual XUI popup pixels occlude each live producer");
                    }
                };
                auto open_frame = capture();
                std::cout << "Popup bounds " << overlay.left << ',' << overlay.top << ',' << overlay.right << ',' << overlay.bottom
                    << " green pixels " << count(open_frame, {0, 0, open_frame.width, open_frame.height},
                        [](DWORD pixel) { return matches(pixel, overlay_color); }) << '\n';
                wchar_t capture_path[32768]{};
                if (GetEnvironmentVariableW(L"XUI_OVERLAY_CAPTURE", capture_path, 32768)) {
                    const DWORD bytes = static_cast<DWORD>(open_frame.data.size() * sizeof(DWORD));
                    BITMAPFILEHEADER header{0x4d42, static_cast<DWORD>(sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER)) + bytes,
                        0, 0, sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER)};
                    BITMAPINFOHEADER info{sizeof(BITMAPINFOHEADER), open_frame.width, -open_frame.height, 1, 32, BI_RGB, bytes};
                    std::ofstream output(capture_path, std::ios::binary);
                    output.write(reinterpret_cast<const char*>(&header), sizeof(header));
                    output.write(reinterpret_cast<const char*>(&info), sizeof(info));
                    output.write(reinterpret_cast<const char*>(open_frame.data.data()), bytes);
                    require(bool(output), "Write owned overlay capture");
                }
                visible_producers(open_frame, false);
                occludes(open_frame);

                for (std::size_t i = 0; i < producers.size(); ++i) {
                    const auto presents = producers[i]->presents();
                    producers[i]->render(panels[i]->metrics(), i == 0);
                    require(producers[i]->presents() == presents + 1, "Each producer presents while overlay is open");
                }
                auto changed_frame = capture();
                visible_producers(changed_frame, true);
                occludes(changed_frame);
                std::cout << "Overlay occludes both producers while their visible output changes\n" << std::flush;

                window.dismiss_popup(*popup);
                require(!popup->is_open(), "Overlay dismissal completes");
                auto closed_frame = capture();
                visible_producers(closed_frame, true);
                for (const auto& area : covered) {
                    const auto pixels = static_cast<std::size_t>(area.right - area.left) * (area.bottom - area.top);
                    require(count(closed_frame, area, producer_pixel) > pixels * 9 / 10,
                        "Dismissal reveals live producer pixels under the former overlay");
                    require(count(closed_frame, area, [](DWORD pixel) { return matches(pixel, overlay_color); }) == 0,
                        "Dismissed overlay leaves no stale compositor pixels");
                }
            } catch (const std::exception& error) {
                failure = error.what();
            }
            for (const auto& panel : panels) panel->on_metrics_changed({});
            window.close();
        })) {
            std::cerr << "Could not post overlay fixture\n" << std::flush;
            std::_Exit(2);
        }
        for (int i = 0; i < 600 && !finished && !stop.stop_requested(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (!finished && !stop.stop_requested()) {
            std::cerr << "Live overlay fixture exceeded its timeout\n" << std::flush;
            std::_Exit(2);
        }
    });
    const auto result = Application::run(window);
    finished = true;
    if (result != 0) std::wcerr << window.error() << L'\n';
    require(ran && result == 0 && failure.empty(), failure.empty() ? "Overlay fixture completes" : failure.c_str());
    require(!popup->is_open(), "Popup is closed after owner teardown");
    for (const auto& panel : panels)
        require(!panel->native_window() && !panel->has_content(), "Owner teardown releases each native composition host");
    std::cout << "Live swap-chain overlay compositor regression passed\n";
}
}

int main() {
    try {
        run_case(false);
        run_case(true);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
