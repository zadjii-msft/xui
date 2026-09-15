#include "xui/foundation.hpp"
#include <algorithm>
#include <cmath>
#include <cwctype>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace xui {
namespace {
constexpr float expander_content_padding = 16;
constexpr float expander_body_border = 1;
constexpr float expander_content_inset = expander_content_padding + expander_body_border;
constexpr float expander_body_chrome = 2 * expander_content_padding + expander_body_border;
constexpr float expander_header_chrome = 16 + 20 + 32 + 8 + 2;
void bounded(double value, const NumericRange& range) {
    if (!std::isfinite(value) || value < range.minimum || value > range.maximum)
        throw std::invalid_argument("Value must be finite and within the range");
}
double moved(double value, const NumericRange& range, RangeKey key) {
    switch (key) {
    case RangeKey::minimum: return range.minimum;
    case RangeKey::maximum: return range.maximum;
    case RangeKey::decrease: return std::max(range.minimum, value - range.small_step);
    case RangeKey::increase: return std::min(range.maximum, value + range.small_step);
    case RangeKey::page_decrease: return std::max(range.minimum, value - range.large_step);
    case RangeKey::page_increase: return std::min(range.maximum, value + range.large_step);
    }
    throw std::invalid_argument("Invalid range key");
}
auto choice(const std::vector<ChoiceItem>& items, std::uint64_t id) {
    return std::find_if(items.begin(), items.end(), [id](const auto& item) { return item.id == id; });
}
void require_choice(const std::vector<ChoiceItem>& items, std::uint64_t id) {
    const auto it = choice(items, id);
    if (it == items.end() || !it->enabled) throw std::invalid_argument("Selected choice must exist and be enabled");
}
struct NumericLayout { Rect editor, decrease, increase; };
NumericLayout numeric_layout(Rect bounds, VisualStyle style, NumberSpinPlacement placement) {
    if (placement == NumberSpinPlacement::hidden)
        return {bounds, {bounds.x + bounds.width, bounds.y, 0, 0}, {bounds.x + bounds.width, bounds.y, 0, 0}};
    if (style == VisualStyle::winui) {
        // Auto columns: Up has margin 4; Down has margin 0,4,4,4.
        const float scale = std::min(1.0f, bounds.width / 76);
        const float button_origin = bounds.x + std::max(0.0f, bounds.width - 76);
        const float editor_width = std::max(0.0f, bounds.width - 72);
        const float inset = std::min(4.0f, bounds.height / 2);
        const float height = std::max(0.0f, bounds.height - 2 * inset);
        return {{bounds.x, bounds.y, editor_width, bounds.height},
            {button_origin + 40 * scale, bounds.y + inset, 32 * scale, height},
            {button_origin + 4 * scale, bounds.y + inset, 32 * scale, height}};
    }
    const float button_width = std::min(40.0f, bounds.width / 2);
    const float editor_width = bounds.width - 2 * button_width;
    return {{bounds.x, bounds.y, editor_width, bounds.height},
        {bounds.x + editor_width, bounds.y, button_width, bounds.height},
        {bounds.x + editor_width + button_width, bounds.y, button_width, bounds.height}};
}
}
void NumericRange::validate() const {
    if (!std::isfinite(minimum) || !std::isfinite(maximum) || minimum >= maximum ||
        !std::isfinite(maximum - minimum) || !std::isfinite(small_step) || small_step <= 0 ||
        !std::isfinite(large_step) || large_step <= 0)
        throw std::invalid_argument("Range requires finite increasing bounds and positive finite steps");
}
RangeInput::RangeInput(std::wstring name) : Control(ControlRole::range_input, std::move(name), {320, 42}) {}
void RangeInput::set_range(NumericRange value) {
    value.validate();
    if (range_ == value) return;
    cancel(); range_ = value; value_ = std::clamp(value_, value.minimum, value.maximum);
    invalidate(Invalidation::paint);
}
void RangeInput::set_value(double value) {
    bounded(value, range_);
    if (value_ == value && !preview_) return;
    preview_.reset(); value_ = value; invalidate(Invalidation::paint);
}
void RangeInput::set_orientation(Axis value) {
    if (value != Axis::horizontal && value != Axis::vertical) throw std::invalid_argument("Invalid orientation");
    if (orientation_ == value) return;
    cancel(); orientation_ = value; invalidate(Invalidation::paint);
}
void RangeInput::set_reversed(bool value) {
    if (reversed_ == value) return;
    cancel(); reversed_ = value; invalidate(Invalidation::paint);
}
bool RangeInput::change_value(double value) {
    bounded(value, range_);
    if (!enabled()) return false;
    const bool changed = value_ != value;
    if (!changed && !preview_) return true;
    set_value(value);
    auto callback = change_;
    if (changed && callback) callback(value);
    return true;
}
bool RangeInput::move(RangeKey key) { return change_value(moved(value_, range_, key)); }
bool RangeInput::begin_drag(double fraction) {
    if (!enabled()) return false;
    if (!std::isfinite(fraction)) throw std::invalid_argument("Pointer fraction must be finite");
    preview_ = value_;
    return drag(fraction);
}
bool RangeInput::drag(double fraction) {
    if (!std::isfinite(fraction)) throw std::invalid_argument("Pointer fraction must be finite");
    if (!enabled() || !preview_) return false;
    fraction = std::clamp(fraction, 0.0, 1.0);
    if (reversed_) fraction = 1 - fraction;
    const double raw = range_.minimum + fraction * (range_.maximum - range_.minimum);
    const double steps = (raw - range_.minimum) / range_.small_step;
    const double snapped = std::isfinite(steps) ? range_.minimum + std::round(steps) * range_.small_step : raw;
    const double value = std::clamp(snapped, range_.minimum, range_.maximum);
    if (*preview_ == value) return true;
    preview_ = value;
    invalidate(Invalidation::paint);
    auto callback = preview_callback_;
    if (callback) callback(value);
    return true;
}
bool RangeInput::commit_drag() {
    if (!preview_) return false;
    const auto value = *preview_;
    preview_.reset();
    invalidate(Invalidation::paint);
    return change_value(value);
}
void RangeInput::cancel() {
    Control::cancel();
    if (!preview_) return;
    preview_.reset(); invalidate(Invalidation::paint);
}
void RangeInput::cancel_drag() {
    if (!preview_) return;
    const auto value = value_;
    cancel();
    auto callback = cancel_callback_;
    if (callback) callback(value);
}

RadioGroup::RadioGroup(std::wstring name, bool list)
    : Control(list ? ControlRole::choice_list : ControlRole::radio_group, std::move(name), {320, 136}) {}
void RadioGroup::set_items(std::vector<ChoiceItem> items, std::optional<std::uint64_t> selected) {
    if (items.size() > 4096) throw std::invalid_argument("Choice controls support at most 4096 items");
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (!items[i].id || items[i].id > static_cast<std::uint64_t>(std::numeric_limits<std::intptr_t>::max()) - 100)
            throw std::invalid_argument("Invalid choice identity");
        for (std::size_t j = 0; j < i; ++j)
            if (items[j].id == items[i].id) throw std::invalid_argument("Duplicate choice identity");
    }
    if (!selected && selected_) {
        const auto old = choice(items, *selected_);
        if (old != items.end() && old->enabled) selected = selected_;
    }
    if (selected) require_choice(items, *selected);
    else {
        auto first = std::find_if(items.begin(), items.end(), [](const auto& item) { return item.enabled; });
        if (first != items.end()) selected = first->id;
    }
    if (items_ == items && selected_ == selected) return;
    items_ = std::move(items); selected_ = selected; reveal_selected();
    invalidate(visual_style() == VisualStyle::winui && !preferred_size_explicit() ?
        Invalidation::layout : Invalidation::paint);
}
void RadioGroup::set_selected(std::uint64_t id) {
    require_choice(items_, id);
    if (selected_ == id) return;
    selected_ = id; reveal_selected(); invalidate(Invalidation::paint);
}
bool RadioGroup::select(std::uint64_t id) {
    require_choice(items_, id);
    if (!enabled()) return false;
    if (selected_ == id) return true;
    set_selected(id);
    auto callback = change_;
    if (callback) callback(id);
    return true;
}
bool RadioGroup::step(int delta) {
    if (!enabled() || items_.empty()) return false;
    auto it = selected_ ? choice(items_, *selected_) : items_.end();
    auto index = it == items_.end() ? (delta < 0 ? 0 : items_.size() - 1) : static_cast<std::size_t>(it - items_.begin());
    for (std::size_t count = 0; count < items_.size(); ++count) {
        index = delta < 0 ? (index ? index - 1 : items_.size() - 1) : (index + 1) % items_.size();
        if (items_[index].enabled) return select(items_[index].id);
    }
    return false;
}
bool RadioGroup::type_ahead(std::wstring_view prefix) {
    if (prefix.empty() || !enabled()) return false;
    for (const auto& item : items_) if (item.enabled && item.text.size() >= prefix.size() &&
        std::equal(prefix.begin(), prefix.end(), item.text.begin(), [](wchar_t a, wchar_t b) {
            return std::towlower(a) == std::towlower(b);
        })) return select(item.id);
    return false;
}
bool RadioGroup::accept() {
    if (!enabled() || !selected_) return false;
    auto callback = accept_;
    const auto id = *selected_;
    if (callback) callback(id);
    return true;
}
float RadioGroup::effective_row_height() const {
    return visual_style() == VisualStyle::winui ?
        (role() == ControlRole::choice_list ? item_height_ : 32.0f) : row_height;
}
float RadioGroup::effective_row_pitch() const {
    return effective_row_height() +
        (visual_style() == VisualStyle::winui && role() == ControlRole::choice_list ? 4.0f : 0.0f);
}
float RadioGroup::effective_vertical_padding() const {
    // ItemsPresenter margin 4 plus the popup border 1.
    return visual_style() == VisualStyle::winui && role() == ControlRole::choice_list ? 5.0f : 0.0f;
}
float RadioGroup::effective_horizontal_padding() const {
    return visual_style() == VisualStyle::winui && role() == ControlRole::choice_list ? 6.0f : 0.0f;
}
Size RadioGroup::measure(Size available) {
    if (!visible()) return {};
    if (visual_style() == VisualStyle::winui && role() == ControlRole::choice_list) {
        const float line_height = measured_text().height;
        item_height_ = (line_height > 0 ? line_height : 19.0f) + 12;
        reveal_selected();
    }
    auto desired = Control::measure(available);
    if (visual_style() == VisualStyle::winui && role() == ControlRole::radio_group && !preferred_size_explicit())
        desired.height = static_cast<float>(items_.size()) * effective_row_height();
    return constrain(desired, available);
}
void RadioGroup::presentation_changed() { reveal_selected(); }
Rect RadioGroup::item_bounds(std::size_t index) const {
    if (index < first_ || index >= items_.size()) return {};
    const float padding = effective_vertical_padding();
    const float margin = (effective_row_pitch() - effective_row_height()) / 2;
    const float y = padding + margin + static_cast<float>(index - first_) * effective_row_pitch();
    const float x = std::min(effective_horizontal_padding(), bounds().width / 2);
    return {x, y, std::max(0.0f, bounds().width - 2 * x),
        std::max(0.0f, std::min(effective_row_height(), bounds().height - padding - y))};
}
std::optional<Rect> RadioGroup::selected_item_bounds() const {
    if (!selected_) return {};
    const auto it = choice(items_, *selected_);
    if (it == items_.end()) return {};
    const auto value = item_bounds(static_cast<std::size_t>(it - items_.begin()));
    return value.height > 0 ? std::optional(value) : std::nullopt;
}
std::optional<std::size_t> RadioGroup::hit_test(float y) const {
    const float padding = effective_vertical_padding();
    if (!std::isfinite(y) || y < padding || y >= bounds().height - padding) return {};
    const auto index = first_ + static_cast<std::size_t>((y - padding) / effective_row_pitch());
    if (index >= items_.size()) return {};
    const auto row = item_bounds(index);
    return y >= row.y && y < row.y + row.height ? std::optional(index) : std::nullopt;
}
void RadioGroup::reveal_selected() {
    first_ = std::min(first_, items_.empty() ? 0 : items_.size() - 1);
    if (!selected_) return;
    const auto it = choice(items_, *selected_);
    if (it == items_.end()) return;
    const auto index = static_cast<std::size_t>(it - items_.begin());
    const float height = std::max(0.0f, bounds().height - 2 * effective_vertical_padding());
    const auto rows = std::max<std::size_t>(1, static_cast<std::size_t>(height / effective_row_pitch()));
    if (visual_style() == VisualStyle::winui)
        first_ = std::min(first_, items_.size() > rows ? items_.size() - rows : 0);
    if (index < first_) first_ = index;
    else if (index >= first_ + rows) first_ = index - rows + 1;
}
void RadioGroup::arrange(Rect value) { Element::arrange(value); reveal_selected(); }

Rect place_popup(Rect anchor, Size desired, Rect viewport, PopupPlacement placement) {
    for (const auto value : {anchor.x, anchor.y, anchor.width, anchor.height, desired.width, desired.height,
        viewport.x, viewport.y, viewport.width, viewport.height})
        if (!std::isfinite(value)) throw std::invalid_argument("Popup geometry must be finite");
    if (anchor.width < 0 || anchor.height < 0 || desired.width <= 0 || desired.height <= 0 ||
        viewport.width < 0 || viewport.height < 0) throw std::invalid_argument("Invalid popup size");
    Rect result{anchor.x, anchor.y + anchor.height, std::min(desired.width, viewport.width), std::min(desired.height, viewport.height)};
    const float right = viewport.x + viewport.width, bottom = viewport.y + viewport.height;
    switch (placement) {
    case PopupPlacement::below:
        if (result.y + result.height > bottom && anchor.y - result.height >= viewport.y) result.y = anchor.y - result.height;
        break;
    case PopupPlacement::above:
        result.y = anchor.y - result.height;
        if (result.y < viewport.y && anchor.y + anchor.height + result.height <= bottom) result.y = anchor.y + anchor.height;
        break;
    case PopupPlacement::right:
        result.x = anchor.x + anchor.width; result.y = anchor.y;
        if (result.x + result.width > right && anchor.x - result.width >= viewport.x) result.x = anchor.x - result.width;
        break;
    case PopupPlacement::left:
        result.x = anchor.x - result.width; result.y = anchor.y;
        if (result.x < viewport.x && anchor.x + anchor.width + result.width <= right) result.x = anchor.x + anchor.width;
        break;
    case PopupPlacement::center:
        result.x = viewport.x + (viewport.width - result.width) / 2;
        result.y = viewport.y + (viewport.height - result.height) / 2;
        break;
    default: throw std::invalid_argument("Invalid popup placement");
    }
    result.x = std::clamp(result.x, viewport.x, right - result.width);
    result.y = std::clamp(result.y, viewport.y, bottom - result.height);
    return result;
}
Popup::Popup(std::shared_ptr<Element> content, std::wstring name)
    : Control(ControlRole::popup, std::move(name), {320, 240}), children_{std::move(content)} { adopt(children_[0]); }
void Popup::arrange(Rect value) {
    Element::arrange(value);
    children_[0]->measure({value.width, value.height});
    children_[0]->arrange(value);
}
void Popup::set_placement(PopupPlacement value) {
    if (value < PopupPlacement::below || value > PopupPlacement::center) throw std::invalid_argument("Invalid popup placement");
    if (placement_ == value) return;
    placement_ = value; invalidate(Invalidation::layout);
}
void Popup::set_window_background(bool value) {
    if (window_background_ == value) return;
    window_background_ = value; invalidate(Invalidation::layout);
}
void Popup::opened() {
    if (open_) throw std::logic_error("Popup is already open");
    open_ = true; ++generation_;
}
void Popup::closed(PopupDismissReason reason) {
    auto callback = close_transition(reason);
    if (callback) callback();
}
std::function<void()> Popup::close_transition(PopupDismissReason reason) {
    if (!open_) return {};
    open_ = false; ++generation_;
    arrange({}); invalidate(Invalidation::layout);
    auto callback = dismiss_;
    return callback ? std::function<void()>([callback = std::move(callback), reason] { callback(reason); }) : std::function<void()>{};
}

ComboBox::ComboBox(std::wstring name, bool editable)
    : Control(ControlRole::combo_box, name, {320, 42}), choices_(std::make_shared<RadioGroup>(name + L" choices", true)),
      popup_(std::make_shared<Popup>(choices_, name + L" popup")) {
    if (editable) {
        editor_ = std::make_shared<TextInput>(name + L" text");
        editor_->set_caption_visible(false);
        editor_->on_change([this](const auto& text) { auto callback = edit_; if (callback) callback(text); });
        children_.push_back(editor_); adopt(editor_);
    }
}
ComboBox::~ComboBox() { if (editor_) editor_->on_change({}); }
void ComboBox::set_items(std::vector<ChoiceItem> items, std::optional<std::uint64_t> selected) {
    if (!selected && selected_) {
        const auto old = choice(items, *selected_);
        if (old != items.end() && old->enabled) selected = selected_;
    }
    choices_->set_items(std::move(items), selected);
    selected_ = choices_->selected();
    if (editor_) editor_->set_text(selected_text());
    invalidate(Invalidation::paint);
}
void ComboBox::set_selected(std::uint64_t id) {
    require_choice(items(), id);
    if (selected_ == id) return;
    selected_ = id;
    if (editor_) editor_->set_text(selected_text());
    invalidate(Invalidation::paint);
}
bool ComboBox::select(std::uint64_t id) {
    require_choice(items(), id);
    if (!enabled()) return false;
    if (selected_ == id) return true;
    set_selected(id);
    auto callback = change_;
    if (callback) callback(id);
    return true;
}
std::wstring ComboBox::selected_text() const {
    const auto it = selected_ ? choice(items(), *selected_) : items().end();
    return it == items().end() ? L"" : it->text;
}
void ComboBox::prepare_popup() {
    if (selected_) choices_->set_selected(*selected_);
    update_popup_size();
}
void ComboBox::update_popup_size() {
    choices_->measure({std::max(160.0f, bounds().width), 504});
    const float row = choices_->effective_row_pitch();
    const float padding = 2 * choices_->effective_vertical_padding();
    const float maximum = visual_style() == VisualStyle::winui ? 504.0f : 8 * row;
    popup_->set_preferred_size({std::max(160.0f, bounds().width),
        std::min(maximum, std::max(1.0f, static_cast<float>(items().size())) * row + padding)});
}
Rect ComboBox::drop_down_bounds() const {
    const auto value = bounds();
    const float width = std::min(value.width, visual_style() == VisualStyle::winui ? 38.0f : 40.0f);
    return {value.x + value.width - width, value.y, width, value.height};
}
Rect ComboBox::editor_bounds() const {
    const auto value = bounds();
    return {value.x, value.y, value.width - drop_down_bounds().width, value.height};
}
void ComboBox::presentation_changed() {
    choices_->set_visual_style(visual_style());
    popup_->set_visual_style(visual_style());
    if (editor_) editor_->set_visual_style(visual_style());
    update_popup_size();
    arrange(bounds());
}
void ComboBox::arrange(Rect value) {
    Element::arrange(value);
    if (editor_) editor_->arrange(editor_bounds());
}

NumericInput::NumericInput(std::wstring name)
    : Control(ControlRole::numeric_input, name, {320, 44}), editor_(std::make_shared<TextInput>(name + L" text")),
      decrease_(std::make_shared<Button>(name + L" decrease")), increase_(std::make_shared<Button>(name + L" increase")),
      children_{editor_, decrease_, increase_} {
    editor_->set_caption_visible(false); editor_->set_maximum_length(128);
    editor_->on_change([this](const auto& text) { commit_text(text); });
    decrease_->on_click([this] { step(-1); }); increase_->on_click([this] { step(1); });
    for (const auto& child : children_) adopt(child);
    format();
}
NumericInput::~NumericInput() {
    editor_->on_change({}); decrease_->on_click({}); increase_->on_click({});
}
void NumericInput::format() {
    std::wostringstream stream; stream.imbue(locale_);
    stream << std::setprecision(std::numeric_limits<double>::max_digits10) << value_;
    editor_->set_text(stream.str()); valid_ = true;
    editor_->set_help_text(L"");
}
void NumericInput::set_locale(std::locale value) {
    if (locale_ == value) return;
    locale_ = std::move(value); format(); invalidate(Invalidation::paint);
}
void NumericInput::set_range(NumericRange value) {
    value.validate();
    if (range_ == value) return;
    range_ = value; value_ = std::clamp(value_, value.minimum, value.maximum);
    format(); invalidate(Invalidation::paint);
}
void NumericInput::set_value(double value) {
    bounded(value, range_);
    if (value_ == value && valid_) return;
    value_ = value; format(); invalidate(Invalidation::paint);
}
bool NumericInput::change_value(double value) {
    bounded(value, range_);
    if (!enabled()) return false;
    const bool changed = value_ != value;
    set_value(value);
    auto callback = change_;
    if (changed && callback) callback(value);
    return true;
}
bool NumericInput::step(int direction) {
    return change_value(moved(value_, range_, direction < 0 ? RangeKey::decrease : RangeKey::increase));
}
bool NumericInput::commit_text(const std::wstring& text) {
    if (!enabled()) return false;
    std::wistringstream stream(text); stream.imbue(locale_);
    double value{};
    stream >> value;
    const bool parsed = !stream.fail();
    stream >> std::ws;
    const bool valid = parsed && stream.eof() && std::isfinite(value) && value >= range_.minimum && value <= range_.maximum;
    editor_->set_text(text);
    if (valid_ != valid) { valid_ = valid; invalidate(Invalidation::paint); }
    editor_->set_help_text(valid ? L"" : L"Enter a finite number within the allowed range.");
    if (!valid) return false;
    const bool changed = value_ != value;
    value_ = value; invalidate(Invalidation::paint);
    auto callback = change_;
    if (changed && callback) callback(value);
    return true;
}
void NumericInput::arrange(Rect value) {
    Element::arrange(value);
    const auto layout = numeric_layout(bounds(), visual_style(), spin_placement_);
    editor_->arrange(layout.editor);
    decrease_->arrange(layout.decrease);
    increase_->arrange(layout.increase);
}
Rect NumericInput::editor_bounds() const { return numeric_layout(bounds(), visual_style(), spin_placement_).editor; }
Rect NumericInput::decrease_bounds() const { return numeric_layout(bounds(), visual_style(), spin_placement_).decrease; }
Rect NumericInput::increase_bounds() const { return numeric_layout(bounds(), visual_style(), spin_placement_).increase; }
void NumericInput::set_spin_placement(NumberSpinPlacement value) {
    if (value != NumberSpinPlacement::hidden && value != NumberSpinPlacement::inline_buttons)
        throw std::invalid_argument("Invalid number spin placement");
    if (spin_placement_ == value) return;
    spin_placement_ = value;
    for (const auto& button : {decrease_, increase_}) {
        button->cancel();
        button->set_visible(value == NumberSpinPlacement::inline_buttons);
    }
    arrange(bounds());
    invalidate(Invalidation::layout);
}
void NumericInput::presentation_changed() {
    editor_->set_visual_style(visual_style());
    for (const auto& button : {decrease_, increase_}) {
        button->set_visual_style(visual_style());
        button->set_appearance(visual_style() == VisualStyle::winui ? ButtonAppearance::subtle : ButtonAppearance::standard);
    }
    arrange(bounds());
}

Expander::Expander(std::wstring header, std::shared_ptr<Element> content)
    : Control(ControlRole::expander, std::move(header), {320, 180}), children_{std::move(content)} { adopt(children_[0]); }
void Expander::set_expanded(bool value) {
    if (expanded_ == value) return;
    expanded_ = value; invalidate(Invalidation::layout);
}
void Expander::activate() {
    set_expanded(!expanded_);
    auto callback = change_;
    if (callback) callback(expanded_);
}
Size Expander::measure(Size available) {
    if (visual_style() == VisualStyle::classic) {
        if (expanded_) return Element::measure(available);
        return constrain({Element::measure(available).width, header_height}, available);
    }
    if (!visible()) return {};
    const auto text = measured_text();
    measured_header_height_ = std::max(48.0f, text.height);
    if (preferred_size_explicit()) return Element::measure(available);
    Size desired{text.width + expander_header_chrome, effective_header_height()};
    if (expanded_) {
        const auto limit = constrain(available, available);
        const auto content = children_[0]->measure({std::max(0.0f, limit.width - 2 * expander_content_inset),
            std::max(0.0f, limit.height - effective_header_height() - expander_body_chrome)});
        desired.width = std::max(desired.width, content.width + 2 * expander_content_inset);
        desired.height += content.height + expander_body_chrome;
    }
    return constrain(desired, available);
}
float Expander::effective_header_height() const {
    return visual_style() == VisualStyle::winui ? measured_header_height_ : header_height;
}
Rect Expander::header_bounds() const {
    const auto value = bounds();
    return {value.x, value.y, value.width, std::min(effective_header_height(), value.height)};
}
Rect Expander::content_bounds() const {
    const auto value = bounds();
    const float body_y = value.y + header_bounds().height;
    if (!expanded_) return {value.x, body_y, 0, 0};
    const float body_height = std::max(0.0f, value.height - effective_header_height());
    if (visual_style() == VisualStyle::classic) return {value.x, body_y, value.width, body_height};
    return {value.x + std::min(expander_content_inset, value.width / 2),
        body_y + std::min(expander_content_padding, body_height),
        std::max(0.0f, value.width - 2 * expander_content_inset),
        std::max(0.0f, body_height - expander_body_chrome)};
}
void Expander::presentation_changed() { arrange(bounds()); }
void Expander::arrange(Rect value) {
    Element::arrange(value);
    const auto content = content_bounds();
    children_[0]->measure({content.width, content.height}); children_[0]->arrange(content);
}
Progress::Progress(std::wstring name) : Control(ControlRole::progress, std::move(name), {320, 42}) {}
void Progress::set_range(double minimum, double maximum) {
    NumericRange value{minimum, maximum}; value.validate();
    if (range_ == value) return;
    range_ = value; value_ = std::clamp(value_, minimum, maximum); text_.clear(); invalidate(Invalidation::paint);
}
void Progress::set_value(double value) {
    bounded(value, range_);
    if (value_ == value) return;
    value_ = value; text_.clear(); invalidate(Invalidation::paint);
}
void Progress::set_state(ProgressState value) {
    if (value < ProgressState::determinate || value > ProgressState::unknown) throw std::invalid_argument("Invalid progress state");
    if (state_ == value) return;
    state_ = value; invalidate(Invalidation::paint);
}
void Progress::set_capacity(double used, double total, std::wstring unit) {
    if (!std::isfinite(used) || !std::isfinite(total) || total <= 0 || used < 0 || used > total)
        throw std::invalid_argument("Capacity requires 0 <= used <= total and finite positive total");
    std::wostringstream stream; stream << used << L" / " << total << L" " << unit;
    const auto text = stream.str();
    if (range_.minimum == 0 && range_.maximum == total && value_ == used && text_ == text && state_ == ProgressState::determinate) return;
    range_ = {0, total}; value_ = used; text_ = text; state_ = ProgressState::determinate; invalidate(Invalidation::paint);
}
SplitButton::SplitButton(std::wstring primary_name, std::wstring secondary_name)
    : Stack(Axis::horizontal), primary_(std::make_shared<Button>(std::move(primary_name))),
      secondary_(std::make_shared<Button>(std::move(secondary_name))) {
    secondary_->set_behavior(ButtonBehavior::dropdown);
    secondary_->set_fixed_size({42, 40});
    add(primary_, 1); add(secondary_);
}
}
