#include "xui/navigation.hpp"
#include <algorithm>
#include <set>

namespace xui {
Breadcrumb::Breadcrumb(std::wstring name) : Control(ControlRole::content_view, std::move(name), {480, 38}),
    overflow_(std::make_shared<Button>(L"Earlier locations")) {
    overflow_->set_behavior(ButtonBehavior::dropdown); adopt(overflow_); children_.push_back(overflow_);
    overflow_->set_icon(ButtonIcon::more);
    overflow_->set_appearance(ButtonAppearance::subtle);
}
Breadcrumb::~Breadcrumb() { state_->navigate = {}; overflow_->on_click({}); }
std::optional<ItemKey> Breadcrumb::current() const { return segments_.empty() ? std::nullopt : std::optional{segments_.back().key}; }
void Breadcrumb::on_navigate(std::function<void(ItemKey)> callback) { state_->navigate = std::move(callback); }
void Breadcrumb::on_overflow(std::function<void()> callback) { overflow_->on_click(std::move(callback)); }
void Breadcrumb::set_segments(std::vector<PathSegment> segments) {
    if (segments.size() > 64) throw std::length_error("Breadcrumb supports at most 64 segments");
    std::set<ItemKey> keys;
    for (const auto& segment : segments) if (!segment.key.id || !keys.insert(segment.key).second || segment.label.empty() || segment.label.size() > 1024)
        throw std::invalid_argument("Invalid breadcrumb segment");
    if (segments == segments_) return;
    std::vector<std::shared_ptr<Element>> children{overflow_};
    for (std::size_t i = 0; i < segments.size(); ++i) {
        const auto& segment = segments[i];
        const auto old = std::find_if(segments_.begin(), segments_.end(), [&](const auto& value) { return value.key == segment.key; });
        auto button = old == segments_.end() ? std::make_shared<Button>(L"") :
            std::static_pointer_cast<Button>(children_[static_cast<std::size_t>(old - segments_.begin()) + 1]);
        if (old == segments_.end()) button->set_appearance(ButtonAppearance::subtle);
        button->set_name(segment.label + (i + 1 == segments.size() ? L"" : L"  ›"));
        button->set_automation_id(L"segment-" + std::to_wstring(segment.key.id) + L"-" + std::to_wstring(segment.key.version));
        button->set_help_text(i + 1 == segments.size() ? L"Current location" : L"Navigate to this location");
        std::weak_ptr<State> weak = state_;
        button->on_click([weak, key = segment.key] { if (auto state = weak.lock()) { auto callback = state->navigate; if (callback) callback(key); } });
        if (old == segments_.end()) adopt(button);
        children.push_back(button);
    }
    children_ = std::move(children); segments_ = std::move(segments); first_ = 0; invalidate(Invalidation::layout);
}
void Breadcrumb::arrange(Rect bounds) {
    Element::arrange(bounds);
    bounds = this->bounds();
    const auto fit = std::max<std::size_t>(1, static_cast<std::size_t>(std::max(0.0f, bounds.width) / 120));
    first_ = segments_.size() <= fit ? 0 : segments_.size() - std::max<std::size_t>(1, fit - 1);
    float x = bounds.x;
    const auto overflow_width = first_ ? std::min(48.0f, bounds.width / 3) : 0;
    overflow_->arrange(first_ ? Rect{x, bounds.y, overflow_width, bounds.height} : Rect{}); x += overflow_width;
    const auto count = segments_.size() - first_;
    const float width = count ? std::max(0.0f, bounds.width - overflow_width) / static_cast<float>(count) : 0;
    for (std::size_t i = 0; i < segments_.size(); ++i) {
        children_[i + 1]->arrange(i < first_ ? Rect{} : Rect{x, bounds.y, width, bounds.height});
        if (i >= first_) x += width;
    }
}
std::shared_ptr<const CommandSet> Breadcrumb::overflow_commands() const {
    std::vector<CommandRecord> records;
    std::weak_ptr<State> weak = state_;
    for (std::size_t i = 0; i < first_; ++i) {
        const auto segment = segments_[i];
        records.push_back({i + 1, 0, segment.label, [weak, key = segment.key] {
            if (auto state = weak.lock()) { auto callback = state->navigate; if (callback) callback(key); }
        }});
    }
    return std::make_shared<CommandSet>(std::move(records));
}
Control* Breadcrumb::adjacent(const Control& current, int direction) const {
    for (std::size_t i = 0; i < children_.size(); ++i) if (children_[i].get() == &current) {
        for (auto next = static_cast<std::ptrdiff_t>(i) + (direction < 0 ? -1 : 1);
            next >= 0 && next < static_cast<std::ptrdiff_t>(children_.size()); next += direction < 0 ? -1 : 1) {
            const auto target = std::static_pointer_cast<Control>(children_[static_cast<std::size_t>(next)]);
            if (target->enabled() && target->bounds().width > 0 && target->bounds().height > 0) return target.get();
        }
    }
    return nullptr;
}
NavigationPane::NavigationPane(std::wstring name) : Control(ControlRole::content_view, name, {320, 320}) {
    auto content = std::make_shared<Stack>(Axis::vertical); content->set_spacing(4);
    items_ = std::make_shared<ItemsView>(name); items_->set_presentation(ItemsPresentation::grouped);
    group_ = std::make_shared<Expander>(name, items_); content->add(group_, 1);
    progress_ = std::make_shared<Progress>(L"Query state"); progress_->set_fixed_size({300, 22});
    progress_->set_state(ProgressState::unknown); content->add(progress_);
    status_ = std::make_shared<Label>(L""); status_->set_caption(true); content->add(status_);
    adopt(content); children_.push_back(content);
}
NavigationPane::~NavigationPane() { cancel(); items_->on_activate({}); }
void NavigationPane::set_items(std::shared_ptr<const ItemsSource> source) {
    if (!source) throw std::invalid_argument("Navigation source is null");
    cancel();
    items_->set_items(std::move(source)); progress_->set_state(ProgressState::unknown); status_->set_text(L"");
}
void NavigationPane::on_navigate(std::function<void(ItemKey)> callback) { items_->on_activate(std::move(callback)); }
NavigationQuery NavigationPane::request(std::wstring text) {
    if (text.size() > 1024) throw std::length_error("Navigation query exceeds 1024 code units");
    stop_.request_stop(); stop_ = std::stop_source{};
    NavigationQuery request{std::move(text), ++generation_, stop_.get_token()};
    auto callback = query_;
    if (callback) {
        progress_->set_state(ProgressState::indeterminate); status_->set_text(L"Searching…"); auto cancellation = stop_;
        try { callback(request); } catch (...) { cancellation.request_stop(); throw; }
    }
    return request;
}
bool NavigationPane::complete(const NavigationQuery& request, std::shared_ptr<const ItemsSource> source, std::wstring error) {
    if (request.generation != generation_ || request.cancellation != stop_.get_token() || request.cancellation.stop_requested()) return false;
    if (!group_->expanded()) { cancel(); return false; }
    if (error.empty()) set_items(std::move(source));
    else { cancel(); status_->set_text(std::move(error)); progress_->set_state(ProgressState::error); }
    return true;
}
void NavigationPane::cancel() { stop_.request_stop(); ++generation_; Control::cancel(); }
void NavigationPane::arrange(Rect bounds) {
    Element::arrange(bounds);
    bounds = this->bounds();
    if (!group_->expanded() || bounds.width <= 0 || bounds.height <= 0) cancel();
    children_[0]->arrange(bounds);
}
LocationPicker::LocationPicker(std::wstring name) {
    auto content = std::make_shared<Stack>(Axis::vertical); content->set_padding({8, 8, 8, 8});
    content->set_spacing(6); content->set_preferred_size({420, 440});
    toolbar_ = std::make_shared<CommandBar>(L"Location navigation"); content->add(toolbar_);
    editor_ = std::make_shared<TextInput>(L"Find a location"); editor_->set_search_style(true);
    editor_->set_maximum_length(1024); editor_->set_fixed_size({400, 40}); content->add(editor_);
    navigation_ = std::make_shared<NavigationPane>(L"Locations"); content->add(navigation_, 1);
    auto footer = std::make_shared<Label>(L"↑ ↓ Select   Enter Navigate   Esc Cancel"); footer->set_caption(true); content->add(footer);
    std::weak_ptr<NavigationPane> weak = navigation_;
    editor_->on_change([weak](const std::wstring& text) { if (auto pane = weak.lock()) pane->request(text); });
    editor_->on_submit([weak] { if (auto pane = weak.lock()) if (auto key = pane->items()->selection().focused()) pane->items()->activate_item(*key); });
    popup_ = std::make_shared<Popup>(content, std::move(name));
    popup_->set_preferred_size({420, 440});
    popup_->on_dismiss([weak](PopupDismissReason) { if (auto pane = weak.lock()) pane->cancel(); });
}
LocationPicker::~LocationPicker() { navigation_->cancel(); editor_->on_change({}); editor_->on_submit({}); }
ViewPicker::ViewPicker(std::shared_ptr<ItemsView> target) {
    if (!target) throw std::invalid_argument("View picker target is null");
    auto content = std::make_shared<Stack>(Axis::horizontal); content->set_spacing(8);
    content->set_padding({12, 12, 12, 12}); content->set_preferred_size({280, 210});
    choices_ = std::make_shared<RadioGroup>(L"Presentation");
    choices_->set_items({{1, L"List"}, {2, L"Tiles"}, {3, L"Groups"}}, static_cast<std::uint64_t>(target->presentation()) + 1);
    std::weak_ptr<ItemsView> weak = target;
    choices_->on_change([weak](std::uint64_t id) { if (auto items = weak.lock()) items->set_presentation(static_cast<ItemsPresentation>(id - 1)); });
    content->add(choices_, 1);
    size_ = std::make_shared<RangeInput>(L"Item size"); size_->set_orientation(Axis::vertical);
    size_->set_range({48, 160, 8, 32}); size_->set_value(std::clamp<double>(target->item_size().height, 48, 160));
    size_->set_fixed_size({48, 180});
    size_->on_change([weak](double value) { if (auto items = weak.lock()) items->set_item_size({static_cast<float>(value * 3), static_cast<float>(value)}); });
    content->add(size_); popup_ = std::make_shared<Popup>(content, L"View settings");
    popup_->set_preferred_size({280, 210});
}
ViewPicker::~ViewPicker() { choices_->on_change({}); size_->on_change({}); }
}
