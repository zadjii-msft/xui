#include "xui/application.hpp"
#include "swap_chain_renderer.hpp"
#include <shellapi.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    using namespace xui;
    try {
        bool use_handle{}, force_warp{}, smoke{};
        int count{};
        auto args = CommandLineToArgvW(GetCommandLineW(), &count);
        if (!args) throw std::runtime_error("Cannot read command-line arguments");
        for (int i = 1; i < count; ++i) {
            use_handle |= std::wstring_view(args[i]) == L"--handle";
            force_warp |= std::wstring_view(args[i]) == L"--warp";
            smoke |= std::wstring_view(args[i]) == L"--smoke";
        }
        LocalFree(args);

        WindowOptions options{L"XUI DirectComposition rainbow triangle", {760, 600}, ThemeMode::dark, {420, 360}};
        options.show_activated = !smoke;
        Window window(options);
        auto root = std::make_shared<Stack>(Axis::vertical);
        root->set_padding({20, 20, 20, 20});
        root->set_spacing(12);
        auto heading = std::make_shared<Label>(L"The obligatory spinning rainbow triangle");
        heading->set_heading(true);
        root->add(heading);
        auto editor = std::make_shared<TextInput>(L"Native text beside graphics");
        editor->set_placeholder(L"Type here: this is an ordinary native XUI text input.");
        root->add(editor);
        auto pause = std::make_shared<Button>(L"Pause rotation");
        root->add(pause);
        auto panel = std::make_shared<SwapChainPanel>(L"Spinning rainbow triangle");
        root->add(panel, 1);
        auto status = std::make_shared<Label>(L"Preparing graphics...");
        root->add(status);
        auto hint = std::make_shared<Label>(L"Resize the window or move it between displays. Use --handle or --warp to select a producer.");
        hint->set_wrapping(true);
        hint->set_caption(true);
        root->add(hint);

        std::unique_ptr<swap_chain_sample::Renderer> producer;
        bool paused{}, failed{}, advancing{};
        float angle{};
        auto last_frame = std::chrono::steady_clock::now();
        SwapChainPanelMetrics status_metrics;
        std::mutex frame_mutex;
        std::condition_variable_any frame_ready;
        bool ticking{};
        std::atomic<bool> frame_pending{};
        const auto set_ticking = [&](bool value) {
            { std::lock_guard lock(frame_mutex); ticking = value; }
            frame_ready.notify_all();
        };
        auto render = [&](const SwapChainPanelMetrics& metrics) {
            if (failed) return;
            try {
                if (!metrics.visible || !metrics.pixel_width || !metrics.pixel_height) {
                    advancing = false;
                    set_ticking(false);
                    return;
                }
                if (!producer) {
                    producer = std::make_unique<swap_chain_sample::Renderer>(use_handle, force_warp);
                    if (use_handle) {
                        panel->set_swap_chain_handle(producer->surface_handle());
                        // The panel imports its own reference; it does not borrow this handle.
                        producer->close_surface_handle();
                    } else {
                        panel->set_swap_chain(producer->chain());
                    }
                }
                const auto now = std::chrono::steady_clock::now();
                if (advancing && !paused)
                    angle = std::fmod(angle + std::chrono::duration<float>(now - last_frame).count() * 1.2f, 6.2831853f);
                last_frame = now;
                advancing = !paused;
                producer->render_triangle(metrics, angle);
                if (status_metrics != metrics) {
                    status_metrics = metrics;
                    status->set_text(std::wstring(producer->warp() ? L"WARP" : L"Hardware") +
                        (use_handle ? L" | Surface handle | " : L" | Swap-chain pointer | ") +
                        std::to_wstring(metrics.pixel_width) + L" x " + std::to_wstring(metrics.pixel_height) +
                        L" pixels | Scale " + std::to_wstring(metrics.rasterization_scale));
                }
                set_ticking(!paused);
                if (smoke && producer->presents() >= 60) window.close();
            } catch (const std::exception& error) {
                failed = true;
                set_ticking(false);
                const std::string message = error.what();
                status->set_tone(TextTone::error);
                status->set_text(std::wstring(message.begin(), message.end()));
                pause->set_enabled(false);
                if (smoke) window.close();
            }
        };
        std::jthread animation;
        panel->on_metrics_changed(render);
        pause->on_click([&] {
            paused = !paused;
            advancing = false;
            pause->set_name(paused ? L"Resume rotation" : L"Pause rotation");
            render(panel->metrics());
        });
        window.on_closed([&] {
            set_ticking(false);
            animation.request_stop();
            // Application::run releases COM after this notification.
            panel->on_metrics_changed({});
            producer.reset();
        });
        window.set_content(root);
        animation = std::jthread([&](std::stop_token stop) {
            std::unique_lock lock(frame_mutex);
            while (frame_ready.wait(lock, stop, [&] { return ticking; })) {
                if (frame_ready.wait_for(lock, stop, std::chrono::milliseconds(16), [&] { return !ticking; })) continue;
                if (stop.stop_requested()) break;
                lock.unlock();
                // At most one frame waits in the UI queue, even during resizing or modal input.
                if (!frame_pending.exchange(true) && !window.post([&] {
                    frame_pending = false;
                    if (!paused) render(panel->metrics());
                })) break;
                lock.lock();
            }
        });
        const auto result = Application::run(window);
        return failed ? 1 : result;
    } catch (const std::exception& error) {
        const std::string message = error.what();
        MessageBoxW(nullptr, std::wstring(message.begin(), message.end()).c_str(),
            L"Swap-chain sample error", MB_OK | MB_ICONERROR);
        return 1;
    }
}
