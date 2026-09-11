#pragma once
#include "xui/application.hpp"
#include "platform.hpp"

namespace xui {
struct TaskWake {
    HANDLE event{CreateEventW(nullptr, FALSE, FALSE, nullptr)};
    TaskWake() { win32_require(event != nullptr, "Create task event"); }
    ~TaskWake() { CloseHandle(event); }
};
struct ViewTask::Impl {
    std::shared_ptr<ViewWorker> worker;
    std::function<void(ViewResult)> receive;
    std::uint64_t generation{}, applied{};
    bool cancelled{};
    void cancel();
    void deliver();
};
}
