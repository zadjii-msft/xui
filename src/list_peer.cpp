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
namespace {
bool image_path(std::wstring_view path) {
    const auto dot = path.find_last_of(L'.');
    if (dot == path.npos) return false;
    const auto extension = path.substr(dot);
    for (const auto supported : {L".png", L".jpg", L".jpeg", L".bmp", L".gif", L".tif", L".tiff", L".webp"})
        if (CompareStringOrdinal(extension.data(), static_cast<int>(extension.size()),
            supported, -1, TRUE) == CSTR_EQUAL) return true;
    return false;
}
}
bool ListPeer::sync_thumbnails(bool shown, Rect clip, const std::shared_ptr<TaskWake>& wake,
    std::vector<std::uint64_t>& retained, std::size_t& remaining) {
    if (!shown || !list_->thumbnails()) { detach_thumbnails(); return false; }
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
    const auto range = visible_range(list_->model().visible_indices().size(), list_->row_height(),
        list_->offset() + std::max(0.0f, clip.y), clip.height);
    const auto& model = list_->model();
    std::vector<const FileItem*> wanted;
    for (auto row = range.begin; row < range.end && wanted.size() < limit; ++row) {
        const auto& item = (*model.items())[model.visible_indices()[row]];
        wanted.push_back(&item);
    }
    std::erase_if(thumbnails_, [&](const auto& slot) {
        return std::none_of(wanted.begin(), wanted.end(), [&](const auto* item) {
            return slot->id == item->id && slot->path == item->path;
        });
    });
    bool changed{};
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
                !item->directory && image_path(item->path) ? ImageKind::wic : ImageKind::shell);
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
    thumb_ = scroll_thumb(static_cast<float>(list_->model().visible_indices().size()) * list_->row_height(),
        height, list_->offset(), std::max(0.0f, height - 8));
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
    snapshot.row_right_inset_pixels = VisualMetrics::gutter * dpi_ / 96.0f;
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
void ListPeer::select_at(int y) {
    if (y < 0) return;
    const auto index = static_cast<size_t>((y * 96.0f / dpi_ + list_->offset()) / list_->row_height());
    if (index < list_->model().visible_indices().size()) {
        list_->select(index, false);
        changed();
    } else { list_->clear_selection(); changed(); }
}
bool ListPeer::in_scrollbar(LPARAM point) const {
    return GET_X_LPARAM(point) * 96.0f / dpi_ >= width() - VisualMetrics::gutter;
}
void ListPeer::pointer_down(LPARAM point) {
    SetFocus(window_);
    if (!in_scrollbar(point)) { select_at(GET_Y_LPARAM(point)); return; }
    if (thumb_.height <= 0) return;
    const float y = GET_Y_LPARAM(point) * 96.0f / dpi_ - 4;
    if (y >= thumb_.top && y < thumb_.top + thumb_.height) {
        SetCapture(window_);
        dragging_ = true;
        drag_offset_ = y - thumb_.top;
    } else {
        list_->scroll_to(list_->offset() + (y < thumb_.top ? -1 : 1) * list_->viewport_height());
        changed();
    }
    invalidate();
}
void ListPeer::pointer_move(LPARAM point) {
    if (dragging_) {
        list_->scroll_to(scroll_from_thumb(thumb_, GET_Y_LPARAM(point) * 96.0f / dpi_ - 4 - drag_offset_));
        changed();
        return;
    }
    if (!tracking_) {
        TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, window_, 0};
        tracking_ = TrackMouseEvent(&tracking) != 0;
    }
    const bool scrollbar = in_scrollbar(point);
    std::optional<size_t> row;
    if (!scrollbar && GET_Y_LPARAM(point) >= 0) {
        const auto index = static_cast<size_t>((GET_Y_LPARAM(point) * 96.0f / dpi_ + list_->offset()) / list_->row_height());
        if (index < list_->model().visible_indices().size()) row = index;
    }
    if (row != hovered_ || scrollbar != hover_scrollbar_) {
        hovered_ = row;
        hover_scrollbar_ = scrollbar;
        invalidate();
    }
}
void ListPeer::paint(Drawing& drawing) {
    drawing.fill({0, 0, width(), list_->viewport_height()}, palette_.surface);
    const float area = std::max(0.0f, width() - VisualMetrics::gutter);
    const auto range = list_->visible_rows();
    rows_ = range.end - range.begin;
    const auto& model = list_->model();
    const auto items = model.items();
    for (size_t row = range.begin; row < range.end; ++row) {
        const auto& item = (*items)[model.visible_indices()[row]];
        const Rect bounds{0, row * list_->row_height() - list_->offset(), area, list_->row_height()};
        const bool selected = model.selected_id() == item.id;
        const Rect highlight{6, bounds.y + 2, std::max(0.0f, area - 12), bounds.height - 4};
        if (selected) drawing.rounded(highlight, palette_.selection, 4);
        else if (hovered_ == row) drawing.rounded(highlight, palette_.hover, 4);
        if (GetFocus() == window_ && list_->focused_id() == item.id)
            drawing.rounded(highlight, palette_.high_contrast && selected ?
                palette_.selection_text : palette_.accent, 4, true);
        const auto text = selected ? palette_.selection_text : palette_.text;
        const float kind_width = area > 260 ? 100.0f : 0;
        const auto thumbnail = std::find_if(thumbnails_.begin(), thumbnails_.end(), [&](const auto& slot) {
            return slot->id == item.id && slot->path == item.path;
        });
        bool drawn{};
        if (thumbnail != thumbnails_.end() && (*thumbnail)->pixels) {
            drawn = drawing.image((*thumbnail)->pixels, {11, bounds.y + 4, 24, 24});
            if (!drawn && !(*thumbnail)->reported && (*thumbnail)->error.empty()) {
                (*thumbnail)->error = L"The thumbnail bitmap budget is full or the upload failed.";
                invalidate();
            }
        }
        if (!drawn) drawing.icon({14, bounds.y + 8, 18, 18},
            selected ? palette_.selection_text : item.directory ? palette_.folder : palette_.file, item.directory);
        drawing.text(item.name, {40, bounds.y, std::max(0.0f, area - kind_width - 52), bounds.height}, text);
        if (kind_width) drawing.text(item.directory ? L"Folder" : L"File",
            {area - kind_width, bounds.y, kind_width - 14, bounds.height},
            selected ? palette_.selection_text : palette_.secondary, true);
    }
    if (model.visible_indices().empty()) {
        const float top = std::max(10.0f, list_->viewport_height() * 0.35f - 20);
        drawing.heading(list_->empty_title(), {24, top, std::max(0.0f, area - 48), 34}, palette_.text);
        drawing.text(list_->empty_detail(), {24, top + 38, std::max(0.0f, area - 48), 24}, palette_.secondary, true);
    }
    if (thumb_.height > 0) {
        const bool active = hover_scrollbar_ || dragging_;
        drawing.rounded({area + (active ? 4 : 6), 4 + thumb_.top,
            active ? 8.0f : 4.0f, thumb_.height},
            active || palette_.high_contrast ? palette_.secondary : palette_.border, 3);
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
            select_at(GET_Y_LPARAM(lparam));
            list_->activate_selected(FileActivation::double_click);
        }
        return 0;
    case WM_MOUSEMOVE: if (IsWindowEnabled(hwnd)) pointer_move(lparam); return 0;
    case WM_MOUSELEAVE:
        tracking_ = false; hovered_.reset(); hover_scrollbar_ = false; invalidate(); return 0;
    case WM_LBUTTONUP:
    case WM_CANCELMODE:
        dragging_ = false;
        if (GetCapture() == hwnd) ReleaseCapture();
        invalidate();
        return 0;
    case WM_CAPTURECHANGED: dragging_ = false; invalidate(); return 0;
    case WM_RBUTTONDOWN:
        SetFocus(hwnd);
        if (!in_scrollbar(lparam)) select_at(GET_Y_LPARAM(lparam));
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
            list_->viewport_height() : lines * list_->row_height()));
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
                list_->row_height() - list_->viewport_height());
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
