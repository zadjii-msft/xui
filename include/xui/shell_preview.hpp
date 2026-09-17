#pragma once
#include "xui/controls.hpp"

namespace xui {
enum class PreviewState : std::uint32_t { idle, loading, accepted, unsupported, failed, retiring };
enum class PreviewReason : std::uint32_t {
    none, no_handler, restricted, unsupported_provider, missing_provider, initialization_failed,
    render_failed, timed_out, broker_failed, resource_limit, cancelled, hidden, unsupported_architecture, activation_failed
};
enum class PreviewPhase : std::uint32_t { none, policy, discovery, activation, initialize, render, resize, focus, unload };
enum class PreviewCleanup : std::uint32_t { none, pending, unloaded, broker_terminated, provider_unknown };
struct PreviewStatus {
    std::uint64_t generation{};
    PreviewState state{};
    PreviewReason reason{};
    PreviewPhase phase{};
    std::int32_t hresult{};
    PreviewCleanup cleanup{};
    bool operator==(const PreviewStatus&) const = default;
};
enum class PreviewOperation { load, unload, focus };

// Explicit installed-handler opt-in in an independent broker window, not an embedded surface or sandbox.
class ShellPreview final : public Control {
public:
    explicit ShellPreview(std::wstring name = L"Installed file preview");
    std::uint64_t load_local(std::wstring path);
    void cancel(std::uint64_t generation);
    void unload();
    void focus_content(bool reverse = false);
    const std::wstring& source() const { return source_; }
    const PreviewStatus& status() const { return status_; }
    void on_changed(std::function<void(PreviewStatus)> callback) { changed_ = std::move(callback); }
    // Native adapter boundary. Obsolete delivery cannot change state.
    void bind(std::function<void(PreviewOperation, bool)> adapter) { adapter_ = std::move(adapter); }
    bool publish(PreviewStatus status);
private:
    void command(PreviewOperation operation, bool reverse = false);
    std::wstring source_;
    PreviewStatus status_;
    std::function<void(PreviewOperation, bool)> adapter_;
    std::function<void(PreviewStatus)> changed_;
};
}
