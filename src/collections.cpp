#include "xui/collections.hpp"
#include <algorithm>
#include <cmath>
#include <climits>
#include <stdexcept>

namespace xui {
std::optional<std::size_t> ItemsSource::navigate(std::optional<std::size_t> row, CollectionNavigation direction) const {
    if (!row) {
        if (!size()) return {};
        if (direction == CollectionNavigation::first_child) return 0;
        if (direction == CollectionNavigation::last_child) return size() - 1;
    } else {
        if (direction == CollectionNavigation::next && *row + 1 < size()) return *row + 1;
        if (direction == CollectionNavigation::previous && *row) return *row - 1;
    }
    return {};
}
bool CollectionSelection::contains(ItemKey key) const {
    for (auto it = terms_.rbegin(); it != terms_.rend(); ++it) {
        if (!it->index) { if (it->key == key) return it->selected; continue; }
        const auto index = it->index->find(key);
        if (index && it->index->selectable(*index) && *index >= it->first && *index <= it->last &&
            (!it->columns || (*index % it->columns >= it->left && *index % it->columns <= it->right))) return it->selected;
    }
    return false;
}
bool CollectionSelection::operator==(const CollectionSelection& other) const {
    if (focus_ != other.focus_ || anchor_ != other.anchor_ || terms_.size() != other.terms_.size()) return false;
    for (std::size_t i = 0; i < terms_.size(); ++i) {
        const auto& a = terms_[i]; const auto& b = other.terms_[i];
        if (a.index != b.index || a.key != b.key || a.first != b.first || a.last != b.last || a.columns != b.columns ||
            a.left != b.left || a.right != b.right || a.selected != b.selected) return false;
    }
    return true;
}
std::optional<std::vector<ItemKey>> CollectionSelection::selected_keys(const std::shared_ptr<const CollectionIndex>& index, std::size_t limit) const {
    std::set<ItemKey> keys;
    if (!index) return std::vector<ItemKey>{};
    if (index->size() <= limit) {
        for (std::size_t i = 0; i < index->size(); ++i) if (contains(index->key(i))) keys.insert(index->key(i));
    } else for (const auto& term : terms_) {
        if (!term.selected) continue;
        if (!term.index) {
            if (index->find(term.key) && contains(term.key)) keys.insert(term.key);
        } else {
            if (term.last - term.first >= limit) return {};
            for (auto i = term.first; i <= term.last && i < term.index->size(); ++i) {
                const auto key = term.index->key(i);
                if (index->find(key) && contains(key)) keys.insert(key);
            }
        }
        if (keys.size() > limit) return {};
    }
    return std::vector<ItemKey>{keys.begin(), keys.end()};
}
void CollectionSelection::clear() { if (!terms_.empty()) { terms_.clear(); ++revision_; } }
void CollectionSelection::append(Term term, bool additive) {
    if (additive && terms_.size() >= maximum_terms) throw std::length_error("Selection term limit reached");
    if (!additive) terms_.clear();
    terms_.push_back(std::move(term)); ++revision_;
}
void CollectionSelection::set(ItemKey key, bool selected) {
    if (contains(key) == selected) return;
    // Replace the trailing point term rather than growing during repeated checkbox clicks.
    if (!terms_.empty() && !terms_.back().index && terms_.back().key == key) {
        terms_.back().selected = selected; ++revision_; return;
    }
    append({{}, key, 0, 0, 0, 0, 0, selected}, true);
}
void CollectionSelection::select(std::shared_ptr<const CollectionIndex> index, ItemKey key, SelectionGesture gesture) {
    const auto row = index ? index->find(key) : std::nullopt;
    if (!row) return;
    if (gesture == SelectionGesture::extend || gesture == SelectionGesture::add_range) {
        const auto anchor = anchor_ ? index->find(*anchor_) : std::nullopt;
        range(index, anchor.value_or(*row), *row, gesture == SelectionGesture::add_range);
    } else if (gesture == SelectionGesture::toggle) {
        set(key, !contains(key)); anchor_ = key;
    } else if (gesture == SelectionGesture::replace) {
        append({{}, key}, false); anchor_ = key;
    }
    focus_ = key;
}
void CollectionSelection::range(std::shared_ptr<const CollectionIndex> index, std::size_t first, std::size_t last, bool additive) {
    if (!index || first >= index->size() || last >= index->size()) return;
    append({std::move(index), {}, std::min(first, last), std::max(first, last)}, additive);
}
void CollectionSelection::rectangle(std::shared_ptr<const CollectionIndex> index, std::size_t first, std::size_t last,
    std::size_t columns, bool additive) {
    if (!index || !columns || first >= index->size() || last >= index->size()) return;
    const auto left = std::min(first % columns, last % columns), right = std::max(first % columns, last % columns);
    append({std::move(index), {}, std::min(first / columns, last / columns) * columns + left,
        std::max(first / columns, last / columns) * columns + right, columns, left, right}, additive);
}
void CollectionSelection::select_all(std::shared_ptr<const CollectionIndex> filtered, SelectAllScope scope,
    std::shared_ptr<const CollectionIndex> full) {
    auto index = scope == SelectAllScope::filtered ? filtered : full;
    if (!index) throw std::invalid_argument("Full-source selection requires an explicit index");
    if (!index->size()) { clear(); return; }
    range(index, 0, index->size() - 1);
}
SelectionState CollectionSelection::state(const std::shared_ptr<const CollectionIndex>& index,
    const std::shared_ptr<const CollectionIndex>& full_source) const {
    if (!index || !index->size() || terms_.empty()) return SelectionState::none;
    // Small domains can answer exactly even when selection terms reference another snapshot.
    if (index->size() <= maximum_terms) {
        bool any{}, all{true}, eligible{};
        for (std::size_t i = 0; i < index->size(); ++i) {
            if (!index->selectable(i)) continue;
            eligible = true;
            const bool selected = contains(index->key(i)); any |= selected; all &= selected;
        }
        return all && eligible ? SelectionState::all : any ? SelectionState::mixed : SelectionState::none;
    }
    // Large-domain state never scans the source. Full-range gestures give an exact all state.
    bool all{}, any{};
    for (const auto& term : terms_) {
        if (term.index && (term.index == full_source || term.index->contains_all(*index)) &&
            term.first == 0 && term.last == term.index->size() - 1 && !term.columns)
            all = any = term.selected;
        else if (!term.index) {
            if (index->find(term.key)) { any |= term.selected; if (!term.selected) all = false; }
        } else { any = true; if (!term.selected) all = false; }
    }
    return all ? SelectionState::all : any ? SelectionState::mixed : SelectionState::none;
}

namespace {
struct Span {
    std::shared_ptr<const ItemsSource> source;
    std::size_t first{}, count{}, depth{};
    std::optional<ItemKey> parent;
    std::optional<ItemGroup> group;
    bool collapsed{};
};
class Projection final : public ItemsSource {
public:
    std::vector<Span> spans;
    std::size_t count{};
    std::shared_ptr<const TreeSource> tree;
    std::map<ItemKey, std::pair<bool, bool>> states;
    std::map<ItemKey, std::wstring> messages;
    void add(Span span) {
        if (span.count > INT_MAX - count) throw std::length_error("Collection supports at most INT_MAX items");
        count += span.count; if (span.count) spans.push_back(std::move(span));
    }
    std::pair<const Span&, std::size_t> at(std::size_t row) const {
        for (const auto& span : spans) {
            if (row < span.count) return {span, span.first + row};
            row -= span.count;
        }
        throw std::out_of_range("Collection index");
    }
    std::size_t size() const override { return count; }
    bool selectable(std::size_t row) const override { return !at(row).first.group; }
    ItemKey key(std::size_t row) const override {
        const auto [span, index] = at(row); return span.group ? span.group->key : span.source->key(index);
    }
    std::optional<std::size_t> find(ItemKey key) const override {
        std::size_t base{};
        for (const auto& span : spans) {
            if (span.group) { if (span.group->key == key) return base; }
            else {
                const auto found = span.source->find(key);
                if (found && *found >= span.first && *found - span.first < span.count) return base + *found - span.first;
            }
            base += span.count;
        }
        return {};
    }
    ItemContent item(std::size_t row) const override {
        const auto [span, index] = at(row);
        auto content = span.group ? ItemContent{span.group->name, std::to_wstring(span.group->count) + L" items"} : span.source->item(index);
        if (!span.group) if (const auto it = messages.find(span.source->key(index)); it != messages.end()) content.secondary = it->second;
        return content;
    }
    ItemHierarchy hierarchy(std::size_t row) const override {
        const auto [span, index] = at(row);
        ItemHierarchy info{span.parent, span.depth, span.group.has_value(), span.group.has_value(), !span.collapsed};
        info.position = index + 1; info.count = span.source ? span.source->size() : 0;
        if (tree) {
            const auto key = span.source->key(index); info.expandable = tree->has_children(key); info.expanded = false;
            if (const auto it = states.find(key); it != states.end()) {
                info.expanded = it->second.first; info.pending = it->second.second;
            }
        }
        return info;
    }
    std::optional<std::size_t> navigate(std::optional<std::size_t> row, CollectionNavigation direction) const override {
        if (!tree) return ItemsSource::navigate(row, direction);
        if (row && direction == CollectionNavigation::parent) {
            const auto parent = hierarchy(*row).parent; return parent ? find(*parent) : std::nullopt;
        }
        const bool child = direction == CollectionNavigation::first_child || direction == CollectionNavigation::last_child;
        if (!row && !child) return {};
        const auto parent = child ? (row ? std::optional{key(*row)} : std::nullopt) : hierarchy(*row).parent;
        std::optional<std::size_t> result; std::size_t first{};
        for (const auto& span : spans) {
            if (span.parent == parent) {
                if (direction == CollectionNavigation::first_child) return first;
                if (direction == CollectionNavigation::last_child) result = first + span.count - 1;
                if (direction == CollectionNavigation::next && first + span.count > *row + 1) return std::max(first, *row + 1);
                if (direction == CollectionNavigation::previous && first < *row) result = std::min(first + span.count - 1, *row - 1);
            }
            first += span.count;
        }
        return result;
    }
};
}
VirtualCollection::VirtualCollection(ControlRole role, std::wstring name) : Control(role, std::move(name), {600, 320}) {}
void VirtualCollection::changed() {
    invalidate(Invalidation::paint);
    auto callback = change_; if (callback) callback();
}
void VirtualCollection::set_selection(CollectionSelection value) { selection_ = std::move(value); invalidate(Invalidation::paint); }
void VirtualCollection::set_source(std::shared_ptr<const ItemsSource> value, std::shared_ptr<const CollectionIndex> full) {
    if (value && value->size() > INT_MAX) throw std::length_error("Collection supports at most INT_MAX items");
    if (source_ == value && full_ == full) return;
    source_ = std::move(value); full_ = std::move(full);
    set_offset(offset_); repair_focus(); invalidate(Invalidation::paint);
}
void VirtualCollection::repair_focus() {
    if (!selection_.focused() && source_ && source_->size()) selection_.set_focus(source_->key(0));
}
void VirtualCollection::select_all() {
    if (!enabled() || !source_) return;
    selection_.select_all(source_, scope_, full_); changed();
}
bool VirtualCollection::select(ItemKey key, SelectionGesture gesture) {
    const auto row = source_ ? source_->find(key) : std::nullopt;
    if (!enabled() || !row || !source_->item(*row).enabled) return false;
    if (source_->hierarchy(*row).group) gesture = SelectionGesture::focus_only;
    selection_.select(source_, key, gesture); reveal(key); changed(); return true;
}
void VirtualCollection::select_rectangle(ItemKey first, ItemKey last, bool additive) {
    if (!enabled() || !source_) return;
    const auto a = source_->find(first), b = source_->find(last);
    if (!a || !b) return;
    selection_.rectangle(source_, *a, *b, columns(), additive);
    selection_.set_focus(last); changed();
}
void VirtualCollection::step(int delta, SelectionGesture gesture) {
    if (!source_ || !source_->size()) return;
    const auto current = selection_.focused() ? source_->find(*selection_.focused()) : std::nullopt;
    const auto start = current ? static_cast<std::int64_t>(*current) : delta < 0 ? static_cast<std::int64_t>(source_->size()) : -1;
    const auto next = std::clamp(start + delta, std::int64_t{0}, static_cast<std::int64_t>(source_->size() - 1));
    select(source_->key(static_cast<std::size_t>(next)), gesture);
}
void VirtualCollection::edge(bool last, SelectionGesture gesture) {
    if (source_ && source_->size()) select(source_->key(last ? source_->size() - 1 : 0), gesture);
}
void VirtualCollection::activate_item(ItemKey key, bool inline_action) {
    const auto index = source_ ? source_->find(key) : std::nullopt;
    if (!enabled() || !index) return;
    const auto content = source_->item(*index);
    if (!content.enabled || (inline_action && content.action.empty())) return;
    auto callback = inline_action ? action_ : activate_; if (callback) callback(key);
}
void VirtualCollection::set_presentation(ItemsPresentation value) {
    if (presentation_ == value) return;
    presentation_ = value; set_offset(offset_); if (selection_.focused()) reveal(*selection_.focused());
    invalidate(Invalidation::paint);
}
void VirtualCollection::set_item_size(Size value) {
    if (!std::isfinite(value.width) || !std::isfinite(value.height) || value.width < 48 || value.height < 32)
        throw std::invalid_argument("Item size must be finite and at least 48 by 32");
    if (item_size_.width == value.width && item_size_.height == value.height) return;
    item_size_ = value; set_offset(offset_); invalidate(Invalidation::paint);
}
std::size_t VirtualCollection::columns() const {
    return presentation_ == ItemsPresentation::tiles ? static_cast<std::size_t>(std::max(1.0f, std::floor(std::max(0.0f, bounds().width - bar_width) / item_size_.width))) : 1;
}
double VirtualCollection::maximum_offset() const {
    return std::max(0.0, (source_ ? std::ceil(double(source_->size()) / columns()) * item_size_.height : 0) - bounds().height);
}
void VirtualCollection::set_offset(double value) {
    value = std::clamp(std::isfinite(value) ? value : 0, 0.0, maximum_offset());
    if (value == offset_) return;
    offset_ = value; invalidate(Invalidation::paint);
}
Rect VirtualCollection::item_bounds(std::size_t index) const {
    const auto cols = columns();
    const float width = std::max(0.0f, bounds().width - bar_width) / cols;
    return {float(index % cols) * width, static_cast<float>(double(index / cols) * item_size_.height - offset_), width, item_size_.height};
}
VisibleRange VirtualCollection::visible_items() const {
    if (!source_ || bounds().height <= 0 || bounds().width <= 0) return {};
    const auto cols = columns(), first = static_cast<std::size_t>(offset_ / item_size_.height) * cols;
    return {std::min(first, source_->size()), std::min(source_->size(),
        first + (static_cast<std::size_t>(std::ceil(bounds().height / item_size_.height)) + 1) * cols)};
}
std::optional<std::size_t> VirtualCollection::hit_test(Point point) const {
    if (!source_ || point.x < 0 || point.y < 0 || point.x >= bounds().width - bar_width || point.y >= bounds().height) return {};
    const auto cols = columns();
    const auto row = static_cast<std::size_t>((point.y + offset_) / item_size_.height) * cols +
        static_cast<std::size_t>(point.x / ((bounds().width - bar_width) / cols));
    return row < source_->size() ? std::optional{row} : std::nullopt;
}
std::vector<CollectionRow> VirtualCollection::visible_content() const {
    std::vector<CollectionRow> rows;
    const auto visible = visible_items(); rows.reserve(visible.end - visible.begin);
    for (auto i = visible.begin; i < visible.end; ++i) rows.push_back({source_->key(i), source_->item(i), item_bounds(i), i});
    return rows;
}
void VirtualCollection::reveal(ItemKey key) {
    const auto row = source_ ? source_->find(key) : std::nullopt;
    if (!row) return;
    const auto b = item_bounds(*row);
    if (b.y < 0) set_offset(offset_ + b.y);
    else if (b.y + b.height > bounds().height) set_offset(offset_ + b.y + b.height - bounds().height);
}
Rect VirtualCollection::thumb() const {
    if (!maximum_offset() || bounds().height <= 0) return {};
    const auto height = bounds().height;
    const float length = std::min(height, std::max(24.0f, float(height * height / (maximum_offset() + height))));
    return {bounds().width - 9, float(offset_ / maximum_offset()) * (height - length), 6, length};
}
void VirtualCollection::arrange(Rect value) {
    const auto before = columns(); Control::arrange(value); set_offset(offset_);
    if (columns() != before && selection_.focused()) reveal(*selection_.focused());
}
bool VirtualCollection::disclose(ItemKey, bool) { return false; }
void VirtualCollection::horizontal(bool right, SelectionGesture gesture) { step(right ? 1 : -1, gesture); }
void ItemsView::set_items(std::shared_ptr<const ItemsSource> value, std::shared_ptr<const CollectionIndex> full) {
    if (items_ == value && full_ == (full ? full : value)) return;
    auto groups = value ? value->groups() : std::vector<ItemGroup>{};
    if (groups.size() > 4096) throw std::length_error("Group limit reached");
    std::size_t end{}; std::set<ItemKey> ids;
    for (const auto& group : groups) {
        if (group.first < end || group.first > value->size() || group.count > value->size() - group.first ||
            !ids.insert(group.key).second || value->find(group.key)) throw std::invalid_argument("Invalid item group");
        end = group.first + group.count;
    }
    std::erase_if(collapsed_, [&](ItemKey key) { return !ids.contains(key); });
    items_ = std::move(value); groups_ = std::move(groups); full_ = full ? std::move(full) : items_; rebuild();
}
void ItemsView::set_presentation(ItemsPresentation value) {
    if (presentation() == value) return;
    VirtualCollection::set_presentation(value); rebuild();
}
void ItemsView::select_all() {
    if (!enabled() || !items_) return;
    selection_.select_all(items_, select_all_scope(), full_); changed();
}
void ItemsView::rebuild() {
    if (presentation() != ItemsPresentation::grouped || groups_.empty()) { set_source(items_, full_); return; }
    auto view = std::make_shared<Projection>(); std::size_t end{};
    for (const auto& group : groups_) {
        view->add({items_, end, group.first - end});
        view->add({{}, 0, 1, 0, {}, group, collapsed_.contains(group.key)});
        if (!collapsed_.contains(group.key)) view->add({items_, group.first, group.count, 0, group.key});
        end = group.first + group.count;
    }
    view->add({items_, end, items_->size() - end}); set_source(std::move(view), full_);
}
bool ItemsView::disclose(ItemKey key, bool expanded) {
    if (!enabled() || std::none_of(groups_.begin(), groups_.end(), [&](const auto& g) { return g.key == key; })) return false;
    if (collapsed_.contains(key) == !expanded) return true;
    if (expanded) collapsed_.erase(key); else collapsed_.insert(key);
    rebuild(); return true;
}
std::vector<CollectionRow> ItemsView::visible_content() const {
    auto rows = VirtualCollection::visible_content();
    const auto view = std::dynamic_pointer_cast<const Projection>(source_);
    if (view) for (auto& row : rows) {
        const auto& span = view->at(row.index).first;
        row.group = span.group.has_value(); row.expandable = row.group; row.expanded = row.group && !span.collapsed;
        row.parent = span.parent;
    }
    return rows;
}
TreeView::TreeView(std::wstring name) : VirtualCollection(ControlRole::tree_view, std::move(name)) {}
TreeView::~TreeView() { for (auto& [key, branch] : branches_) branch.stop.request_stop(); }
void TreeView::set_tree(std::shared_ptr<const TreeSource> value) {
    if (tree_ == value) return;
    for (auto& [key, branch] : branches_) branch.stop.request_stop();
    branches_.clear(); ++generation_; tree_ = std::move(value); rebuild();
}
bool TreeView::expanded(ItemKey key) const {
    const auto it = branches_.find(key); return it != branches_.end() && it->second.open;
}
bool TreeView::descendant(ItemKey key, ItemKey ancestor) const {
    for (std::size_t depth = 0; depth < maximum_depth; ++depth) {
        const auto view = std::dynamic_pointer_cast<const Projection>(source_);
        const auto it = branches_.find(key);
        const auto row = it == branches_.end() && view ? view->find(key) : std::nullopt;
        const auto parent = it != branches_.end() ? it->second.parent : row ? view->at(*row).first.parent : std::nullopt;
        if (!parent) return false;
        if (*parent == ancestor) return true;
        key = *parent;
    }
    return false;
}
bool TreeView::disclose(ItemKey key, bool open) {
    const auto view = std::dynamic_pointer_cast<const Projection>(source_);
    const auto row = view ? view->find(key) : std::nullopt;
    if (!enabled() || !tree_ || !row || !tree_->has_children(key)) return false;
    const auto span = view->at(*row).first;
    if (span.depth >= maximum_depth) return false;
    if (!branches_.contains(key) && branches_.size() >= maximum_branches) throw std::length_error("Tree branch limit reached");
    auto& branch = branches_[key]; branch.parent = span.parent;
    if (!open) {
        if (selection_.focused() && descendant(*selection_.focused(), key)) selection_.set_focus(key);
        for (auto& [child, state] : branches_) if (child == key || descendant(child, key)) {
            state.stop.request_stop(); state.pending = false; ++state.generation;
        }
        branch.open = false; rebuild(); return true;
    }
    if (branch.open && (branch.pending || branch.children)) return true;
    branch.open = true;
    if (branch.children) { rebuild(); return true; }
    branch.stop = std::stop_source{}; branch.pending = true; branch.error.clear();
    branch.generation = ++generation_;
    const TreeRequest request{key, branch.generation, branch.stop.get_token()};
    rebuild();
    auto callback = request_;
    if (callback) {
        try { callback(request); }
        catch (...) { complete(request, {}, L"Child request failed"); throw; }
    } else complete(request, {}, L"No child provider");
    return true;
}
bool TreeView::complete(TreeRequest request, std::shared_ptr<const ItemsSource> children, std::wstring error) {
    const auto it = branches_.find(request.node);
    if (it == branches_.end() || request.cancellation.stop_requested() || request.cancellation != it->second.stop.get_token() || !it->second.pending ||
        !it->second.open || it->second.generation != request.generation) return false;
    if (children && children->size() > INT_MAX) throw std::length_error("Tree child limit reached");
    if (children && children->find(request.node)) throw std::invalid_argument("Tree children contain their parent");
    auto& branch = it->second; branch.pending = false; branch.children = std::move(children);
    branch.error = std::move(error); rebuild(); return true;
}
void TreeView::rebuild() {
    auto view = std::make_shared<Projection>();
    view->tree = tree_;
    for (const auto& [key, branch] : branches_) {
        view->states.emplace(key, std::pair{branch.open, branch.pending});
        if (branch.pending) view->messages.emplace(key, L"Loading children");
        else if (!branch.error.empty()) view->messages.emplace(key, branch.error + L". Right arrow retries.");
    }
    const auto emit = [&](auto&& self, const std::shared_ptr<const ItemsSource>& children,
        std::optional<ItemKey> parent, std::size_t depth) -> void {
        if (!children) return;
        std::vector<std::pair<std::size_t, const Branch*>> expanded;
        for (const auto& [key, branch] : branches_) if (branch.open && branch.parent == parent) {
            if (const auto row = children->find(key)) expanded.emplace_back(*row, &branch);
        }
        std::sort(expanded.begin(), expanded.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        std::size_t first{};
        for (const auto& [row, branch] : expanded) {
            view->add({children, first, row + 1 - first, depth, parent});
            if (depth < maximum_depth) self(self, branch->children, children->key(row), depth + 1);
            first = row + 1;
        }
        view->add({children, first, children->size() - first, depth, parent});
    };
    if (tree_) emit(emit, tree_->roots(), {}, 0);
    set_source(std::move(view));
}
std::vector<CollectionRow> TreeView::visible_content() const {
    auto rows = VirtualCollection::visible_content();
    const auto view = std::dynamic_pointer_cast<const Projection>(source_);
    for (auto& row : rows) {
        const auto& span = view->at(row.index).first; row.depth = span.depth; row.parent = span.parent;
        row.expandable = tree_ && tree_->has_children(row.key); row.expanded = expanded(row.key);
        if (const auto it = branches_.find(row.key); it != branches_.end()) {
            row.pending = it->second.pending;
            if (row.pending) row.content.secondary = L"Loading children";
            else if (!it->second.error.empty()) row.content.secondary = it->second.error + L". Right arrow retries.";
        }
    }
    return rows;
}
void TreeView::horizontal(bool right, SelectionGesture gesture) {
    if (!selection_.focused() || !source_) return;
    const auto key = *selection_.focused();
    if (right) {
        const auto row = source_->find(key);
        const auto child = row ? source_->navigate(row, CollectionNavigation::first_child) : std::nullopt;
        if (expanded(key) && child) select(source_->key(*child), gesture);
        else disclose(key, true);
    }
    else if (expanded(key)) disclose(key, false);
    else {
        const auto view = std::dynamic_pointer_cast<const Projection>(source_);
        if (const auto row = view->find(key)) if (const auto parent = view->at(*row).first.parent) select(*parent, gesture);
    }
}
void TreeView::cancel() {
    Control::cancel();
    bool changed{};
    for (auto& [key, branch] : branches_) if (branch.pending) {
        branch.stop.request_stop(); branch.pending = false; branch.open = false; ++branch.generation; changed = true;
    }
    if (changed) rebuild();
}
}
