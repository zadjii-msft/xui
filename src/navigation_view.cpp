#include "xui/navigation.hpp"
#include "layout_styling.hpp"
#include <algorithm>
#include <cmath>
#include <cwctype>

namespace xui {
namespace {
std::wstring folded(std::wstring_view text) {
    std::wstring result(text);
    std::transform(result.begin(), result.end(), result.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return result;
}
class NavigationSnapshot final : public ItemsSource {
public:
    struct Row { ItemKey key; ItemContent content; ItemHierarchy hierarchy; };
    std::vector<Row> rows;
    std::map<ItemKey, std::size_t> index;
    std::size_t size() const override { return rows.size(); }
    ItemKey key(std::size_t i) const override { return rows.at(i).key; }
    ItemContent item(std::size_t i) const override { return rows.at(i).content; }
    ItemHierarchy hierarchy(std::size_t i) const override { return rows.at(i).hierarchy; }
    bool selectable(std::size_t i) const override { return rows.at(i).content.enabled && !rows.at(i).hierarchy.group; }
    std::optional<std::size_t> find(ItemKey key) const override {
        const auto it = index.find(key);
        return it == index.end() ? std::nullopt : std::optional{it->second};
    }
    std::optional<std::size_t> navigate(std::optional<std::size_t> row, CollectionNavigation direction) const override {
        if (row && direction == CollectionNavigation::parent) {
            const auto parent = hierarchy(*row).parent;
            return parent ? find(*parent) : std::nullopt;
        }
        const bool child = direction == CollectionNavigation::first_child || direction == CollectionNavigation::last_child;
        if (!row && !child) return {};
        const auto parent = child ? (row ? std::optional{key(*row)} : std::nullopt) : hierarchy(*row).parent;
        std::optional<std::size_t> result;
        for (std::size_t i = 0; i < size(); ++i) if (hierarchy(i).parent == parent) {
            if (direction == CollectionNavigation::first_child) return i;
            if (direction == CollectionNavigation::last_child) result = i;
            if (direction == CollectionNavigation::next && i > *row) return i;
            if (direction == CollectionNavigation::previous && i < *row) result = i;
        }
        return result;
    }
};
}

NavigationList::NavigationList(std::wstring name, NavigationView& owner) :
    VirtualCollection(ControlRole::tree_view, std::move(name)), owner_(&owner) {
    set_item_size({240, 40});
    on_activate([this](ItemKey key) { if (owner_) owner_->activate_item(key); });
}
void NavigationList::replace(std::shared_ptr<const ItemsSource> source, std::optional<ItemKey> selected) {
    hover_item({});
    set_help_text(L"");
    auto focus = selection_.focused();
    const auto previous = source_;
    // Move focus to a surviving ancestor when a branch disappears.
    while (focus && !source->find(*focus)) {
        const auto row = previous ? previous->find(*focus) : std::nullopt;
        focus = row ? previous->hierarchy(*row).parent : std::nullopt;
    }
    const auto available = [&](ItemKey key) {
        const auto row = source->find(key);
        return row && source->item(*row).enabled;
    };
    if (focus && !available(*focus)) focus.reset();
    if (!focus && selected && available(*selected)) focus = selected;
    if (!focus) for (std::size_t i = 0; i < source->size(); ++i) if (source->item(i).enabled) { focus = source->key(i); break; }
    selection_.clear();
    if (selected && source->find(*selected)) selection_.set(*selected, true);
    set_source(std::move(source));
    selection_.set_focus(focus);
    reveal_focus_ = true;
    if (focus && bounds().height > 0) reveal(*focus);
}
void NavigationList::arrange(Rect bounds) {
    VirtualCollection::arrange(bounds);
    if (reveal_focus_ && this->bounds().height > 0) {
        reveal_focus_ = false;
        if (const auto focus = selection_.focused()) reveal(*focus);
    }
}
bool NavigationList::remove_selection(ItemKey key) {
    if (!owner_ || !enabled() || !owner_->enabled() || !source_ || !source_->find(key)) return false;
    if (owner_->selected() == key) owner_->clear_selection();
    return true;
}
bool NavigationList::prepare_context_menu(std::optional<Point> position) {
    hover_item({});
    if (!owner_ || !enabled() || !owner_->enabled() || !source_) return false;
    const auto row = position ? hit_test(*position) :
        (selection_.focused() ? source_->find(*selection_.focused()) : std::nullopt);
    if (!row || !source_->selectable(*row) || !source_->item(*row).enabled) return false;
    return select(source_->key(*row), SelectionGesture::focus_only);
}
bool NavigationList::select(ItemKey key, SelectionGesture gesture) {
    const auto row = source_ ? source_->find(key) : std::nullopt;
    if (!owner_ || !enabled() || !owner_->enabled() || !row || !source_->item(*row).enabled) return false;
    selection_.set_focus(key);
    reveal(key);
    invalidate(Invalidation::paint);
    if (gesture == SelectionGesture::focus_only) return true;
    return owner_->navigate(key);
}
void NavigationList::step(int delta, SelectionGesture gesture) {
    if (!source_ || !delta) return;
    const auto current = selection_.focused() ? source_->find(*selection_.focused()) : std::nullopt;
    const auto size = static_cast<std::ptrdiff_t>(source_->size());
    const auto start = current ? static_cast<std::ptrdiff_t>(*current) : delta < 0 ? size : -1;
    auto next = std::clamp(start + delta, std::ptrdiff_t{0}, std::max(std::ptrdiff_t{0}, size - 1));
    for (; next >= 0 && next < size; next += delta < 0 ? -1 : 1)
        if (source_->item(static_cast<std::size_t>(next)).enabled) {
            const auto key = source_->key(static_cast<std::size_t>(next));
            // Arrow navigation focuses groups; Enter, Space, and disclosure toggle them.
            select(key, source_->hierarchy(static_cast<std::size_t>(next)).group ? SelectionGesture::focus_only : gesture);
            return;
        }
}
void NavigationList::edge(bool last, SelectionGesture gesture) {
    if (!source_) return;
    for (std::size_t i = 0; i < source_->size(); ++i) {
        const auto row = last ? source_->size() - 1 - i : i;
        if (source_->item(row).enabled) {
            select(source_->key(row), source_->hierarchy(row).group ? SelectionGesture::focus_only : gesture); return;
        }
    }
}
bool NavigationList::disclose(ItemKey key, bool value) {
    if (!owner_ || !enabled() || !source_ || !source_->find(key)) return false;
    return owner_->set_item_expanded(key, value);
}
void NavigationList::horizontal(bool right, SelectionGesture gesture) {
    if (!owner_ || !source_ || !selection_.focused()) return;
    const auto key = *selection_.focused();
    const auto row = source_->find(key);
    if (!row) return;
    const auto info = source_->hierarchy(*row);
    if (right) {
        if (info.expandable && !info.expanded) { disclose(key, true); return; }
        if (const auto child = source_->navigate(row, CollectionNavigation::first_child))
            select(source_->key(*child), source_->hierarchy(*child).group ? SelectionGesture::focus_only : gesture);
    } else if (info.expanded) disclose(key, false);
    else if (info.parent) select(*info.parent, SelectionGesture::focus_only);
}
std::vector<CollectionRow> NavigationList::visible_content() const {
    auto rows = VirtualCollection::visible_content();
    for (auto& row : rows) {
        const auto info = source_->hierarchy(row.index);
        row.parent = info.parent; row.depth = info.depth; row.group = info.group;
        row.expandable = info.expandable; row.expanded = info.expanded;
        row.navigation = true; row.compact = owner_ && !owner_->expanded();
        row.hovered = hovered_item_ == row.key;
        if (owner_ && owner_->selected_ && !info.expanded) {
            auto selected = owner_->find(*owner_->selected_);
            while (selected && selected->parent) {
                if (*selected->parent == row.key) { row.selected_descendant = true; break; }
                selected = owner_->find(*selected->parent);
            }
        }
    }
    return rows;
}
void NavigationList::hover_item(std::optional<ItemKey> key) {
    if (key && (!source_ || !source_->find(*key))) key.reset();
    if (hovered_item_ == key) return;
    hovered_item_ = key;
    ++hover_revision_;
    hover_requested_ = false;
    const auto row = key && source_ ? source_->find(*key) : std::nullopt;
    set_help_text(row ? source_->item(*row).primary : L"");
    invalidate(Invalidation::paint);
    auto callback = owner_ ? owner_->hover_changed_ : nullptr;
    if (callback) callback(key);
}
std::optional<Rect> NavigationList::hover_anchor() const {
    const auto row = hovered_item_ && source_ ? source_->find(*hovered_item_) : std::nullopt;
    if (!row || !owner_ || !owner_->enabled() || !owner_->visible() || !enabled() || !visible()) return {};
    const auto item = item_bounds(*row), view = content_viewport(), origin = bounds();
    const float top = std::max(item.y, view.y), bottom = std::min(item.y + item.height, view.y + view.height);
    if (bottom <= top || view.width <= 0) return {};
    return Rect{origin.x + item.x, origin.y + top, std::min(item.width, view.width), bottom - top};
}
void NavigationList::request_hover_help() {
    if (hover_requested_ || !hover_anchor()) return;
    hover_requested_ = true;
    auto callback = owner_ ? owner_->hover_requested_ : nullptr;
    if (callback) callback(*hovered_item_);
}
bool NavigationView::set_hover_help(ItemKey key, std::wstring text) {
    for (const auto& list : {header_, main_, footer_}) if (list->hovered_item() == key) {
        list->set_help_text(std::move(text));
        return true;
    }
    return false;
}
void NavigationView::set_hover_delay(unsigned milliseconds) {
    for (const auto& list : {header_, main_, footer_}) list->set_tooltip_delay(milliseconds);
}
bool NavigationList::disclosure_hit(Point point) const {
    const auto row = hit_test(point);
    if (!row || !source_->hierarchy(*row).expandable) return false;
    const auto bounds = item_bounds(*row);
    return (owner_ && !owner_->expanded()) || point.x >= bounds.x + bounds.width - 32;
}

NavigationView::NavigationView(std::wstring name) :
    Control(ControlRole::content_view, name, {280, 600}),
    toggle_(std::make_shared<Button>(L"Collapse navigation")),
    title_(std::make_shared<Label>(name)), empty_(std::make_shared<Label>(L"No matching items")),
    search_(std::make_shared<TextInput>(L"Search navigation")),
    header_(std::shared_ptr<NavigationList>(new NavigationList(name + L" header", *this))),
    main_(std::shared_ptr<NavigationList>(new NavigationList(name + L" items", *this))),
    footer_(std::shared_ptr<NavigationList>(new NavigationList(name + L" footer", *this))) {
    toggle_->set_icon(ButtonIcon::menu);
    toggle_->set_appearance(ButtonAppearance::subtle);
    toggle_->set_help_text(L"Expand or collapse navigation");
    toggle_->on_click([this] { set_expanded(!expanded_); });
    title_->set_heading(true);
    empty_->set_caption(true); empty_->set_tone(TextTone::secondary);
    search_->set_search_style(true); search_->set_placeholder(L"Filter navigation");
    search_->set_maximum_length(256);
    search_->on_change([this](const std::wstring& text) { set_filter(text); });
    children_ = {toggle_, title_, search_, header_, main_, footer_, empty_};
    for (const auto& child : children_) adopt(child);
    rebuild();
}
NavigationView::~NavigationView() {
    toggle_->on_click({}); search_->on_change({});
    for (const auto& list : {header_, main_, footer_}) { list->on_activate({}); list->owner_ = nullptr; }
}
void NavigationView::presentation_changed() {
    const bool winui = visual_style() == VisualStyle::winui;
    title_->set_heading(!winui);
    title_->set_body_strong(winui);
}
const NavigationItem* NavigationView::find(ItemKey key) const {
    const auto it = index_.find(key);
    return it == index_.end() ? nullptr : &entries_[it->second];
}
bool NavigationView::effective_enabled(ItemKey key) const {
    auto item = find(key);
    if (!item) return false;
    for (;;) {
        if (!item->enabled) return false;
        if (!item->parent) return true;
        item = find(*item->parent);
    }
}
bool NavigationView::has_children(ItemKey key) const {
    return std::any_of(entries_.begin(), entries_.end(), [&](const auto& item) { return item.parent == key; });
}
void NavigationView::set_items(std::vector<NavigationItem> items) {
    if (items.size() > maximum_items) throw std::length_error("Navigation supports at most 4096 items");
    std::map<ItemKey, std::size_t> index;
    for (std::size_t i = 0; i < items.size(); ++i) {
        const auto& item = items[i];
        if (!item.key.id || !index.emplace(item.key, i).second || item.label.empty() || item.label.size() > 1024 ||
            item.keywords.size() > 4096 || item.badge.size() > 32 ||
            item.section < NavigationSection::header || item.section > NavigationSection::footer ||
            item.icon < ButtonIcon::none || item.icon > ButtonIcon::drive)
            throw std::invalid_argument("Invalid navigation item");
    }
    for (const auto& item : items) {
        auto parent = item.parent;
        std::size_t depth{};
        while (parent) {
            const auto it = index.find(*parent);
            if (it == index.end() || items[it->second].section != item.section)
                throw std::invalid_argument("Navigation parent must exist in the same section");
            if (*parent == item.key || ++depth >= maximum_depth)
                throw std::invalid_argument("Navigation hierarchy is cyclic or exceeds 64 levels");
            parent = items[it->second].parent;
        }
    }
    std::set<ItemKey> closed;
    for (const auto& item : items)
        if (index_.contains(item.key) ? closed_.contains(item.key) : !item.expanded) closed.insert(item.key);
    entries_ = std::move(items); index_ = std::move(index); closed_ = std::move(closed);
    std::erase_if(filter_expansion_, [&](const auto& entry) {
        const auto* item = find(entry.first);
        return !item || item->section != NavigationSection::main;
    });
    if (selected_ && (!find(*selected_) || !find(*selected_)->selectable || !effective_enabled(*selected_))) selected_.reset();
    rebuild();
}
void NavigationView::rebuild() {
    const auto query = folded(filter_);
    matching_.clear(); matches_ = 0;
    std::set<ItemKey> included;
    std::map<std::optional<ItemKey>, std::vector<const NavigationItem*>> siblings;
    for (const auto& item : entries_) {
        siblings[item.parent].push_back(&item);
        bool match = query.empty();
        for (auto node = &item; node && !match; node = node->parent ? find(*node->parent) : nullptr)
            match = folded(node->label + L" " + node->keywords).find(query) != std::wstring::npos;
        if (item.section != NavigationSection::main || match) {
            matching_.insert(item.key);
            if (item.section == NavigationSection::main && item.selectable) ++matches_;
            for (auto node = &item; node; node = node->parent ? find(*node->parent) : nullptr) included.insert(node->key);
        }
    }
    filter_matches_ = included;
    for (const auto section : {NavigationSection::header, NavigationSection::main, NavigationSection::footer}) {
        auto snapshot = std::make_shared<NavigationSnapshot>();
        const auto append = [&](auto&& self, std::optional<ItemKey> parent, std::size_t depth) -> void {
            const auto children = siblings.find(parent);
            if (children == siblings.end()) return;
            std::vector<const NavigationItem*> visible;
            for (const auto* item : children->second)
                if (item->section == section && included.contains(item->key)) visible.push_back(item);
            for (std::size_t i = 0; i < visible.size(); ++i) {
                const auto& item = *visible[i];
                const bool branch = siblings.contains(item.key);
                const bool open = branch && expanded_state(item);
                ItemContent content{item.label, item.badge, item.icon};
                content.image_path = item.image_path;
                content.enabled = effective_enabled(item.key);
                ItemHierarchy hierarchy{parent, depth, !item.selectable, branch, open, false, i + 1, visible.size()};
                snapshot->index.emplace(item.key, snapshot->rows.size());
                snapshot->rows.push_back({item.key, std::move(content), hierarchy});
                if (open) self(self, item.key, depth + 1);
            }
        };
        append(append, {}, 0);
        const auto& list = section == NavigationSection::header ? header_ : section == NavigationSection::footer ? footer_ : main_;
        list->replace(std::move(snapshot), selected_);
    }
    invalidate(Invalidation::layout);
}
void NavigationView::set_expanded(bool value) {
    if (expanded_ == value) return;
    expanded_ = value;
    toggle_->set_name(value ? L"Collapse navigation" : L"Expand navigation");
    rebuild();
    if (selected_) for (const auto& list : {header_, main_, footer_}) list->reveal(*selected_);
    auto callback = expanded_changed_; if (callback) callback(value);
}
void NavigationView::set_pane_widths(float expanded, float collapsed) {
    if (!std::isfinite(expanded) || !std::isfinite(collapsed) || collapsed < 56 || expanded < 160 || expanded < collapsed)
        throw std::invalid_argument("Navigation widths must be finite, expanded >= 160, collapsed >= 56, and expanded >= collapsed");
    expanded_width_ = expanded; collapsed_width_ = collapsed; invalidate(Invalidation::layout);
}
bool NavigationView::expanded_state(const NavigationItem& item) const {
    if (!expanded_) return false;
    if (item.section == NavigationSection::main && !filter_.empty()) {
        if (const auto it = filter_expansion_.find(item.key); it != filter_expansion_.end()) return it->second;
        if (filter_matches_.contains(item.key)) return true;
    }
    return !closed_.contains(item.key);
}
bool NavigationView::item_expanded(ItemKey key) const {
    const auto* item = find(key);
    return item && has_children(key) && expanded_state(*item);
}
bool NavigationView::set_item_expanded(ItemKey key, bool value) {
    if (!enabled() || !effective_enabled(key) || !has_children(key)) return false;
    const auto* item = find(key);
    if (item->section == NavigationSection::main && !filter_.empty()) filter_expansion_[key] = value;
    else if (value) closed_.erase(key);
    else closed_.insert(key);
    if (value && !expanded_) { set_expanded(true); return true; }
    rebuild(); return true;
}
void NavigationView::set_filter(std::wstring query) {
    if (query.size() > 256) throw std::length_error("Navigation filter exceeds 256 code units");
    if (filter_ == query) return;
    if (filter_.empty() || query.empty()) filter_expansion_.clear();
    filter_ = std::move(query); search_->set_text(filter_); rebuild();
    auto callback = filter_changed_; const auto text = filter_;
    if (callback) callback(text);
}
void NavigationView::set_search_visible(bool value) {
    if (search_visible_ == value) return;
    search_visible_ = value; invalidate(Invalidation::layout);
}
void NavigationView::set_header_visible(bool value) {
    if (header_visible_ == value) return;
    header_visible_ = value; invalidate(Invalidation::layout);
}
bool NavigationView::select(ItemKey key) {
    const auto* item = find(key);
    if (!enabled() || !item || !item->selectable || !effective_enabled(key) ||
        (item->section == NavigationSection::main && !matching_.contains(key))) return false;
    const bool changed = selected_ != key;
    selected_ = key;
    for (auto parent = item->parent; parent; parent = find(*parent)->parent) {
        if (item->section == NavigationSection::main && !filter_.empty()) filter_expansion_[*parent] = true;
        else closed_.erase(*parent);
    }
    rebuild();
    for (const auto& list : {header_, main_, footer_}) if (list->source()->find(key)) {
        list->selection_.set_focus(key); list->reveal(key);
    }
    auto callback = changed ? select_ : std::function<void(ItemKey)>{};
    if (callback) callback(key);
    return true;
}
void NavigationView::clear_selection() {
    if (!selected_) return;
    selected_.reset(); rebuild();
}
bool NavigationView::navigate(ItemKey key) {
    const auto* item = find(key);
    if (!enabled() || !item || !effective_enabled(key)) return false;
    if (has_children(key) && (!expanded_ || !item->selectable)) return set_item_expanded(key, !item_expanded(key));
    return select(key);
}
void NavigationView::activate_item(ItemKey key) {
    const auto* item = find(key);
    if (!enabled() || !item || !effective_enabled(key)) return;
    if (!item->selectable) { navigate(key); return; }
    // Copy before navigation: selection callbacks may release this view.
    auto callback = activate_;
    if (select(key) && callback) callback(key);
}
Size NavigationView::measure(Size available) {
    if (!visible()) return {};
    return constrain({expanded_ ? expanded_width_ : collapsed_width_, available.height}, available);
}
StyleStateMask NavigationView::control_style_state_bits() const {
    return (Control::control_style_state_bits() & style_states::disabled) |
        (expanded_ ? style_states::expanded : style_states::compact) |
        (main_->source()->size() == 0 ? style_states::empty : 0);
}
void NavigationView::arrange(Rect bounds) {
    Element::arrange(bounds); bounds = layout_style::content(*this, this->bounds());
    if (!visible()) {
        for (const auto& child : children_) {
            std::static_pointer_cast<Control>(child)->set_visible(false);
            child->arrange({});
        }
        return;
    }
    const float inset = std::min(6.0f, bounds.width / 2);
    const float width = std::max(0.0f, bounds.width - inset * 2);
    float y = bounds.y + std::min(4.0f, bounds.height);
    const float bottom = bounds.y + bounds.height;
    const auto place = [&](Control& child, float height, bool visible = true) {
        child.set_visible(visible);
        height = visible ? std::min(height, std::max(0.0f, bottom - y)) : 0;
        child.arrange(height > 0 ? Rect{bounds.x + inset, y, width, height} : Rect{});
        if (height > 0) y += height + 4;
    };
    const float top = header_visible_ ? std::min(40.0f, std::max(0.0f, bottom - y)) : 0;
    toggle_->set_visible(header_visible_);
    toggle_->arrange(header_visible_ ? Rect{bounds.x + inset, y, std::min(40.0f, width), top} : Rect{});
    title_->set_visible(expanded_ && header_visible_);
    title_->arrange(expanded_ && header_visible_ ? Rect{bounds.x + inset + 44, y, std::max(0.0f, width - 44), top} : Rect{});
    if (header_visible_) y += top + 4;
    place(*search_, 40, expanded_ && search_visible_);
    const float remaining = std::max(0.0f, bottom - y);
    const auto section_height = [](const NavigationList& list) {
        if (list.source()->size() == 0) return 0.0f;
        float height = static_cast<float>(list.source()->size()) * list.item_size().height;
        if (const auto* root = list.effective_control_style_values(StylePart::root)) {
            const auto padding = root->padding.value_or(Insets{});
            const auto border = root->border_thickness.value_or(Insets{});
            height += padding.top + padding.bottom + border.top + border.bottom;
        }
        return height;
    };
    const float header_height = std::min(section_height(*header_), remaining * 0.3f);
    const float footer_height = std::min(section_height(*footer_), remaining * 0.3f);
    place(*header_, header_height, header_height > 0);
    const float main_height = std::max(0.0f, bottom - y - (footer_height > 0 ? footer_height + 4 : 0));
    const bool empty = main_->source()->size() == 0;
    empty_->set_visible(empty && expanded_);
    empty_->arrange(empty && expanded_ ? Rect{bounds.x + inset, y, width, std::min(main_height, 40.0f)} : Rect{});
    place(*main_, main_height, !empty);
    if (empty) y += main_height + 4;
    place(*footer_, footer_height, footer_height > 0);
}
}
