#pragma once
#include "xui/application.hpp"
#include "platform.hpp"

namespace xui {
struct TaskWake {
    HANDLE event{CreateEventW(nullptr, FALSE, FALSE, nullptr)};
    TaskWake() { win32_require(event != nullptr, "Create task event"); }
    ~TaskWake() { CloseHandle(event); }
    void signal() {
        std::lock_guard lock(mutex);
        win32_require(SetEvent(event) != FALSE, "Wake application dispatcher");
        if (dispatcher && !posted) {
            win32_require(PostMessageW(dispatcher, WM_APP + 71, 0, 0) != FALSE, "Post application wake");
            posted = true;
        }
    }
    void connect(HWND value) { std::lock_guard lock(mutex); dispatcher = value; posted = false; }
    void accepted() { std::lock_guard lock(mutex); posted = false; }
private:
    std::mutex mutex;
    HWND dispatcher{};
    bool posted{};
};
struct ViewTask::Impl {
    std::shared_ptr<ViewWorker> worker;
    std::function<void(ViewResult)> receive;
    std::uint64_t generation{}, applied{};
    bool cancelled{};
    void cancel();
    void deliver();
};
struct SampleTask::Impl {
    struct Worker;
    std::shared_ptr<Worker> worker;
    Receiver receive;
    bool cancelled{};
    std::uint64_t delivered{};
    void cancel();
    void deliver();
    void suspend(bool value);
    void start(Loader loader, std::shared_ptr<TaskWake> wake, unsigned interval);
};
}
