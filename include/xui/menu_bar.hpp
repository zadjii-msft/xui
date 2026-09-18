#pragma once

#include "xui/commands.hpp"

namespace xui {

class MenuBar final : public Control {
    struct Lifetime;
public:
    class Heading final : public Button {
    public:
        std::uint64_t owner_id() const { return owner_id_; }
        CommandId command_id() const { return command_id_; }
        const CommandRecord* current_submenu() const;
        bool selected() const;
        bool expanded() const;
        const std::wstring& display_label() const { return name(); }
        std::optional<wchar_t> mnemonic() const { return mnemonic_; }
    private:
        friend class MenuBar;
        Heading(std::weak_ptr<Lifetime> lifetime, std::uint64_t owner, CommandId command);
        std::weak_ptr<Lifetime> lifetime_;
        std::uint64_t owner_id_{};
        CommandId command_id_{};
        std::optional<wchar_t> mnemonic_;
    };

    explicit MenuBar(std::wstring name = L"Menu bar");
    ~MenuBar() override;
    void set_commands(std::shared_ptr<const CommandSet> commands);
    const std::shared_ptr<const CommandSet>& commands() const { return commands_; }
    std::shared_ptr<Button> command_button(CommandId id) const;
    std::shared_ptr<Heading> heading(CommandId id) const;
    std::span<const std::shared_ptr<Element>> retained_children() const override { return children_; }
    Size measure(Size available) override;
    void arrange(Rect bounds) override;
    void cancel() override;

    std::optional<CommandId> current() const { return current_; }
    std::optional<CommandId> expanded() const { return expanded_; }
    std::shared_ptr<Heading> adjacent(CommandId id, int delta) const;
    std::shared_ptr<Heading> mnemonic_heading(wchar_t character) const;
    // Backend transitions do not invoke commands or application callbacks.
    bool set_current(CommandId id);
    void set_expanded(std::optional<CommandId> id);
    void set_open_handler(std::function<void(CommandId)> handler);
    void synchronize_headings();

private:
    struct Lifetime { MenuBar* owner{}; };
    std::optional<StyleTarget> control_style_target() const override { return StyleTarget::command_bar; }
    bool available(CommandId id) const;
    void repair_current();
    void refresh_headings();
    void bind_heading(const std::shared_ptr<Heading>& heading);
    std::shared_ptr<Lifetime> lifetime_;
    std::shared_ptr<const CommandSet> commands_;
    std::vector<std::shared_ptr<Element>> children_;
    std::optional<CommandId> current_, expanded_;
    std::function<void(CommandId)> open_;
};

}
