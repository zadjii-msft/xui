#include "xui/application.hpp"
#include "xui/native_edit.hpp"
#include "native_document.hpp"
#include "native_runtime_host.hpp"
#include "control_accessibility.hpp"
#include "window_host.hpp"
#include "platform.hpp"
#include "list_peer.hpp"
#include "context_menu.hpp"
#include "async.hpp"
#include "images.hpp"
#include "workspace_accessibility.hpp"
#include "xui/data_grid.hpp"
#include "xui/adaptive_layout.hpp"
#include "xui/commands.hpp"
#include "xui/shell_commands.hpp"
#include "xui/titlebar.hpp"
#include "xui/navigation.hpp"
#include "xui/map_view.hpp"
#include <dwmapi.h>
#include "grid_accessibility.hpp"
#include <UIAutomation.h>
#include <commctrl.h>
#include <windowsx.h>
#include <cmath>
#include <utility>

namespace xui {
namespace {
constexpr UINT update_message = WM_APP + 12;
constexpr UINT metrics_message = WM_APP + 60;
constexpr UINT_PTR tooltip_timer = 41, repeat_timer = 42;
constexpr wchar_t window_class[] = L"Xui.Window.1";
constexpr wchar_t control_class[] = L"Xui.Control.1";
thread_local bool running{};
}

struct Window::Impl : std::enable_shared_from_this<Window::Impl> {
    struct Peer {
        Impl& host;
        std::shared_ptr<Control> control;
        HWND window{}, caption{};
        std::wstring caption_text;
        std::unique_ptr<NativeEditBridge> edit;
        std::shared_ptr<Button> clear_button;
        HWND clear_window{};
        std::weak_ptr<Control> clear_owner;
        std::unique_ptr<NativeDocumentBridge> document;
        std::unique_ptr<NativeRuntimeHost> runtime;
        bool native() const { return edit || document; }
        std::unique_ptr<ListPeer> list;
        std::unique_ptr<ImagePeer> image;
        std::unique_ptr<RowImages> row_images;
        std::shared_ptr<ControlAccessibility> accessibility = std::make_shared<ControlAccessibility>();
        IRawElementProviderSimple* provider{};
        std::shared_ptr<ControlAccessibility> caption_accessibility;
        IRawElementProviderSimple* caption_provider{};
        bool tracking{}, surface{};
        bool repeating{}, repeat_cycle{};
        bool native_occluded{};
        bool suppress_popup_click{};
        std::wstring typeahead;
        ULONGLONG typeahead_time{};
        Rect paint_bounds{};
        RECT placed_bounds{};
        bool placed{};
        Peer* parent{};
        Microsoft::WRL::ComPtr<IDWriteTextLayout> text_layout;
        bool dragging{};
        float drag_y{}, drag_offset{};
        int wheel_remainder{};
        int grid_drag{};
        std::size_t grid_drop{};
        std::size_t grid_column{};
        bool collection_drag{}, collection_scroll{}, collection_additive{};
        std::optional<ItemKey> collection_anchor;
        std::optional<Point> command_pointer;
        std::optional<Point> tab_pointer;
        std::optional<std::size_t> hovered_choice;
        std::optional<std::uint64_t> pressed_choice;
        CollectionSelection collection_before;
        AdaptiveLayout* adaptive{};
        Peer(Impl& owner, std::shared_ptr<Control> value) : host(owner), control(std::move(value)) {}
        ~Peer() {
            runtime.reset();
            if (list) window = nullptr;
            if (window && IsWindow(window)) DestroyWindow(window);
            if (caption && IsWindow(caption)) DestroyWindow(caption);
            disconnect_control(accessibility, provider);
            if (provider) provider->Release();
            if (caption_accessibility) disconnect_control(caption_accessibility, caption_provider);
            if (caption_provider) caption_provider->Release();
        }
    };

    WindowOptions options;
    std::wstring error;
    std::shared_ptr<Stack> root;
    std::shared_ptr<TitleBar> titlebar;
    bool caption_active{};
    std::vector<std::unique_ptr<Peer>> peers;
    std::vector<AdaptiveLayout*> adaptive_layouts;
    std::vector<HWND> focus_targets;
    struct PopupEntry {
        std::shared_ptr<Popup> popup;
        std::shared_ptr<Control> anchor;
        HWND return_focus{};
        std::uint64_t parent{};
        std::shared_ptr<CommandSurface> commands;
        std::shared_ptr<LocationPicker> location;
        std::weak_ptr<CommandMenu> parent_command;
        CommandId parent_command_id{};
        std::shared_ptr<ContentDialog> dialog;
        std::optional<float> combo_alignment;
        Size combo_alignment_size{};
        std::optional<Rect> context_anchor;
    };
    std::vector<PopupEntry> popups;
    bool composing_native{};
    bool keyboard_focus_visible{};
    std::weak_ptr<Control> hovered_edit;
    HWND hovered_edit_source{};
    static constexpr float command_shadow_extent = 20;
    Rect popup_occlusion(const PopupEntry& entry) const {
        auto bounds = entry.popup->bounds();
        if (entry.dialog && palette.style == VisualStyle::winui && !palette.high_contrast) return root->bounds();
        if ((entry.commands || palette.style == VisualStyle::winui) && !palette.high_contrast)
            return {bounds.x - command_shadow_extent, bounds.y - command_shadow_extent,
                bounds.width + 2 * command_shadow_extent, bounds.height + 2 * command_shadow_extent};
        return bounds;
    }
    unsigned input_depth{};
    std::uint64_t tooltip_target{};
    bool tooltip_shown{};
    Rect tooltip_bounds{};
    HWND window{}, last_focus{};
    UINT dpi{96};
    Drawing drawing;
    Palette palette{};
    HBRUSH background{}, field{};
    HFONT font{};
    bool used{}, pending{}, layout_pending{}, ready{}, syncing{}, failed{}, quit_posted{}, attached{}, closing{}, destroying{};
    std::uint64_t paints{}, layouts{};
    std::shared_ptr<TaskWake> wake = std::make_shared<TaskWake>();
    std::vector<std::shared_ptr<ViewTask::Impl>> tasks;
    std::vector<std::shared_ptr<SampleTask::Impl>> samples;
    std::function<bool(const KeyEvent&)> key;
    std::mutex post_mutex;
    std::vector<std::function<void()>> posts;
    bool posts_closed{};
    void close_posts() {
        std::vector<std::function<void()>> removed;
        { std::lock_guard lock(post_mutex); posts_closed = true; removed.swap(posts); }
    }
    std::function<bool(const NavigationEvent&)> navigation;
    bool has_images{};
    const DWORD owner_thread = GetCurrentThreadId();

    explicit Impl(WindowOptions value) : options(std::move(value)) {
        if (options.visual_style < VisualStyle::classic || options.visual_style > VisualStyle::winui)
            throw std::invalid_argument("Invalid visual style");
        if (options.custom_titlebar) titlebar = std::make_shared<TitleBar>(options.title);
    }
    ~Impl() { teardown(); }
    void destroy() {
        if (destroying) return;
        destroying = true;
        closing = true;
        close_posts();
        hide_tooltip();
        if (!popups.empty()) {
            try { auto popup = popups.front().popup; dismiss_popup(*popup, PopupDismissReason::owner_closed, false); }
            catch (...) { failed = true; error = L"A popup dismissal callback failed."; }
        }
        drawing.discard();
        if (window) DestroyWindow(window);
        destroying = false;
    }
    void detach() {
        if (!attached) return;
        root->set_invalidator({});
        for (const auto& peer : peers) {
            peer->control->set_text_measurer({});
            if (auto label = std::dynamic_pointer_cast<Label>(peer->control)) label->set_wrapped_text_measurer({});
        }
        for (const auto& peer : peers)
            if (auto combo = std::dynamic_pointer_cast<ComboBox>(peer->control)) combo->choices()->on_accept({});
        for (const auto& peer : peers) if (peer->image) peer->image->detach();
        for (const auto& peer : peers) if (peer->row_images) peer->row_images->clear();
        for (const auto& peer : peers) if (auto* map = dynamic_cast<MapView*>(peer->control.get())) map->cancel_request();
        for (const auto& peer : peers) if (peer->list) peer->list->detach_thumbnails();
        if (has_images) clear_image_cache();
        attached = false;
    }
    struct InputScope {
        Impl& host;
        explicit InputScope(Impl& value) : host(value) { ++host.input_depth; }
        ~InputScope() { --host.input_depth; }
    };
    void teardown() {
        for (auto& task : samples) task->cancel();
        for (auto& task : tasks) task->cancel();
        detach();
        ready = false;
        destroy();
        if (!input_depth) peers.clear();
        drawing.release();
        if (background) { DeleteObject(background); background = nullptr; }
        if (field) { DeleteObject(field); field = nullptr; }
        if (font) { DeleteObject(font); font = nullptr; }
    }
    void fail() noexcept {
        failed = true;
        try {
            try { throw; }
            catch (const std::exception& failure) { error = exception_message(failure); }
            catch (...) { error = L"A window callback failed."; }
        } catch (...) {}
        // Callback failures cannot escape a Windows procedure or strand the message loop.
        OutputDebugStringW(L"XUI: The window callback failed. Closing the window.\n");
        destroy();
    }
    void invalidate(Invalidation kind) {
        layout_pending = layout_pending || kind == Invalidation::layout;
        if (!window || !ready) return;
        if (!pending) {
            pending = true;
            win32_require(PostMessageW(window, update_message, 0, 0) != 0, "Schedule control update");
        }
    }
    static LRESULT CALLBACK procedure(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) noexcept {
        auto* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
            self->window = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (!self) return DefWindowProcW(hwnd, message, wparam, lparam);
        try { return self->message(hwnd, message, wparam, lparam); }
        catch (...) { self->fail(); return 0; }
    }
    static LRESULT CALLBACK control_procedure(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) noexcept {
        auto* peer = reinterpret_cast<Peer*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            peer = static_cast<Peer*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
            peer->window = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(peer));
        }
        if (!peer) return DefWindowProcW(hwnd, message, wparam, lparam);
        auto* host = &peer->host;
        InputScope scope(*host);
        try { return peer->host.control_message(*peer, hwnd, message, wparam, lparam); }
        catch (...) { host->fail(); return 0; }
    }
    static LRESULT CALLBACK native_clip_procedure(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam,
        UINT_PTR id, DWORD_PTR data) noexcept {
        auto& peer = *reinterpret_cast<Peer*>(data);
        InputScope scope(peer.host);
        if (message == WM_SETFOCUS && !peer.host.enabled(peer)) {
            try { peer.host.traverse(false); } catch (...) { peer.host.fail(); }
            return 0;
        }
        if (!peer.host.enabled(peer) && (message == WM_KEYDOWN || message == WM_CHAR ||
            message == WM_LBUTTONDOWN || message == WM_LBUTTONUP || message == WM_RBUTTONDOWN ||
            message == WM_MOUSEWHEEL || message == WM_CONTEXTMENU)) return 0;
        const bool caption = hwnd == peer.caption;
        auto* provider = caption ? peer.caption_provider : peer.provider;
        if ((message == WM_PRINTCLIENT || message == WM_PRINT) && peer.native_occluded && !peer.host.composing_native) {
            const auto dc = reinterpret_cast<HDC>(wparam);
            const auto region = CreateRectRgn(0, 0, 0, 0);
            const auto saved = SaveDC(dc);
            if (!region || !saved) {
                if (region) DeleteObject(region);
                if (saved) RestoreDC(dc, saved);
                return 0;
            }
            if (GetWindowRgn(hwnd, region) != ERROR) {
                POINT viewport{}, origin{}; GetViewportOrgEx(dc, &viewport); GetWindowOrgEx(dc, &origin);
                OffsetRgn(region, viewport.x - origin.x, viewport.y - origin.y);
                ExtSelectClipRgn(dc, region, RGN_AND);
            }
            const auto result = DefSubclassProc(hwnd, message, wparam, lparam);
            RestoreDC(dc, saved); DeleteObject(region);
            return result;
        }
        // RichEdit supplies its own server-side Text provider. Replacing it with
        // an override provider would hide its native Text/Text2 patterns.
        if (message == WM_GETOBJECT && static_cast<LONG>(lparam) == UiaRootObjectId &&
            peer.control->role() == ControlRole::document_text) return DefSubclassProc(hwnd, message, wparam, lparam);
        if (message == WM_GETOBJECT && static_cast<LONG>(lparam) == UiaRootObjectId && provider)
            return UiaReturnRawElementProvider(hwnd, wparam, lparam, provider);
        if (message == control_action_message) {
            if (wparam != peer.control->id()) return UIA_E_ELEMENTNOTAVAILABLE;
            if (caption) return UIA_E_INVALIDOPERATION;
            if (!peer.host.enabled(peer)) return UIA_E_ELEMENTNOTENABLED;
            try { return peer.host.focus(peer, true) ? S_OK : UIA_E_INVALIDOPERATION; }
            catch (...) { peer.host.fail(); return E_FAIL; }
        }
        if (message == WM_NCDESTROY) {
            RemoveWindowSubclass(hwnd, native_clip_procedure, id);
            disconnect_control(caption ? peer.caption_accessibility : peer.accessibility, provider);
            UiaReturnRawElementProvider(hwnd, 0, 0, nullptr);
        }
        return DefSubclassProc(hwnd, message, wparam, lparam);
    }
    std::optional<LRESULT> navigation_message(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
        if (!navigation || !IsWindowEnabled(window)) return {};
        if (std::any_of(popups.begin(), popups.end(), [](const auto& entry) { return bool(entry.dialog); }) &&
            (message == WM_APPCOMMAND || message == WM_XBUTTONDOWN || message == WM_XBUTTONUP || message == WM_XBUTTONDBLCLK)) return TRUE;
        NavigationDirection direction{};
        std::optional<Point> position;
        HWND source = hwnd;
        if (message == WM_XBUTTONDOWN || message == WM_XBUTTONDBLCLK || message == WM_XBUTTONUP) {
            const auto button = GET_XBUTTON_WPARAM(wparam);
            if (button != XBUTTON1 && button != XBUTTON2) return {};
            // Suppress DefWindowProc's application command on button-down. Release dispatches once.
            if (message != WM_XBUTTONUP) return TRUE;
            direction = button == XBUTTON1 ? NavigationDirection::back : NavigationDirection::forward;
            POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            MapWindowPoints(hwnd, window, &point, 1);
            position = Point{point.x * 96.0f / dpi, point.y * 96.0f / dpi};
        } else if (message == WM_APPCOMMAND) {
            const auto command = GET_APPCOMMAND_LPARAM(lparam);
            if (command != APPCOMMAND_BROWSER_BACKWARD && command != APPCOMMAND_BROWSER_FORWARD) return {};
            direction = command == APPCOMMAND_BROWSER_BACKWARD ? NavigationDirection::back : NavigationDirection::forward;
            const auto origin = reinterpret_cast<HWND>(wparam);
            if (origin == window || IsChild(window, origin)) source = origin;
            if (GET_DEVICE_LPARAM(lparam) == FAPPCOMMAND_MOUSE) {
                const auto coordinates = GetMessagePos();
                POINT point{GET_X_LPARAM(coordinates), GET_Y_LPARAM(coordinates)};
                ScreenToClient(window, &point);
                position = Point{point.x * 96.0f / dpi, point.y * 96.0f / dpi};
            } else if (source == window && IsChild(window, GetFocus())) source = GetFocus();
        } else return {};
        Control* target{};
        for (const auto& peer : peers)
            if (peer->window == source || peer->caption == source) { target = peer->control.get(); break; }
        if (position) {
            for (auto it = peers.rbegin(); it != peers.rend(); ++it) {
                const auto bounds = (*it)->control->bounds();
                if (visible(**it) && position->x >= bounds.x && position->x < bounds.x + bounds.width &&
                    position->y >= bounds.y && position->y < bounds.y + bounds.height) {
                    target = (*it)->control.get();
                    break;
                }
            }
        }
        auto callback = navigation;
        const bool handled = callback({direction, target, position});
        // A release belongs to the button-down consumed above, even without available history.
        if (handled || message == WM_XBUTTONUP) return TRUE;
        return {};
    }
    static LRESULT CALLBACK navigation_procedure(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam,
        UINT_PTR id, DWORD_PTR data) noexcept {
        auto& peer = *reinterpret_cast<Peer*>(data);
        InputScope scope(peer.host);
        if (message == WM_SETFOCUS && !peer.host.enabled(peer)) {
            try { peer.host.traverse(false); } catch (...) { peer.host.fail(); }
            return 0;
        }
        if (!peer.host.enabled(peer) && (message == WM_KEYDOWN || message == WM_CHAR ||
            message == WM_LBUTTONDOWN || message == WM_LBUTTONUP || message == WM_RBUTTONDOWN ||
            message == WM_MOUSEWHEEL || message == WM_CONTEXTMENU)) return 0;
        if (peer.document && (message == WM_SETFOCUS || message == WM_KILLFOCUS)) {
            try {
                peer.control->set_focused(message == WM_SETFOCUS);
                if (message == WM_SETFOCUS) { peer.host.last_focus = hwnd; peer.host.reveal(peer); }
            } catch (...) { peer.host.fail(); return 0; }
        }
        if (peer.host.titlebar) {
            if (message == WM_NCHITTEST) {
                const auto hit = peer.host.caption_hit(peer.host.window, lparam);
                if (hit != HTCLIENT) return hit;
            }
            if (peer.host.is_caption_button(*peer.control)) {
                try {
                    if (message == WM_NCMOUSEMOVE) {
                        peer.control->pointer_move(wparam == HTMINBUTTON || wparam == HTMAXBUTTON || wparam == HTCLOSE);
                        TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE | TME_NONCLIENT, hwnd, 0};
                        win32_require(TrackMouseEvent(&track) != 0, "Track caption pointer");
                    } else if (message == WM_NCMOUSELEAVE) {
                        peer.control->pointer_move(false);
                    } else if ((message == WM_NCLBUTTONDOWN || message == WM_NCLBUTTONDBLCLK) &&
                        (wparam == HTMINBUTTON || wparam == HTMAXBUTTON || wparam == HTCLOSE)) {
                        if (peer.host.enabled(peer) && peer.host.visible(peer)) {
                            peer.host.hide_tooltip();
                            peer.control->pointer_move(true);
                            if (peer.control->pointer_down()) SetCapture(hwnd);
                        }
                        return 0;
                    }
                } catch (...) { peer.host.fail(); return 0; }
            }
            if (message == WM_NCLBUTTONDOWN || message == WM_NCLBUTTONUP || message == WM_NCLBUTTONDBLCLK ||
                message == WM_NCRBUTTONUP || message == WM_NCMOUSEMOVE || message == WM_NCMOUSELEAVE) {
                LRESULT result{};
                if (DwmDefWindowProc(peer.host.window, message, wparam, lparam, &result)) return result;
                return DefWindowProcW(peer.host.window, message, wparam, lparam);
            }
        }
        if (message == WM_LBUTTONDOWN) {
            peer.suppress_popup_click = !peer.host.popups.empty() && peer.host.popups.back().anchor == peer.control;
            try { peer.host.light_dismiss(&peer); }
            catch (...) { peer.host.fail(); return 0; }
        }
        if (message == WM_SETFOCUS || message == WM_MOUSEMOVE) {
            try { peer.host.offer_tooltip(peer); }
            catch (...) { peer.host.fail(); return 0; }
        }
        if (message == WM_MOUSEMOVE && !peer.tracking) {
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd, 0};
            peer.tracking = TrackMouseEvent(&track) != 0;
        }
        if (message == WM_MOUSEMOVE && peer.host.options.visual_style == VisualStyle::winui) {
            try { peer.host.hover_edit(peer.host.edit_at(hwnd, lparam), hwnd); }
            catch (...) { peer.host.fail(); return 0; }
        }
        if (message == WM_MOUSELEAVE && peer.host.hovered_edit_source == hwnd) {
            try { peer.host.hover_edit(nullptr, nullptr); }
            catch (...) { peer.host.fail(); return 0; }
        }
        if (message == WM_MOUSELEAVE) peer.tracking = false;
        if (message == WM_KILLFOCUS || message == WM_MOUSELEAVE) peer.host.hide_tooltip();
        if (message == WM_KILLFOCUS) {
            try { peer.host.focus_departing(reinterpret_cast<HWND>(wparam)); }
            catch (...) { peer.host.fail(); return 0; }
        }
        if (message == WM_CONTEXTMENU && peer.edit && peer.control->has_context_menu()) {
            if (peer.edit->composing()) return 0;
            auto* host = &peer.host;
            try { show_control_menu(*peer.control, hwnd, lparam, host->palette, host->dpi); }
            catch (...) { host->fail(); }
            return 0;
        }
        try {
            if (auto result = peer.host.navigation_message(hwnd, message, wparam, lparam)) return *result;
        } catch (...) { peer.host.fail(); return 0; }
        if (message == WM_NCDESTROY) RemoveWindowSubclass(hwnd, navigation_procedure, id);
        if (peer.edit && (message == WM_IME_STARTCOMPOSITION || message == WM_IME_ENDCOMPOSITION)) {
            auto* host = &peer.host;
            const auto result = DefSubclassProc(hwnd, message, wparam, lparam);
            host->invalidate(Invalidation::layout);
            return result;
        }
        if (peer.edit && peer.parent && peer.parent->control->role() == ControlRole::numeric_input &&
            peer.control->visual_style() == VisualStyle::winui && !peer.edit->composing() &&
            (message == WM_SETFOCUS || (message == WM_LBUTTONDOWN && GetFocus() != hwnd))) {
            const auto input = peer.control;
            const auto result = DefSubclassProc(hwnd, message, wparam, lparam);
            if (GetFocus() == hwnd && input->visual_style() == VisualStyle::winui)
                SendMessageW(hwnd, EM_SETSEL, 0, -1);
            return result;
        }
        return DefSubclassProc(hwnd, message, wparam, lparam);
    }
    void collect(const std::shared_ptr<Element>& element, bool surface = false, Peer* parent = nullptr, AdaptiveLayout* adaptive = nullptr) {
        if (auto stack = std::dynamic_pointer_cast<Stack>(element)) {
            if (auto pages = std::dynamic_pointer_cast<PageView>(stack)) {
                if (pages->child_count()) collect(pages->child_at(pages->selected()), surface || stack->surface(), parent, adaptive);
                return;
            }
            auto* layout = dynamic_cast<AdaptiveLayout*>(stack.get());
            if (layout && std::find(adaptive_layouts.begin(), adaptive_layouts.end(), layout) == adaptive_layouts.end()) adaptive_layouts.push_back(layout);
            for (size_t i = 0; i < stack->child_count(); ++i)
                collect(stack->child_at(i), surface || stack->surface(), parent, layout && stack->child_at(i) == layout->navigation() ? layout : adaptive);
            return;
        }
        auto control = std::dynamic_pointer_cast<Control>(element);
        if (!control) throw std::invalid_argument("Window content supports Stack and standard controls only");
        control->set_visual_style(options.visual_style);
        for (size_t i = 0; i < peers.size(); ++i) if (peers[i]->control == control) {
            auto* peer = peers[i].get();
            peer->surface = surface;
            peer->adaptive = adaptive;
            if (auto scroll = std::dynamic_pointer_cast<ScrollView>(control)) collect(scroll->content(), surface, peer, adaptive);
            if (auto content = std::dynamic_pointer_cast<ContentView>(control)) collect(content->content(), surface, peer, adaptive);
            if (auto split = std::dynamic_pointer_cast<SplitView>(control)) {
                collect(split->first(), surface, peer, adaptive);
                collect(split->second(), surface, peer, adaptive);
            }
            for (const auto& child : control->retained_children())
                collect(child, surface || (options.visual_style == VisualStyle::winui && control->role() == ControlRole::expander), peer, adaptive);
            collect_clear_button(*peer);
            return;
        }
        auto peer = std::make_unique<Peer>(*this, std::move(control));
        peer->surface = surface;
        peer->adaptive = adaptive;
        peer->parent = parent;
        const HWND native_parent = parent ? parent->window : window;
        const auto role = peer->control->role();
        if (role == ControlRole::file_list) {
            auto list = std::dynamic_pointer_cast<FileList>(peer->control);
            if (!list) throw std::invalid_argument("The file-list role requires a FileList control");
            peer->list = std::make_unique<ListPeer>(std::move(list), [this] { fail(); },
                [this, target = peer.get()](HWND hwnd) { last_focus = hwnd; reveal(*target); });
            peer->list->attach(native_parent, static_cast<int>(100 + peers.size()));
            peer->window = peer->list->window();
        } else if (role == ControlRole::document_text || role == ControlRole::password_input || role == ControlRole::date_time) {
            peer->document = std::make_unique<NativeDocumentBridge>(peer->control);
            peer->document->on_failure([this] { fail(); });
            peer->document->attach(native_parent, static_cast<int>(100 + peers.size()));
            peer->window = peer->document->window();
            publish_control(peer->accessibility, nullptr, *peer->control, peer->window);
            peer->provider = create_native_clip_provider(peer->accessibility);
            win32_require(SetWindowSubclass(peer->window, native_clip_procedure, 2,
                reinterpret_cast<DWORD_PTR>(peer.get())) != 0, "Attach native document clipping provider");
        } else if (role == ControlRole::text_input) {
            auto& input = static_cast<TextInput&>(*peer->control);
            // The preceding native STATIC supplies EDIT's accessible name.
            peer->caption = CreateWindowExW(0, L"STATIC", peer->control->name().c_str(),
                WS_CHILD | (input.caption_visible() ? WS_VISIBLE : 0) | SS_LEFT | SS_NOPREFIX, 0, 0, 1, 1, native_parent,
                nullptr, GetModuleHandleW(nullptr), nullptr);
            win32_require(peer->caption != nullptr, "Create text input label");
            peer->caption_text = input.name();
            peer->edit = std::make_unique<NativeEditBridge>();
            peer->edit->set_font_family(drawing.edit_font_family());
            peer->edit->on_failure([this] { fail(); });
            peer->edit->set_placeholder(input.placeholder());
            peer->edit->set_insets({input.search_style() ? 40.0f : 12.0f, 10,
                input.shortcut_hint().empty() ? 12.0f : 78.0f, 10});
            peer->edit->attach(native_parent, static_cast<int>(100 + peers.size()));
            peer->window = peer->edit->window();
            {
                publish_control(peer->accessibility, nullptr, *peer->control, peer->window);
                peer->provider = create_native_clip_provider(peer->accessibility);
                win32_require(SetWindowSubclass(peer->window, native_clip_procedure, 2,
                    reinterpret_cast<DWORD_PTR>(peer.get())) != 0, "Attach native input focus and clipping provider");
                peer->caption_accessibility = std::make_shared<ControlAccessibility>();
                peer->caption_accessibility->snapshot.window = peer->caption;
                peer->caption_accessibility->snapshot.id = peer->control->id();
                peer->caption_provider = create_native_clip_provider(peer->caption_accessibility);
                win32_require(SetWindowSubclass(peer->caption, native_clip_procedure, 2,
                    reinterpret_cast<DWORD_PTR>(peer.get())) != 0, "Attach native caption clipping provider");
            }
        } else {
            const DWORD tab = peer->control->focusable() && peer->control->tab_stop() && !is_caption_button(*peer->control) ? WS_TABSTOP : 0;
            peer->window = CreateWindowExW(WS_EX_TRANSPARENT, control_class, peer->control->name().c_str(),
                WS_CHILD | WS_VISIBLE | tab, 0, 0, 1, 1, native_parent,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(100 + peers.size())),
                GetModuleHandleW(nullptr), peer.get());
            win32_require(peer->window != nullptr, "Create control");
            publish_control(peer->accessibility, nullptr, *peer->control, peer->window);
            peer->provider = role == ControlRole::data_grid ? create_grid_provider(peer->accessibility) :
                role == ControlRole::items_view || role == ControlRole::tree_view || role == ControlRole::command_menu ? create_collection_provider(peer->accessibility) :
                role == ControlRole::tab_strip || role == ControlRole::split_view || role >= ControlRole::popup ?
                create_workspace_provider(peer->accessibility) : create_control_provider(peer->accessibility);
        }
        if (auto runtime = std::dynamic_pointer_cast<RuntimeHost>(peer->control))
            peer->runtime = std::make_unique<NativeRuntimeHost>(runtime, peer->window, [this] { fail(); }, [this] {
                return !closing && popups.empty() && std::none_of(adaptive_layouts.begin(), adaptive_layouts.end(),
                    [](const auto* layout) { return layout->overlay_active(); });
            });
        win32_require(SetWindowSubclass(peer->window, navigation_procedure, 3,
            reinterpret_cast<DWORD_PTR>(peer.get())) != 0, "Attach browser navigation input");
        if (peer->caption)
            win32_require(SetWindowSubclass(peer->caption, navigation_procedure, 3,
                reinterpret_cast<DWORD_PTR>(peer.get())) != 0, "Attach caption navigation input");
        auto* added = peer.get();
        if (role == ControlRole::image) {
            peer->image = std::make_unique<ImagePeer>(static_cast<Image&>(*peer->control));
            has_images = true;
        }
        if (role == ControlRole::label || role == ControlRole::button || role == ControlRole::toggle)
            peer->control->set_text_measurer([this, added](std::wstring_view text, TextStyle style) {
                Size measured{};
                added->text_layout = drawing.layout(text, style, measured);
                return measured;
            });
        if (role == ControlRole::radio_group || role == ControlRole::choice_list || role == ControlRole::expander)
            peer->control->set_text_measurer([this](std::wstring_view text, TextStyle style) {
                Size measured{};
                drawing.layout(text, style, measured);
                return measured;
            });
        if (auto label = std::dynamic_pointer_cast<Label>(peer->control))
            label->set_wrapped_text_measurer([this, added](std::wstring_view text, TextStyle style, float width, std::size_t lines) {
                Size measured{};
                added->text_layout = drawing.layout(text, style, measured, width, lines);
                return measured;
            });
        peers.push_back(std::move(peer));
        if (auto scroll = std::dynamic_pointer_cast<ScrollView>(added->control))
            collect(scroll->content(), surface, added, adaptive);
        if (auto content = std::dynamic_pointer_cast<ContentView>(added->control))
            collect(content->content(), surface, added, adaptive);
        if (auto split = std::dynamic_pointer_cast<SplitView>(added->control)) {
            collect(split->first(), surface, added, adaptive);
            collect(split->second(), surface, added, adaptive);
        }
        for (const auto& child : added->control->retained_children())
            collect(child, surface || (options.visual_style == VisualStyle::winui && role == ControlRole::expander), added, adaptive);
        collect_clear_button(*added);
    }
    bool can_clear(const Peer& peer) const {
        if (!peer.edit || options.visual_style != VisualStyle::winui || peer.edit->composing()) return false;
        const auto& input = static_cast<const TextInput&>(*peer.control);
        return !input.search_style() && !input.shortcut_visible(input.bounds().width) &&
            (!peer.parent || (peer.parent->control->role() != ControlRole::numeric_input &&
                peer.parent->control->role() != ControlRole::combo_box));
    }
    void sync_clear_button(Peer& peer) {
        if (!peer.clear_button) return;
        const auto& input = static_cast<const TextInput&>(*peer.control);
        const bool focused = GetFocus() == peer.window || (peer.clear_window && GetFocus() == peer.clear_window);
        const bool show = can_clear(peer) && enabled(peer) && input.visible() && focused &&
            !input.text().empty() && input.bounds().width >= 5 * VisualMetrics::body_size;
        peer.clear_button->set_visible(show);
        peer.clear_button->set_enabled(show);
        const auto bounds = input.bounds();
        const float header = input.caption_extent();
        peer.clear_button->arrange({bounds.x + std::max(0.0f, bounds.width - 31), bounds.y + header + 1,
            std::min(30.0f, bounds.width), std::max(0.0f, bounds.height - header - 2)});
    }
    void collect_clear_button(Peer& peer) {
        if (!peer.edit) return;
        if (!peer.clear_button && can_clear(peer) && GetFocus() == peer.window &&
            !static_cast<const TextInput&>(*peer.control).text().empty()) {
            peer.clear_button = std::make_shared<Button>(L"Clear " + peer.control->name());
            peer.clear_button->set_icon(ButtonIcon::close);
            peer.clear_button->set_appearance(ButtonAppearance::subtle);
            peer.clear_button->set_tab_stop(false);
            peer.clear_button->set_automation_id(L"xui-text-clear-" + (peer.control->automation_id().empty() ?
                std::to_wstring(peer.control->id()) : peer.control->automation_id()));
            peer.clear_button->set_invalidator([this](Invalidation kind) { invalidate(kind); });
            std::weak_ptr<Control> owner = peer.control;
            peer.clear_button->on_click([this, owner] {
                const auto control = owner.lock();
                auto* input_peer = control ? find_peer(control.get()) : nullptr;
                if (!input_peer || !can_clear(*input_peer) || !enabled(*input_peer) || !visible(*input_peer)) return;
                const auto edit = input_peer->window;
                input_peer->edit->focus(false);
                if (closing || !window || !IsWindow(edit) || GetFocus() != edit) return;
                SendMessageW(edit, EM_SETSEL, 0, -1);
                SendMessageW(edit, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L""));
            });
        }
        if (!peer.clear_button) return;
        sync_clear_button(peer);
        // The native EDIT owns only its text line. The affordance is a sibling in the same clipped surface.
        collect(peer.clear_button, peer.surface, peer.parent, peer.adaptive);
        if (!peer.clear_window) {
            auto* clear_peer = find_peer(peer.clear_button.get());
            clear_peer->clear_owner = peer.control;
            peer.clear_window = clear_peer->window;
        }
    }
    Peer* find_peer(const Control* control) const {
        for (const auto& peer : peers) if (peer->control.get() == control) return peer.get();
        return nullptr;
    }
    std::uint64_t popup_owner(const Peer* peer) const {
        for (; peer; peer = peer->parent) if (peer->control->role() == ControlRole::popup) return peer->control->id();
        return 0;
    }
    bool in_top_popup(const Peer& peer) const {
        return popups.empty() || popup_owner(&peer) == popups.back().popup->id();
    }
    std::uint64_t adaptive_owner(const AdaptiveLayout* layout) const {
        for (const auto& peer : peers) if (peer->adaptive == layout) return popup_owner(peer.get());
        return 0;
    }
    void hide_tooltip() {
        if (window) KillTimer(window, tooltip_timer);
        tooltip_target = 0;
        if (std::exchange(tooltip_shown, false)) invalidate(Invalidation::paint);
    }
    void sync_native_occlusion() {
        struct Region { HRGN handle; ~Region() { if (handle) DeleteObject(handle); } };
        for (const auto& peer : peers) {
            if (!peer->native() || (popups.empty() && adaptive_layouts.empty() && !tooltip_shown && !peer->native_occluded)) continue;
            bool clipped{};
            for (const auto hwnd : {peer->window, peer->caption}) {
                if (!hwnd) continue;
                RECT rect{};
                GetWindowRect(hwnd, &rect);
                Region region{CreateRectRgn(0, 0, rect.right - rect.left, rect.bottom - rect.top)};
                Region existing{CreateRectRgn(0, 0, 0, 0)};
                win32_require(region.handle && existing.handle, "Create native occlusion regions");
                bool occluded{};
                const auto subtract = [&](Rect bounds) {
                    const float scale = dpi / 96.0f;
                    POINT points[2]{{static_cast<LONG>(std::lround(bounds.x * scale)), static_cast<LONG>(std::lround(bounds.y * scale))},
                        {static_cast<LONG>(std::lround((bounds.x + bounds.width) * scale)), static_cast<LONG>(std::lround((bounds.y + bounds.height) * scale))}};
                    MapWindowPoints(window, nullptr, points, 2);
                    const RECT overlay{points[0].x, points[0].y, points[1].x, points[1].y};
                    RECT overlap{};
                    if (!IntersectRect(&overlap, &rect, &overlay)) return;
                    Region cut{CreateRectRgn(overlap.left - rect.left, overlap.top - rect.top, overlap.right - rect.left, overlap.bottom - rect.top)};
                    win32_require(cut.handle != nullptr, "Create native occlusion cut");
                    win32_require(CombineRgn(region.handle, region.handle, cut.handle, RGN_DIFF) != ERROR, "Clip native window under popup");
                    occluded = true;
                };
                const auto owner = popup_owner(peer.get());
                for (const auto* layout : adaptive_layouts)
                    if (layout->overlay_active() && adaptive_owner(layout) == owner && peer->adaptive != layout) subtract(layout->navigation()->bounds());
                bool above = owner == 0;
                for (const auto& entry : popups) {
                    if (above) subtract(popup_occlusion(entry));
                    if (entry.popup->id() == owner) above = true;
                }
                if (tooltip_shown) subtract(tooltip_bounds);
                const auto existing_type = GetWindowRgn(hwnd, existing.handle);
                if (!occluded) {
                    if (existing_type != ERROR) win32_require(SetWindowRgn(hwnd, nullptr, TRUE) != 0, "Restore native window region");
                } else if (existing_type == ERROR || !EqualRgn(existing.handle, region.handle)) {
                    win32_require(SetWindowRgn(hwnd, region.handle, TRUE) != 0, "Set native window region");
                    region.handle = nullptr;
                }
                clipped = clipped || occluded;
            }
            peer->native_occluded = clipped;
        }
    }
    void offer_tooltip(Peer& peer) {
        for (const auto& item : peers) if (item->runtime && item->runtime->active()) { hide_tooltip(); return; }
        if (!window || !ready || !onscreen(peer) || !enabled(peer) || !in_top_popup(peer) ||
            peer.control->help_text().empty() || !IsWindowVisible(window)) return;
        if (tooltip_target == peer.control->id()) return;
        hide_tooltip(); tooltip_target = peer.control->id();
        win32_require(SetTimer(window, tooltip_timer, peer.control->tooltip_delay(), nullptr) != 0, "Start tooltip delay");
    }
    void dismiss_popup(Popup& popup, PopupDismissReason reason, bool restore = true) {
        InputScope input_scope(*this);
        auto it = std::find_if(popups.begin(), popups.end(), [&](const auto& entry) { return entry.popup.get() == &popup; });
        if (it == popups.end()) return;
        const auto index = static_cast<std::size_t>(it - popups.begin());
        const auto return_focus = it->return_focus;
        std::vector<PopupEntry> closed(popups.begin() + index, popups.end());
        popups.erase(popups.begin() + index, popups.end());
        hide_tooltip();
        std::vector<std::function<void()>> callbacks;
        for (auto rit = closed.rbegin(); rit != closed.rend(); ++rit) {
            if (rit->commands) rit->commands->cancel();
            if (rit->location) rit->location->navigation()->cancel();
            rit->popup->set_invalidator({});
            callbacks.push_back(rit->popup->close_transition(reason));
            if (rit->dialog) {
                rit->dialog->bind_close({});
                callbacks.push_back([dialog = rit->dialog, reason] {
                    dialog->notify_result(reason == PopupDismissReason::commit ? DialogResult::primary : DialogResult::cancel);
                });
            }
            if (auto parent = rit->parent_command.lock(); parent && parent->expanded() == rit->parent_command_id)
                parent->set_expanded({});
        }
        std::exception_ptr failure;
        const auto notify = [&](auto&& callback) {
            try { callback(); } catch (...) { if (!failure) failure = std::current_exception(); }
        };
        std::vector<Peer*> closing_peers;
        for (const auto& entry : closed) {
            for (const auto& peer : peers)
                if (popup_owner(peer.get()) == entry.popup->id()) closing_peers.push_back(peer.get());
        }
        for (auto* peer : closing_peers) {
            const auto owner = std::find_if(closed.begin(), closed.end(), [&](const auto& entry) { return entry.popup->id() == popup_owner(peer); });
            if (owner != closed.end() && owner->popup->is_open()) continue;
            if (auto range = std::dynamic_pointer_cast<RangeInput>(peer->control)) notify([&] { range->cancel_drag(); });
            if (owner != closed.end() && owner->popup->is_open()) continue;
            peer->control->cancel();
            if (peer->edit) peer->edit->dismiss_suggestions();
            KillTimer(peer->window, repeat_timer); peer->repeating = false;
            ShowWindow(peer->window, SW_HIDE);
            if (GetCapture() == peer->window) ReleaseCapture();
        }
        invalidate(Invalidation::layout);
        if (window && !closing)
            for (const auto& peer : peers)
                if ((IsWindowEnabled(peer->window) != FALSE) != enabled(*peer)) EnableWindow(peer->window, enabled(*peer));
        for (auto& callback : callbacks) if (callback) notify(callback);
        if (restore && window && !closing && popups.size() == index && IsWindow(return_focus) &&
            IsChild(window, return_focus) && IsWindowVisible(return_focus) && IsWindowEnabled(return_focus)) SetFocus(return_focus);
        if (failure) std::rethrow_exception(failure);
    }
    void light_dismiss(Peer* target) {
        if (popups.empty() || (target && in_top_popup(*target))) return;
        for (const auto& entry : popups) if (entry.dialog && (!target || !enabled(*target))) return;
        const auto owner = target ? popup_owner(target) : 0;
        dismiss_above(owner, PopupDismissReason::outside);
    }
    void dismiss_above(std::uint64_t owner, PopupDismissReason reason) {
        std::size_t index{};
        if (owner) {
            const auto it = std::find_if(popups.begin(), popups.end(), [owner](const auto& entry) { return entry.popup->id() == owner; });
            if (it == popups.end()) return;
            index = static_cast<std::size_t>(it - popups.begin()) + 1;
        }
        if (index >= popups.size()) return;
        auto popup = popups[index].popup;
        dismiss_popup(*popup, reason, false);
    }
    void focus_departing(HWND target) {
        if (popups.empty() || !target || !IsChild(window, target)) return;
        Peer* destination{};
        for (const auto& peer : peers) if (peer->window == target) { destination = peer.get(); break; }
        if (destination && in_top_popup(*destination)) return;
        for (const auto& entry : popups) if (entry.dialog && (!destination || !enabled(*destination))) {
            traverse(false); return;
        }
        const auto owner = destination ? popup_owner(destination) : 0;
        dismiss_above(owner, PopupDismissReason::focus_lost);
    }
    void repeat_start(Peer& peer, bool activate) {
        auto* button = dynamic_cast<Button*>(peer.control.get());
        if (!button || button->behavior() != ButtonBehavior::repeat || !enabled(peer) || !visible(peer)) return;
        peer.repeat_cycle = true;
        if (!peer.repeating) {
            win32_require(SetTimer(peer.window, repeat_timer, button->repeat_delay(), nullptr) != 0, "Start button repeat");
            peer.repeating = true;
        }
        if (activate) activated(peer, button->invoke());
    }
    void show_popup(std::shared_ptr<Popup> popup, Control& anchor, Control* initial, std::shared_ptr<ContentDialog> dialog = {},
        std::shared_ptr<CommandSurface> commands = {}, std::optional<Rect> context_anchor = {}) {
        if (!popup) throw std::invalid_argument("Popup is required");
        if (popup->dialog_surface() && !dialog) throw std::invalid_argument("Use Window::show_dialog for dialog content");
        if (!ready || closing || !window) throw std::logic_error("Popup requires a running window");
        if (popup->is_open()) throw std::logic_error("Popup is already open");
        for (const auto& peer : peers) if (peer->runtime && peer->runtime->active())
            throw std::logic_error("Unload native media and web content before opening an XUI popup");
        auto* anchor_peer = find_peer(&anchor);
        if (!anchor_peer || !enabled(*anchor_peer) || !onscreen(*anchor_peer))
            throw std::invalid_argument("Popup anchor must be an enabled visible control in this window");
        const auto owner = popup_owner(anchor_peer);
        if (popups.size() >= 8) throw std::length_error("At most eight nested popups are supported");
        if (!popups.empty() && owner != popups.back().popup->id())
            throw std::logic_error("A nested popup must be anchored in the top popup");
        hide_tooltip();
        popup->opened();
        popup->set_invalidator([this](Invalidation kind) { invalidate(kind); });
        popups.push_back({popup, anchor_peer->control, GetFocus(), owner});
        popups.back().dialog = std::move(dialog);
        popups.back().commands = std::move(commands);
        popups.back().context_anchor = context_anchor;
        layout_pending = true;
        update();
        if (!window || !popup->is_open()) return;
        if (initial) {
            auto* peer = find_peer(initial);
            if (!peer || popup_owner(peer) != popup->id() || !focus(*peer, false)) {
                dismiss_popup(*popup, PopupDismissReason::cancel);
                throw std::invalid_argument("Initial popup focus must be a visible enabled popup child");
            }
        } else {
            for (const auto& peer : peers)
                if (popup_owner(peer.get()) == popup->id() && peer->control->tab_stop() && focus(*peer, false)) return;
            SetFocus(find_peer(popup.get())->window);
        }
    }
    void open_combo(Peer& peer) {
        auto combo = std::dynamic_pointer_cast<ComboBox>(peer.control);
        if (!combo || !enabled(peer)) return;
        if (combo->popup()->is_open()) { dismiss_popup(*combo->popup(), PopupDismissReason::cancel); return; }
        collect(combo->popup(), true);
        combo->prepare_popup();
        std::weak_ptr<ComboBox> weak = combo;
        combo->choices()->on_accept([this, weak](std::uint64_t id) {
            const auto value = weak.lock();
            if (!value || !value->popup()->is_open()) return;
            auto popup = value->popup();
            dismiss_popup(*popup, PopupDismissReason::commit);
            if (!closing && window) value->select(id);
        });
        show_popup(combo->popup(), *combo, combo->choices().get());
    }
    void prune_popups() {
        if (input_depth) return;
        std::set<std::uint64_t> live;
        const auto visit = [&](const auto& self, const std::shared_ptr<Element>& element) -> void {
            if (!element) return;
            live.insert(element->id());
            if (auto stack = std::dynamic_pointer_cast<Stack>(element))
                for (std::size_t i = 0; i < stack->child_count(); ++i) self(self, stack->child_at(i));
            if (auto scroll = std::dynamic_pointer_cast<ScrollView>(element)) self(self, scroll->content());
            if (auto content = std::dynamic_pointer_cast<ContentView>(element)) self(self, content->content());
            if (auto split = std::dynamic_pointer_cast<SplitView>(element)) {
                self(self, split->first()); self(self, split->second());
            }
            if (auto control = std::dynamic_pointer_cast<Control>(element)) {
                for (const auto& child : control->retained_children()) self(self, child);
            }
        };
        visit(visit, root);
        for (const auto& entry : popups) visit(visit, entry.popup);
        for (const auto& peer : peers)
            if (peer->clear_button && live.contains(peer->control->id())) live.insert(peer->clear_button->id());
        // Remove leaves first; no peer can retain a pointer to a deleted parent.
        for (std::size_t i = peers.size(); i-- > 0;) {
            const auto owner = popup_owner(peers[i].get());
            if (!live.contains(peers[i]->control->id()) ||
                (owner && std::none_of(popups.begin(), popups.end(), [owner](const auto& e) { return e.popup->id() == owner; }))) {
                peers[i]->control->set_text_measurer({});
                if (auto label = std::dynamic_pointer_cast<Label>(peers[i]->control)) label->set_wrapped_text_measurer({});
                if (peers[i]->image) peers[i]->image->detach();
                if (peers[i]->list) peers[i]->list->detach_thumbnails();
                peers.erase(peers.begin() + i);
            }
        }
    }
    void create() {
        if (used) throw std::logic_error("A Window can run only once");
        if (closing) throw std::logic_error("The Window is closed");
        used = true;
        if (!root) throw std::logic_error("Window content is required");
        drawing.initialize(options.visual_style);
        WNDCLASSEXW cls{sizeof(cls)};
        cls.hInstance = GetModuleHandleW(nullptr);
        cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        cls.lpfnWndProc = procedure;
        cls.lpszClassName = window_class;
        if (!RegisterClassExW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            win32_require(false, "Register application window");
        cls.lpfnWndProc = control_procedure;
        cls.style = CS_DBLCLKS;
        cls.lpszClassName = control_class;
        if (!RegisterClassExW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            win32_require(false, "Register control window");
        const auto extent = [](float value, int fallback) {
            return std::isfinite(value) && value > 0 ? static_cast<int>(std::clamp(value, 240.0f, 16000.0f)) : fallback;
        };
        const DWORD style = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;
        const UINT initial_dpi = GetDpiForSystem();
        RECT outer{0, 0, MulDiv(extent(options.size.width, 600), initial_dpi, 96),
            MulDiv(extent(options.size.height, 520), initial_dpi, 96)};
        win32_require(AdjustWindowRectExForDpi(&outer, style, FALSE, WS_EX_CONTROLPARENT, initial_dpi) != 0, "Size application window");
        window = CreateWindowExW(WS_EX_CONTROLPARENT, window_class, options.title.c_str(),
            style, CW_USEDEFAULT, CW_USEDEFAULT,
            outer.right - outer.left, outer.bottom - outer.top, nullptr,
            nullptr, GetModuleHandleW(nullptr), this);
        win32_require(window != nullptr, "Create application window");
        dpi = GetDpiForWindow(window);
        if (titlebar) {
            std::weak_ptr<Impl> weak = shared_from_this();
            titlebar->on_caption([weak](CaptionAction action) {
                if (auto self = weak.lock(); self && self->window) {
                    const WPARAM command = action == CaptionAction::close ? SC_CLOSE : action == CaptionAction::minimize ? SC_MINIMIZE :
                        IsZoomed(self->window) ? SC_RESTORE : SC_MAXIMIZE;
                    PostMessageW(self->window, WM_SYSCOMMAND, command, 0);
                }
            });
            SetWindowPos(window, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        }
        collect(root);
        root->set_invalidator([this](Invalidation kind) { invalidate(kind); });
        attached = true;
        ready = true;
        apply_theme();
        layout_pending = true;
        update();
        ShowWindow(window, SW_SHOWNORMAL);
        if (!IsChild(window, GetFocus())) platform::traverse_focus(focus_targets, false);
    }
    void apply_theme() {
        if (!window) return;
        cancel_control_menu(window);
        if (options.visual_style != VisualStyle::winui) hover_edit(nullptr, nullptr);
        palette = Palette::system(options.theme, options.visual_style);
        drawing.set_visual_style(options.visual_style);
        platform::appearance(window, options.theme, palette);
        HBRUSH next_background = CreateSolidBrush(platform::native_color(palette.background));
        HBRUSH next_field = CreateSolidBrush(platform::native_color(palette.field));
        if (!next_background || !next_field) {
            if (next_background) DeleteObject(next_background);
            if (next_field) DeleteObject(next_field);
            throw std::runtime_error("Create control theme brushes");
        }
        if (background) DeleteObject(background);
        if (field) DeleteObject(field);
        background = next_background;
        field = next_field;
        const auto caption_size = options.visual_style == VisualStyle::winui ? VisualMetrics::body_size : VisualMetrics::caption_size;
        HFONT next_font = CreateFontW(-MulDiv(static_cast<int>(caption_size), dpi, 96), 0, 0, 0, FW_NORMAL,
            FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH, drawing.edit_font_family());
        win32_require(next_font != nullptr, "Create input caption font");
        for (const auto& peer : peers) {
            if (peer->list) peer->list->set_theme(dpi, palette);
            if (peer->document) {
                peer->document->update(dpi, palette);
                publish_control(peer->accessibility, nullptr, *peer->control, peer->window);
            } else if (peer->edit) {
                peer->edit->set_font_family(drawing.edit_font_family());
                peer->edit->set_dpi(dpi);
                SendMessageW(peer->caption, WM_SETFONT, reinterpret_cast<WPARAM>(next_font), TRUE);
                InvalidateRect(peer->window, nullptr, FALSE);
            }
        }
        if (font) DeleteObject(font);
        font = next_font;
        invalidate(Invalidation::paint);
    }
    void update() {
        if (!ready || syncing || !window) return;
        syncing = true;
        struct Reset { bool& value; ~Reset() { value = false; } } reset{syncing};
        pending = false;
        prune_popups();
        InputScope input_scope(*this);
        for (const auto& entry : popups) if (!command_popup_current(entry)) {
            auto popup = entry.popup;
            dismiss_popup(*popup, PopupDismissReason::hidden);
            break;
        }
        if (!ready || !window || closing) return;
        const auto focus_before_layout = GetFocus();
        const auto before = peers.size();
        adaptive_layouts.clear();
        collect(root);
        for (const auto& entry : popups) {
            if (entry.dialog) entry.dialog->set_visual_style(options.visual_style);
            collect(entry.popup, !entry.popup->window_background());
        }
        if (peers.size() != before) apply_theme();
        if (layout_pending) {
            layout_pending = false;
            RECT client{};
            win32_require(GetClientRect(window, &client) != 0, "Read content bounds");
            const Size size{client.right * 96.0f / dpi, client.bottom * 96.0f / dpi};
            root->measure(size);
            root->arrange({0, 0, size.width, size.height});
            // Content panes now have this frame's geometry, including splitter and sidebar changes.
            if (titlebar && titlebar->has_tab_panes()) titlebar->arrange(titlebar->bounds());
            Rect popup_viewport{0, 0, size.width, size.height};
            MONITORINFO monitor{sizeof(monitor)};
            if (GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor)) {
                MapWindowPoints(nullptr, window, reinterpret_cast<POINT*>(&monitor.rcWork), 2);
                const float left = std::max(0.0f, monitor.rcWork.left * 96.0f / dpi);
                const float top = std::max(0.0f, monitor.rcWork.top * 96.0f / dpi);
                popup_viewport = {left, top, std::max(0.0f, std::min(size.width, monitor.rcWork.right * 96.0f / dpi) - left),
                    std::max(0.0f, std::min(size.height, monitor.rcWork.bottom * 96.0f / dpi) - top)};
            }
            for (auto& entry : popups) {
                const bool winui_dialog = entry.dialog && options.visual_style == VisualStyle::winui;
                auto viewport = winui_dialog ? Rect{0, 0, size.width, size.height} : popup_viewport;
                const bool palette_surface = entry.commands && entry.commands->editor();
                if (palette_surface || winui_dialog) {
                    const auto margin = std::min(winui_dialog ? 24.0f : command_shadow_extent, std::min(viewport.width, viewport.height) / 4);
                    viewport = {viewport.x + margin, viewport.y + margin, viewport.width - 2 * margin, viewport.height - 2 * margin};
                }
                const Size available{viewport.width, viewport.height};
                const auto desired = winui_dialog ? entry.dialog->measure(size) :
                    entry.commands ? entry.commands->measure(available) : entry.popup->measure(available);
                if (desired.width > 0 && desired.height > 0) {
                    if (winui_dialog)
                        entry.popup->arrange({viewport.x + (viewport.width - desired.width) / 2,
                            viewport.y + (viewport.height - desired.height) / 2, desired.width, desired.height});
                    else if (palette_surface)
                        entry.popup->arrange({viewport.x + (viewport.width - desired.width) / 2,
                            viewport.y + std::min(64.0f, std::max(0.0f, viewport.height - desired.height)), desired.width, desired.height});
                    else {
                        auto placed = place_popup(entry.context_anchor.value_or(entry.anchor->bounds()), desired, viewport, entry.popup->placement());
                        entry.popup->arrange(placed);
                        if (const auto combo = std::dynamic_pointer_cast<ComboBox>(entry.anchor);
                            combo && !combo->editor() && options.visual_style == VisualStyle::winui) {
                            const auto choices = combo->choices();
                            if (!entry.combo_alignment || entry.combo_alignment_size.width != placed.width ||
                                entry.combo_alignment_size.height != placed.height) {
                                const auto& items = choices->items();
                                auto index = items.size() / 2;
                                for (std::size_t i = 0; i < items.size(); ++i)
                                    if (combo->selected() == items[i].id) { index = i; break; }
                                const auto row = choices->item_bounds(index);
                                entry.combo_alignment = entry.anchor->bounds().height / 2 -
                                    (choices->bounds().y - placed.y + row.y + row.height / 2);
                                entry.combo_alignment_size = {placed.width, placed.height};
                            }
                            placed.y = std::clamp(entry.anchor->bounds().y + *entry.combo_alignment,
                                viewport.y, viewport.y + std::max(0.0f, viewport.height - placed.height));
                            entry.popup->arrange(placed);
                        } else entry.combo_alignment.reset();
                    }
                }
                else entry.popup->arrange({});
            }
            struct Batch {
                struct Group { HWND parent; HDWP handle; };
                std::vector<Group> groups;
                ~Batch() { for (auto& group : groups) if (group.handle) EndDeferWindowPos(group.handle); }
                HDWP& for_parent(HWND parent) {
                    for (auto& group : groups) if (group.parent == parent) return group.handle;
                    groups.push_back({parent, BeginDeferWindowPos(16)});
                    win32_require(groups.back().handle != nullptr, "Begin image control layout");
                    return groups.back().handle;
                }
                void finish() {
                    for (auto& group : groups) {
                        auto value = std::exchange(group.handle, nullptr);
                        if (value) win32_require(EndDeferWindowPos(value) != 0, "Arrange image controls");
                    }
                }
            } batch;
            for (const auto& peer : peers) if (peer->clear_button) sync_clear_button(*peer);
            for (const auto& peer : peers) {
                auto bounds = peer->control->bounds();
                if (peer->parent) {
                    const auto parent = peer->parent->control->bounds();
                    bounds.x -= parent.x;
                    bounds.y -= parent.y;
                }
                if (peer->control->role() == ControlRole::tab_strip) {
                    // Round the shared bottom edge, not the height, so tabs meet their content at fractional DPI.
                    const float scale = dpi / 96.0f;
                    bounds.height = std::max(0.0f, static_cast<float>(
                        std::lround((bounds.y + bounds.height) * scale) - std::lround(bounds.y * scale)) / scale);
                }
                if (peer->document) {
                    if (auto* password = dynamic_cast<PasswordInput*>(peer->control.get()); password && password->revealed())
                        bounds.height = std::max(0.0f, bounds.height - 32);
                    if (palette.style == VisualStyle::winui &&
                        (dynamic_cast<DocumentText*>(peer->control.get()) || dynamic_cast<PasswordInput*>(peer->control.get()))) {
                        bounds.x += 4; bounds.y += 3;
                        bounds.width = std::max(0.0f, bounds.width - 8);
                        bounds.height = std::max(0.0f, bounds.height - 6);
                    }
                    if (visible(*peer)) {
                        const float scale = dpi / 96.0f;
                        const RECT target{static_cast<LONG>(std::lround(bounds.x * scale)),
                            static_cast<LONG>(std::lround(bounds.y * scale)),
                            static_cast<LONG>(std::lround(bounds.width * scale)),
                            static_cast<LONG>(std::lround(bounds.height * scale))};
                        if (!peer->placed || !EqualRect(&target, &peer->placed_bounds)) {
                            platform::place(peer->window, bounds, dpi); peer->placed_bounds = target; peer->placed = true;
                        }
                    }
                } else if (peer->edit) {
                    const bool search = static_cast<TextInput&>(*peer->control).search_style();
                    const auto& input = static_cast<TextInput&>(*peer->control);
                    const bool winui = options.visual_style == VisualStyle::winui;
                    const auto number = peer->parent ? dynamic_cast<const NumericInput*>(peer->parent->control.get()) : nullptr;
                    const bool combo = peer->parent && peer->parent->control->role() == ControlRole::combo_box;
                    const bool reserved_end = combo || (number && number->spin_placement() == NumberSpinPlacement::inline_buttons);
                    peer->edit->set_insets({search ? 40.0f : winui && !combo ? 11.0f : 12.0f, winui ? 6.0f : 10.0f,
                        input.shortcut_visible(bounds.width) ? 78.0f :
                            peer->clear_button && peer->clear_button->visible() ? 37.0f :
                            winui ? reserved_end ? 1.0f : 7.0f : 12.0f, winui ? 7.0f : 10.0f});
                    ShowWindow(peer->caption, !input.caption_visible() || !visible(*peer) ? SW_HIDE : SW_SHOWNA);
                    if (input.caption_visible()) {
                        const float caption_height = style_metrics(options.visual_style).input_header_height;
                        platform::place(peer->caption, {bounds.x, bounds.y, bounds.width, std::min(caption_height, bounds.height)}, dpi);
                        bounds.y += input.caption_extent();
                        bounds.height = std::max(0.0f, bounds.height - input.caption_extent());
                    }
                    peer->edit->arrange(bounds);
                } else if (has_images) {
                    const float scale = dpi / 96.0f;
                    RECT target{static_cast<LONG>(std::lround(bounds.x * scale)),
                        static_cast<LONG>(std::lround(bounds.y * scale)),
                        static_cast<LONG>(std::lround(bounds.width * scale)),
                        static_cast<LONG>(std::lround(bounds.height * scale))};
                    if (!peer->placed || !EqualRect(&target, &peer->placed_bounds)) {
                        // Win32 requires every deferred group to have the same native parent.
                        auto& group = batch.for_parent(peer->parent ? peer->parent->window : window);
                        group = DeferWindowPos(group, peer->window, nullptr,
                            target.left, target.top, target.right, target.bottom,
                            SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW);
                        win32_require(group != nullptr, "Place image control");
                        peer->placed_bounds = target; peer->placed = true;
                    }
                } else platform::place(peer->window, bounds, dpi);
                ShowWindow(peer->window, visible(*peer) ? SW_SHOWNA : SW_HIDE);
            }
            batch.finish();
            // Match native hit testing to the back-to-front popup composition order.
            for (const auto& entry : popups)
                win32_require(SetWindowPos(find_peer(entry.popup.get())->window, HWND_TOP, 0, 0, 0, 0,
                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOREDRAW) != 0, "Raise popup input surface");
            update_paint_bounds();
            ++layouts;
        }
        focus_targets.clear();
        sync_images();
        HWND disabled_focus{};
        std::vector<Peer*> update_peers;
        for (const auto& peer : peers) update_peers.push_back(peer.get());
        for (auto* peer : update_peers) {
            const auto& control = *peer->control;
            if (!visible(*peer)) {
                peer->text_layout.Reset();
                if (auto label = std::dynamic_pointer_cast<Label>(peer->control)) label->discard_wrapped_text();
            }
            if (peer->runtime) {
                peer->runtime->sync(visible(*peer) && onscreen(*peer) && IsWindowVisible(window) && !IsIconic(window), dpi);
                if (!window || closing) return;
                if (peer->runtime->active()) hide_tooltip();
            }
            if ((!visible(*peer) || !IsWindowVisible(window)) && dynamic_cast<MapView*>(peer->control.get()))
                static_cast<MapView&>(*peer->control).cancel_request();
            const bool tab_stop = control.focusable() && control.tab_stop() && !is_caption_button(control);
            const auto native_style = GetWindowLongPtrW(peer->window, GWL_STYLE);
            if (((native_style & WS_TABSTOP) != 0) != tab_stop)
                win32_require(SetWindowLongPtrW(peer->window, GWL_STYLE, tab_stop ?
                    native_style | WS_TABSTOP : native_style & ~static_cast<LONG_PTR>(WS_TABSTOP)) != 0, "Update control tab stop");
            if (tab_stop && enabled(*peer) && visible(*peer) && in_top_popup(*peer) &&
                (control.role() != ControlRole::split_view || static_cast<const SplitView&>(control).expanded()))
                focus_targets.push_back(peer->window);
            if ((!enabled(*peer) || !visible(*peer)) &&
                (GetFocus() == peer->window || focus_before_layout == peer->window)) disabled_focus = peer->window;
            const auto range = dynamic_cast<const RangeInput*>(&control);
            if (!peer->list && !control.captured() && !peer->pressed_choice && !(range && range->dragging()) && !peer->dragging && !peer->grid_drag &&
                !peer->collection_drag && !peer->collection_scroll && GetCapture() == peer->window) ReleaseCapture();
            if (!has_images || (IsWindowEnabled(peer->window) != FALSE) != enabled(*peer))
                EnableWindow(peer->window, enabled(*peer));
            if (!enabled(*peer) || !visible(*peer)) {
                if (auto range_input = std::dynamic_pointer_cast<RangeInput>(peer->control)) range_input->cancel_drag();
                peer->control->cancel();
                peer->control->pointer_move(false);
                peer->pressed_choice.reset();
                peer->hovered_choice.reset();
                peer->dragging = false;
                if (GetCapture() == peer->window) ReleaseCapture();
            }
            if (peer->repeating && (!enabled(*peer) || !visible(*peer) || !control.pressed() || !IsWindowVisible(window))) {
                KillTimer(peer->window, repeat_timer); peer->repeating = false;
            }
            if (peer->document) {
                peer->document->update(dpi, palette);
                publish_control(peer->accessibility, nullptr, control, peer->window);
            } else if (peer->edit) {
                if ((IsWindowEnabled(peer->caption) != FALSE) != enabled(*peer))
                    EnableWindow(peer->caption, enabled(*peer));
                auto& input = static_cast<TextInput&>(*peer->control);
                peer->edit->sync_suggestions(input);
                peer->edit->set_suggestion_colors(platform::native_color(palette.field),
                    platform::native_color(palette.text), platform::native_color(palette.secondary));
                SendMessageW(peer->window, EM_SETLIMITTEXT, input.maximum_length(), 0);
                peer->edit->set_placeholder(input.placeholder());
                peer->edit->set_placeholder_color(platform::native_color(palette.secondary));
                if (!peer->edit->composing() && peer->edit->text() != input.text())
                    peer->edit->set_model_text(input.text());
                if (peer->caption_text != input.name()) {
                    SetWindowTextW(peer->caption, input.name().c_str());
                    peer->caption_text = input.name();
                }
                if (peer->provider) publish_control(peer->accessibility, nullptr, control, peer->window);
            } else if (peer->list) {
                if (GetFocus() == peer->window) last_focus = peer->window;
                peer->list->update(dpi, palette);
            } else {
                publish_control(peer->accessibility, peer->provider, control, peer->window);
            }
        }
        if (disabled_focus) {
            bool repaired{};
            for (const auto& peer : peers) if (peer->window == disabled_focus) {
                if (const auto owner = peer->clear_owner.lock())
                    if (auto* input = find_peer(owner.get())) repaired = focus(*input, false);
                for (auto* parent = peer->parent; parent && !repaired; parent = parent->parent)
                    if (auto* expander = dynamic_cast<Expander*>(parent->control.get()); expander && !expander->expanded()) {
                        repaired = focus(*parent, false); break;
                    }
                break;
            }
            if (!repaired && !platform::traverse_focus(focus_targets, false, disabled_focus)) SetFocus(window);
        }
        for (std::size_t i = 0; i < popups.size(); ++i) {
            auto* anchor = find_peer(popups[i].anchor.get());
            if (!anchor || !enabled(*anchor, false) || !onscreen(*anchor)) {
                auto popup = popups[i].popup;
                dismiss_popup(*popup, PopupDismissReason::hidden);
                if (anchor && window && !closing)
                    for (auto* parent = anchor->parent; parent; parent = parent->parent)
                        if (auto* expander = dynamic_cast<Expander*>(parent->control.get()); expander && !expander->expanded()) {
                            focus(*parent, false); break;
                        }
                break;
            }
        }
        if (tooltip_target) {
            auto it = std::find_if(peers.begin(), peers.end(), [&](const auto& p) { return p->control->id() == tooltip_target; });
            if (it == peers.end() || !enabled(**it) || !onscreen(**it) || (*it)->control->help_text().empty()) hide_tooltip();
        }
        if (window) {
            sync_native_occlusion();
            InvalidateRect(window, nullptr, FALSE);
        }
    }
    void show_commands(std::shared_ptr<CommandSurface> surface, Control& anchor, std::shared_ptr<Popup> root_popup = {},
        std::optional<Rect> context_anchor = {}) {
        if (!surface || !surface->menu()->commands()) throw std::invalid_argument("Command surface has no commands");
        if (!root_popup) root_popup = surface->popup();
        std::weak_ptr<Impl> host = shared_from_this();
        std::weak_ptr<CommandSurface> weak = surface;
        std::weak_ptr<Popup> root_weak = root_popup;
        surface->menu()->on_accept([host, root_weak] {
            if (auto self = host.lock()) if (auto popup = root_weak.lock()) self->dismiss_popup(*popup, PopupDismissReason::commit);
        });
        surface->menu()->on_back([host, weak] {
            if (auto self = host.lock()) if (auto source = weak.lock()) self->dismiss_popup(*source->popup(), PopupDismissReason::cancel);
        });
        surface->menu()->on_submenu([host, weak, root_weak](CommandId id) {
            auto self = host.lock(); auto source = weak.lock(); auto root = root_weak.lock();
            if (!self || !source || !root || !source->popup()->is_open() || self->closing) return;
            self->dismiss_above(source->popup()->id(), PopupDismissReason::cancel);
            if (self->closing || !source->popup()->is_open()) return;
            auto child = std::make_shared<CommandSurface>(source->menu()->commands()->find(id)->label, false);
            child->set_commands(source->menu()->commands(), id);
            child->set_current([weak] { auto source = weak.lock(); return source && source->current(); });
            child->popup()->set_placement(PopupPlacement::right);
            self->show_commands(child, *source->menu(), root);
            for (auto& entry : self->popups) if (entry.popup == child->popup()) {
                entry.parent_command = source->menu(); entry.parent_command_id = id;
                source->menu()->set_expanded(id); break;
            }
        });
        surface->menu()->on_collapse([host, weak](CommandId id) {
            if (auto self = host.lock()) if (auto source = weak.lock(); source && source->menu()->expanded() == id)
                self->dismiss_above(source->popup()->id(), PopupDismissReason::cancel);
        });
        show_popup(surface->popup(), anchor, surface->editor() ? static_cast<Control*>(surface->editor().get()) : surface->menu().get(), {}, surface, context_anchor);
    }
    bool translate(MSG& msg) {
        InputScope scope(*this);
        if (msg.hwnd == window || IsChild(window, msg.hwnd)) {
            const bool keyboard = msg.message == WM_KEYDOWN || msg.message == WM_SYSKEYDOWN;
            const bool pointer = msg.message == WM_LBUTTONDOWN || msg.message == WM_RBUTTONDOWN || msg.message == WM_POINTERDOWN;
            if ((keyboard || pointer) && keyboard_focus_visible != keyboard) {
                keyboard_focus_visible = keyboard;
                invalidate(Invalidation::paint);
            }
        }
        if ((msg.message != WM_KEYDOWN && msg.message != WM_SYSKEYDOWN) || (msg.hwnd != window && !IsChild(window, msg.hwnd))) return false;
        for (const auto& peer : peers)
            if (peer->runtime && peer->control->role() == ControlRole::web_content && peer->runtime->contains_native(msg.hwnd)) return false;
        for (const auto& peer : peers) if (peer->window == msg.hwnd && peer->edit) {
            if (peer->edit->composing()) return false;
            if (peer->edit->suggestion_key(msg.wParam)) return true;
        }
        for (const auto& peer : peers) if (peer->window == msg.hwnd && peer->document && peer->document->composing()) return false;
        Control* target{};
        for (const auto& peer : peers) if (peer->window == msg.hwnd) target = peer->control.get();
        if (key && (popups.empty() || !popups.back().dialog)) {
            auto callback = key;
            if (callback({static_cast<Key>(msg.wParam), (GetKeyState(VK_CONTROL) & 0x8000) != 0,
                (GetKeyState(VK_SHIFT) & 0x8000) != 0, target, (GetKeyState(VK_MENU) & 0x8000) != 0})) return true;
        }
        if (!popups.empty() && popups.back().location) {
            auto picker = popups.back().location;
            if (picker->editor().get() == target && (msg.wParam == VK_UP || msg.wParam == VK_DOWN)) {
                picker->navigation()->items()->step(msg.wParam == VK_UP ? -1 : 1); return true;
            }
        }
        if (!popups.empty() && popups.back().commands) {
            auto surface = popups.back().commands;
            if (surface->editor().get() == target && (msg.wParam == VK_UP || msg.wParam == VK_DOWN || msg.wParam == VK_F2)) {
                if (msg.wParam == VK_F2) {
                    if (auto selected = surface->menu()->selection().focused()) surface->menu()->execute(selected->id, true);
                } else surface->menu()->step(msg.wParam == VK_UP ? -1 : 1);
                return true;
            }
        }
        if (msg.wParam == VK_ESCAPE && !popups.empty()) {
            auto popup = popups.back().popup;
            dismiss_popup(*popup, PopupDismissReason::cancel);
            return true;
        }
        if (!popups.empty() && popups.back().dialog) {
            if (msg.wParam == VK_RETURN && !dynamic_cast<DocumentText*>(target)) {
                auto dialog = popups.back().dialog;
                if (target == dialog->cancel_button().get()) dialog->cancel(); else dialog->accept();
                return true;
            }
            if (msg.wParam == VK_TAB) { traverse((GetKeyState(VK_SHIFT) & 0x8000) != 0); return true; }
            // Application shortcuts cannot activate controls behind a modal dialog.
            return false;
        }
        for (const auto& peer : peers) if (peer->window == msg.hwnd && peer->edit && peer->parent) {
            if (auto* numeric = dynamic_cast<NumericInput*>(peer->parent->control.get());
                numeric && (msg.wParam == VK_UP || msg.wParam == VK_DOWN)) {
                numeric->step(msg.wParam == VK_UP ? 1 : -1); return true;
            }
            if (dynamic_cast<ComboBox*>(peer->parent->control.get()) && (msg.wParam == VK_F4 ||
                (msg.wParam == VK_DOWN && GetKeyState(VK_MENU) < 0))) {
                open_combo(*peer->parent); return true;
            }
        }
        if (msg.wParam == VK_TAB) {
            traverse((GetKeyState(VK_SHIFT) & 0x8000) != 0);
            return true;
        }
        if (target && target->role() == ControlRole::text_input && msg.wParam == VK_RETURN) {
            static_cast<TextInput*>(target)->submit();
            return true;
        }
        if (msg.wParam == VK_PRIOR || msg.wParam == VK_NEXT) {
            if (dynamic_cast<DocumentText*>(target) || dynamic_cast<RangeInput*>(target) || dynamic_cast<RadioGroup*>(target) ||
                dynamic_cast<VirtualCollection*>(target) || dynamic_cast<DataGrid*>(target)) return false;
            for (const auto& peer : peers) if (peer->window == msg.hwnd && !peer->list)
                return scroll_key(*peer, msg.wParam);
        }
        return false;
    }
    void traverse(bool reverse) {
        if (peers.empty()) return;
        const auto current = GetFocus();
        size_t index = reverse ? 0 : peers.size() - 1;
        for (size_t i = 0; i < peers.size(); ++i) if (peers[i]->window == current) index = i;
        for (size_t count = 0; count < peers.size(); ++count) {
            index = reverse ? (index ? index - 1 : peers.size() - 1) : (index + 1) % peers.size();
            if (peers[index]->control->focusable() && peers[index]->control->tab_stop() && !is_caption_button(*peers[index]->control) &&
                enabled(*peers[index]) && in_top_popup(*peers[index]) && focus(*peers[index], false)) return;
        }
    }
    bool enabled(const Peer& peer, bool respect_modal = true) const {
        const auto owner = popup_owner(&peer);
        bool modal_scope = true;
        for (auto it = popups.rbegin(); it != popups.rend(); ++it) {
            if (it->popup->id() == owner) break;
            if (it->dialog) { modal_scope = false; break; }
        }
        if (respect_modal && !modal_scope) return false;
        for (const auto& entry : popups) if (entry.popup->id() == owner && !command_popup_current(entry)) return false;
        for (auto* ancestor = &peer; ancestor; ancestor = ancestor->parent)
            if (!ancestor->control->enabled() || (ancestor->control->role() == ControlRole::content_view &&
                (ancestor->control->bounds().width <= 0 || ancestor->control->bounds().height <= 0))) return false;
        for (auto* parent = peer.parent; parent; parent = parent->parent)
            if (auto* expander = dynamic_cast<Expander*>(parent->control.get()); expander && !expander->expanded()) return false;
        return true;
    }
    bool command_popup_current(const PopupEntry& entry) const {
        if (entry.commands && !entry.commands->current()) return false;
        if (!entry.parent_command_id) return true;
        const auto parent = entry.parent_command.lock();
        return parent && parent->expanded() == entry.parent_command_id && entry.commands &&
            parent->commands() == entry.commands->menu()->commands();
    }
    static Rect viewport(const Peer& peer) {
        if (auto scroll = dynamic_cast<ScrollView*>(peer.control.get())) return scroll->viewport();
        return peer.control->bounds();
    }
    bool onscreen(const Peer& peer) const {
        if (!visible(peer)) return false;
        auto result = peer.control->bounds();
        const auto intersect = [&](Rect clip) {
            const auto right = std::min(result.x + result.width, clip.x + clip.width);
            const auto bottom = std::min(result.y + result.height, clip.y + clip.height);
            result.x = std::max(result.x, clip.x); result.y = std::max(result.y, clip.y);
            result.width = std::max(0.0f, right - result.x); result.height = std::max(0.0f, bottom - result.y);
        };
        intersect(root->bounds());
        for (auto* parent = peer.parent; parent; parent = parent->parent) intersect(viewport(*parent));
        return result.width > 0 && result.height > 0;
    }
    bool visible(const Peer& peer) const {
        for (auto* ancestor = &peer; ancestor; ancestor = ancestor->parent) {
            if (!ancestor->control->visible()) return false;
            if (auto* popup = dynamic_cast<Popup*>(ancestor->control.get()); popup && !popup->is_open()) return false;
            if (ancestor != &peer)
                if (auto* expander = dynamic_cast<Expander*>(ancestor->control.get()); expander && !expander->expanded()) return false;
            const auto rect = viewport(*ancestor);
            if (rect.width <= 0 || rect.height <= 0) return false;
        }
        return true;
    }
    void reveal(Peer& peer) {
        for (auto* ancestor = peer.parent; ancestor; ancestor = ancestor->parent) {
            if (auto scroll = dynamic_cast<ScrollView*>(ancestor->control.get())) {
                scroll->reveal(peer.control->bounds());
                update();
            }
        }
    }
    bool scroll_key(Peer& peer, WPARAM key_code) {
        auto* owner = peer.control->role() == ControlRole::scroll_view ? &peer : peer.parent;
        while (owner && owner->control->role() != ControlRole::scroll_view) owner = owner->parent;
        if (!owner || !owner->control->enabled()) return false;
        auto& scroll = static_cast<ScrollView&>(*owner->control);
        if (scroll.passthrough()) return owner->parent ? scroll_key(*owner->parent, key_code) : false;
        switch (key_code) {
        case VK_UP: scroll.scroll_by(-32); break;
        case VK_DOWN: scroll.scroll_by(32); break;
        case VK_PRIOR: scroll.scroll_by(-scroll.bounds().height); break;
        case VK_NEXT: scroll.scroll_by(scroll.bounds().height); break;
        case VK_HOME: scroll.set_offset(0); break;
        case VK_END: scroll.set_offset(scroll.maximum_offset()); break;
        default: return false;
        }
        update();
        return true;
    }
    bool scroll_wheel(Peer& peer, WPARAM wparam) {
        auto* owner = peer.control->role() == ControlRole::scroll_view ? &peer : peer.parent;
        while (owner && owner->control->role() != ControlRole::scroll_view) owner = owner->parent;
        if (!owner) return false;
        auto& scroll = static_cast<ScrollView&>(*owner->control);
        if (scroll.passthrough()) return owner->parent ? scroll_wheel(*owner->parent, wparam) : false;
        if (!scroll.enabled()) return true;
        owner->wheel_remainder += GET_WHEEL_DELTA_WPARAM(wparam);
        const int ticks = owner->wheel_remainder / WHEEL_DELTA;
        owner->wheel_remainder %= WHEEL_DELTA;
        UINT lines = 3;
        SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
        const float distance = lines == WHEEL_PAGESCROLL ? scroll.bounds().height : 16.0f * lines;
        const auto before = scroll.offset();
        scroll.scroll_by(-ticks * distance);
        update();
        if (ticks && before == scroll.offset() && owner->parent) return scroll_wheel(*owner->parent, wparam);
        return true;
    }
    void paint_surfaces(const Stack& stack) {
        if (stack.surface()) {
            drawing.surface_frame(stack.bounds(), palette);
        }
        if (stack.separator_after()) {
            const auto bounds = stack.bounds();
            drawing.line(bounds.x, bounds.y + bounds.height + 3.5f,
                bounds.x + bounds.width, bounds.y + bounds.height + 3.5f, palette.border);
        }
        for (size_t i = 0; i < stack.child_count(); ++i) paint_content_surface(stack.child_at(i));
    }
    void paint_content_surface(const std::shared_ptr<Element>& element) {
        if (element->bounds().width <= 0 || element->bounds().height <= 0) return;
        if (auto stack = std::dynamic_pointer_cast<Stack>(element)) paint_surfaces(*stack);
        else if (auto scroll = std::dynamic_pointer_cast<ScrollView>(element)) {
            drawing.push_clip(scroll->viewport());
            paint_content_surface(scroll->content());
            drawing.pop_clip();
        } else if (auto content = std::dynamic_pointer_cast<ContentView>(element)) {
            drawing.push_clip(content->bounds());
            paint_content_surface(content->content());
            drawing.pop_clip();
        } else if (auto split = std::dynamic_pointer_cast<SplitView>(element)) {
            paint_content_surface(split->first());
            paint_content_surface(split->second());
        } else if (auto control = std::dynamic_pointer_cast<Control>(element)) {
            drawing.push_clip(control->bounds());
            if (auto expander = std::dynamic_pointer_cast<Expander>(control);
                expander && expander->expanded() && palette.style == VisualStyle::winui) {
                const auto bounds = expander->bounds();
                drawing.rounded({bounds.x + 0.5f, bounds.y + 0.5f,
                    std::max(0.0f, bounds.width - 1), std::max(0.0f, bounds.height - 1)}, palette.surface, 4);
            }
            for (const auto& child : control->retained_children()) paint_content_surface(child);
            drawing.pop_clip();
        }
    }
    void update_paint_bounds() {
        for (const auto& peer : peers) {
            if (peer->edit) continue;
            RECT pixels{};
            win32_require(GetWindowRect(peer->window, &pixels) != 0, "Read control paint bounds");
            MapWindowPoints(nullptr, window, reinterpret_cast<POINT*>(&pixels), 2);
            peer->paint_bounds = {pixels.left * 96.0f / dpi, pixels.top * 96.0f / dpi,
                (pixels.right - pixels.left) * 96.0f / dpi, (pixels.bottom - pixels.top) * 96.0f / dpi};
        }
    }
    void paint() {
        sync_images();
        PAINTSTRUCT paint{};
        BeginPaint(window, &paint);
        bool redraw{};
        try {
            if (drawing.begin(window, static_cast<float>(dpi), palette.background)) {
                paint_surfaces(*root);
                const auto paint_peer = [&](const auto& peer) {
                    if (!visible(*peer)) return;
                    for (auto* parent = peer->parent; parent; parent = parent->parent)
                        drawing.push_clip(viewport(*parent));
                    if (peer->document) {
                        if (palette.style == VisualStyle::winui &&
                            (dynamic_cast<DocumentText*>(peer->control.get()) || dynamic_cast<PasswordInput*>(peer->control.get()))) {
                            const auto bounds = peer->control->bounds();
                            drawing.field_frame({bounds.x + 1, bounds.y + 1, std::max(0.0f, bounds.width - 2),
                                std::max(0.0f, bounds.height - 2)}, palette, peer->control->focused(), enabled(*peer));
                        }
                        if (auto* password = dynamic_cast<PasswordInput*>(peer->control.get()); password && password->revealed()) {
                            const auto bounds = password->bounds();
                            const float inset = palette.style == VisualStyle::winui ? 4.0f : 0.0f;
                            const Rect preview{bounds.x + inset, bounds.y + std::max(0.0f, bounds.height - 32),
                                std::max(0.0f, bounds.width - 2 * inset), std::min(32.0f - inset, bounds.height)};
                            drawing.push_clip(preview);
                            drawing.fill(preview, palette.style == VisualStyle::winui ? palette.field : palette.surface);
                            password->with_password([&](std::wstring_view value) {
                                drawing.text(value, {preview.x + 8, preview.y, std::max(0.0f, preview.width - 16), preview.height}, palette.text);
                            });
                            drawing.pop_clip();
                        }
                    }
                    else if (peer->edit) paint_edit(*peer);
                    else {
                        const auto bounds = peer->paint_bounds;
                        drawing.push_clip(bounds);
                        drawing.origin(bounds.x, bounds.y);
                        if (peer->list) peer->list->paint(drawing);
                        else paint_control(*peer);
                        drawing.origin(0, 0);
                        drawing.pop_clip();
                    }
                    for (auto* parent = peer->parent; parent; parent = parent->parent) drawing.pop_clip();
                };
                for (const auto& peer : peers) if (!popup_owner(peer.get()) && !(peer->adaptive && peer->adaptive->overlay_active())) paint_peer(peer);
                std::vector<Drawing::NativeWindow> native;
                for (const auto& peer : peers) if (peer->native() && visible(*peer)) {
                    RECT clip{};
                    GetClientRect(window, &clip);
                    for (auto* parent = peer->parent; parent; parent = parent->parent) {
                        const auto bounds = viewport(*parent);
                        const float scale = dpi / 96.0f;
                        const RECT parent_clip{static_cast<LONG>(std::lround(bounds.x * scale)),
                            static_cast<LONG>(std::lround(bounds.y * scale)),
                            static_cast<LONG>(std::lround((bounds.x + bounds.width) * scale)),
                            static_cast<LONG>(std::lround((bounds.y + bounds.height) * scale))};
                        IntersectRect(&clip, &clip, &parent_clip);
                    }
                    if (IsWindowVisible(peer->caption)) native.push_back({peer->caption, clip});
                    if (IsWindowVisible(peer->window)) native.push_back({peer->window, clip});
                }
                // Present native and custom pixels together. A transparent viewport
                // does not exclude its opaque descendants from the root presentation.
                const auto composed = [&] {
                    // Compose the complete native pixels beneath translucent popup shadows.
                    struct CaptureScope {
                        bool& value;
                        explicit CaptureScope(bool& flag) : value(flag) { value = true; }
                        ~CaptureScope() { value = false; }
                    } capture(composing_native);
                    return drawing.native_windows(native);
                }();
                const auto paint_adaptive = [&](std::uint64_t owner) {
                    for (const auto* layout : adaptive_layouts) if (layout->overlay_active() && adaptive_owner(layout) == owner) {
                        Peer* representative{};
                        for (const auto& peer : peers) if (peer->adaptive == layout) { representative = peer.get(); break; }
                        if (!representative) continue;
                        for (auto* parent = representative->parent; parent; parent = parent->parent) drawing.push_clip(viewport(*parent));
                        drawing.fill(layout->navigation()->bounds(), palette.surface);
                        paint_content_surface(layout->navigation());
                        std::vector<HWND> overlay_native;
                        for (const auto& peer : peers) if (peer->adaptive == layout && popup_owner(peer.get()) == owner) {
                            paint_peer(peer);
                            if (peer->native()) { overlay_native.push_back(peer->window); if (peer->caption) overlay_native.push_back(peer->caption); }
                        }
                        drawing.present_native(overlay_native);
                        for (auto* parent = representative->parent; parent; parent = parent->parent) drawing.pop_clip();
                    }
                };
                if (composed) paint_adaptive(0);
                if (composed) for (const auto& entry : popups) {
                    const auto bounds = entry.popup->bounds();
                    if (entry.dialog && palette.style == VisualStyle::winui && !palette.high_contrast)
                        drawing.fill(root->bounds(), D2D1::ColorF(0, 0.25f));
                    const float inset = 1;
                    const Rect frame{bounds.x + inset, bounds.y + inset,
                        std::max(0.0f, bounds.width - 2 * inset), std::max(0.0f, bounds.height - 2 * inset)};
                    if ((entry.commands || palette.style == VisualStyle::winui) && !palette.high_contrast) {
                        for (float spread = command_shadow_extent; spread > 0; --spread)
                            drawing.rounded({frame.x - spread, frame.y - spread * 0.6f,
                                frame.width + 2 * spread, frame.height + 1.6f * spread},
                                D2D1::ColorF(0, 0.025f * (1 - spread / (command_shadow_extent + 1))), 8 + spread);
                    }
                    const auto popup_background = entry.popup->window_background() ? palette.background : palette.surface;
                    if (!entry.commands && palette.style == VisualStyle::classic) drawing.fill(bounds, popup_background);
                    const float radius = entry.commands || palette.style == VisualStyle::winui ? 8.0f : 4.0f;
                    paint_content_surface(entry.popup);
                    if (entry.dialog && palette.style == VisualStyle::winui) {
                        const auto footer = entry.dialog->footer_bounds();
                        drawing.push_clip(footer);
                        drawing.rounded(frame, palette.background, radius);
                        drawing.pop_clip();
                        drawing.line(frame.x + 1, footer.y - 0.5f, frame.x + frame.width - 1, footer.y - 0.5f, palette.border);
                    }
                    std::vector<HWND> popup_native;
                    for (const auto& peer : peers) if (popup_owner(peer.get()) == entry.popup->id()) {
                        paint_peer(peer);
                        if (peer->native()) {
                            popup_native.push_back(peer->window); if (peer->caption) popup_native.push_back(peer->caption);
                        }
                    }
                    drawing.present_native(popup_native);
                    paint_adaptive(entry.popup->id());
                    drawing.rounded(frame, entry.commands && palette.style == VisualStyle::classic ?
                        palette.secondary : palette.border, radius, true);
                }
                if (composed && tooltip_shown) {
                    for (const auto& peer : peers) if (peer->control->id() == tooltip_target) {
                        drawing.surface_frame(tooltip_bounds, palette);
                        drawing.text(peer->control->help_text(), {tooltip_bounds.x + 10, tooltip_bounds.y + 2,
                            std::max(0.0f, tooltip_bounds.width - 20), tooltip_bounds.height - 4}, palette.text, true);
                    }
                }
                redraw = !composed || !drawing.end();
                ++paints;
                // Transparent custom HWNDs retain input and UIA, but not targets.
                // WS_CLIPCHILDREN protects opaque native EDIT and caption pixels.
                for (const auto& peer : peers)
                    if (!peer->native()) ValidateRect(peer->window, nullptr);
            }
        } catch (...) { EndPaint(window, &paint); throw; }
        EndPaint(window, &paint);
        if (redraw) invalidate(Invalidation::paint);
    }
    bool sync_images(bool window_shown = true) {
        bool changed{};
        std::vector<std::uint64_t> retained;
        std::size_t remaining = 48;
        for (const auto& peer : peers) if (peer->image || dynamic_cast<VirtualCollection*>(peer->control.get()) ||
            dynamic_cast<DataGrid*>(peer->control.get()) || (peer->list &&
            (static_cast<FileList&>(*peer->control).thumbnails() || peer->list->thumbnail_count()))) {
            Rect rect = peer->control->bounds();
            const auto intersect = [&](Rect clip) {
                const auto right = std::min(rect.x + rect.width, clip.x + clip.width);
                const auto bottom = std::min(rect.y + rect.height, clip.y + clip.height);
                rect.x = std::max(rect.x, clip.x);
                rect.y = std::max(rect.y, clip.y);
                rect.width = std::max(0.0f, right - rect.x);
                rect.height = std::max(0.0f, bottom - rect.y);
            };
            intersect(root->bounds());
            for (auto* parent = peer->parent; parent; parent = parent->parent)
                intersect(viewport(*parent));
            const bool shown = window_shown && IsWindowVisible(window) && !IsIconic(window) &&
                visible(*peer) && rect.width > 0 && rect.height > 0;
            if (peer->image) {
                peer->image->sync(shown, wake);
                if (peer->image->pixels) retained.push_back(peer->image->pixels->id);
            } else if (peer->list) {
                rect.y -= peer->control->bounds().y;
                changed = peer->list->sync_thumbnails(shown, rect, wake, retained, remaining) || changed;
            } else {
                if (!peer->row_images) peer->row_images = std::make_unique<RowImages>();
                rect.x -= peer->control->bounds().x; rect.y -= peer->control->bounds().y;
                std::vector<RowVisual> rows;
                std::shared_ptr<const CollectionIndex> source;
                if (shown) if (auto* collection = dynamic_cast<VirtualCollection*>(peer->control.get())) {
                    source = collection->source();
                    for (const auto& row : collection->visible_content()) {
                        if (rows.size() == RowImages::maximum_rows) break;
                        const auto b = row.bounds;
                        if (b.y + b.height <= rect.y || b.y >= rect.y + rect.height ||
                            b.x + b.width <= rect.x || b.x >= rect.x + rect.width) continue;
                        auto visual = collection->source()->visual(row.index);
                        if (visual.icon == ButtonIcon::none) visual.icon = row.content.icon;
                        if (visual.image_path.empty()) visual.image_path = row.content.image_path;
                        if (row.navigation && !visual.image_path.empty() && visual.icon == ButtonIcon::none)
                            visual.icon = ButtonIcon::folder;
                        rows.push_back({row.key, std::move(visual), row.navigation});
                    }
                } else if (auto* grid = dynamic_cast<DataGrid*>(peer->control.get())) {
                    source = grid->source();
                    const auto column = grid->display_column(0);
                    float x = -static_cast<float>(grid->horizontal_offset());
                    if (column) for (size_t i = 0; i < *column; ++i) x += grid->columns()[i].width;
                    if (column && x < rect.x + rect.width && x + grid->columns()[*column].width > rect.x) {
                        const auto [begin, end] = grid->visible_rows();
                        for (auto row = begin; row < end && rows.size() < RowImages::maximum_rows; ++row) {
                            const auto y = DataGrid::header_height + static_cast<float>(row * double(DataGrid::row_height) - grid->offset());
                            if (y + DataGrid::row_height <= rect.y || y >= rect.y + rect.height) continue;
                            rows.push_back({grid->source()->key(row), grid->source()->visual(row, 0)});
                        }
                    }
                }
                changed = peer->row_images->sync(std::move(source), std::move(rows), dpi, wake, retained, remaining) || changed;
                has_images = has_images || peer->row_images->count() != 0;
            }
        }
        drawing.keep_images(retained);
        return changed;
    }
    void deliver_images() {
        if (!ready || closing || layout_pending) return;
        bool changed{};
        for (const auto& peer : peers) if (peer->image) changed = peer->image->deliver() || changed;
        changed = sync_images() || changed;
        if (changed) invalidate(Invalidation::paint);
    }
    D2D1_COLOR_F input_fill(const Peer& peer) const {
        const auto* owner = &peer;
        if (peer.edit && peer.parent && (peer.parent->control->role() == ControlRole::numeric_input ||
            peer.parent->control->role() == ControlRole::combo_box)) owner = peer.parent;
        bool hovered = owner->control->hovered();
        for (const auto& child : owner->control->retained_children())
            if (const auto* control = dynamic_cast<const Control*>(child.get())) hovered |= control->hovered();
        const auto focused = GetFocus() == owner->window || IsChild(owner->window, GetFocus()) ||
            (owner->clear_window && GetFocus() == owner->clear_window);
        return palette.input_fill(enabled(*owner), focused, hovered, owner->surface);
    }
    void paint_edit(Peer& peer) {
        auto bounds = peer.control->bounds();
        const auto& input = static_cast<TextInput&>(*peer.control);
        if (input.caption_visible()) {
            bounds.y += input.caption_extent();
            bounds.height = std::max(0.0f, bounds.height - input.caption_extent());
        }
        const auto number = peer.parent ? dynamic_cast<NumericInput*>(peer.parent->control.get()) : nullptr;
        const bool invalid = number && !number->valid();
        const bool focused = GetFocus() == peer.window || (peer.clear_window && GetFocus() == peer.clear_window);
        const bool composite = palette.style == VisualStyle::winui && peer.parent &&
            (number || dynamic_cast<ComboBox*>(peer.parent->control.get()));
        if (!composite) {
            const Rect frame = palette.style == VisualStyle::winui ? Rect{bounds.x + 0.5f, bounds.y + 0.5f,
                std::max(0.0f, bounds.width - 1), std::max(0.0f, bounds.height - 1)} : bounds;
            drawing.field_frame(frame, palette, focused, enabled(peer), invalid, input_fill(peer));
        }
        if (input.search_style()) drawing.search_icon({bounds.x + 12, bounds.y + (bounds.height - 18) / 2, 18, 18}, palette.secondary);
        const auto& hint = input.shortcut_hint();
        if (input.shortcut_visible(bounds.width)) {
            const auto top = bounds.y + (bounds.height - 20) / 2;
            drawing.rounded({bounds.x + bounds.width - 62, top, 50, 20}, palette.border, 4, true);
            drawing.text(hint, {bounds.x + bounds.width - 56, top, 42, 20}, palette.secondary, true);
        }
    }
    D2D1_COLOR_F status_fill(StatusSeverity severity) const {
        if (palette.high_contrast || palette.style == VisualStyle::classic) return palette.surface;
        const auto colors = winui_status_colors(palette.mode);
        switch (severity) {
        case StatusSeverity::success: return D2D1::ColorF(colors.success_fill);
        case StatusSeverity::warning: return D2D1::ColorF(colors.warning_fill);
        case StatusSeverity::error: return D2D1::ColorF(colors.error_fill);
        default: return D2D1::ColorF(colors.information_fill);
        }
    }
    void paint_control(Peer& peer) {
        auto& canvas = drawing;
        auto& control = *peer.control;
        const auto bounds = peer.paint_bounds;
        const auto foundation_text = enabled(peer) ? palette.text : palette.disabled;
        const bool fluent = palette.style == VisualStyle::winui;
        const bool focus_visible = control.focused() && (!fluent || keyboard_focus_visible);
        if (auto* runtime = dynamic_cast<RuntimeHost*>(&control)) {
            const auto area = runtime->visual_bounds();
            canvas.fill(area, palette.field);
            if (!peer.runtime || !peer.runtime->active())
                canvas.text(L"Explicit load required. Native runtime is not active.", {12, 12, std::max(0.0f, area.width - 24), 48}, palette.secondary, true);
            if (fluent && (!peer.runtime || !peer.runtime->active()))
                canvas.rounded(area, palette.border, 4, true);
            return;
        }
        if (auto* vector = dynamic_cast<VectorCanvas*>(&control)) {
            const auto area = vector->canvas_bounds();
            canvas.fill(area, palette.field);
            canvas.push_clip(area);
            canvas.scene(vector->scene(), vector->selected(), palette.accent);
            if (auto* map = dynamic_cast<MapView*>(vector)) {
                const auto center = map->center();
                canvas.text(L"Offline coordinate map | " + std::to_wstring(center.latitude).substr(0, 7) + L", " +
                    std::to_wstring(center.longitude).substr(0, 8) + L" | zoom " + std::to_wstring(map->zoom()).substr(0, 4),
                    {8, 4, std::max(0.0f, area.width - 16), 24}, palette.text, true);
                if (!map->error().empty()) canvas.text(map->error(), {8, 30, std::max(0.0f, area.width - 16), 32}, palette.error, true);
            }
            canvas.pop_clip();
            if (fluent) {
                canvas.rounded(area, palette.border, 4, true);
                if (control.focused()) canvas.focus_ring(area, palette);
            }
            return;
        }
        if (control.role() == ControlRole::popup) return;
        if (auto* number = dynamic_cast<NumericInput*>(&control)) {
            if (fluent) canvas.field_frame({0.5f, 0.5f, std::max(0.0f, bounds.width - 1), std::max(0.0f, bounds.height - 1)},
                palette, IsChild(peer.window, GetFocus()) != FALSE, enabled(peer), !number->valid(), input_fill(peer));
            return;
        }
        if (auto* status = dynamic_cast<InlineStatus*>(&control)) {
            auto ink = status->severity() == StatusSeverity::error ? palette.error :
                status->severity() == StatusSeverity::warning ? palette.folder : palette.accent;
            if (fluent && !palette.high_contrast) {
                const auto colors = winui_status_colors(palette.mode);
                if (status->severity() == StatusSeverity::success) ink = D2D1::ColorF(colors.success);
                else if (status->severity() == StatusSeverity::warning) ink = D2D1::ColorF(colors.warning);
            }
            if (!enabled(peer)) ink = palette.disabled;
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
            for (const auto& child : status->retained_children()) if (static_cast<Control&>(*child).visible())
                end = std::min(end, child->bounds().x - control.bounds().x);
            canvas.text(control.name(), {42, 6, std::max(0.0f, end - 46), std::max(0.0f, bounds.height - 12)}, foundation_text);
            return;
        }
        if (auto* color = dynamic_cast<ColorPicker*>(&control)) {
            const auto value = color->value();
            const D2D1_COLOR_F rgba{value.red / 255.0f, value.green / 255.0f, value.blue / 255.0f, value.alpha / 255.0f};
            const Rect preview{2, 2, std::max(0.0f, bounds.width - 4), 42};
            canvas.fill(preview, palette.field);
            for (int y = 0; y < 3; ++y) for (int x = 0; x < static_cast<int>(bounds.width / 14) + 1; ++x)
                if ((x + y) % 2) canvas.fill({2.0f + x * 14, 2.0f + y * 14, std::min(14.0f, std::max(0.0f, bounds.width - 4 - x * 14)), 14}, palette.border);
            canvas.fill(preview, rgba);
            if (fluent) canvas.rounded(preview, palette.border, 4, true);
            else canvas.outline(preview, palette.text);
            const wchar_t* channels[]{L"Red", L"Green", L"Blue", L"Alpha"};
            for (int i = 0; i < 4; ++i) canvas.text(channels[i], {8, 52.0f + i * 42, 52, 38}, foundation_text);
            return;
        }
        if (peer.parent) if (auto* picker = dynamic_cast<ColorPicker*>(peer.parent->control.get())) {
            const auto children = picker->retained_children();
            for (std::size_t i = 4; i < children.size() && i - 4 < picker->swatches().size(); ++i) if (children[i].get() == &control) {
                const auto value = picker->swatches()[i - 4];
                const D2D1_COLOR_F rgb{value.red / 255.0f, value.green / 255.0f, value.blue / 255.0f, 1};
                canvas.rounded({2, 2, std::max(0.0f, bounds.width - 4), bounds.height - 4}, rgb, 4);
                const Rect frame{1, 1, std::max(0.0f, bounds.width - 2), bounds.height - 2};
                if (fluent) {
                    canvas.rounded(frame, palette.border, 4, true);
                    if (control.focused()) canvas.focus_ring(frame, palette);
                } else canvas.outline(frame, control.focused() ? palette.accent : palette.text);
                return;
            }
        }
        if (auto* collection = dynamic_cast<VirtualCollection*>(&control)) {
            const bool commands = control.role() == ControlRole::command_menu;
            const auto* items = dynamic_cast<ItemsView*>(collection);
            const bool trailing_shortcuts = items && items->trailing_shortcut_badges();
            const auto hovered = commands && control.hovered() && peer.command_pointer && enabled(peer) ?
                collection->hit_test(*peer.command_pointer) : std::optional<std::size_t>{};
            const auto hovered_key = hovered && collection->source()->selectable(*hovered) ?
                std::optional{collection->source()->key(*hovered)} : std::optional<ItemKey>{};
            canvas.fill({0, 0, bounds.width, bounds.height}, commands || (fluent && peer.surface) ? palette.surface : palette.background);
            canvas.push_clip({0, 0, std::max(0.0f, bounds.width - VirtualCollection::bar_width), bounds.height});
            for (auto row : collection->visible_content()) {
                if (peer.row_images) {
                    const auto visual = peer.row_images->visual(row.key);
                    row.content.icon = visual.icon;
                    row.content.image_path = visual.image_path;
                }
                canvas.collection_row(row, collection->selection().contains(row.key),
                    control.focused() && collection->selection().focused() == row.key, enabled(peer), palette, hovered_key == row.key,
                    peer.row_images ? peer.row_images->pixels(row.key) : nullptr, trailing_shortcuts, commands);
            }
            if (!collection->source() || !collection->source()->size())
                canvas.text(commands ? L"No matching commands" : L"No matching items",
                    {12, 10, std::max(0.0f, bounds.width - 24), 32}, palette.secondary);
            canvas.pop_clip(); const auto thumb = collection->thumb();
            if (thumb.height) {
                if (fluent) canvas.scrollbar_thumb(thumb, palette, peer.dragging, enabled(peer));
                else canvas.rounded(thumb, palette.secondary, 3);
            }
            return;
        }
        if (auto* choices = dynamic_cast<RadioGroup*>(&control)) {
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
            const float edge = fluent ? 0.5f : 1.0f;
            const Rect face{edge, edge, std::max(0.0f, bounds.width - 2 * edge), std::max(0.0f, bounds.height - 2 * edge)};
            if (fluent) {
                if (combo->editor()) canvas.field_frame(face, palette, IsChild(peer.window, GetFocus()) != FALSE,
                    enabled(peer), false, input_fill(peer));
                else canvas.button_face(face, palette, ButtonAppearance::standard, enabled(peer),
                    control.hovered(), combo->popup()->is_open(), false);
                if (focus_visible) canvas.focus_ring(face, palette);
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
        if (auto* expander = dynamic_cast<Expander*>(&control)) {
            const auto height = fluent ? std::min(bounds.height, expander->effective_header_height()) : expander->effective_header_height();
            const float edge = fluent ? 0.5f : 1.0f;
            const Rect header{edge, edge, std::max(0.0f, bounds.width - 2 * edge),
                std::max(0.0f, std::min(bounds.height, height) - 2 * edge)};
            const auto fill = control.pressed() && fluent ? palette.selection :
                control.hovered() ? palette.hover : palette.surface;
            if (fluent) {
                const Rect frame = expander->expanded() ? Rect{edge, edge, header.width, std::max(0.0f, bounds.height - 1)} : header;
                canvas.push_clip({0, 0, bounds.width, height});
                canvas.rounded(frame, fill, 4);
                canvas.pop_clip();
                canvas.rounded(frame, palette.border, 4, true);
                if (expander->expanded() && bounds.height > height)
                    canvas.line(edge, height - edge, bounds.width - edge, height - edge, palette.border);
                canvas.symbol(expander->expanded() ? Symbol::chevron_up : Symbol::chevron_down,
                    {std::max(0.0f, bounds.width - 41), 0, 32, height}, foundation_text, 12);
            } else {
                canvas.rounded(header, fill, 6);
                canvas.text(expander->expanded() ? L"\u25be" : L"\u25b8", {10, 0, 24, height}, foundation_text);
            }
            canvas.text(control.name(), {fluent ? 17.0f : 36.0f, 0, std::max(0.0f, bounds.width - (fluent ? 78 : 48)), height}, foundation_text);
            if (focus_visible) {
                if (fluent) canvas.focus_ring(header, palette);
                else canvas.rounded(header, palette.accent, 6, true);
            }
            return;
        }
        if (auto* range = dynamic_cast<RangeInput*>(&control)) {
            const auto fraction = (range->preview_value() - range->range().minimum) / (range->range().maximum - range->range().minimum);
            const auto visual = slider_visual({bounds.width, bounds.height}, range->orientation(), range->reversed(), fraction, palette.style);
            const auto track = visual.track, thumb = visual.thumb;
            canvas.rounded(track, fluent && enabled(peer) ? palette.secondary : palette.border, 2);
            if (fluent && visual.filled.width > 0 && visual.filled.height > 0)
                canvas.rounded(visual.filled, enabled(peer) ? palette.accent : palette.disabled, 2);
            const float radius = thumb.width / 2;
            if (fluent) {
                canvas.rounded(thumb, palette.surface, radius);
                canvas.rounded(thumb, palette.border, radius, true);
                const float dot = !enabled(peer) || control.pressed() || peer.dragging ? 4.0f : control.hovered() ? 7.0f : 6.0f;
                canvas.rounded({thumb.x + radius - dot, thumb.y + radius - dot, 2 * dot, 2 * dot},
                    enabled(peer) ? palette.accent : palette.disabled, dot);
            } else canvas.rounded(thumb, enabled(peer) ? palette.accent : palette.disabled, radius);
            if (control.focused()) {
                const Rect face{1, 1, std::max(0.0f, bounds.width - 2), std::max(0.0f, bounds.height - 2)};
                if (fluent) canvas.focus_ring(face, palette);
                else canvas.outline(face, palette.accent);
            }
            return;
        }
        if (auto* progress = dynamic_cast<Progress*>(&control)) {
            const auto thickness = style_metrics(palette.style).progress_thickness;
            const Rect track{2, std::max(2.0f, bounds.height - 10), std::max(0.0f, bounds.width - 4), thickness};
            auto ink = !enabled(peer) ? palette.disabled : progress->state() == ProgressState::error ? palette.error :
                progress->state() == ProgressState::paused ? palette.secondary : palette.accent;
            if (fluent && enabled(peer) && !palette.high_contrast && progress->state() == ProgressState::paused)
                ink = D2D1::ColorF(winui_status_colors(palette.mode).warning);
            canvas.rounded(track, palette.border, thickness / 2);
            std::wstring text = progress->value_text();
            if (progress->state() == ProgressState::indeterminate || progress->state() == ProgressState::unknown) {
                text = progress->state() == ProgressState::unknown ? L"Unknown" : L"In progress";
                if (progress->state() == ProgressState::indeterminate) {
                    if (fluent)
                        canvas.rounded({track.x + track.width * 0.3f, track.y, track.width * 0.4f, track.height}, ink, thickness / 2);
                    else for (int i = 0; i < 5; ++i) canvas.rounded({track.x + track.width * (i + 1) / 6 - 3, track.y, 6, 6}, ink, 3);
                }
            } else {
                const auto fraction = (progress->value() - progress->range().minimum) / (progress->range().maximum - progress->range().minimum);
                if (fraction > 0)
                    canvas.rounded({track.x, track.y, track.width * static_cast<float>(fraction), track.height}, ink, thickness / 2);
                if (text.empty()) text = std::to_wstring(static_cast<int>(fraction * 100)) + L"%";
                if (progress->state() == ProgressState::paused) text += L" (paused)";
                if (progress->state() == ProgressState::error) text += L" (error)";
            }
            canvas.text(text, {4, 0, std::max(0.0f, bounds.width - 8), std::max(0.0f, bounds.height - 12)}, foundation_text, true);
            return;
        }
        if (auto* grid = dynamic_cast<DataGrid*>(&control)) {
            canvas.fill({0, 0, bounds.width, bounds.height}, fluent ? palette.surface : palette.background);
            canvas.push_clip({0, 0, grid->viewport_width(), bounds.height});
            const auto [first, end] = grid->visible_rows();
            const auto& source = grid->source();
            const auto hovered = enabled(peer) && !peer.grid_drag ? grid->hovered_row() : std::optional<std::size_t>{};
            canvas.push_clip({0, DataGrid::header_height, grid->viewport_width(), grid->viewport_height()});
            for (auto row = first; row < end; ++row) {
                const float y = DataGrid::header_height + static_cast<float>(row * double(DataGrid::row_height) - grid->offset());
                const bool selected = grid->selection().contains(source->key(row));
                if (fluent) {
                    if (selected) {
                        canvas.rounded({2, y + 2, std::max(0.0f, grid->viewport_width() - 4), DataGrid::row_height - 4}, palette.selection, 4);
                        canvas.rounded({2, y + 9, 3, DataGrid::row_height - 18}, palette.accent, 1.5f);
                    } else if (hovered == row)
                        canvas.rounded({2, y + 2, std::max(0.0f, grid->viewport_width() - 4), DataGrid::row_height - 4}, palette.hover, 4);
                } else if (selected || hovered == row || row % 2) canvas.fill({0, y, grid->viewport_width(), DataGrid::row_height},
                    selected ? palette.selection : hovered == row ? palette.hover : palette.surface);
                float x = -static_cast<float>(grid->horizontal_offset());
                for (std::size_t c = 0; c < grid->columns().size(); ++c) {
                    const auto& column = grid->columns()[c];
                    if (x + column.width > 0 && x < grid->viewport_width()) {
                        const auto ink = !enabled(peer) ? palette.disabled : selected ? palette.selection_text : palette.text;
                        if (column.checkable) {
                            if (fluent) canvas.check_indicator({x + 10, y + 8, 16, 16}, palette, selected, enabled(peer));
                            else {
                                canvas.rounded({x + 10, y + 8, 16, 16}, ink, 2, true);
                                if (selected) canvas.text(L"\u2713", {x + 10, y, 20, DataGrid::row_height}, ink);
                            }
                        }
                        float left = column.checkable ? 36.0f : 12.0f;
                        canvas.push_clip({x, y, column.width, DataGrid::row_height});
                        if (grid->source_column(c) == 0 && peer.row_images) {
                            const auto row_key = source->key(row);
                            const auto visual = peer.row_images->visual(row_key);
                            if (visual.icon != ButtonIcon::none || !visual.image_path.empty()) {
                                canvas.item_visual(visual, peer.row_images->pixels(row_key), {x + left, y + 4, 24, 24}, ink);
                                left += 32;
                            }
                        }
                        canvas.cell_text(source->text(row, grid->source_column(c)), {x + left, y,
                            std::max(0.0f, column.width - left - 12), DataGrid::row_height}, ink, column.numeric);
                        canvas.pop_clip();
                    }
                    x += column.width;
                }
                if (grid->selected() == source->key(row) && control.focused() && !grid->header_focus()) {
                    const Rect face{1, y + 1, std::max(0.0f, grid->viewport_width() - 2), DataGrid::row_height - 2};
                    if (fluent) canvas.focus_ring(face, palette);
                    else canvas.outline(face, palette.accent);
                } else if (!selected && hovered == row && palette.high_contrast)
                    canvas.outline({1, y + 1, std::max(0.0f, grid->viewport_width() - 2), DataGrid::row_height - 2}, palette.text);
            }
            if (!source || !source->size()) canvas.text(L"No matching rows", {16, 52, grid->viewport_width() - 32, 40}, palette.secondary);
            canvas.pop_clip();
            canvas.fill({0, 0, grid->viewport_width(), DataGrid::header_height}, palette.surface);
            float x = -static_cast<float>(grid->horizontal_offset());
            for (std::size_t c = 0; c < grid->columns().size(); ++c) {
                const auto& column = grid->columns()[c];
                const bool sorted = grid->source_column(c) == grid->sort_column();
                const float sort_space = fluent && sorted ? 20.0f : 0.0f;
                const float text_right = x + column.width - (column.filterable ? 32 : 12);
                canvas.cell_text(column.name + (!fluent && sorted ? (grid->descending() ? L" \u2193" : L" \u2191") : L""),
                    {x + (column.checkable ? 36 : 12), 0, std::max(0.0f, text_right - x - (column.checkable ? 36 : 12) - sort_space),
                    DataGrid::header_height}, palette.secondary, column.numeric);
                if (fluent && sorted)
                    canvas.symbol(grid->descending() ? Symbol::down : Symbol::up,
                        {text_right - 20, 0, 20, DataGrid::header_height}, foundation_text, 12);
                if (column.checkable) {
                    const auto state = grid->check_state();
                    if (fluent) canvas.check_indicator({x + 10, 11, 16, 16}, palette, state == SelectionState::all,
                        enabled(peer), state == SelectionState::mixed);
                    else {
                        canvas.rounded({x + 10, 11, 16, 16}, foundation_text, 2, true);
                        if (state != SelectionState::none) canvas.text(state == SelectionState::all ? L"\u2713" : L"\u2212", {x + 10, 0, 20, DataGrid::header_height}, foundation_text);
                    }
                }
                if (column.filterable) {
                    const auto ink = grid->filters()[grid->source_column(c)].empty() ? palette.secondary : palette.accent;
                    const Rect filter{x + column.width - 28, 0, 24, DataGrid::header_height};
                    if (fluent) canvas.symbol(Symbol::filter, filter, ink, 12);
                    else canvas.text(L"\u25bd", filter, ink);
                }
                canvas.line(x + column.width, 8, x + column.width, DataGrid::header_height - 8, palette.border);
                if (control.focused() && grid->header_focus() && grid->focused_column() == c) {
                    const auto b = grid->header_part_bounds(grid->source_column(c), grid->header_part());
                    const Rect face{b.x + 1, 1, std::max(0.0f, b.width - 2), DataGrid::header_height - 2};
                    if (fluent) canvas.focus_ring(face, palette);
                    else canvas.outline(face, palette.accent);
                }
                x += column.width;
            }
            if (peer.grid_drag == 5) {
                float marker = -static_cast<float>(grid->horizontal_offset());
                for (std::size_t c = 0; c < std::min(peer.grid_drop, grid->columns().size()); ++c) marker += grid->columns()[c].width;
                marker = std::clamp(marker, 2.0f, std::max(2.0f, grid->viewport_width() - 2));
                canvas.fill({marker - 1, 2, 3, DataGrid::header_height - 4}, palette.accent);
            }
            canvas.pop_clip();
            canvas.line(0, DataGrid::header_height, grid->viewport_width(), DataGrid::header_height, palette.border);
            const auto vertical = grid->vertical_thumb(), horizontal = grid->horizontal_thumb();
            if (fluent) {
                if (vertical.height) canvas.scrollbar_thumb(vertical, palette, peer.grid_drag == 1, enabled(peer));
                if (horizontal.width) canvas.scrollbar_thumb(horizontal, palette, peer.grid_drag == 2, enabled(peer));
                canvas.rounded({0.5f, 0.5f, std::max(0.0f, bounds.width - 1), std::max(0.0f, bounds.height - 1)}, palette.border, 4, true);
            } else {
                if (vertical.height) canvas.rounded(vertical, palette.secondary, 3);
                if (horizontal.width) canvas.rounded(horizontal, palette.secondary, 3);
            }
            return;
        }
        if (auto* chart = dynamic_cast<HistoryChart*>(&control)) {
            const Rect plot{12, 38, std::max(0.0f, bounds.width - 24), std::max(0.0f, bounds.height - 58)};
            if (fluent) canvas.surface_frame({1, 1, bounds.width - 2, bounds.height - 2}, palette);
            else canvas.rounded({1, 1, bounds.width - 2, bounds.height - 2}, palette.surface);
            canvas.text(chart->name(), {12, 2, bounds.width - 24, 32}, foundation_text, true);
            for (int i = 0; i <= 4; ++i) {
                const float y = plot.y + plot.height * i / 4;
                canvas.line(plot.x, y, plot.x + plot.width, y, palette.border);
            }
            for (std::size_t i = 1; i < chart->size(); ++i) {
                const auto before = chart->at(i - 1), after = chart->at(i);
                if (!before || !after) continue;
                const float x = plot.x + plot.width * static_cast<float>(HistoryChart::capacity - chart->size() + i) / (HistoryChart::capacity - 1);
                canvas.line(x - plot.width / (HistoryChart::capacity - 1), plot.y + plot.height * static_cast<float>(1 - *before / chart->maximum()),
                    x, plot.y + plot.height * static_cast<float>(1 - *after / chart->maximum()), palette.accent, 2);
            }
            canvas.text(L"60 samples   \u00b7   oldest \u2192 newest", {12, bounds.height - 22, bounds.width - 24, 20}, palette.secondary, true);
            return;
        }
        if (control.role() == ControlRole::content_view) return;
        if (control.role() == ControlRole::split_view) {
            const auto& split = static_cast<SplitView&>(control);
            auto d = split.divider();
            if (!d.width) return;
            d.x -= bounds.x; d.y -= bounds.y;
            canvas.fill(d, palette.background);
            canvas.rounded({d.x + 3, d.height / 2 - 22, 4, 44},
                control.focused() || peer.dragging ? palette.accent : fluent ? palette.secondary : palette.border, 2);
            if (fluent && control.focused()) canvas.focus_ring(d, palette, 2);
            return;
        }
        if (control.role() == ControlRole::tab_strip) {
            const auto& strip = static_cast<TabStrip&>(control);
            canvas.tab_strip(strip, bounds, palette, enabled(peer), peer.surface,
                control.focused() && keyboard_focus_visible, peer.tab_pointer);
            return;
        }
        if (peer.image) {
            canvas.fill({0, 0, bounds.width, bounds.height}, palette.surface);
            peer.image->paint(canvas, {2, 2, std::max(0.0f, bounds.width - 4), std::max(0.0f, bounds.height - 4)});
            const auto& image = static_cast<const Image&>(control);
            if (image.status() != ImageStatus::ready)
                canvas.text(image.status() == ImageStatus::error ? image.error() :
                    image.status() == ImageStatus::loading ? L"Loading image..." : L"No image",
                    {8, 0, std::max(0.0f, bounds.width - 16), bounds.height},
                    image.status() == ImageStatus::error ? palette.error : palette.secondary, true);
            if (fluent) canvas.rounded({1, 1, std::max(0.0f, bounds.width - 2), std::max(0.0f, bounds.height - 2)}, palette.border, 4, true);
            return;
        }
        if (control.role() == ControlRole::scroll_view) {
            const auto& scroll = static_cast<ScrollView&>(control);
            if (scroll.passthrough()) return;
            auto thumb = scroll.thumb();
            thumb.x -= bounds.x;
            thumb.y -= bounds.y;
            if (!scroll.overlay_scrollbar())
                canvas.fill({std::max(0.0f, bounds.width - ScrollView::bar_width), 0,
                    std::min(bounds.width, ScrollView::bar_width), bounds.height}, palette.background);
            if (thumb.height) {
                if (fluent) canvas.scrollbar_thumb(thumb, palette, peer.dragging || control.hovered(), enabled(peer));
                else canvas.rounded(thumb, control.enabled() ?
                    (peer.dragging || control.hovered() ? palette.accent : palette.secondary) : palette.border, 4);
            }
            if (focus_visible) {
                const Rect face{0.5f, 0.5f, std::max(0.0f, bounds.width - 1), std::max(0.0f, bounds.height - 1)};
                if (fluent) canvas.focus_ring(face, palette);
                else canvas.outline(face, palette.accent);
            }
            return;
        }
        if (is_caption_button(control)) {
            canvas.caption_button({0, 0, bounds.width, bounds.height}, static_cast<const Button&>(control).icon(),
                palette, caption_active, enabled(peer), control.hovered(), control.pressed(), control.focused());
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
        if (!fluent || status_parent)
            canvas.fill({0, 0, bounds.width, bounds.height}, fluent && status_parent ? status_fill(status_parent->severity()) :
                peer.surface ? palette.surface : palette.background);
        const float inset_size = fluent ? 0.5f : 2.0f;
        const Rect box{inset_size, inset_size, std::max(0.0f, bounds.width - 2 * inset_size), std::max(0.0f, bounds.height - 2 * inset_size)};
        const auto text = enabled(peer) ? palette.text : palette.disabled;
        const auto role = control.role();
        if (role == ControlRole::label) {
            auto& label = static_cast<Label&>(control);
            const auto color = !enabled(peer) ? palette.disabled : label.tone() == TextTone::accent ? palette.accent :
                label.tone() == TextTone::secondary ? palette.secondary :
                label.tone() == TextTone::error ? palette.error : text;
            const Rect area{0, 0, bounds.width, label.wrapping() ?
                std::min(bounds.height, label.wrapped_text(bounds.width).height) : bounds.height};
            canvas.text_layout(peer.text_layout.Get(), area, color);
        } else {
            const auto* button = dynamic_cast<const Button*>(&control);
            const bool checked_action = button && button->behavior() == ButtonBehavior::toggle && button->checked();
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
                const auto children = breadcrumb->retained_children();
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
                const auto children = peer.parent->control->retained_children();
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
            } else if (winui && !peer.clear_owner.expired())
                canvas.symbol(Symbol::clear, {0, 0, bounds.width, bounds.height}, ink, 12);
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
    void activated(Peer& peer, bool invoked) {
        if (invoked && peer.control->role() == ControlRole::combo_box) open_combo(peer);
        update();
        if (invoked && window && peer.control->role() == ControlRole::button)
            raise_control_invoked(peer.provider);
    }
    bool focus(Peer& peer, bool activate_window) {
        if (!ready || closing || !peer.control->focusable() || !enabled(peer)) return false;
        if (!in_top_popup(peer)) return false;
        if (const auto split = dynamic_cast<SplitView*>(peer.control.get()); split && !split->expanded()) return false;
        if (peer.control->bounds().width <= 0 || peer.control->bounds().height <= 0) return false;
        for (auto* parent = peer.parent; parent; parent = parent->parent) {
            const auto view = viewport(*parent);
            if (view.width <= 0 || view.height <= 0) return false;
        }
        reveal(peer);
        EnableWindow(peer.window, TRUE);
        if (activate_window) {
            if (IsIconic(window)) ShowWindow(window, SW_RESTORE);
            SetForegroundWindow(window);
        }
        SetFocus(peer.window);
        if (window && !closing && peer.runtime && peer.runtime->active())
            if (auto* web = dynamic_cast<WebContent*>(peer.control.get())) web->focus_content();
        return GetFocus() == peer.window || (peer.document && IsChild(peer.window, GetFocus())) ||
            (peer.runtime && peer.runtime->contains_native(GetFocus()));
    }
    Peer* edit_at(HWND source, LPARAM position) const {
        POINT point{GET_X_LPARAM(position), GET_Y_LPARAM(position)};
        MapWindowPoints(source, window, &point, 1);
        const float x = point.x * 96.0f / dpi, y = point.y * 96.0f / dpi;
        for (auto it = peers.rbegin(); it != peers.rend(); ++it) {
            auto& peer = **it;
            if (!peer.edit || (GetParent(peer.window) != source && peer.window != source) ||
                !visible(peer) || !enabled(peer) || !in_top_popup(peer)) continue;
            const auto rect = peer.control->bounds();
            if (x >= rect.x && x < rect.x + rect.width && y >= rect.y && y < rect.y + rect.height)
                return &peer;
        }
        return nullptr;
    }
    bool focus_edit_at(HWND source, LPARAM position) {
        if (auto* peer = edit_at(source, position)) return focus(*peer, false);
        return false;
    }
    void hover_edit(Peer* peer, HWND source) {
        const auto old = hovered_edit.lock();
        if (old && (!peer || old != peer->control)) old->pointer_move(false);
        hovered_edit = peer ? peer->control : std::weak_ptr<Control>{};
        hovered_edit_source = source;
        if (peer) peer->control->pointer_move(true);
    }
    void cancel_input() {
        InputScope input_scope(*this);
        hide_tooltip();
        std::vector<Peer*> snapshot;
        for (const auto& peer : peers) snapshot.push_back(peer.get());
        for (auto* peer : snapshot) {
            if (peer->edit) peer->edit->dismiss_suggestions();
            peer->grid_drag = 0;
            peer->dragging = false;
            if (auto range = std::dynamic_pointer_cast<RangeInput>(peer->control)) range->cancel_drag();
            peer->control->cancel();
            peer->control->pointer_move(false);
            peer->pressed_choice.reset();
            peer->hovered_choice.reset();
            KillTimer(peer->window, repeat_timer); peer->repeating = peer->repeat_cycle = false;
            if (GetCapture() == peer->window) ReleaseCapture();
        }
    }
    double range_fraction(const RangeInput& range, LPARAM point) const {
        const bool vertical = range.orientation() == Axis::vertical;
        const double length = (vertical ? range.bounds().height : range.bounds().width) - 24;
        if (length <= 0) return 0;
        const double coordinate = (vertical ? GET_Y_LPARAM(point) : GET_X_LPARAM(point)) * 96.0 / dpi;
        const double fraction = (coordinate - 12) / length;
        return vertical ? 1 - fraction : fraction;
    }
    LRESULT control_message(Peer& peer, HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
        auto& control = *peer.control;
        const auto gesture = [] {
            const bool ctrl = GetKeyState(VK_CONTROL) < 0, shift = GetKeyState(VK_SHIFT) < 0;
            return shift ? (ctrl ? SelectionGesture::add_range : SelectionGesture::extend) :
                ctrl ? SelectionGesture::focus_only : SelectionGesture::replace;
        };
        const auto inside = [&] {
            RECT rect{};
            GetClientRect(hwnd, &rect);
            if (auto expander = dynamic_cast<const Expander*>(&control);
                expander && options.visual_style == VisualStyle::winui &&
                GET_Y_LPARAM(lparam) * 96.0f / dpi >= expander->effective_header_height()) return false;
            return PtInRect(&rect, {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)}) != 0;
        };
        const auto choice_hit = [&](const RadioGroup& choices) {
            auto hit = choices.hit_test(GET_Y_LPARAM(lparam) * 96.0f / dpi);
            if (hit && options.visual_style == VisualStyle::winui) {
                const auto body = choices.item_bounds(*hit);
                const float x = GET_X_LPARAM(lparam) * 96.0f / dpi;
                if (!inside() || x < body.x || x >= body.x + body.width) hit.reset();
            }
            return hit;
        };
        switch (message) {
        case runtime_event_message:
            if (peer.runtime) peer.runtime->event();
            return 0;
        case WM_TIMER:
            if (wparam == repeat_timer) {
                auto* button = dynamic_cast<Button*>(&control);
                if (!button || !peer.repeating || !enabled(peer) || !visible(peer) || !control.pressed() || !IsWindowVisible(window)) {
                    KillTimer(hwnd, repeat_timer); peer.repeating = false; return 0;
                }
                SetTimer(hwnd, repeat_timer, button->repeat_interval(), nullptr);
                activated(peer, button->invoke()); return 0;
            }
            break;
        case WM_SETCURSOR:
            if (auto* grid = dynamic_cast<DataGrid*>(&control); grid && enabled(peer) && LOWORD(lparam) == HTCLIENT) {
                POINT point{}; GetCursorPos(&point); ScreenToClient(hwnd, &point);
                const float x = point.x * 96.0f / dpi, y = point.y * 96.0f / dpi;
                const auto cursor = peer.grid_drag == 3 || (y >= 0 && y < DataGrid::header_height && grid->resize_boundary(x)) ?
                    IDC_SIZEWE : peer.grid_drag == 5 ? IDC_SIZEALL : IDC_ARROW;
                SetCursor(LoadCursorW(nullptr, cursor)); return TRUE;
            }
            if (control.role() == ControlRole::split_view && static_cast<SplitView&>(control).expanded() &&
                LOWORD(lparam) == HTCLIENT) { SetCursor(LoadCursorW(nullptr, IDC_SIZEWE)); return TRUE; }
            break;
        case WM_COMMAND:
        case WM_NOTIFY:
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLOREDIT:
        case WM_NEXTDLGCTL:
            return SendMessageW(window, message, wparam, lparam);
        case WM_MOUSEWHEEL:
            if (auto* map = dynamic_cast<MapView*>(&control)) {
                if (!enabled(peer)) return 0;
                POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)}; ScreenToClient(hwnd, &point);
                map->zoom_at(GET_WHEEL_DELTA_WPARAM(wparam) / 120.0, {point.x * 96.0f / dpi, point.y * 96.0f / dpi}); return 0;
            }
            if (auto* collection = dynamic_cast<VirtualCollection*>(&control)) {
                if (!enabled(peer)) return 0;
                peer.wheel_remainder += GET_WHEEL_DELTA_WPARAM(wparam);
                const int ticks = peer.wheel_remainder / WHEEL_DELTA; peer.wheel_remainder %= WHEEL_DELTA;
                collection->set_offset(collection->offset() - ticks * collection->item_size().height * 3); return 0;
            }
            if (auto* grid = dynamic_cast<DataGrid*>(&control)) {
                if (!enabled(peer)) return 0;
                peer.wheel_remainder += GET_WHEEL_DELTA_WPARAM(wparam);
                const int ticks = peer.wheel_remainder / WHEEL_DELTA;
                peer.wheel_remainder %= WHEEL_DELTA;
                grid->set_offset(grid->offset() - ((GET_KEYSTATE_WPARAM(wparam) & MK_SHIFT) ? 0 : ticks * 96),
                    grid->horizontal_offset() - ((GET_KEYSTATE_WPARAM(wparam) & MK_SHIFT) ? ticks * 96 : 0));
                return 0;
            }
            if (scroll_wheel(peer, wparam)) return 0;
            break;
        case WM_MOUSEHWHEEL:
            if (auto* grid = dynamic_cast<DataGrid*>(&control)) {
                grid->set_offset(grid->offset(), grid->horizontal_offset() + GET_WHEEL_DELTA_WPARAM(wparam) * 0.8);
                return 0;
            }
            break;
        case WM_CONTEXTMENU:
            if (auto* grid = dynamic_cast<DataGrid*>(&control)) {
                grid->hover_pointer({});
                SetFocus(hwnd);
                std::optional<Point> position;
                if (lparam != -1) {
                    POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)}; ScreenToClient(hwnd, &point);
                    position = Point{point.x * 96.0f / dpi, point.y * 96.0f / dpi};
                }
                grid->prepare_context_menu(position);
            }
            show_control_menu(control, hwnd, lparam, palette, dpi); return 0;
        case WM_ERASEBKGND: return 1;
        case WM_PAINT:
            ValidateRect(hwnd, nullptr);
            return 0;
        case WM_GETOBJECT:
            if (static_cast<LONG>(lparam) == UiaRootObjectId && peer.provider)
                return UiaReturnRawElementProvider(hwnd, wparam, lparam, peer.provider);
            break;
        case WM_SETFOCUS:
            last_focus = hwnd;
            reveal(peer);
            control.set_focused(true);
            return 0;
        case WM_KILLFOCUS:
            peer.grid_drag = 0;
            peer.dragging = false;
            if (auto* range = dynamic_cast<RangeInput*>(&control)) range->cancel_drag();
            control.set_focused(false);
            KillTimer(hwnd, repeat_timer); peer.repeating = peer.repeat_cycle = false;
            if (GetCapture() == hwnd) ReleaseCapture();
            return 0;
        case WM_MOUSEMOVE:
            if (dynamic_cast<TabStrip*>(&control)) {
                const Point point{GET_X_LPARAM(lparam) * 96.0f / dpi, GET_Y_LPARAM(lparam) * 96.0f / dpi};
                if (!peer.tab_pointer || peer.tab_pointer->x != point.x || peer.tab_pointer->y != point.y) {
                    peer.tab_pointer = point;
                    invalidate(Invalidation::paint);
                }
            }
            if (auto* choices = dynamic_cast<RadioGroup*>(&control)) {
                const auto hit = inside() ? choice_hit(*choices) : std::nullopt;
                if (peer.hovered_choice != hit) { peer.hovered_choice = hit; invalidate(Invalidation::paint); }
            }
            if (auto* grid = dynamic_cast<DataGrid*>(&control)) {
                const bool hovering = enabled(peer) && !peer.grid_drag && !GetCapture() &&
                    !(wparam & (MK_LBUTTON | MK_RBUTTON | MK_MBUTTON));
                grid->hover_pointer(hovering ? std::optional{Point{GET_X_LPARAM(lparam) * 96.0f / dpi,
                    GET_Y_LPARAM(lparam) * 96.0f / dpi}} : std::nullopt);
            }
            if (auto* nav_list = dynamic_cast<NavigationList*>(&control)) {
                const auto row = nav_list->hit_test({GET_X_LPARAM(lparam) * 96.0f / dpi, GET_Y_LPARAM(lparam) * 96.0f / dpi});
                nav_list->hover_item(row ? std::optional{nav_list->source()->key(*row)} : std::nullopt);
            }
            if (auto* map = dynamic_cast<MapView*>(&control); map && peer.dragging) {
                const float x = GET_X_LPARAM(lparam) * 96.0f / dpi, y = GET_Y_LPARAM(lparam) * 96.0f / dpi;
                map->pan(x - peer.drag_offset, y - peer.drag_y); peer.drag_offset = x; peer.drag_y = y; return 0;
            }
            if (auto* collection = dynamic_cast<VirtualCollection*>(&control); collection && (peer.collection_drag || peer.collection_scroll)) {
                const float x = GET_X_LPARAM(lparam) * 96.0f / dpi, y = GET_Y_LPARAM(lparam) * 96.0f / dpi;
                if (peer.collection_scroll) {
                    const auto track = control.bounds().height - collection->thumb().height;
                    if (track > 0) collection->set_offset(peer.drag_offset + (y - peer.drag_y) * collection->maximum_offset() / track);
                } else if (peer.collection_anchor) {
                    if (const auto row = collection->hit_test({std::clamp(x, 0.0f, std::max(0.0f, control.bounds().width - 13)),
                        std::clamp(y, 0.0f, std::max(0.0f, control.bounds().height - 1))})) {
                        collection->set_selection(peer.collection_before);
                        collection->select_rectangle(*peer.collection_anchor, collection->source()->key(*row), peer.collection_additive);
                    }
                }
                return 0;
            }
            if (auto* range = dynamic_cast<RangeInput*>(&control); range && range->dragging()) {
                range->drag(range_fraction(*range, lparam)); return 0;
            }
            if (peer.grid_drag) {
                auto& grid = static_cast<DataGrid&>(control);
                const float x = GET_X_LPARAM(lparam) * 96.0f / dpi, y = GET_Y_LPARAM(lparam) * 96.0f / dpi;
                if (peer.grid_drag == 3) grid.resize_column(peer.grid_column, peer.drag_offset + x - peer.drag_y);
                else if (peer.grid_drag >= 4) {
                    if (peer.grid_drag == 4 && (std::abs(x - peer.drag_y) >= 6 || std::abs(y - peer.drag_offset) >= 6))
                        peer.grid_drag = 5;
                    if (peer.grid_drag == 5) {
                        if (x < 24 || x > grid.viewport_width() - 24)
                            grid.set_offset(grid.offset(), grid.horizontal_offset() + (x < 24 ? -32 : 32));
                        float edge = -static_cast<float>(grid.horizontal_offset());
                        peer.grid_drop = 0;
                        for (const auto& column : grid.columns()) {
                            if (x < edge + column.width / 2) break;
                            edge += column.width; ++peer.grid_drop;
                        }
                        SetCursor(LoadCursorW(nullptr, IDC_SIZEALL));
                        invalidate(Invalidation::paint);
                    }
                }
                else if (peer.grid_drag == 1) {
                    const float track = grid.viewport_height() - grid.vertical_thumb().height;
                    if (track > 0) grid.set_offset(peer.drag_offset + (y - peer.drag_y) * grid.maximum_offset() / track, grid.horizontal_offset());
                } else {
                    const float track = grid.viewport_width() - grid.horizontal_thumb().width;
                    if (track > 0) grid.set_offset(grid.offset(), peer.drag_offset + (x - peer.drag_y) * grid.maximum_horizontal() / track);
                }
                return 0;
            }
            if (peer.dragging) {
                if (auto split = dynamic_cast<SplitView*>(&control)) {
                    const float width = split->bounds().width - SplitView::divider_width;
                    if (width > 0) split->set_ratio((GET_X_LPARAM(lparam) * 96.0f / dpi - peer.drag_offset) / width);
                    update();
                    return 0;
                }
                auto& scroll = static_cast<ScrollView&>(control);
                const float track = scroll.bounds().height - scroll.thumb().height;
                if (track > 0) scroll.set_offset(peer.drag_offset +
                    (GET_Y_LPARAM(lparam) * 96.0f / dpi - peer.drag_y) * scroll.maximum_offset() / track);
                update();
                return 0;
            }
            if (auto* menu = dynamic_cast<CommandMenu*>(&control)) {
                const Point point{GET_X_LPARAM(lparam) * 96.0f / dpi, GET_Y_LPARAM(lparam) * 96.0f / dpi};
                const auto previous = peer.command_pointer ? menu->hit_test(*peer.command_pointer) : std::optional<std::size_t>{};
                peer.command_pointer = point;
                if (previous != menu->hit_test(point)) invalidate(Invalidation::paint);
            }
            control.pointer_move(inside());
            if (control.captured() && control.hovered() && peer.repeat_cycle && !peer.repeating) repeat_start(peer, false);
            if (!peer.tracking) {
                TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd, 0};
                peer.tracking = TrackMouseEvent(&track) != 0;
            }
            return 0;
        case WM_MOUSELEAVE:
            if (auto* grid = dynamic_cast<DataGrid*>(&control)) grid->hover_pointer({});
            if (auto* nav_list = dynamic_cast<NavigationList*>(&control)) nav_list->hover_item({});
            if (peer.hovered_choice) { peer.hovered_choice.reset(); invalidate(Invalidation::paint); }
            peer.tracking = false; peer.command_pointer.reset(); peer.tab_pointer.reset(); control.pointer_move(false); return 0;
        case WM_LBUTTONDOWN:
            if (!enabled(peer) || !visible(peer)) return 0;
            if (peer.suppress_popup_click) { SetFocus(hwnd); return 0; }
            hide_tooltip();
            if (focus_edit_at(hwnd, lparam)) return 0;
            if (auto* vector = dynamic_cast<VectorCanvas*>(&control)) {
                SetFocus(hwnd);
                const Point p{GET_X_LPARAM(lparam) * 96.0f / dpi, GET_Y_LPARAM(lparam) * 96.0f / dpi};
                const auto area = vector->canvas_bounds();
                if (p.x < 0 || p.y < 0 || p.x >= area.width || p.y >= area.height) return 0;
                if (auto id = vector->hit_test(p)) { vector->select(*id); return 0; }
                if (dynamic_cast<MapView*>(vector)) { peer.dragging = true; peer.drag_offset = p.x; peer.drag_y = p.y; SetCapture(hwnd); }
                return 0;
            }
            if (auto* collection = dynamic_cast<VirtualCollection*>(&control)) {
                SetFocus(hwnd);
                const Point point{GET_X_LPARAM(lparam) * 96.0f / dpi, GET_Y_LPARAM(lparam) * 96.0f / dpi};
                if (point.x >= control.bounds().width - VirtualCollection::bar_width) {
                    const auto thumb = collection->thumb();
                    if (thumb.height && point.y >= thumb.y && point.y < thumb.y + thumb.height) {
                        peer.collection_scroll = true; peer.drag_y = point.y; peer.drag_offset = static_cast<float>(collection->offset()); SetCapture(hwnd);
                    } else collection->set_offset(collection->offset() + (point.y < thumb.y ? -control.bounds().height : control.bounds().height));
                } else if (const auto index = collection->hit_test(point)) {
                    const auto source = collection->source(); const auto item_key = source->key(*index);
                    const auto info = source->hierarchy(*index); const auto b = collection->item_bounds(*index);
                    const auto* nav_list = dynamic_cast<NavigationList*>(collection);
                    const bool disclosure = nav_list ? nav_list->disclosure_hit(point) :
                        info.expandable && point.x < b.x + 34 + std::min<float>(static_cast<float>(info.depth) * 20, b.width / 3);
                    if (disclosure) { collection->disclose(item_key, !info.expanded); return 0; }
                    if (b.width >= 160 && point.x >= b.x + b.width - 74 && !source->item(*index).action.empty()) {
                        collection->activate_item(item_key, true); return 0;
                    }
                    if (auto* menu = dynamic_cast<CommandMenu*>(collection)) { menu->execute(item_key.id); return 0; }
                    peer.collection_before = collection->selection();
                    const bool ctrl = (wparam & MK_CONTROL) != 0, shift = (wparam & MK_SHIFT) != 0;
                    if (collection->presentation() == ItemsPresentation::tiles && !shift) {
                        peer.collection_drag = true; peer.collection_anchor = item_key; peer.collection_additive = ctrl; SetCapture(hwnd);
                    }
                    collection->select(item_key, shift ? (ctrl ? SelectionGesture::add_range : SelectionGesture::extend) :
                        ctrl ? SelectionGesture::toggle : SelectionGesture::replace);
                }
                return 0;
            }
            if (auto* choices = dynamic_cast<RadioGroup*>(&control)) {
                SetFocus(hwnd);
                const auto index = choice_hit(*choices);
                if (index && choices->items()[*index].enabled) {
                    if (palette.style == VisualStyle::winui) {
                        peer.hovered_choice = index;
                        peer.pressed_choice = choices->items()[*index].id;
                        SetCapture(hwnd);
                        invalidate(Invalidation::paint);
                        return 0;
                    }
                    choices->select(choices->items()[*index].id);
                    if (window && control.role() == ControlRole::choice_list && visible(peer)) choices->accept();
                }
                return 0;
            }
            if (auto* range = dynamic_cast<RangeInput*>(&control)) {
                SetFocus(hwnd); SetCapture(hwnd);
                range->begin_drag(range_fraction(*range, lparam)); return 0;
            }
            if (auto* expander = dynamic_cast<Expander*>(&control);
                expander && GET_Y_LPARAM(lparam) * 96.0f / dpi >= expander->effective_header_height()) return 0;
            if (auto* grid = dynamic_cast<DataGrid*>(&control); grid && enabled(peer)) {
                grid->hover_pointer({});
                SetFocus(hwnd);
                const float x = GET_X_LPARAM(lparam) * 96.0f / dpi, y = GET_Y_LPARAM(lparam) * 96.0f / dpi;
                if (x >= grid->viewport_width() && y >= DataGrid::header_height) {
                    const auto thumb = grid->vertical_thumb();
                    if (thumb.height && y >= thumb.y && y < thumb.y + thumb.height) {
                        peer.grid_drag = 1; peer.drag_y = y; peer.drag_offset = static_cast<float>(grid->offset()); SetCapture(hwnd);
                    } else grid->set_offset(grid->offset() + (y < thumb.y ? -grid->viewport_height() : grid->viewport_height()), grid->horizontal_offset());
                } else if (y >= control.bounds().height - DataGrid::bar_width) {
                    const auto thumb = grid->horizontal_thumb();
                    if (thumb.width && x >= thumb.x && x < thumb.x + thumb.width) {
                        peer.grid_drag = 2; peer.drag_y = x; peer.drag_offset = static_cast<float>(grid->horizontal_offset()); SetCapture(hwnd);
                    } else grid->set_offset(grid->offset(), grid->horizontal_offset() + (x < thumb.x ? -grid->viewport_width() : grid->viewport_width()));
                } else if (y < DataGrid::header_height) {
                    if (const auto c = grid->resize_boundary(x)) {
                        peer.grid_drag = 3; peer.grid_column = *c; peer.drag_y = x; peer.drag_offset = grid->columns()[*c].width;
                        SetCapture(hwnd); return 0;
                    }
                    if (const auto column = grid->column_at(x)) {
                        grid->focus_header(true); grid->step_header(static_cast<int>(*column) - static_cast<int>(grid->focused_column()));
                        const auto part = grid->header_part_at(x); grid->set_header_part(part);
                        if (part == GridHeaderPart::check) { grid->toggle_check(); return 0; }
                        if (part == GridHeaderPart::filter) { grid->open_filter(grid->source_column(*column)); return 0; }
                        peer.grid_drag = 4; peer.grid_column = *column; peer.grid_drop = *column;
                        peer.drag_y = x; peer.drag_offset = y; SetCapture(hwnd);
                    }
                } else if (const auto row = grid->row_at(y)) {
                    const auto column = grid->column_at(x);
                    if (column && grid->columns()[*column].checkable && grid->header_part_at(x) == GridHeaderPart::check)
                        grid->toggle_check(grid->source()->key(*row));
                    else grid->select(grid->source()->key(*row), (wparam & MK_SHIFT) ?
                        ((wparam & MK_CONTROL) ? SelectionGesture::add_range : SelectionGesture::extend) :
                        (wparam & MK_CONTROL) ? SelectionGesture::toggle : SelectionGesture::replace, false);
                }
                else grid->clear_selection();
                return 0;
            }
            if (auto tabs = dynamic_cast<TabStrip*>(&control); tabs && enabled(peer)) {
                keyboard_focus_visible = false;
                invalidate(Invalidation::paint);
                if (auto index = tabs->hit_test(GET_X_LPARAM(lparam) * 96.0f / dpi)) {
                    const auto close = tabs->close_bounds(*index);
                    const auto id = tabs->tabs()[*index].id;
                    const float x = GET_X_LPARAM(lparam) * 96.0f / dpi, y = GET_Y_LPARAM(lparam) * 96.0f / dpi;
                    if (close.width > 0 && x >= close.x && x < close.x + close.width &&
                        y >= close.y && y < close.y + close.height)
                        tabs->request_close(id);
                    else tabs->activate_tab(id);
                }
                return 0;
            }
            if (auto split = dynamic_cast<SplitView*>(&control); split && enabled(peer) && split->expanded()) {
                const float x = GET_X_LPARAM(lparam) * 96.0f / dpi;
                const auto d = split->divider();
                if (x >= d.x - split->bounds().x && x < d.x - split->bounds().x + d.width) {
                    SetFocus(hwnd);
                    peer.dragging = true;
                    peer.drag_offset = x - (d.x - split->bounds().x);
                    SetCapture(hwnd);
                }
                return 0;
            }
            if (control.role() == ControlRole::scroll_view && control.enabled()) {
                auto& scroll = static_cast<ScrollView&>(control);
                SetFocus(hwnd);
                const float y = GET_Y_LPARAM(lparam) * 96.0f / dpi;
                const float x = GET_X_LPARAM(lparam) * 96.0f / dpi;
                const auto thumb = scroll.thumb();
                if (x >= scroll.bounds().width - ScrollView::bar_width && thumb.height) {
                    if (y >= thumb.y - scroll.bounds().y && y < thumb.y - scroll.bounds().y + thumb.height) {
                        peer.dragging = true;
                        peer.drag_y = y;
                        peer.drag_offset = scroll.offset();
                        SetCapture(hwnd);
                    } else scroll.scroll_by(y < thumb.y - scroll.bounds().y ? -scroll.bounds().height : scroll.bounds().height);
                    update();
                }
                return 0;
            }
            control.pointer_move(inside());
            if (control.pointer_down()) {
                if (peer.clear_owner.expired()) SetFocus(hwnd);
                SetCapture(hwnd);
                repeat_start(peer, true);
            }
            return 0;
        case WM_LBUTTONUP:
            if (std::exchange(peer.suppress_popup_click, false)) return 0;
            if (auto* choices = dynamic_cast<RadioGroup*>(&control); choices && peer.pressed_choice) {
                const auto pressed = std::exchange(peer.pressed_choice, std::nullopt);
                const auto hit = inside() ? choice_hit(*choices) : std::nullopt;
                if (GetCapture() == hwnd) ReleaseCapture();
                invalidate(Invalidation::paint);
                if (enabled(peer) && hit && pressed == choices->items()[*hit].id && choices->items()[*hit].enabled) {
                    choices->select(choices->items()[*hit].id);
                    if (window && control.role() == ControlRole::choice_list && visible(peer)) choices->accept();
                }
                return 0;
            }
            if (dynamic_cast<VirtualCollection*>(&control)) {
                peer.collection_drag = peer.collection_scroll = false; peer.collection_anchor.reset(); peer.collection_before = {};
                if (GetCapture() == hwnd) ReleaseCapture(); return 0;
            }
            if (auto* range = dynamic_cast<RangeInput*>(&control); range && range->dragging()) {
                range->commit_drag();
                if (GetCapture() == hwnd) ReleaseCapture();
                return 0;
            }
            if (peer.repeat_cycle) {
                KillTimer(hwnd, repeat_timer); peer.repeating = peer.repeat_cycle = false; control.cancel();
                if (GetCapture() == hwnd) ReleaseCapture();
                return 0;
            }
            if (auto* grid = dynamic_cast<DataGrid*>(&control); grid && peer.grid_drag >= 4 && enabled(peer)) {
                const float x = GET_X_LPARAM(lparam) * 96.0f / dpi, y = GET_Y_LPARAM(lparam) * 96.0f / dpi;
                if (y >= 0 && y < DataGrid::header_height && x >= 0 && x < grid->viewport_width()) {
                    if (peer.grid_drag == 5) {
                        const auto to = peer.grid_drop - (peer.grid_drop > peer.grid_column ? 1 : 0);
                        grid->reorder_column(peer.grid_column, to);
                        grid->step_header(0);
                    } else if (peer.grid_column < grid->columns().size() && grid->column_at(x) == peer.grid_column)
                        grid->sort(grid->source_column(peer.grid_column));
                }
                invalidate(Invalidation::paint);
            }
            peer.grid_drag = 0;
            peer.dragging = false;
            if (is_caption_button(control)) control.pointer_move(inside());
            activated(peer, control.pointer_up(inside()));
            if (GetCapture() == hwnd) ReleaseCapture();
            return 0;
        case WM_LBUTTONDBLCLK:
            if (auto* collection = dynamic_cast<VirtualCollection*>(&control)) {
                if (const auto row = collection->hit_test({GET_X_LPARAM(lparam) * 96.0f / dpi, GET_Y_LPARAM(lparam) * 96.0f / dpi})) {
                    const auto item_key = collection->source()->key(*row); const auto info = collection->source()->hierarchy(*row);
                    if (info.expandable) collection->disclose(item_key, !info.expanded);
                    else collection->activate_item(item_key);
                }
                return 0;
            }
            if (auto* grid = dynamic_cast<DataGrid*>(&control); grid && enabled(peer)) {
                if (auto row = grid->row_at(GET_Y_LPARAM(lparam) * 96.0f / dpi)) {
                    grid->select(grid->source()->key(*row), false); grid->activate_selected();
                }
                return 0;
            }
            return control_message(peer, hwnd, WM_LBUTTONDOWN, wparam, lparam);
        case WM_CANCELMODE:
            peer.pressed_choice.reset();
            peer.collection_drag = peer.collection_scroll = false; peer.collection_anchor.reset(); peer.collection_before = {};
            KillTimer(hwnd, repeat_timer); peer.repeating = peer.repeat_cycle = false;
            peer.grid_drag = 0;
            peer.dragging = false;
            if (auto* range = dynamic_cast<RangeInput*>(&control)) range->cancel_drag();
            control.cancel();
            if (GetCapture() == hwnd) ReleaseCapture();
            return 0;
        case WM_CAPTURECHANGED:
            peer.pressed_choice.reset();
            peer.collection_drag = peer.collection_scroll = false; peer.collection_anchor.reset(); peer.collection_before = {};
            KillTimer(hwnd, repeat_timer); peer.repeating = peer.repeat_cycle = false;
            if (auto* range = dynamic_cast<RangeInput*>(&control)) range->cancel_drag();
            peer.grid_drag = 0; peer.dragging = false; control.cancel(); invalidate(Invalidation::paint); return 0;
        case WM_KEYDOWN:
            if (!enabled(peer) || !visible(peer)) return 0;
            hide_tooltip();
            if (auto* map = dynamic_cast<MapView*>(&control)) {
                if (wparam == VK_LEFT || wparam == VK_RIGHT) { map->pan(wparam == VK_LEFT ? 32 : -32, 0); return 0; }
                if (wparam == VK_UP || wparam == VK_DOWN) { map->pan(0, wparam == VK_UP ? 32 : -32); return 0; }
                if (wparam == VK_ADD || wparam == VK_OEM_PLUS || wparam == VK_SUBTRACT || wparam == VK_OEM_MINUS) {
                    const auto b = map->canvas_bounds(); map->zoom_at(wparam == VK_ADD || wparam == VK_OEM_PLUS ? 1 : -1, {b.width / 2, b.height / 2}); return 0;
                }
                if (wparam == VK_HOME) { map->set_view({}, 1); return 0; }
            }
            if (peer.parent && (wparam == VK_LEFT || wparam == VK_RIGHT))
                if (auto* breadcrumb = dynamic_cast<Breadcrumb*>(peer.parent->control.get())) {
                    if (auto* next = breadcrumb->adjacent(control, wparam == VK_LEFT ? -1 : 1))
                        if (auto* target = find_peer(next)) focus(*target, false);
                    return 0;
                }
            if (wparam == VK_ESCAPE && !popups.empty()) {
                auto popup = popups.back().popup; dismiss_popup(*popup, PopupDismissReason::cancel); return 0;
            }
            if (wparam == VK_ESCAPE && peer.adaptive && peer.adaptive->overlay_active()) {
                peer.adaptive->set_navigation_open(false); return 0;
            }
            if (auto* collection = dynamic_cast<VirtualCollection*>(&control)) {
                if (wparam == VK_ESCAPE) {
                    if (peer.collection_drag) collection->set_selection(peer.collection_before);
                    peer.collection_drag = peer.collection_scroll = false;
                    if (GetCapture() == hwnd) ReleaseCapture(); collection->cancel(); return 0;
                }
                if (wparam == 'A' && GetKeyState(VK_CONTROL) < 0) { collection->select_all(); return 0; }
                if (wparam == VK_UP || wparam == VK_DOWN) {
                    collection->step((wparam == VK_UP ? -1 : 1) * static_cast<int>(collection->columns()), gesture()); return 0;
                }
                if (wparam == VK_LEFT || wparam == VK_RIGHT) { collection->horizontal(wparam == VK_RIGHT, gesture()); return 0; }
                if (wparam == VK_HOME || wparam == VK_END) { collection->edge(wparam == VK_END, gesture()); return 0; }
                if (wparam == VK_PRIOR || wparam == VK_NEXT) {
                    collection->step((wparam == VK_PRIOR ? -1 : 1) * std::max(1, static_cast<int>(control.bounds().height / collection->item_size().height)) *
                        static_cast<int>(collection->columns()), gesture()); return 0;
                }
                if (wparam == VK_SPACE && collection->selection().focused()) {
                    if (!(lparam & (1LL << 30))) collection->select(*collection->selection().focused(), SelectionGesture::toggle);
                    return 0;
                }
                if ((wparam == VK_RETURN || wparam == VK_F2) && collection->selection().focused()) {
                    if (!(lparam & (1LL << 30))) collection->activate_item(*collection->selection().focused(), wparam == VK_F2);
                    return 0;
                }
            }
            if (auto* range = dynamic_cast<RangeInput*>(&control)) {
                if (wparam == VK_ESCAPE) { range->cancel_drag(); if (GetCapture() == hwnd) ReleaseCapture(); return 0; }
                std::optional<RangeKey> range_key;
                if (wparam == VK_HOME) range_key = RangeKey::minimum;
                if (wparam == VK_END) range_key = RangeKey::maximum;
                if (wparam == VK_PRIOR) range_key = RangeKey::page_increase;
                if (wparam == VK_NEXT) range_key = RangeKey::page_decrease;
                if (wparam == VK_LEFT || wparam == VK_DOWN) range_key = range->reversed() ? RangeKey::increase : RangeKey::decrease;
                if (wparam == VK_RIGHT || wparam == VK_UP) range_key = range->reversed() ? RangeKey::decrease : RangeKey::increase;
                if (range_key) { range->move(*range_key); return 0; }
            }
            if (auto* choices = dynamic_cast<RadioGroup*>(&control)) {
                if (wparam == VK_UP || wparam == VK_LEFT || wparam == VK_DOWN || wparam == VK_RIGHT) {
                    choices->step(wparam == VK_UP || wparam == VK_LEFT ? -1 : 1); return 0;
                }
                if (wparam == VK_HOME || wparam == VK_END) {
                    const auto& items = choices->items();
                    for (std::size_t i = 0; i < items.size(); ++i) {
                        const auto& item = items[wparam == VK_HOME ? i : items.size() - i - 1];
                        if (item.enabled) { choices->select(item.id); break; }
                    }
                    return 0;
                }
                if ((wparam == VK_RETURN || wparam == VK_SPACE) && !(lparam & (1LL << 30))) { choices->accept(); return 0; }
            }
            if (auto* number = dynamic_cast<NumericInput*>(&control); number && (wparam == VK_UP || wparam == VK_DOWN)) {
                number->step(wparam == VK_UP ? 1 : -1); return 0;
            }
            if (dynamic_cast<ComboBox*>(&control) && (wparam == VK_F4 || wparam == VK_DOWN || wparam == VK_UP)) {
                open_combo(peer); return 0;
            }
            if (auto* expander = dynamic_cast<Expander*>(&control); expander && (wparam == VK_LEFT || wparam == VK_RIGHT)) {
                if (expander->expanded() != (wparam == VK_RIGHT)) control.invoke();
                return 0;
            }
            if (auto* grid = dynamic_cast<DataGrid*>(&control); grid && enabled(peer)) {
                if (wparam == 'A' && GetKeyState(VK_CONTROL) < 0) { grid->select_all(); return 0; }
                if (wparam == VK_ESCAPE && peer.grid_drag) {
                    if (peer.grid_drag == 3 && peer.grid_column < grid->columns().size()) grid->set_column_width(peer.grid_column, peer.drag_offset);
                    peer.grid_drag = 0;
                    if (GetCapture() == hwnd) ReleaseCapture();
                    invalidate(Invalidation::paint); return 0;
                }
                if (peer.grid_drag) return 0;
                if (wparam == VK_F6) { grid->focus_header(!grid->header_focus()); return 0; }
                if (grid->header_focus()) {
                    if (wparam == VK_F4) {
                        if (!grid->columns().empty()) for (int step = 1; step <= 3; ++step) {
                            const auto next = static_cast<GridHeaderPart>((static_cast<int>(grid->header_part()) + step) % 3);
                            if (grid->header_part_bounds(grid->source_column(grid->focused_column()), next).width) {
                                grid->set_header_part(next); break;
                            }
                        }
                        return 0;
                    }
                    if (wparam == VK_LEFT || wparam == VK_RIGHT) {
                        const int delta = wparam == VK_LEFT ? -1 : 1;
                        if (GetKeyState(VK_CONTROL) < 0 && !grid->columns().empty()) {
                            const auto column = grid->focused_column();
                            if (GetKeyState(VK_SHIFT) < 0) {
                                const auto to = static_cast<std::size_t>(std::clamp(static_cast<int>(column) + delta, 0, static_cast<int>(grid->columns().size() - 1)));
                                grid->reorder_column(column, to);
                            } else grid->resize_column(column, grid->columns()[column].width + delta * 16);
                            grid->step_header(0);
                        } else grid->step_header(delta);
                        return 0;
                    }
                    if (wparam == VK_RETURN || wparam == VK_SPACE) {
                        if (!(lparam & (1LL << 30)) && !grid->columns().empty()) {
                            if (grid->header_part() == GridHeaderPart::check) grid->toggle_check();
                            else if (grid->header_part() == GridHeaderPart::filter) grid->open_filter(grid->source_column(grid->focused_column()));
                            else grid->sort(grid->source_column(grid->focused_column()));
                        }
                        return 0;
                    }
                    if (wparam == VK_DOWN) { grid->focus_header(false); grid->step(0); return 0; }
                } else {
                    if (wparam == VK_UP || wparam == VK_DOWN) { grid->step(wparam == VK_UP ? -1 : 1, gesture()); return 0; }
                    if (wparam == VK_PRIOR || wparam == VK_NEXT) { grid->step((wparam == VK_PRIOR ? -1 : 1) * std::max(1, static_cast<int>(grid->viewport_height() / DataGrid::row_height)), gesture()); return 0; }
                    if (wparam == VK_HOME || wparam == VK_END) { grid->edge(wparam == VK_END, gesture()); return 0; }
                    if (wparam == VK_SPACE) { if (!(lparam & (1LL << 30)) && grid->selected()) grid->toggle_check(grid->selected()); return 0; }
                    if (wparam == VK_RETURN) { if (!(lparam & (1LL << 30))) grid->activate_selected(); return 0; }
                    if (wparam == VK_LEFT || wparam == VK_RIGHT) { grid->set_offset(grid->offset(), grid->horizontal_offset() + (wparam == VK_LEFT ? -80 : 80)); return 0; }
                }
            }
            if (auto tabs = dynamic_cast<TabStrip*>(&control); tabs && enabled(peer)) {
                keyboard_focus_visible = true;
                invalidate(Invalidation::paint);
                if (wparam == VK_LEFT || wparam == VK_RIGHT) { tabs->step(wparam == VK_LEFT ? -1 : 1); return 0; }
                if ((wparam == VK_RETURN || wparam == VK_SPACE) && !(lparam & (1LL << 30)) && tabs->selected()) {
                    tabs->activate_tab(*tabs->selected()); return 0;
                }
                if (wparam == VK_DELETE && !(lparam & (1LL << 30)) && tabs->selected()) {
                    tabs->request_close(*tabs->selected()); return 0;
                }
            }
            if (auto split = dynamic_cast<SplitView*>(&control); split && enabled(peer)) {
                if (wparam == VK_LEFT || wparam == VK_RIGHT) {
                    split->set_ratio(split->ratio() + (wparam == VK_LEFT ? -0.025f : 0.025f)); return 0;
                }
                if (wparam == VK_HOME) { split->set_ratio(0.5f); return 0; }
            }
            if (control.role() == ControlRole::scroll_view && scroll_key(peer, wparam)) return 0;
            if (wparam == VK_ESCAPE) { control.cancel(); if (GetCapture() == hwnd) ReleaseCapture(); return 0; }
            if (wparam == VK_SPACE || wparam == VK_RETURN) {
                const auto accepted = control.key_down(wparam == VK_SPACE ? ActivationKey::space : ActivationKey::enter,
                    (lparam & (1LL << 30)) != 0);
                if (accepted && wparam == VK_SPACE) repeat_start(peer, true);
                activated(peer, accepted && wparam == VK_RETURN);
                return 0;
            }
            break;
        case WM_KEYUP:
            if (wparam == VK_SPACE && peer.repeat_cycle) {
                KillTimer(hwnd, repeat_timer); peer.repeating = peer.repeat_cycle = false; control.cancel(); return 0;
            }
            if (wparam == VK_SPACE || wparam == VK_RETURN) {
                activated(peer, control.key_up(wparam == VK_SPACE ? ActivationKey::space : ActivationKey::enter));
                return 0;
            }
            break;
        case WM_CHAR:
            if (wparam >= 32 && wparam != 127) {
                RadioGroup* choices = dynamic_cast<RadioGroup*>(&control);
                auto* combo = dynamic_cast<ComboBox*>(&control);
                if (combo) choices = combo->choices().get();
                if (choices && enabled(peer)) {
                    const auto now = GetTickCount64();
                    if (now - peer.typeahead_time > 1000 || peer.typeahead.size() >= 128) peer.typeahead.clear();
                    peer.typeahead_time = now; peer.typeahead += static_cast<wchar_t>(wparam);
                    if (!choices->type_ahead(peer.typeahead)) {
                        peer.typeahead.assign(1, static_cast<wchar_t>(wparam)); choices->type_ahead(peer.typeahead);
                    }
                    if (combo && choices->selected()) combo->select(*choices->selected());
                    return 0;
                }
            }
            if (wparam == VK_SPACE || wparam == VK_RETURN) return 0;
            break;
        case control_action_message:
            if (wparam != control.id()) return UIA_E_ELEMENTNOTAVAILABLE;
            if (lparam == 6) { reveal(peer); return S_OK; }
            if (!enabled(peer) || !visible(peer)) return UIA_E_ELEMENTNOTENABLED;
            if (lparam == 1) {
                return focus(peer, true) ? S_OK : UIA_E_INVALIDOPERATION;
            }
            if (auto tabs = dynamic_cast<TabStrip*>(&control)) {
                if (lparam >= 100) {
                    const auto id = static_cast<std::uint64_t>(lparam - 100);
                    if (!tabs->select(id)) return UIA_E_ELEMENTNOTAVAILABLE;
                    update();
                    return S_OK;
                }
                return UIA_E_INVALIDOPERATION;
            }
            if (auto* choices = dynamic_cast<RadioGroup*>(&control)) {
                if (lparam < 100) return UIA_E_INVALIDOPERATION;
                const auto id = static_cast<std::uint64_t>(lparam - 100);
                const auto& items = choices->items();
                const auto it = std::find_if(items.begin(), items.end(), [id](const auto& item) { return item.id == id; });
                if (it == items.end()) return UIA_E_ELEMENTNOTAVAILABLE;
                if (!it->enabled) return UIA_E_ELEMENTNOTENABLED;
                choices->select(id); update(); return S_OK;
            }
            if (auto* combo = dynamic_cast<ComboBox*>(&control); combo && lparam >= 100) {
                const auto id = static_cast<std::uint64_t>(lparam - 100);
                const auto& items = combo->items();
                const auto it = std::find_if(items.begin(), items.end(), [id](const auto& item) { return item.id == id; });
                if (it == items.end()) return UIA_E_ELEMENTNOTAVAILABLE;
                if (!it->enabled) return UIA_E_ELEMENTNOTENABLED;
                combo->select(id); update(); return S_OK;
            }
            if (auto split = dynamic_cast<SplitView*>(&control)) {
                if (lparam < 1000 || lparam > 11000) return E_INVALIDARG;
                split->set_ratio(static_cast<float>(lparam - 1000) / 10000);
                update();
                return S_OK;
            }
            if (control.role() == ControlRole::scroll_view) {
                auto& scroll = static_cast<ScrollView&>(control);
                if (lparam >= 1000 && lparam <= 11000) scroll.set_offset(scroll.maximum_offset() * (lparam - 1000) / 10000.0f);
                else if (lparam >= 2 && lparam <= 5) scroll_key(peer, lparam == 2 ? VK_UP :
                    lparam == 3 ? VK_DOWN : lparam == 4 ? VK_PRIOR : VK_NEXT);
                else return UIA_E_INVALIDOPERATION;
                update();
                return S_OK;
            }
            if (!control.invoke()) return UIA_E_INVALIDOPERATION;
            activated(peer, true);
            return S_OK;
        case foundation_action_message: {
            if (wparam != control.id()) return UIA_E_ELEMENTNOTAVAILABLE;
            FoundationAction action;
            {
                std::lock_guard lock(peer.accessibility->mutex);
                const auto it = peer.accessibility->foundation_actions.find(static_cast<std::uint64_t>(lparam));
                if (it == peer.accessibility->foundation_actions.end()) return UIA_E_ELEMENTNOTAVAILABLE;
                action = std::move(it->second); peer.accessibility->foundation_actions.erase(it);
            }
            if (!enabled(peer) || !visible(peer)) return UIA_E_ELEMENTNOTENABLED;
            if (auto* popup = dynamic_cast<Popup*>(&control); popup && popup->dialog_surface() && action.kind == FoundationAction::collapse) {
                dismiss_popup(*popup, PopupDismissReason::cancel); return S_OK;
            }
            if (action.kind == FoundationAction::expand || action.kind == FoundationAction::collapse) {
                const bool expand = action.kind == FoundationAction::expand;
                if (auto* expander = dynamic_cast<Expander*>(&control)) {
                    if (expander->expanded() != expand) expander->invoke();
                } else if (auto* combo = dynamic_cast<ComboBox*>(&control)) {
                    if (combo->popup()->is_open() != expand) open_combo(peer);
                } else return UIA_E_INVALIDOPERATION;
            } else if (auto* number = dynamic_cast<NumericInput*>(&control)) {
                if (action.kind == FoundationAction::text) {
                    if (!number->commit_text(action.string)) { update(); return E_INVALIDARG; }
                } else {
                    if (!std::isfinite(action.number) || action.number < number->range().minimum || action.number > number->range().maximum) return E_INVALIDARG;
                    number->change_value(action.number);
                }
            } else if (auto* range = dynamic_cast<RangeInput*>(&control); range && action.kind == FoundationAction::value) {
                if (!std::isfinite(action.number) || action.number < range->range().minimum || action.number > range->range().maximum) return E_INVALIDARG;
                range->change_value(action.number);
            } else return UIA_E_INVALIDOPERATION;
            update(); return S_OK;
        }
        case grid_action_message: {
            if (wparam != control.id()) return UIA_E_ELEMENTNOTAVAILABLE;
            auto* grid = dynamic_cast<DataGrid*>(&control);
            GridAction action;
            {
                std::lock_guard lock(peer.accessibility->mutex);
                auto it = peer.accessibility->grid_actions.find(static_cast<std::uint64_t>(lparam));
                if (it == peer.accessibility->grid_actions.end()) return UIA_E_ELEMENTNOTAVAILABLE;
                action = it->second; peer.accessibility->grid_actions.erase(it);
            }
            if (!enabled(peer) || !visible(peer)) return UIA_E_ELEMENTNOTENABLED;
            if (auto* collection = dynamic_cast<VirtualCollection*>(&control)) {
                if (action.key && (!collection->source() || !collection->source()->find(*action.key))) return UIA_E_ELEMENTNOTAVAILABLE;
                if (action.kind == GridAction::scroll) collection->set_offset(action.y);
                else if (action.kind == GridAction::select_all) collection->select_all();
                else if (action.kind == GridAction::focus) {
                    if (!focus(peer, true)) return UIA_E_INVALIDOPERATION;
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
                        } else collection->select(*action.key, SelectionGesture::toggle);
                    }
                } else if (action.kind == GridAction::reveal) collection->reveal(*action.key);
                else if (action.kind == GridAction::expand || action.kind == GridAction::collapse) {
                    if (!collection->disclose(*action.key, action.kind == GridAction::expand)) return UIA_E_INVALIDOPERATION;
                } else if (action.kind == GridAction::invoke || action.kind == GridAction::inline_action)
                    collection->activate_item(*action.key, action.kind == GridAction::inline_action);
                else return UIA_E_INVALIDOPERATION;
                update(); return S_OK;
            }
            if (!grid) return UIA_E_INVALIDOPERATION;
            if (action.kind == GridAction::filter) { grid->filter(action.column, action.text); update(); return S_OK; }
            if (action.kind == GridAction::filter_open) { grid->open_filter(action.column); update(); return S_OK; }
            if (action.kind == GridAction::check) { grid->toggle_check(action.key); update(); return S_OK; }
            if (action.kind == GridAction::add || action.kind == GridAction::remove) {
                if (!action.key || !grid->source() || !grid->source()->find(*action.key)) return UIA_E_ELEMENTNOTAVAILABLE;
                if (grid->selection().contains(*action.key) != (action.kind == GridAction::add)) grid->toggle_check(action.key);
                update(); return S_OK;
            }
            if (action.kind == GridAction::scroll) grid->set_offset(action.y, action.x);
            else if (action.kind == GridAction::reveal) {
                if (!action.key || !grid->reveal(*action.key)) return UIA_E_ELEMENTNOTAVAILABLE;
            } else if (action.kind == GridAction::clear) {
                if (!action.key || !grid->source() || !grid->source()->find(*action.key)) return UIA_E_ELEMENTNOTAVAILABLE;
                if (grid->selection().contains(*action.key)) grid->toggle_check(action.key);
            }
            else if (action.kind == GridAction::sort) {
                if (action.column >= grid->columns().size()) return UIA_E_ELEMENTNOTAVAILABLE;
                grid->sort(action.column);
            } else if (action.kind == GridAction::header_focus) {
                const auto column = grid->display_column(action.column);
                if (!column) return UIA_E_ELEMENTNOTAVAILABLE;
                if (!focus(peer, true)) return UIA_E_INVALIDOPERATION;
                grid->focus_header(true);
                grid->step_header(static_cast<int>(*column) - static_cast<int>(grid->focused_column()));
                grid->set_header_part(static_cast<GridHeaderPart>(static_cast<int>(action.x)));
            } else if (action.kind == GridAction::focus && !action.key) {
                if (!focus(peer, true)) return UIA_E_INVALIDOPERATION;
            } else {
                if (!action.key || !grid->select(*action.key, action.kind == GridAction::focus ?
                    SelectionGesture::focus_only : SelectionGesture::replace)) return UIA_E_ELEMENTNOTAVAILABLE;
                if (action.kind == GridAction::focus && !focus(peer, true)) return UIA_E_INVALIDOPERATION;
                if (action.kind == GridAction::invoke) grid->activate_selected();
            }
            update(); return S_OK;
        }
        case WM_NCDESTROY:
            peer.runtime.reset();
            control.cancel();
            control.set_focused(false);
            disconnect_control(peer.accessibility, peer.provider);
            UiaReturnRawElementProvider(hwnd, 0, 0, nullptr);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            peer.window = nullptr;
            break;
        }
        return DefWindowProcW(hwnd, message, wparam, lparam);
    }
    bool is_caption_button(const Control& control) const {
        return titlebar && (&control == titlebar->minimize().get() || &control == titlebar->maximize().get() ||
            &control == titlebar->close().get());
    }
    LRESULT caption_hit(HWND hwnd, LPARAM position) {
        POINT p{GET_X_LPARAM(position), GET_Y_LPARAM(position)};
        RECT outer{}; GetWindowRect(hwnd, &outer);
        const int frame = GetSystemMetricsForDpi(SM_CXSIZEFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
        if (!IsZoomed(hwnd)) {
            const bool left = p.x < outer.left + frame, right = p.x >= outer.right - frame;
            const bool top = p.y < outer.top + frame, bottom = p.y >= outer.bottom - frame;
            if (top) return left ? HTTOPLEFT : right ? HTTOPRIGHT : HTTOP;
            if (bottom) return left ? HTBOTTOMLEFT : right ? HTBOTTOMRIGHT : HTBOTTOM;
            if (left || right) return left ? HTLEFT : HTRIGHT;
        }
        ScreenToClient(hwnd, &p);
        const auto hit = titlebar->hit_test({p.x * 96.0f / dpi, p.y * 96.0f / dpi});
        switch (hit) {
        case CaptionHit::drag: return HTCAPTION;
        case CaptionHit::minimize: return HTMINBUTTON;
        case CaptionHit::maximize: return HTMAXBUTTON;
        case CaptionHit::close: return HTCLOSE;
        default: return HTCLIENT;
        }
    }
    LRESULT message(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
        if (titlebar && message == WM_NCCALCSIZE && wparam) {
            auto& params = *reinterpret_cast<NCCALCSIZE_PARAMS*>(lparam);
            const auto top = params.rgrc[0].top;
            DefWindowProcW(hwnd, message, wparam, lparam);
            params.rgrc[0].top = top + (IsZoomed(hwnd) ? GetSystemMetricsForDpi(SM_CYSIZEFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi) : 0);
            return 0;
        }
        if (titlebar && message == WM_NCHITTEST) return caption_hit(hwnd, lparam);
        if (auto result = navigation_message(hwnd, message, wparam, lparam)) return *result;
        switch (message) {
        case WM_TIMER:
            if (wparam == tooltip_timer) {
                KillTimer(hwnd, tooltip_timer);
                if (!IsWindowVisible(hwnd) || IsIconic(hwnd)) { hide_tooltip(); return 0; }
                for (const auto& peer : peers) if (peer->control->id() == tooltip_target && visible(*peer) && enabled(*peer)) {
                    if (peer->control->help_text().empty()) break;
                    const auto view = root->bounds();
                    tooltip_bounds = place_popup(peer->control->bounds(), {std::min(400.0f, view.width), 48}, view, PopupPlacement::below);
                    tooltip_shown = true; invalidate(Invalidation::paint); return 0;
                }
                hide_tooltip(); return 0;
            }
            break;
        case WM_MOUSEWHEEL: {
            POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            ScreenToClient(window, &point);
            for (auto it = peers.rbegin(); it != peers.rend(); ++it) {
                const auto& peer = **it;
                const auto bounds = peer.control->bounds();
                if (peer.control->role() == ControlRole::scroll_view &&
                    point.x * 96.0f / dpi >= bounds.x && point.x * 96.0f / dpi < bounds.x + bounds.width &&
                    point.y * 96.0f / dpi >= bounds.y && point.y * 96.0f / dpi < bounds.y + bounds.height) {
                    scroll_wheel(**it, wparam);
                    return 0;
                }
            }
            break;
        }
        case WM_CANCELMODE: cancel_input(); return DefWindowProcW(hwnd, message, wparam, lparam);
        case WM_ACTIVATE:
            caption_active = LOWORD(wparam) != WA_INACTIVE;
            if (titlebar) invalidate(Invalidation::paint);
            if (LOWORD(wparam) == WA_INACTIVE && !IsChild(hwnd, reinterpret_cast<HWND>(lparam))) {
                cancel_input();
                if (!popups.empty() && std::none_of(popups.begin(), popups.end(),
                    [](const auto& entry) { return entry.dialog || (entry.commands && entry.commands->editor()); })) {
                    auto popup = popups.front().popup; dismiss_popup(*popup, PopupDismissReason::focus_lost, false);
                }
            }
            break;
        case WM_ENABLE:
            if (!wparam) {
                cancel_input();
                if (!popups.empty()) { auto popup = popups.front().popup; dismiss_popup(*popup, PopupDismissReason::hidden, false); }
            }
            break;
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: if (ready) paint(); else return DefWindowProcW(hwnd, message, wparam, lparam); return 0;
        case WM_SIZE:
            if (titlebar) titlebar->set_maximized(wparam == SIZE_MAXIMIZED);
            hide_tooltip();
            if (wparam == SIZE_MINIMIZED) {
                cancel_input();
                if (!popups.empty()) { auto popup = popups.front().popup; dismiss_popup(*popup, PopupDismissReason::hidden, false); }
            }
            for (const auto& peer : peers) if (peer->edit) peer->edit->dismiss_suggestions();
            for (auto& task : samples) task->suspend(wparam == SIZE_MINIMIZED);
            if (wparam == SIZE_MINIMIZED) for (const auto& peer : peers) {
                if (peer->runtime) peer->runtime->suspend();
                if (auto* map = dynamic_cast<MapView*>(peer->control.get())) map->cancel_request();
            }
            if (wparam != SIZE_MINIMIZED) invalidate(Invalidation::layout);
            else if (ready) sync_images(false);
            return 0;
        case WM_SHOWWINDOW:
            if (!wparam && ready) sync_images(false);
            if (!wparam) {
                cancel_input();
                if (!popups.empty()) { auto popup = popups.front().popup; dismiss_popup(*popup, PopupDismissReason::hidden, false); }
            }
            for (auto& task : samples) task->suspend(!wparam || IsIconic(hwnd));
            if (!wparam) for (const auto& peer : peers) {
                if (peer->runtime) peer->runtime->suspend();
                if (auto* map = dynamic_cast<MapView*>(peer->control.get())) map->cancel_request();
            }
            else invalidate(Invalidation::paint);
            break;
        case WM_GETMINMAXINFO: {
            auto& limits = *reinterpret_cast<MINMAXINFO*>(lparam);
            const UINT scale = GetDpiForWindow(hwnd) ? GetDpiForWindow(hwnd) : GetDpiForSystem();
            if (std::isfinite(options.minimum_size.width) && options.minimum_size.width > 0)
                limits.ptMinTrackSize.x = MulDiv(static_cast<int>(std::min(options.minimum_size.width, 16000.0f)), scale, 96);
            if (std::isfinite(options.minimum_size.height) && options.minimum_size.height > 0)
                limits.ptMinTrackSize.y = MulDiv(static_cast<int>(std::min(options.minimum_size.height, 16000.0f)), scale, 96);
            return 0;
        }
        case WM_MOVE:
            hide_tooltip();
            if (!popups.empty()) invalidate(Invalidation::layout);
            for (const auto& peer : peers) if (peer->edit) peer->edit->dismiss_suggestions();
            for (const auto& peer : peers) if (peer->list) peer->list->publish();
            return 0;
        case update_message: update(); return 0;
        case WM_DPICHANGED: {
            cancel_input();
            dpi = HIWORD(wparam);
            apply_theme();
            const auto& suggested = *reinterpret_cast<RECT*>(lparam);
            SetWindowPos(hwnd, nullptr, suggested.left, suggested.top,
                suggested.right - suggested.left, suggested.bottom - suggested.top, SWP_NOZORDER | SWP_NOACTIVATE);
            invalidate(Invalidation::layout);
            return 0;
        }
        case WM_SETTINGCHANGE:
        case WM_SYSCOLORCHANGE:
        case WM_THEMECHANGED: if (ready) apply_theme(); break;
        case WM_DISPLAYCHANGE:
            drawing.discard();
            invalidate(Invalidation::paint);
            return 0;
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLOREDIT: {
            const auto dc = reinterpret_cast<HDC>(wparam);
            const bool editable = message == WM_CTLCOLOREDIT ||
                (reinterpret_cast<HWND>(lparam) && GetDlgCtrlID(reinterpret_cast<HWND>(lparam)) >= 100);
            bool on_surface{};
            auto editable_fill = palette.field;
            for (const auto& peer : peers) {
                if (peer->caption == reinterpret_cast<HWND>(lparam)) { on_surface = peer->surface; break; }
                if (peer->edit && peer->window == reinterpret_cast<HWND>(lparam)) { editable_fill = input_fill(*peer); break; }
            }
            SetTextColor(dc, platform::native_color(IsWindowEnabled(reinterpret_cast<HWND>(lparam)) ?
                palette.text : palette.disabled));
            SetBkColor(dc, platform::native_color(editable ? editable_fill : on_surface ? palette.surface : palette.background));
            if (editable && palette.style == VisualStyle::winui) {
                SetDCBrushColor(dc, platform::native_color(editable_fill));
                return reinterpret_cast<LRESULT>(GetStockObject(DC_BRUSH));
            }
            if (!editable && on_surface) {
                SetDCBrushColor(dc, platform::native_color(palette.surface));
                return reinterpret_cast<LRESULT>(GetStockObject(DC_BRUSH));
            }
            return reinterpret_cast<LRESULT>(editable ? field : background);
        }
        case WM_NOTIFY: {
            const auto* notification = reinterpret_cast<const NMHDR*>(lparam);
            if (notification) for (const auto& peer : peers)
                if (peer->document && peer->window == notification->hwndFrom) return enabled(*peer) ? peer->document->notify(*notification) : 0;
            break;
        }
        case WM_COMMAND:
            for (const auto& peer : peers) if (peer->document && peer->window == reinterpret_cast<HWND>(lparam)) {
                if (HIWORD(wparam) == EN_CHANGE && enabled(*peer)) peer->document->changed();
                return 0;
            }
            for (const auto& peer : peers) if (peer->edit && peer->window == reinterpret_cast<HWND>(lparam)) {
                if (HIWORD(wparam) == EN_CHANGE && enabled(*peer) && !peer->edit->composing()) {
                    static_cast<TextInput&>(*peer->control).commit_text(peer->edit->text());
                    peer->edit->text_changed();
                }
                if (HIWORD(wparam) == EN_SETFOCUS || HIWORD(wparam) == EN_KILLFOCUS)
                    peer->control->set_focused(HIWORD(wparam) == EN_SETFOCUS);
                if (HIWORD(wparam) == EN_SETFOCUS) { last_focus = peer->window; reveal(*peer); }
                return 0;
            }
            break;
        case WM_SETFOCUS:
            if (ready) platform::request_focus_restore(hwnd);
            return 0;
        case platform::restore_focus_message:
            if (ready) platform::restore_focus(hwnd, last_focus, focus_targets);
            return 0;
        case WM_NEXTDLGCTL:
            if (lparam) {
                const auto target = reinterpret_cast<HWND>(wparam);
                if (std::find(focus_targets.begin(), focus_targets.end(), target) != focus_targets.end() &&
                    IsWindowEnabled(target)) {
                    for (const auto& peer : peers) if (peer->window == target) { focus(*peer, false); break; }
                }
            } else traverse(wparam != 0);
            return 0;
        case WM_MOUSEMOVE:
            if (options.visual_style == VisualStyle::winui) {
                if (hovered_edit_source != hwnd) {
                    TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd, 0};
                    win32_require(TrackMouseEvent(&track) != 0, "Track input frame pointer");
                }
                hover_edit(edit_at(hwnd, lparam), hwnd);
            }
            return 0;
        case WM_MOUSELEAVE:
            if (hovered_edit_source == hwnd) hover_edit(nullptr, nullptr);
            return 0;
        case WM_LBUTTONDOWN:
            light_dismiss(nullptr);
            focus_edit_at(hwnd, lparam);
            return 0;
        case metrics_message:
            if (wparam == 30) return static_cast<LRESULT>(drawing.native_buffer_bytes());
            if (wparam == 31) return static_cast<LRESULT>(drawing.native_bitmap_bytes());
            if (wparam == 32) return static_cast<LRESULT>(drawing.scene_paths());
            if (wparam == 22 || wparam == 23) {
                std::size_t count{};
                for (const auto& peer : peers) if (peer->list)
                    count += wparam == 22 ? peer->list->thumbnail_count() : peer->list->thumbnail_ready();
                return static_cast<LRESULT>(count);
            }
            if (wparam == 17) {
                std::uint64_t count{}; for (const auto& task : samples) count += task->delivered;
                return static_cast<LRESULT>(count);
            }
            if (wparam == 18 || wparam == 19 || wparam == 21) {
                for (const auto& peer : peers) if (auto* grid = dynamic_cast<DataGrid*>(peer->control.get())) {
                    if (wparam == 21) return grid->header_focus();
                    const auto [first, last] = grid->visible_rows();
                    return static_cast<LRESULT>(wparam == 18 ? last - first : grid->source() ? grid->source()->size() : 0);
                }
                return 0;
            }
            if (wparam == 14) return static_cast<LRESULT>(peers.size());
            if (wparam == 24) return static_cast<LRESULT>(popups.size());
            if (wparam == 25) return tooltip_shown;
            if (wparam == 15) return static_cast<LRESULT>(std::count_if(tasks.begin(), tasks.end(),
                [](const auto& task) { return !task->cancelled; }));
            if (wparam == 16) return static_cast<LRESULT>(tasks.size());
            if (wparam == 11) return static_cast<LRESULT>(Drawing::live_targets());
            if (wparam == 12) return static_cast<LRESULT>(Drawing::created_text_layouts());
            if (wparam == 13) {
                size_t count{};
                for (const auto& peer : peers) if (peer->text_layout) ++count;
                return static_cast<LRESULT>(count);
            }
            if (wparam == 0) {
                return static_cast<LRESULT>(paints);
            }
            if (wparam == 1 || wparam == 8 || wparam == 9) {
                for (const auto& peer : peers) if (peer->list) {
                    const auto& list = static_cast<FileList&>(*peer->control);
                    return static_cast<LRESULT>(wparam == 1 ? peer->list->rows() :
                        wparam == 8 ? list.model().visible_indices().size() : list.model().items()->size());
                }
            }
            if (!tasks.empty()) {
                const auto index = static_cast<std::size_t>(lparam);
                if (index >= tasks.size()) return 0;
                const auto& task = tasks[index];
                if (wparam == 3) return static_cast<LRESULT>(task->generation);
                if (wparam == 7) return static_cast<LRESULT>(task->applied);
                if (wparam == 4 || wparam == 10) return task->generation != task->applied;
                if (wparam == 5) return task->worker && task->worker->busy();
            }
            if (wparam == 2) return static_cast<LRESULT>(layouts);
            if (wparam == 6) return options.theme == ThemeMode::dark ? 0 : 1;
            return 0;
        case WM_CLOSE: destroy(); return 0;
        case WM_DESTROY:
            for (auto& task : samples) task->cancel();
            ready = false;
            drawing.discard();
            for (auto& task : tasks) task->cancel();
            detach();
            PostQuitMessage(failed ? 1 : 0);
            quit_posted = true;
            return 0;
        case WM_NCDESTROY:
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            window = nullptr;
            break;
        }
        return DefWindowProcW(hwnd, message, wparam, lparam);
    }
};

Window::Window(WindowOptions options) : impl_(std::make_shared<Impl>(std::move(options))) {}
Window::~Window() { impl_->teardown(); }
void Window::set_title(std::wstring title) {
    if (GetCurrentThreadId() != impl_->owner_thread) throw std::logic_error("Set window title on its UI thread");
    if (impl_->closing || (impl_->used && !impl_->ready)) throw std::logic_error("The Window is closed");
    if (title.find(L'\0') != std::wstring::npos) throw std::invalid_argument("Window title must not contain a null character");
    if (impl_->options.title == title) return;
    if (impl_->window) win32_require(SetWindowTextW(impl_->window, title.c_str()) != 0, "Set window title");
    if (impl_->titlebar) impl_->titlebar->set_title(title);
    impl_->options.title = std::move(title);
}
const std::wstring& Window::title() const { return impl_->options.title; }
const std::shared_ptr<TitleBar>& Window::titlebar() const { return impl_->titlebar; }
void Window::set_content(std::shared_ptr<Stack> content) {
    if (impl_->used) throw std::logic_error("Set window content before Application::run");
    if (!content) throw std::invalid_argument("Window content must not be null");
    if (impl_->titlebar) {
        if (impl_->root) throw std::logic_error("Custom title bar content is already attached");
        auto root = std::make_shared<Stack>(Axis::vertical);
        root->add(impl_->titlebar); root->add(std::move(content), 1); impl_->root = std::move(root);
    } else impl_->root = std::move(content);
}
void Window::set_theme(ThemeMode theme) {
    if (impl_->options.theme == theme) return;
    impl_->options.theme = theme;
    impl_->apply_theme();
}
ThemeMode Window::theme() const { return impl_->options.theme; }
void Window::set_visual_style(VisualStyle style) {
    if (GetCurrentThreadId() != impl_->owner_thread) throw std::logic_error("Set visual style on its UI thread");
    if (impl_->closing || (impl_->used && !impl_->ready)) throw std::logic_error("The Window is closed");
    if (style < VisualStyle::classic || style > VisualStyle::winui)
        throw std::invalid_argument("Invalid visual style");
    if (impl_->options.visual_style == style) return;
    impl_->options.visual_style = style;
    impl_->apply_theme();
    impl_->invalidate(Invalidation::layout);
}
VisualStyle Window::visual_style() const { return impl_->options.visual_style; }
const std::wstring& Window::error() const { return impl_->error; }
bool Window::focus(Control& control, bool select_all) {
    const auto impl = impl_;
    Impl::InputScope scope(*impl);
    if (!impl->ready || impl->closing || !control.focusable()) return false;
    if (impl->layout_pending) impl->update();
    if (!impl->ready || impl->closing) return false;
    for (const auto& peer : impl->peers)
        if (peer->control.get() == &control) {
            if (!impl->focus(*peer, false)) return false;
            if (peer->edit && select_all) SendMessageW(peer->window, EM_SETSEL, 0, -1);
            return true;
        }
    return false;
}
void Window::show_popup(std::shared_ptr<Popup> popup, Control& anchor, Control* initial) {
    if (GetCurrentThreadId() != impl_->owner_thread) throw std::logic_error("Show popups on the window UI thread");
    const auto impl = impl_;
    Impl::InputScope scope(*impl);
    impl->show_popup(std::move(popup), anchor, initial);
}
void Window::show_dialog(std::shared_ptr<ContentDialog> dialog, Control& anchor, Control* initial) {
    if (!dialog) throw std::invalid_argument("Dialog is required");
    if (GetCurrentThreadId() != impl_->owner_thread) throw std::logic_error("Show dialogs on the window UI thread");
    const auto impl = impl_;
    Impl::InputScope scope(*impl);
    std::weak_ptr<Impl> host = impl;
    std::weak_ptr<ContentDialog> weak = dialog;
    dialog->bind_close([host, weak](DialogResult result) {
        if (auto self = host.lock()) if (auto dialog = weak.lock())
            self->dismiss_popup(*dialog->popup(), result == DialogResult::primary ? PopupDismissReason::commit : PopupDismissReason::cancel);
    });
    impl->show_popup(dialog->popup(), anchor, initial, dialog);
}
void Window::dismiss_popup(Popup& popup, PopupDismissReason reason) {
    if (GetCurrentThreadId() != impl_->owner_thread) throw std::logic_error("Dismiss popups on the window UI thread");
    const auto impl = impl_;
    Impl::InputScope scope(*impl);
    impl->dismiss_popup(popup, reason);
}
void Window::show_commands(std::shared_ptr<CommandSurface> surface, Control& anchor) {
    const auto impl = impl_;
    if (GetCurrentThreadId() != impl->owner_thread) throw std::logic_error("Show commands on the window UI thread");
    Impl::InputScope scope(*impl); impl->show_commands(std::move(surface), anchor);
}
void Window::show_location_picker(std::shared_ptr<LocationPicker> picker, Control& anchor) {
    if (!picker) throw std::invalid_argument("Location picker is null");
    const auto impl = impl_;
    if (GetCurrentThreadId() != impl->owner_thread) throw std::logic_error("Show locations on the window UI thread");
    Impl::InputScope scope(*impl);
    impl->show_popup(picker->popup(), anchor, picker->editor().get());
    for (auto& entry : impl->popups) if (entry.popup == picker->popup()) entry.location = picker;
}
void Window::show_shell_commands(Control& anchor, const std::vector<std::wstring>& paths) {
    const auto impl = impl_;
    if (GetCurrentThreadId() != impl->owner_thread) throw std::logic_error("Show Shell commands on the window UI thread");
    Impl::InputScope scope(*impl);
    auto* peer = impl->find_peer(&anchor);
    if (!peer || !impl->ready || impl->closing || !impl->enabled(*peer)) throw std::logic_error("Shell command owner is unavailable");
    POINT point{}; GetCursorPos(&point);
    RECT bounds{}; GetWindowRect(peer->window, &bounds);
    if (!PtInRect(&bounds, point)) point = {bounds.left + 12, bounds.top + 12};
    track_shell_commands(impl->window, paths, {static_cast<float>(point.x), static_cast<float>(point.y)});
}
void Window::close() {
    auto impl = impl_;
    Impl::InputScope input_scope(*impl);
    impl->closing = true;
    impl->close_posts();
    for (auto& task : impl->samples) task->cancel();
    for (auto& task : impl->tasks) task->cancel();
    for (const auto& peer : impl->peers) {
        if (peer->runtime) peer->runtime->cancel_owner();
        if (auto* map = dynamic_cast<MapView*>(peer->control.get())) map->cancel_request();
    }
    if (impl->window) PostMessageW(impl->window, WM_CLOSE, 0, 0);
}
void Window::on_key(std::function<bool(const KeyEvent&)> callback) { impl_->key = std::move(callback); }
bool Window::post(std::function<void()> callback) {
    if (!callback) throw std::invalid_argument("A posted callback is required");
    const auto impl = impl_;
    std::lock_guard lock(impl->post_mutex);
    if (impl->posts_closed) return false;
    impl->posts.push_back(std::move(callback));
    SetEvent(impl->wake->event);
    return true;
}
void Window::on_navigation(std::function<bool(const NavigationEvent&)> callback) { impl_->navigation = std::move(callback); }
std::shared_ptr<SampleTask> Window::create_sample_task(SampleTask::Loader loader, SampleTask::Receiver receive, unsigned interval) {
    if (impl_->closing || (impl_->used && !impl_->ready)) throw std::logic_error("The Window is closed");
    if (!loader || !receive) throw std::invalid_argument("A sample task requires a loader and receiver");
    std::erase_if(impl_->samples, [](const auto& task) { return task->cancelled; });
    auto state = std::make_shared<SampleTask::Impl>();
    state->receive = std::move(receive);
    state->start(std::move(loader), impl_->wake, interval);
    impl_->samples.push_back(state);
    return std::shared_ptr<SampleTask>(new SampleTask(std::move(state)));
}
bool Window::confirm(const std::wstring& title, const std::wstring& message) {
    if (!impl_->ready || impl_->closing) return false;
    return MessageBoxW(impl_->window, message.c_str(), title.c_str(),
        MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES;
}
std::shared_ptr<ViewTask> Window::create_view_task(ViewWorker::Loader loader, std::function<void(ViewResult)> receive) {
    if (impl_->closing || (impl_->used && !impl_->ready)) throw std::logic_error("The Window is closed");
    std::erase_if(impl_->tasks, [](const auto& task) { return task->cancelled; });
    auto state = std::make_shared<ViewTask::Impl>();
    state->receive = std::move(receive);
    state->worker = std::make_shared<ViewWorker>(std::move(loader), [wake = impl_->wake] { SetEvent(wake->event); });
    state->worker->start();
    impl_->tasks.push_back(state);
    return std::shared_ptr<ViewTask>(new ViewTask(std::move(state)));
}
void Window::copy_text(const std::wstring& text) {
    if (!impl_->ready || impl_->closing || !impl_->window)
        throw std::logic_error("Clipboard text requires an open Window");
    const auto bytes = (text.size() + 1) * sizeof(wchar_t);
    auto storage = GlobalAlloc(GMEM_MOVEABLE, bytes);
    win32_require(storage != nullptr, "Allocate clipboard text");
    auto destination = GlobalLock(storage);
    if (!destination) { GlobalFree(storage); win32_require(false, "Lock clipboard text"); }
    memcpy(destination, text.c_str(), bytes);
    GlobalUnlock(storage);
    if (!OpenClipboard(impl_->window)) { GlobalFree(storage); win32_require(false, "Open clipboard"); }
    const bool copied = EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, storage);
    const bool closed = CloseClipboard() != 0;
    if (!copied) GlobalFree(storage);
    win32_require(copied, "Copy clipboard text");
    win32_require(closed, "Close clipboard");
}
int Application::run(Window& window) {
    // A command can delete its public Window. Retain the backend until dispatch
    // unwinds, but Window destruction still closes native windows immediately.
    const auto impl = window.impl_;
    if (running) {
        impl->error = L"Another window is already running on this UI thread.";
        return 1;
    }
    running = true;
    struct Reset { ~Reset() { running = false; } } reset;
    try {
        platform::Runtime runtime;
        bool shutdown_attempted{};
        try {
            impl->create();
            const int result = runtime.run([&](MSG& msg) {
                try { return impl->translate(msg); }
                catch (...) { impl->fail(); return true; }
            },
                impl->wake->event, [&] {
                    try {
                        std::vector<std::function<void()>> posts;
                        { std::lock_guard lock(impl->post_mutex); posts.swap(impl->posts); }
                        for (auto& post : posts) {
                            if (!impl->ready || impl->closing) break;
                            post();
                        }
                        const auto tasks = impl->tasks;
                        for (const auto& task : tasks) {
                            if (!impl->ready || impl->closing) break;
                            task->deliver();
                        }
                        impl->deliver_images();
                        const auto samples = impl->samples;
                        for (const auto& task : samples) {
                            if (!impl->ready || impl->closing) break;
                            task->deliver();
                        }
                    } catch (...) { impl->fail(); }
                });
            impl->quit_posted = false;
            impl->teardown();
            shutdown_attempted = true;
            if (!NativeRuntimeHost::drain_shutdown())
                throw std::runtime_error("Owned web runtime shutdown did not complete within thirty seconds");
            return impl->failed ? 1 : result;
        } catch (...) {
            impl->teardown();
            if (!shutdown_attempted) NativeRuntimeHost::drain_shutdown();
            if (impl->quit_posted) {
                MSG quit{};
                PeekMessageW(&quit, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE);
                impl->quit_posted = false;
            }
            throw;
        }
    } catch (const std::exception& error) {
        impl->error = exception_message(error);
        OutputDebugStringW(impl->error.c_str());
        impl->teardown();
        return 1;
    }
}

}
