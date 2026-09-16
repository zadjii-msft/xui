#include "xui/runtime_hosts.hpp"
#include "style_hosts_geometry.hpp"
#include <algorithm>
#include <cmath>
#include <cwctype>
#include <stdexcept>

namespace xui {
namespace {
void local_path(const std::wstring& path) {
    if (path.size() < 3 || path.size() > 2048 || path[1] != L':' || path[2] != L'\\' ||
        !((path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z')) ||
        path.find_first_of(L"\r\n") != std::wstring::npos || path.find(L'\0') != std::wstring::npos ||
        path.find(L':', 2) != std::wstring::npos)
        throw std::invalid_argument("An absolute local drive path is required");
}
}
RuntimeHost::RuntimeHost(ControlRole role, std::wstring name) : Control(role, std::move(name), {480, 300}),
    status_(std::make_shared<InlineStatus>(L"Not loaded")), children_{status_} { adopt(status_); }
StyleStateMask RuntimeHost::control_style_state_bits() const {
    constexpr StyleStateMask states[]{style_states::idle, style_states::loading, style_states::ready,
        style_states::playing, style_states::paused, style_states::stopped, style_states::suspended, style_states::error};
    return Control::control_style_state_bits() | states[static_cast<unsigned>(state_)];
}
Rect RuntimeHost::content_bounds() const {
    const auto b = bounds();
    const auto* root = effective_control_style_values(StylePart::root);
    return host_content_rect({0, 0, b.width, b.height}, root, {},
        root && visual_style() == VisualStyle::winui ? Insets{1, 1, 1, 1} : Insets{});
}
Rect RuntimeHost::visual_bounds() const {
    const auto b = content_bounds();
    return {b.x, b.y, b.width, std::max(0.0f, b.height - 44)};
}
void RuntimeHost::arrange(Rect b) {
    Control::arrange(b); const auto v = visual_bounds(); const auto content = content_bounds();
    status_->arrange({b.x + v.x, b.y + v.y + v.height, v.width, content.height - v.height});
}
void RuntimeHost::publish_state(HostState state, std::wstring message) {
    if (message.size() > 4096) message.resize(4096);
    if (state_ == state && status_->name() == message) return;
    state_ = state; error_ = state == HostState::error ? message : L"";
    status_->set_message(std::move(message), state == HostState::error ? StatusSeverity::error : StatusSeverity::information);
    invalidate_state();
    auto callback = state_callback_; if (callback) callback(state);
}
MediaPlayback::MediaPlayback(std::wstring name) : RuntimeHost(ControlRole::media_playback, std::move(name)) {
    set_help_text(L"Windows Media Foundation. Use separate Play, Pause, Stop, Seek, and Volume controls. Hiding unloads the file.");
}
void MediaPlayback::load_local(std::wstring path) {
    local_path(path); source_ = std::move(path); ++revision_; command(MediaOperation::load);
}
void MediaPlayback::command(MediaOperation op, double value) { auto callback = adapter_; if (callback) callback(op, value); }
void MediaPlayback::play() { command(MediaOperation::play); }
void MediaPlayback::pause() { command(MediaOperation::pause); }
void MediaPlayback::stop() { command(MediaOperation::stop); }
void MediaPlayback::seek(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0 || seconds > 86400 * 7) throw std::invalid_argument("Seek must be between zero and seven days");
    command(MediaOperation::seek, seconds);
}
void MediaPlayback::set_volume(double value) {
    if (!std::isfinite(value) || value < 0 || value > 1) throw std::invalid_argument("Volume must be between zero and one");
    volume_ = value; command(MediaOperation::volume, value);
}
void MediaPlayback::refresh() { command(MediaOperation::refresh); }
void MediaPlayback::unload() { source_.clear(); ++revision_; duration_ = position_ = 0; command(MediaOperation::unload); }
WebContent::WebContent(std::wstring name) : RuntimeHost(ControlRole::web_content, std::move(name)) {
    set_help_text(L"Optional Microsoft Edge WebView2. Explicit owned HTML or allowed HTTPS origins only. Hiding unloads the browser.");
}
void WebContent::set_profile_root(std::wstring path) {
    local_path(path);
    if (state() != HostState::idle && state() != HostState::suspended && state() != HostState::error) throw std::logic_error("Unload the browser before changing its profile root");
    profile_root_ = std::move(path);
}
void WebContent::set_allowed_origins(std::vector<std::wstring> origins) {
    if (origins.size() > 16) throw std::length_error("At most sixteen web origins are supported");
    for (auto& origin : origins) {
        if (origin.size() < 9 || origin.size() > 256 || origin.substr(0, 8) != L"https://" ||
            origin.find_first_of(L"/\\?#@ \t\r\n", 8) != std::wstring::npos ||
            origin.find(L'\0') != std::wstring::npos) throw std::invalid_argument("Web origins require an exact HTTPS scheme and authority without a trailing slash");
        std::transform(origin.begin(), origin.end(), origin.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
        if (origin.ends_with(L":443")) origin.resize(origin.size() - 4);
    }
    origins_ = std::move(origins);
}
bool WebContent::allows(std::wstring_view uri) const {
    if (uri == L"about:blank") return true;
    if (uri.size() > 4096 || uri.find_first_of(L"\\@\r\n\t") != std::wstring_view::npos || uri.find(L'\0') != std::wstring_view::npos) return false;
    const auto end = uri.find_first_of(L"/?#", 8);
    std::wstring origin(uri.substr(0, end));
    std::transform(origin.begin(), origin.end(), origin.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    if (origin.ends_with(L":443")) origin.resize(origin.size() - 4);
    return std::find(origins_.begin(), origins_.end(), origin) != origins_.end();
}
void WebContent::set_html(std::wstring html) {
    if (html.size() > 256 * 1024 || html.find(L'\0') != std::wstring::npos) throw std::length_error("Owned HTML must not exceed 262144 UTF-16 units or contain nulls");
    content_ = std::move(html); html_ = true; has_source_ = true; ++revision_; command(WebOperation::load);
}
void WebContent::navigate(std::wstring uri) {
    if (!allows(uri)) throw std::invalid_argument("Web navigation is outside the explicit origin allowlist");
    content_ = std::move(uri); html_ = false; has_source_ = true; ++revision_; command(WebOperation::load);
}
void WebContent::command(WebOperation operation) { auto callback = adapter_; if (callback) callback(operation); }
void WebContent::stop() { command(WebOperation::stop); }
void WebContent::reload() { command(WebOperation::reload); }
void WebContent::unload() { content_.clear(); has_source_ = false; ++revision_; command(WebOperation::unload); }
void WebContent::focus_content() { command(WebOperation::focus); }
void WebContent::evaluate(std::wstring script, std::function<void(std::wstring, std::wstring)> callback) {
    if (script.size() > 65536 || !callback) throw std::invalid_argument("Script requires a callback and at most 65536 units");
    auto adapter = evaluate_;
    if (adapter) adapter(std::move(script), std::move(callback));
    else callback({}, L"Web content is not attached");
}
void WebContent::bind(std::function<void(WebOperation)> adapter,
    std::function<void(std::wstring, std::function<void(std::wstring, std::wstring)>)> evaluate) {
    adapter_ = std::move(adapter); evaluate_ = std::move(evaluate);
}
}
