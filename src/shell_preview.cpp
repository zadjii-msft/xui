#include "xui/shell_preview.hpp"
#include <stdexcept>

namespace xui {
ShellPreview::ShellPreview(std::wstring name) : Control(ControlRole::shell_preview, std::move(name), {480, 320}) {
    set_help_text(L"Installed Windows preview in a separate window. Providers can access files and network resources.");
}
void ShellPreview::command(PreviewOperation operation, bool reverse) {
    auto callback = adapter_;
    if (callback) callback(operation, reverse);
}
std::uint64_t ShellPreview::load_local(std::wstring path) {
    if (path.empty() || path.size() > 32767 || path.find(L'\0') != std::wstring::npos)
        throw std::invalid_argument("Preview path requires 1 to 32767 UTF-16 units without NUL");
    if (status_.generation == UINT64_MAX) throw std::overflow_error("Preview generation exhausted");
    source_ = std::move(path);
    status_ = {status_.generation + 1, PreviewState::loading};
    const auto generation = status_.generation;
    invalidate(Invalidation::layout);
    command(PreviewOperation::load);
    auto callback = changed_;
    if (callback) callback(status_);
    return generation;
}
void ShellPreview::cancel(std::uint64_t generation) {
    if (generation == status_.generation) unload();
}
void ShellPreview::unload() {
    if (status_.generation == UINT64_MAX) throw std::overflow_error("Preview generation exhausted");
    const bool requested = !source_.empty() && bool(adapter_);
    source_.clear();
    status_ = {status_.generation + 1, PreviewState::idle, PreviewReason::cancelled,
        PreviewPhase::unload, 0, requested ? PreviewCleanup::pending : PreviewCleanup::none};
    command(PreviewOperation::unload);
    invalidate(Invalidation::paint);
    auto callback = changed_;
    if (callback) callback(status_);
}
void ShellPreview::focus_content(bool reverse) { command(PreviewOperation::focus, reverse); }
bool ShellPreview::publish(PreviewStatus status) {
    if (status.generation != status_.generation || status == status_) return false;
    status_ = status;
    invalidate(Invalidation::paint);
    auto callback = changed_;
    if (callback) callback(status);
    return true;
}
}
