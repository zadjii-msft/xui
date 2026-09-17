#include "xui/menu_bar.hpp"
#include "layout_styling.hpp"
#include <algorithm>
#include <cwctype>

namespace xui {
namespace {
std::pair<std::wstring, std::optional<wchar_t>> menu_label(const std::wstring& label) {
    std::wstring display;
    std::optional<wchar_t> mnemonic;
    for (std::size_t i = 0; i < label.size(); ++i) {
        if (label[i] == L'&' && i + 1 < label.size()) {
            if (label[i + 1] != L'&' && !mnemonic)
                mnemonic = static_cast<wchar_t>(std::towupper(label[i + 1]));
            display += label[++i];
        } else display += label[i];
    }
    return {std::move(display), mnemonic};
}
}

MenuBar::Heading::Heading(std::weak_ptr<Lifetime> lifetime, std::uint64_t owner, CommandId command)
    : Button(L""), lifetime_(std::move(lifetime)), owner_id_(owner), command_id_(command) {
    set_appearance(ButtonAppearance::subtle);
    set_tab_stop(false);
}

const CommandRecord* MenuBar::Heading::current_submenu() const {
    const auto lifetime = lifetime_.lock();
    const auto* owner = lifetime ? lifetime->owner : nullptr;
    if (!owner || owner->heading(command_id_).get() != this || !owner->commands_) return nullptr;
    return owner->commands_->find(command_id_);
}
bool MenuBar::Heading::selected() const {
    const auto lifetime = lifetime_.lock();
    return current_submenu() && lifetime && lifetime->owner && lifetime->owner->current_ == command_id_;
}
bool MenuBar::Heading::expanded() const {
    const auto lifetime = lifetime_.lock();
    return current_submenu() && lifetime && lifetime->owner && lifetime->owner->expanded_ == command_id_;
}

MenuBar::MenuBar(std::wstring name)
    : Control(ControlRole::content_view, std::move(name), {480, 40}), lifetime_(std::make_shared<Lifetime>()) {
    lifetime_->owner = this;
    set_tab_stop(false);
    set_auto_size(true);
}
MenuBar::~MenuBar() {
    lifetime_->owner = nullptr;
    for (const auto& child : children_) {
        const auto button = std::static_pointer_cast<Heading>(child);
        button->on_click({});
        button->on_focus({});
        button->set_enabled(false);
    }
}

std::shared_ptr<MenuBar::Heading> MenuBar::heading(CommandId id) const {
    for (const auto& child : children_) {
        const auto value = std::static_pointer_cast<Heading>(child);
        if (value->command_id() == id) return value;
    }
    return {};
}
std::shared_ptr<Button> MenuBar::command_button(CommandId id) const { return heading(id); }

bool MenuBar::available(CommandId id) const {
    const auto button = heading(id);
    return enabled() && visible() && commands_ && commands_->enabled(id) &&
        button && button->enabled() && button->visible();
}
void MenuBar::bind_heading(const std::shared_ptr<Heading>& button) {
    const auto dispatch = [weak = std::weak_ptr<Lifetime>(lifetime_), target = std::weak_ptr<Heading>(button),
        snapshot = std::weak_ptr<const CommandSet>(commands_)](bool open) {
        const auto lifetime = weak.lock();
        const auto value = target.lock();
        auto* owner = lifetime ? lifetime->owner : nullptr;
        if (!owner || !value || owner->commands_ != snapshot.lock() ||
            owner->heading(value->command_id()) != value || !owner->available(value->command_id())) return;
        owner->set_current(value->command_id());
        // Copy before calling the host: opening may replace or destroy the bar.
        const auto callback = open ? owner->open_ : std::function<void(CommandId)>{};
        if (callback) callback(value->command_id());
    };
    button->on_click([dispatch] { dispatch(true); });
    button->on_focus([dispatch] { dispatch(false); });
}
void MenuBar::set_open_handler(std::function<void(CommandId)> handler) { open_ = std::move(handler); }

void MenuBar::set_commands(std::shared_ptr<const CommandSet> commands) {
    if (!commands) throw std::invalid_argument("Menu bar requires a command set");
    std::vector<const CommandRecord*> roots;
    for (const auto& record : commands->records()) if (!record.parent) {
        if (record.kind != CommandKind::submenu)
            throw std::invalid_argument("Menu bar roots must be submenu groups");
        roots.push_back(&record);
    }
    if (roots.size() > 64) throw std::length_error("Menu bar supports at most 64 root groups");
    if (commands_ == commands) return;
    std::vector<std::shared_ptr<Element>> next;
    next.reserve(roots.size());
    for (const auto* record : roots) {
        auto button = heading(record->id);
        if (!button) {
            button = std::shared_ptr<Heading>(new Heading(lifetime_, Element::id(), record->id));
            adopt(button);
        }
        next.push_back(std::move(button));
    }
    for (const auto& child : children_) if (std::find(next.begin(), next.end(), child) == next.end()) {
        const auto retired = std::static_pointer_cast<Heading>(child);
        retired->on_click({});
        retired->on_focus({});
        retired->set_tab_stop(false);
        retired->set_checked(false);
        retired->set_enabled(false);
    }
    expanded_.reset();
    commands_ = std::move(commands);
    children_ = std::move(next);
    for (std::size_t i = 0; i < roots.size(); ++i) {
        const auto button = std::static_pointer_cast<Heading>(children_[i]);
        auto [display, mnemonic] = menu_label(roots[i]->label);
        button->set_name(std::move(display));
        button->mnemonic_ = mnemonic;
        button->set_enabled(roots[i]->enabled);
        bind_heading(button);
    }
    repair_current();
    refresh_headings();
    invalidate(Invalidation::layout);
}

void MenuBar::repair_current() {
    // Keep the roving stop while the entire bar is disabled or hidden.
    const auto valid = [&](CommandId id) {
        const auto button = heading(id);
        return commands_ && commands_->enabled(id) && button && button->enabled() && button->visible();
    };
    if (current_ && valid(*current_)) return;
    current_.reset();
    for (const auto& child : children_) {
        const auto button = std::static_pointer_cast<Heading>(child);
        if (valid(button->command_id())) { current_ = button->command_id(); break; }
    }
}
void MenuBar::refresh_headings() {
    for (const auto& child : children_) {
        const auto button = std::static_pointer_cast<Heading>(child);
        button->set_tab_stop(current_ == button->command_id());
        button->set_checked(expanded_ == button->command_id());
    }
}
void MenuBar::synchronize_headings() {
    repair_current();
    if (expanded_ && !available(*expanded_)) expanded_.reset();
    refresh_headings();
}
bool MenuBar::set_current(CommandId id) {
    if (!available(id)) return false;
    if (current_ == id) return true;
    current_ = id;
    expanded_.reset();
    refresh_headings();
    invalidate(Invalidation::paint);
    return true;
}
void MenuBar::set_expanded(std::optional<CommandId> id) {
    if (id && !available(*id)) throw std::invalid_argument("Menu heading is not available");
    if (id) set_current(*id);
    if (expanded_ == id) return;
    expanded_ = id;
    refresh_headings();
    invalidate(Invalidation::paint);
}
void MenuBar::cancel() {
    Control::cancel();
    set_expanded({});
}
std::shared_ptr<MenuBar::Heading> MenuBar::adjacent(CommandId id, int delta) const {
    if (!delta || children_.empty()) return {};
    std::size_t position = delta > 0 ? children_.size() - 1 : 0;
    for (std::size_t i = 0; i < children_.size(); ++i)
        if (std::static_pointer_cast<Heading>(children_[i])->command_id() == id) { position = i; break; }
    for (std::size_t i = 0; i < children_.size(); ++i) {
        position = delta > 0 ? (position + 1) % children_.size() : (position + children_.size() - 1) % children_.size();
        const auto button = std::static_pointer_cast<Heading>(children_[position]);
        if (available(button->command_id())) return button;
    }
    return {};
}
std::shared_ptr<MenuBar::Heading> MenuBar::mnemonic_heading(wchar_t character) const {
    character = static_cast<wchar_t>(std::towupper(character));
    // Duplicate mnemonics cycle from the current heading.
    auto button = adjacent(current_.value_or(0), 1);
    for (std::size_t i = 0; button && i < children_.size(); ++i) {
        if (button->mnemonic() == character) return button;
        button = adjacent(button->command_id(), 1);
    }
    return {};
}
Size MenuBar::measure(Size available_size) {
    if (!visible()) return {};
    if (!auto_size()) return Element::measure(available_size);
    const auto insets = layout_style::insets(effective_control_style_values(StylePart::root));
    const auto inner = layout_style::inner(available_size, insets);
    Size desired{0, 40};
    for (const auto& child : children_) {
        const auto button = std::static_pointer_cast<Heading>(child);
        if (!button->visible()) continue;
        const auto size = button->measure(inner);
        desired.width += size.width;
        desired.height = std::max(desired.height, size.height);
    }
    return constrain(layout_style::outer(desired, insets), available_size);
}
void MenuBar::arrange(Rect bounds) {
    Element::arrange(bounds);
    const auto content = layout_style::content(*this, this->bounds());
    synchronize_headings();
    float desired{};
    for (const auto& child : children_)
        if (std::static_pointer_cast<Heading>(child)->visible())
            desired += child->measure({content.width, content.height}).width;
    const float scale = desired > content.width && desired > 0 ? content.width / desired : 1;
    float x = content.x;
    for (const auto& child : children_) {
        if (!std::static_pointer_cast<Heading>(child)->visible()) { child->arrange({}); continue; }
        const float width = std::min(std::max(0.0f, content.x + content.width - x),
            child->measure({content.width, content.height}).width * scale);
        child->arrange({x, content.y, width, content.height});
        x += width;
    }
}

}
