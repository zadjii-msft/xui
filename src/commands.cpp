#include "xui/commands.hpp"
#include <algorithm>
#include <cwctype>

namespace xui {
namespace {
constexpr auto command_help = L"↑ ↓ Select   Enter Run   Esc Close";
constexpr auto menu_help = L"↑ ↓ Select   → Submenu   F2 Pin   Esc Close";
std::wstring fold(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return value;
}
class CommandRows final : public ItemsSource {
public:
    CommandRows(std::shared_ptr<const CommandSet> set, CommandId parent, const std::wstring& query,
        std::optional<CommandId> expanded = {}) : set_(std::move(set)), expanded_(expanded) {
        const auto needle = fold(query);
        std::map<CommandId, CommandId> sections;
        std::map<CommandId, std::vector<CommandId>> matches;
        for (const auto& record : set_->records()) {
            if (record.kind == CommandKind::section) { sections[record.parent] = record.id; continue; }
            if (needle.empty() ? record.parent != parent : record.kind != CommandKind::action ||
                fold(record.label).find(needle) == std::wstring::npos) continue;
            matches[sections[record.parent]].push_back(record.id);
        }
        const auto append = [&](CommandId id) {
            index_.emplace(id, ids_.size()); ids_.push_back(id);
            const auto kind = set_->find(id)->kind;
            const auto previous = offsets_.back();
            offsets_.push_back({previous.first + (kind == CommandKind::action || kind == CommandKind::submenu ? 1 : 0),
                previous.second + (kind == CommandKind::section ? 32 : kind == CommandKind::separator ? 12 : 0)});
        };
        const auto append_section = [&](CommandId section) {
            const auto found = matches.find(section);
            if (found == matches.end()) return;
            const bool has_commands = std::any_of(found->second.begin(), found->second.end(),
                [&](auto id) { return set_->find(id)->kind != CommandKind::separator; });
            if (section && !has_commands) return;
            if (section) append(section);
            for (auto id : found->second) append(id);
        };
        append_section(0);
        for (const auto& record : set_->records()) {
            if (record.kind == CommandKind::section) append_section(record.id);
        }
    }
    double row_start(std::size_t index, double row_height) const override {
        const auto [rows, decoration] = offsets_.at(std::min(index, size()));
        return rows * row_height + decoration;
    }
    std::size_t row_at(double offset, double row_height) const override {
        std::size_t first{}, last = size();
        while (first < last) {
            const auto middle = first + (last - first) / 2;
            if (row_start(middle + 1, row_height) <= offset) first = middle + 1;
            else last = middle;
        }
        return first;
    }
    std::size_t size() const override { return ids_.size(); }
    ItemKey key(std::size_t i) const override { return {ids_.at(i), 1}; }
    std::optional<std::size_t> find(ItemKey key) const override {
        const auto it = index_.find(key.id);
        return key.version == 1 && it != index_.end() ? std::optional{it->second} : std::nullopt;
    }
    bool selectable(std::size_t i) const override {
        const auto& record = *set_->find(ids_.at(i));
        return (record.kind == CommandKind::action || record.kind == CommandKind::submenu) && set_->enabled(record.id);
    }
    ItemContent item(std::size_t i) const override {
        const auto& record = *set_->find(ids_.at(i));
        std::wstring hints;
        for (const auto& hint : record.shortcut_hints) { if (!hints.empty()) hints += L"  ·  "; hints += hint; }
        ItemContent result{record.label, std::move(hints), record.icon, {}, record.pin_label, set_->enabled(record.id)};
        result.checked = record.checked; result.separator = record.kind == CommandKind::separator;
        result.submenu = record.kind == CommandKind::submenu;
        return result;
    }
    ItemHierarchy hierarchy(std::size_t i) const override {
        ItemHierarchy result;
        result.group = set_->find(ids_.at(i))->kind == CommandKind::section;
        result.expandable = set_->find(ids_.at(i))->kind == CommandKind::submenu;
        result.expanded = expanded_ == ids_.at(i);
        return result;
    }
private:
    std::shared_ptr<const CommandSet> set_;
    std::vector<CommandId> ids_;
    std::map<CommandId, std::size_t> index_;
    std::optional<CommandId> expanded_;
    std::vector<std::pair<std::size_t, double>> offsets_{{0, 0}};
};
}
CommandSet::CommandSet(std::vector<CommandRecord> records) : records_(std::move(records)) {
    if (records_.size() > maximum_commands) throw std::length_error("Too many commands");
    for (std::size_t i = 0; i < records_.size(); ++i) {
        const auto& r = records_[i];
        if (r.kind != CommandKind::action && r.kind != CommandKind::submenu &&
            r.kind != CommandKind::separator && r.kind != CommandKind::section)
            throw std::invalid_argument("Unknown command kind");
        if (!r.id || !index_.emplace(r.id, i).second) throw std::invalid_argument("Command IDs must be unique and nonzero");
        if (r.label.size() > 1024 || r.pin_label.size() > 128 || r.shortcut_hints.size() > 8)
            throw std::length_error("Command text exceeds its limit");
        for (const auto& hint : r.shortcut_hints) if (hint.size() > 64) throw std::length_error("Shortcut hint exceeds its limit");
        if (bool(r.pin) != !r.pin_label.empty() || (r.kind != CommandKind::action && (r.action || r.pin)))
            throw std::invalid_argument("Invalid command action");
        if (r.kind == CommandKind::section && (r.checked || r.icon != ButtonIcon::none || !r.shortcut_hints.empty()))
            throw std::invalid_argument("Section headers cannot have command adornments");
        if (r.kind != CommandKind::separator && r.label.empty()) throw std::invalid_argument("Command label is empty");
    }
    for (const auto& r : records_) {
        auto parent = r.parent;
        for (unsigned depth = 0; parent; ++depth) {
            const auto* p = find(parent);
            if (!p || p->kind != CommandKind::submenu || depth >= 7 || parent == r.id)
                throw std::invalid_argument("Invalid command hierarchy");
            parent = p->parent;
        }
    }
}
const CommandRecord* CommandSet::find(CommandId id) const {
    const auto it = index_.find(id); return it == index_.end() ? nullptr : &records_[it->second];
}
bool CommandSet::enabled(CommandId id) const {
    auto record = find(id);
    if (!record) return false;
    for (; record; record = record->parent ? find(record->parent) : nullptr)
        if (!record->enabled) return false;
    return true;
}
bool CommandSet::invoke(CommandId id, bool pin) const {
    const auto* record = find(id);
    if (!record || !enabled(id) || record->kind != CommandKind::action) return false;
    auto callback = pin ? record->pin : record->action;
    if (!callback) return false;
    callback(); return true;
}
void CommandBindings::bind(CommandShortcut shortcut, CommandId command) {
    if (!shortcut.virtual_key || !command) throw std::invalid_argument("Shortcut key and command must be nonzero");
    if (bindings_.size() >= 256) throw std::length_error("At most 256 keyboard bindings are supported");
    if (!bindings_.emplace(shortcut, command).second) throw std::invalid_argument("Duplicate keyboard binding");
}
bool CommandBindings::invoke(const CommandSet& commands, CommandShortcut shortcut) const {
    const auto it = bindings_.find(shortcut);
    return it != bindings_.end() && commands.invoke(it->second);
}
CommandMenu::CommandMenu(std::wstring name) : VirtualCollection(ControlRole::command_menu, std::move(name)) {
    set_item_size({320, 48}); set_preferred_size({360, 288});
    on_activate([this](ItemKey key) { execute(key.id); });
    on_action([this](ItemKey key) { execute(key.id, true); });
}
CommandMenu::~CommandMenu() { on_activate({}); on_action({}); }
void CommandMenu::set_commands(std::shared_ptr<const CommandSet> commands, CommandId parent, std::wstring query) {
    if (!commands || query.size() > 256) throw std::invalid_argument("Invalid command source or query");
    if (parent && (!commands->find(parent) || commands->find(parent)->kind != CommandKind::submenu))
        throw std::invalid_argument("Invalid submenu");
    auto rows = std::make_shared<CommandRows>(commands, parent, query);
    commands_ = std::move(commands); parent_ = parent; query_text_ = std::move(query); expanded_.reset();
    set_source(std::move(rows));
    auto selected = selection();
    if (!selected.focused() || !source()->find(*selected.focused()) ||
        !source()->selectable(*source()->find(*selected.focused()))) {
        selected.clear();
        selected.set_focus({});
        for (std::size_t i = 0; i < source()->size(); ++i)
            if (source()->selectable(i)) { selected.select(source(), source()->key(i), SelectionGesture::replace); break; }
        set_selection(std::move(selected));
    } else if (!selected.contains(*selected.focused())) {
        selected.select(source(), *selected.focused(), SelectionGesture::replace);
        set_selection(std::move(selected));
    }
    invalidate(Invalidation::layout);
}
void CommandMenu::set_expanded(std::optional<CommandId> command) {
    if (expanded_ == command) return;
    if (command) {
        const auto* record = commands_ ? commands_->find(*command) : nullptr;
        if (!record || record->kind != CommandKind::submenu || !source()->find({*command, 1}))
            throw std::invalid_argument("Expanded command is not a visible submenu");
    }
    auto rows = commands_ ? std::make_shared<CommandRows>(commands_, parent_, query_text_, command) : nullptr;
    expanded_ = command;
    if (rows) set_source(std::move(rows));
}
void CommandMenu::step(int delta, SelectionGesture) {
    if (!source() || !source()->size() || !delta) return;
    const auto focused = selection().focused();
    const auto current = focused ? source()->find(*focused) : std::nullopt;
    auto next = current ? static_cast<std::ptrdiff_t>(*current) : delta < 0 ? static_cast<std::ptrdiff_t>(source()->size()) : -1;
    for (next += delta < 0 ? -1 : 1; next >= 0 && next < static_cast<std::ptrdiff_t>(source()->size()); next += delta < 0 ? -1 : 1)
        if (source()->selectable(static_cast<std::size_t>(next))) {
            select(source()->key(static_cast<std::size_t>(next))); return;
        }
}
bool CommandMenu::select(ItemKey key, SelectionGesture) {
    const auto index = source() ? source()->find(key) : std::nullopt;
    return index && source()->selectable(*index) && VirtualCollection::select(key, SelectionGesture::replace);
}
void CommandMenu::edge(bool last, SelectionGesture) {
    if (!source()) return;
    for (std::size_t i = 0; i < source()->size(); ++i) {
        const auto index = last ? source()->size() - 1 - i : i;
        if (source()->selectable(index)) { select(source()->key(index)); return; }
    }
}
bool CommandMenu::execute(CommandId id, bool pin) {
    auto commands = commands_;
    const auto* record = commands ? commands->find(id) : nullptr;
    if (!record || !enabled() || !commands->enabled(id) || !source()->find({id, 1})) return false;
    if (record->kind == CommandKind::submenu && !pin) return disclose({id, 1}, true);
    auto action = pin ? record->pin : record->action;
    if (!action || record->kind != CommandKind::action) return false;
    auto accept = pin ? std::function<void()>{} : accept_;
    if (accept) accept();
    action(); return true;
}
std::vector<CollectionRow> CommandMenu::visible_content() const {
    auto rows = VirtualCollection::visible_content();
    for (auto& row : rows) {
        const auto hierarchy = source()->hierarchy(row.index);
        row.group = hierarchy.group; row.expandable = hierarchy.expandable; row.expanded = hierarchy.expanded;
    }
    return rows;
}
bool CommandMenu::disclose(ItemKey key, bool expanded) {
    const auto* record = commands_ ? commands_->find(key.id) : nullptr;
    if (!record || !enabled() || !commands_->enabled(key.id) || record->kind != CommandKind::submenu || !source()->find(key)) return false;
    if (expanded_ == key.id && expanded) return true;
    if (expanded_ != key.id && !expanded) return true;
    auto callback = expanded ? submenu_ : collapse_; if (callback) callback(key.id); return bool(callback);
}
void CommandMenu::horizontal(bool right, SelectionGesture) {
    if (right) { if (auto key = selection().focused()) disclose(*key, true); }
    else { auto callback = back_; if (callback) callback(); }
}
CommandSurface::CommandSurface(std::wstring name, bool searchable) {
    auto content = std::make_shared<Stack>(Axis::vertical);
    content->set_padding({16, 16, 16, 16}); content->set_spacing(12);
    menu_ = std::make_shared<CommandMenu>(name);
    if (searchable) {
        auto header = std::make_shared<Stack>(Axis::horizontal);
        header->set_spacing(12);
        auto title = std::make_shared<Label>(name);
        title->set_heading(true); title->set_preferred_size({448, 32});
        header->add(title, 1);
        close_ = std::make_shared<Button>(L"Close command palette");
        close_->set_icon(ButtonIcon::close); close_->set_fixed_size({32, 32});
        close_->on_click([this] { menu_->horizontal(false, SelectionGesture::replace); });
        header->add(close_); content->add(header);
        editor_ = std::make_shared<TextInput>(L"Search commands");
        editor_->set_search_style(true); editor_->set_maximum_length(256);
        editor_->set_preferred_size({448, 48});
        editor_->set_placeholder(L"Type a command...");
        editor_->on_change([this](const std::wstring& text) { request(text); });
        editor_->on_submit([this] { if (auto key = menu_->selection().focused()) menu_->execute(key->id); });
        content->add(editor_);
    }
    auto results = std::make_shared<Stack>(Axis::vertical);
    results->add(menu_, 1); results->set_separator_after(true);
    content->add(results, 1);
    status_ = std::make_shared<Label>(searchable ? command_help : menu_help);
    status_->set_caption(true); status_->set_tone(TextTone::secondary);
    status_->set_preferred_size({448, 24}); content->add(status_);
    popup_ = std::make_shared<Popup>(content, std::move(name));
    popup_->set_preferred_size({searchable ? 480.0f : 400.0f, searchable ? 460.0f : 356.0f});
    popup_->on_dismiss([this](PopupDismissReason) { cancel(); });
}
Size CommandSurface::measure(Size available) const {
    auto desired = popup_->measure(available);
    const auto source = menu_->source();
    const auto rows = source ? source->row_start(source->size(), menu_->item_size().height) : 0;
    desired.height = std::min(desired.height, (editor_ ? 172.0f : 68.0f) + static_cast<float>(std::max(48.0, rows)));
    return desired;
}
CommandSurface::~CommandSurface() {
    cancel(); popup_->on_dismiss({});
    if (close_) close_->on_click({});
    if (editor_) { editor_->on_change({}); editor_->on_submit({}); }
}
void CommandSurface::set_commands(std::shared_ptr<const CommandSet> commands, CommandId parent) {
    menu_->set_commands(commands, parent, text_); commands_ = std::move(commands); cancel();
}
CommandQuery CommandSurface::request(std::wstring text) {
    if (text.size() > 256) throw std::length_error("Command query exceeds 256 code units");
    stop_.request_stop(); stop_ = std::stop_source{}; text_ = std::move(text);
    CommandQuery request{text_, ++generation_, stop_.get_token()};
    auto callback = query_;
    if (callback) {
        status_->set_text(L"Searching…"); auto cancellation = stop_;
        try { callback(request); } catch (...) { cancellation.request_stop(); throw; }
    }
    else if (commands_) menu_->set_commands(commands_, menu_->parent(), text_);
    return request;
}
bool CommandSurface::complete(const CommandQuery& request, std::shared_ptr<const CommandSet> commands, std::wstring error) {
    if (request.generation != generation_ || request.cancellation != stop_.get_token() || request.cancellation.stop_requested())
        return false;
    if (error.empty()) {
        if (!commands) throw std::invalid_argument("Command result is null");
        menu_->set_commands(commands, menu_->parent()); commands_ = std::move(commands);
    }
    cancel();
    error_ = std::move(error); status_->set_text(error_.empty() ? (editor_ ? command_help : menu_help) : error_);
    status_->set_tone(error_.empty() ? TextTone::secondary : TextTone::error);
    return true;
}
void CommandSurface::cancel() { stop_.request_stop(); ++generation_; }
CommandBar::CommandBar(std::wstring name) : Control(ControlRole::content_view, std::move(name), {480, 40}),
    overflow_(std::make_shared<Button>(L"More commands")) { overflow_->set_behavior(ButtonBehavior::dropdown); }
CommandBar::~CommandBar() {
    for (const auto& child : children_) std::static_pointer_cast<Button>(child)->on_click({});
}
void CommandBar::set_commands(std::shared_ptr<const CommandSet> commands) {
    if (!commands || commands->records().size() > 64) throw std::invalid_argument("Command bar supports at most 64 records");
    std::vector<std::shared_ptr<Element>> children;
    std::vector<CommandId> ids;
    for (const auto& r : commands->records()) {
        if (r.parent || r.kind == CommandKind::separator || r.kind == CommandKind::section) continue;
        if (r.kind != CommandKind::action) throw std::invalid_argument("Command bar roots must be actions");
        auto button = std::make_shared<Button>(r.label); button->set_enabled(r.enabled);
        button->set_icon(r.icon);
        if (r.checked) { button->set_behavior(ButtonBehavior::toggle); button->set_checked(*r.checked); }
        button->on_click([commands, id = r.id, weak = std::weak_ptr<Button>(button)] {
            if (auto value = weak.lock()) if (auto checked = commands->find(id)->checked) value->set_checked(*checked);
            commands->invoke(id);
        });
        adopt(button); children.push_back(button); ids.push_back(r.id);
    }
    if (children_.empty()) adopt(overflow_);
    children.push_back(overflow_); children_ = std::move(children); ids_ = std::move(ids); commands_ = std::move(commands);
    invalidate(Invalidation::layout);
}
void CommandBar::arrange(Rect bounds) {
    Element::arrange(bounds);
    bounds = this->bounds();
    const auto fit = static_cast<std::size_t>(std::max(0.0f, bounds.width) / 112);
    visible_ = fit >= ids_.size() ? ids_.size() : fit ? fit - 1 : 0;
    float x = bounds.x;
    for (std::size_t i = 0; i < ids_.size(); ++i) {
        children_[i]->arrange(i < visible_ ? Rect{x, bounds.y, 108, bounds.height} : Rect{});
        if (i < visible_) x += 112;
    }
    overflow_->arrange(visible_ < ids_.size() ? Rect{x, bounds.y, std::min(108.0f, std::max(0.0f, bounds.width - (x - bounds.x))), bounds.height} : Rect{});
}
std::shared_ptr<const CommandSet> CommandBar::overflow_commands() const {
    std::vector<CommandRecord> records;
    for (std::size_t i = visible_; i < ids_.size(); ++i) records.push_back(*commands_->find(ids_[i]));
    return std::make_shared<CommandSet>(std::move(records));
}
void CommandBar::on_overflow(std::function<void()> callback) { overflow_->on_click(std::move(callback)); }
}
