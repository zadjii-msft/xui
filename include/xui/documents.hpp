#pragma once
#include "xui/foundation.hpp"
#include <array>

namespace xui {

struct TextSelection {
    std::size_t start{}, end{};
    bool operator==(const TextSelection&) const = default;
};
enum class TextCommand { undo, redo, copy, cut, paste, select_all };
struct TextRun {
    std::wstring text;
    bool bold{}, italic{}, underline{};
    std::wstring link;
    bool operator==(const TextRun&) const = default;
};

// UTF-16 documents. The Windows RichEdit peer owns composition, selection, and undo.
class DocumentText : public Control {
public:
    static constexpr std::size_t document_limit = 1024 * 1024;
    const std::wstring& text() const { return text_; }
    void set_text(std::wstring value);
    std::size_t maximum_length() const { return maximum_; }
    void set_maximum_length(std::size_t value);
    bool read_only() const { return read_only_; }
    void set_read_only(bool value);
    bool monospace() const { return monospace_; }
    void set_monospace(bool value);
    bool rich() const { return rich_; }
    std::uint64_t revision() const { return revision_; }
    const std::vector<TextRun>& runs() const { return runs_; }
    TextSelection selection() const { return selection_; }
    void set_selection(TextSelection value);
    std::uint64_t selection_revision() const { return selection_revision_; }
    bool command(TextCommand command);
    void on_change(std::function<void(const std::wstring&)> callback) { change_ = std::move(callback); }
    void on_link(std::function<void(const std::wstring&)> callback) { link_ = std::move(callback); }
    // Native adapter boundaries. No clipboard or link action runs from a property setter.
    void commit_text(std::wstring value);
    void commit_selection(TextSelection value);
    void activate_link(std::size_t position);
    void bind_commands(std::function<bool(TextCommand)> callback) { command_ = std::move(callback); }
protected:
    DocumentText(std::wstring name, bool rich);
    void assign_runs(std::vector<TextRun> value);
private:
    std::wstring text_;
    std::vector<TextRun> runs_;
    std::size_t maximum_{65536};
    bool rich_{}, read_only_{}, monospace_{}, notifying_{};
    std::uint64_t revision_{1}, selection_revision_{};
    TextSelection selection_;
    std::function<void(const std::wstring&)> change_;
    std::function<void(const std::wstring&)> link_;
    std::function<bool(TextCommand)> command_;
};
class MultilineText final : public DocumentText {
public:
    explicit MultilineText(std::wstring name = L"Text document") : DocumentText(std::move(name), false) {}
};
class RichText final : public DocumentText {
public:
    explicit RichText(std::wstring name = L"Rich document") : DocumentText(std::move(name), true) {}
    void set_runs(std::vector<TextRun> value) { assign_runs(std::move(value)); }
};

enum class PasswordRevealPolicy { never, explicit_request };
class PasswordInput final : public Control {
public:
    explicit PasswordInput(std::wstring name = L"Password");
    ~PasswordInput() override;
    void set_password(std::wstring value);
    // Explicit application boundary. Never publish this value as a name, event, or UIA value.
    void with_password(const std::function<void(std::wstring_view)>& receiver) const;
    std::size_t length() const { return value_.size(); }
    std::size_t maximum_length() const { return maximum_; }
    void set_maximum_length(std::size_t value);
    void set_reveal_policy(PasswordRevealPolicy value);
    PasswordRevealPolicy reveal_policy() const { return policy_; }
    void set_revealed(bool value);
    bool revealed() const { return revealed_; }
    Size measure(Size available) override;
    std::uint64_t revision() const { return revision_; }
    void on_change(std::function<void()> callback) { change_ = std::move(callback); }
    void commit_password(std::wstring value);
private:
    std::wstring value_;
    std::size_t maximum_{256};
    PasswordRevealPolicy policy_{};
    bool revealed_{}, notifying_{};
    std::uint64_t revision_{1};
    std::function<void()> change_;
};

struct DateTimeValue {
    int year{2026}, month{1}, day{1}, hour{}, minute{}, second{};
    void validate() const;
    auto operator<=>(const DateTimeValue&) const = default;
};
enum class DateTimePresentation { date, time, calendar };
class DateTimePicker final : public Control {
public:
    explicit DateTimePicker(std::wstring name = L"Date", DateTimePresentation presentation = DateTimePresentation::date);
    DateTimePresentation presentation() const { return presentation_; }
    DateTimeValue value() const { return value_; }
    DateTimeValue minimum() const { return minimum_; }
    DateTimeValue maximum() const { return maximum_; }
    void set_value(DateTimeValue value);
    void set_range(DateTimeValue minimum, DateTimeValue maximum);
    void on_change(std::function<void(DateTimeValue)> callback) { change_ = std::move(callback); }
    bool change_value(DateTimeValue value);
    std::uint64_t revision() const { return revision_; }
private:
    DateTimePresentation presentation_;
    DateTimeValue minimum_{1601, 1, 1}, maximum_{9999, 12, 31, 23, 59, 59}, value_;
    std::uint64_t revision_{1};
    std::function<void(DateTimeValue)> change_;
};

enum class StatusSeverity { information, success, warning, error };
class InlineStatus final : public Control {
public:
    explicit InlineStatus(std::wstring message = L"");
    ~InlineStatus() override;
    void set_message(std::wstring message, StatusSeverity severity = StatusSeverity::information);
    StatusSeverity severity() const { return severity_; }
    void set_dismissible(bool value);
    bool dismissed() const { return dismissed_; }
    void show();
    void dismiss();
    void on_dismiss(std::function<void()> callback) { dismiss_callback_ = std::move(callback); }
    void set_action(std::wstring label, std::function<void()> callback);
    std::span<const std::shared_ptr<Element>> retained_children() const override { return children_; }
    void arrange(Rect bounds) override;
private:
    StatusSeverity severity_{};
    bool dismissible_{}, dismissed_{};
    std::shared_ptr<Button> action_, dismiss_;
    std::vector<std::shared_ptr<Element>> children_;
    std::function<void()> dismiss_callback_;
};

// Unpremultiplied sRGB bytes. Alpha is independent of the RGB channels.
struct RgbaColor {
    std::uint8_t red{}, green{}, blue{}, alpha{255};
    bool operator==(const RgbaColor&) const = default;
};
class ColorPicker final : public Control {
public:
    explicit ColorPicker(std::wstring name = L"Color");
    ~ColorPicker() override;
    RgbaColor value() const { return value_; }
    void set_value(RgbaColor value);
    bool change_value(RgbaColor value);
    void on_change(std::function<void(RgbaColor)> callback) { change_ = std::move(callback); }
    void set_swatches(std::vector<RgbaColor> values);
    const std::vector<RgbaColor>& swatches() const { return swatches_; }
    const std::array<std::shared_ptr<NumericInput>, 4>& channels() const { return channels_; }
    std::span<const std::shared_ptr<Element>> retained_children() const override { return children_; }
    void arrange(Rect bounds) override;
private:
    void sync();
    RgbaColor value_;
    std::array<std::shared_ptr<NumericInput>, 4> channels_;
    std::vector<RgbaColor> swatches_;
    std::vector<std::shared_ptr<Element>> children_;
    std::function<void(RgbaColor)> change_;
};

enum class DialogResult { primary, cancel };
class ContentDialog final : public std::enable_shared_from_this<ContentDialog> {
public:
    ContentDialog(std::wstring title, std::shared_ptr<Element> content);
    ~ContentDialog();
    const std::shared_ptr<Popup>& popup() const { return popup_; }
    const std::shared_ptr<Button>& primary() const { return primary_; }
    const std::shared_ptr<Button>& cancel_button() const { return cancel_; }
    const std::shared_ptr<InlineStatus>& validation() const { return validation_; }
    void on_validate(std::function<std::wstring()> callback) { validate_ = std::move(callback); }
    void on_result(std::function<void(DialogResult)> callback) { result_ = std::move(callback); }
    // Window binds dismissal while open; callbacks run after the popup closes.
    void bind_close(std::function<void(DialogResult)> callback) { close_ = std::move(callback); }
    void accept();
    void cancel();
    void notify_result(DialogResult result);
private:
    std::shared_ptr<Popup> popup_;
    std::shared_ptr<Button> primary_, cancel_;
    std::shared_ptr<InlineStatus> validation_;
    std::function<std::wstring()> validate_;
    std::function<void(DialogResult)> result_, close_;
};
}
