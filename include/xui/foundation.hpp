#pragma once

#include "xui/controls.hpp"
#include <locale>

namespace xui {

// Finite closed interval. The span and both steps must also be finite.
struct NumericRange {
    double minimum{0}, maximum{100}, small_step{1}, large_step{10};
    void validate() const;
    bool operator==(const NumericRange&) const = default;
};

enum class RangeKey { decrease, increase, page_decrease, page_increase, minimum, maximum };
class RangeInput final : public Control {
public:
    explicit RangeInput(std::wstring name = L"Value");
    const NumericRange& range() const { return range_; }
    void set_range(NumericRange range);
    double value() const { return value_; }
    double preview_value() const { return preview_.value_or(value_); }
    void set_value(double value);
    Axis orientation() const { return orientation_; }
    void set_orientation(Axis value);
    bool reversed() const { return reversed_; }
    void set_reversed(bool value);
    void on_preview(std::function<void(double)> callback) { preview_callback_ = std::move(callback); }
    void on_change(std::function<void(double)> callback) { change_ = std::move(callback); }
    void on_cancel(std::function<void(double)> callback) { cancel_callback_ = std::move(callback); }
    bool move(RangeKey key);
    bool begin_drag(double fraction);
    bool drag(double fraction);
    bool commit_drag();
    bool dragging() const { return preview_.has_value(); }
    void cancel() override;
    void cancel_drag();
    bool change_value(double value);
private:
    NumericRange range_;
    double value_{};
    std::optional<double> preview_;
    Axis orientation_{Axis::horizontal};
    bool reversed_{};
    std::function<void(double)> preview_callback_, change_, cancel_callback_;
};

struct ChoiceItem {
    std::uint64_t id{};
    std::wstring text;
    bool enabled{true};
    bool operator==(const ChoiceItem&) const = default;
};

// One native peer; accessible children use stable IDs, not retained row controls.
class RadioGroup final : public Control {
public:
    explicit RadioGroup(std::wstring name = L"Choices", bool list_presentation = false);
    const std::vector<ChoiceItem>& items() const { return items_; }
    void set_items(std::vector<ChoiceItem> items, std::optional<std::uint64_t> selected = {});
    std::optional<std::uint64_t> selected() const { return selected_; }
    void set_selected(std::uint64_t id);
    bool select(std::uint64_t id);
    bool step(int delta);
    bool type_ahead(std::wstring_view prefix);
    void on_change(std::function<void(std::uint64_t)> callback) { change_ = std::move(callback); }
    void on_accept(std::function<void(std::uint64_t)> callback) { accept_ = std::move(callback); }
    bool accept();
    Rect item_bounds(std::size_t index) const;
    std::optional<Rect> selected_item_bounds() const;
    std::optional<std::size_t> hit_test(float y) const;
    Size measure(Size available) override;
    void arrange(Rect bounds) override;
    float effective_row_height() const;
    float effective_row_pitch() const;
    float effective_vertical_padding() const;
    float effective_horizontal_padding() const;
    static constexpr float row_height = 34;
protected:
    void presentation_changed() override;
private:
    void reveal_selected();
    std::vector<ChoiceItem> items_;
    std::optional<std::uint64_t> selected_;
    std::size_t first_{};
    float item_height_{31};
    std::function<void(std::uint64_t)> change_, accept_;
};

enum class PopupPlacement { below, above, right, left };
enum class PopupDismissReason { cancel, commit, outside, focus_lost, hidden, owner_closed };
// Placement is clipped to the intersection of the window client and monitor work area.
Rect place_popup(Rect anchor, Size desired, Rect viewport, PopupPlacement placement);
class Popup final : public Control {
public:
    explicit Popup(std::shared_ptr<Element> content, std::wstring name = L"Popup");
    const std::shared_ptr<Element>& content() const { return children_[0]; }
    std::span<const std::shared_ptr<Element>> retained_children() const override { return children_; }
    void arrange(Rect bounds) override;
    bool is_open() const { return open_; }
    bool dialog_surface() const { return dialog_surface_; }
    std::uint64_t generation() const { return generation_; }
    bool current(std::uint64_t generation) const { return open_ && generation == generation_; }
    void set_placement(PopupPlacement value);
    PopupPlacement placement() const { return placement_; }
    void on_dismiss(std::function<void(PopupDismissReason)> callback) { dismiss_ = std::move(callback); }
    // Backend boundary. Window owns peer creation, focus, clipping, and dismissal.
    void opened();
    void closed(PopupDismissReason reason);
    std::function<void()> close_transition(PopupDismissReason reason);
private:
    friend class ContentDialog;
    void set_dialog_surface(bool value) { if (open_) throw std::logic_error("Cannot change an open popup role"); dialog_surface_ = value; }
    std::vector<std::shared_ptr<Element>> children_;
    PopupPlacement placement_{PopupPlacement::below};
    bool open_{};
    bool dialog_surface_{};
    std::uint64_t generation_{};
    std::function<void(PopupDismissReason)> dismiss_;
};

class ComboBox final : public Control {
public:
    explicit ComboBox(std::wstring name = L"Selection", bool editable = false);
    ~ComboBox();
    void set_items(std::vector<ChoiceItem> items, std::optional<std::uint64_t> selected = {});
    const std::vector<ChoiceItem>& items() const { return choices_->items(); }
    std::optional<std::uint64_t> selected() const { return selected_; }
    void set_selected(std::uint64_t id);
    bool select(std::uint64_t id);
    std::wstring selected_text() const;
    const std::shared_ptr<TextInput>& editor() const { return editor_; }
    const std::shared_ptr<Popup>& popup() const { return popup_; }
    const std::shared_ptr<RadioGroup>& choices() const { return choices_; }
    std::span<const std::shared_ptr<Element>> retained_children() const override { return children_; }
    void arrange(Rect bounds) override;
    Rect editor_bounds() const;
    Rect drop_down_bounds() const;
    void on_change(std::function<void(std::uint64_t)> callback) { change_ = std::move(callback); }
    // Editable text is separate from committed identity until an item is accepted.
    void on_edit(std::function<void(const std::wstring&)> callback) { edit_ = std::move(callback); }
    void prepare_popup();
protected:
    void presentation_changed() override;
private:
    void update_popup_size();
    std::shared_ptr<RadioGroup> choices_;
    std::shared_ptr<Popup> popup_;
    std::shared_ptr<TextInput> editor_;
    std::vector<std::shared_ptr<Element>> children_;
    std::optional<std::uint64_t> selected_;
    std::function<void(std::uint64_t)> change_;
    std::function<void(const std::wstring&)> edit_;
};

enum class NumberSpinPlacement { hidden, inline_buttons };
class NumericInput final : public Control {
public:
    explicit NumericInput(std::wstring name = L"Number");
    ~NumericInput();
    const NumericRange& range() const { return range_; }
    void set_range(NumericRange value);
    double value() const { return value_; }
    void set_value(double value);
    void set_locale(std::locale locale);
    const std::locale& locale() const { return locale_; }
    bool valid() const { return valid_; }
    const std::shared_ptr<TextInput>& editor() const { return editor_; }
    std::span<const std::shared_ptr<Element>> retained_children() const override { return children_; }
    void arrange(Rect bounds) override;
    // Existing XUI controls default to Inline; Hidden matches the WinUI default variant.
    NumberSpinPlacement spin_placement() const { return spin_placement_; }
    void set_spin_placement(NumberSpinPlacement value);
    Rect editor_bounds() const;
    Rect decrease_bounds() const;
    Rect increase_bounds() const;
    void on_change(std::function<void(double)> callback) { change_ = std::move(callback); }
    bool step(int direction);
    bool change_value(double value);
    bool commit_text(const std::wstring& text);
protected:
    void presentation_changed() override;
private:
    void format();
    NumericRange range_;
    double value_{};
    bool valid_{true};
    NumberSpinPlacement spin_placement_{NumberSpinPlacement::inline_buttons};
    std::locale locale_{""};
    std::shared_ptr<TextInput> editor_;
    std::shared_ptr<Button> decrease_, increase_;
    std::vector<std::shared_ptr<Element>> children_;
    std::function<void(double)> change_;
};

class Expander final : public Control {
public:
    Expander(std::wstring header, std::shared_ptr<Element> content);
    const std::shared_ptr<Element>& content() const { return children_[0]; }
    std::span<const std::shared_ptr<Element>> retained_children() const override { return children_; }
    bool expanded() const { return expanded_; }
    void set_expanded(bool value);
    void on_change(std::function<void(bool)> callback) { change_ = std::move(callback); }
    void arrange(Rect bounds) override;
    Size measure(Size available) override;
    float effective_header_height() const;
    Rect header_bounds() const;
    Rect content_bounds() const;
    static constexpr float header_height = 38;
protected:
    void presentation_changed() override;
private:
    void activate() override;
    std::vector<std::shared_ptr<Element>> children_;
    bool expanded_{true};
    float measured_header_height_{48};
    std::function<void(bool)> change_;
};

enum class ProgressState { determinate, indeterminate, paused, error, unknown };
class Progress final : public Control {
public:
    explicit Progress(std::wstring name = L"Progress");
    void set_range(double minimum, double maximum);
    const NumericRange& range() const { return range_; }
    void set_value(double value);
    double value() const { return value_; }
    void set_state(ProgressState value);
    ProgressState state() const { return state_; }
    // Capacity is read-only and never starts an animation.
    void set_capacity(double used, double total, std::wstring unit = L"bytes");
    const std::wstring& value_text() const { return text_; }
private:
    NumericRange range_;
    double value_{};
    ProgressState state_{ProgressState::determinate};
    std::wstring text_;
};

// Composition retains two independent keyboard/UIA Button targets.
class SplitButton final : public Stack {
public:
    SplitButton(std::wstring primary_name, std::wstring secondary_name);
    const std::shared_ptr<Button>& primary() const { return primary_; }
    const std::shared_ptr<Button>& secondary() const { return secondary_; }
private:
    std::shared_ptr<Button> primary_, secondary_;
};
}
