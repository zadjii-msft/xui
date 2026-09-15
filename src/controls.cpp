#include "xui/controls.hpp"
#include <algorithm>
#include <cmath>

namespace xui {
void PageView::add_page(std::shared_ptr<Element> content) { add(std::make_shared<ContentView>(std::move(content), L"Page")); }
void PageView::select(std::size_t index) {
    if (index >= child_count()) throw std::out_of_range("Page index");
    if (selected_ == index) return;
    selected_ = index; invalidate(Invalidation::layout);
}
void PageView::arrange(Rect rectangle) {
    Element::arrange(rectangle);
    for (std::size_t i = 0; i < child_count(); ++i) {
        const auto& child = child_at(i);
        if (i == selected_) child->arrange(rectangle);
        else if (child->bounds().width != 0 || child->bounds().height != 0)
            child->arrange({rectangle.x, rectangle.y, 0, 0});
    }
}

Control::Control(ControlRole role, std::wstring name, Size preferred)
    : role_(role), name_(std::move(name)) { set_preferred_size(preferred); }

void Control::set_name(std::wstring name) {
    if (name_ == name) return;
    name_ = std::move(name);
    text_changed();
}
void Control::set_help_text(std::wstring value) {
    if (help_text_ == value) return;
    help_text_ = std::move(value);
    invalidate(Invalidation::paint);
}
void Control::set_tooltip_delay(unsigned value) {
    if (value < 100 || value > 60000) throw std::invalid_argument("Tooltip delay must be 100 to 60000 milliseconds");
    if (tooltip_delay_ == value) return;
    tooltip_delay_ = value;
    invalidate(Invalidation::paint);
}
bool Control::actionable() const {
    return role_ == ControlRole::button || role_ == ControlRole::toggle ||
        role_ == ControlRole::expander || role_ == ControlRole::combo_box;
}
void Control::text_changed() {
    text_dirty_ = true;
    invalidate(auto_size() ? Invalidation::layout : Invalidation::paint);
}
void Control::set_text_measurer(TextMeasurer measurer) {
    measurer_ = std::move(measurer);
    text_dirty_ = true;
    if (measurer_) invalidate(auto_size() ? Invalidation::layout : Invalidation::paint);
}
Size Control::measured_text() {
    if (text_dirty_) {
        // Headless callers can inject a measurer. No guessed glyph widths in the core.
        text_size_ = measurer_ ? measurer_(name_, text_style()) : Size{};
        text_dirty_ = false;
    }
    return text_size_;
}
Size Control::measure(Size available) {
    if (!visible_) return {};
    if (!auto_size() || !measurer_) return Element::measure(available);
    const auto text = measured_text();
    const float inset = role_ == ControlRole::toggle ? 54.0f : role_ == ControlRole::button ? 28.0f : 0;
    const float height = role_ == ControlRole::button ? 36.0f : role_ == ControlRole::toggle ? 32.0f : 24.0f;
    return constrain({text.width + inset, std::max(height, text.height + (inset ? 12.0f : 0.0f))}, available);
}

ScrollView::ScrollView(std::shared_ptr<Element> content, std::wstring name)
    : Control(ControlRole::scroll_view, std::move(name), {320, 240}), content_(std::move(content)) {
    adopt(content_);
}
Size ScrollView::measure(Size available) {
    return Element::measure(available);
}
Rect ScrollView::viewport() const {
    auto result = bounds();
    result.width = std::max(0.0f, result.width - bar_width);
    return result;
}
float ScrollView::maximum_offset() const { return std::max(0.0f, extent_ - bounds().height); }
void ScrollView::arrange(Rect rectangle) {
    Element::arrange(rectangle);
    const auto view = viewport();
    const auto desired = content_->measure({view.width, std::numeric_limits<float>::infinity()});
    extent_ = std::max(view.height, desired.height);
    offset_ = std::clamp(offset_, 0.0f, maximum_offset());
    content_->arrange({view.x, view.y - offset_, view.width, extent_});
}
void ScrollView::set_offset(float value) {
    value = std::isnan(value) ? 0.0f : std::clamp(value, 0.0f, maximum_offset());
    if (offset_ == value) return;
    offset_ = value;
    invalidate(Invalidation::layout);
}
void ScrollView::reveal(Rect target) {
    const auto view = viewport();
    if (target.y < view.y) scroll_by(target.y - view.y);
    else if (target.y + target.height > view.y + view.height)
        scroll_by(std::min(target.y - view.y, target.y + target.height - view.y - view.height));
}
Rect ScrollView::thumb() const {
    const auto view = bounds();
    if (maximum_offset() <= 0 || view.height <= 0) return {};
    const float height = std::min(view.height, std::max(24.0f, view.height * view.height / extent_));
    return {view.x + std::max(0.0f, view.width - bar_width) + 2,
        view.y + (view.height - height) * offset_ / maximum_offset(), std::min(8.0f, view.width), height};
}
void Control::set_enabled(bool enabled) {
    if (enabled_ == enabled) return;
    enabled_ = enabled;
    if (!enabled) { cancel(); hovered_ = false; focused_ = false; }
    invalidate(Invalidation::paint);
}
void Control::set_focused(bool focused) {
    focused = focused && focusable();
    if (focused_ == focused) return;
    focused_ = focused;
    if (!focused) cancel();
    invalidate(Invalidation::paint);
    if (focused && focus_) { auto callback = focus_; callback(); }
}
void Control::pointer_move(bool inside) {
    inside = inside && enabled_;
    if (hovered_ == inside) return;
    hovered_ = inside;
    invalidate(Invalidation::paint);
}
bool Control::pointer_down() {
    if (!enabled_ || !hovered_ || !actionable())
        return false;
    pointer_ = true;
    keyboard_ = false;
    invalidate(Invalidation::paint);
    return true;
}
bool Control::pointer_up(bool inside) {
    const bool fire = pointer_ && inside;
    cancel();
    return fire && invoke();
}
void Control::cancel() {
    if (!pointer_ && !keyboard_) return;
    pointer_ = keyboard_ = false;
    invalidate(Invalidation::paint);
}
bool Control::key_down(ActivationKey key, bool repeat) {
    if (!enabled_ || !focused_ || repeat || pointer_) return false;
    if (key == ActivationKey::enter) return role_ != ControlRole::toggle && invoke();
    if (!actionable()) return false;
    keyboard_ = true;
    invalidate(Invalidation::paint);
    return true;
}
bool Control::key_up(ActivationKey key) {
    const bool fire = key == ActivationKey::space && keyboard_ && focused_;
    cancel();
    return fire && invoke();
}
bool Control::invoke() {
    if (!enabled_ || !actionable()) return false;
    activate();
    return true;
}
void Button::activate() {
    const auto callback = click_;
    const auto toggle = toggle_;
    const bool checked = !checked_;
    if (behavior_ == ButtonBehavior::toggle) {
        set_checked(checked);
        if (toggle) toggle(checked);
        return;
    }
    if (callback) callback();
}
void Button::set_behavior(ButtonBehavior value) {
    if (value < ButtonBehavior::momentary || value > ButtonBehavior::dropdown)
        throw std::invalid_argument("Invalid button behavior");
    if (behavior_ == value) return;
    cancel(); behavior_ = value; invalidate(Invalidation::paint);
}
void Button::set_checked(bool value) {
    if (checked_ == value) return;
    checked_ = value; invalidate(Invalidation::paint);
}
void Button::set_repeat_timing(unsigned delay, unsigned interval) {
    if (delay < 100 || delay > 60000 || interval < 16 || interval > 60000)
        throw std::invalid_argument("Invalid repeat timing");
    if (repeat_delay_ == delay && repeat_interval_ == interval) return;
    cancel(); repeat_delay_ = delay; repeat_interval_ = interval;
}
void Toggle::set_checked(bool checked) {
    if (checked_ == checked) return;
    checked_ = checked;
    invalidate(Invalidation::paint);
}
void Toggle::activate() {
    set_checked(!checked_);
    const auto callback = change_;
    if (callback) callback(checked_);
}
void TextInput::set_text(std::wstring text) {
    ++suggestion_revision_;
    assign_text(std::move(text));
    if (suggestions_) invalidate(Invalidation::paint);
}
TextInput::Selection TextInput::normalize_selection(std::wstring_view text, Selection value) {
    value.start = std::min(value.start, text.size());
    value.end = std::min(value.end, text.size());
    if (value.start > value.end) std::swap(value.start, value.end);
    const auto splits_pair = [&](std::size_t offset) {
        return offset > 0 && offset < text.size() &&
            text[offset - 1] >= 0xd800 && text[offset - 1] <= 0xdbff &&
            text[offset] >= 0xdc00 && text[offset] <= 0xdfff;
    };
    if (value.start == value.end) {
        if (splits_pair(value.start)) --value.start;
        value.end = value.start;
    } else {
        if (splits_pair(value.start)) --value.start;
        if (splits_pair(value.end)) ++value.end;
    }
    return value;
}
TextInput::Selection TextInput::selection() const {
    return read_selection_ ? read_selection_() : normalize_selection(text_, selection_);
}
void TextInput::set_selection(Selection value) {
    value = normalize_selection(text_, value);
    if (write_selection_) write_selection_(value);
    selection_ = value;
}
void TextInput::bind_selection(std::function<Selection()> reader, std::function<void(Selection)> writer) {
    writer(normalize_selection(text_, selection_));
    read_selection_ = std::move(reader);
    write_selection_ = std::move(writer);
}
void TextInput::assign_text(std::wstring text) {
    const auto nul = text.find(L'\0');
    if (nul != std::wstring::npos) text.resize(nul);
    if (text.size() > maximum_length_) {
        text.resize(maximum_length_);
        if constexpr (sizeof(wchar_t) == 2) {
            if (text.back() >= 0xd800 && text.back() <= 0xdbff) text.pop_back();
        }
    }
    if (text_ == text) return;
    text_ = std::move(text);
    invalidate(Invalidation::paint);
}
void TextInput::set_maximum_length(std::size_t value) {
    maximum_length_ = std::clamp<std::size_t>(value, 1, 32767);
    set_text(text_);
    invalidate(Invalidation::paint);
}

void TabStrip::set_tabs(std::vector<TabItem> tabs, std::optional<std::uint64_t> selected) {
    for (std::size_t i = 0; i < tabs.size(); ++i) {
        if (!tabs[i].id || tabs[i].id > static_cast<std::uint64_t>(std::numeric_limits<std::intptr_t>::max()) - 100)
            throw std::invalid_argument("Tab identity is outside the supported range");
        for (std::size_t j = 0; j < i; ++j)
            if (tabs[j].id == tabs[i].id) throw std::invalid_argument("Duplicate tab identity");
    }
    if (selected && std::none_of(tabs.begin(), tabs.end(), [&](const auto& tab) { return tab.id == *selected; }))
        throw std::invalid_argument("Selected tab must exist");
    if (!tabs.empty() && !selected) selected = tabs.front().id;
    tabs_ = std::move(tabs);
    selected_ = selected;
    reveal_selected();
    invalidate(Invalidation::paint);
}
bool TabStrip::select(std::uint64_t id) {
    if (!enabled() || std::none_of(tabs_.begin(), tabs_.end(), [&](const auto& tab) { return tab.id == id; })) return false;
    if (selected_ == id) return true;
    selected_ = id;
    reveal_selected();
    invalidate(Invalidation::paint);
    if (select_) { auto callback = select_; callback(id); }
    return true;
}
void TabStrip::step(int delta) {
    if (tabs_.empty()) return;
    auto found = std::find_if(tabs_.begin(), tabs_.end(), [&](const auto& tab) { return selected_ == tab.id; });
    auto index = found == tabs_.end() ? 0 : static_cast<std::size_t>(found - tabs_.begin());
    index = delta < 0 ? (index ? index - 1 : tabs_.size() - 1) : (index + 1) % tabs_.size();
    select(tabs_[index].id);
}
void TabStrip::request_close(std::uint64_t id) {
    if (enabled() && close_ && std::any_of(tabs_.begin(), tabs_.end(), [&](const auto& tab) { return tab.id == id; })) {
        auto callback = close_;
        callback(id);
    }
}
Rect TabStrip::tab_bounds(std::size_t index) const {
    if (index >= tabs_.size() || index < first_) return {};
    const auto width = std::min(180.0f, bounds().width / std::max(1.0f, std::min(3.0f, static_cast<float>(tabs_.size()))));
    const float x = (index - first_) * width;
    return {x, 0, std::max(0.0f, std::min(width, bounds().width - x)), bounds().height};
}
std::optional<std::size_t> TabStrip::hit_test(float x) const {
    if (x < 0 || x >= bounds().width) return {};
    for (std::size_t i = first_; i < tabs_.size(); ++i) {
        const auto rect = tab_bounds(i);
        if (x >= rect.x && x < rect.x + rect.width) return i;
    }
    return {};
}
void TabStrip::reveal_selected() {
    first_ = std::min(first_, tabs_.empty() ? 0 : tabs_.size() - 1);
    if (!selected_) return;
    for (std::size_t i = 0; i < tabs_.size(); ++i) if (tabs_[i].id == selected_) {
        if (i < first_) first_ = i;
        while (first_ < i && tab_bounds(i).width < std::min(120.0f, bounds().width)) ++first_;
        break;
    }
}
void TabStrip::arrange(Rect rect) { Element::arrange(rect); reveal_selected(); }
ContentView::ContentView(std::shared_ptr<Element> content, std::wstring name)
    : Control(ControlRole::content_view, std::move(name), {320, 240}), content_(std::move(content)) { adopt(content_); }
void ContentView::arrange(Rect rect) {
    Element::arrange(rect);
    content_->measure({bounds().width, bounds().height});
    content_->arrange(bounds());
}
SplitView::SplitView(std::shared_ptr<Element> first, std::shared_ptr<Element> second, std::wstring name)
    : Control(ControlRole::split_view, std::move(name), {640, 480}),
      first_(std::make_shared<ContentView>(std::move(first), L"Left pane")),
      second_(std::make_shared<ContentView>(std::move(second), L"Right pane")) {
    adopt(first_); adopt(second_);
}
bool SplitView::expanded() const { return secondary_visible_ && bounds().width >= 2 * minimum_pane_width + divider_width; }
Rect SplitView::divider() const {
    if (!expanded()) return {};
    const auto b = bounds();
    const float width = b.width - divider_width;
    const float left = std::clamp(width * ratio_, minimum_pane_width, width - minimum_pane_width);
    return {b.x + left, b.y, divider_width, b.height};
}
void SplitView::arrange(Rect rect) {
    Element::arrange(rect);
    const auto b = bounds();
    const auto d = divider();
    first_->arrange({b.x, b.y, expanded() ? d.x - b.x : b.width, b.height});
    second_->arrange({expanded() ? d.x + d.width : b.x + b.width, b.y,
        expanded() ? b.x + b.width - d.x - d.width : 0, expanded() ? b.height : 0});
    const bool value = expanded();
    if (arranged_expanded_ != value) {
        arranged_expanded_ = value;
        auto callback = expanded_callback_;
        if (callback) callback(value);
    }
}
void SplitView::set_ratio(float value) {
    if (!std::isfinite(value)) return;
    value = std::clamp(value, 0.1f, 0.9f);
    if (value == ratio_) return;
    ratio_ = value;
    invalidate(Invalidation::layout);
}
void SplitView::set_secondary_visible(bool value) {
    if (secondary_visible_ == value) return;
    secondary_visible_ = value;
    invalidate(Invalidation::layout);
}
void TextInput::commit_text(std::wstring text) {
    if (text_ == text) return;
    const auto previous = text_;
    assign_text(std::move(text));
    if (text_ == previous) return;
    const auto callback = change_;
    if (callback) {
        const auto value = text_;
        callback(value);
    }
}
Control* next_focus(std::span<Control* const> controls, const Control* current, bool reverse) {
    if (controls.empty()) return nullptr;
    const auto found = std::find(controls.begin(), controls.end(), current);
    size_t index = found == controls.end() ? (reverse ? 0 : controls.size() - 1)
                                          : static_cast<size_t>(found - controls.begin());
    for (size_t count = 0; count < controls.size(); ++count) {
        index = reverse ? (index ? index - 1 : controls.size() - 1) : (index + 1) % controls.size();
        if (controls[index]->focusable()) return controls[index];
    }
    return nullptr;
}

}
