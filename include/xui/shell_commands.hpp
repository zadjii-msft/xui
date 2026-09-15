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
    bool has_native_icon{}; // Bitmap metadata alone does not prevent text-only custom presentation.
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
// UI-thread model. Actions retain original Shell identities, never reconstructed verbs.
class CustomShellMenu final {
public:
    CustomShellMenu(std::shared_ptr<ShellCommandProvider> provider, const std::vector<MenuItem>& app_commands,
        std::function<bool()> current, std::function<void()> windows_menu);
    ~CustomShellMenu();
    const std::shared_ptr<const CommandSet>& commands() const { return commands_; }
    std::vector<MenuItem> menu_items() const;
    const std::string& discovery_error() const { return error_; }
    bool current() const;
    void dismiss(PopupDismissReason reason);
private:
    struct State;
    std::shared_ptr<State> state_;
    std::shared_ptr<const CommandSet> commands_;
    std::string error_;
};
#ifdef _WIN32
// native_owner is an HWND on the calling STA thread. Paths must share a parent.
// Native fallback preserves third-party owner-drawn items and dynamic submenu messages.
std::shared_ptr<ShellCommandProvider> shell_command_provider(void* native_owner, const std::vector<std::wstring>& paths);
// Explicit user interaction only. A returned key identifies the selected verb.
std::optional<ItemKey> track_shell_commands(void* native_owner, const std::vector<std::wstring>& paths, Point screen_pixels);
// App commands share the native top-level menu. current rejects stale selections before any action.
std::optional<ItemKey> track_shell_commands(void* native_owner, const std::vector<std::wstring>& paths, Point screen_pixels,
    const std::vector<MenuItem>& app_commands, std::function<bool()> current);
std::optional<ItemKey> track_shell_commands(std::shared_ptr<ShellCommandProvider> provider, Point screen_pixels,
    const std::vector<MenuItem>& app_commands, std::function<bool()> current);
#endif
}
