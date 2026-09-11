#include "xui/controls.hpp"
#include <algorithm>

namespace xui {

Control::Control(ControlRole role, std::wstring name, Size preferred)
    : role_(role), name_(std::move(name)) { set_preferred_size(preferred); }

void Control::set_name(std::wstring name) {
    if (name_ == name) return;
    name_ = std::move(name);
    invalidate(Invalidation::paint);
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
}
void Control::pointer_move(bool inside) {
    inside = inside && enabled_;
    if (hovered_ == inside) return;
    hovered_ = inside;
    invalidate(Invalidation::paint);
}
bool Control::pointer_down() {
    if (!enabled_ || !hovered_ || (role_ != ControlRole::button && role_ != ControlRole::toggle))
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
    if (key == ActivationKey::enter) return role_ == ControlRole::button && invoke();
    if (role_ != ControlRole::button && role_ != ControlRole::toggle) return false;
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
    if (!enabled_ || (role_ != ControlRole::button && role_ != ControlRole::toggle)) return false;
    activate();
    return true;
}
void Button::activate() {
    const auto callback = click_;
    if (callback) callback();
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
    const auto nul = text.find(L'\0');
    if (nul != std::wstring::npos) text.resize(nul);
    if (text.size() > 1024) {
        text.resize(1024);
        if constexpr (sizeof(wchar_t) == 2) {
            if (text.back() >= 0xd800 && text.back() <= 0xdbff) text.pop_back();
        }
    }
    if (text_ == text) return;
    text_ = std::move(text);
    invalidate(Invalidation::paint);
}
void TextInput::commit_text(std::wstring text) {
    if (text_ == text) return;
    const auto previous = text_;
    set_text(std::move(text));
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
