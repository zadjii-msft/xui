#include "xui/application.hpp"
#include "xui/native_edit.hpp"
#include "control_accessibility.hpp"
#include "window_host.hpp"
#include "platform.hpp"
#include "list_peer.hpp"
#include "async.hpp"
#include "images.hpp"
#include "workspace_accessibility.hpp"
#include "xui/data_grid.hpp"
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
constexpr wchar_t window_class[] = L"Xui.Window.1";
constexpr wchar_t control_class[] = L"Xui.Control.1";
thread_local bool running{};
}

struct Window::Impl {
    struct Peer {
        Impl& host;
        std::shared_ptr<Control> control;
        HWND window{}, caption{};
        std::unique_ptr<NativeEditBridge> edit;
        std::unique_ptr<ListPeer> list;
        std::unique_ptr<ImagePeer> image;
        std::shared_ptr<ControlAccessibility> accessibility = std::make_shared<ControlAccessibility>();
        IRawElementProviderSimple* provider{};
        std::shared_ptr<ControlAccessibility> caption_accessibility;
        IRawElementProviderSimple* caption_provider{};
        bool tracking{}, surface{};
        Rect paint_bounds{};
        RECT placed_bounds{};
        bool placed{};
        Peer* parent{};
        Microsoft::WRL::ComPtr<IDWriteTextLayout> text_layout;
        bool dragging{};
        float drag_y{}, drag_offset{};
        int wheel_remainder{};
        int grid_drag{};
        std::size_t grid_column{};
        Peer(Impl& owner, std::shared_ptr<Control> value) : host(owner), control(std::move(value)) {}
        ~Peer() {
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
    std::vector<std::unique_ptr<Peer>> peers;
    std::vector<HWND> focus_targets;
    HWND window{}, last_focus{};
    UINT dpi{96};
    Drawing drawing;
    Palette palette{};
    HBRUSH background{}, field{};
    HFONT font{};
    bool used{}, pending{}, layout_pending{}, ready{}, syncing{}, failed{}, quit_posted{}, attached{}, closing{};
    std::uint64_t paints{}, layouts{};
    std::shared_ptr<TaskWake> wake = std::make_shared<TaskWake>();
    std::vector<std::shared_ptr<ViewTask::Impl>> tasks;
    std::vector<std::shared_ptr<SampleTask::Impl>> samples;
    std::function<bool(const KeyEvent&)> key;
    bool has_images{};

    explicit Impl(WindowOptions value) : options(std::move(value)) {}
    ~Impl() { teardown(); }
    void destroy() {
        drawing.discard();
        if (window) DestroyWindow(window);
    }
    void detach() {
        if (!attached) return;
        root->set_invalidator({});
        for (const auto& peer : peers) peer->control->set_text_measurer({});
        for (const auto& peer : peers) if (peer->image) peer->image->detach();
        if (has_images) clear_image_cache();
        attached = false;
    }
    void teardown() {
        for (auto& task : samples) task->cancel();
        for (auto& task : tasks) task->cancel();
        detach();
        ready = false;
        destroy();
        peers.clear();
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
        try { return peer->host.control_message(*peer, hwnd, message, wparam, lparam); }
        catch (...) { peer->host.fail(); return 0; }
    }
    static LRESULT CALLBACK native_clip_procedure(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam,
        UINT_PTR id, DWORD_PTR data) noexcept {
        auto& peer = *reinterpret_cast<Peer*>(data);
        const bool caption = hwnd == peer.caption;
        auto* provider = caption ? peer.caption_provider : peer.provider;
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
    void collect(const std::shared_ptr<Element>& element, bool surface = false, Peer* parent = nullptr) {
        if (auto stack = std::dynamic_pointer_cast<Stack>(element)) {
            for (size_t i = 0; i < stack->child_count(); ++i) collect(stack->child_at(i), surface || stack->surface(), parent);
            return;
        }
        auto control = std::dynamic_pointer_cast<Control>(element);
        if (!control) throw std::invalid_argument("Window content supports Stack and standard controls only");
        for (size_t i = 0; i < peers.size(); ++i) if (peers[i]->control == control) {
            auto* peer = peers[i].get();
            peer->surface = surface;
            if (auto scroll = std::dynamic_pointer_cast<ScrollView>(control)) collect(scroll->content(), surface, peer);
            if (auto content = std::dynamic_pointer_cast<ContentView>(control)) collect(content->content(), surface, peer);
            if (auto split = std::dynamic_pointer_cast<SplitView>(control)) {
                collect(split->first(), surface, peer);
                collect(split->second(), surface, peer);
            }
            return;
        }
        auto peer = std::make_unique<Peer>(*this, std::move(control));
        peer->surface = surface;
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
        } else if (role == ControlRole::text_input) {
            auto& input = static_cast<TextInput&>(*peer->control);
            // The preceding native STATIC supplies EDIT's accessible name.
            peer->caption = CreateWindowExW(0, L"STATIC", peer->control->name().c_str(),
                WS_CHILD | (input.search_style() ? 0 : WS_VISIBLE) | SS_LEFT | SS_NOPREFIX, 0, 0, 1, 1, native_parent,
                nullptr, GetModuleHandleW(nullptr), nullptr);
            win32_require(peer->caption != nullptr, "Create text input label");
            peer->edit = std::make_unique<NativeEditBridge>();
            peer->edit->on_failure([this] { fail(); });
            peer->edit->set_placeholder(input.placeholder());
            peer->edit->set_insets({input.search_style() ? 40.0f : 12.0f, 10,
                input.shortcut_hint().empty() ? 12.0f : 78.0f, 10});
            peer->edit->attach(native_parent, static_cast<int>(100 + peers.size()));
            peer->window = peer->edit->window();
            if (parent) {
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
            const DWORD tab = peer->control->focusable() ? WS_TABSTOP : 0;
            peer->window = CreateWindowExW(WS_EX_TRANSPARENT, control_class, peer->control->name().c_str(),
                WS_CHILD | WS_VISIBLE | tab, 0, 0, 1, 1, native_parent,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(100 + peers.size())),
                GetModuleHandleW(nullptr), peer.get());
            win32_require(peer->window != nullptr, "Create control");
            publish_control(peer->accessibility, nullptr, *peer->control, peer->window);
            peer->provider = role == ControlRole::data_grid ? create_grid_provider(peer->accessibility) :
                role == ControlRole::tab_strip || role == ControlRole::split_view ?
                create_workspace_provider(peer->accessibility) : create_control_provider(peer->accessibility);
        }
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
        peers.push_back(std::move(peer));
        if (auto scroll = std::dynamic_pointer_cast<ScrollView>(added->control))
            collect(scroll->content(), surface, added);
        if (auto content = std::dynamic_pointer_cast<ContentView>(added->control))
            collect(content->content(), surface, added);
        if (auto split = std::dynamic_pointer_cast<SplitView>(added->control)) {
            collect(split->first(), surface, added);
            collect(split->second(), surface, added);
        }
    }
    void create() {
        if (used) throw std::logic_error("A Window can run only once");
        if (closing) throw std::logic_error("The Window is closed");
        used = true;
        if (!root) throw std::logic_error("Window content is required");
        drawing.initialize();
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
        palette = Palette::system(options.theme);
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
        HFONT next_font = CreateFontW(-MulDiv(static_cast<int>(VisualMetrics::caption_size), dpi, 96), 0, 0, 0, FW_NORMAL,
            FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        win32_require(next_font != nullptr, "Create input caption font");
        for (const auto& peer : peers) {
            if (peer->edit) {
                peer->edit->set_dpi(dpi);
                SendMessageW(peer->caption, WM_SETFONT, reinterpret_cast<WPARAM>(next_font), TRUE);
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
        const auto before = peers.size();
        collect(root);
        if (peers.size() != before) apply_theme();
        if (layout_pending) {
            layout_pending = false;
            RECT client{};
            win32_require(GetClientRect(window, &client) != 0, "Read content bounds");
            const Size size{client.right * 96.0f / dpi, client.bottom * 96.0f / dpi};
            root->measure(size);
            root->arrange({0, 0, size.width, size.height});
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
            for (const auto& peer : peers) {
                auto bounds = peer->control->bounds();
                if (peer->parent) {
                    const auto parent = peer->parent->control->bounds();
                    bounds.x -= parent.x;
                    bounds.y -= parent.y;
                }
                if (peer->edit) {
                    const bool search = static_cast<TextInput&>(*peer->control).search_style();
                    const auto& input = static_cast<TextInput&>(*peer->control);
                    peer->edit->set_insets({search ? 40.0f : 12.0f, 10,
                        input.shortcut_hint().empty() ? 12.0f : 78.0f, 10});
                    ShowWindow(peer->caption, search || !visible(*peer) ? SW_HIDE : SW_SHOWNA);
                    if (!search) {
                        platform::place(peer->caption, {bounds.x, bounds.y, bounds.width, std::min(24.0f, bounds.height)}, dpi);
                        bounds.y += 24;
                        bounds.height = std::max(0.0f, bounds.height - 24);
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
            update_paint_bounds();
            ++layouts;
        }
        focus_targets.clear();
        if (has_images) sync_images();
        HWND disabled_focus{};
        for (const auto& peer : peers) {
            const auto& control = *peer->control;
            if (control.focusable() && visible(*peer) &&
                (control.role() != ControlRole::split_view || static_cast<const SplitView&>(control).expanded()))
                focus_targets.push_back(peer->window);
            if ((!enabled(*peer) || !visible(*peer)) && GetFocus() == peer->window) disabled_focus = peer->window;
            if (!peer->list && !control.captured() && !peer->dragging && !peer->grid_drag && GetCapture() == peer->window) ReleaseCapture();
            if (!has_images || (IsWindowEnabled(peer->window) != FALSE) != enabled(*peer))
                EnableWindow(peer->window, enabled(*peer));
            if (!enabled(*peer)) {
                peer->control->cancel();
                peer->control->pointer_move(false);
                peer->dragging = false;
                if (GetCapture() == peer->window) ReleaseCapture();
            }
            if (peer->edit) {
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
                SetWindowTextW(peer->caption, input.name().c_str());
                InvalidateRect(peer->caption, nullptr, FALSE);
            } else if (peer->list) {
                if (GetFocus() == peer->window) last_focus = peer->window;
                peer->list->update(dpi, palette);
            } else {
                publish_control(peer->accessibility, peer->provider, control, peer->window);
            }
            if (peer->edit) InvalidateRect(peer->window, nullptr, FALSE);
        }
        if (disabled_focus) {
            if (!platform::traverse_focus(focus_targets, false, disabled_focus)) SetFocus(window);
        }
        InvalidateRect(window, nullptr, FALSE);
    }
    bool translate(MSG& msg) {
        if ((msg.message != WM_KEYDOWN && msg.message != WM_SYSKEYDOWN) || (msg.hwnd != window && !IsChild(window, msg.hwnd))) return false;
        for (const auto& peer : peers) if (peer->window == msg.hwnd && peer->edit) {
            if (peer->edit->composing()) return false;
            if (peer->edit->suggestion_key(msg.wParam)) return true;
        }
        Control* target{};
        for (const auto& peer : peers) if (peer->window == msg.hwnd) target = peer->control.get();
        if (key) {
            auto callback = key;
            if (callback({static_cast<Key>(msg.wParam), (GetKeyState(VK_CONTROL) & 0x8000) != 0,
                (GetKeyState(VK_SHIFT) & 0x8000) != 0, target, (GetKeyState(VK_MENU) & 0x8000) != 0})) return true;
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
            if (peers[index]->control->focusable() && enabled(*peers[index]) && focus(*peers[index], false)) return;
        }
    }
    bool enabled(const Peer& peer) const {
        for (auto* ancestor = &peer; ancestor; ancestor = ancestor->parent)
            if (!ancestor->control->enabled() || (ancestor->control->role() == ControlRole::content_view &&
                (ancestor->control->bounds().width <= 0 || ancestor->control->bounds().height <= 0))) return false;
        return true;
    }
    static Rect viewport(const Peer& peer) {
        if (auto scroll = dynamic_cast<ScrollView*>(peer.control.get())) return scroll->viewport();
        return peer.control->bounds();
    }
    bool visible(const Peer& peer) const {
        for (auto* ancestor = &peer; ancestor; ancestor = ancestor->parent) {
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
            drawing.rounded(stack.bounds(), palette.surface);
            drawing.rounded(stack.bounds(), palette.border, 6, true);
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
        PAINTSTRUCT paint{};
        BeginPaint(window, &paint);
        bool redraw{};
        try {
            if (drawing.begin(window, static_cast<float>(dpi), palette.background)) {
                paint_surfaces(*root);
                for (const auto& peer : peers) {
                    if (!visible(*peer)) continue;
                    for (auto* parent = peer->parent; parent; parent = parent->parent)
                        drawing.push_clip(viewport(*parent));
                    if (peer->edit) paint_edit(*peer);
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
                }
                redraw = !drawing.end();
                ++paints;
                // Transparent custom HWNDs retain input and UIA, but not targets.
                // WS_CLIPCHILDREN protects opaque native EDIT and caption pixels.
                for (const auto& peer : peers)
                    if (!peer->edit) ValidateRect(peer->window, nullptr);
                // A transparent viewport does not protect its opaque descendants
                // in the root target's clip region. Native pixels must paint last.
                for (const auto& peer : peers) if (peer->edit && peer->parent) {
                    RedrawWindow(peer->caption, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
                    RedrawWindow(peer->window, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
                }
            }
        } catch (...) { EndPaint(window, &paint); throw; }
        EndPaint(window, &paint);
        if (redraw) invalidate(Invalidation::paint);
    }
    void sync_images() {
        std::vector<std::uint64_t> retained;
        for (const auto& peer : peers) if (peer->image) {
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
            peer->image->sync(rect.width > 0 && rect.height > 0, wake);
            if (peer->image->pixels) retained.push_back(peer->image->pixels->id);
        }
        drawing.keep_images(retained);
    }
    void deliver_images() {
        if (!ready || closing || !has_images) return;
        bool changed{};
        for (const auto& peer : peers) if (peer->image) changed = peer->image->deliver() || changed;
        if (changed) invalidate(Invalidation::paint);
    }
    void paint_edit(Peer& peer) {
        auto bounds = peer.control->bounds();
        const auto& input = static_cast<TextInput&>(*peer.control);
        if (!input.search_style()) {
            bounds.y += 24;
            bounds.height = std::max(0.0f, bounds.height - 24);
        }
        drawing.rounded(bounds, palette.field);
        drawing.rounded(bounds, GetFocus() == peer.window ? palette.accent : palette.border, 6, true);
        if (input.search_style()) drawing.search_icon({bounds.x + 12, bounds.y + 13, 18, 18}, palette.secondary);
        const auto& hint = input.shortcut_hint();
        if (!hint.empty()) {
            drawing.rounded({bounds.x + bounds.width - 62, bounds.y + 12, 50, 20}, palette.border, 4, true);
            drawing.text(hint, {bounds.x + bounds.width - 56, bounds.y + 12, 42, 20}, palette.secondary, true);
        }
    }
    void paint_control(Peer& peer) {
        auto& canvas = drawing;
        auto& control = *peer.control;
        const auto bounds = peer.paint_bounds;
        if (auto* grid = dynamic_cast<DataGrid*>(&control)) {
            canvas.fill({0, 0, bounds.width, bounds.height}, palette.background);
            canvas.push_clip({0, 0, grid->viewport_width(), bounds.height});
            const auto [first, end] = grid->visible_rows();
            const auto& source = grid->source();
            canvas.push_clip({0, DataGrid::header_height, grid->viewport_width(), grid->viewport_height()});
            for (auto row = first; row < end; ++row) {
                const float y = DataGrid::header_height + static_cast<float>(row * double(DataGrid::row_height) - grid->offset());
                const bool selected = grid->selected() == source->key(row);
                if (selected || row % 2) canvas.fill({0, y, grid->viewport_width(), DataGrid::row_height},
                    selected ? palette.selection : palette.surface);
                float x = -static_cast<float>(grid->horizontal_offset());
                for (std::size_t c = 0; c < grid->columns().size(); ++c) {
                    const auto& column = grid->columns()[c];
                    if (x + column.width > 0 && x < grid->viewport_width())
                        canvas.cell_text(source->text(row, c), {x + 12, y, column.width - 24, DataGrid::row_height},
                            selected ? palette.selection_text : palette.text, column.numeric);
                    x += column.width;
                }
                if (selected && control.focused() && !grid->header_focus())
                    canvas.outline({1, y + 1, std::max(0.0f, grid->viewport_width() - 2), DataGrid::row_height - 2}, palette.accent);
            }
            if (!source || !source->size()) canvas.text(L"No matching rows", {16, 52, grid->viewport_width() - 32, 40}, palette.secondary);
            canvas.pop_clip();
            canvas.fill({0, 0, grid->viewport_width(), DataGrid::header_height}, palette.surface);
            float x = -static_cast<float>(grid->horizontal_offset());
            for (std::size_t c = 0; c < grid->columns().size(); ++c) {
                const auto& column = grid->columns()[c];
                canvas.cell_text(column.name + (c == grid->sort_column() ? (grid->descending() ? L" \u2193" : L" \u2191") : L""),
                    {x + 12, 0, column.width - 24, DataGrid::header_height}, palette.secondary, column.numeric);
                canvas.line(x + column.width, 8, x + column.width, DataGrid::header_height - 8, palette.border);
                if (control.focused() && grid->header_focus() && grid->focused_column() == c)
                    canvas.outline({x + 1, 1, column.width - 2, DataGrid::header_height - 2}, palette.accent);
                x += column.width;
            }
            canvas.pop_clip();
            canvas.line(0, DataGrid::header_height, grid->viewport_width(), DataGrid::header_height, palette.border);
            const auto vertical = grid->vertical_thumb(), horizontal = grid->horizontal_thumb();
            if (vertical.height) canvas.rounded(vertical, palette.secondary, 3);
            if (horizontal.width) canvas.rounded(horizontal, palette.secondary, 3);
            return;
        }
        if (auto* chart = dynamic_cast<HistoryChart*>(&control)) {
            const Rect plot{12, 38, std::max(0.0f, bounds.width - 24), std::max(0.0f, bounds.height - 58)};
            canvas.rounded({1, 1, bounds.width - 2, bounds.height - 2}, palette.surface);
            canvas.text(chart->name(), {12, 2, bounds.width - 24, 32}, palette.text, true);
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
                control.focused() || peer.dragging ? palette.accent : palette.border, 2);
            return;
        }
        if (control.role() == ControlRole::tab_strip) {
            const auto& strip = static_cast<TabStrip&>(control);
            canvas.fill({0, 0, bounds.width, bounds.height}, palette.background);
            for (std::size_t i = 0; i < strip.tabs().size(); ++i) {
                auto b = strip.tab_bounds(i);
                if (b.width <= 0) continue;
                const bool selected = strip.selected() == strip.tabs()[i].id;
                canvas.rounded({b.x + 2, 2, std::max(0.0f, b.width - 4), b.height - 4},
                    selected ? palette.selection : palette.surface, 5);
                const auto ink = selected ? palette.selection_text : palette.secondary;
                canvas.text(strip.tabs()[i].title, {b.x + 12, 0, std::max(0.0f, b.width - 42), b.height}, ink, true);
                if (strip.closable() && b.width >= 48) {
                    canvas.line(b.x + b.width - 22, 15, b.x + b.width - 14, 23, ink);
                    canvas.line(b.x + b.width - 22, 23, b.x + b.width - 14, 15, ink);
                }
                if (selected && control.focused())
                    canvas.rounded({b.x + 2, 2, std::max(0.0f, b.width - 4), b.height - 4}, palette.accent, 5, true);
            }
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
            return;
        }
        if (control.role() == ControlRole::scroll_view) {
            const auto& scroll = static_cast<ScrollView&>(control);
            auto thumb = scroll.thumb();
            thumb.x -= bounds.x;
            thumb.y -= bounds.y;
            canvas.fill({std::max(0.0f, bounds.width - ScrollView::bar_width), 0,
                std::min(bounds.width, ScrollView::bar_width), bounds.height}, palette.background);
            if (thumb.height) canvas.rounded(thumb, control.enabled() ?
                (peer.dragging || control.hovered() ? palette.accent : palette.secondary) : palette.border, 4);
            if (control.focused()) canvas.outline({0.5f, 0.5f, std::max(0.0f, bounds.width - 1),
                std::max(0.0f, bounds.height - 1)}, palette.accent);
            return;
        }
        control.measured_text();
        canvas.fill({0, 0, bounds.width, bounds.height}, peer.surface ? palette.surface : palette.background);
        const Rect box{2, 2, std::max(0.0f, bounds.width - 4), std::max(0.0f, bounds.height - 4)};
        const auto text = enabled(peer) ? palette.text : palette.disabled;
        const auto role = control.role();
        if (role == ControlRole::label) {
            const auto& label = static_cast<const Label&>(control);
            const auto color = !enabled(peer) ? palette.disabled : label.tone() == TextTone::accent ? palette.accent :
                label.tone() == TextTone::secondary ? palette.secondary :
                label.tone() == TextTone::error ? palette.error : text;
            const Rect area{0, 0, bounds.width, bounds.height};
            canvas.text_layout(peer.text_layout.Get(), area, color);
        } else {
            if (role == ControlRole::button || control.hovered() || control.pressed())
                canvas.rounded(box, control.pressed() ? palette.selection :
                    control.hovered() ? palette.hover : palette.surface);
            if (role == ControlRole::button)
                canvas.rounded(box, palette.border, 6, true);
            float inset = 14;
            if (role == ControlRole::toggle) {
                const bool checked = static_cast<const Toggle&>(control).checked();
                Rect mark{12, bounds.height / 2 - 9, 18, 18};
                canvas.rounded(mark, checked ? (enabled(peer) ? palette.accent : palette.disabled) : palette.field, 3);
                canvas.rounded(mark, enabled(peer) ? palette.accent : palette.disabled, 3, true);
                if (checked) {
                    const auto ink = palette.high_contrast && enabled(peer) ? palette.selection_text : palette.background;
                    canvas.line(mark.x + 4, mark.y + 9, mark.x + 8, mark.y + 13, ink, 2);
                    canvas.line(mark.x + 8, mark.y + 13, mark.x + 14, mark.y + 5, ink, 2);
                }
                inset = 42;
            }
            if (role == ControlRole::button)
                inset = std::max(14.0f, (bounds.width - control.measured_text().width) / 2);
            canvas.text_layout(peer.text_layout.Get(),
                {inset, 0, std::max(0.0f, bounds.width - inset - 12), bounds.height},
                control.pressed() ? palette.selection_text : text);
            if (control.focused()) canvas.rounded(box,
                palette.high_contrast && control.pressed() ? palette.selection_text : palette.accent, 6, true);
        }
    }
    void activated(Peer& peer, bool invoked) {
        update();
        if (invoked && window && peer.control->role() == ControlRole::button)
            raise_control_invoked(peer.provider);
    }
    bool focus(Peer& peer, bool activate_window) {
        if (!ready || closing || !peer.control->focusable() || !enabled(peer)) return false;
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
        return GetFocus() == peer.window;
    }
    void cancel_input() {
        for (const auto& peer : peers) {
            if (peer->edit) peer->edit->dismiss_suggestions();
            peer->grid_drag = 0;
            peer->dragging = false;
            peer->control->cancel();
            peer->control->pointer_move(false);
            if (GetCapture() == peer->window) ReleaseCapture();
        }
    }
    LRESULT control_message(Peer& peer, HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
        auto& control = *peer.control;
        const auto inside = [&] {
            RECT rect{};
            GetClientRect(hwnd, &rect);
            return PtInRect(&rect, {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)}) != 0;
        };
        switch (message) {
        case WM_SETCURSOR:
            if (control.role() == ControlRole::split_view && static_cast<SplitView&>(control).expanded() &&
                LOWORD(lparam) == HTCLIENT) { SetCursor(LoadCursorW(nullptr, IDC_SIZEWE)); return TRUE; }
            break;
        case WM_COMMAND:
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLOREDIT:
        case WM_NEXTDLGCTL:
            return SendMessageW(window, message, wparam, lparam);
        case WM_MOUSEWHEEL:
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
            if (auto* grid = dynamic_cast<DataGrid*>(&control); grid && lparam != -1) {
                POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)}; ScreenToClient(hwnd, &point);
                const auto row = grid->row_at(point.y * 96.0f / dpi);
                if (row && point.x * 96.0f / dpi < grid->viewport_width()) grid->select(grid->source()->key(*row), false);
                else grid->clear_selection();
                SetFocus(hwnd);
            }
            show_control_menu(control, hwnd, lparam); return 0;
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
            control.set_focused(false);
            if (GetCapture() == hwnd) ReleaseCapture();
            return 0;
        case WM_MOUSEMOVE:
            if (peer.grid_drag) {
                auto& grid = static_cast<DataGrid&>(control);
                const float x = GET_X_LPARAM(lparam) * 96.0f / dpi, y = GET_Y_LPARAM(lparam) * 96.0f / dpi;
                if (peer.grid_drag == 3) grid.resize_column(peer.grid_column, peer.drag_offset + x - peer.drag_y);
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
            control.pointer_move(inside());
            if (!peer.tracking) {
                TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd, 0};
                peer.tracking = TrackMouseEvent(&track) != 0;
            }
            return 0;
        case WM_MOUSELEAVE: peer.tracking = false; control.pointer_move(false); return 0;
        case WM_LBUTTONDOWN:
            if (auto* grid = dynamic_cast<DataGrid*>(&control); grid && enabled(peer)) {
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
                    float edge = -static_cast<float>(grid->horizontal_offset());
                    for (std::size_t c = 0; c < grid->columns().size(); ++c) {
                        edge += grid->columns()[c].width;
                        if (std::abs(x - edge) <= 5) {
                            peer.grid_drag = 3; peer.grid_column = c; peer.drag_y = x; peer.drag_offset = grid->columns()[c].width;
                            SetCapture(hwnd); return 0;
                        }
                    }
                    if (const auto column = grid->column_at(x)) {
                        grid->focus_header(true); grid->step_header(static_cast<int>(*column) - static_cast<int>(grid->focused_column()));
                        grid->sort(*column);
                    }
                } else if (const auto row = grid->row_at(y)) grid->select(grid->source()->key(*row), false);
                else grid->clear_selection();
                return 0;
            }
            if (auto tabs = dynamic_cast<TabStrip*>(&control); tabs && enabled(peer)) {
                SetFocus(hwnd);
                if (auto index = tabs->hit_test(GET_X_LPARAM(lparam) * 96.0f / dpi)) {
                    const auto b = tabs->tab_bounds(*index);
                    const auto id = tabs->tabs()[*index].id;
                    if (tabs->closable() && b.width >= 48 && GET_X_LPARAM(lparam) * 96.0f / dpi >= b.x + b.width - 30)
                        tabs->request_close(id);
                    else tabs->select(id);
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
            if (control.pointer_down()) { SetFocus(hwnd); SetCapture(hwnd); }
            return 0;
        case WM_LBUTTONUP:
            peer.grid_drag = 0;
            peer.dragging = false;
            activated(peer, control.pointer_up(inside()));
            if (GetCapture() == hwnd) ReleaseCapture();
            return 0;
        case WM_LBUTTONDBLCLK:
            if (auto* grid = dynamic_cast<DataGrid*>(&control); grid && enabled(peer)) {
                if (auto row = grid->row_at(GET_Y_LPARAM(lparam) * 96.0f / dpi)) {
                    grid->select(grid->source()->key(*row), false); grid->activate_selected();
                }
                return 0;
            }
            return control_message(peer, hwnd, WM_LBUTTONDOWN, wparam, lparam);
        case WM_CANCELMODE:
            peer.grid_drag = 0;
            peer.dragging = false;
            control.cancel();
            if (GetCapture() == hwnd) ReleaseCapture();
            return 0;
        case WM_CAPTURECHANGED: peer.grid_drag = 0; peer.dragging = false; control.cancel(); invalidate(Invalidation::paint); return 0;
        case WM_KEYDOWN:
            if (auto* grid = dynamic_cast<DataGrid*>(&control); grid && enabled(peer)) {
                if (wparam == VK_F6) { grid->focus_header(!grid->header_focus()); return 0; }
                if (grid->header_focus()) {
                    if (wparam == VK_LEFT || wparam == VK_RIGHT) { grid->step_header(wparam == VK_LEFT ? -1 : 1); return 0; }
                    if (wparam == VK_RETURN || wparam == VK_SPACE) {
                        if (!(lparam & (1LL << 30))) grid->sort(grid->focused_column());
                        return 0;
                    }
                    if (wparam == VK_DOWN) { grid->focus_header(false); grid->step(0); return 0; }
                } else {
                    if (wparam == VK_UP || wparam == VK_DOWN) { grid->step(wparam == VK_UP ? -1 : 1); return 0; }
                    if (wparam == VK_PRIOR || wparam == VK_NEXT) { grid->step((wparam == VK_PRIOR ? -1 : 1) * std::max(1, static_cast<int>(grid->viewport_height() / DataGrid::row_height))); return 0; }
                    if (wparam == VK_HOME || wparam == VK_END) { grid->edge(wparam == VK_END); return 0; }
                    if (wparam == VK_RETURN) { if (!(lparam & (1LL << 30))) grid->activate_selected(); return 0; }
                    if (wparam == VK_LEFT || wparam == VK_RIGHT) { grid->set_offset(grid->offset(), grid->horizontal_offset() + (wparam == VK_LEFT ? -80 : 80)); return 0; }
                }
            }
            if (auto tabs = dynamic_cast<TabStrip*>(&control); tabs && enabled(peer)) {
                if (wparam == VK_LEFT || wparam == VK_RIGHT) { tabs->step(wparam == VK_LEFT ? -1 : 1); return 0; }
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
                activated(peer, accepted && wparam == VK_RETURN);
                return 0;
            }
            break;
        case WM_KEYUP:
            if (wparam == VK_SPACE || wparam == VK_RETURN) {
                activated(peer, control.key_up(wparam == VK_SPACE ? ActivationKey::space : ActivationKey::enter));
                return 0;
            }
            break;
        case WM_CHAR: if (wparam == VK_SPACE || wparam == VK_RETURN) return 0; break;
        case control_action_message:
            if (wparam != control.id()) return UIA_E_ELEMENTNOTAVAILABLE;
            if (lparam == 6) { reveal(peer); return S_OK; }
            if (!enabled(peer)) return UIA_E_ELEMENTNOTENABLED;
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
        case grid_action_message: {
            if (wparam != control.id()) return UIA_E_ELEMENTNOTAVAILABLE;
            auto* grid = dynamic_cast<DataGrid*>(&control);
            if (!grid) return UIA_E_INVALIDOPERATION;
            GridAction action;
            {
                std::lock_guard lock(peer.accessibility->mutex);
                auto it = peer.accessibility->grid_actions.find(static_cast<std::uint64_t>(lparam));
                if (it == peer.accessibility->grid_actions.end()) return UIA_E_ELEMENTNOTAVAILABLE;
                action = it->second; peer.accessibility->grid_actions.erase(it);
            }
            if (!enabled(peer)) return UIA_E_ELEMENTNOTENABLED;
            if (action.kind == GridAction::scroll) grid->set_offset(action.y, action.x);
            else if (action.kind == GridAction::reveal) {
                if (!action.key || !grid->reveal(*action.key)) return UIA_E_ELEMENTNOTAVAILABLE;
            } else if (action.kind == GridAction::clear) {
                if (!action.key || !grid->source() || !grid->source()->find(*action.key)) return UIA_E_ELEMENTNOTAVAILABLE;
                if (grid->selected() == action.key) grid->clear_selection();
            }
            else if (action.kind == GridAction::sort) {
                if (action.column >= grid->columns().size()) return UIA_E_ELEMENTNOTAVAILABLE;
                grid->sort(action.column);
            } else if (action.kind == GridAction::header_focus) {
                if (!focus(peer, true)) return UIA_E_INVALIDOPERATION;
                grid->focus_header(true);
                grid->step_header(static_cast<int>(action.column) - static_cast<int>(grid->focused_column()));
            } else if (action.kind == GridAction::focus && !action.key) {
                if (!focus(peer, true)) return UIA_E_INVALIDOPERATION;
            } else {
                if (!action.key || !grid->select(*action.key)) return UIA_E_ELEMENTNOTAVAILABLE;
                if (action.kind == GridAction::focus && !focus(peer, true)) return UIA_E_INVALIDOPERATION;
                if (action.kind == GridAction::invoke) grid->activate_selected();
            }
            update(); return S_OK;
        }
        case WM_NCDESTROY:
            control.cancel();
            control.set_focused(false);
            disconnect_control(peer.accessibility, peer.provider);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            peer.window = nullptr;
            break;
        }
        return DefWindowProcW(hwnd, message, wparam, lparam);
    }
    LRESULT message(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
        switch (message) {
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
        case WM_CANCELMODE: cancel_input(); return 0;
        case WM_ACTIVATE: if (LOWORD(wparam) == WA_INACTIVE) cancel_input(); break;
        case WM_ENABLE: if (!wparam) cancel_input(); break;
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: if (ready) paint(); else return DefWindowProcW(hwnd, message, wparam, lparam); return 0;
        case WM_SIZE:
            for (const auto& peer : peers) if (peer->edit) peer->edit->dismiss_suggestions();
            for (auto& task : samples) task->suspend(wparam == SIZE_MINIMIZED);
            if (wparam != SIZE_MINIMIZED) invalidate(Invalidation::layout);
            return 0;
        case WM_SHOWWINDOW:
            for (auto& task : samples) task->suspend(!wparam || IsIconic(hwnd));
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
            for (const auto& peer : peers) if (peer->caption == reinterpret_cast<HWND>(lparam)) {
                on_surface = peer->surface;
                break;
            }
            SetTextColor(dc, platform::native_color(IsWindowEnabled(reinterpret_cast<HWND>(lparam)) ?
                palette.text : palette.disabled));
            SetBkColor(dc, platform::native_color(editable ? palette.field : on_surface ? palette.surface : palette.background));
            if (!editable && on_surface) {
                SetDCBrushColor(dc, platform::native_color(palette.surface));
                return reinterpret_cast<LRESULT>(GetStockObject(DC_BRUSH));
            }
            return reinterpret_cast<LRESULT>(editable ? field : background);
        }
        case WM_COMMAND:
            for (const auto& peer : peers) if (peer->edit && peer->window == reinterpret_cast<HWND>(lparam)) {
                if (HIWORD(wparam) == EN_CHANGE && !peer->edit->composing()) {
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
        case WM_LBUTTONDOWN:
            for (const auto& peer : peers) if (peer->edit && peer->control->enabled()) {
                const auto rect = peer->control->bounds();
                const float x = GET_X_LPARAM(lparam) * 96.0f / dpi;
                const float y = GET_Y_LPARAM(lparam) * 96.0f / dpi;
                if (x >= rect.x && x < rect.x + rect.width && y >= rect.y && y < rect.y + rect.height)
                    peer->edit->focus();
            }
            return 0;
        case metrics_message:
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
                const auto& task = tasks.front();
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

Window::Window(WindowOptions options) : impl_(std::make_unique<Impl>(std::move(options))) {}
Window::~Window() = default;
void Window::set_content(std::shared_ptr<Stack> content) {
    if (impl_->used) throw std::logic_error("Set window content before Application::run");
    if (!content) throw std::invalid_argument("Window content must not be null");
    impl_->root = std::move(content);
}
void Window::set_theme(ThemeMode theme) {
    if (impl_->options.theme == theme) return;
    impl_->options.theme = theme;
    impl_->apply_theme();
}
ThemeMode Window::theme() const { return impl_->options.theme; }
const std::wstring& Window::error() const { return impl_->error; }
bool Window::focus(Control& control, bool select_all) {
    if (!impl_->ready || impl_->closing || !control.focusable()) return false;
    for (const auto& peer : impl_->peers)
        if (peer->control.get() == &control) {
            if (!impl_->focus(*peer, false)) return false;
            if (peer->edit && select_all) SendMessageW(peer->window, EM_SETSEL, 0, -1);
            return true;
        }
    return false;
}
void Window::close() {
    impl_->closing = true;
    for (auto& task : impl_->samples) task->cancel();
    for (auto& task : impl_->tasks) task->cancel();
    if (impl_->window) PostMessageW(impl_->window, WM_CLOSE, 0, 0);
}
void Window::on_key(std::function<bool(const KeyEvent&)> callback) { impl_->key = std::move(callback); }
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
    if (running) {
        window.impl_->error = L"Another window is already running on this UI thread.";
        return 1;
    }
    running = true;
    struct Reset { ~Reset() { running = false; } } reset;
    try {
        platform::Runtime runtime;
        try {
            window.impl_->create();
            const int result = runtime.run([&](MSG& msg) {
                try { return window.impl_->translate(msg); }
                catch (...) { window.impl_->fail(); return true; }
            },
                window.impl_->wake->event, [&] {
                    try {
                        const auto tasks = window.impl_->tasks;
                        for (const auto& task : tasks) {
                            if (!window.impl_->ready || window.impl_->closing) break;
                            task->deliver();
                        }
                        window.impl_->deliver_images();
                        const auto samples = window.impl_->samples;
                        for (const auto& task : samples) {
                            if (!window.impl_->ready || window.impl_->closing) break;
                            task->deliver();
                        }
                    } catch (...) { window.impl_->fail(); }
                });
            window.impl_->quit_posted = false;
            window.impl_->teardown();
            return window.impl_->failed ? 1 : result;
        } catch (...) {
            window.impl_->teardown();
            if (window.impl_->quit_posted) {
                MSG quit{};
                PeekMessageW(&quit, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE);
                window.impl_->quit_posted = false;
            }
            throw;
        }
    } catch (const std::exception& error) {
        window.impl_->error = exception_message(error);
        OutputDebugStringW(window.impl_->error.c_str());
        window.impl_->teardown();
        return 1;
    }
}

}
