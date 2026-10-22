#include "basic_host.hpp"
#include "context_menu.hpp"
#include "xui/commands.hpp"
#include "xui/titlebar.hpp"
#include "xui/navigation.hpp"
#include "xui/miller_columns.hpp"
#include "xui/map_view.hpp"
#include "xui/reveal.hpp"
#include "xui/data_grid.hpp"
#include <windowsx.h>
#include <cmath>
#include <utility>

namespace xui {
namespace { constexpr UINT_PTR repeat_timer = 42; }

BasicHostPeer::BasicHostPeer(ControlHostServices& owner, std::shared_ptr<Control> model)
    : ControlHostPeer(owner, HostFamily::basic, std::move(model)) {}
BasicHostPeer::~BasicHostPeer() { detach_owner(); }
BasicHostPeer* basic_peer(HostPeer& peer) {
    return peer.family == HostFamily::basic ? static_cast<BasicHostPeer*>(&peer) : nullptr;
}
const BasicHostPeer* basic_peer(const HostPeer& peer) {
    return peer.family == HostFamily::basic ? static_cast<const BasicHostPeer*>(&peer) : nullptr;
}
void BasicHostPeer::reset_text_layout() { text_layout.Reset(); }
void BasicHostPeer::detach_owner() {
    control->set_text_measurer({});
    if (auto* label = dynamic_cast<Label*>(control.get())) label->set_wrapped_text_measurer({});
    if (auto* combo = dynamic_cast<ComboBox*>(control.get())) combo->choices()->on_accept({});
}
void BasicHostPeer::cancel_input() {
    if (auto* range = dynamic_cast<RangeInput*>(control.get())) range->cancel_drag();
    pressed_choice.reset();
    hovered_choice.reset();
    KillTimer(window, repeat_timer);
    repeating = repeat_cycle = false;
    ControlHostPeer::cancel_input();
}
bool BasicHostPeer::capture_active() const {
    const auto* range = dynamic_cast<const RangeInput*>(control.get());
    return pressed_choice || (range && range->dragging());
}
void BasicHostPeer::reconcile_input() {
    if (repeating && (!host.input_enabled(*this) || !host.input_visible(*this) ||
        !control->pressed() || !IsWindowVisible(services.owner_window()))) stop_repeat();
}
void BasicHostPeer::stop_repeat() { KillTimer(window, repeat_timer); repeating = false; }
void BasicHostPeer::start_repeat(bool activate) {
    auto* button = dynamic_cast<Button*>(control.get());
    if (!button || button->behavior() != ButtonBehavior::repeat ||
        !host.input_enabled(*this) || !host.input_visible(*this)) return;
    repeat_cycle = true;
    if (!repeating) {
        win32_require(SetTimer(window, repeat_timer, button->repeat_delay(), nullptr) != 0, "Start button repeat");
        repeating = true;
    }
    if (activate) services.activate_control(*this, button->invoke());
}
void BasicHostPeer::open_combo() {
    auto combo = std::dynamic_pointer_cast<ComboBox>(control);
    if (!combo || !host.input_enabled(*this)) return;
    if (combo->popup()->is_open()) {
        services.dismiss_host_popup(*combo->popup(), PopupDismissReason::cancel);
        return;
    }
    services.collect_popup_surface(combo->popup());
    combo->prepare_popup();
    const std::weak_ptr<ComboBox> weak = combo;
    const std::weak_ptr<ControlHostServices> owner =
        std::static_pointer_cast<ControlHostServices>(host.acquire_owner_lease());
    combo->choices()->on_accept([owner, weak](std::uint64_t id) {
        const auto value = weak.lock();
        const auto host = owner.lock();
        if (!host || !value || !value->popup()->is_open()) return;
        const auto popup = value->popup();
        host->dismiss_host_popup(*popup, PopupDismissReason::commit);
        if (host->owner_open()) value->select(id);
    });
    services.show_host_popup(combo->popup(), *combo, combo->choices().get());
}

std::optional<LRESULT> BasicHostPeer::input(UINT message, WPARAM wparam, LPARAM lparam,
    const HostInputContext& context) {
    const auto inside = [&] { return row_pointer_inside(window, lparam); };
    const auto choice_hit = [&](const RadioGroup& choices) {
        auto hit = choices.hit_test({GET_X_LPARAM(lparam) * 96.0f / context.dpi,
            GET_Y_LPARAM(lparam) * 96.0f / context.dpi});
        if (hit && (context.palette.style == VisualStyle::winui || choices.has_control_styling())) {
            const auto body = choices.item_bounds(*hit);
            const float x = GET_X_LPARAM(lparam) * 96.0f / context.dpi;
            if (!inside() || x < body.x || x >= body.x + body.width) hit.reset();
        }
        return hit;
    };
    const auto fraction = [&](const RangeInput& range) {
        return range.slider_geometry().pointer_fraction(
            {GET_X_LPARAM(lparam) * 96.0f / context.dpi, GET_Y_LPARAM(lparam) * 96.0f / context.dpi},
            range.orientation());
    };
    switch (message) {
    case WM_TIMER:
        if (wparam == repeat_timer) {
            auto* button = dynamic_cast<Button*>(control.get());
            if (!button || !repeating || !host.input_enabled(*this) || !host.input_visible(*this) ||
                !control->pressed() || !IsWindowVisible(services.owner_window())) {
                KillTimer(window, repeat_timer);
                repeating = false;
                return 0;
            }
            win32_require(SetTimer(window, repeat_timer, button->repeat_interval(), nullptr) != 0, "Continue button repeat");
            services.activate_control(*this, button->invoke());
            return 0;
        }
        break;
    case WM_SETCURSOR:
        if (dynamic_cast<HyperlinkButton*>(control.get()) && host.input_enabled(*this) && LOWORD(lparam) == HTCLIENT) {
            SetCursor(LoadCursorW(nullptr, IDC_HAND));
            return TRUE;
        }
        break;
    case WM_MOUSEMOVE:
        if (auto* choices = dynamic_cast<RadioGroup*>(control.get())) {
            const auto hit = inside() ? choice_hit(*choices) : std::nullopt;
            if (hovered_choice != hit) { hovered_choice = hit; host.invalidate_peer_paint(); }
        }
        if (auto* range = dynamic_cast<RangeInput*>(control.get()); range && range->dragging()) {
            range->drag(fraction(*range));
            return 0;
        }
        control->pointer_move(inside());
        if (control->captured() && control->hovered() && repeat_cycle && !repeating) start_repeat(false);
        track_pointer_leave();
        return 0;
    case WM_MOUSELEAVE:
        if (hovered_choice) { hovered_choice.reset(); host.invalidate_peer_paint(); }
        break;
    case WM_LBUTTONDOWN:
        if (auto* choices = dynamic_cast<RadioGroup*>(control.get())) {
            SetFocus(window);
            const auto index = choice_hit(*choices);
            if (index && choices->items()[*index].enabled) {
                if (context.palette.style == VisualStyle::winui) {
                    hovered_choice = index;
                    pressed_choice = choices->items()[*index].id;
                    SetCapture(window);
                    host.invalidate_peer_paint();
                    return 0;
                }
                choices->select(choices->items()[*index].id);
                if (host.owner_open() && control->role() == ControlRole::choice_list && host.input_visible(*this))
                    choices->accept();
            }
            return 0;
        }
        if (auto* range = dynamic_cast<RangeInput*>(control.get())) {
            SetFocus(window);
            SetCapture(window);
            range->begin_drag(fraction(*range));
            return 0;
        }
        break;
    case WM_LBUTTONUP:
        if (std::exchange(suppress_popup_click, false)) return 0;
        if (auto* choices = dynamic_cast<RadioGroup*>(control.get()); choices && pressed_choice) {
            const auto pressed = std::exchange(pressed_choice, std::nullopt);
            const auto hit = inside() ? choice_hit(*choices) : std::nullopt;
            if (GetCapture() == window) ReleaseCapture();
            host.invalidate_peer_paint();
            if (host.input_enabled(*this) && hit && pressed == choices->items()[*hit].id && choices->items()[*hit].enabled) {
                choices->select(choices->items()[*hit].id);
                if (host.owner_open() && control->role() == ControlRole::choice_list && host.input_visible(*this))
                    choices->accept();
            }
            return 0;
        }
        if (auto* range = dynamic_cast<RangeInput*>(control.get()); range && range->dragging()) {
            range->commit_drag();
            if (GetCapture() == window) ReleaseCapture();
            return 0;
        }
        if (repeat_cycle) {
            KillTimer(window, repeat_timer);
            repeating = repeat_cycle = false;
            control->cancel();
            if (GetCapture() == window) ReleaseCapture();
            return 0;
        }
        break;
    case WM_KEYDOWN:
        if (auto* range = dynamic_cast<RangeInput*>(control.get())) {
            if (wparam == VK_ESCAPE) {
                range->cancel_drag();
                if (GetCapture() == window) ReleaseCapture();
                return 0;
            }
            std::optional<RangeKey> key;
            if (wparam == VK_HOME) key = RangeKey::minimum;
            if (wparam == VK_END) key = RangeKey::maximum;
            if (wparam == VK_PRIOR) key = RangeKey::page_increase;
            if (wparam == VK_NEXT) key = RangeKey::page_decrease;
            if (wparam == VK_LEFT || wparam == VK_DOWN) key = range->reversed() ? RangeKey::increase : RangeKey::decrease;
            if (wparam == VK_RIGHT || wparam == VK_UP) key = range->reversed() ? RangeKey::decrease : RangeKey::increase;
            if (key) { range->move(*key); return 0; }
        }
        if (auto* choices = dynamic_cast<RadioGroup*>(control.get())) {
            if (wparam == VK_UP || wparam == VK_LEFT || wparam == VK_DOWN || wparam == VK_RIGHT) {
                choices->step(wparam == VK_UP || wparam == VK_LEFT ? -1 : 1);
                return 0;
            }
            if (wparam == VK_HOME || wparam == VK_END) {
                const auto& items = choices->items();
                for (std::size_t i = 0; i < items.size(); ++i) {
                    const auto& item = items[wparam == VK_HOME ? i : items.size() - i - 1];
                    if (item.enabled) { choices->select(item.id); break; }
                }
                return 0;
            }
            if ((wparam == VK_RETURN || wparam == VK_SPACE) && !(lparam & (1LL << 30))) {
                choices->accept();
                return 0;
            }
        }
        if (auto* number = dynamic_cast<NumericInput*>(control.get()); number && (wparam == VK_UP || wparam == VK_DOWN)) {
            number->step(wparam == VK_UP ? 1 : -1);
            return 0;
        }
        if (dynamic_cast<ComboBox*>(control.get()) && (wparam == VK_F4 || wparam == VK_DOWN || wparam == VK_UP)) {
            open_combo();
            return 0;
        }
        break;
    case WM_KEYUP:
        if (wparam == VK_SPACE && repeat_cycle) {
            KillTimer(window, repeat_timer);
            repeating = repeat_cycle = false;
            control->cancel();
            return 0;
        }
        break;
    case WM_CHAR:
        if (wparam >= 32 && wparam != 127) {
            auto* choices = dynamic_cast<RadioGroup*>(control.get());
            auto* combo = dynamic_cast<ComboBox*>(control.get());
            if (combo) choices = combo->choices().get();
            if (choices && host.input_enabled(*this)) {
                const auto now = GetTickCount64();
                if (now - typeahead_time > 1000 || typeahead.size() >= 128) typeahead.clear();
                typeahead_time = now;
                typeahead += static_cast<wchar_t>(wparam);
                if (!choices->type_ahead(typeahead)) {
                    typeahead.assign(1, static_cast<wchar_t>(wparam));
                    choices->type_ahead(typeahead);
                }
                if (combo && choices->selected()) combo->select(*choices->selected());
                return 0;
            }
        }
        break;
    }
    return ControlHostPeer::input(message, wparam, lparam, context);
}

std::optional<LRESULT> BasicHostPeer::accessibility_action(LPARAM action) {
    RadioGroup* choices = dynamic_cast<RadioGroup*>(control.get());
    auto* combo = dynamic_cast<ComboBox*>(control.get());
    if (combo) choices = combo->choices().get();
    if (!choices) return {};
    if (action < 100) return UIA_E_INVALIDOPERATION;
    const auto id = static_cast<std::uint64_t>(action - 100);
    const auto& items = choices->items();
    const auto it = std::find_if(items.begin(), items.end(), [id](const auto& item) { return item.id == id; });
    if (it == items.end()) return UIA_E_ELEMENTNOTAVAILABLE;
    if (!it->enabled) return UIA_E_ELEMENTNOTENABLED;
    if (combo) combo->select(id);
    else choices->select(id);
    host.update_peer_state();
    return S_OK;
}

HRESULT BasicHostPeer::foundation_action(const FoundationAction& action) {
    if (auto* combo = dynamic_cast<ComboBox*>(control.get()); combo &&
        (action.kind == FoundationAction::expand || action.kind == FoundationAction::collapse)) {
        if (combo->popup()->is_open() != (action.kind == FoundationAction::expand)) open_combo();
        return S_OK;
    }
    if (dynamic_cast<MenuBar::Heading*>(control.get()) &&
        (action.kind == FoundationAction::expand || action.kind == FoundationAction::collapse))
        return services.expand_menu_heading(*this, action.kind == FoundationAction::expand) ? S_OK : UIA_E_INVALIDOPERATION;
    if (action.kind == FoundationAction::expand || action.kind == FoundationAction::collapse)
        return UIA_E_INVALIDOPERATION;
    if (auto* number = dynamic_cast<NumericInput*>(control.get())) {
        if (action.kind == FoundationAction::text) {
            if (!number->commit_text(action.string)) { host.update_peer_state(); return E_INVALIDARG; }
        } else {
            if (!std::isfinite(action.number) || action.number < number->range().minimum ||
                action.number > number->range().maximum) return E_INVALIDARG;
            number->change_value(action.number);
        }
        return S_OK;
    }
    if (auto* range = dynamic_cast<RangeInput*>(control.get()); range && action.kind == FoundationAction::value) {
        if (!std::isfinite(action.number) || action.number < range->range().minimum ||
            action.number > range->range().maximum) return E_INVALIDARG;
        range->change_value(action.number);
        return S_OK;
    }
    return UIA_E_INVALIDOPERATION;
}

namespace {
D2D1_COLOR_F argb_color(uint32_t value) {
    return D2D1::ColorF(value & 0xffffff, static_cast<float>(value >> 24) / 255);
}
}

void BasicHostPeer::paint(const HostPaintContext& context) {
    auto& peer = *this;
    auto& canvas = context.canvas;
    const auto& palette = context.palette;
    auto& control = *peer.control;
    const auto bounds = peer.paint_bounds;
    const auto presentation = services.control_presentation(peer);
    const auto foundation_text = context.enabled ? palette.text : palette.disabled;
    const bool fluent = palette.style == VisualStyle::winui;
    const bool focus_visible = context.focus_visible;
    const auto enabled = [&](const HostPeer& value) { return host.input_enabled(value); };
    const auto visible = [&](const HostPeer& value) { return host.input_visible(value); };
    const auto input_fill = [&](const HostPeer& value) { return host_input_fill(value, palette, host.input_enabled(value)); };
    const auto status_fill = [&](StatusSeverity value) { return host_status_fill(value, palette); };
    const bool progress_animating = presentation.progress_animating;
    const float progress_phase = presentation.progress_phase;
    const auto animated_progress = [&](const HostPeer&) { return presentation.progress_animating; };
    const auto is_caption_button = [&](const Control& value) { return services.caption_control(value); };
    const bool caption_active = presentation.caption_active;
    const auto* titlebar = presentation.titlebar;

    if (auto* number = dynamic_cast<NumericInput*>(&control)) {
        if (number->has_control_styling()) {
            const PartStyleValues empty;
            const auto* root_values = number->effective_control_style_values(StylePart::root);
            const auto* field_values = number->effective_control_style_values(StylePart::field);
            const auto* header = number->effective_control_style_values(StylePart::header);
            canvas.styled_surface({0, 0, bounds.width, bounds.height}, palette, root_values ? *root_values : empty,
                D2D1::ColorF(0, 0.0f), palette.border, 0, {});
            auto frame = number->field_bounds();
            frame.x -= control.bounds().x; frame.y -= control.bounds().y;
            canvas.styled_surface(frame, palette, field_values ? *field_values : empty, input_fill(peer),
                !number->valid() ? palette.error : palette.border, fluent ? 4.0f : 6.0f, {1, 1, 1, 1});
            if (header) {
                auto rect = number->header_bounds();
                rect.x -= control.bounds().x; rect.y -= control.bounds().y;
                const auto ink = !palette.high_contrast && header->foreground ?
                    D2D1::ColorF(header->foreground->resolve(palette.mode)) : foundation_text;
                canvas.styled_text(control.name(), rect, ink, *header, TextStyle::caption);
            }
            if (IsChild(peer.window, GetFocus())) canvas.styled_field_focus(frame, palette, field_values ? *field_values : empty);
            return;
        }
        if (fluent) canvas.field_frame({0.5f, 0.5f, std::max(0.0f, bounds.width - 1), std::max(0.0f, bounds.height - 1)},
            palette, IsChild(peer.window, GetFocus()) != FALSE, enabled(peer), !number->valid(), input_fill(peer));
        return;
    }
    if (auto* status = dynamic_cast<InlineStatus*>(&control)) {
        const PartStyleValues* host_message = nullptr;
        if (const auto* host = peer.parent ? dynamic_cast<const RuntimeHost*>(peer.parent->control.get()) : nullptr;
            host && host->has_control_styling())
            host_message = host->effective_control_style_values(
                host->state() == HostState::error ? StylePart::error : StylePart::status);
        if (host_message && host_message->empty()) host_message = nullptr;
        auto ink = status->severity() == StatusSeverity::error ? palette.error :
            status->severity() == StatusSeverity::warning ? palette.folder : palette.accent;
        if (fluent && !palette.high_contrast) {
            const auto colors = winui_status_colors(palette.mode);
            if (status->severity() == StatusSeverity::success) ink = D2D1::ColorF(colors.success);
            else if (status->severity() == StatusSeverity::warning) ink = D2D1::ColorF(colors.warning);
        }
        if (!enabled(peer)) ink = palette.disabled;
        if (status->has_control_styling()) {
            const PartStyleValues empty;
            const auto part = [&](StylePart value) -> const PartStyleValues& {
                const auto* values = status->effective_control_style_values(value);
                return values ? *values : empty;
            };
            const auto ink_for = [&](const PartStyleValues& values, D2D1_COLOR_F fallback) {
                return !palette.high_contrast && values.foreground ?
                    D2D1::ColorF(values.foreground->resolve(palette.mode)) : fallback;
            };
            canvas.styled_surface({0, 0, bounds.width, bounds.height}, palette, part(StylePart::root),
                status_fill(status->severity()), palette.border, fluent ? 4.0f : 6.0f, fluent ? Insets{1, 1, 1, 1} : Insets{});
            const auto content = status->content_bounds();
            const auto& stripe = part(StylePart::stripe);
            const float stripe_width = stripe.size.value_or(fluent ? 0.0f : 4.0f);
            canvas.styled_surface({content.x, content.y, stripe_width, content.height}, palette, stripe, ink, ink, 2, {});
            const auto& icon = part(StylePart::icon);
            const float icon_size = icon.size.value_or(24);
            const Rect icon_rect{content.x + stripe_width + 8, content.y + 6, icon_size,
                std::max(0.0f, std::min(38.0f, content.height - 12))};
            const Symbol symbols[]{Symbol::information, Symbol::success, Symbol::warning, Symbol::error};
            canvas.symbol(symbols[static_cast<int>(status->severity())], icon_rect, ink_for(icon, ink), icon_size);
            float end = content.x + content.width - 8;
            for (const auto& child : status->owned_children()) if (static_cast<Control&>(*child).visible())
                end = std::min(end, child->bounds().x - control.bounds().x);
            const float left = icon_rect.x + icon_size + 6;
            const auto message = host_message ? merge_part_values(*host_message, part(StylePart::message)) :
                part(StylePart::message);
            canvas.styled_text(control.name(), {left, content.y + 6, std::max(0.0f, end - left - 4),
                std::max(0.0f, content.height - 12)}, ink_for(message, foundation_text), message);
            return;
        }
        const Rect frame{1, 1, std::max(0.0f, bounds.width - 2), std::max(0.0f, bounds.height - 2)};
        canvas.rounded(frame, status_fill(status->severity()), fluent ? 4.0f : 6.0f);
        if (fluent) canvas.rounded(frame, palette.border, 4, true);
        else canvas.rounded({2, 2, 4, std::max(0.0f, bounds.height - 4)}, ink, 2);
        const wchar_t* icons[]{L"\u2139", L"\u2713", L"!", L"\u00d7"};
        if (fluent) {
            const Symbol symbols[]{Symbol::information, Symbol::success, Symbol::warning, Symbol::error};
            canvas.symbol(symbols[static_cast<int>(status->severity())], {12, 6, 24, 38}, ink);
        } else canvas.text(icons[static_cast<int>(status->severity())], {12, 6, 24, 38}, ink);
        float end = bounds.width - 8;
        for (const auto& child : status->owned_children()) if (static_cast<Control&>(*child).visible())
            end = std::min(end, child->bounds().x - control.bounds().x);
        const Rect message_bounds{42, 6, std::max(0.0f, end - 46), std::max(0.0f, bounds.height - 12)};
        if (host_message) {
            const auto message_ink = !palette.high_contrast && host_message->foreground ?
                D2D1::ColorF(host_message->foreground->resolve(palette.mode)) : foundation_text;
            canvas.styled_text(control.name(), message_bounds, message_ink, *host_message);
        } else canvas.text(control.name(), message_bounds, foundation_text);
        return;
    }
    if (auto* color = dynamic_cast<ColorPicker*>(&control)) {
        const auto value = color->value();
        const D2D1_COLOR_F rgba{value.red / 255.0f, value.green / 255.0f, value.blue / 255.0f, value.alpha / 255.0f};
        const auto content = color->content_bounds();
        const Rect preview{content.x + 2, content.y + 2, std::max(0.0f, content.width - 4), 42};
        const PartStyleValues empty;
        const auto part = [&](StylePart value) -> const PartStyleValues& {
            const auto* values = color->has_control_styling() ? color->effective_control_style_values(value) : nullptr;
            return values ? *values : empty;
        };
        const auto& checkerboard_light = part(StylePart::checkerboard_light);
        const auto& checkerboard_dark = part(StylePart::checkerboard_dark);
        const auto light = !palette.high_contrast && checkerboard_light.background ?
            D2D1::ColorF(checkerboard_light.background->resolve(palette.mode)) : palette.field;
        const auto dark = !palette.high_contrast && checkerboard_dark.background ?
            D2D1::ColorF(checkerboard_dark.background->resolve(palette.mode)) : palette.border;
        if (color->has_control_styling()) canvas.styled_surface({0, 0, bounds.width, bounds.height}, palette,
            part(StylePart::root), D2D1::ColorF(0, 0.0f), palette.border, 0, {});
        canvas.fill(preview, light);
        for (int y = 0; y < 3; ++y) for (int x = 0; x < static_cast<int>(content.width / 14) + 1; ++x)
            if ((x + y) % 2) canvas.fill({preview.x + x * 14, preview.y + y * 14,
                std::min(14.0f, std::max(0.0f, preview.width - x * 14)), 14}, dark);
        canvas.fill(preview, rgba);
        if (color->has_control_styling()) canvas.styled_surface(preview, palette, part(StylePart::preview),
            D2D1::ColorF(0, 0.0f), fluent ? palette.border : palette.text, fluent ? 4.0f : 0.0f, {1, 1, 1, 1});
        else if (fluent) canvas.rounded(preview, palette.border, 4, true);
        else canvas.outline(preview, palette.text);
        const wchar_t* channels[]{L"Red", L"Green", L"Blue", L"Alpha"};
        for (int i = 0; i < 4; ++i) {
            const auto label = color->has_control_styling() ?
                color->channel_label_style_values(static_cast<std::size_t>(i)) : PartStyleValues{};
            const auto padding = label.padding.value_or(Insets{});
            const auto label_ink = !palette.high_contrast && label.foreground ?
                D2D1::ColorF(label.foreground->resolve(palette.mode)) : foundation_text;
            const Rect rect{content.x + 8 + padding.left, content.y + 52.0f + i * 42 + padding.top,
                std::max(0.0f, 52 - padding.left - padding.right), std::max(0.0f, 38 - padding.top - padding.bottom)};
            if (color->has_control_styling()) canvas.styled_text(channels[i], rect, label_ink, label);
            else canvas.text(channels[i], rect, foundation_text);
        }
        return;
    }
    if (peer.parent) if (auto* picker = dynamic_cast<ColorPicker*>(peer.parent->control.get())) {
        const auto children = picker->owned_children();
        for (std::size_t i = 4; i < children.size() && i - 4 < picker->swatches().size(); ++i) if (children[i].get() == &control) {
            const auto value = picker->swatches()[i - 4];
            const D2D1_COLOR_F rgb{value.red / 255.0f, value.green / 255.0f, value.blue / 255.0f,
                picker->has_control_styling() ? value.alpha / 255.0f : 1};
            if (picker->has_control_styling() && value.alpha < 255) {
                const auto* checkerboard_light = picker->effective_control_style_values(StylePart::checkerboard_light);
                const auto* checkerboard_dark = picker->effective_control_style_values(StylePart::checkerboard_dark);
                const auto light = !palette.high_contrast && checkerboard_light && checkerboard_light->background ?
                    D2D1::ColorF(checkerboard_light->background->resolve(palette.mode)) : palette.field;
                const auto dark = !palette.high_contrast && checkerboard_dark && checkerboard_dark->background ?
                    D2D1::ColorF(checkerboard_dark->background->resolve(palette.mode)) : palette.border;
                canvas.fill({2, 2, std::max(0.0f, bounds.width - 4), std::max(0.0f, bounds.height - 4)}, light);
                for (int y = 0; y * 8 < bounds.height - 4; ++y) for (int x = 0; x * 8 < bounds.width - 4; ++x)
                    if ((x + y) % 2) canvas.fill({2.0f + x * 8, 2.0f + y * 8,
                        std::min(8.0f, std::max(0.0f, bounds.width - 4 - x * 8)),
                        std::min(8.0f, std::max(0.0f, bounds.height - 4 - y * 8))}, dark);
            }
            canvas.rounded({2, 2, std::max(0.0f, bounds.width - 4), bounds.height - 4}, rgb, 4);
            const Rect frame{1, 1, std::max(0.0f, bounds.width - 2), bounds.height - 2};
            if (picker->has_control_styling()) {
                const auto state = (picker->value() == value ? style_states::selected : 0) |
                    (control.focused() ? style_states::focused : 0) |
                    (control.hovered() ? style_states::hovered : 0) |
                    (control.pressed() ? style_states::pressed : 0) |
                    (!enabled(peer) ? style_states::disabled : 0);
                auto swatch = picker->resolve_control_style_part(StylePart::swatch, state);
                const auto& button = static_cast<const Button&>(control);
                if (button.has_control_styling()) {
                    const auto child = button.surface_style_values();
                    if (child.border_brush) swatch.border_brush = child.border_brush;
                    if (child.border_thickness) swatch.border_thickness = child.border_thickness;
                    if (child.corner_radius) swatch.corner_radius = child.corner_radius;
                }
                canvas.styled_surface(frame, palette, swatch, D2D1::ColorF(0, 0.0f),
                    picker->value() == value ? palette.accent : palette.border, 4, {1, 1, 1, 1});
                if (control.focused()) canvas.focus_ring(frame, palette);
            } else if (fluent) {
                canvas.rounded(frame, palette.border, 4, true);
                if (control.focused()) canvas.focus_ring(frame, palette);
            } else canvas.outline(frame, control.focused() ? palette.accent : palette.text);
            return;
        }
    }

    if (auto* choices = dynamic_cast<RadioGroup*>(&control)) {
        if (choices->has_control_styling() || choices->horizontal_presentation()) {
            const PartStyleValues empty;
            const auto* root_values = choices->effective_control_style_values(StylePart::root);
            const bool radio = control.role() == ControlRole::radio_group;
            const bool selector = fluent && !palette.high_contrast && choices->horizontal_presentation();
            canvas.styled_surface({0, 0, bounds.width, bounds.height}, palette, root_values ? *root_values : empty,
                (fluent && radio) || selector ? D2D1::ColorF(0, 0.0f) : palette.surface,
                palette.border, fluent ? 4.0f : 0.0f, {});
            const auto ink_for = [&](const PartStyleValues& values, D2D1_COLOR_F fallback) {
                return !palette.high_contrast && values.foreground ?
                    D2D1::ColorF(values.foreground->resolve(palette.mode)) : fallback;
            };
            for (std::size_t i = 0; i < choices->items().size(); ++i) {
                const auto b = choices->item_bounds(i);
                if (b.height <= 0) continue;
                const auto& item = choices->items()[i];
                const bool selected = choices->selected() == item.id;
                const bool hovered = enabled(peer) && item.enabled && peer.hovered_choice == i;
                const bool pressed = hovered && peer.pressed_choice == item.id;
                const auto row = choices->item_style_values(StylePart::item, i, hovered, pressed);
                auto label = choices->item_style_values(StylePart::label, i, hovered, pressed);
                if (choices->horizontal_presentation() && !label.horizontal_alignment)
                    label.horizontal_alignment = StyleAlignment::center;
                auto ink = !enabled(peer) || !item.enabled ? palette.disabled :
                    selected && !radio ? palette.selection_text : palette.text;
                canvas.styled_surface(b, palette, row, selected && !radio && !selector ? palette.selection :
                    hovered && !radio && !selector ? palette.hover : D2D1::ColorF(0, 0.0f), palette.border, 4, {});
                if (radio) {
                    const auto indicator = choices->item_style_values(StylePart::indicator, i, hovered, pressed);
                    const auto mark = choices->item_style_values(StylePart::mark, i, hovered, pressed);
                    const auto circle = choices->indicator_bounds(i, hovered, pressed);
                    const float diameter = circle.width;
                    if (fluent && indicator.empty() && mark.empty())
                        ink = canvas.radio_indicator({circle.x + 0.5f, circle.y, circle.width, circle.height},
                            palette, selected, enabled(peer) && item.enabled, hovered, pressed);
                    else {
                        canvas.styled_surface(circle, palette, indicator, selected ? palette.accent : palette.field,
                            ink, diameter / 2, {1, 1, 1, 1});
                        if (selected) {
                            const float dot = std::min(diameter, mark.size.value_or(diameter / 2));
                            canvas.rounded({circle.x + (diameter - dot) / 2, circle.y + (diameter - dot) / 2, dot, dot},
                                ink_for(mark, palette.high_contrast ? palette.selection_text : palette.background), dot / 2);
                        }
                    }
                } else if (selected) {
                    const auto marker = choices->item_style_values(StylePart::selected_marker, i, hovered, pressed);
                    const float width = std::min(b.width, marker.size.value_or(3));
                    const float extent = fluent && !palette.high_contrast ? std::min(16.0f, b.width) : b.width / 2;
                    const auto marker_bounds = choices->horizontal_presentation() ?
                        Rect{b.x + (b.width - extent) / 2, b.y + std::max(0.0f, b.height - width), extent, std::min(width, b.height)} :
                        Rect{b.x, b.y + b.height / 4, width, b.height / 2};
                    canvas.styled_surface(marker_bounds, palette, marker,
                        palette.accent, palette.border, width / 2, {});
                }
                canvas.styled_text(item.text, choices->label_bounds(i, hovered, pressed), ink_for(label, ink), label);
                if (selected && focus_visible) canvas.focus_ring(b, palette);
            }
            return;
        }
        if (!fluent)
            canvas.fill({0, 0, bounds.width, bounds.height}, palette.surface);
        else if (control.role() != ControlRole::radio_group &&
            (!peer.parent || peer.parent->control->role() != ControlRole::popup))
            canvas.rounded({0, 0, bounds.width, bounds.height}, palette.surface, 4);
        for (std::size_t i = 0; i < choices->items().size(); ++i) {
            const auto b = choices->item_bounds(i);
            if (b.height <= 0) continue;
            const auto& item = choices->items()[i];
            const bool selected = choices->selected() == item.id;
            const bool radio = control.role() == ControlRole::radio_group;
            const bool hovered = enabled(peer) && item.enabled && peer.hovered_choice == i;
            const bool pressed = hovered && peer.pressed_choice == item.id;
            const Rect face = fluent && !radio ? b : Rect{1, b.y + 1,
                std::max(0.0f, b.width - 2), std::max(0.0f, b.height - 2)};
            if (selected && !radio) {
                canvas.rounded(fluent ? face : b, palette.selection, fluent ? 4.0f : 3.0f);
                if (fluent) {
                    const float pill = pressed ? 10.0f : 16.0f;
                    canvas.rounded({b.x, b.y + (b.height - pill) / 2, 3, pill}, palette.accent, 1.5f);
                }
            }
            else if (fluent && hovered && !radio) canvas.rounded(face, palette.hover, 4);
            auto ink = !enabled(peer) || !item.enabled ? palette.disabled :
                selected && !radio ? palette.selection_text : palette.text;
            if (radio) {
                if (fluent) ink = canvas.radio_indicator({0.5f, b.y + (b.height - 19) / 2, 19, 19}, palette, selected,
                    enabled(peer) && item.enabled, hovered, pressed);
                else {
                    canvas.rounded({10, b.y + 9, 16, 16}, ink, 8, true);
                    if (selected) canvas.rounded({14, b.y + 13, 8, 8}, ink, 4);
                }
            }
            const float text_left = radio ? (fluent ? 28.0f : 36.0f) : fluent ? b.x + 11 : 12.0f;
            canvas.text(item.text, {text_left, b.y + (fluent && !radio ? 5 : 0),
                std::max(0.0f, b.x + b.width - text_left - (fluent && !radio ? 11 : 12)),
                std::max(0.0f, b.height - (fluent && !radio ? 12 : 0))}, ink);
            if (selected && focus_visible) {
                if (fluent) canvas.focus_ring(face, palette);
                else canvas.outline(face, palette.accent);
            }
        }
        return;
    }
    if (auto* combo = dynamic_cast<ComboBox*>(&control)) {
        const bool template_focus = fluent && !palette.high_contrast && !combo->editor();
        if (combo->has_control_styling()) {
            const PartStyleValues empty;
            const auto part = [&](StylePart value) -> const PartStyleValues& {
                const auto* values = combo->effective_control_style_values(value);
                return values ? *values : empty;
            };
            const auto ink_for = [&](const PartStyleValues& values) {
                return !palette.high_contrast && values.foreground ?
                    D2D1::ColorF(values.foreground->resolve(palette.mode)) : foundation_text;
            };
            const auto local = [&](Rect rect) {
                rect.x -= control.bounds().x; rect.y -= control.bounds().y; return rect;
            };
            canvas.styled_surface({0, 0, bounds.width, bounds.height}, palette, part(StylePart::root),
                D2D1::ColorF(0, 0.0f), palette.border, 0, {});
            const auto frame = local(combo->field_bounds());
            canvas.styled_surface(frame, palette, part(StylePart::field), input_fill(peer), palette.border,
                fluent ? 4.0f : 6.0f, {1, 1, 1, 1});
            const auto& header = part(StylePart::header);
            if (const auto rect = local(combo->header_bounds()); rect.height > 0)
                canvas.styled_text(control.name(), rect, ink_for(header), header, TextStyle::caption);
            if (!combo->editor()) {
                auto text = local(combo->editor_bounds());
                const float pad = std::min(12.0f, text.width / 2);
                text.x += pad; text.width -= 2 * pad;
                const auto& values = part(StylePart::text);
                canvas.styled_text(combo->selected_text(), text, ink_for(values), values);
            }
            const auto arrow = local(combo->drop_down_bounds());
            if (fluent) canvas.chevron(arrow, ink_for(part(StylePart::arrow)), true);
            else canvas.text(L"\u25be", arrow, ink_for(part(StylePart::arrow)));
            if (!template_focus && (focus_visible || IsChild(peer.window, GetFocus())))
                canvas.styled_field_focus(frame, palette, part(StylePart::field));
            return;
        }
        const float edge = fluent ? 0.5f : 1.0f;
        const Rect face{edge, edge, std::max(0.0f, bounds.width - 2 * edge), std::max(0.0f, bounds.height - 2 * edge)};
        if (fluent) {
            if (combo->editor()) canvas.field_frame(face, palette, IsChild(peer.window, GetFocus()) != FALSE,
                enabled(peer), false, input_fill(peer));
            else canvas.button_face(face, palette, ButtonAppearance::standard, enabled(peer),
                control.hovered(), combo->popup()->is_open(), false);
            if (focus_visible && !template_focus) canvas.focus_ring(face, palette);
        } else {
            canvas.rounded(face, palette.field);
            canvas.rounded(face, control.focused() ? palette.accent : palette.border, 6, true);
        }
        if (!combo->editor()) canvas.text(combo->selected_text(),
            {12, fluent ? 5.0f : 0.0f, std::max(0.0f, bounds.width - 50),
                std::max(0.0f, bounds.height - (fluent ? 12 : 0))}, foundation_text);
        auto arrow = combo->drop_down_bounds();
        arrow.x -= control.bounds().x;
        arrow.y -= control.bounds().y;
        if (!fluent) arrow = {std::max(0.0f, bounds.width - 28), 0, 24, bounds.height};
        if (fluent) canvas.chevron(arrow, foundation_text, true);
        else canvas.text(L"\u25be", arrow, foundation_text);
        return;
    }
    if (auto* range = dynamic_cast<RangeInput*>(&control)) {
        const auto visual = range->slider_geometry();
        const auto track = visual.track, thumb = visual.thumb;
        const bool native_brushes = fluent && !palette.high_contrast;
        const auto brushes = winui_slider_brushes(palette.mode, enabled(peer), control.hovered(), control.pressed() || range->dragging());
        const auto track_ink = native_brushes ? argb_color(brushes.track) :
            fluent && enabled(peer) ? palette.secondary : palette.border;
        const auto value_ink = native_brushes ? argb_color(brushes.value) : enabled(peer) ? palette.accent : palette.disabled;
        const auto paint_default_thumb = [&] {
            const float radius = thumb.width / 2;
            if (fluent) {
                const auto fill = native_brushes ? argb_color(brushes.thumb) : palette.surface;
                canvas.styled_surface(thumb, palette, {}, fill, palette.border, 10, {1, 1, 1, 1});
                const float dot = !enabled(peer) || control.pressed() || range->dragging() ? 4.0f : control.hovered() ? 7.0f : 6.0f;
                canvas.rounded({thumb.x + radius - dot, thumb.y + radius - dot, 2 * dot, 2 * dot},
                    value_ink, dot);
            } else canvas.rounded(thumb, enabled(peer) ? palette.accent : palette.disabled, radius);
        };
        if (range->has_control_styling()) {
            const PartStyleValues empty;
            const auto part = [&](StylePart value) -> const PartStyleValues& {
                const auto* values = range->effective_control_style_values(value);
                return values ? *values : empty;
            };
            canvas.styled_surface({0, 0, bounds.width, bounds.height}, palette, part(StylePart::root),
                D2D1::ColorF(0, 0.0f), palette.border, 0, {});
            canvas.styled_surface(track, palette, part(StylePart::track), track_ink, palette.border, 2, {});
            const auto& fill = part(StylePart::fill);
            const auto fill_ink = !palette.high_contrast && fill.foreground ?
                D2D1::ColorF(fill.foreground->resolve(palette.mode)) : value_ink;
            canvas.styled_surface(visual.filled, palette, fill, fill_ink, palette.border, 2, {});
            const auto& thumb_style = part(StylePart::thumb);
            if (fluent && thumb_style.empty()) paint_default_thumb();
            else canvas.styled_surface(thumb, palette, thumb_style, value_ink, palette.border, thumb.width / 2, {1, 1, 1, 1});
            if (!fluent && control.focused()) canvas.focus_ring({1, 1, std::max(0.0f, bounds.width - 2),
                std::max(0.0f, bounds.height - 2)}, palette);
            return;
        }
        canvas.rounded(track, track_ink, 2);
        if (fluent && visual.filled.width > 0 && visual.filled.height > 0)
            canvas.rounded(visual.filled, value_ink, 2);
        paint_default_thumb();
        if (!fluent && control.focused()) {
            const Rect face{1, 1, std::max(0.0f, bounds.width - 2), std::max(0.0f, bounds.height - 2)};
            canvas.outline(face, palette.accent);
        }
        return;
    }
    if (auto* progress = dynamic_cast<Progress*>(&control)) {
        const PartStyleValues empty;
        const auto part = [&](StylePart value) -> const PartStyleValues& {
            const auto* values = progress->has_control_styling() ? progress->effective_control_style_values(value) : nullptr;
            return values ? *values : empty;
        };
        const auto& root_style = part(StylePart::root);
        const auto& track_style = part(StylePart::track);
        const auto& fill_style = part(StylePart::fill);
        const auto& caption_style = part(StylePart::caption);
        const auto thickness = track_style.thickness.value_or(style_metrics(palette.style).progress_thickness);
        const auto content = progress->content_bounds();
        const Rect track{content.x + 2, content.y + std::max(2.0f, content.height - std::max(10.0f, thickness + 2)),
            std::max(0.0f, content.width - 4), std::min(thickness, content.height)};
        auto ink = !enabled(peer) ? palette.disabled : progress->state() == ProgressState::error ? palette.error :
            progress->state() == ProgressState::paused ? palette.secondary : palette.accent;
        if (fluent && enabled(peer) && !palette.high_contrast && progress->state() == ProgressState::paused)
            ink = D2D1::ColorF(winui_status_colors(palette.mode).warning);
        if (progress->has_control_styling())
            canvas.styled_surface({0, 0, bounds.width, bounds.height}, palette, root_style,
                D2D1::ColorF(0, 0.0f), palette.border, 0, {});
        if (progress->ring_presentation()) {
            const Rect ring{content.x + 2, content.y + 2,
                std::max(0.0f, content.width - 4), std::max(0.0f, content.height - 4)};
            const auto track_ink = !palette.high_contrast && track_style.background ?
                D2D1::ColorF(track_style.background->resolve(palette.mode)) : palette.border;
            if (!palette.high_contrast && fill_style.background)
                ink = D2D1::ColorF(fill_style.background->resolve(palette.mode));
            canvas.arc(ring, 0, 1, track_ink, thickness);
            if (progress->state() == ProgressState::indeterminate)
                canvas.arc(ring, animated_progress(peer) && progress_animating ? progress_phase : 0, 0.25f,
                    ink, fill_style.thickness.value_or(thickness));
            else if (progress->state() != ProgressState::unknown) {
                const auto fraction = static_cast<float>(progress->presented_fraction());
                canvas.arc(ring, 0, fraction, ink, fill_style.thickness.value_or(thickness));
            }
            return;
        }
        if (progress->has_control_styling())
            canvas.styled_surface(track, palette, track_style, palette.border, palette.border, thickness / 2, {});
        else canvas.rounded(track, palette.border, thickness / 2);
        const auto segment = [&](Rect rect) {
            if (progress->has_control_styling())
                canvas.styled_surface(rect, palette, fill_style, ink, palette.border, thickness / 2, {});
            else canvas.rounded(rect, ink, thickness / 2);
        };
        std::wstring text = progress->value_text();
        if (progress->state() == ProgressState::indeterminate || progress->state() == ProgressState::unknown) {
            text = progress->state() == ProgressState::unknown ? L"Unknown" : L"In progress";
            if (progress->state() == ProgressState::indeterminate) {
                if (progress_animating && animated_progress(peer)) {
                    const float offset = progress_phase * 1.4f - 0.4f;
                    const float left = std::max(0.0f, offset);
                    const float right = std::min(1.0f, offset + 0.4f);
                    if (right > left)
                        segment({track.x + track.width * left, track.y, track.width * (right - left), track.height});
                } else if (fluent)
                    segment({track.x + track.width * 0.3f, track.y, track.width * 0.4f, track.height});
                else for (int i = 0; i < 5; ++i) segment({track.x + track.width * (i + 1) / 6 - 3, track.y, 6, thickness});
            }
        } else {
            const auto fraction = progress->presented_fraction();
            if (fraction > 0)
                segment({track.x, track.y, track.width * static_cast<float>(fraction), track.height});
            if (text.empty()) text = std::to_wstring(static_cast<int>(fraction * 100)) + L"%";
            if (progress->state() == ProgressState::paused) text += L" (paused)";
            if (progress->state() == ProgressState::error) text += L" (error)";
        }
        const Rect caption{content.x + 4, content.y, std::max(0.0f, content.width - 8),
            std::max(0.0f, track.y - content.y - 2)};
        if (progress->has_control_styling()) {
            const auto caption_ink = !palette.high_contrast && caption_style.foreground ?
                D2D1::ColorF(caption_style.foreground->resolve(palette.mode)) : foundation_text;
            canvas.styled_text(text, caption, caption_ink, caption_style, TextStyle::caption);
        } else canvas.text(text, caption, foundation_text, true);
        return;
    }

    if (auto* chart = dynamic_cast<HistoryChart*>(&control)) {
        const auto plot = chart->plot_bounds();
        const auto chart_root_values = chart->part_style(StylePart::root), title = chart->part_style(StylePart::title),
            caption = chart->part_style(StylePart::caption), grid_line = chart->part_style(StylePart::grid_line), series = chart->part_style(StylePart::plot);
        if (chart->has_control_styling()) canvas.styled_surface({1, 1, bounds.width - 2, bounds.height - 2}, palette, chart_root_values,
            palette.surface, palette.border, VisualMetrics::radius, fluent ? Insets{1, 1, 1, 1} : Insets{});
        else if (fluent) canvas.surface_frame({1, 1, bounds.width - 2, bounds.height - 2}, palette);
        else canvas.rounded({1, 1, bounds.width - 2, bounds.height - 2}, palette.surface);
        if (title.empty()) canvas.text(chart->name(), chart->title_bounds(), foundation_text, true);
        else canvas.styled_text(chart->name(), chart->title_bounds(), Drawing::style_foreground(title, palette, foundation_text), title, TextStyle::caption);
        if (chart->has_control_styling()) canvas.push_clip(plot);
        for (int i = 0; i <= 4; ++i) {
            const float y = plot.y + plot.height * i / 4;
            if (grid_line.thickness.value_or(1) > 0) canvas.line(plot.x, y, plot.x + plot.width, y,
                Drawing::style_foreground(grid_line, palette, palette.border), grid_line.thickness.value_or(1));
        }
        for (std::size_t i = 1; i < chart->size(); ++i) {
            const auto before = chart->at(i - 1), after = chart->at(i);
            if (!before || !after) continue;
            const float x = plot.x + plot.width * static_cast<float>(HistoryChart::capacity - chart->size() + i) / (HistoryChart::capacity - 1);
            if (series.thickness.value_or(2) > 0) canvas.line(x - plot.width / (HistoryChart::capacity - 1), plot.y + plot.height * static_cast<float>(1 - *before / chart->maximum()),
                x, plot.y + plot.height * static_cast<float>(1 - *after / chart->maximum()),
                Drawing::style_foreground(series, palette, palette.accent), series.thickness.value_or(2));
        }
        if (chart->has_control_styling()) canvas.pop_clip();
        if (caption.empty()) canvas.text(L"60 samples   \u00b7   oldest \u2192 newest", chart->caption_bounds(), palette.secondary, true);
        else canvas.styled_text(L"60 samples   \u00b7   oldest \u2192 newest", chart->caption_bounds(),
            Drawing::style_foreground(caption, palette, palette.secondary), caption, TextStyle::caption);
        return;
    }
    if (is_caption_button(control)) {
        const auto& button = static_cast<const Button&>(control);
        const auto state = (caption_active ? style_states::active : style_states::inactive) |
            (titlebar->maximized() ? style_states::maximized : 0) |
            (control.hovered() ? style_states::hovered : 0) | (control.pressed() ? style_states::pressed : 0) |
            (control.focused() ? style_states::focused : 0) | (!enabled(peer) ? style_states::disabled : 0);
        auto values = titlebar->has_control_styling() ? titlebar->resolve_control_style_part(
            button.icon() == ButtonIcon::close ? StylePart::caption_close : StylePart::caption_button, state) : PartStyleValues{};
        values = merge_part_values(std::move(values), button.surface_style_values());
        const auto icon_values = button.content_style_values(StylePart::icon);
        canvas.caption_button({0, 0, bounds.width, bounds.height}, button.icon(),
            palette, caption_active, enabled(peer), control.hovered(), control.pressed(), control.focused(),
            values.empty() ? nullptr : &values, icon_values.empty() ? nullptr : &icon_values);
        return;
    }
    const auto* wrapping_label = dynamic_cast<const Label*>(&control);
    if (!wrapping_label || !wrapping_label->wrapping()) control.measured_text();
    if (!peer.text_layout && (control.role() == ControlRole::label ||
        control.role() == ControlRole::button || control.role() == ControlRole::toggle)) {
        Size measured{};
        peer.text_layout = canvas.layout(control.name(), control.text_style(), measured);
    }
    const auto* status_parent = peer.parent ? dynamic_cast<InlineStatus*>(peer.parent->control.get()) : nullptr;
    if (status_parent)
        canvas.fill({0, 0, bounds.width, bounds.height}, status_fill(status_parent->severity()));
    const float inset_size = fluent ? 0.5f : 2.0f;
    const Rect box{inset_size, inset_size, std::max(0.0f, bounds.width - 2 * inset_size), std::max(0.0f, bounds.height - 2 * inset_size)};
    const auto text = enabled(peer) ? palette.text : palette.disabled;
    const auto role = control.role();
    if (role == ControlRole::button && peer.parent) {
        const auto* breadcrumb = dynamic_cast<const Breadcrumb*>(peer.parent->control.get());
        if (breadcrumb && (breadcrumb->has_control_styling() || control.has_control_styling())) {
            const auto& button = static_cast<const Button&>(control);
            bool separator = &button == breadcrumb->overflow_button().get();
            std::wstring_view label;
            const auto children = breadcrumb->owned_children();
            for (std::size_t i = 0; i < breadcrumb->segments().size(); ++i) if (children[i + 1].get() == &button) {
                label = breadcrumb->segments()[i].label;
                separator = i + 1 != breadcrumb->segments().size();
                break;
            }
            const auto content = button.content_bounds({0, 0, bounds.width, bounds.height});
            const auto arrow = &button == breadcrumb->overflow_button().get() ?
                button.content_style_values(StylePart::arrow) : PartStyleValues{};
            const auto arrow_padding = arrow.padding.value_or(Insets{});
            const auto separator_width = separator ?
                std::min(arrow.size.value_or(20.0f) + arrow_padding.left + arrow_padding.right, content.width / 2) : 0.0f;
            canvas.styled_button(button, {0, 0, bounds.width, bounds.height}, palette, enabled(peer), focus_visible,
                label, separator_width);
            if (separator) {
                const auto* values = breadcrumb->effective_control_style_values(StylePart::separator);
                auto ink = values && values->foreground && !palette.high_contrast ?
                    D2D1::ColorF(values->foreground->resolve(palette.mode)) : text;
                if (arrow.foreground && !palette.high_contrast) ink = D2D1::ColorF(arrow.foreground->resolve(palette.mode));
                const Rect glyph{content.x + content.width - separator_width + arrow_padding.left,
                    content.y + arrow_padding.top,
                    std::max(0.0f, separator_width - arrow_padding.left - arrow_padding.right),
                    std::max(0.0f, content.height - arrow_padding.top - arrow_padding.bottom)};
                canvas.symbol(Symbol::breadcrumb_separator,
                    glyph, ink, std::min({arrow.size ? *arrow.size * 0.6f : 12.0f, glyph.width, glyph.height}));
            }
            return;
        }
    }
    if (auto* badge = dynamic_cast<InfoBadge*>(&control)) {
        canvas.info_badge(*badge, {0, 0, bounds.width, bounds.height}, palette, enabled(peer));
        return;
    }
    if (auto* link = dynamic_cast<HyperlinkButton*>(&control)) {
        canvas.hyperlink(*link, {0, 0, bounds.width, bounds.height}, palette, enabled(peer), focus_visible);
        return;
    }
    if (role == ControlRole::button && (control.has_control_styling() || static_cast<const Button&>(control).vertical_text())) {
        std::optional<bool> step_increment;
        if (peer.parent && peer.parent->control->role() == ControlRole::numeric_input) {
            const auto& numeric = static_cast<const NumericInput&>(*peer.parent->control);
            step_increment = numeric.increase_button().get() == &control;
        }
        canvas.styled_button(static_cast<const Button&>(control), {0, 0, bounds.width, bounds.height},
            palette, enabled(peer), focus_visible, {}, 0, step_increment);
        return;
    }
    if (role == ControlRole::toggle && (control.has_control_styling() ||
        static_cast<const Toggle&>(control).switch_presentation() || dynamic_cast<CheckBox*>(&control))) {
        canvas.styled_toggle(static_cast<const Toggle&>(control), {0, 0, bounds.width, bounds.height},
            palette, enabled(peer), peer.text_layout.Get(), focus_visible);
        return;
    }
    if (role == ControlRole::label) {
        auto& label = static_cast<Label&>(control);
        if (label.has_control_styling()) {
            canvas.styled_label(label, {0, 0, bounds.width, bounds.height}, palette, enabled(peer));
            return;
        }
        const auto color = !enabled(peer) ? palette.disabled : label.tone() == TextTone::accent ? palette.accent :
            label.tone() == TextTone::secondary ? palette.secondary :
            label.tone() == TextTone::error ? palette.error : text;
        const Rect area{0, 0, bounds.width, label.wrapping() ?
            std::min(bounds.height, label.wrapped_text(bounds.width).height) : bounds.height};
        canvas.text_layout(peer.text_layout.Get(), area, color);
    } else {
        const auto* button = dynamic_cast<const Button*>(&control);
        const bool checked_action = button && button->checked() &&
            (button->behavior() == ButtonBehavior::toggle || dynamic_cast<const MenuBar::Heading*>(button));
        const bool winui = palette.style == VisualStyle::winui;
        auto ink = control.pressed() || checked_action ? palette.selection_text : text;
        if (button && winui)
            ink = canvas.button_face(box, palette, button->appearance(), enabled(peer),
                control.hovered(), control.pressed(), checked_action);
        else if (!winui && (role == ControlRole::button || control.hovered() || control.pressed()))
            canvas.rounded(box, control.pressed() ? palette.selection :
                checked_action ? palette.selection : control.hovered() ? palette.hover : palette.surface);
        if (role == ControlRole::button && !winui)
            canvas.rounded(box, palette.border, 6, true);
        float inset = 14;
        if (role == ControlRole::toggle) {
            const bool checked = static_cast<const Toggle&>(control).checked();
            Rect mark = winui ? Rect{0.5f, bounds.height / 2 - 9.5f, 19, 19} :
                Rect{12, bounds.height / 2 - 9, 18, 18};
            if (winui) ink = canvas.check_indicator(mark, palette, checked, enabled(peer), false,
                control.hovered(), control.pressed());
            else {
                const auto check_fill = palette.high_contrast ? palette.selection : palette.accent;
                canvas.rounded(mark, checked ? (enabled(peer) ? check_fill : palette.disabled) : palette.field, 3);
                canvas.rounded(mark, enabled(peer) ? palette.accent : palette.disabled, 3, true);
                if (checked) {
                    const auto check_ink = palette.high_contrast && enabled(peer) ? palette.selection_text : palette.background;
                    canvas.line(mark.x + 4, mark.y + 9, mark.x + 8, mark.y + 13, check_ink, 2);
                    canvas.line(mark.x + 8, mark.y + 13, mark.x + 14, mark.y + 5, check_ink, 2);
                }
            }
            inset = winui ? 28.0f : 42.0f;
        }
        if (role == ControlRole::button)
            inset = std::max(winui ? 12.0f : 14.0f, (bounds.width - control.measured_text().width) / 2);
        const auto icon = role == ControlRole::button ? static_cast<const Button&>(control).icon() : ButtonIcon::none;
        const auto* breadcrumb = winui && peer.parent ? dynamic_cast<const Breadcrumb*>(peer.parent->control.get()) : nullptr;
        if (button && breadcrumb) {
            const auto children = breadcrumb->owned_children();
            if (button == breadcrumb->overflow_button().get()) {
                canvas.symbol(Symbol::more, {0, 0, bounds.width / 2, bounds.height}, ink);
                canvas.symbol(Symbol::breadcrumb_separator, {bounds.width / 2, 0, bounds.width / 2, bounds.height}, ink, 12);
            } else for (std::size_t i = 0; i < breadcrumb->segments().size(); ++i) if (children[i + 1].get() == button) {
                const bool separator = i + 1 != breadcrumb->segments().size();
                canvas.text(breadcrumb->segments()[i].label, {12, 0,
                    std::max(0.0f, bounds.width - (separator ? 36 : 24)), bounds.height}, ink);
                if (separator) canvas.symbol(Symbol::breadcrumb_separator,
                    {bounds.width - 24, 0, 24, bounds.height}, ink, 12);
                break;
            }
        } else if (button && peer.parent && peer.parent->control->role() == ControlRole::numeric_input) {
            const auto children = peer.parent->control->owned_children();
            if (winui) {
                canvas.symbol(children[1].get() == button ? Symbol::chevron_down : Symbol::chevron_up,
                    {0, 0, bounds.width, bounds.height}, ink, 12);
            } else canvas.text(children[1].get() == button ? L"\u2212" : L"+", {12, 0, 24, bounds.height}, ink);
        } else if (button && button->behavior() == ButtonBehavior::dropdown) {
            const Rect arrow{std::max(0.0f, bounds.width - 26), 0, 24, bounds.height};
            if (winui) canvas.chevron(arrow, ink, true);
            else canvas.text(L"\u25be", arrow, ink);
            if (bounds.width > (winui ? 44.0f : 64.0f))
                canvas.text(control.name(), {12, winui ? 6.0f : 0.0f, std::max(0.0f, bounds.width - 44),
                    std::max(0.0f, bounds.height - (winui ? 13 : 0))}, ink);
        }
        else if (icon != ButtonIcon::none)
            canvas.button_icon({(bounds.width - 16) / 2, (bounds.height - 16) / 2, 16, 16}, ink, icon);
        else canvas.text_layout(peer.text_layout.Get(),
            {inset, winui && button ? 6.0f : 0.0f,
                std::max(0.0f, bounds.width - inset - (winui && role == ControlRole::toggle ? 0.0f : 12.0f)),
                std::max(0.0f, bounds.height - (winui && button ? 13 : 0))}, ink);
        if (focus_visible) {
            if (winui) canvas.focus_ring(box, palette);
            else canvas.rounded(box,
                palette.high_contrast && control.pressed() ? palette.selection_text : palette.accent, 6, true);
        }
    }
}

}

namespace xui {
void BasicHostPeer::attach(HWND parent, int id) {
    ControlHostPeer::attach(parent, id);
    auto* peer = this;
    auto* added = this;
    const auto role = control->role();
        if ((role == ControlRole::label || role == ControlRole::button || role == ControlRole::toggle))
            peer->control->set_text_measurer([this, added](std::wstring_view text, TextStyle style) {
                Size measured{};
                const auto& control = *added->control;
                const auto* label = dynamic_cast<const Label*>(&control);
                const auto button_text = control.role() == ControlRole::button && control.has_control_styling() ?
                    static_cast<const Button&>(control).content_style_values(StylePart::label) : PartStyleValues{};
                const auto* values = control.role() == ControlRole::button && control.has_control_styling() ? &button_text :
                    control.has_control_styling() ?
                    control.effective_control_style_values(label ? label->text_part() : StylePart::label) : nullptr;
                if (values && label) {
                    auto typography = *values;
                    typography.wrapping = label->wrapping();
                    typography.maximum_lines = static_cast<uint32_t>(std::min<std::size_t>(label->maximum_lines(), UINT32_MAX));
                    added->text_layout = services.control_drawing().styled_layout(text, style, typography, measured);
                } else added->text_layout = values ? services.control_drawing().styled_layout(text, style, *values, measured) :
                    services.control_drawing().layout(text, style, measured);
                return measured;
            });
        if (role == ControlRole::radio_group || role == ControlRole::choice_list)
            peer->control->set_text_measurer([this](std::wstring_view text, TextStyle style) {
                Size measured{};
                services.control_drawing().layout(text, style, measured);
                return measured;
            });
        if (auto label = std::dynamic_pointer_cast<Label>(peer->control))
            label->set_wrapped_text_measurer([this, added](std::wstring_view text, TextStyle style, float width, std::size_t lines) {
                Size measured{};
                const auto& label = static_cast<const Label&>(*added->control);
                const auto* values = label.has_control_styling() ? label.effective_control_style_values(label.text_part()) : nullptr;
                if (values) {
                    auto typography = *values;
                    typography.wrapping = label.wrapping();
                    typography.maximum_lines = static_cast<uint32_t>(std::min<std::size_t>(lines, UINT32_MAX));
                    added->text_layout = services.control_drawing().styled_layout(text, style, typography, measured, width, lines);
                } else added->text_layout = services.control_drawing().layout(text, style, measured, width, lines);
                return measured;
            });
}
}
