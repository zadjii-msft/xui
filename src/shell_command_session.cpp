#include "xui/shell_commands.hpp"
#include <set>
#include <thread>
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

struct CustomShellMenu::State {
    const std::thread::id thread{std::this_thread::get_id()};
    std::shared_ptr<ShellCommandSession> session;
    std::function<bool()> valid;
    std::function<void()> windows;
    bool available{true}, committed{};
    void check_thread() const {
        if (thread != std::this_thread::get_id()) throw std::logic_error("Custom Shell menus belong to their UI thread");
    }
    void cancel() { available = false; session.reset(); windows = {}; }
    bool current() {
        check_thread();
        if (!available) return false;
        if (valid && !valid()) { cancel(); return false; }
        return true;
    }
    bool take() { if (!current()) return false; available = false; return true; }
    void shell(ItemKey key) {
        if (!take()) return;
        auto selected = std::move(session); windows = {};
        if (!selected || !selected->invoke(key)) throw std::logic_error("Shell command is no longer invocable");
    }
    void app(const std::function<void()>& action) {
        if (!take()) return;
        session.reset(); windows = {};
        if (action) action();
    }
    void fallback() {
        if (!take()) return;
        auto action = std::move(windows); session.reset();
        if (action) action();
    }
};
namespace {
std::wstring menu_label(std::wstring_view text) {
    std::wstring result;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == L'&' && i + 1 < text.size()) {
            if (text[i + 1] != L'&') continue;
            ++i;
        }
        result += text[i];
    }
    return result;
}
}
CustomShellMenu::CustomShellMenu(std::shared_ptr<ShellCommandProvider> provider, const std::vector<MenuItem>& apps,
    std::function<bool()> current, std::function<void()> windows_menu) : state_(std::make_shared<State>()) {
    if (!windows_menu) throw std::invalid_argument("A Windows menu fallback is required");
    if (apps.size() > CommandSet::maximum_commands - 4) throw std::length_error("Too many app commands for a custom Shell menu");
    state_->valid = std::move(current); state_->windows = std::move(windows_menu);
    state_->session = std::make_shared<ShellCommandSession>(std::move(provider));
    std::vector<CommandRecord> records;
    const auto fallback = [state = state_] { state->fallback(); };
    const auto append = [&](CommandRecord record) {
        record.id = records.size() + 1; records.push_back(std::move(record)); return records.back().id;
    };
    bool overflow{};
    const auto budget = CommandSet::maximum_commands - apps.size() - 4;
    if (state_->current()) {
        try { state_->session->discover(); }
        catch (const std::bad_alloc&) { throw; }
        catch (const std::exception& error) { error_ = error.what(); }
    }
    if (state_->session && error_.empty()) {
        const auto emit = [&](const auto& self, const std::vector<ShellCommandInfo>& items, CommandId parent, unsigned depth) -> void {
            for (const auto& item : items) {
                if (records.size() >= budget) { overflow = true; return; }
                CommandRecord record;
                record.parent = parent; record.enabled = item.enabled;
                if (item.checked) record.checked = true;
                record.label = menu_label(item.label); record.shortcut_hints = item.shortcut_hints;
                record.icon = item.icon;
                if (item.separator) { record.kind = CommandKind::separator; record.checked.reset(); append(std::move(record)); continue; }
                const bool incompatible = item.native_only || record.label.empty() || depth >= 7;
                if (incompatible) {
                    if (record.label.empty()) record.label = L"Windows command";
                    if (record.label.size() > 990) record.label = L"Long command label";
                    record.label += L" (Windows menu...)"; record.action = fallback;
                    append(std::move(record));
                } else if (!item.children.empty()) {
                    record.kind = CommandKind::submenu;
                    const auto index = records.size(), id = append(std::move(record));
                    self(self, item.children, id, depth + 1);
                    if (records.size() == index + 1) {
                        records[index].kind = CommandKind::action; records[index].action = fallback;
                        records[index].label = L"Show group in Windows menu...";
                    }
                } else {
                    record.action = [state = state_, key = item.key] { state->shell(key); };
                    append(std::move(record));
                }
            }
        };
        emit(emit, state_->session->commands(), 0, 0);
    }
    if (!error_.empty() || overflow) {
        CommandRecord notice;
        notice.label = error_.empty() ? L"Additional commands are in the Windows menu" : L"Shell discovery failed - use the Windows menu";
        notice.kind = CommandKind::section; notice.enabled = false; append(std::move(notice));
    }
    const auto separator = [&] { CommandRecord record; record.kind = CommandKind::separator; append(std::move(record)); };
    if (!records.empty() && !apps.empty()) separator();
    for (const auto& item : apps) {
        CommandRecord record;
        const auto tab = item.text.find(L'\t');
        record.label = menu_label(std::wstring_view(item.text).substr(0, tab));
        if (tab != std::wstring::npos) record.shortcut_hints.push_back(item.text.substr(tab + 1));
        record.enabled = item.enabled;
        if (item.separator) record.kind = CommandKind::separator;
        else {
            if (item.checked) record.checked = true;
            record.action = [state = state_, action = item.action] { state->app(action); };
        }
        append(std::move(record));
    }
    if (!records.empty()) separator();
    CommandRecord native; native.label = L"Show Windows menu..."; native.action = fallback; append(std::move(native));
    commands_ = std::make_shared<CommandSet>(std::move(records));
}
CustomShellMenu::~CustomShellMenu() { if (!state_->committed) state_->cancel(); }
std::vector<MenuItem> CustomShellMenu::menu_items() const {
    state_->check_thread();
    std::vector<MenuItem> items;
    for (const auto& record : commands_->records()) {
        if (record.parent) continue;
        MenuItem item;
        for (const auto character : record.label) {
            if (character == L'&') item.text += L'&';
            item.text += character;
        }
        if (record.kind == CommandKind::submenu) {
            item.text += L" (Windows menu...)";
            item.action = [state = state_] { state->fallback(); };
        } else if (record.kind == CommandKind::action) {
            item.action = [commands = commands_, id = record.id] { commands->invoke(id); };
        }
        if (!record.shortcut_hints.empty()) {
            item.text += L'\t';
            for (std::size_t i = 0; i < record.shortcut_hints.size(); ++i) {
                if (i) item.text += L" / ";
                item.text += record.shortcut_hints[i];
            }
        }
        item.enabled = record.enabled && record.kind != CommandKind::section;
        item.checked = record.checked.value_or(false);
        item.separator = record.kind == CommandKind::separator;
        items.push_back(std::move(item));
    }
    return items;
}
bool CustomShellMenu::current() const { return state_->current(); }
void CustomShellMenu::dismiss(PopupDismissReason reason) {
    state_->check_thread();
    if (reason == PopupDismissReason::commit) state_->committed = true;
    else state_->cancel();
}
}
