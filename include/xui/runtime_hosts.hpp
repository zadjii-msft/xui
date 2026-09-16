#pragma once
#include "xui/documents.hpp"
#include <array>

namespace xui {
enum class HostState { idle, loading, ready, playing, paused, stopped, suspended, error };
class NativeRuntimeHost;
class RuntimeHost : public Control {
public:
    HostState state() const { return state_; }
    const std::wstring& error() const { return error_; }
    const std::shared_ptr<InlineStatus>& status() const { return status_; }
    void on_state(std::function<void(HostState)> callback) { state_callback_ = std::move(callback); }
    std::span<const std::shared_ptr<Element>> retained_children() const override { return children_; }
    void arrange(Rect bounds) override;
    Rect visual_bounds() const;
    Rect content_bounds() const;
    // Adapter boundary. No native interface crosses the public API.
    void publish_state(HostState state, std::wstring message);
protected:
    RuntimeHost(ControlRole role, std::wstring name);
    std::optional<StyleTarget> control_style_target() const override {
        return role() == ControlRole::media_playback ? StyleTarget::media_playback : StyleTarget::web_content;
    }
    StyleStateMask control_style_state_bits() const override;
private:
    friend class NativeRuntimeHost;
    void detached() noexcept {
        if (state_ != HostState::idle && state_ != HostState::error) {
            state_ = HostState::suspended;
            try { invalidate_state(); } catch (...) {}
        }
    }
    HostState state_{HostState::idle};
    std::wstring error_;
    std::shared_ptr<InlineStatus> status_;
    std::array<std::shared_ptr<Element>, 1> children_;
    std::function<void(HostState)> state_callback_;
};
enum class MediaOperation { load, play, pause, stop, seek, volume, refresh, unload };
class MediaPlayback final : public RuntimeHost {
public:
    explicit MediaPlayback(std::wstring name = L"Media playback");
    // Explicit local files only. Network URLs and UNC paths are not accepted.
    void load_local(std::wstring path);
    const std::wstring& source() const { return source_; }
    void play();
    void pause();
    void stop();
    void seek(double seconds);
    void set_volume(double value);
    double volume() const { return volume_; }
    double duration() const { return duration_; }
    double position() const { return position_; }
    void refresh();
    void unload();
    std::uint64_t revision() const { return revision_; }
    void bind(std::function<void(MediaOperation, double)> adapter) { adapter_ = std::move(adapter); }
    void publish_position(double position, double duration) { position_ = position; duration_ = duration; }
private:
    void command(MediaOperation operation, double value = 0);
    std::wstring source_;
    std::uint64_t revision_{};
    double volume_{0.5}, duration_{}, position_{};
    std::function<void(MediaOperation, double)> adapter_;
};
enum class WebOperation { load, stop, reload, unload, focus };
class WebContent final : public RuntimeHost {
public:
    explicit WebContent(std::wstring name = L"Web content");
    void set_profile_root(std::wstring path);
    const std::wstring& profile_root() const { return profile_root_; }
    void set_allowed_origins(std::vector<std::wstring> origins);
    bool allows(std::wstring_view uri) const;
    void set_html(std::wstring html);
    void navigate(std::wstring uri);
    void stop();
    void reload();
    void unload();
    void focus_content();
    void evaluate(std::wstring script, std::function<void(std::wstring, std::wstring)> callback);
    const std::wstring& content() const { return content_; }
    bool has_source() const { return has_source_; }
    bool is_html() const { return html_; }
    std::uint64_t revision() const { return revision_; }
    void bind(std::function<void(WebOperation)> adapter,
        std::function<void(std::wstring, std::function<void(std::wstring, std::wstring)>)> evaluate);
private:
    void command(WebOperation operation);
    std::wstring profile_root_, content_;
    std::vector<std::wstring> origins_;
    bool html_{true}, has_source_{};
    std::uint64_t revision_{};
    std::function<void(WebOperation)> adapter_;
    std::function<void(std::wstring, std::function<void(std::wstring, std::wstring)>)> evaluate_;
};
}
