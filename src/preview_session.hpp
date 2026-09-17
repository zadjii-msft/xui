#pragma once
#include "preview_protocol.hpp"
#include <mutex>
#include <optional>

namespace xui::preview {
struct Session {
    std::mutex mutex;
    bool cancel{}, alive{true};
    RECT rect{};
    std::optional<bool> focus;
    std::optional<PreviewStatus> result;
    LONG focus_actions{};
    DWORD broker_process{};
    std::wstring provider;
    void revoke() {
        std::lock_guard lock(mutex);
        alive = false; cancel = true; result.reset(); focus_actions = 0;
    }
};
std::shared_ptr<Session> start_session(std::wstring helper, std::wstring path,
    RECT rect, std::uint64_t generation);
std::wstring preview_helper_path();
bool drain_sessions(unsigned timeout_ms = 2500);
}
