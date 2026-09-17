#pragma once
#include "xui/shell_preview.hpp"
#include <windows.h>
#include <type_traits>

namespace xui::preview {
constexpr std::uint32_t protocol_version = 1;
constexpr unsigned startup_ms = 5000, operation_ms = 1000, retire_ms = 1000;
constexpr unsigned max_sessions = 4;
constexpr std::uint64_t max_file_bytes = 64ull * 1024 * 1024;
constexpr std::size_t max_path = 32768;
enum class Command : LONG { load = 1, resize, focus, unload, ping };
enum FocusAction : LONG { leave_forward = 1, leave_reverse = 2, dismiss = 4 };
// Single outstanding command. Command/event and completion/event provide publication barriers.
struct Channel {
    std::uint32_t version{protocol_version}, bytes{sizeof(Channel)};
    std::uint64_t generation{};
    wchar_t provider[40]{};
    wchar_t path[max_path]{};
    RECT rect{};
    Command command{Command::load};
    LONG reverse{};
    volatile LONG issued{}, completed{}, focus_actions{}, phase{};
    PreviewStatus status{};
    DWORD server_process{};
};
static_assert(std::is_trivially_copyable_v<Channel>);

class Handle {
public:
    HANDLE value{};
    Handle() = default;
    explicit Handle(HANDLE handle) : value(handle) {}
    ~Handle() { reset(); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept : value(other.value) { other.value = nullptr; }
    Handle& operator=(Handle&& other) noexcept {
        if (this != &other) { reset(other.value); other.value = nullptr; }
        return *this;
    }
    void reset(HANDLE handle = nullptr) {
        if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value);
        value = handle;
    }
    explicit operator bool() const { return value && value != INVALID_HANDLE_VALUE; }
};
struct Failure {
    PreviewReason reason;
    PreviewPhase phase;
    HRESULT hr;
};
inline void require(HRESULT hr, PreviewReason reason, PreviewPhase phase) {
    if (FAILED(hr)) throw Failure{reason, phase, hr};
}
}
