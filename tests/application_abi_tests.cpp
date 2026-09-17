#include "xui/xui.h"
#include <windows.h>
#include <atomic>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void ok(xui_status status) { require(status == XUI_OK, "Unexpected ABI status"); }
xui_string text(const char* value) { return {value, static_cast<uint32_t>(std::strlen(value)), 0}; }
struct Delivery {
    std::atomic<unsigned> accepted{}, executed{}, released{};
    static xui_status XUI_CALL callback(void* context, uint32_t execute) {
        auto& value = *static_cast<Delivery*>(context);
        if (execute) ++value.executed; else ++value.released;
        return XUI_OK;
    }
    void check() const { require(accepted == executed + released, "Accepted post did not execute or release exactly once"); }
};
struct Lifetime {
    xui_handle application{}, first{}, second{};
    HWND first_hwnd{}, second_hwnd{};
    unsigned closed{}, retired{};
    bool fail{};
    static xui_status XUI_CALL retire(void* context, uint32_t execute) {
        auto& self = *static_cast<Lifetime*>(context);
        if (!execute) return XUI_CALLBACK_FAILED;
        uint32_t state{};
        if (!self.retired) {
            ok(xui_window_destroy(self.first));
            ok(xui_window_state(self.second, &state));
            require(state == 1 && IsWindow(self.second_hwnd), "Opener retirement closed the survivor");
            ok(xui_window_close(self.second));
        } else {
            ok(xui_window_destroy(self.second));
        }
        ++self.retired;
        return XUI_OK;
    }
    static xui_status XUI_CALL on_closed(void* context, const xui_event* event) {
        auto& self = *static_cast<Lifetime*>(context);
        uint32_t state{};
        ok(xui_window_state(event->source, &state));
        require(event->kind == 100 && state == 3, "Closed event has an invalid state");
        require(xui_window_destroy(event->source) == XUI_BUSY, "Callback destruction was not deferred");
        require(xui_application_destroy(self.application) == XUI_BUSY, "Application disappeared during a callback");
        ++self.closed;
        ok(xui_application_post(self.application, retire, &self));
        return XUI_OK;
    }
    static xui_status XUI_CALL close_first(void* context, uint32_t execute) {
        auto& self = *static_cast<Lifetime*>(context);
        if (!execute) return XUI_CALLBACK_FAILED;
        if (self.fail) return 1234;
        return xui_window_close(self.first);
    }
    xui_handle create(const char* title) {
        xui_window_options options{sizeof(options), XUI_ABI_VERSION, text(title), 400, 260};
        xui_handle window{}, root{}, label{};
        ok(xui_application_window_create(application, &options, 0, &window));
        require(xui_application_destroy(application) == XUI_BUSY, "Created window lost its application");
        require(xui_window_run(window) == XUI_INVALID_ARGUMENT, "Application window entered a legacy run");
        ok(xui_stack_create(window, 1, &root));
        ok(xui_create(window, XUI_LABEL, text("Independent ABI document"), 0, &label));
        ok(xui_stack_add(root, label, 1));
        ok(xui_window_content(window, root));
        ok(xui_window_closed(window, on_closed, this));
        ok(xui_application_show(application, window));
        return window;
    }
    void run() {
        ok(xui_application_create(&application));
        first = create("ABI opener");
        second = create("ABI survivor");
        first_hwnd = FindWindowW(L"Xui.Window.1", L"ABI opener");
        second_hwnd = FindWindowW(L"Xui.Window.1", L"ABI survivor");
        require(first_hwnd && second_hwnd && first_hwnd != second_hwnd, "Distinct ABI native windows were not created");
        require(!GetWindow(first_hwnd, GW_OWNER) && !GetWindow(second_hwnd, GW_OWNER), "Documents acquired an HWND owner");
        std::thread wrong_thread([&] {
            require(xui_application_show(application, first) == XUI_WRONG_THREAD, "Show accepted the wrong thread");
        });
        wrong_thread.join();
        Delivery canceled;
        ok(xui_window_post(first, close_first, this));
        for (unsigned i = 0; i < 130; ++i) {
            ok(xui_window_post(first, Delivery::callback, &canceled));
            ++canceled.accepted;
        }
        const auto result = xui_application_run(application);
        require(result == (fail ? XUI_CALLBACK_FAILED : XUI_OK), "Window failure was lost after retirement");
        require(closed == 2 && retired == 2, "Each window must close and retire exactly once");
        require(!IsWindow(first_hwnd) && !IsWindow(second_hwnd), "Native hosts survived final retirement");
        canceled.check();
        require(canceled.executed == 0 && canceled.released == 130, "Window closure executed obsolete posts");
        require(xui_application_post(application, Delivery::callback, &canceled) == XUI_CLOSED, "Stopped dispatcher accepted work");
        ok(xui_application_destroy(application));
    }
};
void destroy_race() {
    for (unsigned iteration = 0; iteration < 16; ++iteration) {
        xui_handle app{};
        ok(xui_application_create(&app));
        Delivery delivery;
        std::atomic<bool> started{};
        std::thread worker([&] {
            for (unsigned i = 0; i < 2000; ++i) {
                const auto status = xui_application_post(app, Delivery::callback, &delivery);
                if (status == XUI_OK) ++delivery.accepted;
                else require(status == XUI_INVALID_HANDLE || status == XUI_CLOSED, "Post race returned an invalid status");
                if (i == 100) started = true;
            }
        });
        while (!started) std::this_thread::yield();
        ok(xui_application_destroy(app));
        worker.join();
        delivery.check();
        require(delivery.executed == 0, "Never-run application executed a post");
    }
}
void application_failure() {
    xui_handle app{};
    ok(xui_application_create(&app));
    ok(xui_application_post(app, [](void*, uint32_t) -> xui_status { return 4321; }, nullptr));
    require(xui_application_run(app) == XUI_CALLBACK_FAILED, "Application-post failure lost its ABI status");
    ok(xui_application_destroy(app));
}
}
int main(int argc, char** argv) {
    try {
        require(argc == 1 || (argc == 2 && std::strcmp(argv[1], "--post-race-only") == 0),
            "Usage: xui_application_abi_tests [--post-race-only]");
        if (argc == 1) {
            Lifetime{}.run();
            Lifetime failed; failed.fail = true; failed.run();
        }
        destroy_race();
        application_failure();
        std::cout << (argc == 1 ? "Application ABI lifetime, failure, batched cancellation and worker/destroy races passed.\n"
            : "Application ABI worker/destroy races passed.\n");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
