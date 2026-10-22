#include "collection_host.hpp"
#include "xui/navigation.hpp"
#include "xui/miller_columns.hpp"
#include "xui/commands.hpp"
#include "context_menu.hpp"
#include "wheel.hpp"
#include <windowsx.h>

namespace xui {

CollectionHostPeer::CollectionHostPeer(PeerHostServices& owner, std::shared_ptr<VirtualCollection> value)
    : HostPeer(owner, HostFamily::collection, value), collection_(*value) {}

void CollectionHostPeer::publish_accessibility() {
    publish_collection(accessibility_, provider, collection_, window);
}
void CollectionHostPeer::disconnect_accessibility() {
    disconnect_row_accessibility(accessibility_, provider);
}
void CollectionHostPeer::sync(UINT, const Palette&) { publish_accessibility(); }
void CollectionHostPeer::detach_owner() {
    wheel_.active = false;
    if (row_images_) row_images_->clear();
}
void CollectionHostPeer::wheel_scroll(double delta, bool precise) {
    const auto value = wheel_.scroll(collection_.offset(), collection_.maximum_offset(),
        collection_.source(), delta, precise, host.wheel_animation_allowed());
    collection_.set_offset(value);
    if (wheel_.active) host.invalidate_peer_paint();
}
bool CollectionHostPeer::advance_wheel(Animation::Clock::time_point now, bool settle, bool tick) {
    if (const auto value = wheel_.advance(collection_.offset(), collection_.source().get(), now, settle, tick)) {
        collection_.set_offset(*value);
        wheel_.last = collection_.offset();
    }
    return wheel_.active;
}
std::size_t CollectionHostPeer::metric(PeerMetric metric) const {
    return metric == PeerMetric::retained_images && row_images_ ? row_images_->count() : 0;
}

CollectionHostPeer::~CollectionHostPeer() {
    host.forget_peer(*this);
    detach_owner();
    retire_native();
    disconnect_accessibility();
}
void CollectionHostPeer::attach(HWND parent_window, int id) {
    window = host.create_input_surface(*this, parent_window, id);
    publish_collection(accessibility_, nullptr, collection_, window);
    provider = create_collection_provider(accessibility_);
}
bool CollectionHostPeer::capture_active() const { return collection_drag || collection_scroll; }
void CollectionHostPeer::cancel_input() {
    collection_drag = collection_scroll = false;
    collection_anchor.reset();
    collection_before = {};
    if (auto* list = dynamic_cast<MillerColumnList*>(&collection_)) list->hover_pointer({});
    collection_.cancel();
}

void CollectionHostPeer::paint(const HostPaintContext& context) {
    auto& canvas = context.canvas;
    const auto& palette = context.palette;
    auto& control = *this->control;
    const auto bounds = paint_bounds;
    const bool fluent = palette.style == VisualStyle::winui;
    const auto foundation_text = context.enabled ? palette.text : palette.disabled;
    auto* collection = &collection_;
    const bool commands = control.role() == ControlRole::command_menu;
    const auto* items = dynamic_cast<ItemsView*>(collection);
    const bool trailing_shortcuts = items && items->trailing_shortcut_badges();
    const auto* miller = dynamic_cast<MillerColumnList*>(collection);
    const auto hovered = !context.enabled || GetCapture() ? std::optional<std::size_t>{} :
        miller ? miller->hovered_row() : control.hovered() && command_pointer ?
        collection->hit_test(*command_pointer) : std::optional<std::size_t>{};
    const auto hovered_key = hovered ?
        std::optional{collection->source()->key(*hovered)} : std::optional<ItemKey>{};
    const auto collection_background = commands || (fluent && surface) ? palette.surface :
        palette.backdrop ? D2D1::ColorF(0, 0.0f) : palette.background;
    if (collection->has_control_styling())
        canvas.styled_surface({0, 0, bounds.width, bounds.height}, palette,
            collection->resolve_control_style_part(StylePart::root, 0), collection_background, palette.border, 0, {});
    else canvas.fill({0, 0, bounds.width, bounds.height}, collection_background);
    const auto viewport = collection->content_viewport();
    const auto& presentation = detail::CollectionPresentationAccess::get(*collection);
    canvas.push_clip(viewport);
    for (auto row : collection->visible_content()) {
        if (command_pointer) row.hovered = row.content.enabled && context.enabled && hovered_key == row.key;
        if (row_images_) {
            const auto visual = row_images_->visual(row.key);
            row.content.icon = visual.icon;
            row.content.image_path = visual.image_path;
        }
        if (presentation) canvas.push_clip(detail::CollectionPresentationAccess::clip(*collection, row.index));
        canvas.collection_row(row, collection->selection().contains(row.key),
            control.focused() && collection->selection().focused() == row.key, context.enabled, palette,
            row.content.enabled && context.enabled && hovered_key == row.key,
            row_images_ ? row_images_->pixels(row.key) : nullptr, trailing_shortcuts, commands, collection,
            row_images_ && row_images_->loading(row.key));
        if (presentation) canvas.pop_clip();
    }
    bool outgoing_visible{};
    if (presentation) for (const auto& exit : presentation->outgoing()) {
        const auto painted = exit.bounds.intersect(exit.clip).intersect(
            {0, collection->offset(), viewport.width, viewport.height});
        if (painted.width <= 0 || painted.height <= 0) continue;
        outgoing_visible = true;
        const auto& frozen = *exit.frozen;
        auto row = frozen.row;
        row.bounds = exit.bounds.in_view(viewport, collection->offset());
        row.content.icon = frozen.visual.icon; row.content.image_path = frozen.visual.image_path;
        canvas.push_clip(painted.in_view(viewport, collection->offset()));
        canvas.collection_row(row, frozen.selected, false, context.enabled, palette, false,
            row_images_ ? row_images_->pixels(row.key) : nullptr, trailing_shortcuts, commands, collection,
            row_images_ && row_images_->loading(row.key));
        canvas.pop_clip();
    }
    if ((!collection->source() || !collection->source()->size()) &&
        !outgoing_visible)
        if (collection->has_control_styling()) {
            const auto values = collection->resolve_control_style_part(StylePart::empty, 0);
            canvas.styled_text(commands ? L"No matching commands" : L"No matching items",
                {viewport.x + 12, viewport.y + 10, std::max(0.0f, viewport.width - 24), 32},
                values.foreground && !palette.high_contrast ? D2D1::ColorF(values.foreground->resolve(palette.mode)) : palette.secondary, values);
        } else canvas.text(commands ? L"No matching commands" : L"No matching items",
            {12, 10, std::max(0.0f, bounds.width - 24), 32}, palette.secondary);
    canvas.pop_clip(); const auto thumb = collection->thumb();
    if (thumb.height) {
        if (collection->has_control_styling()) {
            const auto track = collection->resolve_control_style_part(StylePart::scrollbar_track, 0);
            canvas.styled_surface({viewport.x + viewport.width, viewport.y, collection->scrollbar_width(), viewport.height},
                palette, track, collection_background, palette.border, 0, {});
            const auto values = collection->resolve_control_style_part(StylePart::scrollbar_thumb, 0);
            canvas.styled_surface(thumb, palette, values, context.enabled ? palette.secondary : palette.disabled, palette.border, 3, {});
        } else if (fluent) canvas.scrollbar_thumb(thumb, palette, false, context.enabled);
        else canvas.rounded(thumb, palette.secondary, 3);
    }
    return;
}

bool CollectionHostPeer::image_work(RowImageWork& work) {
    if (!row_images_) row_images_ = std::make_unique<RowImages>();
    auto* collection = &collection_;
    auto rect = work.clip;
    rect.x -= control->bounds().x; rect.y -= control->bounds().y;
    std::vector<RowVisual> rows;
    std::shared_ptr<const CollectionIndex> source;
    if (work.shown) {
        source = collection->source();
        for (const auto& row : collection->visible_content()) {
            if (rows.size() == RowImages::maximum_rows) break;
            const auto b = detail::CollectionPresentationAccess::get(*collection) ?
                detail::CollectionPresentationAccess::clip(*collection, row.index) : row.bounds;
            if (b.y + b.height <= rect.y || b.y >= rect.y + rect.height ||
                b.x + b.width <= rect.x || b.x >= rect.x + rect.width) continue;
            auto visual = collection->source()->visual(row.index);
            if (visual.icon == ButtonIcon::none) visual.icon = row.content.icon;
            if (visual.image_path.empty()) visual.image_path = row.content.image_path;
            if (row.navigation && !visual.image_path.empty() && visual.icon == ButtonIcon::none)
                visual.icon = ButtonIcon::folder;
            float image_dips{};
            const auto* tree = dynamic_cast<const TreeView*>(collection);
            if (collection->presentation() == ItemsPresentation::gallery || tree) {
                const auto state = collection_row_style_state(row, collection->selection().contains(row.key),
                    collection->focused() && collection->selection().focused() == row.key, host.input_enabled(*this),
                    !GetCapture() && collection->hovered() && command_pointer &&
                        collection->hit_test(*command_pointer) == row.index);
                if (collection->presentation() == ItemsPresentation::gallery)
                    image_dips = std::clamp(collection->gallery_layout(row, state).image.width,
                        1.0f, float(ImageLimits::output_dimension));
                else if (!tree->detail_columns().empty()) {
                    auto visual_row = row;
                    visual_row.content.icon = visual.icon; visual_row.content.image_path = visual.image_path;
                    image_dips = std::clamp(tree->details_layout(visual_row, state).icon.width,
                        1.0f, float(ImageLimits::output_dimension));
                } else if (tree->has_control_styling()) {
                    if (const auto extent = tree->row_style_values(StylePart::icon, row, state).size)
                        image_dips = std::clamp(*extent, 1.0f, float(ImageLimits::output_dimension));
                }
            }
            rows.push_back({row.key, std::move(visual), row.navigation, image_dips});
        }
        if (const auto& frame = detail::CollectionPresentationAccess::get(*collection)) {
            const auto viewport = collection->content_viewport();
            for (const auto& exit : frame->outgoing()) {
                if (rows.size() == RowImages::maximum_rows) break;
                const auto painted = exit.bounds.intersect(exit.clip).intersect(
                    {0, collection->offset(), viewport.width, viewport.height});
                if (painted.width <= 0 || painted.height <= 0) continue;
                const auto b = painted.in_view(viewport, collection->offset());
                if (b.y + b.height <= rect.y || b.y >= rect.y + rect.height ||
                    b.x + b.width <= rect.x || b.x >= rect.x + rect.width) continue;
                const auto& frozen = *exit.frozen;
                rows.push_back({frozen.row.key, frozen.visual, frozen.row.navigation});
            }
        }
    }
    work.clip = rect;
    work.source = std::move(source); work.rows = std::move(rows);
    return true;
}
bool CollectionHostPeer::sync_images(RowImageWork work, UINT dpi, const std::shared_ptr<TaskWake>& wake,
    std::vector<std::uint64_t>& retained, std::size_t&) {
    const bool retain_on_source_change = dynamic_cast<NavigationList*>(&collection_) != nullptr ||
        collection_.stable_image_identity() || detail::CollectionPresentationAccess::get(collection_);
    return row_images_->sync(std::move(work.source), std::move(work.rows), dpi, wake, retained,
        retain_on_source_change, collection_.stable_image_identity());
}

HRESULT CollectionHostPeer::accessibility_action(WPARAM wparam, LPARAM lparam) {
    if (wparam != control->id()) return UIA_E_ELEMENTNOTAVAILABLE;
    GridAction action;
    if (!take_row_action(accessibility_, control->id(), window, static_cast<std::uint64_t>(lparam), action))
        return UIA_E_ELEMENTNOTAVAILABLE;
    if (!host.input_enabled(*this) || !host.input_visible(*this)) return UIA_E_ELEMENTNOTENABLED;
    auto* collection = &collection_;
    if (action.key && (!collection->source() || !collection->source()->find(*action.key))) return UIA_E_ELEMENTNOTAVAILABLE;
    if (action.kind == GridAction::scroll) collection->set_offset(action.y);
    else if (action.kind == GridAction::select_all) collection->select_all();
    else if (action.kind == GridAction::focus) {
        if (!host.focus_peer(*this, true)) return UIA_E_INVALIDOPERATION;
        if (action.key) collection->select(*action.key, SelectionGesture::focus_only);
    } else if (!action.key) return E_INVALIDARG;
    else if (action.kind == GridAction::select) collection->select(*action.key);
    else if (action.kind == GridAction::add || action.kind == GridAction::remove) {
        const bool selected = collection->selection().contains(*action.key);
        if (action.kind == GridAction::add && !collection->multiple_selection() &&
            !selected && !collection->selection().empty()) return UIA_E_INVALIDOPERATION;
        if (selected != (action.kind == GridAction::add)) {
            if (auto* nav_list = dynamic_cast<NavigationList*>(collection); nav_list && action.kind == GridAction::remove) {
                if (!nav_list->remove_selection(*action.key)) return UIA_E_INVALIDOPERATION;
            } else if (auto* miller_list = dynamic_cast<MillerColumnList*>(collection);
                miller_list && action.kind == GridAction::remove) {
                if (!miller_list->remove_selection(*action.key)) return UIA_E_INVALIDOPERATION;
            } else collection->select(*action.key, SelectionGesture::toggle);
        }
    } else if (action.kind == GridAction::reveal) collection->reveal(*action.key);
    else if (action.kind == GridAction::expand || action.kind == GridAction::collapse) {
        if (!collection->disclose(*action.key, action.kind == GridAction::expand)) return UIA_E_INVALIDOPERATION;
    } else if (action.kind == GridAction::invoke || action.kind == GridAction::inline_action)
        collection->activate_item(*action.key, action.kind == GridAction::inline_action);
    else return UIA_E_INVALIDOPERATION;
    host.update_peer_state(); return S_OK;
}

std::optional<LRESULT> CollectionHostPeer::input(UINT message, WPARAM wparam, LPARAM lparam,
    const HostInputContext& context) {
    auto& control = *this->control;
    auto* collection = &collection_;
    const auto hwnd = window;
    const auto dpi = context.dpi;
    switch (message) {
    case WM_MOUSEWHEEL: {
        if (!host.input_enabled(*this)) return 0;
        const int delta = GET_WHEEL_DELTA_WPARAM(wparam);
        wheel_scroll(-vertical_wheel_dips(wparam, collection->content_viewport().height),
            std::abs(delta) < WHEEL_DELTA); return 0;
    }
    case WM_CONTEXTMENU: {
        if (!host.input_enabled(*this)) return 0;
        std::optional<Point> position;
        if (lparam != -1) {
            POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)}; ScreenToClient(hwnd, &point);
            position = Point{point.x * 96.0f / dpi, point.y * 96.0f / dpi};
        }
        if (auto* list = dynamic_cast<MillerColumnList*>(collection)) {
            if (!list->prepare_context_menu(position)) return 0;
        } else if (auto* nav = dynamic_cast<NavigationList*>(collection)) {
            if (!nav->prepare_context_menu(position)) return 0;
        } else if (position) {
            if (const auto row = collection->hit_test(*position)) {
                const auto source = collection->source();
                const auto item_key = source->key(*row);
                if (!collection->select(item_key) || collection->source() != source) return 0;
            } else collection->set_selection({});
        }
        if (!host.owner_open() || !host.focus_peer(*this, false)) return 0;
        show_control_menu(control, hwnd, lparam, context.palette, dpi);
        return 0;
    }
    case WM_MOUSEMOVE: {
        if (auto* list = dynamic_cast<MillerColumnList*>(&control)) {
            const bool hovering = host.input_enabled(*this) && !GetCapture() &&
                !(wparam & (MK_LBUTTON | MK_RBUTTON | MK_MBUTTON));
            list->hover_pointer(hovering ? std::optional{Point{GET_X_LPARAM(lparam) * 96.0f / dpi,
                GET_Y_LPARAM(lparam) * 96.0f / dpi}} : std::nullopt);
        }
        if (auto* nav_list = dynamic_cast<NavigationList*>(&control)) {
            const bool hovering = host.input_enabled(*this) && !GetCapture() &&
                !(wparam & (MK_LBUTTON | MK_RBUTTON | MK_MBUTTON));
            const auto row = hovering ? nav_list->hit_test({GET_X_LPARAM(lparam) * 96.0f / dpi,
                GET_Y_LPARAM(lparam) * 96.0f / dpi}) : std::nullopt;
            nav_list->hover_item(row ? std::optional{nav_list->source()->key(*row)} : std::nullopt);
            host.offer_row_tooltip(*this);
        }
        if (collection_drag || collection_scroll) {
            const float x = GET_X_LPARAM(lparam) * 96.0f / dpi, y = GET_Y_LPARAM(lparam) * 96.0f / dpi;
            if (collection_scroll) {
                const auto track = collection->content_viewport().height - collection->thumb().height;
                if (track > 0) collection->set_offset(drag_offset + (y - drag_y) * collection->maximum_offset() / track);
            } else if (collection_anchor) {
                const auto viewport = collection->content_viewport();
                if (const auto row = collection->hit_test({std::clamp(x, viewport.x, viewport.x + std::max(0.0f, viewport.width - 1)),
                    std::clamp(y, viewport.y, viewport.y + std::max(0.0f, viewport.height - 1))})) {
                    collection->set_selection(collection_before);
                    collection->select_rectangle(*collection_anchor, collection->source()->key(*row), collection_additive);
                }
            }
            return 0;
        }
        const Point point{GET_X_LPARAM(lparam) * 96.0f / dpi, GET_Y_LPARAM(lparam) * 96.0f / dpi};
        const auto previous = command_pointer ? collection->hit_test(*command_pointer) : std::optional<std::size_t>{};
        command_pointer = point;
        if (previous != collection->hit_test(point)) host.invalidate_peer_paint();
        control.pointer_move(row_pointer_inside(hwnd, lparam));
        track_pointer_leave();
        return 0;
    }
    case WM_MOUSELEAVE:
        if (auto* list = dynamic_cast<MillerColumnList*>(&control)) list->hover_pointer({});
        if (auto* list = dynamic_cast<NavigationList*>(&control)) list->hover_item({});
        command_pointer.reset();
        tracking = false;
        control.pointer_move(false);
        return 0;
    case WM_LBUTTONDOWN: {
        wheel_.active = false;
        if (auto* list = dynamic_cast<MillerColumnList*>(&control)) list->hover_pointer({});
        SetFocus(hwnd);
        const Point point{GET_X_LPARAM(lparam) * 96.0f / dpi, GET_Y_LPARAM(lparam) * 96.0f / dpi};
        const auto viewport = collection->content_viewport();
        if (point.x >= viewport.x + viewport.width && point.x < viewport.x + viewport.width + collection->scrollbar_width() &&
            point.y >= viewport.y && point.y < viewport.y + viewport.height) {
            const auto thumb = collection->thumb();
            if (thumb.height && point.y >= thumb.y && point.y < thumb.y + thumb.height) {
                collection_scroll = true; drag_y = point.y; drag_offset = static_cast<float>(collection->offset()); SetCapture(hwnd);
            } else collection->set_offset(collection->offset() +
                (point.y < thumb.y ? -collection->content_viewport().height : collection->content_viewport().height));
        } else if (const auto index = collection->hit_test(point)) {
            const auto source = collection->source(); const auto item_key = source->key(*index);
            const auto info = source->hierarchy(*index); const auto b = collection->item_bounds(*index);
            const auto* nav_list = dynamic_cast<NavigationList*>(collection);
            const bool disclosure = nav_list ? nav_list->disclosure_hit(point) :
                collection->disclosure_hit(*index, point);
            if (disclosure) { collection->disclose(item_key, !info.expanded); return 0; }
            if (b.width >= 160 && point.x >= b.x + b.width - 74 && !source->item(*index).action.empty()) {
                collection->activate_item(item_key, true); return 0;
            }
            if (auto* menu = dynamic_cast<CommandMenu*>(collection)) { menu->execute(item_key.id); return 0; }
            collection_before = collection->selection();
            const bool ctrl = (wparam & MK_CONTROL) != 0, shift = (wparam & MK_SHIFT) != 0;
            const bool single_click = collection->single_click_activation() && !ctrl && !shift && !info.group;
            if (collection->wraps_items() && !shift && !single_click) {
                collection_drag = true; collection_anchor = item_key; collection_additive = ctrl; SetCapture(hwnd);
            }
            const bool selected = collection->select(item_key, shift ? (ctrl ? SelectionGesture::add_range : SelectionGesture::extend) :
                ctrl ? SelectionGesture::toggle : SelectionGesture::replace);
            if (single_click && selected && host.owner_open() && host.input_visible(*this) && host.input_enabled(*this) &&
                collection->single_click_activation() && collection->source() == source)
                collection->activate_item(item_key);
        }
        return 0;
    }
    case WM_LBUTTONUP:
        if (std::exchange(suppress_popup_click, false)) return 0;
        collection_drag = collection_scroll = false;
        collection_anchor.reset(); collection_before = {};
        if (GetCapture() == hwnd) ReleaseCapture();
        return 0;
    case WM_LBUTTONDBLCLK: {
        if (collection->single_click_activation()) return 0;
        if (const auto row = collection->hit_test({GET_X_LPARAM(lparam) * 96.0f / dpi, GET_Y_LPARAM(lparam) * 96.0f / dpi})) {
            const auto item_key = collection->source()->key(*row); const auto info = collection->source()->hierarchy(*row);
            if (info.expandable) collection->disclose(item_key, !info.expanded);
            else collection->activate_item(item_key);
        }
        return 0;
    }
    case WM_CANCELMODE:
    case WM_CAPTURECHANGED:
        cancel_input();
        if (GetCapture() == hwnd) ReleaseCapture();
        if (message == WM_CAPTURECHANGED) host.invalidate_peer_paint();
        return 0;
    case WM_KEYDOWN: {
        if (wparam == VK_ESCAPE) {
            if (collection_drag) collection->set_selection(collection_before);
            collection_drag = collection_scroll = false;
            if (GetCapture() == hwnd) ReleaseCapture(); collection->cancel(); return 0;
        }
        if (wparam == 'A' && GetKeyState(VK_CONTROL) < 0) { collection->select_all(); return 0; }
        if (wparam == VK_UP || wparam == VK_DOWN) {
            collection->step((wparam == VK_UP ? -1 : 1) * static_cast<int>(collection->columns()), row_keyboard_gesture()); return 0;
        }
        if (wparam == VK_LEFT || wparam == VK_RIGHT) { collection->horizontal(wparam == VK_RIGHT, row_keyboard_gesture()); return 0; }
        if (wparam == VK_HOME || wparam == VK_END) { collection->edge(wparam == VK_END, row_keyboard_gesture()); return 0; }
        if (wparam == VK_PRIOR || wparam == VK_NEXT) {
            collection->step((wparam == VK_PRIOR ? -1 : 1) * std::max(1, static_cast<int>(collection->content_viewport().height / collection->item_size().height)) *
                static_cast<int>(collection->columns()), row_keyboard_gesture()); return 0;
        }
        if (wparam == VK_SPACE && collection->selection().focused()) {
            if (!(lparam & (1LL << 30))) collection->select(*collection->selection().focused(), SelectionGesture::toggle);
            return 0;
        }
        if ((wparam == VK_RETURN || wparam == VK_F2) && collection->selection().focused()) {
            if (!(lparam & (1LL << 30))) collection->activate_item(*collection->selection().focused(), wparam == VK_F2);
            return 0;
        }
        return {};
    }
    case grid_action_message: return accessibility_action(wparam, lparam);
    default: return {};
    }
}
}
