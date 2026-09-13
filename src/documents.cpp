#include "xui/documents.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
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
}
DocumentText::DocumentText(std::wstring name, bool rich)
    : Control(ControlRole::document_text, std::move(name), {400, 180}), rich_(rich) {}
void DocumentText::set_text(std::wstring value) {
    validate_text(value, maximum_);
    normalize_paragraphs(value);
    if (text_ == value && runs_.empty()) return;
    text_ = std::move(value); runs_.clear(); ++revision_;
    selection_ = {}; ++selection_revision_;
    invalidate(Invalidation::paint);
}
void DocumentText::set_maximum_length(std::size_t value) {
    if (!value || value > document_limit || value < text_.size()) throw std::invalid_argument("Invalid document limit");
    if (maximum_ == value) return;
    maximum_ = value; invalidate(Invalidation::paint);
}
void DocumentText::set_read_only(bool value) {
    if (read_only_ == value) return;
    read_only_ = value; invalidate(Invalidation::paint);
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
    selection_ = {}; ++selection_revision_; invalidate(Invalidation::paint);
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
    if (!enabled() || !command_) return false;
    auto callback = command_; return callback(value);
}
void DocumentText::commit_text(std::wstring value) {
    validate_text(value, maximum_);
    normalize_paragraphs(value);
    if (read_only_ || notifying_ || text_ == value) return;
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
    invalidate(Invalidation::paint);
    auto callback = change_;
    if (callback) { Notification notification(notifying_); callback(text_); }
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
    erase_secret(value_); value_.swap(value); erase_secret(value); ++revision_; invalidate(Invalidation::paint);
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
    return visible() && revealed_ ? constrain({size.width, size.height + 32}, available) : size;
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
InlineStatus::InlineStatus(std::wstring message)
    : Control(ControlRole::inline_status, std::move(message), {400, 64}),
      action_(std::make_shared<Button>(L"Action")), dismiss_(std::make_shared<Button>(L"Dismiss message")), children_{action_, dismiss_} {
    validate_text(name(), 4096);
    for (const auto& child : children_) adopt(child);
    action_->set_visible(false); dismiss_->set_visible(false);
    dismiss_->set_icon(ButtonIcon::close); dismiss_->on_click([this] { dismiss(); });
}
InlineStatus::~InlineStatus() { dismiss_->on_click({}); }
void InlineStatus::set_message(std::wstring message, StatusSeverity severity) {
    validate_text(message, 4096);
    if (severity < StatusSeverity::information || severity > StatusSeverity::error) throw std::invalid_argument("Invalid status severity");
    if (severity_ == severity && name() == message) return;
    severity_ = severity; set_name(std::move(message)); invalidate(Invalidation::paint);
}
void InlineStatus::set_dismissible(bool value) {
    if (dismissible_ == value) return;
    dismissible_ = value; dismiss_->set_visible(value); invalidate(Invalidation::layout);
}
void InlineStatus::show() { dismissed_ = false; set_visible(true); }
void InlineStatus::dismiss() {
    if (!dismissible_ || dismissed_) return;
    dismissed_ = true; set_visible(false); auto callback = dismiss_callback_; if (callback) callback();
}
void InlineStatus::set_action(std::wstring label, std::function<void()> callback) {
    validate_text(label, 128);
    action_->set_name(std::move(label)); action_->set_visible(bool(callback)); action_->on_click(std::move(callback));
}
void InlineStatus::arrange(Rect bounds) {
    Element::arrange(bounds);
    const auto close_width = dismissible_ ? 36.0f : 0.0f;
    dismiss_->arrange({bounds.x + std::max(0.0f, bounds.width - close_width), bounds.y + 8, close_width, 36});
    action_->arrange({bounds.x + std::max(0.0f, bounds.width - close_width - 116), bounds.y + 8, 112, 36});
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
    for (std::size_t i = 0; i < 4; ++i)
        channels_[i]->arrange({bounds.x + 64, bounds.y + 52 + i * 42, std::max(0.0f, bounds.width - 64), 38});
    const auto width = swatches_.empty() ? 0.0f : bounds.width / static_cast<float>(swatches_.size());
    for (std::size_t i = 4; i < children_.size(); ++i)
        children_[i]->arrange({bounds.x + (i - 4) * width, bounds.y + 226, std::max(0.0f, width - 4), 36});
}
ContentDialog::ContentDialog(std::wstring title, std::shared_ptr<Element> content) {
    if (!content) throw std::invalid_argument("Dialog content is required");
    auto stack = std::make_shared<Stack>(Axis::vertical); stack->set_padding({16, 16, 16, 16}); stack->set_spacing(10);
    auto heading = std::make_shared<Label>(title); heading->set_heading(true); stack->add(heading); stack->add(content);
    validation_ = std::make_shared<InlineStatus>(); validation_->set_visible(false); stack->add(validation_);
    auto row = std::make_shared<Stack>(Axis::horizontal); row->set_spacing(8);
    primary_ = std::make_shared<Button>(L"OK"); cancel_ = std::make_shared<Button>(L"Cancel");
    row->add(primary_, 1); row->add(cancel_, 1); stack->add(row);
    popup_ = std::make_shared<Popup>(stack, std::move(title)); popup_->set_preferred_size({460, 360});
    popup_->set_dialog_surface(true);
    primary_->on_click([this] { accept(); }); cancel_->on_click([this] { cancel(); });
}
ContentDialog::~ContentDialog() { primary_->on_click({}); cancel_->on_click({}); }
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
