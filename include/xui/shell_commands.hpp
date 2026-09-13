#pragma once
#include "xui/commands.hpp"

namespace xui {
struct ShellCommandInfo {
    ItemKey key;
    std::wstring label, verb;
    bool enabled{true}, checked{}, separator{}, native_only{};
    std::vector<ShellCommandInfo> children;
    ButtonIcon icon{ButtonIcon::none};
    std::vector<std::wstring> shortcut_hints;
    bool has_native_icon{};
};
// Implementations own their apartment and extension objects. Discovery never invokes a verb.
class ShellCommandProvider {
public:
    virtual ~ShellCommandProvider() = default;
    virtual std::vector<ShellCommandInfo> discover(std::stop_token cancellation) = 0;
    virtual void invoke(ItemKey key) = 0;
};
class ShellCommandSession final {
public:
    explicit ShellCommandSession(std::shared_ptr<ShellCommandProvider> provider);
    ~ShellCommandSession();
    void discover();
    const std::vector<ShellCommandInfo>& commands() const;
    bool invoke(ItemKey key);
    void cancel();
private:
    struct State;
    std::shared_ptr<State> state_;
};
#ifdef _WIN32
// native_owner is an HWND on the calling STA thread. Paths must share a parent.
// Native fallback preserves third-party owner-drawn items and dynamic submenu messages.
std::shared_ptr<ShellCommandProvider> shell_command_provider(void* native_owner, const std::vector<std::wstring>& paths);
// Explicit user interaction only. A returned key identifies the selected verb.
std::optional<ItemKey> track_shell_commands(void* native_owner, const std::vector<std::wstring>& paths, Point screen_pixels);
#endif
}
