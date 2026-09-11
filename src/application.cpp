#include "xui/application.hpp"
#include "xui/native_edit.hpp"
#include "control_accessibility.hpp"
#include "window_host.hpp"
#include "platform.hpp"
#include "list_peer.hpp"
#include "async.hpp"
#include <UIAutomation.h>
#include <commctrl.h>
#include <windowsx.h>
#include <cmath>

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
        Drawing drawing;
        std::shared_ptr<ControlAccessibility> accessibility = std::make_shared<ControlAccessibility>();
        IRawElementProviderSimple* provider{};
        bool tracking{}, surface{};
        Peer(Impl& owner, std::shared_ptr<Control> value) : host(owner), control(std::move(value)) {}
        ~Peer() {
            if (list) window = nullptr;
            if (window && IsWindow(window)) DestroyWindow(window);
            if (caption && IsWindow(caption)) DestroyWindow(caption);
            disconnect_control(accessibility, provider);
            if (provider) provider->Release();
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
    std::function<bool(const KeyEvent&)> key;

    explicit Impl(WindowOptions value) : options(std::move(value)) {}
    ~Impl() { teardown(); }
    void detach() {
        if (!attached) return;
        root->set_invalidator({});
        attached = false;
    }
    void teardown() {
        for (auto& task : tasks) task->cancel();
        detach();
        ready = false;
        if (window) DestroyWindow(window);
        peers.clear();
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
        if (window) DestroyWindow(window);
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
    void collect(const std::shared_ptr<Element>& element, bool surface = false) {
        if (auto stack = std::dynamic_pointer_cast<Stack>(element)) {
            for (size_t i = 0; i < stack->child_count(); ++i) collect(stack->child_at(i), surface || stack->surface());
            return;
        }
        auto control = std::dynamic_pointer_cast<Control>(element);
        if (!control) throw std::invalid_argument("Window content supports Stack and standard controls only");
        for (const auto& peer : peers) if (peer->control == control) { peer->surface = surface; return; }
        auto peer = std::make_unique<Peer>(*this, std::move(control));
        peer->surface = surface;
        const auto role = peer->control->role();
        if (role == ControlRole::file_list) {
            auto list = std::dynamic_pointer_cast<FileList>(peer->control);
            if (!list) throw std::invalid_argument("The file-list role requires a FileList control");
            peer->list = std::make_unique<ListPeer>(std::move(list), [this] { fail(); },
                [this](HWND hwnd) { last_focus = hwnd; });
            peer->list->attach(window, static_cast<int>(100 + peers.size()), drawing);
            peer->window = peer->list->window();
        } else if (role == ControlRole::text_input) {
            auto& input = static_cast<TextInput&>(*peer->control);
            // The preceding native STATIC supplies EDIT's accessible name.
            peer->caption = CreateWindowExW(0, L"STATIC", peer->control->name().c_str(),
                WS_CHILD | (input.search_style() ? 0 : WS_VISIBLE) | SS_LEFT | SS_NOPREFIX, 0, 0, 1, 1, window,
                nullptr, GetModuleHandleW(nullptr), nullptr);
            win32_require(peer->caption != nullptr, "Create text input label");
            peer->edit = std::make_unique<NativeEditBridge>();
            peer->edit->set_placeholder(input.placeholder());
            peer->edit->set_insets(input.search_style() ? Insets{40, 10, 78, 10} : Insets{12, 10, 12, 10});
            peer->edit->attach(window, static_cast<int>(100 + peers.size()));
            peer->window = peer->edit->window();
        } else {
            peer->drawing.initialize(drawing);
            const DWORD tab = role == ControlRole::label ? 0 : WS_TABSTOP;
            peer->window = CreateWindowExW(0, control_class, peer->control->name().c_str(),
                WS_CHILD | WS_VISIBLE | tab, 0, 0, 1, 1, window,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(100 + peers.size())),
                GetModuleHandleW(nullptr), peer.get());
            win32_require(peer->window != nullptr, "Create control");
            publish_control(peer->accessibility, nullptr, *peer->control, peer->window);
            peer->provider = create_control_provider(peer->accessibility);
        }
        peers.push_back(std::move(peer));
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
        HFONT next_font = CreateFontW(-MulDiv(13, dpi, 96), 0, 0, 0, FW_NORMAL,
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
            for (const auto& peer : peers) {
                auto bounds = peer->control->bounds();
                if (peer->edit) {
                    const bool search = static_cast<TextInput&>(*peer->control).search_style();
                    peer->edit->set_insets(search ? Insets{40, 10, 78, 10} : Insets{12, 10, 12, 10});
                    ShowWindow(peer->caption, search ? SW_HIDE : SW_SHOWNA);
                    if (!search) {
                        platform::place(peer->caption, {bounds.x, bounds.y, bounds.width, std::min(24.0f, bounds.height)}, dpi);
                        bounds.y += 24;
                        bounds.height = std::max(0.0f, bounds.height - 24);
                    }
                    peer->edit->arrange(bounds);
                } else platform::place(peer->window, bounds, dpi);
            }
            ++layouts;
        }
        focus_targets.clear();
        HWND disabled_focus{};
        for (const auto& peer : peers) {
            const auto& control = *peer->control;
            if (control.role() != ControlRole::label) focus_targets.push_back(peer->window);
            if (!control.enabled() && GetFocus() == peer->window) disabled_focus = peer->window;
            if (!peer->list && !control.captured() && GetCapture() == peer->window) ReleaseCapture();
            EnableWindow(peer->window, control.enabled());
            if (peer->edit) {
                auto& input = static_cast<TextInput&>(*peer->control);
                peer->edit->set_placeholder(input.placeholder());
                peer->edit->set_placeholder_color(platform::native_color(palette.secondary));
                if (!peer->edit->composing() && peer->edit->text() != input.text())
                    SetWindowTextW(peer->window, input.text().c_str());
                SetWindowTextW(peer->caption, input.name().c_str());
                InvalidateRect(peer->caption, nullptr, FALSE);
            } else if (peer->list) {
                if (GetFocus() == peer->window) last_focus = peer->window;
                peer->list->update(dpi, palette);
            } else {
                publish_control(peer->accessibility, peer->provider, control, peer->window);
            }
            InvalidateRect(peer->window, nullptr, FALSE);
        }
        if (disabled_focus) {
            if (!platform::traverse_focus(focus_targets, false, disabled_focus)) SetFocus(window);
        }
        InvalidateRect(window, nullptr, FALSE);
    }
    bool translate(MSG& msg) {
        if (msg.message != WM_KEYDOWN || (msg.hwnd != window && !IsChild(window, msg.hwnd))) return false;
        for (const auto& peer : peers)
            if (peer->window == msg.hwnd && peer->edit && peer->edit->composing()) return false;
        Control* target{};
        for (const auto& peer : peers) if (peer->window == msg.hwnd) target = peer->control.get();
        if (key) {
            auto callback = key;
            if (callback({static_cast<Key>(msg.wParam), (GetKeyState(VK_CONTROL) & 0x8000) != 0,
                (GetKeyState(VK_SHIFT) & 0x8000) != 0, target})) return true;
        }
        if (msg.wParam == VK_TAB) {
            platform::traverse_focus(focus_targets, (GetKeyState(VK_SHIFT) & 0x8000) != 0);
            return true;
        }
        if (target && target->role() == ControlRole::text_input && msg.wParam == VK_RETURN) {
            static_cast<TextInput*>(target)->submit();
            return true;
        }
        return false;
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
        for (size_t i = 0; i < stack.child_count(); ++i)
            if (auto child = std::dynamic_pointer_cast<Stack>(stack.child_at(i))) paint_surfaces(*child);
    }
    void paint() {
        PAINTSTRUCT paint{};
        BeginPaint(window, &paint);
        bool redraw{};
        try {
            if (drawing.begin(window, static_cast<float>(dpi), palette.background)) {
                paint_surfaces(*root);
                for (const auto& peer : peers) if (peer->edit) {
                    auto bounds = peer->control->bounds();
                    const bool search = static_cast<TextInput&>(*peer->control).search_style();
                    if (!search) {
                        bounds.y += 24;
                        bounds.height = std::max(0.0f, bounds.height - 24);
                    }
                    drawing.rounded(bounds, palette.field);
                    drawing.rounded(bounds, GetFocus() == peer->window ? palette.accent : palette.border, 6, true);
                    if (search) drawing.search_icon({bounds.x + 12, bounds.y + 13, 18, 18}, palette.secondary);
                    const auto& hint = static_cast<TextInput&>(*peer->control).shortcut_hint();
                    if (!hint.empty()) {
                        drawing.rounded({bounds.x + bounds.width - 62, bounds.y + 12, 50, 20}, palette.border, 4, true);
                        drawing.text(hint, {bounds.x + bounds.width - 56, bounds.y + 12, 42, 20}, palette.secondary, true);
                    }
                }
                redraw = !drawing.end();
                ++paints;
            }
        } catch (...) { EndPaint(window, &paint); throw; }
        EndPaint(window, &paint);
        if (redraw) invalidate(Invalidation::paint);
    }
    void paint_control(Peer& peer) {
        PAINTSTRUCT paint{};
        BeginPaint(peer.window, &paint);
        bool redraw{};
        try {
            auto& canvas = peer.drawing;
            if (canvas.begin(peer.window, static_cast<float>(dpi), peer.surface ? palette.surface : palette.background)) {
                const auto& control = *peer.control;
                const auto bounds = control.bounds();
                const Rect box{2, 2, std::max(0.0f, bounds.width - 4), std::max(0.0f, bounds.height - 4)};
                const auto text = control.enabled() ? palette.text : palette.secondary;
                const auto role = control.role();
                if (role == ControlRole::label) {
                    const auto& label = static_cast<const Label&>(control);
                    const auto color = label.tone() == TextTone::accent ? palette.accent :
                        label.tone() == TextTone::secondary ? palette.secondary :
                        label.tone() == TextTone::error ? palette.error : text;
                    const Rect area{0, 0, bounds.width, bounds.height};
                    if (label.heading()) canvas.heading(control.name(), area, color);
                    else canvas.text(control.name(), area, color, label.caption());
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
                        canvas.rounded(mark, checked ? palette.accent : palette.field, 3);
                        canvas.rounded(mark, control.enabled() ? palette.accent : palette.secondary, 3, true);
                        if (checked) {
                            canvas.line(mark.x + 4, mark.y + 9, mark.x + 8, mark.y + 13, palette.background, 2);
                            canvas.line(mark.x + 8, mark.y + 13, mark.x + 14, mark.y + 5, palette.background, 2);
                        }
                        inset = 42;
                    }
                    canvas.text(control.name(), {inset, 0, std::max(0.0f, bounds.width - inset - 12), bounds.height}, text);
                    if (control.focused()) canvas.rounded(box, palette.accent, 6, true);
                }
                redraw = !canvas.end();
                ++paints;
            }
        } catch (...) { EndPaint(peer.window, &paint); throw; }
        EndPaint(peer.window, &paint);
        if (redraw) invalidate(Invalidation::paint);
    }
    void activated(Peer& peer, bool invoked) {
        update();
        if (invoked && window && peer.control->role() == ControlRole::button)
            raise_control_invoked(peer.provider);
    }
    bool focus(Peer& peer, bool activate_window) {
        if (!ready || closing || !peer.control->focusable()) return false;
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
        case WM_CONTEXTMENU: show_control_menu(control, hwnd, lparam); return 0;
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: paint_control(peer); return 0;
        case WM_GETOBJECT:
            if (static_cast<LONG>(lparam) == UiaRootObjectId && peer.provider)
                return UiaReturnRawElementProvider(hwnd, wparam, lparam, peer.provider);
            break;
        case WM_SETFOCUS:
            last_focus = hwnd;
            control.set_focused(true);
            return 0;
        case WM_KILLFOCUS:
            control.set_focused(false);
            if (GetCapture() == hwnd) ReleaseCapture();
            return 0;
        case WM_MOUSEMOVE:
            control.pointer_move(inside());
            if (!peer.tracking) {
                TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd, 0};
                peer.tracking = TrackMouseEvent(&track) != 0;
            }
            return 0;
        case WM_MOUSELEAVE: peer.tracking = false; control.pointer_move(false); return 0;
        case WM_LBUTTONDOWN:
            control.pointer_move(inside());
            if (control.pointer_down()) { SetFocus(hwnd); SetCapture(hwnd); }
            return 0;
        case WM_LBUTTONUP:
            activated(peer, control.pointer_up(inside()));
            if (GetCapture() == hwnd) ReleaseCapture();
            return 0;
        case WM_CANCELMODE:
            control.cancel();
            if (GetCapture() == hwnd) ReleaseCapture();
            return 0;
        case WM_CAPTURECHANGED: control.cancel(); return 0;
        case WM_KEYDOWN:
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
            if (!control.enabled()) return UIA_E_ELEMENTNOTENABLED;
            if (lparam == 1) {
                return focus(peer, true) ? S_OK : UIA_E_INVALIDOPERATION;
            }
            if (!control.invoke()) return UIA_E_INVALIDOPERATION;
            activated(peer, true);
            return S_OK;
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
        case WM_CANCELMODE: cancel_input(); return 0;
        case WM_ACTIVATE: if (LOWORD(wparam) == WA_INACTIVE) cancel_input(); break;
        case WM_ENABLE: if (!wparam) cancel_input(); break;
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: if (ready) paint(); else return DefWindowProcW(hwnd, message, wparam, lparam); return 0;
        case WM_SIZE: if (wparam != SIZE_MINIMIZED) invalidate(Invalidation::layout); return 0;
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
            for (const auto& peer : peers) if (peer->list) peer->list->publish();
            return 0;
        case update_message: update(); return 0;
        case WM_DPICHANGED: {
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
            for (const auto& peer : peers) {
                peer->drawing.discard();
                if (peer->list) peer->list->discard();
            }
            invalidate(Invalidation::paint);
            return 0;
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLOREDIT: {
            const auto dc = reinterpret_cast<HDC>(wparam);
            const bool editable = message == WM_CTLCOLOREDIT ||
                (reinterpret_cast<HWND>(lparam) && GetDlgCtrlID(reinterpret_cast<HWND>(lparam)) >= 100);
            SetTextColor(dc, platform::native_color(IsWindowEnabled(reinterpret_cast<HWND>(lparam)) ?
                palette.text : palette.secondary));
            SetBkColor(dc, platform::native_color(editable ? palette.field : palette.background));
            return reinterpret_cast<LRESULT>(editable ? field : background);
        }
        case WM_COMMAND:
            for (const auto& peer : peers) if (peer->edit && peer->window == reinterpret_cast<HWND>(lparam)) {
                if (HIWORD(wparam) == EN_CHANGE && !peer->edit->composing())
                    static_cast<TextInput&>(*peer->control).commit_text(peer->edit->text());
                if (HIWORD(wparam) == EN_SETFOCUS || HIWORD(wparam) == EN_KILLFOCUS)
                    peer->control->set_focused(HIWORD(wparam) == EN_SETFOCUS);
                if (HIWORD(wparam) == EN_SETFOCUS) last_focus = peer->window;
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
                    IsWindowEnabled(target) && IsWindowVisible(target)) SetFocus(target);
            } else platform::traverse_focus(focus_targets, wparam != 0);
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
            if (wparam == 0) {
                auto count = paints;
                for (const auto& peer : peers) if (peer->list) count += peer->list->paints();
                return static_cast<LRESULT>(count);
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
        case WM_CLOSE: DestroyWindow(hwnd); return 0;
        case WM_DESTROY:
            ready = false;
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
            if (peer->edit && select_all) peer->edit->focus(true);
            return impl_->focus(*peer, false);
        }
    return false;
}
void Window::close() {
    impl_->closing = true;
    for (auto& task : impl_->tasks) task->cancel();
    if (impl_->window) PostMessageW(impl_->window, WM_CLOSE, 0, 0);
}
void Window::on_key(std::function<bool(const KeyEvent&)> callback) { impl_->key = std::move(callback); }
std::shared_ptr<ViewTask> Window::create_view_task(ViewWorker::Loader loader, std::function<void(ViewResult)> receive) {
    if (impl_->closing || (impl_->used && !impl_->ready)) throw std::logic_error("The Window is closed");
    auto state = std::make_shared<ViewTask::Impl>();
    state->receive = std::move(receive);
    state->worker = std::make_shared<ViewWorker>(std::move(loader), [wake = impl_->wake] { SetEvent(wake->event); });
    state->worker->start();
    impl_->tasks.push_back(state);
    return std::shared_ptr<ViewTask>(new ViewTask(std::move(state)));
}
void Window::copy_text(const std::wstring& text) {
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
