#include "xui/documents.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <stdexcept>

namespace xui {
namespace {
void validate_text(std::wstring_view value, std::size_t limit) {
    if (value.size() > limit) throw std::length_error("Document exceeds its UTF-16 limit");
    for (std::size_t i = 0; i < value.size(); ++i) {
        const auto c = static_cast<unsigned>(value[i]);
        if (!c) throw std::invalid_argument("Embedded null is not document text");
        if (c >= 0xd800 && c <= 0xdbff) {
            if (++i == value.size() || value[i] < 0xdc00 || value[i] > 0xdfff)
                throw std::invalid_argument("Unpaired UTF-16 surrogate");
        } else if (c >= 0xdc00 && c <= 0xdfff) throw std::invalid_argument("Unpaired UTF-16 surrogate");
    }
}
void erase_secret(std::wstring& value) {
    volatile wchar_t* data = value.data();
    for (std::size_t i = 0; i < value.size(); ++i) data[i] = 0;
    value.clear();
}
struct Notification {
    bool& active;
    explicit Notification(bool& value) : active(value) { active = true; }
    ~Notification() { active = false; }
};
void normalize_paragraphs(std::wstring& value) {
    std::size_t out{};
    for (std::size_t i = 0; i < value.size(); ++i) {
        auto c = value[i];
        if (c == L'\r' && i + 1 < value.size() && value[i + 1] == L'\n') ++i;
        value[out++] = c == L'\n' ? L'\r' : c;
    }
    value.resize(out);
}
std::vector<SyntaxSpan> highlight(const std::function<std::vector<SyntaxSpan>(std::wstring_view)>& callback,
    std::wstring_view text, bool& active) {
    if (!callback) return {};
    Notification notification(active);
    auto spans = callback(text);
    if (spans.size() > DocumentText::document_limit) throw std::length_error("Too many document syntax spans");
    std::size_t end{};
    const auto boundary = [&](std::size_t i) {
        return i == text.size() || text[i] < 0xdc00 || text[i] > 0xdfff;
    };
    for (const auto& span : spans) {
        if (span.start < end || span.start >= span.end || span.end > text.size())
            throw std::invalid_argument("Syntax spans must be ordered, nonempty, nonoverlapping and within the document");
        if (!boundary(span.start) || !boundary(span.end))
            throw std::invalid_argument("Syntax span splits a UTF-16 surrogate");
        if (span.kind < SyntaxKind::other || span.kind > SyntaxKind::variable)
            throw std::invalid_argument("Invalid document syntax kind");
        end = span.end;
    }
    return spans;
}
}
DocumentText::DocumentText(std::wstring name, bool rich)
    : Control(ControlRole::document_text, std::move(name), {400, 180}), rich_(rich) {}
void DocumentText::set_text(std::wstring value) {
    if (highlighting_) throw std::logic_error("Cannot change document text from its syntax highlighter");
    validate_text(value, maximum_);
    normalize_paragraphs(value);
    if (text_ == value && runs_.empty()) return;
    auto spans = highlight(highlighter_, value, highlighting_);
    text_ = std::move(value); runs_.clear(); ++revision_;
    syntax_spans_ = std::move(spans);
    if (highlighter_) ++syntax_revision_;
    selection_ = {}; ++selection_revision_;
    invalidate_state();
}
void DocumentText::set_syntax_highlighter(std::function<std::vector<SyntaxSpan>(std::wstring_view)> callback) {
    if (rich_) throw std::logic_error("Syntax highlighting supports plain documents only");
    if (highlighting_) throw std::logic_error("Cannot replace a running document syntax highlighter");
    if (!callback && !highlighter_) return;
    auto spans = highlight(callback, text_, highlighting_);
    highlighter_ = std::move(callback);
    syntax_spans_ = std::move(spans);
    ++syntax_revision_;
    invalidate(Invalidation::paint);
}
void DocumentText::set_maximum_length(std::size_t value) {
    if (highlighting_) throw std::logic_error("Cannot change document limits from its syntax highlighter");
    if (!value || value > document_limit || value < text_.size()) throw std::invalid_argument("Invalid document limit");
    if (maximum_ == value) return;
    maximum_ = value; invalidate(Invalidation::paint);
}
void DocumentText::set_read_only(bool value) {
    if (highlighting_) throw std::logic_error("Cannot change document editability from its syntax highlighter");
    if (read_only_ == value) return;
    read_only_ = value; invalidate_state();
}
void DocumentText::set_monospace(bool value) {
    if (monospace_ == value) return;
    monospace_ = value; invalidate(Invalidation::paint);
}
void DocumentText::assign_runs(std::vector<TextRun> value) {
    if (value.size() > 4096) throw std::length_error("At most 4096 document runs are supported");
    std::wstring text;
    for (auto& run : value) {
        validate_text(run.text, maximum_ - text.size());
        normalize_paragraphs(run.text);
        validate_text(run.link, 2048);
        if (!run.link.empty() && !(run.link.starts_with(L"https://") || run.link.starts_with(L"http://")))
            throw std::invalid_argument("Document links require an HTTP or HTTPS target");
        if (!run.link.empty()) {
            const auto authority = run.link.starts_with(L"https://") ? 8u : 7u;
            if (run.link.size() == authority || run.link.find_first_of(L"/?#", authority) == authority ||
                std::any_of(run.link.begin(), run.link.end(), [](wchar_t c) { return c <= 32 || c == 127 || c == L'\\'; }))
                throw std::invalid_argument("Document links require a nonempty authority and no control characters");
        }
        text += run.text;
    }
    if (runs_ == value && text_ == text) return;
    text_ = std::move(text); runs_ = std::move(value); ++revision_;
    selection_ = {}; ++selection_revision_; invalidate_state();
}
void DocumentText::set_selection(TextSelection value) {
    if (value.start > value.end || value.end > text_.size()) throw std::out_of_range("Invalid document selection");
    const auto boundary = [&](std::size_t i) { return !i || i == text_.size() || text_[i] < 0xdc00 || text_[i] > 0xdfff; };
    if (!boundary(value.start) || !boundary(value.end)) throw std::invalid_argument("Selection splits a UTF-16 surrogate");
    if (selection_ == value) return;
    selection_ = value; ++selection_revision_; invalidate(Invalidation::paint);
}
void DocumentText::commit_selection(TextSelection value) {
    if (value.start <= value.end && value.end <= text_.size()) selection_ = value;
}
bool DocumentText::command(TextCommand value) {
    if (value < TextCommand::undo || value > TextCommand::select_all) throw std::invalid_argument("Invalid document command");
    if (!enabled() || notifying_ || highlighting_ || !command_) return false;
    auto callback = command_; return callback(value);
}
TextSelection DocumentText::replace_range(TextSelection range, std::wstring_view expected_text, std::wstring replacement) {
    if (rich_) throw std::logic_error("Range replacement supports plain documents only");
    if (!visible() || !enabled() || read_only_ || notifying_ || highlighting_ || !replace_)
        throw std::logic_error("Document is unavailable for range replacement");
    validate_text(expected_text, document_limit);
    validate_text(replacement, document_limit);
    if (expected_text != text_) throw std::logic_error("Document range replacement is stale");
    if (range.start > range.end || range.end > expected_text.size())
        throw std::invalid_argument("Invalid document replacement range");
    const auto boundary = [&](std::size_t i) {
        return i == expected_text.size() || expected_text[i] < 0xdc00 || expected_text[i] > 0xdfff;
    };
    if (!boundary(range.start) || !boundary(range.end))
        throw std::invalid_argument("Replacement range splits a UTF-16 surrogate");
    normalize_paragraphs(replacement);
    if (replacement.size() > maximum_ - (expected_text.size() - (range.end - range.start)))
        throw std::length_error("Document replacement exceeds its UTF-16 limit");
    if (expected_text.substr(range.start, range.end - range.start) == replacement)
        throw std::invalid_argument("Document replacement must change text");
    const std::wstring snapshot(expected_text);
    auto callback = replace_;
    return callback(range, snapshot, replacement);
}
void DocumentText::commit_text(std::wstring value, std::optional<TextSelection> selection) {
    if (highlighting_) throw std::logic_error("Cannot commit document text from its syntax highlighter");
    validate_text(value, maximum_);
    normalize_paragraphs(value);
    if (read_only_ || notifying_ || text_ == value) return;
    std::vector<SyntaxSpan> spans;
    std::exception_ptr syntax_error;
    try { spans = highlight(highlighter_, value, highlighting_); }
    catch (...) { syntax_error = std::current_exception(); }
    if (!runs_.empty()) {
        std::size_t prefix{}, suffix{};
        while (prefix < text_.size() && prefix < value.size() && text_[prefix] == value[prefix]) ++prefix;
        while (suffix < text_.size() - prefix && suffix < value.size() - prefix &&
            text_[text_.size() - suffix - 1] == value[value.size() - suffix - 1]) ++suffix;
        if (prefix && prefix < text_.size() && text_[prefix] >= 0xdc00 && text_[prefix] <= 0xdfff) --prefix;
        if (suffix && text_[text_.size() - suffix] >= 0xdc00 && text_[text_.size() - suffix] <= 0xdfff) --suffix;
        std::vector<TextRun> next;
        const auto append = [&](TextRun run) {
            if (run.text.empty()) return;
            if (!next.empty() && next.back().bold == run.bold && next.back().italic == run.italic &&
                next.back().underline == run.underline && next.back().link == run.link) next.back().text += run.text;
            else next.push_back(std::move(run));
        };
        const auto copy_range = [&](std::size_t start, std::size_t end) {
            std::size_t offset{};
            for (const auto& run : runs_) {
                const auto left = std::max(start, offset), right = std::min(end, offset + run.text.size());
                if (left < right) { auto part = run; part.text = run.text.substr(left - offset, right - left); append(std::move(part)); }
                offset += run.text.size();
            }
        };
        copy_range(0, prefix);
        TextRun inserted{value.substr(prefix, value.size() - prefix - suffix)};
        std::size_t offset{};
        for (const auto& run : runs_) {
            if (prefix >= offset && prefix <= offset + run.text.size()) {
                inserted.bold = run.bold; inserted.italic = run.italic; inserted.underline = run.underline; break;
            }
            offset += run.text.size();
        }
        append(std::move(inserted)); copy_range(text_.size() - suffix, text_.size());
        if (next.size() <= 4096) runs_ = std::move(next); else runs_.clear();
    }
    text_ = std::move(value);
    // A native edit already happened. Publish it even when its tokenizer fails;
    // stale spans must never describe the new text or suppress its change event.
    syntax_spans_ = std::move(spans);
    if (highlighter_) ++syntax_revision_;
    if (selection) commit_selection(*selection);
    invalidate_state();
    auto callback = change_;
    if (callback) { Notification notification(notifying_); callback(text_); }
    if (syntax_error) std::rethrow_exception(syntax_error);
}
void DocumentText::activate_link(std::size_t position) {
    std::size_t offset{};
    for (const auto& run : runs_) {
        if (position >= offset && position < offset + run.text.size() && !run.link.empty()) {
            auto callback = link_; const auto target = run.link; if (callback) callback(target); return;
        }
        offset += run.text.size();
    }
}
PasswordInput::PasswordInput(std::wstring name) : Control(ControlRole::password_input, std::move(name), {320, 38}) {}
PasswordInput::~PasswordInput() { erase_secret(value_); }
void PasswordInput::set_password(std::wstring value) {
    try { validate_text(value, maximum_); } catch (...) { erase_secret(value); throw; }
    if (value_ == value) { erase_secret(value); return; }
    erase_secret(value_); value_.swap(value); erase_secret(value); ++revision_; invalidate_state();
}
void PasswordInput::with_password(const std::function<void(std::wstring_view)>& receiver) const {
    if (!receiver) throw std::invalid_argument("Password receiver is required");
    receiver(value_);
}
void PasswordInput::set_maximum_length(std::size_t value) {
    if (!value || value > 4096 || value < value_.size()) throw std::invalid_argument("Invalid password limit");
    if (maximum_ == value) return;
    maximum_ = value; invalidate(Invalidation::paint);
}
void PasswordInput::set_reveal_policy(PasswordRevealPolicy value) {
    if (value != PasswordRevealPolicy::never && value != PasswordRevealPolicy::explicit_request)
        throw std::invalid_argument("Invalid password reveal policy");
    if (policy_ == value) return;
    policy_ = value; revealed_ = false; invalidate(Invalidation::layout);
}
void PasswordInput::set_revealed(bool value) {
    if (value && policy_ != PasswordRevealPolicy::explicit_request) throw std::logic_error("Password reveal is not permitted");
    if (revealed_ == value) return;
    revealed_ = value; invalidate(Invalidation::layout);
}
Size PasswordInput::measure(Size available) {
    const auto size = Control::measure(available);
    return visible() && revealed_ ? constrain({size.width, size.height + reveal_extent()}, available) : size;
}
float PasswordInput::reveal_extent() const {
    if (!revealed_) return 0;
    const auto* text = effective_control_style_values(StylePart::text);
    return text && text->font_size ? std::max(32.0f, *text->font_size * 1.5f + 4) : 32.0f;
}
void PasswordInput::commit_password(std::wstring value) {
    if (value_ == value || notifying_) { erase_secret(value); return; }
    set_password(std::move(value));
    auto callback = change_; if (callback) { Notification notification(notifying_); callback(); }
}
void DateTimeValue::validate() const {
    const std::chrono::year_month_day date{std::chrono::year{year}, std::chrono::month{static_cast<unsigned>(month)},
        std::chrono::day{static_cast<unsigned>(day)}};
    if (year < 1601 || year > 9999 || !date.ok() || hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59)
        throw std::invalid_argument("Invalid Gregorian date or local time");
}
DateTimePicker::DateTimePicker(std::wstring name, DateTimePresentation presentation)
    : Control(ControlRole::date_time, std::move(name), presentation == DateTimePresentation::calendar ? Size{280, 210} : Size{280, 38}),
      presentation_(presentation) {
    if (presentation != DateTimePresentation::date && presentation != DateTimePresentation::time && presentation != DateTimePresentation::calendar)
        throw std::invalid_argument("Invalid date/time presentation");
}
void DateTimePicker::set_value(DateTimeValue value) {
    value.validate();
    if (value < minimum_ || value > maximum_) throw std::out_of_range("Date/time is outside its range");
    if (value_ == value) return;
    value_ = value; ++revision_; invalidate(Invalidation::paint);
}
void DateTimePicker::set_range(DateTimeValue minimum, DateTimeValue maximum) {
    minimum.validate(); maximum.validate();
    if (minimum > maximum || value_ < minimum || value_ > maximum) throw std::invalid_argument("Date/time range excludes the current value");
    if (minimum_ == minimum && maximum_ == maximum) return;
    minimum_ = minimum; maximum_ = maximum; ++revision_; invalidate(Invalidation::paint);
}
bool DateTimePicker::change_value(DateTimeValue value) {
    if (!enabled() || value_ == value) return false;
    set_value(value); auto callback = change_; if (callback) callback(value); return true;
}
static Rect status_style_content(const Element& owner, Rect bounds) {
    if (!owner.has_control_styling()) return bounds;
    const auto* root = owner.effective_control_style_values(StylePart::root);
    if (!root) return bounds;
    const auto padding = root->padding.value_or(Insets{});
    const auto border = root->border_thickness.value_or(Insets{});
    const float left = std::min(bounds.width, padding.left + border.left);
    const float top = std::min(bounds.height, padding.top + border.top);
    return {bounds.x + left, bounds.y + top,
        std::max(0.0f, bounds.width - left - padding.right - border.right),
        std::max(0.0f, bounds.height - top - padding.bottom - border.bottom)};
}
InlineStatus::InlineStatus(std::wstring message)
    : Control(ControlRole::inline_status, std::move(message), {400, 64}),
      action_(std::make_shared<Button>(L"Action")), dismiss_(std::make_shared<Button>(L"Dismiss message")), children_{action_, dismiss_} {
    validate_text(name(), 4096);
    for (const auto& child : children_) adopt(child);
    action_->set_visible(false); dismiss_->set_visible(false);
    dismiss_->set_appearance(ButtonAppearance::subtle);
    dismiss_->set_icon(ButtonIcon::close); dismiss_->on_click([this] { dismiss(); });
}
InlineStatus::~InlineStatus() { dismiss_->on_click({}); }
StyleStateMask InlineStatus::control_style_state_bits() const {
    constexpr StyleStateMask states[]{style_states::information, style_states::success,
        style_states::warning, style_states::error};
    return (Control::control_style_state_bits() & style_states::disabled) | states[static_cast<unsigned>(severity_)] |
        (dismissed_ ? style_states::dismissed : 0);
}
void InlineStatus::set_message(std::wstring message, StatusSeverity severity) {
    validate_text(message, 4096);
    if (severity < StatusSeverity::information || severity > StatusSeverity::error) throw std::invalid_argument("Invalid status severity");
    if (severity_ == severity && name() == message) return;
    severity_ = severity; set_name(std::move(message)); invalidate_state();
}
void InlineStatus::set_dismissible(bool value) {
    if (dismissible_ == value) return;
    dismissible_ = value; dismiss_->set_visible(value); invalidate(Invalidation::layout);
}
void InlineStatus::show() { dismissed_ = false; set_visible(true); invalidate_state(); }
void InlineStatus::dismiss() {
    if (!dismissible_ || dismissed_) return;
    dismissed_ = true; set_visible(false); invalidate_state(); auto callback = dismiss_callback_; if (callback) callback();
}
void InlineStatus::set_action(std::wstring label, std::function<void()> callback) {
    validate_text(label, 128);
    action_->set_name(std::move(label)); action_->set_visible(bool(callback)); action_->on_click(std::move(callback));
}
void InlineStatus::arrange(Rect bounds) {
    Element::arrange(bounds);
    const auto content = content_bounds();
    bounds = {bounds.x + content.x, bounds.y + content.y, content.width, content.height};
    const auto close_width = dismissible_ ? 36.0f : 0.0f;
    dismiss_->arrange({bounds.x + std::max(0.0f, bounds.width - close_width), bounds.y + 8, close_width, 36});
    action_->arrange({bounds.x + std::max(0.0f, bounds.width - close_width - 116), bounds.y + 8, 112, 36});
}
Rect InlineStatus::content_bounds() const {
    return status_style_content(*this, {0, 0, bounds().width, bounds().height});
}
ColorPicker::ColorPicker(std::wstring name) : Control(ControlRole::color_picker, std::move(name), {400, 280}) {
    const wchar_t* names[]{L"Red", L"Green", L"Blue", L"Alpha"};
    for (std::size_t i = 0; i < 4; ++i) {
        channels_[i] = std::make_shared<NumericInput>(names[i]); channels_[i]->set_range({0, 255, 1, 16});
        children_.push_back(channels_[i]); adopt(channels_[i]);
        channels_[i]->on_change([this, i](double value) {
            auto next = value_; const auto byte = static_cast<std::uint8_t>(std::lround(value));
            if (i == 0) next.red = byte; else if (i == 1) next.green = byte; else if (i == 2) next.blue = byte; else next.alpha = byte;
            if (!change_value(next)) sync();
        });
    }
    sync(); set_swatches({{0, 0, 0}, {255, 255, 255}, {220, 45, 45}, {40, 170, 80}, {45, 100, 230}});
}
ColorPicker::~ColorPicker() {
    for (auto& channel : channels_) channel->on_change({});
    for (std::size_t i = 4; i < children_.size(); ++i) std::static_pointer_cast<Button>(children_[i])->on_click({});
}
StyleStateMask ColorPicker::control_style_state_bits() const {
    const bool invalid = std::any_of(channels_.begin(), channels_.end(), [](const auto& channel) { return !channel->valid(); });
    return (Control::control_style_state_bits() & style_states::disabled) | (invalid ? style_states::invalid : 0);
}
PartStyleValues ColorPicker::channel_label_style_values(std::size_t index) const {
    if (index >= channels_.size()) throw std::out_of_range("Color channel index is outside the channel range");
    const auto& channel = channels_[index];
    const auto state = (channel->valid() ? 0 : style_states::invalid) |
        (channel->enabled() ? 0 : style_states::disabled);
    return resolve_control_style_part(StylePart::channel_label, state);
}
void ColorPicker::sync() {
    channels_[0]->set_value(value_.red); channels_[1]->set_value(value_.green);
    channels_[2]->set_value(value_.blue); channels_[3]->set_value(value_.alpha);
    set_help_text(L"RGBA " + std::to_wstring(value_.red) + L", " + std::to_wstring(value_.green) + L", " +
        std::to_wstring(value_.blue) + L", " + std::to_wstring(value_.alpha));
}
void ColorPicker::set_value(RgbaColor value) {
    if (value_ == value) return;
    value_ = value; sync(); invalidate(Invalidation::paint);
}
bool ColorPicker::change_value(RgbaColor value) {
    if (!enabled() || value_ == value) return false;
    set_value(value); auto callback = change_; if (callback) callback(value); return true;
}
void ColorPicker::set_swatches(std::vector<RgbaColor> values) {
    if (values.size() > 16) throw std::length_error("At most 16 color swatches are supported");
    if (swatches_ == values) return;
    for (std::size_t i = 4; i < children_.size(); ++i) {
        auto old = std::static_pointer_cast<Button>(children_[i]); old->on_click({}); old->set_visible(false);
    }
    swatches_ = std::move(values);
    for (std::size_t i = 0; i < swatches_.size(); ++i) {
        const auto value = swatches_[i];
        if (children_.size() <= i + 4) {
            auto button = std::make_shared<Button>(L"Color"); children_.push_back(button); adopt(button);
        }
        auto button = std::static_pointer_cast<Button>(children_[i + 4]);
        button->set_name(L"RGBA " + std::to_wstring(value.red) + L", " + std::to_wstring(value.green) +
            L", " + std::to_wstring(value.blue) + L", " + std::to_wstring(value.alpha));
        button->set_icon(ButtonIcon::none);
        button->set_visible(true); button->on_click([this, value] { change_value(value); });
    }
    invalidate(Invalidation::layout);
}
void ColorPicker::arrange(Rect bounds) {
    Element::arrange(bounds);
    const auto content = content_bounds();
    bounds = {bounds.x + content.x, bounds.y + content.y, content.width, content.height};
    for (std::size_t i = 0; i < 4; ++i)
        channels_[i]->arrange({bounds.x + 64, bounds.y + 52 + i * 42, std::max(0.0f, bounds.width - 64), 38});
    const auto width = swatches_.empty() ? 0.0f : bounds.width / static_cast<float>(swatches_.size());
    for (std::size_t i = 4; i < children_.size(); ++i)
        children_[i]->arrange({bounds.x + (i - 4) * width, bounds.y + 226, std::max(0.0f, width - 4), 36});
}
Rect ColorPicker::content_bounds() const {
    return status_style_content(*this, {0, 0, bounds().width, bounds().height});
}
class ContentDialog::Layout final : public Stack {
    class Body final : public Stack {
    public:
        Body(std::shared_ptr<Label> title, std::shared_ptr<Element> content,
            std::shared_ptr<InlineStatus> validation)
            : Stack(Axis::vertical), title_(std::move(title)), content_(std::move(content)),
              validation_(std::move(validation)) {
            set_default_spacing(10);
            add(title_); add(content_); add(validation_);
        }
        void set_style(VisualStyle style) {
            style_ = style;
            set_default_spacing(style == VisualStyle::winui ? 12.0f : 10.0f);
            set_default_padding(style == VisualStyle::winui ? Insets{24, 24, 24, 25} : Insets{});
            title_->set_subtitle(style == VisualStyle::winui);
            title_->set_wrapping(style == VisualStyle::winui, style == VisualStyle::winui ? 2 : 0);
            title_->set_visible(style == VisualStyle::classic || !title_->text().empty());
            invalidate(Invalidation::layout);
        }
        Size measure(Size available) override {
            if (style_ == VisualStyle::classic) return Stack::measure(available);
            const auto inset = content_insets();
            const auto sizes = measure_children(std::max(0.0f, available.width - inset.left - inset.right));
            const float width = std::max({sizes[0].width, sizes[1].width, sizes[2].width}) + inset.left + inset.right;
            const float height = inset.top + inset.bottom + sizes[0].height + title_gap(sizes) +
                sizes[1].height + validation_gap(sizes) + sizes[2].height;
            return constrain({width, height}, available);
        }
        void arrange(Rect rectangle) override {
            if (style_ == VisualStyle::classic) { Stack::arrange(rectangle); return; }
            Element::arrange(rectangle);
            rectangle = bounds();
            const auto inset = content_insets();
            const float width = std::max(0.0f, rectangle.width - inset.left - inset.right);
            const auto sizes = measure_children(width);
            const float x = rectangle.x + std::min(inset.left, rectangle.width);
            float y = rectangle.y + std::min(inset.top, rectangle.height);
            const auto* style = effective_control_style_values(StylePart::root);
            const auto vertical = style ? style->vertical_alignment.value_or(StyleAlignment::start) : StyleAlignment::start;
            const float total = sizes[0].height + title_gap(sizes) + sizes[1].height + validation_gap(sizes) + sizes[2].height;
            if (vertical == StyleAlignment::center || vertical == StyleAlignment::end)
                y += std::max(0.0f, rectangle.height - inset.top - inset.bottom - total) /
                    (vertical == StyleAlignment::center ? 2 : 1);
            const auto place = [&](Element& child, Size size, float top) {
                const auto horizontal = style ? style->horizontal_alignment.value_or(StyleAlignment::stretch) : StyleAlignment::stretch;
                const float actual = horizontal == StyleAlignment::stretch ? width : std::min(width, size.width);
                const float offset = horizontal == StyleAlignment::center ? (width - actual) / 2 :
                    horizontal == StyleAlignment::end ? width - actual : 0;
                child.arrange({x + offset, top, actual, size.height});
            };
            place(*title_, sizes[0], y);
            y += sizes[0].height + title_gap(sizes);
            place(*content_, sizes[1], y);
            y += sizes[1].height + validation_gap(sizes);
            place(*validation_, sizes[2], y);
        }
    private:
        Insets content_insets() const {
            return effective_layout_insets();
        }
        float gap() const {
            return effective_spacing();
        }
        std::array<Size, 3> measure_children(float width) const {
            const Size available{width, (std::numeric_limits<float>::max)()};
            return {title_->measure(available), content_->measure(available), validation_->measure(available)};
        }
        float title_gap(const std::array<Size, 3>& sizes) const {
            return sizes[0].height > 0 && sizes[1].height > 0 ? gap() : 0.0f;
        }
        float validation_gap(const std::array<Size, 3>& sizes) const {
            return sizes[2].height > 0 && (sizes[0].height > 0 || sizes[1].height > 0) ? gap() : 0.0f;
        }
        VisualStyle style_{VisualStyle::classic};
        std::shared_ptr<Label> title_;
        std::shared_ptr<Element> content_;
        std::shared_ptr<InlineStatus> validation_;
    };
public:
    Layout(std::shared_ptr<Label> title, std::shared_ptr<Element> content,
        std::shared_ptr<InlineStatus> validation, std::shared_ptr<Stack> actions)
        : Stack(Axis::vertical), body_(std::make_shared<Body>(std::move(title), std::move(content), std::move(validation))),
          scroll_(std::make_shared<ScrollView>(body_, L"Dialog content")), actions_(std::move(actions)) {
        set_default_padding({16, 16, 16, 16}); set_default_spacing(10);
        scroll_->set_tab_stop(false);
        scroll_->set_overlay_scrollbar(true);
        scroll_->set_passthrough(true);
        add(scroll_); add(actions_);
    }
    void set_style(VisualStyle style) {
        if (style_ == style) return;
        style_ = style;
        body_->set_style(style);
        actions_->set_separator_inset_enabled(style != VisualStyle::winui);
        scroll_->set_passthrough(style == VisualStyle::classic);
        footer_ = {};
        invalidate(Invalidation::layout);
    }
    Rect footer_bounds() const { return footer_; }
    std::shared_ptr<Stack> body() const { return body_; }
    Size measure(Size available) override {
        if (style_ == VisualStyle::classic) return Stack::measure(available);
        const auto width_limit = std::min(548.0f, dimension(available.width));
        const Size inner{std::max(0.0f, width_limit - 48), unlimited};
        const auto body = body_->measure({width_limit, unlimited});
        float button_width = 0;
        for (std::size_t i = 0; i < actions_->child_count(); ++i)
            button_width = std::max(button_width, actions_->child_at(i)->measure(inner).width);
        const float width = std::min(width_limit, std::max({320.0f, body.width, 2 * button_width + 56}));
        const float height = body_->measure({width, unlimited}).height +
            action_height(std::max(0.0f, width - 48)) + 48 + separator_extent();
        return {width, std::min(dimension(available.height), std::clamp(height, 184.0f, 756.0f))};
    }
    void arrange(Rect rectangle) override {
        if (style_ == VisualStyle::classic) { footer_ = {}; Stack::arrange(rectangle); return; }
        Element::arrange(rectangle);
        rectangle = bounds();
        const float inner_width = std::max(0.0f, rectangle.width - 48);
        const auto buttons_height = action_height(inner_width);
        const auto separator = separator_extent();
        const auto footer_height_here = std::min(buttons_height + 48 + separator, rectangle.height);
        const float body_height = rectangle.height - footer_height_here;
        footer_ = {rectangle.x, rectangle.y + body_height, rectangle.width, footer_height_here};
        const float left = std::min(24.0f, rectangle.width);
        scroll_->arrange({rectangle.x, rectangle.y, rectangle.width, body_height});
        const float action_top = std::min(24.0f + separator, footer_height_here);
        actions_->arrange({rectangle.x + left, footer_.y + action_top, inner_width,
            std::min(buttons_height, std::max(0.0f, footer_height_here - action_top - 24))});
    }
private:
    static constexpr float unlimited = (std::numeric_limits<float>::max)();
    float separator_extent() const {
        const auto* values = actions_->effective_control_style_values(StylePart::separator);
        return values && values->thickness ? std::max(0.0f, *values->thickness - 1) : 0;
    }
    float action_height(float width) const {
        if (actions_->has_control_styling()) return actions_->measure({width, unlimited}).height;
        float height = 0;
        const Size column{std::max(0.0f, (width - 8) / 2), unlimited};
        for (std::size_t i = 0; i < actions_->child_count(); ++i)
            height = std::max(height, actions_->child_at(i)->measure(column).height);
        return height;
    }
    static float dimension(float value) { return std::isnan(value) ? 0.0f : std::clamp(value, 0.0f, unlimited); }
    VisualStyle style_{VisualStyle::classic};
    std::shared_ptr<Body> body_;
    std::shared_ptr<ScrollView> scroll_;
    std::shared_ptr<Stack> actions_;
    Rect footer_;
};
ContentDialog::ContentDialog(std::wstring title, std::shared_ptr<Element> content) {
    if (!content) throw std::invalid_argument("Dialog content is required");
    auto heading = title_ = std::make_shared<Label>(title); heading->set_heading(true);
    validation_ = std::make_shared<InlineStatus>(); validation_->set_visible(false);
    auto row = footer_ = std::make_shared<Stack>(Axis::horizontal); row->set_default_spacing(8);
    primary_ = std::make_shared<Button>(L"OK"); cancel_ = std::make_shared<Button>(L"Cancel");
    primary_->set_appearance(ButtonAppearance::accent);
    row->add(primary_, 1); row->add(cancel_, 1);
    layout_ = std::make_shared<Layout>(heading, std::move(content), validation_, row);
    popup_ = std::make_shared<Popup>(layout_, std::move(title)); popup_->set_default_size({460, 360});
    popup_->set_dialog_surface(true);
    primary_->on_click([this] { accept(); }); cancel_->on_click([this] { cancel(); });
}
ContentDialog::~ContentDialog() { primary_->on_click({}); cancel_->on_click({}); }
void ContentDialog::set_visual_style(VisualStyle style) {
    if (style != VisualStyle::classic && style != VisualStyle::winui) throw std::invalid_argument("Invalid visual style");
    layout_->set_style(style);
    const auto apply = [style](const auto& self, const std::shared_ptr<Element>& element) -> void {
        if (auto control = std::dynamic_pointer_cast<Control>(element)) {
            control->set_visual_style(style);
            if (auto scroll = std::dynamic_pointer_cast<ScrollView>(control)) self(self, scroll->content());
            for (const auto& child : control->retained_children()) self(self, child);
        } else if (auto stack = std::dynamic_pointer_cast<Stack>(element)) {
            for (std::size_t i = 0; i < stack->child_count(); ++i) self(self, stack->child_at(i));
        }
    };
    apply(apply, popup_);
}
Size ContentDialog::measure(Size available) {
    if (popup_->visual_style() == VisualStyle::classic) return popup_->measure(available);
    const auto inset = [](float value) {
        return std::isnan(value) ? 0.0f : std::clamp(value - 48, 0.0f, (std::numeric_limits<float>::max)());
    };
    const Size viewport{inset(available.width), inset(available.height)};
    auto desired = popup_->preferred_size_explicit() ? popup_->measure(viewport) : layout_->measure(viewport);
    desired.width = std::clamp(desired.width, 320.0f, 548.0f);
    desired.height = std::clamp(desired.height, 184.0f, 756.0f);
    desired = popup_->constrain(desired, viewport);
    desired.width = std::min(548.0f, desired.width);
    desired.height = std::min(756.0f, desired.height);
    return desired;
}
Rect ContentDialog::footer_bounds() const { return layout_->footer_bounds(); }
std::shared_ptr<Stack> ContentDialog::body() const { return layout_->body(); }
void ContentDialog::accept() {
    auto lifetime = weak_from_this().lock();
    if (!popup_->is_open() || !primary_->enabled()) return;
    const auto generation = popup_->generation(); auto validate = validate_;
    const auto message = validate ? validate() : std::wstring{};
    if (!popup_->current(generation)) return;
    if (!message.empty()) { validation_->set_message(message, StatusSeverity::error); validation_->show(); return; }
    validation_->set_visible(false); auto callback = close_; if (callback) callback(DialogResult::primary);
}
void ContentDialog::cancel() { auto lifetime = weak_from_this().lock(); auto callback = close_; if (popup_->is_open() && callback) callback(DialogResult::cancel); }
void ContentDialog::notify_result(DialogResult value) { auto callback = result_; if (callback) callback(value); }
}
