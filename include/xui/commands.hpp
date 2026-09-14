#pragma once

#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include "xui/collections.hpp"
#include "xui/foundation.hpp"

namespace xui {

using CommandId = std::uint64_t;
enum class CommandKind { action, submenu, separator, section };
struct CommandRecord {
    CommandId id{}, parent{};
    std::wstring label;
    std::function<void()> action;
    bool enabled{true};
    std::optional<bool> checked;
    ButtonIcon icon{ButtonIcon::none};
    std::vector<std::wstring> shortcut_hints;
    std::wstring pin_label;
    std::function<void()> pin;
    CommandKind kind{CommandKind::action};
};
class CommandSet final {
public:
    static constexpr std::size_t maximum_commands = 4096;
    explicit CommandSet(std::vector<CommandRecord> records);
    const std::vector<CommandRecord>& records() const { return records_; }
    const CommandRecord* find(CommandId id) const;
    bool enabled(CommandId id) const;
    bool invoke(CommandId id, bool pin = false) const;
private:
    std::vector<CommandRecord> records_;
    std::map<CommandId, std::size_t> index_;
};
struct CommandShortcut {
    std::uint16_t virtual_key{};
    bool control{}, shift{}, alt{};
    auto operator<=>(const CommandShortcut&) const = default;
};
// Explicit bindings are independent of presentation hints.
class CommandBindings final {
public:
    void bind(CommandShortcut shortcut, CommandId command);
    bool invoke(const CommandSet& commands, CommandShortcut shortcut) const;
private:
    std::map<CommandShortcut, CommandId> bindings_;
};
struct CommandQuery {
    std::wstring text;
    std::uint64_t generation{};
    std::stop_token cancellation;
};
class CommandMenu final : public VirtualCollection {
public:
    explicit CommandMenu(std::wstring name = L"Commands");
    ~CommandMenu() override;
    void set_commands(std::shared_ptr<const CommandSet> commands, CommandId parent = 0, std::wstring query = {});
    const std::shared_ptr<const CommandSet>& commands() const { return commands_; }
    CommandId parent() const { return parent_; }
    std::optional<CommandId> expanded() const { return expanded_; }
    void set_expanded(std::optional<CommandId> command);
    bool execute(CommandId id, bool pin = false);
    std::vector<CollectionRow> visible_content() const override;
    bool select(ItemKey key, SelectionGesture gesture = SelectionGesture::replace) override;
    void select_all() override {}
    void step(int delta, SelectionGesture gesture = SelectionGesture::replace) override;
    void edge(bool last, SelectionGesture gesture = SelectionGesture::replace) override;
    bool disclose(ItemKey key, bool expanded) override;
    void horizontal(bool right, SelectionGesture gesture) override;
    void on_submenu(std::function<void(CommandId)> callback) { submenu_ = std::move(callback); }
    void on_collapse(std::function<void(CommandId)> callback) { collapse_ = std::move(callback); }
    void on_back(std::function<void()> callback) { back_ = std::move(callback); }
    void on_accept(std::function<void()> callback) { accept_ = std::move(callback); }
private:
    std::shared_ptr<const CommandSet> commands_;
    CommandId parent_{};
    std::wstring query_text_;
    std::optional<CommandId> expanded_;
    std::function<void(CommandId)> submenu_, collapse_;
    std::function<void()> back_, accept_;
};
// One outstanding query. Completion belongs to the UI thread.
class CommandSurface final {
public:
    explicit CommandSurface(std::wstring name = L"Commands", bool searchable = true);
    ~CommandSurface();
    const std::shared_ptr<Popup>& popup() const { return popup_; }
    const std::shared_ptr<CommandMenu>& menu() const { return menu_; }
    const std::shared_ptr<TextInput>& editor() const { return editor_; }
    Size measure(Size available) const;
    void set_commands(std::shared_ptr<const CommandSet> commands, CommandId parent = 0);
    void on_query(std::function<void(CommandQuery)> callback) { query_ = std::move(callback); }
    CommandQuery request(std::wstring text);
    bool complete(const CommandQuery& request, std::shared_ptr<const CommandSet> commands, std::wstring error = {});
    void cancel();
    const std::wstring& error() const { return error_; }
private:
    std::shared_ptr<Popup> popup_;
    std::shared_ptr<CommandMenu> menu_;
    std::shared_ptr<TextInput> editor_;
    std::shared_ptr<Button> close_;
    std::shared_ptr<Label> status_;
    std::shared_ptr<const CommandSet> commands_;
    std::stop_source stop_;
    std::uint64_t generation_{};
    std::wstring text_, error_;
    std::function<void(CommandQuery)> query_;
};
class CommandBar final : public Control {
public:
    explicit CommandBar(std::wstring name = L"Command bar");
    ~CommandBar() override;
    void set_commands(std::shared_ptr<const CommandSet> commands);
    std::span<const std::shared_ptr<Element>> retained_children() const override { return children_; }
    void arrange(Rect bounds) override;
    const std::shared_ptr<Button>& overflow_button() const { return overflow_; }
    std::shared_ptr<const CommandSet> overflow_commands() const;
    void on_overflow(std::function<void()> callback);
private:
    std::shared_ptr<const CommandSet> commands_;
    std::vector<std::shared_ptr<Element>> children_;
    std::vector<CommandId> ids_;
    std::shared_ptr<Button> overflow_;
    std::size_t visible_{};
};

struct Command {
    std::wstring label;
    std::function<bool()> enabled;
    std::function<void()> execute;
};

class Commands {
public:
    void add(unsigned id, Command command) {
        if (!commands_.emplace(id, std::move(command)).second)
            throw std::invalid_argument("Duplicate command ID");
    }
    const Command& get(unsigned id) const { return commands_.at(id); }
    bool invoke(unsigned id) const {
        const auto& command = get(id);
        if (!command.enabled()) return false;
        command.execute();
        return true;
    }
private:
    std::map<unsigned, Command> commands_;
};

}
