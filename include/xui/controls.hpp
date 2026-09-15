#pragma once

#include "xui/core.hpp"
#include "xui/theme.hpp"
#include <span>

namespace xui {

enum class ControlRole { label, button, toggle, text_input, file_list, scroll_view, image, tab_strip, split_view, content_view, data_grid, history_chart,
    popup, radio_group, choice_list, combo_box, numeric_input, range_input, expander, progress, items_view, tree_view, command_menu,
    document_text, password_input, date_time, inline_status, color_picker, vector_canvas, map_view, media_playback, web_content };
enum class TextStyle { body, caption, heading, subtitle, body_strong };
using TextMeasurer = std::function<Size(std::wstring_view, TextStyle)>;
enum class ActivationKey { space, enter };
enum class TextTone { normal, secondary, accent, error };
enum class ButtonIcon { none, back, forward, up, refresh, split, theme, add, minimize, maximize, restore, close, more,
    menu, home, folder, settings, search, library, history, bookmark, drive };
enum class ButtonBehavior { momentary, repeat, toggle, dropdown };
struct MenuItem {
    // Use '&' for a mnemonic, '&&' for a literal '&', and '\t' before a shortcut label.
    // Shortcut labels do not register application keyboard shortcuts.
    std::wstring text;
    std::function<void()> action;
    bool enabled{true}, checked{}, separator{};
};
enum class ShellMenuPresentation { windows, xui };
struct ContextMenuContent {
    std::vector<MenuItem> items;
    std::vector<std::wstring> shell_paths;
    std::function<bool()> current;
    ShellMenuPresentation presentation{ShellMenuPresentation::windows};
};

// Control state and actions are independent of Windows and the renderer.
class Control : public Element {
public:
    ControlRole role() const { return role_; }
    VisualStyle visual_style() const { return visual_style_; }
    // Backend presentation context. Changing style preserves the control model.
    void set_visual_style(VisualStyle style);
    const std::wstring& name() const { return name_; }
    const std::wstring& automation_id() const { return automation_id_; }
    void set_automation_id(std::wstring value) { automation_id_ = std::move(value); invalidate(Invalidation::paint); }
    void set_name(std::wstring name);
    bool enabled() const { return enabled_; }
    bool visible() const { return visible_; }
    void set_visible(bool value) { if (visible_ != value) { visible_ = value; invalidate(Invalidation::layout); } }
    void set_enabled(bool enabled);
    bool focused() const { return focused_; }
    // Backend state transition. Applications request focus through Window::focus.
    void set_focused(bool focused);
    void on_focus(std::function<void()> callback) { focus_ = std::move(callback); }
    bool hovered() const { return hovered_; }
    bool pressed() const { return pointer_ ? hovered_ : keyboard_; }
    bool captured() const { return pointer_; }
    bool tab_stop() const { return tab_stop_; }
    void set_tab_stop(bool value) { if (tab_stop_ != value) { tab_stop_ = value; invalidate(Invalidation::layout); } }
    bool focusable() const { return enabled_ && role_ != ControlRole::label && role_ != ControlRole::image && role_ != ControlRole::content_view && role_ != ControlRole::history_chart && role_ != ControlRole::popup && role_ != ControlRole::progress && role_ != ControlRole::inline_status && role_ != ControlRole::color_picker; }
    const std::wstring& help_text() const { return help_text_; }
    void set_help_text(std::wstring value);
    unsigned tooltip_delay() const { return tooltip_delay_; }
    void set_tooltip_delay(unsigned milliseconds);
    virtual std::span<const std::shared_ptr<Element>> retained_children() const { return {}; }
    // Input adapter boundary. Applications normally use callbacks and property setters.
    void pointer_move(bool inside);
    bool pointer_down();
    bool pointer_up(bool inside);
    virtual void cancel();
    bool key_down(ActivationKey key, bool repeat = false);
    bool key_up(ActivationKey key);
    bool invoke();
    Size measure(Size available) override;
    // Backend boundary. Cached until text, typography, or the measurer changes.
    void set_text_measurer(TextMeasurer measurer);
    Size measured_text();
    virtual TextStyle text_style() const { return TextStyle::body; }
    // The Windows backend snapshots flat items, closes the menu, then runs one enabled action.
    void on_context_menu(std::function<std::vector<MenuItem>()> callback) {
        if (!callback) { menu_ = {}; return; }
        menu_ = [callback = std::move(callback)] { return ContextMenuContent{callback()}; };
    }
    void on_context_menu_content(std::function<ContextMenuContent()> callback) { menu_ = std::move(callback); }
    bool has_context_menu() const { return bool(menu_); }
    ContextMenuContent context_menu_content() const {
        auto callback = menu_;
        return callback ? callback() : ContextMenuContent{};
    }
    std::vector<MenuItem> context_menu() const { return context_menu_content().items; }
protected:
    Control(ControlRole role, std::wstring name, Size preferred);
    virtual void activate() {}
    bool actionable() const;
    void text_changed();
    virtual void presentation_changed() {}
private:
    ControlRole role_;
    VisualStyle visual_style_{VisualStyle::classic};
    std::wstring name_;
    std::wstring automation_id_;
    std::wstring help_text_;
    unsigned tooltip_delay_{600};
    bool enabled_{true}, visible_{true}, focused_{}, hovered_{}, pointer_{}, keyboard_{};
    bool tab_stop_{true};
    std::function<ContextMenuContent()> menu_;
    std::function<void()> focus_;
    TextMeasurer measurer_;
    Size text_size_{};
    bool text_dirty_{true};
};

class Label final : public Control {
public:
    using WrappedTextMeasurer = std::function<Size(std::wstring_view, TextStyle, float, std::size_t)>;
    explicit Label(std::wstring text) : Control(ControlRole::label, std::move(text), {320, 28}) { set_auto_size(true); }
    void set_text(std::wstring text) { set_name(std::move(text)); }
    const std::wstring& text() const { return name(); }
    TextTone tone() const { return tone_; }
    void set_tone(TextTone value) { tone_ = value; invalidate(Invalidation::paint); }
    bool caption() const { return caption_; }
    void set_caption(bool value) { if (caption_ != value) { caption_ = value; text_changed(); } }
    TextStyle text_style() const override { return subtitle_ ? TextStyle::subtitle : heading_ ? TextStyle::heading :
        body_strong_ ? TextStyle::body_strong : caption_ ? TextStyle::caption : TextStyle::body; }
    void set_body_strong(bool value) { if (body_strong_ != value) { body_strong_ = value; text_changed(); } }
    void set_subtitle(bool value) { if (subtitle_ != value) { subtitle_ = value; text_changed(); } }
    void set_wrapping(bool value, std::size_t maximum_lines = 0);
    bool wrapping() const { return wrapping_; }
    void set_wrapped_text_measurer(WrappedTextMeasurer measurer);
    void discard_wrapped_text() { wrapped_valid_ = false; }
    Size wrapped_text(float width);
    Size measure(Size available) override;
    bool heading() const { return heading_; }
    void set_heading(bool heading) {
        if (heading_ == heading) return;
        heading_ = heading;
        text_changed();
    }
private:
    void presentation_changed() override { discard_wrapped_text(); }
    WrappedTextMeasurer wrapped_measurer_;
    std::wstring wrapped_name_;
    Size wrapped_size_{};
    float wrapped_width_{};
    TextStyle wrapped_style_{};
    std::size_t maximum_lines_{};
    bool wrapping_{}, wrapped_valid_{};
    bool heading_{};
    bool subtitle_{};
    bool body_strong_{};
    bool caption_{};
    TextTone tone_{};
};

class Button final : public Control {
public:
    explicit Button(std::wstring text) : Control(ControlRole::button, std::move(text), {240, 40}) { set_auto_size(true); }
    void on_click(std::function<void()> callback) { click_ = std::move(callback); }
    ButtonBehavior behavior() const { return behavior_; }
    void set_behavior(ButtonBehavior value);
    ButtonAppearance appearance() const { return appearance_; }
    void set_appearance(ButtonAppearance value);
    bool checked() const { return checked_; }
    void set_checked(bool value);
    void on_toggle(std::function<void(bool)> callback) { toggle_ = std::move(callback); }
    unsigned repeat_delay() const { return repeat_delay_; }
    unsigned repeat_interval() const { return repeat_interval_; }
    void set_repeat_timing(unsigned delay, unsigned interval);
    // Icon-only presentation retains name() for accessibility and commands.
    void set_icon(ButtonIcon value) {
        if (icon_ == value) return;
        icon_ = value; invalidate(Invalidation::layout);
    }
    ButtonIcon icon() const { return icon_; }
    Size measure(Size available) override {
        const float size = style_metrics(visual_style()).button_height;
        return icon_ == ButtonIcon::none || !auto_size() ? Control::measure(available) : constrain({size, size}, available);
    }
private:
    ButtonIcon icon_{};
    ButtonBehavior behavior_{};
    ButtonAppearance appearance_{};
    bool checked_{};
    unsigned repeat_delay_{400}, repeat_interval_{80};
    std::function<void(bool)> toggle_;
    void activate() override;
    std::function<void()> click_;
};

class Toggle final : public Control {
public:
    explicit Toggle(std::wstring text) : Control(ControlRole::toggle, std::move(text), {320, 36}) { set_auto_size(true); }
    bool checked() const { return checked_; }
    // Property updates do not invoke the application callback.
    void set_checked(bool checked);
    void on_change(std::function<void(bool)> callback) { change_ = std::move(callback); }
private:
    void activate() override;
    bool checked_{};
    std::function<void(bool)> change_;
};

// Retains ordinary content; unlike FileList, this does not virtualize children.
class ScrollView final : public Control {
public:
    explicit ScrollView(std::shared_ptr<Element> content, std::wstring name = L"Scrollable content");
    const std::shared_ptr<Element>& content() const { return content_; }
    Size measure(Size available) override;
    void arrange(Rect bounds) override;
    float offset() const { return passthrough_ ? 0 : offset_; }
    bool passthrough() const { return passthrough_; }
    void set_passthrough(bool value) { if (passthrough_ != value) { passthrough_ = value; invalidate(Invalidation::layout); } }
    bool overlay_scrollbar() const { return overlay_scrollbar_; }
    void set_overlay_scrollbar(bool value) { if (overlay_scrollbar_ != value) { overlay_scrollbar_ = value; invalidate(Invalidation::layout); } }
    float extent() const { return extent_; }
    float maximum_offset() const;
    void set_offset(float offset);
    void scroll_by(float delta) { set_offset(offset_ + delta); }
    void reveal(Rect bounds);
    Rect viewport() const;
    Rect thumb() const;
    static constexpr float bar_width = 12;
private:
    bool passthrough_{}, overlay_scrollbar_{};
    std::shared_ptr<Element> content_;
    float offset_{}, extent_{};
};

class SuggestionSource;

class TextInput final : public Control {
public:
    struct Selection {
        std::size_t start{}, end{};
        bool operator==(const Selection&) const = default;
    };
    explicit TextInput(std::wstring name) : Control(ControlRole::text_input, std::move(name), {320, 68}) {}
    Size measure(Size available) override;
    float caption_extent() const {
        const auto metrics = style_metrics(visual_style());
        return caption_visible() ? metrics.input_header_height + metrics.input_header_spacing : 0;
    }
    const std::wstring& text() const { return text_; }
    void set_text(std::wstring text);
    Selection selection() const;
    void set_selection(Selection value);
    // UTF-16 endpoints clamp to the text and never split a surrogate pair.
    static Selection normalize_selection(std::wstring_view text, Selection value);
    // Backend boundary. The native reader/writer must enforce peer lifetime and thread affinity.
    void bind_selection(std::function<Selection()> reader, std::function<void(Selection)> writer);
    void set_maximum_length(std::size_t value);
    std::size_t maximum_length() const { return maximum_length_; }
    void on_change(std::function<void(const std::wstring&)> callback) { change_ = std::move(callback); }
    // Backend boundary: publish committed native text, not IME preedit text.
    void commit_text(std::wstring text);
    void set_search_style(bool value) { search_ = value; invalidate(Invalidation::layout); }
    bool search_style() const { return search_; }
    void set_caption_visible(bool value) {
        if (caption_visible_ == value) return;
        caption_visible_ = value; invalidate(Invalidation::layout);
    }
    bool caption_visible() const { return caption_visible_ && !search_; }
    bool shortcut_visible(float width) const { return !shortcut_.empty() && (search_ || caption_visible_ || width >= 260); }
    void set_placeholder(std::wstring value) { placeholder_ = std::move(value); invalidate(Invalidation::paint); }
    const std::wstring& placeholder() const { return placeholder_; }
    const std::wstring& shortcut_hint() const { return shortcut_; }
    void set_shortcut_hint(std::wstring value) { shortcut_ = std::move(value); invalidate(Invalidation::paint); }
    void on_submit(std::function<void()> callback) { submit_ = std::move(callback); }
    void submit() { if (submit_) { auto callback = submit_; callback(); } }
    void set_suggestions(std::shared_ptr<SuggestionSource> source) {
        suggestions_ = std::move(source); ++suggestion_revision_; invalidate(Invalidation::paint);
    }
    const std::shared_ptr<SuggestionSource>& suggestions() const { return suggestions_; }
    void set_suggestion_context(std::wstring context) {
        if (suggestion_context_ == context) return;
        suggestion_context_ = std::move(context); ++suggestion_revision_; invalidate(Invalidation::paint);
    }
    const std::wstring& suggestion_context() const { return suggestion_context_; }
    std::uint64_t suggestion_revision() const { return suggestion_revision_; }
private:
    void assign_text(std::wstring text);
    std::shared_ptr<SuggestionSource> suggestions_;
    std::wstring suggestion_context_;
    std::uint64_t suggestion_revision_{};
    std::wstring text_;
    std::wstring placeholder_;
    std::wstring shortcut_;
    bool search_{};
    bool caption_visible_{true};
    std::size_t maximum_length_{1024};
    std::function<void()> submit_;
    std::function<void(const std::wstring&)> change_;
    Selection selection_;
    std::function<Selection()> read_selection_;
    std::function<void(Selection)> write_selection_;
};

struct TabItem {
    std::uint64_t id{};
    std::wstring title;
    bool operator==(const TabItem&) const = default;
};

// One retained control and one native peer, regardless of tab count.
class TabStrip final : public Control {
public:
    explicit TabStrip(std::wstring name = L"Tabs") : Control(ControlRole::tab_strip, std::move(name), {320, 38}) {}
    const std::vector<TabItem>& tabs() const { return tabs_; }
    std::optional<std::uint64_t> selected() const { return selected_; }
    void set_tabs(std::vector<TabItem> tabs, std::optional<std::uint64_t> selected);
    bool select(std::uint64_t id);
    void step(int delta);
    void request_close(std::uint64_t id);
    void on_select(std::function<void(std::uint64_t)> callback) { select_ = std::move(callback); }
    void on_close(std::function<void(std::uint64_t)> callback) { close_ = std::move(callback); }
    bool closable() const { return bool(close_); }
    Rect tab_bounds(std::size_t index) const;
    Rect close_bounds(std::size_t index) const;
    std::optional<std::size_t> hit_test(float x) const;
    void arrange(Rect bounds) override;
private:
    void reveal_selected();
    std::vector<TabItem> tabs_;
    std::optional<std::uint64_t> selected_;
    std::size_t first_{};
    std::function<void(std::uint64_t)> select_, close_;
};

// A clipped retained subtree. It adds no renderer or independent message loop.
class ContentView final : public Control {
public:
    explicit ContentView(std::shared_ptr<Element> content, std::wstring name = L"Pane");
    const std::shared_ptr<Element>& content() const { return content_; }
    void arrange(Rect bounds) override;
private:
    std::shared_ptr<Element> content_;
};

// Retains pages but arranges only the active content host. Native peers are
// created on first use and retained thereafter, including native editor undo.
// Selection performs no I/O.
class PageView final : public Stack {
public:
    PageView() : Stack(Axis::vertical) { set_preferred_size({640, 360}); }
    void add_page(std::shared_ptr<Element> content);
    void select(std::size_t index);
    std::size_t selected() const { return selected_; }
    Size measure(Size available) override { return Element::measure(available); }
    void arrange(Rect bounds) override;
private:
    std::size_t selected_{};
};

class SplitView final : public Control {
public:
    SplitView(std::shared_ptr<Element> first, std::shared_ptr<Element> second,
        std::wstring name = L"Pane divider");
    const std::shared_ptr<ContentView>& first() const { return first_; }
    const std::shared_ptr<ContentView>& second() const { return second_; }
    void arrange(Rect bounds) override;
    void set_ratio(float ratio);
    float ratio() const { return ratio_; }
    void set_secondary_visible(bool visible);
    bool secondary_visible() const { return secondary_visible_; }
    bool expanded() const;
    void on_expanded(std::function<void(bool)> callback) { expanded_callback_ = std::move(callback); }
    Rect divider() const;
    static constexpr float divider_width = 10;
    static constexpr float minimum_pane_width = 300;
private:
    std::shared_ptr<ContentView> first_, second_;
    float ratio_{0.5f};
    bool secondary_visible_{true};
    bool arranged_expanded_{};
    std::function<void(bool)> expanded_callback_;
};

// Returns null when there are no enabled focus targets. Traversal wraps.
Control* next_focus(std::span<Control* const> controls, const Control* current, bool reverse);

}
