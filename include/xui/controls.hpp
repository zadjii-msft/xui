#pragma once

#include "xui/core.hpp"
#include <span>

namespace xui {

enum class ControlRole { label, button, toggle, text_input, file_list };
enum class ActivationKey { space, enter };
enum class TextTone { normal, secondary, accent, error };
struct MenuItem {
    std::wstring text;
    std::function<void()> action;
    bool enabled{true}, checked{}, separator{};
};

// Control state and actions are independent of Windows and the renderer.
class Control : public Element {
public:
    ControlRole role() const { return role_; }
    const std::wstring& name() const { return name_; }
    const std::wstring& automation_id() const { return automation_id_; }
    void set_automation_id(std::wstring value) { automation_id_ = std::move(value); invalidate(Invalidation::paint); }
    void set_name(std::wstring name);
    bool enabled() const { return enabled_; }
    void set_enabled(bool enabled);
    bool focused() const { return focused_; }
    // Backend state transition. Applications request focus through Window::focus.
    void set_focused(bool focused);
    bool hovered() const { return hovered_; }
    bool pressed() const { return pointer_ ? hovered_ : keyboard_; }
    bool captured() const { return pointer_; }
    bool focusable() const { return enabled_ && role_ != ControlRole::label; }
    // Input adapter boundary. Applications normally use callbacks and property setters.
    void pointer_move(bool inside);
    bool pointer_down();
    bool pointer_up(bool inside);
    void cancel();
    bool key_down(ActivationKey key, bool repeat = false);
    bool key_up(ActivationKey key);
    bool invoke();
    void on_context_menu(std::function<std::vector<MenuItem>()> callback) { menu_ = std::move(callback); }
    std::vector<MenuItem> context_menu() const { return menu_ ? menu_() : std::vector<MenuItem>{}; }
protected:
    Control(ControlRole role, std::wstring name, Size preferred);
    virtual void activate() {}
private:
    ControlRole role_;
    std::wstring name_;
    std::wstring automation_id_;
    bool enabled_{true}, focused_{}, hovered_{}, pointer_{}, keyboard_{};
    std::function<std::vector<MenuItem>()> menu_;
};

class Label final : public Control {
public:
    explicit Label(std::wstring text) : Control(ControlRole::label, std::move(text), {320, 28}) {}
    void set_text(std::wstring text) { set_name(std::move(text)); }
    const std::wstring& text() const { return name(); }
    TextTone tone() const { return tone_; }
    void set_tone(TextTone value) { tone_ = value; invalidate(Invalidation::paint); }
    bool caption() const { return caption_; }
    void set_caption(bool value) { caption_ = value; invalidate(Invalidation::paint); }
    bool heading() const { return heading_; }
    void set_heading(bool heading) {
        if (heading_ == heading) return;
        heading_ = heading;
        invalidate(Invalidation::paint);
    }
private:
    bool heading_{};
    bool caption_{};
    TextTone tone_{};
};

class Button final : public Control {
public:
    explicit Button(std::wstring text) : Control(ControlRole::button, std::move(text), {240, 40}) {}
    void on_click(std::function<void()> callback) { click_ = std::move(callback); }
private:
    void activate() override;
    std::function<void()> click_;
};

class Toggle final : public Control {
public:
    explicit Toggle(std::wstring text) : Control(ControlRole::toggle, std::move(text), {320, 36}) {}
    bool checked() const { return checked_; }
    // Property updates do not invoke the application callback.
    void set_checked(bool checked);
    void on_change(std::function<void(bool)> callback) { change_ = std::move(callback); }
private:
    void activate() override;
    bool checked_{};
    std::function<void(bool)> change_;
};

class TextInput final : public Control {
public:
    explicit TextInput(std::wstring name) : Control(ControlRole::text_input, std::move(name), {320, 68}) {}
    const std::wstring& text() const { return text_; }
    void set_text(std::wstring text);
    void on_change(std::function<void(const std::wstring&)> callback) { change_ = std::move(callback); }
    // Backend boundary: publish committed native text, not IME preedit text.
    void commit_text(std::wstring text);
    void set_search_style(bool value) { search_ = value; invalidate(Invalidation::layout); }
    bool search_style() const { return search_; }
    void set_placeholder(std::wstring value) { placeholder_ = std::move(value); invalidate(Invalidation::paint); }
    const std::wstring& placeholder() const { return placeholder_; }
    const std::wstring& shortcut_hint() const { return shortcut_; }
    void set_shortcut_hint(std::wstring value) { shortcut_ = std::move(value); invalidate(Invalidation::paint); }
    void on_submit(std::function<void()> callback) { submit_ = std::move(callback); }
    void submit() { if (submit_) { auto callback = submit_; callback(); } }
private:
    std::wstring text_;
    std::wstring placeholder_;
    std::wstring shortcut_;
    bool search_{};
    std::function<void()> submit_;
    std::function<void(const std::wstring&)> change_;
};

// Returns null when there are no enabled focus targets. Traversal wraps.
Control* next_focus(std::span<Control* const> controls, const Control* current, bool reverse);

}
