#include "xui/collections.hpp"
#include "collection_presentation.hpp"
#include <algorithm>
#include <cmath>
#include <climits>
#include <stdexcept>

namespace xui {
namespace detail {
std::shared_ptr<const FrozenCollectionRow> CollectionPresentationAccess::freeze(
    const VirtualCollection& control, const CollectionRow& row) {
    if (!control.source() || row.index >= control.source()->size() || control.source()->key(row.index) != row.key)
        throw std::invalid_argument("Cannot freeze a stale collection row");
    auto snapshot = row;
    snapshot.index = (std::numeric_limits<std::size_t>::max)();
    snapshot.hovered = false;
    auto visual = control.source()->visual(row.index);
    if (visual.icon == ButtonIcon::none) visual.icon = row.content.icon;
    if (visual.image_path.empty()) visual.image_path = row.content.image_path;
    if (row.navigation && !visual.image_path.empty() && visual.icon == ButtonIcon::none) visual.icon = ButtonIcon::folder;
    return std::make_shared<const FrozenCollectionRow>(FrozenCollectionRow{
        std::move(snapshot), std::move(visual), control.selection().contains(row.key)});
}
bool CollectionBox::contains(double px, double py) const {
    return px >= x && py >= y && px < x + width && py < y + height;
}
CollectionBox CollectionBox::intersect(CollectionBox b) const {
    const auto left = std::max(x, b.x), top = std::max(y, b.y);
    return {left, top, std::max(0.0, std::min(x + width, b.x + b.width) - left),
        std::max(0.0, std::min(y + height, b.y + b.height) - top)};
}
Rect CollectionBox::in_view(Rect viewport, double offset) const {
    return {viewport.x + static_cast<float>(x), viewport.y + static_cast<float>(y - offset),
        static_cast<float>(width), static_cast<float>(height)};
}
CollectionPresentation::CollectionPresentation(std::shared_ptr<const ItemsSource> source, std::uint64_t version,
    double width, double item_height, double extent, std::vector<CollectionBand> bands,
    std::vector<OutgoingCollectionRow> outgoing) :
    source_(std::move(source)), version_(version), width_(width), item_height_(item_height), extent_(extent),
    bands_(std::move(bands)), outgoing_(std::move(outgoing)) {
    if (!source_ || source_->size() > INT_MAX || !version || !std::isfinite(width) || width <= 0 ||
        !std::isfinite(item_height) || item_height < 1 || !std::isfinite(extent) || extent < 0)
        throw std::invalid_argument("Invalid collection presentation dimensions or source");
    if (bands_.size() > maximum_bands || outgoing_.size() > maximum_outgoing)
        throw std::length_error("Collection presentation storage limit reached");
    const auto valid_box = [](CollectionBox b) {
        return std::isfinite(b.x) && std::isfinite(b.y) && std::isfinite(b.width) && std::isfinite(b.height) &&
            b.x >= 0 && b.y >= 0 && b.width >= 0 && b.height >= 0 &&
            std::isfinite(b.x + b.width) && std::isfinite(b.y + b.height);
    };
    std::size_t next{};
    double end{};
    for (std::size_t i = 0; i < bands_.size(); ++i) {
        const auto& b = bands_[i];
        if (b.first != next || !b.count || b.count > source_->size() - next ||
            !valid_box(b.row) || !valid_box(b.clip) || b.row.width <= 0 || b.row.height < 1 ||
            b.row.x + b.row.width > width || !std::isfinite(b.stride) || b.stride < b.row.height)
            throw std::invalid_argument("Invalid ordered collection presentation band");
        const auto bottom = b.row.y + (b.count - 1) * b.stride + b.row.height;
        if (!std::isfinite(bottom)) throw std::invalid_argument("Collection presentation extent overflow");
        const auto painted = CollectionBox{b.row.x, b.row.y, b.row.width, bottom - b.row.y}.intersect(b.clip);
        if (painted.width > 0 && painted.height > 0) {
            if (painted.y < end || painted.y + painted.height > extent)
                throw std::invalid_argument("Collection presentation bands overlap or exceed the extent");
            intervals_.push_back({painted.y, painted.y + painted.height, i});
            end = painted.y + painted.height;
        }
        next += b.count;
    }
    if (next != source_->size()) throw std::invalid_argument("Collection presentation must cover every logical row");
    std::vector<std::pair<double, double>> exits;
    std::set<ItemKey> keys;
    for (const auto& row : outgoing_) {
        if (!row.frozen || source_->find(row.frozen->row.key) || !keys.insert(row.frozen->row.key).second ||
            !valid_box(row.bounds) || !valid_box(row.clip) || row.bounds.width <= 0 || row.bounds.height < 1 ||
            row.bounds.x + row.bounds.width > width)
            throw std::invalid_argument("Invalid draw-only outgoing collection row");
        const auto painted = row.bounds.intersect(row.clip);
        if (painted.width <= 0 || painted.height <= 0) continue;
        if (painted.y + painted.height > extent)
            throw std::invalid_argument("Outgoing collection row exceeds the presentation extent");
        const auto interval = std::lower_bound(intervals_.begin(), intervals_.end(), painted.y,
            [](const Interval& a, double top) { return a.bottom <= top; });
        if (interval != intervals_.end() && interval->top < painted.y + painted.height)
            throw std::invalid_argument("Outgoing collection pixels overlap live rows");
        exits.emplace_back(painted.y, painted.y + painted.height);
    }
    std::sort(exits.begin(), exits.end());
    for (std::size_t i = 1; i < exits.size(); ++i)
        if (exits[i].first < exits[i - 1].second) throw std::invalid_argument("Outgoing collection rows overlap");
}
const CollectionBand& CollectionPresentation::band(std::size_t index) const {
    if (index >= source_->size()) throw std::out_of_range("Collection presentation row index");
    return *std::prev(std::upper_bound(bands_.begin(), bands_.end(), index,
        [](std::size_t row, const CollectionBand& b) { return row < b.first; }));
}
CollectionBox CollectionPresentation::bounds(std::size_t index) const {
    const auto& b = band(index);
    auto result = b.row;
    result.y += (index - b.first) * b.stride;
    return result;
}
CollectionBox CollectionPresentation::clip(std::size_t index) const {
    return bounds(index).intersect(band(index).clip);
}
std::vector<std::size_t> CollectionPresentation::visible(double offset, double height) const {
    std::vector<std::size_t> rows;
    if (!std::isfinite(offset) || !std::isfinite(height) || offset < 0 || height < 0)
        throw std::invalid_argument("Invalid collection presentation viewport");
    if (height == 0) return rows;
    auto interval = std::lower_bound(intervals_.begin(), intervals_.end(), offset,
        [](const Interval& a, double top) { return a.bottom <= top; });
    for (; interval != intervals_.end() && interval->top < offset + height; ++interval) {
        const auto& b = bands_[interval->band];
        const auto top = std::max(offset, interval->top), bottom = std::min(offset + height, interval->bottom);
        const auto first = static_cast<std::size_t>(std::clamp(std::floor((top - b.row.y) / b.stride), 0.0, double(b.count)));
        const auto last = static_cast<std::size_t>(std::clamp(std::ceil((bottom - b.row.y) / b.stride), 0.0, double(b.count)));
        for (auto i = first; i < last; ++i) {
            const auto box = clip(b.first + i);
            if (box.y + box.height > offset && box.y < offset + height && box.height > 0) rows.push_back(b.first + i);
        }
    }
    return rows;
}
std::optional<std::size_t> CollectionPresentation::hit(double x, double y) const {
    if (!std::isfinite(x) || !std::isfinite(y)) throw std::invalid_argument("Invalid collection presentation point");
    const auto interval = std::lower_bound(intervals_.begin(), intervals_.end(), y,
        [](const Interval& a, double top) { return a.bottom <= top; });
    if (interval == intervals_.end() || y < interval->top) return {};
    const auto& b = bands_[interval->band];
    const auto relative = std::floor((y - b.row.y) / b.stride);
    if (relative < 0 || relative >= double(b.count)) return {};
    const auto index = b.first + static_cast<std::size_t>(relative);
    return clip(index).contains(x, y) ? std::optional{index} : std::nullopt;
}
}

double ItemsSource::row_start(std::size_t index, double row_height) const {
    return std::min(index, size()) * row_height;
}
std::size_t ItemsSource::row_at(double offset, double row_height) const {
    return static_cast<std::size_t>(std::clamp(std::floor(offset / row_height), 0.0, double(size())));
}
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
    ItemVisual visual(std::size_t row) const override {
        const auto [span, index] = at(row);
        return span.group ? ItemVisual{} : span.source->visual(index);
    }
    std::wstring cell(std::size_t row, std::size_t column) const override {
        const auto [span, index] = at(row);
        return span.group ? (column == 0 ? span.group->name : std::wstring{}) : span.source->cell(index, column);
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
            info.error = !info.pending && messages.contains(key);
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
StyleStateMask collection_row_style_state(const CollectionRow& row, bool selected, bool focused,
    bool enabled, bool hovered) {
    using namespace style_states;
    StyleStateMask state = selected ? style_states::selected : 0;
    if (focused) state |= style_states::focused;
    if (row.content.checked.value_or(false)) state |= checked;
    if (row.expanded) state |= row.content.submenu ? open : expanded;
    if (row.pending) state |= loading;
    if (row.error) state |= error;
    if (row.selected_descendant) state |= selected_descendant;
    if (row.compact) state |= compact;
    if (!enabled || !row.content.enabled) state |= disabled;
    else if (hovered || row.hovered) state |= style_states::hovered;
    return state;
}
std::optional<StyleTarget> VirtualCollection::control_style_target() const {
    switch (role()) {
    case ControlRole::items_view: return StyleTarget::items_view;
    case ControlRole::tree_view: return StyleTarget::tree_view;
    case ControlRole::command_menu: return StyleTarget::command_menu;
    default: return {};
    }
}
StyleStateMask VirtualCollection::control_style_state_bits() const {
    return Control::control_style_state_bits() & (style_states::focused | style_states::hovered | style_states::disabled);
}
Size VirtualCollection::item_size() const {
    if (!has_control_styling()) return item_size_;
    auto result = item_size_;
    const auto* root = effective_control_style_values(StylePart::root);
    if (root) result.height = std::max(1.0f, root->row_height.value_or(result.height));
    if (role() == ControlRole::items_view)
        if (const auto* tile = effective_control_style_values(StylePart::tile))
            result.width = std::max(1.0f, tile->width.value_or(result.width));
    return result;
}
float VirtualCollection::scrollbar_width() const {
    if (has_control_styling())
        if (const auto* bar = effective_control_style_values(StylePart::scrollbar))
            return bar->width.value_or(bar_width);
    return bar_width;
}
Rect VirtualCollection::content_viewport() const {
    Insets inset{};
    if (has_control_styling()) if (const auto* root = effective_control_style_values(StylePart::root)) {
        const auto padding = root->padding.value_or(Insets{});
        const auto border = root->border_thickness.value_or(Insets{});
        inset = {padding.left + border.left, padding.top + border.top,
            padding.right + border.right, padding.bottom + border.bottom};
    }
    const auto x = std::min(inset.left, bounds().width), y = std::min(inset.top, bounds().height);
    return {x, y, std::max(0.0f, bounds().width - x - inset.right - scrollbar_width()),
        std::max(0.0f, bounds().height - y - inset.bottom)};
}
PartStyleValues VirtualCollection::row_style_values(StylePart part, const CollectionRow& row, StyleStateMask state) const {
    state |= control_style_state_bits() & style_states::disabled;
    auto result = resolve_control_style_part(part, state);
    const auto face = row.group ? StylePart::group_header :
        role() == ControlRole::items_view && wraps_items() ? StylePart::tile : StylePart::row;
    if (face == StylePart::row) return result;
    const auto& schema = control_style_schema(*control_style_target());
    const auto definition = control_style();
    auto current = part;
    for (std::size_t depth = 0; depth < schema.parts.size(); ++depth) {
        if (current == StylePart::root) break;
        if (current == StylePart::row) {
            result.foreground = resolve_control_style_part(face, state).foreground;
            break;
        }
        const auto metadata = std::find_if(schema.parts.begin(), schema.parts.end(), [&](const auto& entry) { return entry.part == current; });
        if (metadata == schema.parts.end()) break;
        const auto authored = definition ? definition->resolve(current, state & metadata->states) : std::nullopt;
        if (control_style_values(current).foreground || (authored && authored->foreground) || !metadata->foreground_from) break;
        current = *metadata->foreground_from;
    }
    return result;
}
void VirtualCollection::changed() {
    invalidate(Invalidation::paint);
    auto callback = change_; if (callback) callback();
}
void VirtualCollection::set_selection(CollectionSelection value) { selection_ = std::move(value); invalidate(Invalidation::paint); }
void VirtualCollection::set_source(std::shared_ptr<const ItemsSource> value, std::shared_ptr<const CollectionIndex> full,
    std::shared_ptr<const detail::CollectionPresentation> frame) {
    if (value && value->size() > INT_MAX) throw std::length_error("Collection supports at most INT_MAX items");
    if (frame && (frame->source() != value || (presentation_ != ItemsPresentation::list && presentation_ != ItemsPresentation::grouped) ||
        frame->width() != content_viewport().width || frame->item_height() != item_size().height ||
        (frame != collection_presentation_ && frame->version() <= collection_presentation_version_)))
        throw std::invalid_argument("Stale or incompatible collection presentation");
    if (source_ == value && full_ == full && (!frame || frame == collection_presentation_)) return;
    if (!frame) clear_collection_presentation();
    source_ = std::move(value); full_ = std::move(full);
    collection_presentation_ = std::move(frame);
    if (collection_presentation_) {
        collection_presentation_version_ = collection_presentation_->version();
        if (selection_.focused() && !source_->find(*selection_.focused())) selection_.set_focus({});
    } else offset_ = std::clamp(offset_, 0.0, maximum_offset());
    repair_focus(); invalidate(Invalidation::paint);
}
void VirtualCollection::set_collection_presentation(std::shared_ptr<const detail::CollectionPresentation> frame) {
    if (!frame) clear_collection_presentation();
    else set_source(source_, full_, std::move(frame));
}
void VirtualCollection::set_collection_presentation_offset(double value) {
    if (!collection_presentation_ || !std::isfinite(value) || value < 0)
        throw std::invalid_argument("Presentation offset requires an active frame and a finite nonnegative value");
    if (offset_ == value) return;
    offset_ = value;
    invalidate(Invalidation::paint);
}
void VirtualCollection::clear_collection_presentation() {
    if (!collection_presentation_) return;
    collection_presentation_.reset();
    offset_ = std::clamp(offset_, 0.0, maximum_offset());
    collection_presentation_retired();
    invalidate(Invalidation::paint);
}
void VirtualCollection::cancel() {
    Control::cancel();
    clear_collection_presentation();
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
    if (value < ItemsPresentation::list || value > ItemsPresentation::gallery ||
        (value == ItemsPresentation::gallery && role() != ControlRole::items_view))
        throw std::invalid_argument("Invalid collection presentation");
    if (presentation_ == value) return;
    clear_collection_presentation();
    presentation_ = value; set_offset(offset_); if (selection_.focused()) reveal(*selection_.focused());
    invalidate(Invalidation::paint);
}
void VirtualCollection::set_item_size(Size value) {
    if (!std::isfinite(value.width) || !std::isfinite(value.height) || value.width < 48 || value.height < 32)
        throw std::invalid_argument("Item size must be finite and at least 48 by 32");
    if (item_size_.width == value.width && item_size_.height == value.height) return;
    clear_collection_presentation();
    item_size_ = value; set_offset(offset_); invalidate(Invalidation::paint);
}
std::size_t VirtualCollection::columns() const {
    return wraps_items() ? static_cast<std::size_t>(std::max(1.0f, std::floor(content_viewport().width / item_size().width))) : 1;
}
double VirtualCollection::maximum_offset() const {
    if (collection_presentation_) return std::max(0.0, collection_presentation_->extent() - content_viewport().height);
    const auto height = item_size().height;
    return std::max(0.0, (source_ ? columns() == 1 ? source_->row_start(source_->size(), height) :
        std::ceil(double(source_->size()) / columns()) * height : 0) - content_viewport().height);
}
void VirtualCollection::set_offset(double value) {
    clear_collection_presentation();
    value = std::clamp(std::isfinite(value) ? value : 0, 0.0, maximum_offset());
    if (value == offset_) return;
    offset_ = value; invalidate(Invalidation::paint);
}
Rect VirtualCollection::item_bounds(std::size_t index) const {
    if (collection_presentation_) return collection_presentation_->bounds(index).in_view(content_viewport(), offset());
    const auto cols = columns();
    const auto viewport = content_viewport();
    const auto height = item_size().height;
    const float width = viewport.width / cols;
    const auto scroll = std::min(offset_, maximum_offset());
    if (source_ && cols == 1) {
        const auto top = source_->row_start(index, height);
        return {viewport.x, viewport.y + static_cast<float>(top - scroll), width,
            static_cast<float>(source_->row_start(index + 1, height) - top)};
    }
    return {viewport.x + float(index % cols) * width,
        viewport.y + static_cast<float>(double(index / cols) * height - scroll), width, height};
}
CollectionGalleryLayout VirtualCollection::gallery_layout(const CollectionRow& row, StyleStateMask state) const {
    auto b = row.bounds;
    PartStyleValues face, icon;
    if (has_control_styling()) {
        face = row_style_values(StylePart::tile, row, state);
        icon = row_style_values(StylePart::icon, row, state);
    }
    const auto p = face.padding.value_or(Insets{}), t = face.border_thickness.value_or(Insets{});
    const auto left = std::min(b.width, p.left + t.left + 8);
    const auto top = std::min(b.height, p.top + t.top + 8);
    const auto action = !row.content.action.empty() && b.width >= 160 ? 74.0f : 0.0f;
    b = {b.x + left, b.y + top, std::max(0.0f, b.width - left - p.right - t.right - 8 - action),
        std::max(0.0f, b.height - top - p.bottom - t.bottom - 8)};
    const float secondary = row.content.secondary.empty() ? 0.0f : std::min(20.0f, b.height / 3);
    const float primary = std::min(40.0f, b.height - secondary);
    const float label_top = b.y + b.height - primary - secondary;
    const float image_height = std::max(0.0f, label_top - b.y - 4);
    const float extent = std::max(0.0f, std::min({b.width, image_height,
        icon.size.value_or(std::max(0.0f, item_size().width - 16))}));
    return {{b.x + (b.width - extent) / 2, b.y + (image_height - extent) / 2, extent, extent},
        {b.x, label_top, b.width, primary}, {b.x, label_top + primary, b.width, secondary}};
}
Rect VirtualCollection::disclosure_bounds(const CollectionRow &row, bool hovered) const {
    if (const auto* tree = dynamic_cast<const TreeView*>(this); tree && !tree->detail_columns().empty())
        return tree->details_layout(row, collection_row_style_state(row, selection_.contains(row.key),
            focused() && selection_.focused() == row.key, enabled(), hovered || row.hovered)).disclosure;
    auto b = row.bounds;
    if (row.navigation)
        return {b.x + std::max(0.0f, b.width - 32), b.y, std::min(b.width, 32.0f), b.height};
    float indent = 20;
    if (has_control_styling()) {
        const auto state = collection_row_style_state(row, selection_.contains(row.key), focused() && selection_.focused() == row.key,
                                                      enabled(), hovered || row.hovered);
        const auto values =
            resolve_control_style_part(row.group ? StylePart::group_header
                                       : role() == ControlRole::items_view && wraps_items() ? StylePart::tile
                                                                                                                         : StylePart::row,
                                       state);
        const auto p = values.padding.value_or(Insets{}), t = values.border_thickness.value_or(Insets{});
        const auto left = std::min(b.width, p.left + t.left), top = std::min(b.height, p.top + t.top);
        b = {b.x + left, b.y + top, std::max(0.0f, b.width - left - p.right - t.right),
             std::max(0.0f, b.height - top - p.bottom - t.bottom)};
        const auto root = resolve_control_style_part(StylePart::root, 0);
        indent = root.indentation.value_or(indent);
    }
    const float left = b.x + 10 + std::min(static_cast<float>(row.depth) * indent, b.width / 3) + (row.content.checked ? 24 : 0);
    return {left, b.y, std::min(24.0f, std::max(0.0f, b.x + b.width - left)), b.height};
}
bool VirtualCollection::disclosure_hit(std::size_t index, Point point) const {
    if (!source_ || index >= source_->size())
        return false;
    if (collection_presentation_ && hit_test(point) != index) return false;
    const auto info = source_->hierarchy(index);
    if (!info.expandable)
        return false;
    CollectionRow row{source_->key(index), source_->item(index), item_bounds(index), index};
    row.group = info.group;
    row.expandable = info.expandable;
    row.depth = info.depth;
    row.expanded = info.expanded;
    row.pending = info.pending;
    row.error = info.error;
    row.hovered = true;
    if (row.content.submenu)
        return false;
    const auto* tree = dynamic_cast<const TreeView*>(this);
    if (!has_control_styling() && (!tree || tree->detail_columns().empty()))
        return point.x < row.bounds.x + 34 + std::min<float>(static_cast<float>(row.depth) * 20, row.bounds.width / 3);
    const auto b = disclosure_bounds(row);
    return point.x >= b.x && point.x < b.x + b.width && point.y >= b.y && point.y < b.y + b.height;
}
VisibleRange VirtualCollection::visible_items() const {
    const auto viewport = content_viewport();
    if (!source_ || viewport.height <= 0 || viewport.width <= 0) return {};
    if (collection_presentation_) {
        const auto rows = collection_presentation_->visible(offset(), viewport.height);
        return rows.empty() ? VisibleRange{} : VisibleRange{rows.front(), rows.back() + 1};
    }
    const auto height = item_size().height;
    const auto scroll = std::min(offset_, maximum_offset());
    if (columns() == 1) return {source_->row_at(scroll, height),
        std::min(source_->size(), source_->row_at(scroll + viewport.height, height) + 1)};
    const auto cols = columns(), first = static_cast<std::size_t>(scroll / height) * cols;
    return {std::min(first, source_->size()), std::min(source_->size(),
        first + (static_cast<std::size_t>(std::ceil(viewport.height / height)) + 1) * cols)};
}
std::optional<std::size_t> VirtualCollection::hit_test(Point point) const {
    const auto viewport = content_viewport();
    point.x -= viewport.x; point.y -= viewport.y;
    if (!source_ || point.x < 0 || point.y < 0 || point.x >= viewport.width || point.y >= viewport.height) return {};
    if (collection_presentation_) return collection_presentation_->hit(point.x, point.y + offset());
    const auto cols = columns();
    const auto height = item_size().height;
    const auto scroll = std::min(offset_, maximum_offset());
    if (cols == 1) {
        const auto row = source_->row_at(point.y + scroll, height);
        return row < source_->size() ? std::optional{row} : std::nullopt;
    }
    const auto row = static_cast<std::size_t>((point.y + scroll) / height) * cols +
        static_cast<std::size_t>(point.x / (viewport.width / cols));
    return row < source_->size() ? std::optional{row} : std::nullopt;
}
std::vector<CollectionRow> VirtualCollection::visible_content() const {
    std::vector<CollectionRow> rows;
    if (collection_presentation_) {
        for (const auto i : collection_presentation_->visible(offset(), content_viewport().height))
            rows.push_back({source_->key(i), source_->item(i), item_bounds(i), i});
        return rows;
    }
    const auto visible = visible_items(); rows.reserve(visible.end - visible.begin);
    for (auto i = visible.begin; i < visible.end; ++i) rows.push_back({source_->key(i), source_->item(i), item_bounds(i), i});
    return rows;
}
void VirtualCollection::reveal(ItemKey key) {
    const auto row = source_ ? source_->find(key) : std::nullopt;
    if (!row) return;
    clear_collection_presentation();
    const auto b = item_bounds(*row);
    const auto viewport = content_viewport();
    if (b.y < viewport.y) set_offset(offset() + b.y - viewport.y);
    else if (b.y + b.height > viewport.y + viewport.height)
        set_offset(offset() + b.y + b.height - viewport.y - viewport.height);
}
Rect VirtualCollection::thumb() const {
    const auto viewport = content_viewport();
    if (!maximum_offset() || viewport.height <= 0 || scrollbar_width() <= 0) return {};
    const auto height = viewport.height;
    const float length = std::min(height, std::max(24.0f, float(height * height / (maximum_offset() + height))));
    const auto width = scrollbar_width();
    return {viewport.x + viewport.width + width / 4,
        viewport.y + float(offset() / maximum_offset()) * (height - length), width / 2, length};
}
void VirtualCollection::arrange(Rect value) {
    const auto before = columns();
    if (collection_presentation_ && (value.width != bounds().width || value.height != bounds().height))
        clear_collection_presentation();
    Control::arrange(value);
    if (collection_presentation_ && (collection_presentation_->width() != content_viewport().width ||
        collection_presentation_->item_height() != item_size().height)) clear_collection_presentation();
    if (!collection_presentation_) set_offset(offset_);
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
void TreeView::set_columns(std::vector<GridColumn> columns) {
    if (columns.size() > 64) throw std::invalid_argument("Tree details support at most 64 columns");
    for (const auto& column : columns)
        if (!std::isfinite(column.width) || column.width < 48 || column.width > 2000 || column.filterable || column.checkable)
            throw std::invalid_argument("Tree details require widths from 48 to 2000 and read-only columns");
    if (detail_columns_ == columns) return;
    detail_columns_ = std::move(columns);
    invalidate(Invalidation::paint);
}
TreeDetailsLayout TreeView::details_layout(const CollectionRow& row, StyleStateMask state) const {
    TreeDetailsLayout result;
    if (detail_columns_.empty()) return result;
    auto b = row.bounds;
    const auto face = row_style_values(StylePart::row, row, state);
    const auto padding = face.padding.value_or(Insets{}), border = face.border_thickness.value_or(Insets{});
    const float left = std::min(b.width, padding.left + border.left), top = std::min(b.height, padding.top + border.top);
    b = {b.x + left, b.y + top, std::max(0.0f, b.width - left - padding.right - border.right),
        std::max(0.0f, b.height - top - padding.bottom - border.bottom)};
    if (!row.content.action.empty() && row.bounds.width >= 160) b.width = std::max(0.0f, b.width - 74);
    float total{};
    for (const auto& column : detail_columns_) total += column.width;
    float x = b.x;
    for (std::size_t i = 0; i < detail_columns_.size(); ++i) {
        const auto width = detail_columns_[i].width + (i == 0 ? std::max(0.0f, b.width - total) : 0);
        result.columns.push_back({std::min(x, b.x + b.width), b.y, std::max(0.0f, std::min(width, b.x + b.width - x)), b.height});
        x += width;
    }
    const auto name = result.columns.front();
    const auto indentation = row_style_values(StylePart::root, row, state).indentation.value_or(16);
    const float end = name.x + name.width;
    x = std::min(end, name.x + 4 + static_cast<float>(row.depth) * indentation + (row.content.checked ? 20 : 0));
    const auto slot = [&](float width) {
        const Rect rect{x, name.y, std::max(0.0f, std::min(width, end - x)), name.height};
        x += rect.width;
        return rect;
    };
    if (row.expandable && !row.content.submenu) result.disclosure = slot(16);
    if (row.content.icon != ButtonIcon::none || !row.content.image_path.empty()) {
        const auto extent = std::min({row_style_values(StylePart::icon, row, state).size.value_or(16), name.height, std::max(0.0f, end - x)});
        result.icon = {x, name.y + (name.height - extent) / 2, extent, extent};
        x = std::min(end, x + extent + 6);
    }
    result.name = {x, name.y, std::max(0.0f, end - x - 6), name.height};
    return result;
}
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
        const auto previous_focus = selection_.focused();
        if (selection_.focused() && descendant(*selection_.focused(), key)) selection_.set_focus(key);
        for (auto& [child, state] : branches_) if (child == key || descendant(child, key)) {
            state.stop.request_stop(); state.pending = false; ++state.generation;
        }
        branch.open = false;
        rebuild();
        if (selection_.focused() != previous_focus) changed();
        return true;
    }
    if (branch.open && (branch.pending || (branch.children && branch.error.empty()))) return true;
    branch.open = true;
    if (branch.children && branch.error.empty()) { rebuild(); return true; }
    branch.children.reset();
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
            row.error = !it->second.error.empty();
            if (row.pending) row.content.secondary = L"Loading children";
            else if (!it->second.error.empty()) row.content.secondary = it->second.error + L". Right arrow retries.";
        }
        if (!detail_columns_.empty()) {
            row.cells.reserve(detail_columns_.size() - 1);
            for (std::size_t column = 1; column < detail_columns_.size(); ++column)
                row.cells.push_back(source_->cell(row.index, column));
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
    VirtualCollection::cancel();
    bool changed{};
    for (auto& [key, branch] : branches_) if (branch.pending) {
        branch.stop.request_stop(); branch.pending = false; branch.open = false; ++branch.generation; changed = true;
    }
    if (changed) rebuild();
}
}
