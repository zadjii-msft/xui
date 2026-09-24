#pragma once
#include "xui/animation.hpp"

#include "xui/core.hpp"
#include "xui/virtual_viewport.hpp"
#include "xui/theme.hpp"
#include "xui/styling.hpp"
#include "xui/control_styling.hpp"
#include <span>
#include <stdexcept>

namespace xui {

enum class ControlRole { label, button, toggle, text_input, file_list, scroll_view, image, tab_strip, split_view, content_view, data_grid, history_chart,
    popup, radio_group, choice_list, combo_box, numeric_input, range_input, expander, progress, items_view, tree_view, command_menu,
    document_text, password_input, date_time, inline_status, color_picker, vector_canvas, map_view, media_playback, web_content, swap_chain_panel };
enum class TextStyle { body, caption, heading, subtitle, body_strong };
using TextMeasurer = std::function<Size(std::wstring_view, TextStyle)>;
enum class ActivationKey { space, enter };
enum class TextTone { normal, secondary, accent, error };
enum class ButtonIcon { none, back, forward, up, refresh, split, theme, add, minimize, maximize, restore, close, more,
    menu, home, folder, settings, search, library, history, bookmark, drive, open,
    save = 23, save_as = 24, undo = 25, redo = 26, chevron_up = 27, chevron_down = 28, chevron_right = 29,
    folders_first = 30, files_first = 31, mixed = 32 };
enum class ButtonBehavior { momentary, repeat, toggle, dropdown };
enum class CheckState { unchecked, checked, indeterminate };
enum class InfoBadgeKind { dot, count, icon };
struct MenuIcon {
    // Top-down premultiplied BGRA. An empty image selects the theme-colored command glyph.
    std::uint32_t width{}, height{};
    std::vector<std::uint32_t> pixels;
};
struct MenuItem {
    // Use '&' for a mnemonic, '&&' for a literal '&', and '\t' before a shortcut label.
    // Shortcut labels do not register application keyboard shortcuts.
    std::wstring text;
    std::function<void()> action;
    bool enabled{true}, checked{}, separator{};
    std::shared_ptr<const MenuIcon> icon;
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
    bool thumbnail_fill() const { return thumbnail_fill_; }
    void set_thumbnail_fill(bool value) { thumbnail_fill_ = value; invalidate(Invalidation::paint); }
    bool single_click_activation() const { return single_click_activation_; }
    void set_single_click_activation(bool value) { single_click_activation_ = value; }
    float presentation_font_size() const { return presentation_font_size_; }
    void set_presentation_font_size(float value);
    const std::shared_ptr<const StyleFontFamily>& presentation_font_family() const { return presentation_font_family_; }
    void set_presentation_font_family(std::string_view value);
    // Backend presentation context. Changing style preserves the control model.
    void set_visual_style(VisualStyle style);
    const std::wstring& name() const { return name_; }
    const std::wstring& automation_id() const { return automation_id_; }
    void set_automation_id(std::wstring value) { automation_id_ = std::move(value); invalidate(Invalidation::paint); }
    void set_name(std::wstring name);
    virtual void validate_name(std::wstring_view) const {}
    bool enabled() const { return enabled_; }
    bool visible() const { return visible_; }
    bool participates_in_layout() const override { return visible_; }
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
    bool supports_axis_constraints() const override;
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
    void invalidate_state();
    Size measure_control(Size available, bool natural);
    virtual void presentation_changed() {}
    void control_style_changed(Invalidation kind) override;
    // The state bits this control currently contributes (focused/hovered/
    // pressed/disabled from the base state, plus any state a derived control
    // adds, e.g. Toggle ORs in checked).
    StyleStateMask control_style_state_bits() const override;
private:
    friend class Window;
    ControlRole role_;
    VisualStyle visual_style_{VisualStyle::classic};
    std::wstring name_;
    std::wstring automation_id_;
    std::wstring help_text_;
    unsigned tooltip_delay_{600};
    bool enabled_{true}, visible_{true}, focused_{}, hovered_{}, pointer_{}, keyboard_{};
    bool tab_stop_{true};
    bool thumbnail_fill_{}, single_click_activation_{};
    float presentation_font_size_{};
    std::shared_ptr<const StyleFontFamily> presentation_font_family_;
    std::function<ContextMenuContent()> menu_;
    std::function<void()> focus_;
    TextMeasurer measurer_;
    Size text_size_{};
    bool text_dirty_{true};
};

enum class LabelOverflow { clip, character_ellipsis };
struct LabelTextLayout {
    bool wrapping{};
    LabelOverflow overflow{LabelOverflow::clip};
    uint32_t maximum_lines{};
    bool operator==(const LabelTextLayout&) const = default;
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
    void set_text_layout(std::optional<LabelTextLayout> layout);
    const std::optional<LabelTextLayout>& text_layout() const { return text_layout_; }
    bool clips_text() const { return text_layout_ && text_layout_->overflow == LabelOverflow::clip; }
    void validate_name(std::wstring_view text) const override;
    bool wrapping() const;
    std::size_t maximum_lines() const;
    StylePart text_part() const {
        const auto style = text_style();
        return style == TextStyle::heading || style == TextStyle::subtitle ? StylePart::heading :
            style == TextStyle::caption ? StylePart::caption : StylePart::label;
    }
    Rect content_bounds(Rect bounds) const;
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
    std::optional<StyleTarget> control_style_target() const override { return StyleTarget::label; }
    Size measure_label(Size available, bool natural);
    StyleStateMask control_style_state_bits() const override {
        return Control::control_style_state_bits() & style_states::disabled;
    }
    void presentation_changed() override { discard_wrapped_text(); }
    WrappedTextMeasurer wrapped_measurer_;
    std::wstring wrapped_name_;
    Size wrapped_size_{};
    float wrapped_width_{};
    TextStyle wrapped_style_{};
    std::size_t maximum_lines_{};
    bool wrapping_{}, wrapped_valid_{};
    bool wrapping_explicit_{};
    std::optional<LabelTextLayout> text_layout_;
    std::size_t wrapped_lines_{};
    bool heading_{};
    bool subtitle_{};
    bool body_strong_{};
    bool caption_{};
    TextTone tone_{};
};

class Button : public Control {
public:
    explicit Button(std::wstring text) : Control(ControlRole::button, std::move(text), {240, 40}) { set_auto_size(true); }
    void on_click(std::function<void()> callback) { click_ = std::move(callback); }
    const std::function<void()>& click_callback() const { return click_; }
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
        if (value < ButtonIcon::none || value > ButtonIcon::mixed)
            throw std::invalid_argument("Invalid button icon");
        if (icon_ == value) return;
        icon_ = value; invalidate(Invalidation::layout);
    }
    ButtonIcon icon() const { return icon_; }
    void set_style(std::shared_ptr<const ButtonStyle> style);
    std::shared_ptr<const ButtonStyle> style() const;
    void set_style_values(ButtonStyleValues values);
    const ButtonStyleValues& style_values() const;
    const ButtonStyleValues* effective_style_values() const;
    // Backend context includes disabled ancestors and modal input restrictions.
    void set_style_enabled(bool enabled);
    Invalidation style_state_changed();
    PartStyleValues surface_style_values() const;
    PartStyleValues content_style_values(StylePart part) const;
    Rect content_bounds(Rect bounds) const;
    Rect icon_bounds(Rect bounds) const;
    Rect dropdown_bounds(Rect bounds) const;
    Size measure(Size available) override;
private:
    friend class Control;
    std::optional<StyleTarget> control_style_target() const override { return StyleTarget::button; }
    StyleStateMask control_style_state_bits() const override {
        return Control::control_style_state_bits() | (checked_ ? style_states::checked : 0);
    }
    Size measure_button(Size available, bool natural);
    Size measure_control_styled(Size available, bool natural);
    PartStyleValues own_surface_style_values() const;
    Size measure_styled(Size available, bool natural);
    struct StyleData {
        std::shared_ptr<const ButtonStyle> style;
        ButtonStyleValues local;
        mutable ButtonStyleValues effective;
        mutable unsigned mask{32};
        bool context_enabled{true};
    };
    unsigned style_state_mask() const;
    void replace_style_data(std::unique_ptr<StyleData> next);
    std::unique_ptr<StyleData> style_data_;
    ButtonIcon icon_{};
    ButtonBehavior behavior_{};
    ButtonAppearance appearance_{};
    bool checked_{};
    unsigned repeat_delay_{400}, repeat_interval_{80};
    std::function<void(bool)> toggle_;
    void activate() override;
    std::function<void()> click_;
};

class ToggleButton final : public Button {
public:
    explicit ToggleButton(std::wstring text) : Button(std::move(text)) { set_behavior(ButtonBehavior::toggle); }
};

class HyperlinkButton final : public Button {
public:
    explicit HyperlinkButton(std::wstring name) : Button(std::move(name)) { set_appearance(ButtonAppearance::subtle); }
};

class Toggle : public Control {
public:
    explicit Toggle(std::wstring text) : Toggle(std::move(text), false) {}
    bool switch_presentation() const { return switch_; }
    bool checked() const { return state_ == CheckState::checked; }
    bool indeterminate() const { return state_ == CheckState::indeterminate; }
    // Property updates do not invoke the application callback.
    void set_checked(bool checked);
    void on_change(std::function<void(bool)> callback) { change_ = std::move(callback); }
    // Thin forwarders onto Control's shared styling engine.
    void set_style(std::shared_ptr<const ControlStyle> style) { set_control_style(std::move(style)); }
    std::shared_ptr<const ControlStyle> style() const { return control_style(); }
    void set_style_values(StylePart part, PartStyleValues values) { set_control_style_values(part, std::move(values)); }
    const PartStyleValues& style_values(StylePart part) const { return control_style_values(part); }
    const PartStyleValues* effective_style_values(StylePart part) const { return effective_control_style_values(part); }
    // Only diverges from the default measurement when a layout-affecting
    // property (root padding/border, or indicator size) is actually
    // authored; an unstyled or paint-only-styled Toggle measures identically
    // to before this engine existed.
    Size measure(Size available) override;
    // Reusable geometry so painting and hit testing agree with what
    // measurement assumed, instead of each reconstructing indicator/mark
    // placement independently.
    struct Layout { Insets padding, border, indicator_border; float indicator_size, gap; };
    Layout layout_metrics() const;
    Rect indicator_bounds(Rect bounds) const;
    Rect mark_bounds(Rect bounds) const;
    Rect mark_bounds(Rect bounds, bool enabled) const;
    Rect content_bounds(Rect bounds) const;
    Rect label_bounds(Rect bounds) const;
protected:
    CheckState check_state() const { return state_; }
    void set_check_state(CheckState state);
    Toggle(std::wstring text, bool switch_presentation)
        : Control(ControlRole::toggle, std::move(text), {320, 36}), switch_(switch_presentation) { set_auto_size(true); }
    std::optional<StyleTarget> control_style_target() const override { return StyleTarget::toggle; }
    StyleStateMask control_style_state_bits() const override {
        return Control::control_style_state_bits() | (state_ != CheckState::unchecked ? style_states::checked : 0);
    }
private:
    void activate() override;
    Size measure_toggle(Size available, bool natural);
    CheckState state_{CheckState::unchecked};
    bool switch_{};
    std::function<void(bool)> change_;
};

class ToggleSwitch final : public Toggle {
public:
    explicit ToggleSwitch(std::wstring text) : Toggle(std::move(text), true) {}
};

class CheckBox final : public Toggle {
public:
    explicit CheckBox(std::wstring name) : Toggle(std::move(name)) {}
    CheckState state() const { return check_state(); }
    void set_state(CheckState value) { set_check_state(value); }
    bool three_state() const { return three_state_; }
    void set_three_state(bool value) { if (three_state_ != value) { cancel(); three_state_ = value; } }
    void on_change(std::function<void(CheckState)> callback) { change_ = std::move(callback); }
private:
    void activate() override;
    bool three_state_{};
    std::function<void(CheckState)> change_;
};

class InfoBadge final : public Control {
public:
    explicit InfoBadge(std::wstring name) : Control(ControlRole::inline_status, std::move(name), {8, 8}) { set_auto_size(true); }
    InfoBadgeKind kind() const { return kind_; }
    std::uint32_t count() const { return count_; }
    void set_count(std::uint32_t value);
    ButtonIcon icon() const { return icon_; }
    void set_icon(ButtonIcon value);
    void set_dot();
    std::wstring display_text() const;
    Size measure(Size available) override;
protected:
    std::optional<StyleTarget> control_style_target() const override { return StyleTarget::inline_status; }
private:
    InfoBadgeKind kind_{InfoBadgeKind::dot};
    std::uint32_t count_{};
    ButtonIcon icon_{};
};

// Retains ordinary content; unlike FileList, this does not virtualize children.
class ScrollView final : public Control {
public:
    explicit ScrollView(std::shared_ptr<Element> content, std::wstring name = L"Scrollable content");
    const std::shared_ptr<Element>& content() const { return content_; }
    std::shared_ptr<VirtualViewport> begin_virtual_viewport(std::uint32_t item_count, float row_height, std::uint64_t source_version);
    const std::shared_ptr<VirtualViewport>& virtual_viewport() const { return virtual_viewport_; }
    Rect available_viewport() const;
    Size measure(Size available) override;
    void arrange(Rect bounds) override;
    float offset() const { return passthrough_ ? 0 : offset_; }
    bool passthrough() const { return passthrough_; }
    bool fill_viewport() const { return fill_viewport_; }
    void set_fill_viewport(bool value) {
        if (value && virtual_viewport_) throw std::logic_error("Virtual viewports do not fill ordinary scroll content");
        if (fill_viewport_ != value) { fill_viewport_ = value; invalidate(Invalidation::layout); }
    }
    void set_passthrough(bool value) {
        if (value && virtual_viewport_) throw std::logic_error("Virtual viewports cannot use scroll passthrough");
        if (passthrough_ != value) { passthrough_ = value; invalidate_control_style_state(); invalidate(Invalidation::layout); }
    }
    bool overlay_scrollbar() const { return overlay_scrollbar_; }
    void set_overlay_scrollbar(bool value) { if (overlay_scrollbar_ != value) { overlay_scrollbar_ = value; invalidate(Invalidation::layout); } }
    float extent() const { return extent_; }
    float maximum_offset() const;
    void set_offset(float offset);
    void scroll_by(float delta) { set_offset((virtual_viewport_ ? virtual_viewport_->requested_offset() : offset_) + delta); }
    void reveal(Rect bounds);
    Rect viewport() const;
    Rect thumb() const;
    Rect scrollbar_track() const;
    Rect scrollbar_thumb_track() const;
    float effective_bar_width() const;
    float requested_maximum_offset() const { return virtual_viewport_ ? virtual_viewport_->requested_maximum_offset() : maximum_offset(); }
    void set_style_dragging(bool dragging);
    static constexpr float bar_width = 12;
protected:
    std::optional<StyleTarget> control_style_target() const override { return StyleTarget::scroll_view; }
    StyleStateMask control_style_state_bits() const override;
private:
    bool passthrough_{}, overlay_scrollbar_{};
    bool fill_viewport_{true};
    Size measure_scroll(Size available, bool natural);
    bool style_dragging_{}, style_scrollable_{};
    std::shared_ptr<Element> content_;
    std::shared_ptr<VirtualViewport> virtual_viewport_;
    float offset_{}, extent_{};
};

class SuggestionSource;
enum class TextInputPurpose { normal, email, url, telephone, number };

class TextInput final : public Control {
public:
    struct Selection {
        std::size_t start{}, end{};
        bool operator==(const Selection&) const = default;
    };
    explicit TextInput(std::wstring name, TextInputPurpose purpose = TextInputPurpose::normal);
    TextInputPurpose purpose() const { return purpose_; }
    Size measure(Size available) override;
    float caption_extent() const;
    float caption_height() const;
    Insets field_insets(Insets fallback) const;
    Size shortcut_size() const;
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
    const std::function<void(const std::wstring&)>& change_callback() const { return change_; }
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
    void set_shortcut_hint(std::wstring value);
    void on_submit(std::function<void()> callback) { submit_ = std::move(callback); }
    const std::function<void()>& submit_callback() const { return submit_; }
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
protected:
    std::optional<StyleTarget> control_style_target() const override { return StyleTarget::text_input; }
    StyleStateMask control_style_state_bits() const override {
        return (Control::control_style_state_bits() & (style_states::focused | style_states::disabled)) |
            (text_.empty() ? style_states::empty : 0);
    }
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
    TextInputPurpose purpose_{TextInputPurpose::normal};
};

struct TabItem {
    std::uint64_t id{};
    std::wstring title;
    ButtonIcon icon{ButtonIcon::none};
    std::wstring image_path;
    bool operator==(const TabItem&) const = default;
};

// Optional 0xRRGGBB colors. Unset values follow the theme; high contrast ignores overrides.
struct TabColors {
    std::optional<std::uint32_t> row_background, selected_background, selected_text;
    std::optional<std::uint32_t> inactive_background, inactive_text, hover_background, border;
    bool operator==(const TabColors&) const = default;
};

// One strip peer regardless of tab count, plus one retained optional action button.
class TabStrip final : public Control, public Animation {
public:
    explicit TabStrip(std::wstring name = L"Tabs");
    ~TabStrip() override;
    const std::vector<TabItem>& tabs() const { return tabs_; }
    const TabColors& colors() const { return colors_; }
    void set_colors(TabColors colors);
    std::optional<std::uint64_t> selected() const { return selected_; }
    void set_tabs(std::vector<TabItem> tabs, std::optional<std::uint64_t> selected);
    void set_duration(unsigned milliseconds);
    unsigned duration() const { return duration_; }
    bool animating() const override { return scrolling_ || !motion_.empty(); }
    void advance(Clock::time_point now) override;
    void settle() override;
    bool select(std::uint64_t id);
    bool activate_tab(std::uint64_t id);
    void step(int delta);
    void request_close(std::uint64_t id);
    bool prepare_context_menu(std::optional<Point> position);
    std::optional<std::uint64_t> context_tab() const { return context_tab_; }
    std::uint64_t tabs_revision() const { return tabs_revision_; }
    void on_select(std::function<void(std::uint64_t)> callback) { select_ = std::move(callback); }
    // Pointer clicks and Enter/Space activate after selection, including the already-selected tab.
    void on_activate(std::function<void(std::uint64_t)> callback) { activate_ = std::move(callback); }
    void on_close(std::function<void(std::uint64_t)> callback) { close_ = std::move(callback); }
    bool closable() const { return bool(close_); }
    void set_new_tab_button_visible(bool visible);
    bool new_tab_button_visible() const { return new_button_visible_; }
    const std::shared_ptr<Button>& new_tab_button() const { return new_button_; }
    void on_new_tab(std::function<void()> callback) { new_tab_ = std::move(callback); }
    void request_new_tab();
    Rect new_tab_button_bounds() const;
    std::span<const std::shared_ptr<Element>> retained_children() const override { return children_; }
    Rect tab_bounds(std::size_t index) const;
    Rect close_bounds(std::size_t index) const;
    Rect content_bounds() const;
    Rect content_bounds(Rect bounds) const;
    std::optional<std::size_t> hit_test(float x) const;
    std::optional<std::size_t> hit_test(Point point) const;
    std::size_t insertion_index(float x) const;
    void set_drop_indicator(std::optional<std::size_t> index);
    std::optional<std::size_t> drop_indicator() const { return drop_indicator_; }
    void arrange(Rect bounds) override;
private:
    std::optional<StyleTarget> control_style_target() const override { return StyleTarget::tab_strip; }
    void presentation_changed() override { settle(); }
    float tab_viewport_width() const;
    float target_tab_width() const;
    Rect target_tab_bounds(std::size_t index) const;
    Rect motion_tab_bounds(std::size_t index) const;
    bool overflows() const;
    bool tabs_fit() const;
    void arrange_new_button();
    void reveal_selected();
    double presented_first() const;
    void start_scroll(double from);
    std::vector<TabItem> tabs_;
    struct TabMotion {
        std::uint64_t id;
        Rect from, to;
    };
    std::vector<TabMotion> motion_;
    unsigned duration_{};
    float motion_progress_{1};
    bool motion_crossing_{};
    bool scrolling_{};
    double scroll_from_{};
    float new_button_from_{}, new_button_to_{};
    Clock::time_point motion_started_{};
    TabColors colors_;
    std::optional<std::uint64_t> selected_;
    std::size_t first_{};
    std::optional<std::size_t> drop_indicator_;
    std::optional<std::uint64_t> context_tab_;
    std::uint64_t tabs_revision_{};
    std::function<void(std::uint64_t)> select_, close_, activate_;
    std::shared_ptr<Button> new_button_;
    std::vector<std::shared_ptr<Element>> children_;
    std::function<void()> new_tab_;
    bool new_button_visible_{};
};

// A clipped retained subtree. It adds no renderer or independent message loop.
class ContentView final : public Control {
public:
    explicit ContentView(std::shared_ptr<Element> content, std::wstring name = L"Pane");
    const std::shared_ptr<Element>& content() const { return content_; }
    void arrange(Rect bounds) override;
    Size measure(Size available) override;
    Rect content_bounds() const;
protected:
    std::optional<StyleTarget> control_style_target() const override { return StyleTarget::content_view; }
    StyleStateMask control_style_state_bits() const override { return Control::control_style_state_bits() & style_states::disabled; }
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
    Size measure(Size available) override;
    void arrange(Rect bounds) override;
protected:
    std::optional<StyleTarget> control_style_target() const override { return StyleTarget::page_view; }
private:
    std::size_t selected_{};
};

class SplitView final : public Control, public Animation {
public:
    SplitView(std::shared_ptr<Element> first, std::shared_ptr<Element> second,
        std::wstring name = L"Pane divider");
    const std::shared_ptr<ContentView>& first() const { return first_; }
    const std::shared_ptr<ContentView>& second() const { return second_; }
    void arrange(Rect bounds) override;
    void set_ratio(float ratio);
    float ratio() const { return ratio_; }
    void set_layout(Axis axis, float minimum_extent);
    Axis axis() const { return axis_; }
    float minimum_pane_extent() const { return minimum_extent_; }
    void on_ratio_changed(std::function<void(float)> callback) { ratio_callback_ = std::move(callback); }
    void set_secondary_visible(bool visible);
    bool secondary_visible() const { return secondary_visible_; }
    void set_transition_duration(unsigned milliseconds);
    unsigned transition_duration() const { return transition_duration_; }
    bool animating() const override { return animating_; }
    float progress() const { return progress_; }
    void advance(Clock::time_point now) override;
    void settle() override;
    void set_primary_visible(bool visible);
    bool primary_visible() const { return primary_visible_; }
    bool expanded() const;
    void on_expanded(std::function<void(bool)> callback) { expanded_callback_ = std::move(callback); }
    Rect divider() const;
    Rect pane_area() const;
    Rect first_pane_area() const;
    Rect second_pane_area() const;
    float effective_divider_width() const;
    void set_style_dragging(bool dragging);
    bool divider_dragging() const { return style_dragging_; }
    static constexpr float divider_width = 10;
    static constexpr float minimum_pane_width = 300;
protected:
    std::optional<StyleTarget> control_style_target() const override { return StyleTarget::split_view; }
    StyleStateMask control_style_state_bits() const override;
    void presentation_changed() override { if (ratio_animating_) settle(); }
private:
    float pane_extent() const;
    float presented_first_extent() const;
    std::shared_ptr<ContentView> first_, second_;
    float ratio_{0.5f};
    Axis axis_{Axis::horizontal};
    float minimum_extent_{minimum_pane_width};
    bool primary_visible_{true}, secondary_visible_{true};
    bool arranged_expanded_{};
    bool style_dragging_{};
    std::function<void(bool)> expanded_callback_;
    unsigned transition_duration_{};
    bool animating_{};
    float progress_{1}, start_{1};
    Clock::time_point started_{};
    bool ratio_animating_{};
    float ratio_start_{}, ratio_target_{}, ratio_presented_{};
    Size ratio_viewport_{};
    std::function<void(float)> ratio_callback_;
};

// Returns null when there are no enabled focus targets. Traversal wraps.
Control* next_focus(std::span<Control* const> controls, const Control* current, bool reverse);

}
