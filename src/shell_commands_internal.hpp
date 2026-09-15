#pragma once
#include "xui/shell_commands.hpp"
#include <windows.h>
#include <shobjidl.h>
#include <atomic>

namespace xui {
// Deterministic native-menu tests never select a real Shell verb.
struct ShellMenuTestAccess {
    using Track = UINT(*)(HMENU, HWND, Point);
    static std::atomic<Track> track;
    using Create = std::shared_ptr<ShellCommandProvider>(*)(HWND, const std::vector<std::wstring>&);
    // Runs on the Shell STA. Test providers must be created on that thread.
    static std::atomic<Create> create;
    static std::shared_ptr<ShellCommandProvider> provider(HWND owner, IContextMenu* context);
    static std::optional<ItemKey> run(HWND owner, IContextMenu* context, const std::vector<MenuItem>& items,
        std::function<bool()> current = {});
};
// Private gallery path: copied metadata crosses threads, never COM interfaces or callbacks.
class AsyncShellMenu final {
public:
    struct State;
    static std::shared_ptr<AsyncShellMenu> prepare(HWND owner, std::vector<std::wstring> paths);
    static std::shared_ptr<AsyncShellMenu> start(HWND owner, std::vector<std::wstring> paths);
    ~AsyncShellMenu();
    bool ready() const;
    void begin();
    bool finished() const;
    std::vector<ShellCommandInfo> commands() const;
    std::string discovery_error() const;
    void invoke(ItemKey key);
    void windows_menu(Point point, const std::vector<MenuItem>& apps);
    std::optional<size_t> result() const;
    void cancel();
    const void* identity() const { return state_.get(); }
private:
    explicit AsyncShellMenu(std::shared_ptr<State> state) : state_(std::move(state)) {}
    std::shared_ptr<State> state_;
};
UINT shell_validation_message();
}
