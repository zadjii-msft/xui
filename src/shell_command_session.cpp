#include "xui/shell_commands.hpp"
#include <set>
namespace xui {
struct ShellCommandSession::State {
    std::shared_ptr<ShellCommandProvider> provider;
    std::vector<ShellCommandInfo> commands;
    std::stop_source stop;
};
ShellCommandSession::ShellCommandSession(std::shared_ptr<ShellCommandProvider> provider) : state_(std::make_shared<State>()) {
    if (!provider) throw std::invalid_argument("Shell command provider is null");
    state_->provider = std::move(provider);
}
ShellCommandSession::~ShellCommandSession() { cancel(); }
const std::vector<ShellCommandInfo>& ShellCommandSession::commands() const { return state_->commands; }
void ShellCommandSession::discover() {
    const auto state = state_;
    state->stop.request_stop(); state->stop = std::stop_source{};
    state->commands.clear();
    const auto token = state->stop.get_token();
    auto provider = state->provider;
    std::vector<ShellCommandInfo> commands;
    try { commands = provider->discover(token); } catch (...) {
        if (token == state->stop.get_token()) { state->stop.request_stop(); state->commands.clear(); }
        throw;
    }
    if (token.stop_requested() || token != state->stop.get_token()) return;
    std::set<ItemKey> keys; std::size_t count{};
    const auto check = [&](const auto& self, const std::vector<ShellCommandInfo>& items, unsigned depth) -> void {
        if (depth > 8) throw std::length_error("Shell menu nesting exceeds eight");
        for (const auto& item : items) {
            if (++count > 4096 || item.label.size() > 1024 || item.verb.size() > 1024) throw std::length_error("Shell menu exceeds its limit");
            if (item.shortcut_hints.size() > 8) throw std::length_error("Too many Shell shortcut hints");
            for (const auto& hint : item.shortcut_hints) if (hint.size() > 64) throw std::length_error("Shell shortcut hint exceeds its limit");
            if (!item.key.id || !keys.insert(item.key).second) throw std::invalid_argument("Shell command identity is invalid");
            self(self, item.children, depth + 1);
        }
    };
    try { check(check, commands, 0); } catch (...) { state->stop.request_stop(); state->commands.clear(); throw; }
    state->commands = std::move(commands);
}
bool ShellCommandSession::invoke(ItemKey key) {
    const auto state = state_;
    if (state->stop.stop_requested()) return false;
    bool found{};
    const auto visit = [&](const auto& self, const std::vector<ShellCommandInfo>& items, bool enabled) -> void {
        for (const auto& item : items) {
            if (item.key == key) found = enabled && item.enabled && !item.separator && item.children.empty() && !item.native_only;
            self(self, item.children, enabled && item.enabled);
        }
    };
    visit(visit, state->commands, true); if (!found) return false;
    auto provider = state->provider; cancel(); provider->invoke(key); return true;
}
void ShellCommandSession::cancel() { state_->stop.request_stop(); state_->commands.clear(); }
}
