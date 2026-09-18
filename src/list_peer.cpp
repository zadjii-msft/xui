#include "list_peer.hpp"
#include "platform.hpp"
#include "context_menu.hpp"
#include <UIAutomation.h>
#include <windowsx.h>

namespace xui {
ListPeer::ListPeer(std::shared_ptr<FileList> control, std::function<void()> failure, std::function<void(HWND)> focus)
    : list_(std::move(control)), failure_(std::move(failure)), focus_(std::move(focus)) {
    list_->set_disposer([](std::shared_ptr<const FilteredView> view) { dispose_later(std::move(view)); });
}
ListPeer::~ListPeer() {
    detach_thumbnails();
    if (window_) DestroyWindow(window_);
    if (provider_) provider_->Release();
}
void ListPeer::detach_thumbnails() {
    thumbnails_.clear();
    thumbnail_source_.reset();
}
VisibleRange ListPeer::thumbnail_range(Rect clip) const {
    return visible_range(list_->model().visible_indices().size(), list_->row_height(),
        list_->offset() + std::max(0.0f, clip.y - list_->content_viewport(width()).y), clip.height);
}
bool ListPeer::sync_thumbnails(bool shown, Rect clip, const std::shared_ptr<TaskWake>& wake,
    std::vector<std::uint64_t>& retained, std::size_t& remaining) {
    if (!shown || !list_->thumbnails()) {
        const bool changed = !thumbnails_.empty(); detach_thumbnails(); return changed;
    }
    const auto view = list_->model().view();
    const auto source = view->source();
    const auto pixels = std::clamp(static_cast<UINT>(std::lround(24.0 * dpi_ / 96.0)), 1u,
        ImageLimits::output_dimension);
    if (thumbnail_source_.lock() != source || thumbnail_revision_ != list_->thumbnail_revision() ||
        thumbnail_pixels_ != pixels) {
        detach_thumbnails();
        thumbnail_source_ = source;
        thumbnail_revision_ = list_->thumbnail_revision();
        thumbnail_pixels_ = pixels;
    }
    // Bound each pane and the whole window, even on an unusually tall desktop.
    const auto limit = std::min<std::size_t>(24, remaining);
    const auto range = thumbnail_range(clip);
    const auto& model = list_->model();
    std::vector<const FileItem*> wanted;
    for (auto row = range.begin; row < range.end && wanted.size() < limit; ++row) {
        const auto& item = (*model.items())[model.visible_indices()[row]];
        wanted.push_back(&item);
    }
    const auto removed = std::erase_if(thumbnails_, [&](const auto& slot) {
        return std::none_of(wanted.begin(), wanted.end(), [&](const auto* item) {
            return slot->id == item->id && slot->path == item->path;
        });
    });
    bool changed = removed != 0;
    std::vector<std::pair<ItemId, std::wstring>> errors;
    for (const auto* item : wanted) {
        auto found = std::find_if(thumbnails_.begin(), thumbnails_.end(), [&](const auto& slot) {
            return slot->id == item->id && slot->path == item->path;
        });
        if (found == thumbnails_.end()) {
            auto slot = std::make_unique<Thumbnail>();
            slot->id = item->id;
            slot->path = item->path;
            slot->request = request_image(slot->path, {pixels, pixels}, wake,
                thumbnail_kind(item->path, item->directory));
            thumbnails_.push_back(std::move(slot));
            found = std::prev(thumbnails_.end());
        }
        auto& slot = **found;
        if (auto request = slot.request) {
            std::lock_guard lock(request->mutex);
            if (request->done && !request->cancelled) {
                slot.pixels = std::move(request->pixels);
                if (!slot.pixels) slot.error = request->error.empty() ?
                    L"Thumbnail decoding failed." : std::move(request->error);
                slot.request.reset();
                changed = true;
            }
        }
        if (!slot.error.empty() && !slot.reported) {
            errors.emplace_back(slot.id, slot.error);
            slot.reported = true;
        }
        if (slot.pixels) retained.push_back(slot.pixels->id);
    }
    remaining -= thumbnails_.size();
    for (const auto& [id, error] : errors) {
        if (list_->model().view() != view || !list_->thumbnails() ||
            list_->thumbnail_revision() != thumbnail_revision_) break;
        list_->thumbnail_error(id, error);
    }
    return changed;
}
void ListPeer::attach(HWND parent, int id) {
    if (list_->id() > (std::numeric_limits<WPARAM>::max() >> 8))
        throw std::overflow_error("List control identity exhausted");
    WNDCLASSEXW cls{sizeof(cls)};
    cls.style = CS_DBLCLKS;
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.lpfnWndProc = procedure;
    cls.lpszClassName = L"Xui.FileList.1";
    if (!RegisterClassExW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        win32_require(false, "Register list control");
    window_ = CreateWindowExW(WS_EX_TRANSPARENT, cls.lpszClassName, list_->name().c_str(),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 1, 1, parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), cls.hInstance, this);
    win32_require(window_ != nullptr, "Create list control");
    accessibility_->window = window_;
    provider_ = create_list_provider(accessibility_);
}
void ListPeer::invalidate() { if (window_) InvalidateRect(GetAncestor(window_, GA_ROOT), nullptr, FALSE); }
void ListPeer::update(UINT dpi, const Palette& palette) {
    set_theme(dpi, palette);
    if (!IsWindowEnabled(window_)) {
        palette_.text = palette_.secondary = palette_.selection_text = palette_.folder = palette_.file = palette_.disabled;
        hovered_.reset();
        hover_scrollbar_ = false;
        dragging_ = false;
        if (GetCapture() == window_) ReleaseCapture();
    }
    viewport();
    invalidate();
}
float ListPeer::width() const {
    RECT bounds{};
    GetClientRect(window_, &bounds);
    return bounds.right * 96.0f / dpi_;
}
void ListPeer::viewport() {
    RECT bounds{};
    GetClientRect(window_, &bounds);
    const float height = bounds.bottom * 96.0f / dpi_;
    if (height != list_->viewport_height()) list_->set_viewport_height(height);
    const auto content_height = list_->content_height();
    thumb_ = scroll_thumb(static_cast<float>(list_->model().visible_indices().size()) * list_->row_height(),
        content_height, list_->offset(), std::max(0.0f, content_height - 8));
    publish(published_ != list_->model().view().get());
}
void ListPeer::publish(bool structure) {
    if (!window_) return;
    AccessibleSnapshot snapshot;
    snapshot.control_id = list_->id();
    if (!name_ || *name_ != list_->name()) name_ = std::make_shared<const std::wstring>(list_->name());
    const auto id = list_->automation_id().empty() ? std::to_wstring(list_->id()) : list_->automation_id();
    if (!automation_id_ || *automation_id_ != id) automation_id_ = std::make_shared<const std::wstring>(id);
    snapshot.name = name_;
    snapshot.automation_id = automation_id_;
    if (list_->help_text().empty()) help_text_.reset();
    else if (!help_text_ || *help_text_ != list_->help_text()) help_text_ = std::make_shared<const std::wstring>(list_->help_text());
    snapshot.help_text = help_text_;
    snapshot.view = list_->model().view();
    snapshot.selected = list_->model().selected_index() ? list_->model().selected_id() : std::nullopt;
    snapshot.focused_item = list_->focused_index() ? list_->focused_id() : std::nullopt;
    GetClientRect(window_, &snapshot.screen_bounds);
    POINT origin{};
    ClientToScreen(window_, &origin);
    OffsetRect(&snapshot.screen_bounds, origin.x, origin.y);
    const auto viewport = list_->content_viewport(width());
    snapshot.row_right_inset_pixels = (width() - viewport.x - viewport.width) * dpi_ / 96.0f;
    snapshot.row_left_inset_pixels = viewport.x * dpi_ / 96.0f;
    snapshot.row_top_inset_pixels = viewport.y * dpi_ / 96.0f;
    snapshot.row_bottom_inset_pixels = (list_->viewport_height() - viewport.y - viewport.height) * dpi_ / 96.0f;
    snapshot.row_height_pixels = list_->row_height() * dpi_ / 96.0f;
    snapshot.offset_pixels = list_->offset() * dpi_ / 96.0f;
    snapshot.focused = GetFocus() == window_;
    snapshot.enabled = IsWindowEnabled(window_) != FALSE;
    const auto current_selection = snapshot.selected;
    const auto current_focus = snapshot.focused_item;
    AccessibleSnapshot previous;
    std::optional<ItemId> previous_selection, previous_focus;
    bool previous_keyboard_focus{};
    {
        std::lock_guard lock(accessibility_->mutex);
        previous_selection = accessibility_->snapshot.selected;
        previous_focus = accessibility_->snapshot.focused_item;
        previous_keyboard_focus = accessibility_->snapshot.focused;
        previous = std::move(accessibility_->snapshot);
        accessibility_->snapshot = std::move(snapshot);
    }
    raise_list_properties(provider_, accessibility_, previous);
    if (previous.view != list_->model().view()) dispose_later(std::move(previous.view));
    published_ = list_->model().view().get();
    if (structure) {
        hovered_.reset();
        if (provider_) raise_list_structure(provider_);
    }
    if (provider_ && previous_selection != current_selection) {
        if (const auto index = list_->model().selected_index()) raise_list_selection(provider_, accessibility_, *index);
        else if (previous_selection) {
            if (const auto removed = list_->model().find_visible(*previous_selection))
                raise_list_selection_removed(provider_, accessibility_, *removed);
        }
    }
    if (provider_ && GetFocus() == window_ &&
        (!previous_keyboard_focus || previous_focus != current_focus))
        raise_list_focus(provider_, accessibility_, list_->focused_index());
}
void ListPeer::changed() {
    hovered_.reset();
    viewport();
    invalidate();
}
bool ListPeer::select_at(int x, int y) {
    const auto viewport = list_->content_viewport(width());
    const auto local_x = x * 96.0f / dpi_ - viewport.x;
    const auto local_y = y * 96.0f / dpi_ - viewport.y;
    if (local_x < 0 || local_x >= viewport.width || local_y < 0 || local_y >= viewport.height) return false;
    const auto index = static_cast<size_t>((local_y + list_->offset()) / list_->row_height());
    if (index < list_->model().visible_indices().size()) {
        list_->select(index, false);
        changed();
        return true;
    } else { list_->clear_selection(); changed(); }
    return false;
}
bool ListPeer::in_scrollbar(LPARAM point) const {
    const auto viewport = list_->content_viewport(width());
    const auto x = GET_X_LPARAM(point) * 96.0f / dpi_, y = GET_Y_LPARAM(point) * 96.0f / dpi_;
    return x >= viewport.x + viewport.width && x < viewport.x + viewport.width + list_->scrollbar_width() &&
        y >= viewport.y && y < viewport.y + viewport.height;
}
void ListPeer::pointer_down(LPARAM point) {
    SetFocus(window_);
    if (!in_scrollbar(point)) { select_at(GET_X_LPARAM(point), GET_Y_LPARAM(point)); return; }
    if (thumb_.height <= 0) return;
    const float y = GET_Y_LPARAM(point) * 96.0f / dpi_ - list_->content_viewport(width()).y - 4;
    if (y >= thumb_.top && y < thumb_.top + thumb_.height) {
        SetCapture(window_);
        dragging_ = true;
        drag_offset_ = y - thumb_.top;
    } else {
        list_->scroll_to(list_->offset() + (y < thumb_.top ? -1 : 1) * list_->content_height());
        changed();
    }
    invalidate();
}
void ListPeer::pointer_move(LPARAM point) {
    pointer_ = Point{GET_X_LPARAM(point) * 96.0f / dpi_, GET_Y_LPARAM(point) * 96.0f / dpi_};
    list_->pointer_move(pointer_->x >= 0 && pointer_->x < width() && pointer_->y >= 0 && pointer_->y < list_->viewport_height());
    if (dragging_) {
        list_->scroll_to(scroll_from_thumb(thumb_, GET_Y_LPARAM(point) * 96.0f / dpi_ -
            list_->content_viewport(width()).y - 4 - drag_offset_));
        changed();
        return;
    }
    if (!tracking_) {
        TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, window_, 0};
        tracking_ = TrackMouseEvent(&tracking) != 0;
    }
    const bool scrollbar = in_scrollbar(point);
    std::optional<size_t> row;
    const auto viewport = list_->content_viewport(width());
    const auto x = GET_X_LPARAM(point) * 96.0f / dpi_, y = GET_Y_LPARAM(point) * 96.0f / dpi_ - viewport.y;
    if (!scrollbar && x >= viewport.x && x < viewport.x + viewport.width && y >= 0 && y < viewport.height) {
        const auto index = static_cast<size_t>((y + list_->offset()) / list_->row_height());
        if (index < list_->model().visible_indices().size()) row = index;
    }
    if (row != hovered_ || scrollbar != hover_scrollbar_) {
        hovered_ = row;
        hover_scrollbar_ = scrollbar;
        invalidate();
    }
}
void ListPeer::paint(Drawing& drawing) {
    const bool styled = list_->has_control_styling();
    const auto resolve = [&](StylePart part, StyleStateMask state = 0) {
        return styled ? list_->resolve_control_style_part(part, state) : PartStyleValues{};
    };
    const auto color = [&](std::optional<ThemeColor> value, D2D1_COLOR_F fallback) {
        return value && !palette_.high_contrast ? D2D1::ColorF(value->resolve(palette_.mode)) : fallback;
    };
    if (styled) drawing.styled_surface({0, 0, width(), list_->viewport_height()}, palette_, resolve(StylePart::root),
        palette_.surface, palette_.border, 0, {});
    else drawing.fill({0, 0, width(), list_->viewport_height()}, palette_.surface);
    const bool enabled = IsWindowEnabled(window_) != FALSE;
    const bool winui = palette_.style == VisualStyle::winui;
    const auto viewport = list_->content_viewport(width());
    const float area = viewport.width;
    if (styled) drawing.push_clip(viewport);
    const auto range = list_->visible_rows();
    rows_ = range.end - range.begin;
    const auto& model = list_->model();
    const auto items = model.items();
    auto hovered = hovered_;
    if (styled) {
        hovered.reset();
        if (pointer_ && enabled && !dragging_ && pointer_->x >= viewport.x && pointer_->x < viewport.x + viewport.width &&
            pointer_->y >= viewport.y && pointer_->y < viewport.y + viewport.height) {
            const auto index = static_cast<std::size_t>((pointer_->y - viewport.y + list_->offset()) / list_->row_height());
            if (index < model.visible_indices().size()) hovered = index;
        }
    }
    for (size_t row = range.begin; row < range.end; ++row) {
        const auto& item = (*items)[model.visible_indices()[row]];
        const Rect bounds{viewport.x, viewport.y + row * list_->row_height() - list_->offset(), area, list_->row_height()};
        const bool selected = model.selected_id() == item.id;
        const bool focused = GetFocus() == window_ && list_->focused_id() == item.id;
        const auto state = (selected ? style_states::selected : 0) | (focused ? style_states::focused : 0) |
            (!enabled ? style_states::disabled : hovered == row ? style_states::hovered : 0);
        const auto row_values = resolve(StylePart::row, state);
        const Rect highlight{bounds.x + 6, bounds.y + 2, std::max(0.0f, area - 12), std::max(0.0f, bounds.height - 4)};
        if (styled && (selected || hovered == row || row_values.background || row_values.border_brush || row_values.border_thickness))
            drawing.styled_surface(highlight, palette_, row_values, selected ? palette_.selection : hovered == row ? palette_.hover : palette_.surface,
                palette_.border, 4, {});
        else if (selected) drawing.rounded(highlight, palette_.selection, 4);
        else if (hovered == row) drawing.rounded(highlight, palette_.hover, 4);
        if (styled && selected) {
            const auto marker = resolve(StylePart::selected_marker, state);
            const auto definition = list_->control_style();
            if ((definition && definition->has_part(StylePart::selected_marker)) || !list_->control_style_values(StylePart::selected_marker).empty())
                drawing.styled_surface({highlight.x, highlight.y + 4, std::min(highlight.width, marker.size.value_or(3)),
                std::max(0.0f, highlight.height - 8)}, palette_, marker, color(marker.foreground, palette_.accent), palette_.accent, 1.5f, {});
        }
        if (focused) {
            if (styled) {
                const auto marker = resolve(StylePart::focus_marker, state);
                const auto definition = list_->control_style();
                const auto thickness = marker.size.value_or(1);
                if ((definition && definition->has_part(StylePart::focus_marker)) || !list_->control_style_values(StylePart::focus_marker).empty())
                    drawing.styled_surface(highlight, palette_, marker,
                        color(row_values.background, selected ? palette_.selection : palette_.surface), color(marker.foreground, palette_.accent),
                        4, {thickness, thickness, thickness, thickness});
            }
            if (palette_.style == VisualStyle::winui) drawing.focus_ring(highlight, palette_);
            else drawing.rounded(highlight, palette_.high_contrast && selected ?
                palette_.selection_text : palette_.accent, 4, true);
        }
        const auto text = winui && !enabled ? palette_.disabled : selected ? palette_.selection_text : palette_.text;
        const auto p = row_values.padding.value_or(Insets{}), border = row_values.border_thickness.value_or(Insets{});
        const auto left = std::min(bounds.width, p.left + border.left), top = std::min(bounds.height, p.top + border.top);
        const Rect content{bounds.x + left, bounds.y + top, std::max(0.0f, bounds.width - left - p.right - border.right),
            std::max(0.0f, bounds.height - top - p.bottom - border.bottom)};
        if (styled) drawing.push_clip(content);
        const float kind_width = content.width > 260 ? 100.0f : 0;
        const auto thumbnail = std::find_if(thumbnails_.begin(), thumbnails_.end(), [&](const auto& slot) {
            return slot->id == item.id && slot->path == item.path;
        });
        bool drawn{};
        if (thumbnail != thumbnails_.end() && (*thumbnail)->pixels) {
            drawn = drawing.image((*thumbnail)->pixels, {content.x + 11, content.y + (content.height - 24) / 2, 24, 24});
            if (!drawn && !(*thumbnail)->reported && (*thumbnail)->error.empty()) {
                (*thumbnail)->error = L"The thumbnail bitmap budget is full or the upload failed.";
                invalidate();
            }
        }
        if (!drawn) drawing.icon({content.x + 14, content.y + (content.height - 18) / 2 + (styled ? 0 : 1), 18, 18},
            color(resolve(StylePart::icon, state).foreground,
                winui && !enabled ? palette_.disabled : selected ? palette_.selection_text : item.directory ? palette_.folder : palette_.file), item.directory);
        const auto primary = resolve(StylePart::primary_text, state);
        const Rect text_bounds{content.x + 40, content.y, std::max(0.0f, content.width - kind_width - 52), content.height};
        if (styled) drawing.styled_text(item.name, text_bounds, color(primary.foreground, text), primary);
        else drawing.text(item.name, text_bounds, text);
        if (kind_width) {
            const auto secondary = resolve(StylePart::secondary_text, state);
            const Rect kind{content.x + content.width - kind_width, content.y, kind_width - 14, content.height};
            const auto ink = color(secondary.foreground, winui && !enabled ? palette_.disabled : selected ? palette_.selection_text : palette_.secondary);
            if (styled) drawing.styled_text(item.directory ? L"Folder" : L"File", kind, ink, secondary, TextStyle::caption);
            else drawing.text(item.directory ? L"Folder" : L"File", kind, ink, true);
        }
        if (styled) drawing.pop_clip();
    }
    if (model.visible_indices().empty()) {
        const float top = viewport.y + std::max(10.0f, viewport.height * 0.35f - 20);
        const auto values = resolve(StylePart::empty);
        if (styled) {
            drawing.styled_text(list_->empty_title(), {viewport.x + 24, top, std::max(0.0f, area - 48), 34}, color(values.foreground, palette_.text), values, TextStyle::heading);
            drawing.styled_text(list_->empty_detail(), {viewport.x + 24, top + 38, std::max(0.0f, area - 48), 24}, color(values.foreground, palette_.secondary), values, TextStyle::caption);
        } else {
            drawing.heading(list_->empty_title(), {24, top, std::max(0.0f, area - 48), 34}, palette_.text);
            drawing.text(list_->empty_detail(), {24, top + 38, std::max(0.0f, area - 48), 24}, palette_.secondary, true);
        }
    }
    if (styled) drawing.pop_clip();
    if (thumb_.height > 0) {
        const bool active = hover_scrollbar_ || dragging_;
        const auto bar_width = list_->scrollbar_width();
        const float thumb_width = active ? bar_width / 2 : bar_width / 4;
        const Rect thumb{viewport.x + area + (bar_width - thumb_width) / 2, viewport.y + 4 + thumb_.top, thumb_width, thumb_.height};
        if (styled) {
            drawing.styled_surface({viewport.x + area, viewport.y, bar_width, viewport.height}, palette_,
                resolve(StylePart::scrollbar_track), palette_.surface, palette_.border, 0, {});
            drawing.styled_surface(thumb, palette_, resolve(StylePart::scrollbar_thumb),
                active || palette_.high_contrast ? palette_.secondary : palette_.border, palette_.border, 3, {});
        } else if (winui) drawing.scrollbar_thumb(thumb, palette_, active, enabled);
        else drawing.rounded(thumb, active || palette_.high_contrast ? palette_.secondary : palette_.border, 3);
    }
}
LRESULT CALLBACK ListPeer::procedure(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) noexcept {
    auto* self = reinterpret_cast<ListPeer*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = static_cast<ListPeer*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        self->window_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return DefWindowProcW(hwnd, message, wparam, lparam);
    if (message == WM_CONTEXTMENU) {
        const auto failure = self->failure_;
        try { show_control_menu(*self->list_, hwnd, lparam, self->palette_, self->dpi_); }
        catch (...) { failure(); }
        return 0;
    }
    try { return self->message(hwnd, message, wparam, lparam); }
    catch (...) { self->failure_(); return 0; }
}
LRESULT ListPeer::message(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: ValidateRect(hwnd, nullptr); return 0;
    case WM_GETOBJECT:
        if (static_cast<LONG>(lparam) == UiaRootObjectId && provider_)
            return UiaReturnRawElementProvider(hwnd, wparam, lparam, provider_);
        break;
    case WM_SIZE: viewport(); return 0;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        if (message == WM_SETFOCUS) focus_(hwnd);
        list_->set_focused(message == WM_SETFOCUS);
        publish();
        invalidate();
        return 0;
    case WM_LBUTTONDOWN: if (IsWindowEnabled(hwnd)) pointer_down(lparam); return 0;
    case WM_LBUTTONDBLCLK:
        if (IsWindowEnabled(hwnd) && !in_scrollbar(lparam)) {
            if (select_at(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam))) list_->activate_selected(FileActivation::double_click);
        }
        return 0;
    case WM_MOUSEMOVE: if (IsWindowEnabled(hwnd)) pointer_move(lparam); return 0;
    case WM_MOUSELEAVE:
        tracking_ = false; hovered_.reset(); pointer_.reset(); hover_scrollbar_ = false;
        list_->pointer_move(false); invalidate(); return 0;
    case WM_LBUTTONUP:
    case WM_CANCELMODE:
        dragging_ = false;
        if (GetCapture() == hwnd) ReleaseCapture();
        invalidate();
        return 0;
    case WM_CAPTURECHANGED: dragging_ = false; invalidate(); return 0;
    case WM_RBUTTONDOWN:
        SetFocus(hwnd);
        if (!in_scrollbar(lparam)) select_at(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
        else list_->clear_selection();
        return 0;
    case WM_KEYDOWN: {
        if (!IsWindowEnabled(hwnd)) return 0;
        std::optional<Navigation> navigation;
        switch (wparam) {
        case VK_RETURN:
            if (!(lparam & (1LL << 30))) list_->activate_selected(FileActivation::enter);
            return 0;
        case VK_UP: navigation = Navigation::previous; break;
        case VK_DOWN: navigation = Navigation::next; break;
        case VK_HOME: navigation = Navigation::first; break;
        case VK_END: navigation = Navigation::last; break;
        case VK_PRIOR: navigation = Navigation::page_up; break;
        case VK_NEXT: navigation = Navigation::page_down; break;
        }
        if (navigation) { list_->navigate(*navigation); changed(); return 0; }
        break;
    }
    case WM_MOUSEWHEEL: {
        if (!IsWindowEnabled(hwnd)) return 0;
        UINT lines = 3;
        SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
        wheel_delta_ += GET_WHEEL_DELTA_WPARAM(wparam);
        const int ticks = wheel_delta_ / WHEEL_DELTA;
        wheel_delta_ %= WHEEL_DELTA;
        list_->scroll_to(list_->offset() - ticks * (lines == WHEEL_PAGESCROLL ?
            list_->content_height() : lines * list_->row_height()));
        changed();
        return 0;
    }
    case accessibility_action_message: {
        if ((wparam >> 8) != list_->id()) return FALSE;
        if (!IsWindowEnabled(hwnd)) return -2;
        const auto action = static_cast<AccessibilityAction>(wparam & 255);
        if (action == AccessibilityAction::focus_list) {
            list_->focus_list(); SetFocus(hwnd); publish();
            return TRUE;
        }
        if (action == AccessibilityAction::scroll_percent) {
            if (lparam < 0 || lparam > 10000) return FALSE;
            const float extent = std::max(0.0f, list_->model().visible_indices().size() *
                list_->row_height() - list_->content_height());
            list_->scroll_to(extent * static_cast<float>(lparam) / 10000);
            changed(); return TRUE;
        }
        const auto found = list_->model().find_visible(static_cast<ItemId>(lparam));
        if (!found) return FALSE;
        const auto index = *found;
        switch (action) {
        case AccessibilityAction::add_selection:
            if (list_->model().selected_index() && list_->model().selected_id() != static_cast<ItemId>(lparam)) return -1;
            [[fallthrough]];
        case AccessibilityAction::select: list_->select(index); changed(); return TRUE;
        case AccessibilityAction::remove_selection:
            if (list_->model().selected_id() == static_cast<ItemId>(lparam)) {
                list_->clear_selection(); changed();
            }
            return TRUE;
        case AccessibilityAction::focus_item:
            list_->focus_item(index); SetFocus(hwnd); changed();
            return TRUE;
        case AccessibilityAction::reveal: list_->reveal(index); changed(); return TRUE;
        default: return FALSE;
        }
    }
    case WM_NCDESTROY:
        list_->set_focused(false);
        {
            std::lock_guard lock(accessibility_->mutex);
            accessibility_->window = nullptr;
            dispose_later(std::move(accessibility_->snapshot.view));
        }
        if (provider_) UiaDisconnectProvider(provider_);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        window_ = nullptr;
        break;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}
}
