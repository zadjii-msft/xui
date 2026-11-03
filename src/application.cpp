#include "xui/application.hpp"
#include "text_input_host.hpp"
#include "document_host.hpp"
#include "password_host.hpp"
#include "date_time_host.hpp"
#include "native_file_dialog.hpp"
#include "content_inspection.hpp"
#include "native_runtime_host.hpp"
#include "native_swap_chain_host.hpp"
#include "control_accessibility.hpp"
#include "window_host.hpp"
#include "composition_presenter.hpp"
#include "platform.hpp"
#include "wheel.hpp"
#include "collection_host.hpp"
#include "grid_host.hpp"
#include "file_list_host.hpp"
#include "basic_host.hpp"
#include "scene_host.hpp"
#include "navigation_host.hpp"
#include "composite_host.hpp"
#include "layout_styling.hpp"
#include "context_menu.hpp"
#include "shell_commands_internal.hpp"
#include "file_transfer.hpp"
#include "async.hpp"
#include "images.hpp"
#include "window_icon.hpp"
#include "workspace_accessibility.hpp"
#include "xui/data_grid.hpp"
#include "xui/adaptive_layout.hpp"
#include "xui/commands.hpp"
#include "xui/shell_commands.hpp"
#include "xui/titlebar.hpp"
#include "xui/navigation.hpp"
#include "xui/map_view.hpp"
#include "xui/reveal.hpp"
#include <dwmapi.h>
#include "grid_accessibility.hpp"
#include <UIAutomation.h>
#include <commctrl.h>
#include <shellapi.h>
#include <windowsx.h>
#include <cmath>
#include <chrono>
#include <utility>
#include <unordered_map>

namespace xui {
namespace {
constexpr UINT update_message = WM_APP + 12;
constexpr UINT metrics_message = WM_APP + 60;
constexpr UINT_PTR tooltip_timer = 41, animation_timer = 43, progress_timer = 44;
constexpr wchar_t window_class[] = L"Xui.Window.1";
constexpr wchar_t control_class[] = L"Xui.Control.1";
constexpr wchar_t scroll_content_class[] = L"Xui.ScrollContent.1";
thread_local bool running{};
thread_local bool application_context{};
D2D1_COLOR_F argb_color(uint32_t value) {
    return D2D1::ColorF(value & 0xffffff, static_cast<float>(value >> 24) / 255);
}
std::mutex host_claim_mutex;
std::unordered_map<std::uint64_t, const void*> host_claims;
struct WindowIcons {
    HICON small_icon{}, large_icon{};
    ~WindowIcons() { if (small_icon) DestroyIcon(small_icon); if (large_icon) DestroyIcon(large_icon); }
};
std::unique_ptr<WindowIcons> file_type_icons(const std::wstring& extension, bool directory, UINT dpi) {
    auto result = std::make_unique<WindowIcons>();
    for (bool is_small : {true, false}) {
        WindowIcons source;
        if (directory || extension.empty()) {
            SHSTOCKICONINFO info{sizeof(info)};
            hr_require(SHGetStockIconInfo(directory ? SIID_FOLDER : SIID_DOCNOASSOC,
                SHGSI_ICON | (is_small ? SHGSI_SMALLICON : SHGSI_LARGEICON), &info), "Load stock window icon");
            source.small_icon = info.hIcon;
        } else {
            SHFILEINFOW info{};
            const auto name = L"xui" + extension;
            win32_require(SHGetFileInfoW(name.c_str(), FILE_ATTRIBUTE_NORMAL, &info, sizeof(info),
                SHGFI_USEFILEATTRIBUTES | SHGFI_ICON | (is_small ? SHGFI_SMALLICON : SHGFI_LARGEICON)) != 0,
                "Load file-type window icon");
            source.small_icon = info.hIcon;
        }
        const int width = GetSystemMetricsForDpi(is_small ? SM_CXSMICON : SM_CXICON, dpi);
        const int height = GetSystemMetricsForDpi(is_small ? SM_CYSMICON : SM_CYICON, dpi);
        const auto copy = static_cast<HICON>(CopyImage(source.small_icon, IMAGE_ICON, width, height, 0));
        win32_require(copy != nullptr, "Size window icon");
        (is_small ? result->small_icon : result->large_icon) = copy;
    }
    return result;
}
}

struct Window::Impl : std::enable_shared_from_this<Window::Impl>, ControlHostServices, NativeFieldServices {
    using Peer = HostPeer;
    static Impl& owner(Peer& peer) { return static_cast<Impl&>(peer.host); }
    static const Impl& owner(const Peer& peer) { return static_cast<const Impl&>(peer.host); }
    static std::shared_ptr<Button> clear_adornment(const Peer& peer) {
        const auto* value = text_input_peer(peer);
        return value ? value->clear_button() : nullptr;
    }
    static std::shared_ptr<Control> clear_owner(const Peer& peer) {
        return peer.family == HostFamily::text_clear ? static_cast<const TextClearHostPeer&>(peer).owner() : nullptr;
    }
    static bool is_clear_adornment(const Peer& peer) {
        return peer.family == HostFamily::text_clear;
    }
    static TextInputHostPeer* text_input_peer(const Peer& peer) {
        const auto* field = peer.native_field();
        return field ? field->text_input() : nullptr;
    }
    static NativeFieldHostPeer* native_document_peer(const Peer& peer) {
        auto* field = peer.native_field();
        return field && !field->text_input() ? field : nullptr;
    }

    HWND create_input_surface(Peer& peer, HWND native_parent, int id) override {
        const DWORD tab = peer.control->focusable() && peer.control->tab_stop() &&
            !is_caption_button(*peer.control) ? WS_TABSTOP : 0;
        const auto result = CreateWindowExW(WS_EX_TRANSPARENT, control_class, peer.control->name().c_str(),
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | tab, 0, 0, 1, 1, native_parent,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), &peer);
        win32_require(result != nullptr, "Create control");
        return result;
    }
    LRESULT dispatch_input(Peer& peer, HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) noexcept override {
        InputScope scope(*this);
        try { return control_message(peer, hwnd, message, wparam, lparam); }
        catch (...) { fail(); return 0; }
    }
    bool input_enabled(const Peer& peer) const override { return enabled(peer); }
    bool input_visible(const Peer& peer) const override { return visible(peer); }
    bool owner_open() const override { return ready && !closing && window; }
    bool focus_peer(Peer& peer, bool accessibility) override { return focus(peer, accessibility); }
    void reveal_peer(Peer& peer) override {
        if (GetFocus() == peer.window) last_focus = peer.window;
        reveal(peer);
    }
    void update_peer_state() override { update(); }
    void invalidate_peer_paint() override { invalidate(Invalidation::paint); }
    bool wheel_animation_allowed() const override {
        return smooth_scrolling && client_animation && !palette.high_contrast;
    }
    void offer_row_tooltip(Peer& peer) override {
        if (tooltip_target && tooltip_target != peer.control->id()) hide_tooltip();
        offer_tooltip(peer);
    }
    bool file_drag_eligible(const Control& control, HWND hwnd) const override {
        const auto it = peer_lookup.find(&control);
        return it != peer_lookup.end() && it->second->window == hwnd && owner_open() &&
            enabled(*it->second) && visible(*it->second) && in_top_popup(*it->second);
    }
    void deliver_file_drop(std::function<void()> callback) noexcept override {
        InputScope scope(*this);
        try { callback(); } catch (...) { fail(); }
    }
    std::shared_ptr<PeerHostServices> acquire_owner_lease() override {
        return std::shared_ptr<PeerHostServices>(shared_from_this(), this);
    }
    void forget_peer(Peer& peer) noexcept override {
        const auto it = peer_lookup.find(peer.control.get());
        if (it != peer_lookup.end() && it->second == &peer) peer_lookup.erase(it);
    }
    void peer_failure() noexcept override { fail(); }
    Drawing& control_drawing() override { return drawing(); }
    HWND owner_window() const override { return window; }
    ControlHostPresentation control_presentation(const Peer& peer) const override {
        return {dpi, options.composition, keyboard_focus_visible, caption_active,
            progress_animating && animated_progress(peer), progress_phase, titlebar.get()};
    }
    bool native_activation_allowed(bool runtime) const override {
        return !closing && (!runtime || popups.empty()) &&
            std::none_of(adaptive_layouts.begin(), adaptive_layouts.end(),
                [](const auto* layout) { return layout->overlay_active(); });
    }
    std::weak_ptr<platform::CompositionPresenter> composition_presenter() const override { return presenter; }
    Peer* presented_peer(const Control* control) const override { return find_peer(control); }
    void activate_control(Peer& peer, bool invoked) override { activated(peer, invoked); }
    bool caption_control(const Control& control) const override { return is_caption_button(control); }
    void set_keyboard_focus_visible(bool value) override { keyboard_focus_visible = value; }
    bool tab_gesture_allowed(const Peer& peer) const override {
        const auto* tabs = dynamic_cast<const TabStrip*>(peer.control.get());
        return !closing && tab_drag_handler && tabs && titlebar_strip(tabs).has_value();
    }
    void begin_tab_gesture(NavigationHostPeer& peer, POINT point) override { begin_tab_move(peer, point); }
    HWND create_scroll_surface(Peer& peer) override {
        const auto result = CreateWindowExW(WS_EX_TRANSPARENT, scroll_content_class, L"",
            WS_CHILD | WS_VISIBLE, 0, 0, 1, 1, peer.window, nullptr, GetModuleHandleW(nullptr), &peer);
        win32_require(result != nullptr, "Create scroll content window");
        win32_require(SetWindowSubclass(result, navigation_procedure, 1,
            reinterpret_cast<DWORD_PTR>(&peer)) != 0, "Attach scroll content navigation");
        return result;
    }
    void collect_popup_surface(const std::shared_ptr<Popup>& popup) override { collect(popup, true); }
    void show_host_popup(const std::shared_ptr<Popup>& popup, Control& anchor, Control* initial) override {
        show_popup(popup, anchor, initial);
    }
    void dismiss_host_popup(Popup& popup, PopupDismissReason reason) override {
        InputScope input(*this);
        dismiss_popup(popup, reason);
    }
    bool expand_menu_heading(Peer& peer, bool expand) override { return menu_bar_expand(peer, expand); }
    void focus_owned_control(Control& control) override {
        InputScope input(*this);
        update();
        if (auto* peer = find_peer(&control)) focus(*peer, false);
    }
    bool scroll_host_key(Peer& peer, WPARAM key) override { return scroll_key(peer, key); }

    WindowOptions options;
    std::wstring error;
    std::shared_ptr<Stack> root;
    std::shared_ptr<TitleBar> titlebar;
    bool caption_active{};
    std::unordered_map<const Control*, Peer*> peer_lookup;
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
#include "menu_bar_host.inc"
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
    unsigned input_depth{}, dispatch_depth{};
    std::uint64_t tooltip_target{};
    std::uint64_t tooltip_revision{};
    std::optional<Rect> tooltip_anchor;
    std::chrono::steady_clock::time_point tooltip_due;
    bool tooltip_shown{};
    bool progress_animating{}, client_animation{true};
    bool smooth_scrolling{}, presentation_animations{true};
    std::shared_ptr<const StyleFontFamily> presentation_font;
    float presentation_font_size{14};
    std::chrono::steady_clock::time_point progress_epoch;
    std::uint64_t progress_frames{};
    float progress_phase{};
    Rect tooltip_bounds{};
    std::unique_ptr<ControlStyleAttachment> tooltip_styling;
    HWND window{}, last_focus{};
    UINT dpi{96};
    bool icon_configured{}, icon_directory{};
    std::wstring icon_extension;
    std::unique_ptr<WindowIcons> window_icons;
    void apply_icons(std::unique_ptr<WindowIcons> value) {
        SendMessageW(window, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(value->small_icon));
        SendMessageW(window, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(value->large_icon));
        window_icons = std::move(value);
    }
    Drawing root_drawing;
    Drawing* paint_drawing{&root_drawing};
    Drawing& drawing() const { return *paint_drawing; }
    bool painting{}, destroy_after_paint{};
    Palette palette{};
    HBRUSH background{}, field{};
    HFONT font{};
    bool used{}, pending{}, layout_pending{}, ready{}, syncing{}, failed{}, quit_posted{}, attached{}, closing{}, destroying{}, replacing{};
    bool placement_pending{}, animation_timer_running{};
    bool scroll_pending{}, state_pending{};
    std::uint64_t animation_tick_count{};
    std::uint64_t split_intermediate_paints{};
    std::uint64_t reveal_intermediate_paints{};
    bool animation_frame_pending{};
    Animation::Clock::time_point animation_frame_due{};
    ContentHost* replacement_host{};
    std::shared_ptr<NativeFileDialog> native_file_dialog;
    bool file_dialog_teardown_pending{};
    struct ContentPicking {
        std::weak_ptr<ContentHost> host;
        inspection::Registration registration;
        std::uint64_t generation{};
        std::optional<std::uint32_t> pending;
        std::optional<std::uint32_t> highlight;
        bool enabled{}, mouse_down{}, pointer_down{};
    };
    std::unordered_map<std::uint64_t, ContentPicking> content_picking;
    std::uint64_t paints{}, layouts{};
    std::shared_ptr<TaskWake> wake = std::make_shared<TaskWake>();
    WindowIcon window_icon;
    std::shared_ptr<AsyncShellMenu> shell_prefetch;
    std::vector<std::shared_ptr<ViewTask::Impl>> tasks;
    std::vector<std::shared_ptr<SampleTask::Impl>> samples;
    std::function<bool(const KeyEvent&)> key;
    std::mutex post_mutex;
    std::vector<std::function<void()>> posts;
    bool posts_closed{};
    bool application_managed{}, closed_notified{};
    std::weak_ptr<void> application;
    Window* public_window{};
    std::optional<WindowPlacement> initial_placement;
    std::function<bool(const TabDragEvent&)> tab_drag_handler;
    std::function<void()> closed_callback;
    std::vector<std::shared_ptr<Element>> claimed;
    void claim(const std::shared_ptr<Element>& element) {
        std::lock_guard lock(host_claim_mutex);
        const auto [it, inserted] = host_claims.emplace(element->id(), this);
        if (it->second != this) throw std::logic_error("Content belongs to another live Window");
        if (inserted) {
            try { claimed.push_back(element); }
            catch (...) { host_claims.erase(it); throw; }
        }
    }
    void release_claims(const std::set<std::uint64_t>* live = nullptr) {
        std::vector<std::shared_ptr<Element>> removed;
        if (live) removed.reserve(claimed.size());
        {
            std::lock_guard lock(host_claim_mutex);
            if (!live) {
                for (const auto& element : claimed) host_claims.erase(element->id());
                removed.swap(claimed);
            } else {
                std::erase_if(claimed, [&](const auto& element) {
                    if (live->contains(element->id())) return false;
                    host_claims.erase(element->id());
                    removed.push_back(element);
                    return true;
                });
            }
        }
    }
    void close_posts() {
        shell_prefetch.reset();
        std::vector<std::function<void()>> removed;
        { std::lock_guard lock(post_mutex); posts_closed = true; removed.swap(posts); }
        content_picking.clear();
    }
    std::function<bool(const NavigationEvent&)> navigation;
    bool has_images{};
    const DWORD owner_thread = GetCurrentThreadId();

#include "application_tab_drag.inc"

    explicit Impl(WindowOptions value) : options(std::move(value)) {
        if (options.visual_style < VisualStyle::classic || options.visual_style > VisualStyle::winui)
            throw std::invalid_argument("Invalid visual style");
        if (options.transparent && options.custom_titlebar)
            throw std::invalid_argument("A transparent window cannot use the XUI title bar");
        if (options.backdrop < WindowBackdrop::none || options.backdrop > WindowBackdrop::acrylic)
            throw std::invalid_argument("Invalid window backdrop");
        if (options.composition && options.backdrop != WindowBackdrop::none)
            throw std::invalid_argument("A composition window cannot use a system backdrop");
        if (options.custom_titlebar) titlebar = std::make_shared<TitleBar>(options.title);
    }
    // A transparent window that requests a backdrop at creation is a borderless,
    // non-layered popup: DWM draws the material, rounded corners, and shadow.
    bool material_popup{};
    // A transparent composition window is frameless and non-layered. Its window region
    // follows the painted shapes so empty pixels pass pointer input through.
    bool composition_card() const { return options.composition && options.transparent; }
    bool frameless() const { return material_popup || composition_card(); }
    bool layered() const { return options.transparent && !material_popup && !options.composition; }
    std::shared_ptr<platform::CompositionPresenter> presenter;
    // Popups and tooltips over active swap chain panels render into a second composition layer.
    Drawing composition_overlay;
    bool composition_overlay_ready{};
    // Panels inside popups compose above the overlay layer, so only window-level panels need it.
    bool active_panel() const {
        return std::any_of(peers.begin(), peers.end(), [&](const auto& peer) {
            return peer->swap_chain_host() && peer->swap_chain_host()->active() && !popup_owner(peer.get());
        });
    }
    struct HitShape {
        Rect bounds; float radius;
        bool operator==(const HitShape& other) const {
            return bounds.x == other.bounds.x && bounds.y == other.bounds.y && bounds.width == other.bounds.width &&
                bounds.height == other.bounds.height && radius == other.radius;
        }
    };
    std::vector<HitShape> hit_shapes, applied_hit_shapes;
    UINT applied_hit_dpi{};
    void add_hit_shape(Rect bounds, float radius) {
        if (composition_card() && bounds.width > 0 && bounds.height > 0) hit_shapes.push_back({bounds, radius});
    }
    void apply_hit_region() {
        if (!composition_card() || (hit_shapes == applied_hit_shapes && applied_hit_dpi == dpi)) return;
        applied_hit_shapes = hit_shapes;
        applied_hit_dpi = dpi;
        if (hit_shapes.empty()) {
            win32_require(SetWindowRgn(window, nullptr, FALSE) != 0, "Clear composition hit region");
            return;
        }
        const float scale = dpi / 96.0f;
        HRGN region = CreateRectRgn(0, 0, 0, 0);
        win32_require(region != nullptr, "Create composition hit region");
        for (const auto& shape : hit_shapes) {
            const int left = static_cast<int>(std::floor(shape.bounds.x * scale));
            const int top = static_cast<int>(std::floor(shape.bounds.y * scale));
            const int right = static_cast<int>(std::ceil((shape.bounds.x + shape.bounds.width) * scale));
            const int bottom = static_cast<int>(std::ceil((shape.bounds.y + shape.bounds.height) * scale));
            const int diameter = static_cast<int>(std::lround(2 * shape.radius * scale));
            HRGN part = diameter ? CreateRoundRectRgn(left, top, right + 1, bottom + 1, diameter, diameter) :
                CreateRectRgn(left, top, right, bottom);
            if (!part) { DeleteObject(region); win32_require(false, "Create composition hit shape"); }
            CombineRgn(region, region, part, RGN_OR);
            DeleteObject(part);
        }
        if (!SetWindowRgn(window, region, FALSE)) {
            DeleteObject(region);
            win32_require(false, "Apply composition hit region");
        }
    }
    void present_composition() {
        auto& target = root_drawing;
        const auto size = target.composition_size();
        std::vector<platform::BackdropRegion> regions;
        regions.reserve(target.backdrop_holes().size());
        for (const auto& hole : target.backdrop_holes()) regions.push_back({hole.bounds, hole.visible, hole.radius});
        presenter->update(target.composition_swap_chain(), target.composition_generation(),
            SIZE{static_cast<LONG>(size.width), static_cast<LONG>(size.height)}, regions,
            palette.mode == ThemeMode::light ? 0x99f3f3f3u : 0x8c202020u);
        apply_hit_region();
    }
    // The DWM type currently applied, and whether client pixels expose it.
    DWORD backdrop_type{};
    bool backdrop_presented{}, transparency_effects{true};
    DWORD desired_backdrop_type() const {
        if (palette.high_contrast || !transparency_effects || layered()) return 0;
        switch (options.backdrop) {
        case WindowBackdrop::mica: return DWMSBT_MAINWINDOW;
        case WindowBackdrop::mica_alt: return DWMSBT_TABBEDWINDOW;
        case WindowBackdrop::acrylic: return DWMSBT_TRANSIENTWINDOW;
        default: return 0;
        }
    }
    // The window appearance attributes are applied first, so DWM selects the material last.
    void sync_backdrop() {
        if (!window) return;
        const DWORD type = desired_backdrop_type();
        bool presented = backdrop_presented;
        if (type != backdrop_type) {
            presented = (type || backdrop_type) && platform::system_backdrop(window, type, titlebar != nullptr || frameless());
            backdrop_type = presented ? type : 0;
        }
        if (presented == backdrop_presented) return;
        backdrop_presented = presented;
        root_drawing.discard();
        for (const auto& peer : peers)
            if (peer->native()) platform::opaque_native_paint(peer->window, presented);
        InvalidateRect(window, nullptr, FALSE);
    }
    std::function<void(HWND)> native_created;
    std::optional<Rect> drag_region;
    bool drag_hit(LPARAM position) const {
        if (!options.transparent || !drag_region) return false;
        POINT point{GET_X_LPARAM(position), GET_Y_LPARAM(position)};
        win32_require(ScreenToClient(window, &point) != 0, "Locate transparent caption hit");
        const float x = point.x * 96.0f / dpi, y = point.y * 96.0f / dpi;
        const auto& area = *drag_region;
        return x >= area.x && x < area.x + area.width && y >= area.y && y < area.y + area.height;
    }
    ~Impl() { teardown(); }
    void destroy() {
        if (painting) {
            closing = true;
            close_posts();
            destroy_after_paint = true;
            return;
        }
        stop_progress_animation();
        if (native_file_dialog) {
            closing = true;
            native_file_dialog->cancel();
            close_posts();
            return;
        }
        if (destroying) return;
        destroying = true;
        closing = true;
        sync_animations(true);
        window_icon.set_source({}, window, dpi, wake);
        ImageResources::clear_unused();
        close_posts();
        for (auto& task : samples) task->cancel();
        for (auto& task : tasks) task->cancel();
        for (const auto& peer : peers) {
            if (peer->runtime_host()) peer->runtime_host()->cancel_owner();
            if (peer->swap_chain_host()) peer->swap_chain_host()->cancel_owner();
            if (auto* map = dynamic_cast<MapView*>(peer->control.get())) map->cancel_request();
        }
        try { hide_tooltip(); }
        catch (...) { failed = true; error = L"A navigation hover cancellation callback failed."; }
        if (!popups.empty()) {
            try { auto popup = popups.front().popup; dismiss_popup(*popup, PopupDismissReason::owner_closed, false); }
            catch (...) { failed = true; error = L"A popup dismissal callback failed."; }
        }
        root_drawing.discard();
        composition_overlay.discard();
        for (const auto& peer : peers) if (peer->overlay_surface()) peer->overlay_surface()->discard();
        if (window) DestroyWindow(window);
        destroying = false;
    }
    void detach_peer(Peer& peer) {
        if (auto* animation = dynamic_cast<Animation*>(peer.control.get())) animation->settle();
        peer.control->set_text_measurer({});
        if (auto label = std::dynamic_pointer_cast<Label>(peer.control)) label->set_wrapped_text_measurer({});
        if (auto* columns = dynamic_cast<MillerColumns*>(peer.control.get())) columns->on_focus_column({});
        if (auto combo = std::dynamic_pointer_cast<ComboBox>(peer.control)) combo->choices()->on_accept({});
        peer.detach_owner();
        if (auto* map = dynamic_cast<MapView*>(peer.control.get())) map->cancel_request();
    }
    void detach() {
        stop_progress_animation();
        if (!attached) return;
        root->set_invalidator({});
        for (const auto& peer : peers) detach_peer(*peer);
        if (has_images) ImageResources::clear_unused();
        attached = false;
    }
    // Posted work permits content replacement, but still delays window retirement.
    struct DispatchScope {
        Impl& host;
        explicit DispatchScope(Impl& value) : host(value) { ++host.dispatch_depth; }
        ~DispatchScope() {
            if (--host.dispatch_depth == 0 && !host.input_depth && host.closing && host.application_managed)
                SetEvent(host.wake->event);
        }
    };
    struct InputScope {
        Impl& host;
        explicit InputScope(Impl& value) : host(value) { ++host.input_depth; }
        ~InputScope() {
            if (--host.input_depth == 0 && host.closing && host.application_managed)
                SetEvent(host.wake->event);
        }
    };
#include "application_content_inspection.inc"
    void teardown() {
        if (native_file_dialog) {
            file_dialog_teardown_pending = true;
            destroy();
            return;
        }
        for (auto& task : samples) task->cancel();
        for (auto& task : tasks) task->cancel();
        detach();
        ready = false;
        destroy();
        if (!input_depth && !dispatch_depth) {
            peer_lookup.clear();
            peers.clear();
        }
        root_drawing.release();
        if (background) { DeleteObject(background); background = nullptr; }
        if (field) { DeleteObject(field); field = nullptr; }
        if (font) { DeleteObject(font); font = nullptr; }
        window_icons.reset();
        if (!input_depth && !dispatch_depth) release_claims();
    }
    void complete() {
        if (closed_notified || input_depth || dispatch_depth || native_file_dialog) return;
        teardown();
        closed_notified = true;
        if (closed_callback) {
            try { auto callback = closed_callback; callback(); }
            catch (...) { fail(); }
        }
        closed_callback = {};
    }
    void deliver() {
        DispatchScope scope(*this);
        try {
            std::vector<std::function<void()>> queued;
            {
                std::lock_guard lock(post_mutex);
                const auto count = std::min<std::size_t>(posts.size(), 64);
                queued.insert(queued.end(), std::make_move_iterator(posts.begin()), std::make_move_iterator(posts.begin() + count));
                posts.erase(posts.begin(), posts.begin() + count);
                if (!posts.empty()) wake->signal();
            }
            for (auto& callback : queued) {
                if (!ready || closing) break;
                callback();
            }
            if (ready && !closing) deliver_content_picks();
            const auto views = tasks;
            for (const auto& task : views) {
                if (!ready || closing) break;
                task->deliver();
            }
            if (ready && !closing) deliver_images();
            const auto periodic = samples;
            for (const auto& task : periodic) {
                if (!ready || closing) break;
                task->deliver();
            }
        } catch (...) { fail(); }
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
        placement_pending = placement_pending || kind == Invalidation::placement;
        scroll_pending = scroll_pending || kind == Invalidation::scroll;
        state_pending = state_pending || kind != Invalidation::scroll;
        if (!window || !ready) return;
        if (progress_animating && !syncing) sync_progress_animation();
        if (!pending) {
            pending = true;
            win32_require(PostMessageW(window, update_message, 0, 0) != 0, "Schedule control update");
        }
    }
    void sync_animations(bool settle = false, bool tick = false) {
        bool active{};
        const bool showing = window && IsWindowVisible(window) && !IsIconic(window);
        std::optional<bool> allowed;
        const auto now = Animation::Clock::now();
        for (const auto& peer : peers) {
            if (peer->family == HostFamily::collection || peer->family == HostFamily::grid) {
                const bool stop = settle || !showing || !visible(*peer) || !enabled(*peer) ||
                    !smooth_scrolling || !client_animation || palette.high_contrast;
                active = peer->advance_wheel(now, stop, tick) || active;
            }
            auto* animation = dynamic_cast<Animation*>(peer->control.get());
            if (!animation || !animation->animating()) continue;
            bool ancestors_visible = true;
            for (auto* parent = peer->parent; parent; parent = parent->parent) {
                const auto* expander = dynamic_cast<Expander*>(parent->control.get());
                if (!visible(*parent, !layout_pending) || !enabled(*parent, true, !layout_pending) ||
                    (expander && !expander->expanded())) {
                    ancestors_visible = false; break;
                }
            }
            if (settle || !showing || !peer->control->visible() || !peer->control->enabled() || !ancestors_visible ||
                (!layout_pending && !opening_reveal(*peer) &&
                    (peer->control->bounds().width <= 0 || peer->control->bounds().height <= 0))) {
                animation->settle();
                continue;
            }
            if (!allowed) {
                BOOL value{};
                win32_require(SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &value, 0) != FALSE,
                    "Read system animation preference");
                allowed = value != FALSE && presentation_animations && !palette.high_contrast;
            }
            if (!*allowed) animation->settle();
            else if (tick) animation->advance(now);
            active = active || animation->animating();
        }
        if (active && !animation_timer_running) {
            win32_require(SetTimer(window, animation_timer, 16, nullptr) != 0, "Start window animation");
            animation_timer_running = true;
            animation_frame_due = now + std::chrono::milliseconds(16);
        } else if (!active && animation_timer_running) {
            win32_require(KillTimer(window, animation_timer) != 0, "Stop window animation");
            animation_timer_running = false;
        }
    }
    void present_animation_frame() {
        if (!ready || closing || !window || (!animation_timer_running && !animation_frame_pending)) return;
        const auto now = Animation::Clock::now();
        if (!animation_frame_pending && animation_timer_running && now >= animation_frame_due) {
            animation_frame_due = now + std::chrono::milliseconds(16);
            // Posted traffic outranks generated WM_TIMER and WM_PAINT messages.
            MSG timer{};
            const auto owner = window;
            if (PeekMessageW(&timer, owner, WM_TIMER, WM_TIMER, PM_REMOVE)) {
                if (timer.message == WM_QUIT) {
                    PostQuitMessage(static_cast<int>(timer.wParam));
                    return;
                }
                // The window filter also retrieves messages for native children.
                if (ready && !closing && window == owner)
                    DispatchMessageW(&timer);
            }
        }
        if (!animation_frame_pending || !ready || closing || !window) return;
        animation_frame_pending = false;
        MSG update{};
        const auto owner = window;
        if (PeekMessageW(&update, owner, update_message, update_message, PM_REMOVE)) {
            if (update.message == WM_QUIT) {
                PostQuitMessage(static_cast<int>(update.wParam));
                return;
            }
            if (ready && !closing && window == owner)
                DispatchMessageW(&update);
        }
        if (ready && !closing && window)
            win32_require(UpdateWindow(window) != FALSE, "Present scheduled animation frame");
    }
    static LRESULT CALLBACK procedure(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) noexcept {
        auto* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
            self->window = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (!self) return DefWindowProcW(hwnd, message, wparam, lparam);
        try {
            if (message == update_message) {
                DispatchScope scope(*self);
                return self->message(hwnd, message, wparam, lparam);
            }
            InputScope scope(*self);
            return self->message(hwnd, message, wparam, lparam);
        }
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
        return peer->host.dispatch_input(*peer, hwnd, message, wparam, lparam);
    }
    static LRESULT CALLBACK scroll_content_procedure(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) noexcept {
        auto* peer = reinterpret_cast<Peer*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            peer = static_cast<Peer*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(peer));
        }
        if (!peer) return DefWindowProcW(hwnd, message, wparam, lparam);
        InputScope scope(owner(*peer));
        try {
            if (message == WM_ERASEBKGND) return 1;
            if (message == WM_PAINT) { ValidateRect(hwnd, nullptr); return 0; }
            if (message == WM_LBUTTONDOWN && owner(*peer).focus_edit_at(hwnd, lparam)) return 0;
            if (message == WM_MOUSEWHEEL) {
                owner(*peer).scroll_wheel(*peer, wparam);
                return 0;
            }
            if (message == WM_COMMAND || message == WM_NOTIFY || message == WM_CTLCOLOREDIT ||
                message == WM_CTLCOLORSTATIC || message == WM_NEXTDLGCTL)
                return SendMessageW(peer->window, message, wparam, lparam);
        } catch (...) { owner(*peer).fail(); return 0; }
        return DefWindowProcW(hwnd, message, wparam, lparam);
    }
    LRESULT dispatch_native_field(NativeFieldHostPeer& peer, HWND hwnd, UINT message,
        WPARAM wparam, LPARAM lparam) noexcept override {
        InputScope scope(*this);
        try {
            if (message == WM_SETFOCUS && !enabled(peer)) { traverse(false); return 0; }
            if (!enabled(peer) && (message == WM_KEYDOWN || message == WM_CHAR ||
                message == WM_LBUTTONDOWN || message == WM_LBUTTONUP || message == WM_RBUTTONDOWN ||
                message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL || message == WM_CONTEXTMENU)) return 0;
            if (message == WM_NCHITTEST && drag_hit(lparam)) return HTCAPTION;
            if (message == WM_NCLBUTTONDOWN && wparam == HTCAPTION && options.transparent)
                return SendMessageW(window, message, wparam, lparam);
            return peer.native_message(hwnd, message, wparam, lparam, 2, composing_native);
        } catch (...) {
            fail();
            return message == control_action_message ? E_FAIL : 0;
        }
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
            if (peer->window == source || peer->caption_window() == source) { target = peer->control.get(); break; }
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
        InputScope scope(owner(peer));
        if (message == WM_WINDOWPOSCHANGING && owner(peer).syncing) {
            // The root presents all peers together after layout. USER must not copy
            // old child pixels or repaint intermediate positions between those frames.
            reinterpret_cast<WINDOWPOS*>(lparam)->flags |= SWP_NOREDRAW | SWP_NOCOPYBITS;
        }
        try {
            if (auto result = owner(peer).inspect_pointer(hwnd, message, wparam, lparam)) return *result;
        } catch (...) { owner(peer).fail(); return 0; }
        if (message == WM_SETFOCUS && !owner(peer).enabled(peer)) {
            try { owner(peer).traverse(false); } catch (...) { owner(peer).fail(); }
            return 0;
        }
        if (!owner(peer).enabled(peer) && (message == WM_KEYDOWN || message == WM_CHAR ||
            message == WM_LBUTTONDOWN || message == WM_LBUTTONUP || message == WM_RBUTTONDOWN ||
            message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL || message == WM_CONTEXTMENU)) return 0;
        if (message == WM_SETFOCUS) {
            try { owner(peer).dismiss_adaptive_outside(peer); }
            catch (...) { owner(peer).fail(); return 0; }
        }
        if (native_document_peer(peer) && (message == WM_SETFOCUS || message == WM_KILLFOCUS)) {
            try {
                peer.control->set_focused(message == WM_SETFOCUS);
                if (message == WM_SETFOCUS) { owner(peer).last_focus = hwnd; owner(peer).reveal(peer); }
            } catch (...) { owner(peer).fail(); return 0; }
        }
        if (owner(peer).titlebar) {
            if (message == WM_NCHITTEST) {
                const auto hit = owner(peer).caption_hit(owner(peer).window, lparam);
                if (hit != HTCLIENT) return hit;
            }
            if (owner(peer).is_caption_button(*peer.control)) {
                try {
                    if (message == WM_NCMOUSEMOVE) {
                        peer.control->pointer_move(wparam == HTMINBUTTON || wparam == HTMAXBUTTON || wparam == HTCLOSE);
                        TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE | TME_NONCLIENT, hwnd, 0};
                        win32_require(TrackMouseEvent(&track) != 0, "Track caption pointer");
                    } else if (message == WM_NCMOUSELEAVE) {
                        peer.control->pointer_move(false);
                    } else if ((message == WM_NCLBUTTONDOWN || message == WM_NCLBUTTONDBLCLK) &&
                        (wparam == HTMINBUTTON || wparam == HTMAXBUTTON || wparam == HTCLOSE)) {
                        if (owner(peer).enabled(peer) && owner(peer).visible(peer)) {
                            owner(peer).hide_tooltip();
                            peer.control->pointer_move(true);
                            if (peer.control->pointer_down()) SetCapture(hwnd);
                        }
                        return 0;
                    }
                } catch (...) { owner(peer).fail(); return 0; }
            }
            if (message == WM_NCLBUTTONDOWN || message == WM_NCLBUTTONUP || message == WM_NCLBUTTONDBLCLK ||
                message == WM_NCRBUTTONUP || message == WM_NCMOUSEMOVE || message == WM_NCMOUSELEAVE) {
                LRESULT result{};
                if (DwmDefWindowProc(owner(peer).window, message, wparam, lparam, &result)) return result;
                return DefWindowProcW(owner(peer).window, message, wparam, lparam);
            }
        }
        if (message == WM_LBUTTONDOWN) {
            peer.suppress_popup_click = (!owner(peer).popups.empty() && owner(peer).popups.back().anchor == peer.control) ||
                owner(peer).menu_bar_popup_anchor(peer);
            try { owner(peer).light_dismiss(&peer); }
            catch (...) { owner(peer).fail(); return 0; }
        }
        if ((message == WM_SETFOCUS || message == WM_MOUSEMOVE) && !dynamic_cast<NavigationList*>(peer.control.get())) {
            try { owner(peer).offer_tooltip(peer); }
            catch (...) { owner(peer).fail(); return 0; }
        }
        if (message == WM_MOUSEMOVE && !peer.tracking) {
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd, 0};
            peer.tracking = TrackMouseEvent(&track) != 0;
        }
        if (message == WM_MOUSEMOVE && owner(peer).options.visual_style == VisualStyle::winui) {
            try { owner(peer).hover_edit(owner(peer).edit_at(hwnd, lparam), hwnd); }
            catch (...) { owner(peer).fail(); return 0; }
        }
        if (message == WM_MOUSELEAVE && owner(peer).hovered_edit_source == hwnd) {
            try { owner(peer).hover_edit(nullptr, nullptr); }
            catch (...) { owner(peer).fail(); return 0; }
        }
        if (message == WM_MOUSELEAVE) peer.tracking = false;
        if (message == WM_KILLFOCUS || message == WM_MOUSELEAVE || message == WM_KEYDOWN ||
            message == WM_SYSKEYDOWN || message == WM_CHAR || message == WM_IME_STARTCOMPOSITION) {
            try { owner(peer).hide_tooltip(); }
            catch (...) { owner(peer).fail(); return 0; }
        }
        if (message == WM_KILLFOCUS) {
            try { owner(peer).focus_departing(reinterpret_cast<HWND>(wparam)); }
            catch (...) { owner(peer).fail(); return 0; }
        }
        if (message == WM_CONTEXTMENU && text_input_peer(peer) && peer.control->has_context_menu()) {
            if (text_input_peer(peer)->composing()) return 0;
            auto* host = &owner(peer);
            try { show_control_menu(*peer.control, hwnd, lparam, host->palette, host->dpi); }
            catch (...) { host->fail(); }
            return 0;
        }
        try {
            if (auto result = owner(peer).navigation_message(hwnd, message, wparam, lparam)) return *result;
        } catch (...) { owner(peer).fail(); return 0; }
        if (message == WM_NCDESTROY) RemoveWindowSubclass(hwnd, navigation_procedure, id);
        if (text_input_peer(peer) && (message == WM_IME_STARTCOMPOSITION || message == WM_IME_ENDCOMPOSITION)) {
            auto* host = &owner(peer);
            const auto result = DefSubclassProc(hwnd, message, wparam, lparam);
            host->invalidate(Invalidation::layout);
            return result;
        }
        if (text_input_peer(peer) && peer.parent && peer.parent->control->role() == ControlRole::numeric_input &&
            peer.control->visual_style() == VisualStyle::winui && !text_input_peer(peer)->composing() &&
            (message == WM_SETFOCUS || (message == WM_LBUTTONDOWN && GetFocus() != hwnd))) {
            const auto input = peer.control;
            const auto result = DefSubclassProc(hwnd, message, wparam, lparam);
            if (GetFocus() == hwnd && input->visual_style() == VisualStyle::winui)
                SendMessageW(hwnd, EM_SETSEL, 0, -1);
            return result;
        }
        return DefSubclassProc(hwnd, message, wparam, lparam);
    }
    bool layout_style_context_enabled(const Peer* parent) const {
        const auto owner = popup_owner(parent);
        for (auto it = popups.rbegin(); it != popups.rend(); ++it) {
            if (it->popup->id() == owner) break;
            if (it->dialog) return false;
        }
        for (const auto& entry : popups)
            if (entry.popup->id() == owner && !command_popup_current(entry)) return false;
        for (auto* ancestor = parent; ancestor; ancestor = ancestor->parent)
            if (!ancestor->control->enabled()) return false;
        return true;
    }
    void prepare_popup_surface(Peer& peer) {
        // Composition windows draw popups in their own composition layers, not child HWND surfaces.
        if (options.composition) return;
        if (peer.control->role() != ControlRole::popup || peer.overlay_surface() ||
            std::none_of(peers.begin(), peers.end(), [](const auto& item) { return bool(item->swap_chain_host()); })) return;
        composite_peer(peer)->prepare_overlay(options.visual_style);
    }
    void apply_typography(Element& element) {
        if (!presentation_font) return;
        const auto target = element.control_style_target();
        if (!target) return;
        const auto* control = dynamic_cast<Control*>(&element);
        const float font_size = control && control->presentation_font_size() != 0 ?
            control->presentation_font_size() : presentation_font_size;
        const auto& font_family = control && control->presentation_font_family() ?
            control->presentation_font_family() : presentation_font;
        for (const auto& part : control_style_schema(*target).parts) {
            constexpr auto family = style_property(StyleProperty::font_family), size = style_property(StyleProperty::font_size);
            if (!(part.allowed & family) && !(part.allowed & size)) continue;
            auto values = element.control_style_values(part.part);
            if (part.allowed & family) values.font_family = font_family;
            if (part.allowed & size) values.font_size = std::min(font_size, part.limits.maximum_font_size);
            element.set_control_style_values(part.part, std::move(values));
        }
    }
    void claim_deferred_tree(const std::shared_ptr<Element>& element) {
        claim(element);
        apply_typography(*element);
        if (auto* control = dynamic_cast<Control*>(element.get())) control->set_visual_style(options.visual_style);
        else if (!dynamic_cast<Stack*>(element.get()))
            throw std::invalid_argument("Window content supports Stack and standard controls only");
        for (const auto& child : element->owned_children()) claim_deferred_tree(child);
    }
    bool defer_reveal_children(const std::shared_ptr<Control>& control) {
        const auto* reveal = dynamic_cast<const Reveal*>(control.get());
        if (!reveal || reveal->open() || reveal->animating()) return false;
        std::set<std::uint64_t> ids;
        content_ids(reveal->content(), ids, true);
        if (replacement_host && ids.contains(replacement_host->id())) return false;
        // Keep ownership and typography, but do not allocate HWNDs for unopened content.
        claim_deferred_tree(reveal->content());
        return true;
    }
    void collect(const std::shared_ptr<Element>& element, bool surface = false, Peer* parent = nullptr,
        AdaptiveLayout* adaptive = nullptr, TextInputHostPeer* clear_owner = nullptr) {
        claim(element);
        apply_typography(*element);
        if (auto stack = std::dynamic_pointer_cast<Stack>(element)) {
            stack->set_control_style_context_enabled(layout_style_context_enabled(parent));
            if (auto pages = std::dynamic_pointer_cast<PageView>(stack)) {
                const auto children = pages->owned_children();
                if (!children.empty()) collect(children[pages->selected()], surface || stack->surface(), parent, adaptive);
                if (replacement_host) for (std::size_t i = 0; i < children.size(); ++i) {
                    if (i == pages->selected()) continue;
                    std::set<std::uint64_t> ids;
                    content_ids(children[i], ids);
                    if (ids.contains(replacement_host->id()))
                        collect(children[i], surface || stack->surface(), parent, adaptive);
                }
                return;
            }
            auto* layout = dynamic_cast<AdaptiveLayout*>(stack.get());
            if (layout && std::find(adaptive_layouts.begin(), adaptive_layouts.end(), layout) == adaptive_layouts.end()) adaptive_layouts.push_back(layout);
            for (const auto& child : stack->owned_children())
                collect(child, surface || stack->surface(), parent, layout && child == layout->navigation() ? layout : adaptive);
            return;
        }
        auto control = std::dynamic_pointer_cast<Control>(element);
        if (!control) throw std::invalid_argument("Window content supports Stack and standard controls only");
        control->set_visual_style(options.visual_style);
        bind_menu_bar(control);
        if (auto* peer = find_peer(control.get())) {
            prepare_popup_surface(*peer);
            peer->surface = surface;
            peer->adaptive = adaptive;
            if (!defer_reveal_children(control))
                for (const auto& child : control->owned_children())
                    collect(child, surface || (options.visual_style == VisualStyle::winui && control->role() == ControlRole::expander), peer, adaptive);
            collect_clear_button(*peer);
            return;
        }
        std::unique_ptr<Peer> peer;
        const auto role = control->role();
        if (clear_owner) {
            if (control != clear_owner->clear_button()) throw std::logic_error("Mismatched native clear adornment owner");
            peer = std::make_unique<TextClearHostPeer>(*this, clear_owner->clear_button(),
                std::static_pointer_cast<TextInput>(clear_owner->control));
        } else if (role == ControlRole::text_input) {
            auto input = std::dynamic_pointer_cast<TextInput>(control);
            if (!input) throw std::invalid_argument("The text-input role requires TextInput");
            peer = std::make_unique<TextInputHostPeer>(*this, *this, std::move(input));
        } else if (role == ControlRole::document_text) {
            auto document = std::dynamic_pointer_cast<DocumentText>(control);
            if (!document) throw std::invalid_argument("The document-text role requires DocumentText");
            peer = std::make_unique<DocumentTextHostPeer>(*this, *this, std::move(document));
        } else if (role == ControlRole::password_input) {
            auto password = std::dynamic_pointer_cast<PasswordInput>(control);
            if (!password) throw std::invalid_argument("The password-input role requires PasswordInput");
            peer = std::make_unique<PasswordHostPeer>(*this, *this, std::move(password));
        } else if (role == ControlRole::date_time) {
            auto date = std::dynamic_pointer_cast<DateTimePicker>(control);
            if (!date) throw std::invalid_argument("The date/time role requires DateTimePicker");
            peer = std::make_unique<DateTimeHostPeer>(*this, *this, std::move(date));
        } else if (role == ControlRole::file_list) {
            auto list = std::dynamic_pointer_cast<FileList>(control);
            if (!list) throw std::invalid_argument("The file-list role requires a FileList control");
            peer = std::make_unique<FileListHostPeer>(*this, std::move(list));
        } else if (role == ControlRole::data_grid) {
            auto grid = std::dynamic_pointer_cast<DataGrid>(control);
            if (!grid) throw std::invalid_argument("The data-grid role requires a DataGrid control");
            peer = std::make_unique<GridHostPeer>(*this, std::move(grid));
        } else if (role == ControlRole::items_view || role == ControlRole::tree_view || role == ControlRole::command_menu) {
            auto collection = std::dynamic_pointer_cast<VirtualCollection>(control);
            if (!collection) throw std::invalid_argument("The collection role requires a VirtualCollection control");
            peer = std::make_unique<CollectionHostPeer>(*this, std::move(collection));
        } else if (dynamic_cast<Image*>(control.get()) || dynamic_cast<VectorCanvas*>(control.get()) ||
            dynamic_cast<RuntimeHost*>(control.get()) || dynamic_cast<SwapChainPanel*>(control.get())) {
            peer = std::make_unique<SceneHostPeer>(*this, std::move(control));
        } else if (dynamic_cast<MillerColumns*>(control.get()) || dynamic_cast<TabStrip*>(control.get()) ||
            dynamic_cast<TitleBar*>(control.get()) || dynamic_cast<NavigationView*>(control.get()) ||
            dynamic_cast<Breadcrumb*>(control.get()) ||
            dynamic_cast<MenuBar*>(control.get())) {
            peer = std::make_unique<NavigationHostPeer>(*this, std::move(control));
        } else if (role == ControlRole::popup || role == ControlRole::content_view ||
            role == ControlRole::scroll_view || role == ControlRole::split_view || role == ControlRole::expander) {
            peer = std::make_unique<CompositeHostPeer>(*this, std::move(control));
        } else {
            peer = std::make_unique<BasicHostPeer>(*this, std::move(control));
        }
        peer->surface = surface;
        peer->adaptive = adaptive;
        peer->parent = parent;
        const HWND native_parent = parent ? (parent->child_content_window() ?
            parent->child_content_window() : parent->window) : window;
        peer->attach(native_parent, static_cast<int>(100 + peers.size()));
        if (peer->native_field()) {
            if (backdrop_presented) platform::opaque_native_paint(peer->window, true);
            if (options.composition) platform::layered_child(peer->window);
            prepare_popup_surface(*peer);
        }
        win32_require(SetWindowSubclass(peer->window, navigation_procedure, 3,
            reinterpret_cast<DWORD_PTR>(peer.get())) != 0, "Attach browser navigation input");
        if (peer->caption_window())
            win32_require(SetWindowSubclass(peer->caption_window(), navigation_procedure, 3,
                reinterpret_cast<DWORD_PTR>(peer.get())) != 0, "Attach caption navigation input");
        auto* added = peer.get();
        if (peer->image_peer()) has_images = true;
        peers.push_back(std::move(peer));
        peer_lookup.emplace(added->control.get(), added);
        if (!defer_reveal_children(added->control))
            for (const auto& child : added->control->owned_children())
                collect(child, surface || (options.visual_style == VisualStyle::winui && role == ControlRole::expander), added, adaptive);
        collect_clear_button(*added);
    }
    void sync_clear_button(Peer& value) {
        if (auto* input = text_input_peer(value)) input->synchronize_clear();
    }
    void collect_clear_button(Peer& value) {
        if (auto* input = text_input_peer(value)) input->collect_clear();
    }
    void collect_text_clear(TextInputHostPeer& input) override {
        collect(input.clear_button(), input.surface, input.parent, input.adaptive, &input);
        if (!input.clear_window()) input.set_clear_window(find_peer(input.clear_button().get())->window);
    }
    void activate_text_clear(std::uint64_t id) override {
        InputScope scope(*this);
        for (const auto& peer : peers)
            if (peer->control->id() == id) {
                if (auto* input = text_input_peer(*peer)) input->clear_text();
                return;
            }
    }
    void invalidate_native_field(Invalidation kind) override { invalidate(kind); }
    const wchar_t* native_font_family() const override { return root_drawing.edit_font_family(); }
    HFONT native_caption_font() const override { return font; }
    std::shared_ptr<NativeFieldServices> acquire_native_lease() override {
        return std::shared_ptr<NativeFieldServices>(shared_from_this(), this);
    }
    NativeFieldEmbedding native_embedding(const NativeFieldHostPeer& field) const override {
        NativeFieldEmbedding result;
        const Peer* owner = &field;
        if (field.parent) {
            const auto* number = dynamic_cast<const NumericInput*>(field.parent->control.get());
            result.numeric = number != nullptr;
            result.combo = field.parent->control->role() == ControlRole::combo_box;
            result.composite = result.numeric || dynamic_cast<const ComboBox*>(field.parent->control.get());
            result.invalid = number && !number->valid();
            result.reserved_end = result.combo || (number && number->spin_placement() == NumberSpinPlacement::inline_buttons);
            if (result.numeric || result.combo) owner = field.parent;
        }
        if (result.numeric || result.combo) result.field_style = owner->control->effective_control_style_values(StylePart::field);
        result.styled = owner->control->has_control_styling();
        result.enabled = enabled(*owner);
        result.focused = GetFocus() == owner->window || IsChild(owner->window, GetFocus());
        result.hovered = owner->control->hovered();
        for (const auto& child : owner->control->owned_children())
            if (const auto* control = dynamic_cast<const Control*>(child.get())) result.hovered |= control->hovered();
        result.surface = owner->surface;
        return result;
    }
    Peer* find_peer(const Control* control) const {
        const auto found = peer_lookup.find(control);
        return found == peer_lookup.end() ? nullptr : found->second;
    }
    std::uint64_t popup_owner(const Peer* peer) const {
        for (; peer; peer = peer->parent) if (peer->control->role() == ControlRole::popup) return peer->control->id();
        return 0;
    }
    bool in_top_popup(const Peer& peer) const {
        return popups.empty() || popup_owner(&peer) == popups.back().popup->id();
    }
    bool in_popup_surface(const Peer& peer) const {
        for (auto* ancestor = &peer; ancestor; ancestor = ancestor->parent)
            if (ancestor->overlay_surface()) return true;
        return false;
    }
    std::uint64_t adaptive_owner(const AdaptiveLayout* layout) const {
        for (const auto& peer : peers) if (peer->adaptive == layout) return popup_owner(peer.get());
        return 0;
    }
    void hide_tooltip(bool cancel_hover = true) {
        if (window) KillTimer(window, tooltip_timer);
        const auto target = std::exchange(tooltip_target, 0);
        tooltip_revision = 0;
        tooltip_anchor.reset();
        if (std::exchange(tooltip_shown, false)) invalidate(Invalidation::paint);
        if (cancel_hover && target) for (const auto& peer : peers) if (peer->control->id() == target) {
            const auto list = std::dynamic_pointer_cast<NavigationList>(peer->control);
            if (list) list->hover_item({});
            break;
        }
    }
    bool tooltip_current(const Peer& peer) const {
        if (peer.control->id() != tooltip_target) return false;
        const auto* list = dynamic_cast<const NavigationList*>(peer.control.get());
        if (!list) return true;
        const auto anchor = list->hover_anchor();
        return list->hover_revision() == tooltip_revision && anchor && tooltip_anchor &&
            anchor->x == tooltip_anchor->x && anchor->y == tooltip_anchor->y &&
            anchor->width == tooltip_anchor->width && anchor->height == tooltip_anchor->height;
    }
    void place_tooltip(const Peer& peer) {
        const auto view = root->bounds();
        Size size{std::min(400.0f, view.width), 48};
        const auto* list = dynamic_cast<const NavigationList*>(peer.control.get());
        if (tooltip_styling || list) {
            const auto* face = tooltip_styling ? tooltip_styling->effective(StylePart::root, style_states::open) : nullptr;
            const auto* text = tooltip_styling ? tooltip_styling->effective(StylePart::text, style_states::open) : nullptr;
            const auto padding = face && face->padding ? *face->padding : list ? Insets{12, 12, 12, 12} : Insets{10, 2, 10, 2};
            const auto border = face && face->border_thickness ? *face->border_thickness : Insets{1, 1, 1, 1};
            const float horizontal = padding.left + padding.right + border.left + border.right;
            const float vertical = padding.top + padding.bottom + border.top + border.bottom;
            Size measured{};
            auto typography = text ? *text : PartStyleValues{};
            typography.wrapping = true;
            drawing().styled_layout(peer.control->help_text(), TextStyle::caption, typography,
                measured, std::max(1.0f, size.width - horizontal));
            size.height = std::min(view.height, std::max(48.0f, measured.height + vertical));
        }
        auto anchor = list ? list->hover_anchor().value_or(peer.control->bounds()) : peer.control->bounds();
        if (list) anchor.width += 6;
        tooltip_bounds = place_popup(anchor, size, view, list ? PopupPlacement::right : PopupPlacement::below);
    }
    void tooltip_style_changed() {
        if (tooltip_styling && tooltip_styling->fully_empty()) tooltip_styling.reset();
        if (tooltip_shown) for (const auto& peer : peers) if (peer->control->id() == tooltip_target) {
            place_tooltip(*peer);
            break;
        }
        invalidate(Invalidation::paint);
    }
    void visit_visual_tree(const std::shared_ptr<Element>& element, const std::function<void(Element&, Rect)>& visit,
        Rect clip = {0, 0, (std::numeric_limits<float>::max)(), (std::numeric_limits<float>::max)()}) {
        if (!element) return;
        const auto intersect = [](Rect bounds, Rect clip) {
            const auto right = std::min(bounds.x + bounds.width, clip.x + clip.width);
            const auto bottom = std::min(bounds.y + bounds.height, clip.y + clip.height);
            bounds.x = std::max(bounds.x, clip.x); bounds.y = std::max(bounds.y, clip.y);
            bounds.width = std::max(0.0f, right - bounds.x); bounds.height = std::max(0.0f, bottom - bounds.y);
            return bounds;
        };
        auto child_clip = clip;
        if (auto* control = dynamic_cast<Control*>(element.get())) {
            const auto* peer = find_peer(control);
            if (!peer || !visible(*peer)) return;
            child_clip = intersect(clip, viewport(*peer));
        }
        visit(*element, intersect(element->bounds(), clip));
        for (const auto& child : element->owned_children()) visit_visual_tree(child, visit, child_clip);
        if (auto* peer = find_peer(dynamic_cast<Control*>(element.get())); peer && clear_adornment(*peer))
            visit_visual_tree(clear_adornment(*peer), visit, child_clip);
    }
    std::vector<Peer*> visual_peer_order() {
        std::vector<Peer*> ordered;
        const auto append = [&](const std::shared_ptr<Element>& content, std::uint64_t owner,
            const AdaptiveLayout* adaptive) {
            visit_visual_tree(content, [&](Element& element, Rect) {
                auto* peer = find_peer(dynamic_cast<Control*>(&element));
                if (peer && popup_owner(peer) == owner &&
                    (!(peer->adaptive && peer->adaptive->overlay_active()) || peer->adaptive == adaptive))
                    ordered.push_back(peer);
            });
        };
        append(root, 0, nullptr);
        for (const auto* layout : adaptive_layouts)
            if (layout->overlay_active() && !adaptive_owner(layout)) append(layout->navigation(), 0, layout);
        for (const auto& entry : popups) {
            append(entry.popup, entry.popup->id(), nullptr);
            for (const auto* layout : adaptive_layouts)
                if (layout->overlay_active() && adaptive_owner(layout) == entry.popup->id())
                    append(layout->navigation(), entry.popup->id(), layout);
        }
        return ordered;
    }
    void sync_native_order() {
        const auto ordered = visual_peer_order();
        std::unordered_map<HWND, HWND> previous;
        for (auto it = ordered.rbegin(); it != ordered.rend(); ++it) {
            for (const auto hwnd : {(*it)->window, (*it)->caption_window()}) {
                if (!hwnd) continue;
                auto& above = previous[GetParent(hwnd)];
                if (GetWindow(hwnd, GW_HWNDPREV) != above)
                    win32_require(SetWindowPos(hwnd, above ? above : HWND_TOP, 0, 0, 0, 0,
                        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOREDRAW) != 0,
                        "Match native sibling order to visual order");
                above = hwnd;
            }
        }
    }
    void sync_native_occlusion() {
        struct Region { HRGN handle; ~Region() { if (handle) DeleteObject(handle); } };
        if (std::none_of(peers.begin(), peers.end(), [](const auto& peer) { return peer->native(); })) return;
        struct InlineSurface { Peer* peer{}; Rect bounds{}; float radius{}; std::uint64_t owner{}; };
        std::vector<InlineSurface> inline_surfaces;
        const auto append_surfaces = [&](const std::shared_ptr<Element>& content, std::uint64_t owner) {
            visit_visual_tree(content, [&](Element& element, Rect bounds) {
                auto* control = dynamic_cast<Control*>(&element);
                auto* peer = find_peer(control);
                if (peer && (popup_owner(peer) != owner || (peer->adaptive && peer->adaptive->overlay_active()))) return;
                const auto* style = element.has_control_styling() ? element.effective_control_style_values(StylePart::root) : nullptr;
                const auto* stack = dynamic_cast<Stack*>(&element);
                const bool surface = (style && style->background) || (stack && stack->surface());
                const bool paints_content = peer && !peer->native() && control->role() != ControlRole::content_view &&
                    control->role() != ControlRole::scroll_view && control->role() != ControlRole::split_view &&
                    control->role() != ControlRole::popup;
                inline_surfaces.push_back({peer, surface || paints_content ? bounds : Rect{},
                    palette.high_contrast ? 0.0f : style ? style->corner_radius.value_or(0.0f) : stack && stack->surface() ? 4.0f : 0.0f,
                    owner});
            });
        };
        append_surfaces(root, 0);
        for (const auto& entry : popups) append_surfaces(entry.popup, entry.popup->id());
        for (const auto& peer : peers) {
            if (!peer->native()) continue;
            const auto owner = popup_owner(peer.get());
            const auto inline_begin = std::find_if(inline_surfaces.begin(), inline_surfaces.end(),
                [&](const auto& surface) { return surface.peer == peer.get(); });
            if (!peer->native_field()->native_occluded) {
                const auto bounds = clipped_bounds(*peer);
                if (bounds.width <= 0 || bounds.height <= 0) continue;
                const auto overlaps = [&](Rect overlay) {
                    return overlay.width > 0 && overlay.height > 0 &&
                        bounds.x < overlay.x + overlay.width && overlay.x < bounds.x + bounds.width &&
                        bounds.y < overlay.y + overlay.height && overlay.y < bounds.y + bounds.height;
                };
                bool covered = tooltip_shown && overlaps(tooltip_bounds);
                for (const auto* layout : adaptive_layouts)
                    if (layout->overlay_active() && adaptive_owner(layout) == owner && peer->adaptive != layout)
                        covered = covered || overlaps(layout->navigation()->bounds());
                bool above = owner == 0;
                for (const auto& entry : popups) {
                    if (above) covered = covered || overlaps(popup_occlusion(entry));
                    if (entry.popup->id() == owner) above = true;
                }
                if (inline_begin != inline_surfaces.end())
                    for (auto it = std::next(inline_begin); it != inline_surfaces.end() && it->owner == owner; ++it)
                        covered = covered || overlaps(it->bounds);
                if (!covered) continue;
            }
            bool clipped{};
            for (const auto hwnd : {peer->window, peer->caption_window()}) {
                if (!hwnd) continue;
                RECT rect{};
                GetWindowRect(hwnd, &rect);
                Region region{CreateRectRgn(0, 0, rect.right - rect.left, rect.bottom - rect.top)};
                Region existing{CreateRectRgn(0, 0, 0, 0)};
                win32_require(region.handle && existing.handle, "Create native occlusion regions");
                bool occluded{};
                const auto subtract = [&](Rect bounds, float radius = 0.0f) {
                    const float scale = dpi / 96.0f;
                    POINT points[2]{{static_cast<LONG>(std::lround(bounds.x * scale)), static_cast<LONG>(std::lround(bounds.y * scale))},
                        {static_cast<LONG>(std::lround((bounds.x + bounds.width) * scale)), static_cast<LONG>(std::lround((bounds.y + bounds.height) * scale))}};
                    MapWindowPoints(window, nullptr, points, 2);
                    const RECT overlay{points[0].x, points[0].y, points[1].x, points[1].y};
                    RECT overlap{};
                    if (!IntersectRect(&overlap, &rect, &overlay)) return;
                    const auto diameter = static_cast<int>(std::lround(2 * radius * scale));
                    Region cut{diameter ? CreateRoundRectRgn(overlay.left - rect.left, overlay.top - rect.top,
                        overlay.right - rect.left + 1, overlay.bottom - rect.top + 1, diameter, diameter) :
                        CreateRectRgn(overlap.left - rect.left, overlap.top - rect.top, overlap.right - rect.left, overlap.bottom - rect.top)};
                    win32_require(cut.handle != nullptr, "Create native occlusion cut");
                    win32_require(CombineRgn(region.handle, region.handle, cut.handle, RGN_DIFF) != ERROR, "Clip native window under popup");
                    occluded = true;
                };
                for (const auto* layout : adaptive_layouts)
                    if (layout->overlay_active() && adaptive_owner(layout) == owner && peer->adaptive != layout) subtract(layout->navigation()->bounds());
                bool above = owner == 0;
                for (const auto& entry : popups) {
                    if (above) subtract(popup_occlusion(entry));
                    if (entry.popup->id() == owner) above = true;
                }
                if (tooltip_shown) subtract(tooltip_bounds);
                // Transparent text still needs the shared frame above the live EDIT.
                // Complete native capture restores its pixels beneath that text.
                if (inline_begin != inline_surfaces.end())
                    for (auto it = std::next(inline_begin); it != inline_surfaces.end() && it->owner == owner; ++it)
                        if (it->bounds.width > 0 && it->bounds.height > 0) subtract(it->bounds, it->radius);
                const auto existing_type = GetWindowRgn(hwnd, existing.handle);
                if (!occluded) {
                    if (existing_type != ERROR) {
                        win32_require(SetWindowRgn(hwnd, nullptr, TRUE) != 0, "Restore native window region");
                        // A layered child keeps the clipped pixels until it paints again.
                        if (options.composition) RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME);
                    }
                } else if (existing_type == ERROR || !EqualRgn(existing.handle, region.handle)) {
                    win32_require(SetWindowRgn(hwnd, region.handle, TRUE) != 0, "Set native window region");
                    region.handle = nullptr;
                }
                clipped = clipped || occluded;
            }
            peer->native_field()->native_occluded = clipped;
        }
    }
    void offer_tooltip(Peer& peer) {
        for (const auto& item : peers)
            if ((item->runtime_host() && item->runtime_host()->active()) ||
                (item->swap_chain_host() && item->swap_chain_host()->active() && !options.composition)) {
                hide_tooltip(); return;
            }
        if (!window || !ready || !onscreen(peer) || !enabled(peer) || !in_top_popup(peer) ||
            peer.control->help_text().empty() || !IsWindowVisible(window)) {
            if (tooltip_target == peer.control->id()) hide_tooltip();
            return;
        }
        const auto* list = dynamic_cast<const NavigationList*>(peer.control.get());
        const auto anchor = list ? list->hover_anchor() : std::nullopt;
        if (list && !anchor) { hide_tooltip(); return; }
        if (tooltip_current(peer)) return;
        hide_tooltip(tooltip_target != peer.control->id());
        tooltip_target = peer.control->id();
        tooltip_revision = list ? list->hover_revision() : 0;
        tooltip_anchor = anchor;
        tooltip_due = std::chrono::steady_clock::now() + std::chrono::milliseconds(peer.control->tooltip_delay());
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
            if (text_input_peer(*peer)) text_input_peer(*peer)->dismiss_suggestions();
            if (auto* basic = basic_peer(*peer)) basic->stop_repeat();
            ShowWindow(peer->window, SW_HIDE);
            if (GetCapture() == peer->window) ReleaseCapture();
        }
        invalidate(Invalidation::layout);
        if (window && !closing)
            for (const auto& peer : peers)
                if ((IsWindowEnabled(peer->window) != FALSE) != enabled(*peer)) EnableWindow(peer->window, enabled(*peer));
        for (auto& callback : callbacks) if (callback) notify(callback);
        if (restore && window && !closing && GetForegroundWindow() == window && popups.size() == index && IsWindow(return_focus) &&
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
    void dismiss_adaptive_outside(const Peer& destination) {
        // Popup teardown and layout repair can temporarily focus an unrelated native peer.
        if (syncing || !visible(destination)) return;
        for (auto* layout : adaptive_layouts) {
            if (!layout->overlay_active() || !layout->dismiss_on_focus_outside()) continue;
            std::set<std::uint64_t> navigation_ids;
            content_ids(layout->navigation(), navigation_ids);
            const Peer* target = &destination;
            while (target && target->adaptive != layout && !navigation_ids.contains(target->control->id()) &&
                (!target->adaptive || !navigation_ids.contains(target->adaptive->id()))) {
                const auto owner = popup_owner(target);
                const auto entry = std::find_if(popups.begin(), popups.end(),
                    [owner](const auto& entry) { return entry.popup->id() == owner; });
                if (entry == popups.end()) { target = nullptr; break; }
                target = find_peer(entry->anchor.get());
            }
            if (!target) layout->set_navigation_open(false);
        }
    }
    void show_popup(std::shared_ptr<Popup> popup, Control& anchor, Control* initial, std::shared_ptr<ContentDialog> dialog = {},
        std::shared_ptr<CommandSurface> commands = {}, std::optional<Rect> context_anchor = {}) {
        if (has_content_picking()) throw std::invalid_argument("Close pointer picking before opening a popup");
        if (!popup) throw std::invalid_argument("Popup is required");
        if (popup->dialog_surface() && !dialog) throw std::invalid_argument("Use Window::show_dialog for dialog content");
        if (!ready || closing || !window) throw std::logic_error("Popup requires a running window");
        if (popup->is_open()) throw std::logic_error("Popup is already open");
        if (replacing && !retains(anchor))
            throw std::invalid_argument("Popup anchor is being retired");
        for (const auto& peer : peers)
            if (peer->runtime_host() && peer->runtime_host()->active())
                throw std::logic_error("Detach native media and web content before opening an XUI popup");
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
        if (GetForegroundWindow() != window) {
            if (initial) {
                auto* peer = find_peer(initial);
                if (!peer || popup_owner(peer) != popup->id() || !enabled(*peer) || !visible(*peer)) {
                    dismiss_popup(*popup, PopupDismissReason::cancel, false);
                    throw std::invalid_argument("Initial popup focus must be a visible enabled popup child");
                }
            }
            return;
        }
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
        if (auto* basic = basic_peer(peer)) basic->open_combo();
    }
    static void content_ids(const std::shared_ptr<Element>& element, std::set<std::uint64_t>& ids, bool validate = false) {
        if (!element) {
            if (validate) throw std::invalid_argument("Content children must not be null");
            return;
        }
        if (!ids.insert(element->id()).second)
            throw std::invalid_argument("Window content must not contain duplicates or cycles");
        const auto* control = dynamic_cast<const Control*>(element.get());
        if (!control && !dynamic_cast<const Stack*>(element.get()))
            throw std::invalid_argument("Window content supports Stack and standard controls only");
        if (validate && control) {
            const auto* value = control;
            bool supported{};
            switch (control->role()) {
            case ControlRole::label: supported = dynamic_cast<const Label*>(value); break;
            case ControlRole::button: supported = dynamic_cast<const Button*>(value); break;
            case ControlRole::toggle: supported = dynamic_cast<const Toggle*>(value); break;
            case ControlRole::text_input: supported = dynamic_cast<const TextInput*>(value); break;
            case ControlRole::file_list: supported = dynamic_cast<const FileList*>(value); break;
            case ControlRole::scroll_view: supported = dynamic_cast<const ScrollView*>(value); break;
            case ControlRole::image: supported = dynamic_cast<const Image*>(value); break;
            case ControlRole::tab_strip: supported = dynamic_cast<const TabStrip*>(value); break;
            case ControlRole::split_view: supported = dynamic_cast<const SplitView*>(value); break;
            case ControlRole::content_view: supported = true; break;
            case ControlRole::data_grid: supported = dynamic_cast<const DataGrid*>(value); break;
            case ControlRole::history_chart: supported = dynamic_cast<const HistoryChart*>(value); break;
            case ControlRole::popup: supported = dynamic_cast<const Popup*>(value); break;
            case ControlRole::radio_group:
            case ControlRole::choice_list: supported = dynamic_cast<const RadioGroup*>(value); break;
            case ControlRole::combo_box: supported = dynamic_cast<const ComboBox*>(value); break;
            case ControlRole::numeric_input: supported = dynamic_cast<const NumericInput*>(value); break;
            case ControlRole::range_input: supported = dynamic_cast<const RangeInput*>(value); break;
            case ControlRole::expander: supported = dynamic_cast<const Expander*>(value); break;
            case ControlRole::progress: supported = dynamic_cast<const Progress*>(value); break;
            case ControlRole::items_view:
            case ControlRole::tree_view:
            case ControlRole::command_menu: supported = dynamic_cast<const VirtualCollection*>(value); break;
            case ControlRole::document_text: supported = dynamic_cast<const DocumentText*>(value); break;
            case ControlRole::password_input: supported = dynamic_cast<const PasswordInput*>(value); break;
            case ControlRole::date_time: supported = dynamic_cast<const DateTimePicker*>(value); break;
            case ControlRole::inline_status: supported = dynamic_cast<const InlineStatus*>(value) || dynamic_cast<const InfoBadge*>(value); break;
            case ControlRole::color_picker: supported = dynamic_cast<const ColorPicker*>(value); break;
            case ControlRole::vector_canvas: supported = dynamic_cast<const VectorCanvas*>(value); break;
            case ControlRole::map_view: supported = dynamic_cast<const MapView*>(value); break;
            case ControlRole::media_playback: supported = dynamic_cast<const MediaPlayback*>(value); break;
            case ControlRole::web_content: supported = dynamic_cast<const WebContent*>(value); break;
            case ControlRole::swap_chain_panel: supported = dynamic_cast<const SwapChainPanel*>(value); break;
            }
            if (!supported) throw std::invalid_argument("Control role requires its standard control type");
        }
        for (const auto& child : element->owned_children()) content_ids(child, ids, validate);
    }
    bool retains(const Control& control) const {
        std::set<std::uint64_t> ids;
        content_ids(root, ids);
        for (const auto& entry : popups) content_ids(entry.popup, ids);
        return ids.contains(control.id());
    }
    void prune_popups() {
        if (input_depth) return;
        std::set<std::uint64_t> live;
        content_ids(root, live);
        for (const auto& entry : popups) content_ids(entry.popup, live);
        for (const auto& peer : peers)
            if (clear_adornment(*peer) && live.contains(peer->control->id())) live.insert(clear_adornment(*peer)->id());
        std::set<Peer*> retired;
        for (const auto& peer : peers) {
            const auto owner = popup_owner(peer.get());
            if (!live.contains(peer->control->id()) ||
                (owner && std::none_of(popups.begin(), popups.end(), [owner](const auto& e) { return e.popup->id() == owner; }))) {
                retired.insert(peer.get());
            }
        }
        if (retired.empty()) {
            release_claims(&live);
            return;
        }
        InputScope input_scope(*this);
        const auto retiring_window = [&](HWND hwnd) {
            if (!hwnd) return false;
            return std::any_of(retired.begin(), retired.end(), [hwnd](const auto* peer) {
                return hwnd == peer->window || hwnd == peer->caption_window() || IsChild(peer->window, hwnd);
            });
        };
        if (std::any_of(retired.begin(), retired.end(), [&](const auto* peer) { return peer->control->id() == tooltip_target; }))
            hide_tooltip();
        if (const auto hovered = hovered_edit.lock(); hovered &&
            std::any_of(retired.begin(), retired.end(), [&](const auto* peer) { return peer->control == hovered; }))
            hover_edit(nullptr, nullptr);
        if (retiring_window(last_focus)) last_focus = nullptr;
        std::erase_if(focus_targets, retiring_window);
        for (auto& entry : popups) if (retiring_window(entry.return_focus)) entry.return_focus = nullptr;
        if (retiring_window(GetCapture())) ReleaseCapture();
        if (retiring_window(GetFocus())) SetFocus(nullptr);
        for (const auto& peer : peers) if (retired.contains(peer.get())) {
            if (auto* basic = basic_peer(*peer)) basic->stop_repeat();
            if (auto range = std::dynamic_pointer_cast<RangeInput>(peer->control)) range->cancel_drag();
            peer->control->cancel();
            peer->control->pointer_move(false);
            if (text_input_peer(*peer)) text_input_peer(*peer)->dismiss_suggestions();
            detach_peer(*peer);
        }
        // Remove peers from lookups before DestroyWindow sends synchronous notifications.
        // Keep all retired parents alive until their children have been destroyed.
        std::vector<std::unique_ptr<Peer>> removed;
        removed.reserve(retired.size());
        for (auto& peer : peers) if (retired.contains(peer.get())) {
            peer_lookup.erase(peer->control.get());
            removed.push_back(std::move(peer));
        }
        std::erase_if(peers, [](const auto& peer) { return !peer; });
        while (!removed.empty()) removed.pop_back();
        release_claims(&live);
    }
    void create() {
        if (used) throw std::logic_error("A Window can run only once");
        if (closing) throw std::logic_error("The Window is closed");
        used = true;
        if (!root) throw std::logic_error("Window content is required");
        claim(root);
        drawing().initialize(options.visual_style);
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
        cls.lpfnWndProc = scroll_content_procedure;
        cls.lpszClassName = scroll_content_class;
        if (!RegisterClassExW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            win32_require(false, "Register scroll content window");
        const auto extent = [](float value, int fallback) {
            return std::isfinite(value) && value > 0 ? static_cast<int>(std::clamp(value, 240.0f, 16000.0f)) : fallback;
        };
        material_popup = options.transparent && options.backdrop != WindowBackdrop::none && !options.composition;
        // DWM draws only the solid fallback behind a frameless WS_POPUP. A captioned frame whose
        // non-client area is removed in WM_NCCALCSIZE receives the live material.
        const DWORD frame = frameless() ? WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX :
            options.transparent ? WS_POPUP : WS_OVERLAPPEDWINDOW;
        const DWORD style = frame | WS_CLIPCHILDREN |
            (initial_placement && initial_placement->maximized ? WS_MAXIMIZE : 0);
        // A composition window has no redirection surface; its visual tree supplies every pixel.
        const DWORD ex_style = WS_EX_CONTROLPARENT | (layered() ? WS_EX_LAYERED : 0) |
            (options.composition ? WS_EX_NOREDIRECTIONBITMAP : 0);
        const UINT initial_dpi = GetDpiForSystem();
        RECT outer{0, 0, MulDiv(extent(options.size.width, 600), initial_dpi, 96),
            MulDiv(extent(options.size.height, 520), initial_dpi, 96)};
        if (!frameless())
            win32_require(AdjustWindowRectExForDpi(&outer, style, FALSE, ex_style, initial_dpi) != 0, "Size application window");
        window = CreateWindowExW(ex_style, window_class, options.title.c_str(),
            style, initial_placement ? initial_placement->x : CW_USEDEFAULT,
            initial_placement ? initial_placement->y : CW_USEDEFAULT,
            initial_placement ? initial_placement->width : outer.right - outer.left,
            initial_placement ? initial_placement->height : outer.bottom - outer.top, nullptr,
            nullptr, GetModuleHandleW(nullptr), this);
        win32_require(window != nullptr, "Create application window");
        dpi = GetDpiForWindow(window);
        if (options.composition) {
            platform::composition_frame(window, composition_card());
            presenter = std::make_shared<platform::CompositionPresenter>(window);
        }
        if (material_popup) platform::round_corners(window);
        if (frameless())
            win32_require(SetWindowPos(window, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                SWP_NOACTIVATE | SWP_FRAMECHANGED) != 0, "Remove the window frame");
        if (auto configure = std::move(native_created)) configure(window);
        if (icon_configured) apply_icons(file_type_icons(icon_extension, icon_directory, dpi));
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
        if (!icon_configured) window_icon.refresh(window, dpi, wake);
        apply_theme();
        layout_pending = true;
        update();
        if (layered() || composition_card()) paint();
        if (!options.show_activated && moving_tabs && moving_tabs->window && moving_tabs != this &&
            moving_tabs->application.lock() == application.lock())
            win32_require(SetWindowPos(window, moving_tabs->window, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW) != 0, "Show remaining content below dragged window");
        else if (initial_placement && initial_placement->maximized)
            ShowWindow(window, options.show_activated ? SW_SHOW : SW_SHOWNA);
        else ShowWindow(window, options.show_activated ? SW_SHOWNORMAL : SW_SHOWNOACTIVATE);
        if (options.show_activated && !IsChild(window, GetFocus())) platform::traverse_focus(focus_targets, false);
    }
    void apply_theme() {
        BOOL animation{};
        win32_require(SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animation, 0) != 0,
            "Read client area animation preference");
        client_animation = animation != FALSE;
        if (!client_animation) stop_progress_animation();
        if (!window) return;
        cancel_control_menu(window);
        if (options.visual_style != VisualStyle::winui) hover_edit(nullptr, nullptr);
        palette = Palette::system(options.theme, options.visual_style);
        drawing().set_visual_style(options.visual_style);
        transparency_effects = platform::transparency_effects_enabled();
        const bool backdrop_wanted = desired_backdrop_type() != 0;
        platform::appearance(window, palette.mode, palette, backdrop_wanted);
        sync_backdrop();
        if (backdrop_wanted && !backdrop_presented) platform::appearance(window, palette.mode, palette, false);
        palette.backdrop = backdrop_presented;
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
            DEFAULT_QUALITY, DEFAULT_PITCH, drawing().edit_font_family());
        win32_require(next_font != nullptr, "Create input caption font");
        for (const auto& peer : peers) {
            if (auto* field = peer->native_field()) field->theme_native(dpi, palette, next_font);
            else peer->set_theme(dpi, palette);
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
        if (scroll_pending && animation_timer_running) layout_pending = true;
        bool scroll_only = scroll_pending && !layout_pending && !placement_pending && !state_pending;
        const bool arrange_scroll = std::exchange(scroll_pending, false);
        state_pending = false;
        if (!scroll_only) prune_popups();
        InputScope input_scope(*this);
        for (const auto& entry : popups) if (!command_popup_current(entry)) {
            auto popup = entry.popup;
            dismiss_popup(*popup, PopupDismissReason::hidden);
            break;
        }
        if (!ready || !window || closing) return;
        const auto focus_before_layout = GetFocus();
        if (!scroll_only) {
            const auto before = peers.size();
            adaptive_layouts.clear();
            collect(root);
            for (const auto& entry : popups) {
                if (entry.dialog) entry.dialog->set_visual_style(options.visual_style);
                collect(entry.popup, !entry.popup->window_background());
            }
            if (peers.size() != before) apply_theme();
            sync_animations();
        }
        if (arrange_scroll && !layout_pending) {
            std::vector<Rect> anchors;
            for (const auto& entry : popups) anchors.push_back(entry.anchor->bounds());
            for (const auto& peer : peers) {
                const auto* scroll = dynamic_cast<const ScrollView*>(peer->control.get());
                if (!scroll || scroll->passthrough()) continue;
                const auto view = scroll->viewport();
                const auto content = scroll->content()->bounds();
                if (content.x != view.x || content.y != view.y - scroll->offset())
                    scroll->content()->arrange({view.x, view.y - scroll->offset(), view.width, scroll->extent()});
            }
            // A stationary popup can contain a scrolling viewport without changing its placement.
            // Only a moved anchor requires layout outside the scrolled subtree.
            for (std::size_t i = 0; i < popups.size(); ++i) {
                const auto bounds = popups[i].anchor->bounds();
                if (i >= anchors.size() || bounds.x != anchors[i].x || bounds.y != anchors[i].y ||
                    bounds.width != anchors[i].width || bounds.height != anchors[i].height)
                    layout_pending = true;
            }
            // Custom layout callbacks may have invalidated layout or state during arrangement.
            if (layout_pending || state_pending) scroll_only = false;
        }
        const bool arrange_layout = layout_pending;
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
                                const auto combo_field = combo->field_bounds();
                                entry.combo_alignment = combo_field.y - entry.anchor->bounds().y + combo_field.height / 2 -
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
            ++layouts;
        }
        if (arrange_layout || placement_pending || arrange_scroll) {
            if (!arrange_layout) {
                for (const auto& peer : peers)
                    if (dynamic_cast<Animation*>(peer->control.get()))
                        peer->control->arrange(peer->control->bounds());
            }
            placement_pending = false;
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
            for (const auto& peer : peers) if (clear_adornment(*peer)) sync_clear_button(*peer);
            for (const auto& peer : peers) {
                auto bounds = peer->control->bounds();
                if (text_input_peer(*peer) && peer->arranged &&
                    (bounds.x != peer->absolute_bounds.x || bounds.y != peer->absolute_bounds.y))
                    text_input_peer(*peer)->dismiss_suggestions();
                peer->absolute_bounds = bounds;
                if (peer->child_content_window()) {
                    // One native translation moves the retained editors without recreating them
                    // or sending placement notifications to every descendant.
                    const auto& scroll = static_cast<const ScrollView&>(*peer->control);
                    const auto content = scroll.content()->bounds();
                    const float scale = dpi / 96.0f;
                    const RECT target{static_cast<LONG>(std::lround((content.x - bounds.x) * scale)),
                        static_cast<LONG>(std::lround((content.y - bounds.y) * scale)),
                        static_cast<LONG>(std::lround(content.width * scale)),
                        static_cast<LONG>(std::lround(content.height * scale))};
                    if (!composite_peer(*peer)->scroll_placed || !EqualRect(&target, &composite_peer(*peer)->scroll_placed_bounds)) {
                        auto& group = batch.for_parent(peer->window);
                        group = DeferWindowPos(group, peer->child_content_window(), nullptr,
                            target.left, target.top, target.right, target.bottom,
                            SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW | SWP_NOCOPYBITS);
                        win32_require(group != nullptr, "Arrange scroll content window");
                        composite_peer(*peer)->scroll_placed_bounds = target;
                        composite_peer(*peer)->scroll_placed = true;
                    }
                }
                if (peer->parent) {
                    const auto parent = peer->parent->child_content_window() ?
                        static_cast<const ScrollView&>(*peer->parent->control).content()->bounds() :
                        peer->parent->control->bounds();
                    bounds.x -= parent.x;
                    bounds.y -= parent.y;
                }
                if (scroll_only && peer->arranged && bounds.x == peer->arranged_bounds.x &&
                    bounds.y == peer->arranged_bounds.y && bounds.width == peer->arranged_bounds.width &&
                    bounds.height == peer->arranged_bounds.height) continue;
                peer->arranged_bounds = bounds;
                peer->arranged = true;
                if (peer->control->role() == ControlRole::tab_strip) {
                    // Round the shared bottom edge, not the height, so tabs meet their content at fractional DPI.
                    const float scale = dpi / 96.0f;
                    bounds.height = std::max(0.0f, static_cast<float>(
                        std::lround((bounds.y + bounds.height) * scale) - std::lround(bounds.y * scale)) / scale);
                }
                if (peer->parent && dynamic_cast<MillerColumns*>(peer->parent->control.get())) {
                    const float scale = dpi / 96.0f;
                    bounds.width = std::max(0.0f, static_cast<float>(
                        std::lround((bounds.x + bounds.width) * scale) - std::lround(bounds.x * scale)) / scale);
                }
                if (auto* field = peer->native_field()) {
                    field->arrange_native(bounds, dpi, visible(*peer), palette);
                } else if (has_images || animation_timer_running || !arrange_layout) {
                    const float scale = dpi / 96.0f;
                    RECT target{static_cast<LONG>(std::lround(bounds.x * scale)),
                        static_cast<LONG>(std::lround(bounds.y * scale)),
                        static_cast<LONG>(std::lround(bounds.width * scale)),
                        static_cast<LONG>(std::lround(bounds.height * scale))};
                    if (!peer->placed || !EqualRect(&target, &peer->placed_bounds)) {
                        // Win32 requires every deferred group to have the same native parent.
                        auto& group = batch.for_parent(GetParent(peer->window));
                        group = DeferWindowPos(group, peer->window, nullptr,
                            target.left, target.top, target.right, target.bottom,
                            SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW);
                        win32_require(group != nullptr, "Place image control");
                        peer->placed_bounds = target; peer->placed = true;
                    }
                } else {
                    platform::place(peer->window, bounds, dpi);
                    peer->placed = false;
                }
                const bool shown = visible(*peer);
                if (((GetWindowLongPtrW(peer->window, GWL_STYLE) & WS_VISIBLE) != 0) != shown)
                    ShowWindow(peer->window, shown ? SW_SHOWNA : SW_HIDE);
            }
            batch.finish();
            sync_native_order();
            // Match native hit testing to the back-to-front popup composition order.
            for (const auto& entry : popups) {
                auto* peer = find_peer(entry.popup.get());
                win32_require(SetWindowPos(peer->window, HWND_TOP, 0, 0, 0, 0,
                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOREDRAW) != 0, "Raise popup input surface");
                if (peer->overlay_surface()) {
                    RECT bounds{};
                    win32_require(GetClientRect(peer->window, &bounds) != 0, "Read popup surface bounds");
                    const auto* style = entry.popup->effective_control_style_values(StylePart::root);
                    const auto radius = palette.high_contrast ? 0.0f :
                        style && style->corner_radius ? *style->corner_radius :
                        entry.commands || palette.style == VisualStyle::winui ? 8.0f : 0.0f;
                    const auto diameter = static_cast<int>(std::lround(2 * radius * dpi / 96.0f));
                    auto region = diameter ? CreateRoundRectRgn(0, 0, bounds.right + 1, bounds.bottom + 1, diameter, diameter) :
                        CreateRectRgn(0, 0, bounds.right, bounds.bottom);
                    win32_require(region != nullptr, "Create popup surface region");
                    if (!SetWindowRgn(peer->window, region, FALSE)) {
                        DeleteObject(region);
                        win32_require(false, "Clip popup surface");
                    }
                }
            }
            update_paint_bounds();
        }
        if (arrange_layout) sync_animations();
        if (!scroll_only) focus_targets.clear();
        sync_images();
        HWND disabled_focus{};
        std::vector<Peer*> update_peers;
        for (const auto& peer : peers) update_peers.push_back(peer.get());
        for (auto* peer : update_peers) {
            const auto& control = *peer->control;
            if (!visible(*peer)) {
                if (auto* state = basic_peer(*peer)) state->reset_text_layout();
                if (auto label = std::dynamic_pointer_cast<Label>(peer->control)) label->discard_wrapped_text();
            }
            if (peer->runtime_host()) {
                peer->runtime_host()->sync(visible(*peer) && onscreen(*peer) && IsWindowVisible(window) && !IsIconic(window), dpi);
                if (!window || closing) return;
                if (peer->runtime_host()->active()) hide_tooltip();
            }
            if (peer->swap_chain_host()) {
                auto clip = clipped_bounds(*peer);
                const bool shown = visible(*peer) && clip.width > 0 && clip.height > 0 &&
                    IsWindowVisible(window) && !IsIconic(window);
                clip.x -= peer->paint_bounds.x;
                clip.y -= peer->paint_bounds.y;
                peer->swap_chain_host()->sync(shown, dpi, clip, popup_owner(peer) != 0);
                if (!window || closing) return;
                if (peer->swap_chain_host()->active() && !options.composition) hide_tooltip();
            }
            if (scroll_only) {
                // UIA reads native bounds on demand. Only scroll state changed; do not rewrite
                // native text, fonts, selection, or the unchanged accessibility snapshots.
                if (control.role() == ControlRole::scroll_view)
                    peer->publish_accessibility();
                continue;
            }
            const bool tab_stop = control.focusable() && control.tab_stop() && !is_caption_button(control);
            const auto native_style = GetWindowLongPtrW(peer->window, GWL_STYLE);
            if (((native_style & WS_TABSTOP) != 0) != tab_stop)
                win32_require(SetWindowLongPtrW(peer->window, GWL_STYLE, tab_stop ?
                    native_style | WS_TABSTOP : native_style & ~static_cast<LONG_PTR>(WS_TABSTOP)) != 0, "Update control tab stop");
            if (tab_stop && enabled(*peer) && visible(*peer) && in_top_popup(*peer) &&
                (control.role() != ControlRole::split_view || static_cast<const SplitView&>(control).divider().width > 0))
                focus_targets.push_back(peer->window);
            if ((!enabled(*peer) || !visible(*peer)) &&
                (GetFocus() == peer->window || focus_before_layout == peer->window)) disabled_focus = peer->window;
            if (auto* composite = composite_peer(*peer)) composite->reconcile_input();
            if (!control.captured() && !peer->capture_active() && GetCapture() == peer->window) ReleaseCapture();
            if (!has_images || (IsWindowEnabled(peer->window) != FALSE) != enabled(*peer))
                EnableWindow(peer->window, enabled(*peer));
            peer->control->set_control_style_context_enabled(enabled(*peer));
            if (!enabled(*peer) || !visible(*peer)) {
                peer->cancel_input();
                peer->control->pointer_move(false);
                if (GetCapture() == peer->window) ReleaseCapture();
            }
            if (peer->family == HostFamily::file_list && GetFocus() == peer->window) last_focus = peer->window;
            peer->sync(dpi, palette);
        }
        if (disabled_focus) {
            bool repaired{};
            for (const auto& peer : peers) if (peer->window == disabled_focus) {
                if (const auto owner = clear_owner(*peer))
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
            if (it == peers.end() || !enabled(**it) || !onscreen(**it) || !tooltip_current(**it) ||
                (*it)->control->help_text().empty()) hide_tooltip();
            else if (tooltip_shown) place_tooltip(**it);
        }
        if (window) {
            sync_progress_animation();
            sync_native_occlusion();
            InvalidateRect(window, nullptr, FALSE);
        }
        validate_active_inspection();
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
            const auto forward = [](const Element& from, Element& to, StyleTarget target) {
                if (!from.has_control_styling()) return;
                to.set_control_style(from.control_style());
                for (const auto& part : control_style_schema(target).parts) {
                    const auto& values = from.control_style_values(part.part);
                    if (!values.empty()) to.set_control_style_values(part.part, values);
                }
            };
            forward(*source->menu(), *child->menu(), StyleTarget::command_menu);
            forward(*source->popup(), *child->popup(), StyleTarget::popup);
            forward(*source->status(), *child->status(), StyleTarget::label);
            forward(*source->content(), *child->content(), StyleTarget::stack);
            forward(*source->results(), *child->results(), StyleTarget::stack);
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
        if (auto result = inspect_pointer(msg.hwnd, msg.message, msg.wParam, msg.lParam)) return true;
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
            if (peer->runtime_host() && peer->control->role() == ControlRole::web_content && peer->runtime_host()->contains_native(msg.hwnd)) return false;
        for (const auto& peer : peers) if (peer->window == msg.hwnd && text_input_peer(*peer)) {
            if (text_input_peer(*peer)->composing()) return false;
            if (text_input_peer(*peer)->pretranslate_key(msg.wParam)) return true;
        }
        for (const auto& peer : peers) if (peer->window == msg.hwnd && native_document_peer(*peer) && native_document_peer(*peer)->composing()) return false;
        if (translate_menu_bar(msg)) return true;
        Control* target{};
        for (const auto& peer : peers) if (peer->window == msg.hwnd) target = peer->control.get();
        if (key && (popups.empty() || !popups.back().dialog)) {
            const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            const bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
            const bool altgr = control && (GetKeyState(VK_RMENU) & 0x8000) != 0;
            const bool editor = target && (target->role() == ControlRole::text_input ||
                target->role() == ControlRole::document_text || target->role() == ControlRole::password_input);
            const auto character = MapVirtualKeyExW(static_cast<UINT>(msg.wParam), MAPVK_VK_TO_CHAR, GetKeyboardLayout(0)) & 0x7fffffff;
            const bool text_input = !editor && ((!control && !alt) || altgr) &&
                (character >= 32 || msg.wParam == VK_PACKET || msg.wParam == VK_PROCESSKEY);
            auto callback = key;
            if (callback({static_cast<Key>(msg.wParam), control,
                (GetKeyState(VK_SHIFT) & 0x8000) != 0, target, alt, text_input})) return true;
            // Redirect before TranslateMessage so dead keys, Unicode and IME stay in the native editor.
            if (text_input && GetFocus() != msg.hwnd) {
                for (const auto& peer : peers) if (peer->window == GetFocus() && text_input_peer(*peer) && enabled(*peer)) {
                    msg.hwnd = peer->window;
                    return false;
                }
            }
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
        if (msg.wParam == VK_ESCAPE && target) {
            auto* peer = find_peer(target);
            if (peer && peer->adaptive && peer->adaptive->overlay_active() && peer->adaptive->dismiss_on_focus_outside()) {
                peer->adaptive->set_navigation_open(false);
                update();
                traverse(false);
                return true;
            }
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
        for (const auto& peer : peers) if (peer->window == msg.hwnd && text_input_peer(*peer) && peer->parent) {
            if (auto* numeric = dynamic_cast<NumericInput*>(peer->parent->control.get());
                numeric && (msg.wParam == VK_UP || msg.wParam == VK_DOWN)) {
                numeric->step(msg.wParam == VK_UP ? 1 : -1); return true;
            }
            if (dynamic_cast<ComboBox*>(peer->parent->control.get()) && (msg.wParam == VK_F4 ||
                (msg.wParam == VK_DOWN && GetKeyState(VK_MENU) < 0))) {
                open_combo(*peer->parent); return true;
            }
        }
        if (const auto* panel = dynamic_cast<SwapChainPanel*>(target); panel && panel->native_input())
            return false;
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
            for (const auto& peer : peers) if (peer->window == msg.hwnd && peer->family != HostFamily::file_list)
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
    bool enabled(const Peer& peer, bool respect_modal = true, bool respect_geometry = true) const {
        const auto owner = popup_owner(&peer);
        bool modal_scope = true;
        for (auto it = popups.rbegin(); it != popups.rend(); ++it) {
            if (it->popup->id() == owner) break;
            if (it->dialog) { modal_scope = false; break; }
        }
        if (respect_modal && !modal_scope) return false;
        for (const auto& entry : popups) if (entry.popup->id() == owner && !command_popup_current(entry)) return false;
        for (auto* ancestor = &peer; ancestor; ancestor = ancestor->parent)
            if (!ancestor->control->enabled() || (respect_geometry && ancestor->control->role() == ControlRole::content_view &&
                !opening_reveal(*ancestor) &&
                (ancestor->control->bounds().width <= 0 || ancestor->control->bounds().height <= 0))) return false;
        for (auto* parent = peer.parent; parent; parent = parent->parent)
            if (auto* expander = dynamic_cast<Expander*>(parent->control.get()); expander && !expander->expanded()) return false;
        for (auto* ancestor = &peer; ancestor; ancestor = ancestor->parent)
            if (auto* reveal = dynamic_cast<Reveal*>(ancestor->control.get()); reveal && !reveal->open()) return false;
        for (auto* ancestor = &peer; ancestor && ancestor->parent; ancestor = ancestor->parent)
            if (auto* split = dynamic_cast<SplitView*>(ancestor->parent->control.get());
                split && ancestor->control == split->owned_children()[1] && !split->expanded()) return false;
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
        if (auto content = dynamic_cast<ContentView*>(peer.control.get())) return content->content_bounds();
        if (auto expander = dynamic_cast<Expander*>(peer.control.get())) return expander->content_bounds();
        if (auto popup = dynamic_cast<Popup*>(peer.control.get())) return popup->content_bounds();
        return peer.control->bounds();
    }
    static bool opening_reveal(const Peer& peer) {
        if (const auto* expander = dynamic_cast<Expander*>(peer.control.get()))
            return expander->allows_empty_clip() && expander->bounds().width > 0;
        const auto* reveal = dynamic_cast<Reveal*>(peer.control.get());
        if (!reveal || !reveal->opening_from_zero()) return false;
        const auto bounds = reveal->bounds();
        return reveal->vertical() ? bounds.width > 0 : bounds.height > 0;
    }
    Rect clipped_bounds(const Peer& peer, float outset = 0) const {
        auto result = peer.control->bounds();
        result = {result.x - outset, result.y - outset, result.width + 2 * outset, result.height + 2 * outset};
        const auto intersect = [&](Rect clip) {
            const auto right = std::min(result.x + result.width, clip.x + clip.width);
            const auto bottom = std::min(result.y + result.height, clip.y + clip.height);
            result.x = std::max(result.x, clip.x); result.y = std::max(result.y, clip.y);
            result.width = std::max(0.0f, right - result.x); result.height = std::max(0.0f, bottom - result.y);
        };
        intersect(root->bounds());
        for (auto* parent = peer.parent; parent; parent = parent->parent) intersect(viewport(*parent));
        return result;
    }
    bool onscreen(const Peer& peer) const {
        if (!visible(peer)) return false;
        const auto result = clipped_bounds(peer);
        return result.width > 0 && result.height > 0;
    }
    bool visible(const Peer& peer, bool respect_geometry = true) const {
        for (auto* ancestor = &peer; ancestor; ancestor = ancestor->parent) {
            if (!ancestor->control->visible()) return false;
            if (auto* popup = dynamic_cast<Popup*>(ancestor->control.get()); popup && !popup->is_open()) return false;
            if (ancestor != &peer)
                if (auto* expander = dynamic_cast<Expander*>(ancestor->control.get()); expander && !expander->body_presented()) return false;
            const auto rect = ancestor == &peer ? ancestor->control->bounds() : viewport(*ancestor);
            if (respect_geometry && (rect.width <= 0 || rect.height <= 0) && !opening_reveal(*ancestor)) return false;
        }
        return true;
    }
    void stop_progress_animation() {
        if (progress_animating && window) KillTimer(window, progress_timer);
        progress_animating = false;
        progress_phase = 0;
    }
    bool animated_progress(const Peer& peer) const {
        const auto* progress = dynamic_cast<const Progress*>(peer.control.get());
        return progress && progress->state() == ProgressState::indeterminate &&
            enabled(peer) && onscreen(peer) && retains(*progress);
    }
    void sync_progress_animation() {
        const bool active = window && ready && !closing && client_animation && presentation_animations && !palette.high_contrast &&
            IsWindowVisible(window) && IsWindowEnabled(window) && !IsIconic(window) &&
            std::any_of(peers.begin(), peers.end(), [&](const auto& peer) { return animated_progress(*peer); });
        if (!active) { stop_progress_animation(); return; }
        if (!progress_animating) {
            win32_require(SetTimer(window, progress_timer, 16, nullptr) != 0, "Start progress animation");
            progress_epoch = std::chrono::steady_clock::now();
            progress_animating = true;
        }
    }
    void reveal(Peer& peer) {
        if (auto* list = dynamic_cast<MillerColumnList*>(peer.control.get());
            list && list->owner() && list->column_index() < list->owner()->columns().size()) {
            list->owner()->set_active_column(list->column_index());
            update();
        }
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
        case VK_PRIOR: scroll.scroll_by(-scroll.viewport().height); break;
        case VK_NEXT: scroll.scroll_by(scroll.viewport().height); break;
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
        const double distance = vertical_wheel_dips(wparam, scroll.viewport().height);
        const auto before = scroll.offset();
        scroll.scroll_by(static_cast<float>(-distance));
        update();
        if (distance && before == scroll.offset() && owner->parent) return scroll_wheel(*owner->parent, wparam);
        return true;
    }
    using SubtreePaint = std::function<void(Peer*)>;
    void paint_surfaces(const Stack& stack, const SubtreePaint& paint_peer = {},
        std::uint64_t owner = 0, const AdaptiveLayout* adaptive = nullptr) {
        if (stack.material() != SurfaceMaterial::solid) {
            const auto* style = stack.effective_control_style_values(StylePart::root);
            const float radius = palette.high_contrast ? 0.0f : style && style->corner_radius ? *style->corner_radius :
                style_metrics(palette.style).surface_radius;
            // Without a composition window, or without transparency effects, backdrop acrylic
            // falls back to the in-app material or its solid color. Popups also use the in-app
            // material: a backdrop hole would show the desktop instead of the window beneath them.
            const bool backdrop = stack.material() == SurfaceMaterial::backdrop_acrylic && options.composition &&
                transparency_effects && !palette.high_contrast && owner == 0;
            if (!(backdrop && drawing().backdrop_hole(stack.bounds(), radius)))
                drawing().acrylic(stack.bounds(), palette, radius, transparency_effects);
            if (style) drawing().styled_surface(stack.bounds(), palette, *style, D2D1::ColorF(0, 0.0f),
                palette.border, radius, {1, 1, 1, 1});
            else drawing().rounded(stack.bounds(), palette.border, radius, true);
            add_hit_shape(stack.bounds(), radius);
        } else if (const auto* style = stack.effective_control_style_values(StylePart::root)) {
            drawing().styled_surface(stack.bounds(), palette, *style, stack.surface() ? palette.surface : D2D1::ColorF(0, 0.0f),
                palette.border, stack.surface() ? 4.0f : 0.0f, stack.surface() ? Insets{1, 1, 1, 1} : Insets{});
            if (stack.surface() || style->background)
                add_hit_shape(stack.bounds(), palette.high_contrast ? 0.0f : style->corner_radius.value_or(stack.surface() ? 4.0f : 0.0f));
        } else if (stack.surface()) {
            drawing().surface_frame(stack.bounds(), palette);
            add_hit_shape(stack.bounds(), style_metrics(palette.style).surface_radius);
        }
        if (const auto* style = stack.effective_separator_style()) {
            const bool dialog_footer = palette.style == VisualStyle::winui && std::any_of(popups.begin(), popups.end(),
                [&](const auto& entry) { return entry.dialog && entry.dialog->footer().get() == &stack; });
            if (const auto* split = dynamic_cast<const SplitButton*>(&stack)) {
                const auto primary = split->primary()->bounds(), secondary = split->secondary()->bounds();
                const float gap = std::max(0.0f, secondary.x - primary.x - primary.width);
                const float thickness = std::min(1.0f, gap);
                const auto top = std::max(primary.y, secondary.y);
                const auto height = std::max(0.0f, std::min(primary.y + primary.height, secondary.y + secondary.height) - top);
                drawing().fill({primary.x + primary.width + (gap - thickness) / 2,
                    top, thickness, height},
                    !palette.high_contrast && style->background ? D2D1::ColorF(style->background->resolve(palette.mode)) : palette.border);
            } else if (!dialog_footer) {
                const auto bounds = stack.bounds();
                const auto thickness = std::min(bounds.height, style->thickness.value_or(1.0f));
                drawing().fill({bounds.x, bounds.y + bounds.height - thickness, bounds.width, thickness},
                    !palette.high_contrast && style->background ? D2D1::ColorF(style->background->resolve(palette.mode)) : palette.border);
            }
        } else if (stack.separator_after()) {
            const auto bounds = stack.bounds();
            drawing().line(bounds.x, bounds.y + bounds.height + 3.5f,
                bounds.x + bounds.width, bounds.y + bounds.height + 3.5f, palette.border);
        }
        const auto* style = palette.high_contrast ? nullptr : stack.effective_control_style_values(StylePart::root);
        const auto rounded = style && style->corner_radius ?
            drawing().push_rounded_clip(stack.bounds(), *style->corner_radius) : 0;
        for (const auto& child : stack.owned_children())
            paint_content_surface(child, paint_peer, owner, adaptive);
        for (std::size_t i = 0; i < rounded; ++i) drawing().pop_rounded_clip();
    }
    void paint_content_surface(const std::shared_ptr<Element>& element, const SubtreePaint& paint_peer = {},
        std::uint64_t owner = 0, const AdaptiveLayout* adaptive = nullptr) {
        if (!element) return;
        if (element->bounds().width <= 0 || element->bounds().height <= 0) return;
        auto* peer = find_peer(dynamic_cast<Control*>(element.get()));
        if (peer && (!visible(*peer) || (paint_peer && (popup_owner(peer) != owner ||
            (peer->adaptive && peer->adaptive->overlay_active() && peer->adaptive != adaptive))))) return;
        if (auto stack = std::dynamic_pointer_cast<Stack>(element)) paint_surfaces(*stack, paint_peer, owner, adaptive);
        else if (auto scroll = std::dynamic_pointer_cast<ScrollView>(element)) {
            if (const auto* style = scroll->effective_control_style_values(StylePart::root))
                drawing().styled_surface(scroll->bounds(), palette, *style, D2D1::ColorF(0, 0.0f), palette.border, 0, {});
            if (paint_peer && peer) paint_peer(peer);
            drawing().push_clip(scroll->viewport());
            for (const auto& child : scroll->owned_children()) paint_content_surface(child, paint_peer, owner, adaptive);
            drawing().pop_clip();
        } else if (auto content = std::dynamic_pointer_cast<ContentView>(element)) {
            if (const auto* style = content->effective_control_style_values(StylePart::root))
                drawing().styled_surface(content->bounds(), palette, *style, D2D1::ColorF(0, 0.0f), palette.border, 0, {});
            if (paint_peer && peer) paint_peer(peer);
            drawing().push_clip(content->content_bounds());
            for (const auto& child : content->owned_children()) paint_content_surface(child, paint_peer, owner, adaptive);
            drawing().pop_clip();
        } else if (auto split = std::dynamic_pointer_cast<SplitView>(element)) {
            drawing().push_clip(split->bounds());
            if (const auto* style = split->effective_control_style_values(StylePart::root))
                drawing().styled_surface(split->bounds(), palette, *style, D2D1::ColorF(0, 0.0f), palette.border, 0, {});
            if (const auto* style = split->primary_visible() ? split->effective_control_style_values(StylePart::first_pane) : nullptr)
                drawing().styled_surface(split->first_pane_area(),
                    palette, *style, D2D1::ColorF(0, 0.0f), palette.border, 0, {});
            const auto second = split->second_pane_area();
            if (second.width > 0 && second.height > 0)
                if (const auto* style = split->effective_control_style_values(StylePart::second_pane))
                drawing().styled_surface(second,
                    palette, *style, D2D1::ColorF(0, 0.0f), palette.border, 0, {});
            if (paint_peer && peer) paint_peer(peer);
            for (const auto& child : split->owned_children()) paint_content_surface(child, paint_peer, owner, adaptive);
            drawing().pop_clip();
        } else if (auto control = std::dynamic_pointer_cast<Control>(element)) {
            if (control->visible()) add_hit_shape(control->bounds(), 0);
            drawing().push_clip(control->bounds());
            if (control->visible() && (dynamic_cast<NavigationView*>(control.get()) || dynamic_cast<NavigationPane*>(control.get()) ||
                dynamic_cast<Breadcrumb*>(control.get()) || dynamic_cast<CommandBar*>(control.get()) ||
                dynamic_cast<MenuBar*>(control.get()) || dynamic_cast<TitleBar*>(control.get()))) {
                if (const auto* style = control->effective_control_style_values(StylePart::root))
                    drawing().styled_surface(control->bounds(), palette, *style, D2D1::ColorF(0, 0.0f), palette.border, 0, {});
            }
            if (auto bar = dynamic_cast<CommandBar*>(control.get()); bar && bar->visible()) {
                if (const auto* style = bar->effective_control_style_values(StylePart::separator))
                    for (std::size_t i = 1; i < bar->owned_children().size(); ++i) {
                        const auto bounds = bar->separator_bounds(i);
                        if (bounds.width > 0 && bounds.height > 0)
                            drawing().styled_surface(bounds, palette, *style, palette.border, palette.border, 0, {});
                    }
            }
            if (auto expander = std::dynamic_pointer_cast<Expander>(control); expander && expander->has_control_styling()) {
                if (const auto* style = expander->effective_control_style_values(StylePart::root))
                    drawing().styled_surface(expander->bounds(), palette, *style, D2D1::ColorF(0, 0.0f), palette.border, 0, {});
                if (expander->body_presented() && palette.style != VisualStyle::winui)
                    if (const auto* style = expander->effective_control_style_values(StylePart::content))
                        drawing().styled_surface(expander->content_surface_bounds(), palette, *style, palette.surface, palette.border, 0, {});
            }
            if (auto expander = std::dynamic_pointer_cast<Expander>(control);
                expander && expander->body_presented() && palette.style == VisualStyle::winui) {
                const PartStyleValues empty;
                const auto* style = expander->effective_control_style_values(StylePart::content);
                const auto brushes = winui_card_brushes(palette.mode);
                drawing().styled_surface(expander->content_surface_bounds(), palette, style ? *style : empty,
                    palette.high_contrast ? palette.surface : argb_color(brushes.secondary_fill),
                    palette.high_contrast ? palette.border : argb_color(brushes.stroke),
                    4, {1, 0, 1, 1}, Drawing::SurfaceCorners::bottom);
            }
            const auto* expander = dynamic_cast<Expander*>(control.get());
            if (paint_peer && peer) {
                drawing().pop_clip();
                paint_peer(peer);
                drawing().push_clip(control->bounds());
            }
            if (expander) drawing().push_clip(expander->content_bounds());
            for (const auto& child : control->owned_children()) paint_content_surface(child, paint_peer, owner, adaptive);
            if (paint_peer && peer && clear_adornment(*peer))
                if (auto* clear = find_peer(clear_adornment(*peer).get())) paint_peer(clear);
            if (expander) drawing().pop_clip();
            drawing().pop_clip();
        }
    }
    void update_paint_bounds() {
        for (const auto& peer : peers) {
            if (text_input_peer(*peer)) continue;
            RECT pixels{};
            win32_require(GetWindowRect(peer->window, &pixels) != 0, "Read control paint bounds");
            MapWindowPoints(nullptr, window, reinterpret_cast<POINT*>(&pixels), 2);
            peer->paint_bounds = {pixels.left * 96.0f / dpi, pixels.top * 96.0f / dpi,
                (pixels.right - pixels.left) * 96.0f / dpi, (pixels.bottom - pixels.top) * 96.0f / dpi};
        }
    }
    void paint() {
        if (painting) { ValidateRect(window, nullptr); return; }
        painting = true;
        struct PaintReset { bool& value; ~PaintReset() { value = false; } } painting_reset{painting};
        std::vector<std::pair<PopupEntry, std::uint64_t>> popup_frames;
        for (const auto& entry : popups) popup_frames.emplace_back(entry, entry.popup->generation());
        sync_images();
        PAINTSTRUCT paint{};
        BeginPaint(window, &paint);
        bool redraw{};
        bool split_middle{};
        bool reveal_middle{};
        try {
            // A composed frame can cover native descendants of transparent viewports.
            // Balance the native caret's XOR before replacing pixels beneath it.
            struct CaretPresentation {
                HWND owner{};
                explicit CaretPresentation(HWND host) {
                    GUITHREADINFO info{sizeof(info)};
                    win32_require(GetGUIThreadInfo(GetCurrentThreadId(), &info) != FALSE, "Read native caret owner");
                    if (info.hwndCaret && IsChild(host, info.hwndCaret)) {
                        win32_require(HideCaret(info.hwndCaret) != FALSE, "Suspend native caret presentation");
                        owner = info.hwndCaret;
                    }
                }
                ~CaretPresentation() {
                    if (owner && IsWindow(owner) && !ShowCaret(owner))
                        OutputDebugStringW(L"XUI: Cannot restore native caret presentation.\n");
                }
            } caret(window);
            if (options.composition) hit_shapes.clear();
            const bool begun = options.composition ?
                drawing().begin_composition(window, static_cast<float>(dpi),
                    options.transparent && !palette.high_contrast ? D2D1::ColorF(0, 0.0f) : palette.background) :
                drawing().begin(window, static_cast<float>(dpi),
                    (layered() && !palette.high_contrast) || backdrop_presented ? D2D1::ColorF(0, 0.0f) : palette.background,
                    {}, layered(), backdrop_presented);
            if (begun) {
                Peer* external_focus{};
                if (palette.style == VisualStyle::winui && keyboard_focus_visible)
                    for (const auto& peer : peers)
                        if (peer->control->focused() && enabled(*peer) && visible(*peer) &&
                            !is_caption_button(*peer->control) && !is_clear_adornment(*peer)) {
                            const auto* toggle = dynamic_cast<const Toggle*>(peer->control.get());
                            const auto* radios = dynamic_cast<const RadioGroup*>(peer->control.get());
                            const auto* combo = dynamic_cast<const ComboBox*>(peer->control.get());
                            if (dynamic_cast<Button*>(peer->control.get()) || dynamic_cast<Expander*>(peer->control.get()) ||
                                dynamic_cast<RangeInput*>(peer->control.get()) || toggle ||
                                (combo && !palette.high_contrast && !combo->editor() && !combo->popup()->is_open()) ||
                                (radios && (radios->role() == ControlRole::radio_group ||
                                    (radios->horizontal_presentation() && !palette.high_contrast)))) {
                                external_focus = peer.get();
                                break;
                            }
                        }
                const bool combo_focus = external_focus && dynamic_cast<const ComboBox*>(external_focus->control.get());
                enum class PeerPaint { content, focus, focus_background };
                const auto paint_peer = [&](Peer* peer, PeerPaint pass = PeerPaint::content) {
                    if (!visible(*peer)) return;
                    if (const auto* split = dynamic_cast<SplitView*>(peer->control.get()))
                        split_middle |= split->progress() > 0 && split->progress() < 1;
                    if (const auto* reveal = dynamic_cast<Reveal*>(peer->control.get()))
                        reveal_middle |= reveal->progress() > 0 && reveal->progress() < 1;
                    // Preserve focus rings and slider thumbs outside the control's own bounds.
                    const auto paint_clip = clipped_bounds(*peer, 16);
                    if (paint_clip.width <= 0 || paint_clip.height <= 0) return;
                    for (auto* parent = peer->parent; parent; parent = parent->parent)
                        drawing().push_clip(viewport(*parent));
                    std::size_t host_clips{};
                    if (!palette.high_contrast) for (auto* parent = peer->parent; parent; parent = parent->parent) {
                        if (!dynamic_cast<RuntimeHost*>(parent->control.get()) && !dynamic_cast<VectorCanvas*>(parent->control.get())) continue;
                        if (const auto* style = parent->control->effective_control_style_values(StylePart::root))
                            host_clips += drawing().push_rounded_clip(parent->control->bounds(),
                                style->corner_radius.value_or(palette.style == VisualStyle::winui ? 4.0f : 0.0f));
                    }
                    if (const auto* combo = pass == PeerPaint::content ? nullptr : dynamic_cast<const ComboBox*>(peer->control.get())) {
                        auto bounds = combo->field_bounds();
                        bounds.x += peer->paint_bounds.x - combo->bounds().x;
                        bounds.y += peer->paint_bounds.y - combo->bounds().y;
                        if (pass == PeerPaint::focus_background) drawing().winui_combo_focus_background(bounds, palette);
                        else drawing().winui_combo_focus_marker(bounds, palette);
                    }
                    else if (pass == PeerPaint::focus) {
                        if (const auto* button = dynamic_cast<const Button*>(peer->control.get()))
                            drawing().winui_focus_ring(peer->paint_bounds, palette,
                                button->surface_style_values().corner_radius.value_or(4.0f));
                        else if (const auto* radios = dynamic_cast<const RadioGroup*>(peer->control.get())) {
                            for (std::size_t i = 0; i < radios->items().size(); ++i) {
                                const auto& item = radios->items()[i];
                                if (radios->selected() != item.id || !item.enabled) continue;
                                auto bounds = radios->item_bounds(i);
                                const bool hovered = basic_peer(*peer)->hovered_choice == i;
                                const auto style = radios->item_style_values(StylePart::item, i,
                                    hovered, hovered && basic_peer(*peer)->pressed_choice == item.id);
                                bounds.x += peer->paint_bounds.x;
                                bounds.y += peer->paint_bounds.y;
                                const bool horizontal = radios->horizontal_presentation();
                                drawing().winui_focus_ring(bounds, palette, style.corner_radius.value_or(4.0f), horizontal ? 2.0f : 7.0f,
                                    Drawing::SurfaceCorners::all, horizontal ? 2.0f : 3.0f);
                                break;
                            }
                        }
                        else if (const auto* toggle = dynamic_cast<const Toggle*>(peer->control.get()))
                            drawing().winui_toggle_focus(*toggle, peer->paint_bounds, palette, basic_peer(*peer)->text_layout.Get());
                        else if (const auto* expander = dynamic_cast<const Expander*>(peer->control.get())) {
                            auto bounds = expander->header_bounds();
                            bounds.x += peer->paint_bounds.x - expander->bounds().x;
                            bounds.y += peer->paint_bounds.y - expander->bounds().y;
                            const auto* header = expander->effective_control_style_values(StylePart::header);
                            drawing().winui_focus_ring(bounds, palette, header ? header->corner_radius.value_or(4.0f) : 4.0f, 3,
                                expander->expanded() ? Drawing::SurfaceCorners::top : Drawing::SurfaceCorners::all);
                        }
                        else if (const auto* range = dynamic_cast<const RangeInput*>(peer->control.get()))
                            drawing().winui_focus_ring(layout_style::content(*range, peer->paint_bounds), palette, 4, 7,
                                Drawing::SurfaceCorners::all, 0);
                    }
                    else if (peer->native_field()) {
                        peer->paint({drawing(), palette, enabled(*peer), keyboard_focus_visible && peer->control->focused()});
                    }
                    else {
                        const auto bounds = peer->paint_bounds;
                        auto clip = bounds;
                        if (const auto* range = dynamic_cast<const RangeInput*>(peer->control.get());
                            range && palette.style == VisualStyle::winui) {
                            const auto* thumb = range->effective_control_style_values(StylePart::thumb);
                            if (!thumb || !thumb->size) {
                                const bool vertical = range->orientation() == Axis::vertical;
                                const float x = vertical ? 0 : winui_slider_thumb_outset;
                                const float y = vertical ? winui_slider_thumb_outset : 0;
                                clip = {bounds.x - x, bounds.y - y, bounds.width + 2 * x, bounds.height + 2 * y};
                            }
                        }
                        drawing().push_clip(clip);
                        drawing().origin(bounds.x, bounds.y);
                        paint_control(*peer, peer != external_focus);
                        drawing().origin(0, 0);
                        drawing().pop_clip();
                    }
                    while (host_clips) { drawing().pop_rounded_clip(); --host_clips; }
                    for (auto* parent = peer->parent; parent; parent = parent->parent) drawing().pop_clip();
                };
                std::vector<Drawing::NativeWindow> native;
                for (const auto& peer : peers) if (peer->native() && onscreen(*peer) && !in_popup_surface(*peer)) {
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
                    if (IsWindowVisible(peer->window)) native.push_back({peer->window, clip});
                }
                // Present native and custom pixels together. A transparent viewport
                // does not exclude its opaque descendants from the root presentation.
                const auto capture_native = [&](std::span<const Drawing::NativeWindow> native) {
                    // Compose the complete native pixels beneath translucent popup shadows.
                    struct CaptureScope {
                        bool& value;
                        explicit CaptureScope(bool& flag) : value(flag) { value = true; }
                        ~CaptureScope() { value = false; }
                    } capture(composing_native);
                    // Capture complete EDIT pixels without presenting them above later siblings.
                    drawing().push_clip({0, 0, 0, 0});
                    const auto captured = drawing().native_windows(native);
                    if (captured) drawing().pop_clip();
                    return captured;
                };
                const auto composed = capture_native(native);
                const SubtreePaint ordered_peer = [&](Peer* peer) {
                    if (combo_focus && peer == external_focus) paint_peer(peer, PeerPaint::focus_background);
                    paint_peer(peer);
                    if (peer->native() && onscreen(*peer)) {
                        const HWND hwnd = peer->window;
                        drawing().present_native(std::span<const HWND>(&hwnd, 1));
                    }
                    if (peer == external_focus) paint_peer(peer, PeerPaint::focus);
                };
                if (composed) paint_surfaces(*root, ordered_peer);
                const auto paint_adaptive = [&](std::uint64_t owner) {
                    for (const auto* layout : adaptive_layouts) if (layout->overlay_active() && adaptive_owner(layout) == owner) {
                        Peer* representative{};
                        for (const auto& peer : peers) if (peer->adaptive == layout) { representative = peer.get(); break; }
                        if (!representative) continue;
                        for (auto* parent = representative->parent; parent; parent = parent->parent) drawing().push_clip(viewport(*parent));
                        drawing().fill(layout->navigation()->bounds(), palette.surface);
                        paint_content_surface(layout->navigation(), ordered_peer, owner, layout);
                        for (auto* parent = representative->parent; parent; parent = parent->parent) drawing().pop_clip();
                    }
                };
                if (composed) paint_adaptive(0);
                if (composed) paint_content_highlights();
                const auto paint_popup = [&](const PopupEntry& entry, bool native_surface) {
                    const auto bounds = entry.popup->bounds();
                    if (!native_surface && entry.dialog && palette.style == VisualStyle::winui && !palette.high_contrast) {
                        drawing().fill(root->bounds(), D2D1::ColorF(0, 0.25f));
                        // A modal dim on a transparent card also blocks input to the windows beneath it.
                        add_hit_shape(root->bounds(), 0);
                    }
                    const float inset = 1;
                    const Rect frame{bounds.x + inset, bounds.y + inset,
                        std::max(0.0f, bounds.width - 2 * inset), std::max(0.0f, bounds.height - 2 * inset)};
                    if (!native_surface && (entry.commands || palette.style == VisualStyle::winui) && !palette.high_contrast) {
                        for (float spread = command_shadow_extent; spread > 0; --spread)
                            drawing().rounded({frame.x - spread, frame.y - spread * 0.6f,
                                frame.width + 2 * spread, frame.height + 1.6f * spread},
                                D2D1::ColorF(0, 0.025f * (1 - spread / (command_shadow_extent + 1))), 8 + spread);
                    }
                    const auto popup_background = entry.popup->window_background() ? palette.background : palette.surface;
                    const float radius = entry.commands || palette.style == VisualStyle::winui ? 8.0f : 4.0f;
                    const auto* popup_style = entry.popup->effective_control_style_values(StylePart::root);
                    if (popup_style) drawing().styled_surface(bounds, palette, *popup_style, popup_background, palette.border, radius, {1, 1, 1, 1});
                    else if (!entry.commands && palette.style == VisualStyle::classic) drawing().fill(bounds, popup_background);
                    else if (entry.commands || palette.style == VisualStyle::winui)
                        drawing().rounded(frame, entry.commands ? palette.surface : popup_background, radius);
                    if (entry.dialog && palette.style == VisualStyle::winui) {
                        const auto footer = entry.dialog->footer_bounds();
                        drawing().push_clip(footer);
                        if (const auto* style = entry.dialog->footer()->effective_control_style_values(StylePart::root))
                            drawing().styled_surface(footer, palette, *style, palette.background, palette.border, 0, {});
                        else drawing().rounded(frame, palette.background, radius);
                        drawing().pop_clip();
                        const auto* separator = entry.dialog->footer()->effective_control_style_values(StylePart::separator);
                        const auto color = separator && separator->background && !palette.high_contrast ?
                            D2D1::ColorF(separator->background->resolve(palette.mode)) : palette.border;
                        const float thickness = std::min(separator && separator->thickness ? *separator->thickness : 1,
                            footer.height + 1);
                        const float separator_y = footer.y + std::max(0.0f, thickness - 1) - thickness / 2;
                        if (thickness > 0) drawing().line(frame.x + 1, separator_y, frame.x + frame.width - 1, separator_y,
                            color, thickness);
                    }
                    paint_content_surface(entry.popup, ordered_peer, entry.popup->id());
                    paint_adaptive(entry.popup->id());
                    if (!popup_style) drawing().rounded(frame, entry.commands && palette.style == VisualStyle::classic ?
                        palette.secondary : palette.border, radius, true);
                };
                // Over an active swap chain panel, popups and tooltips need a composition layer above
                // the panel visuals: finish the window content here and continue in the overlay layer.
                const bool overlay_layer = composed && options.composition && presenter && active_panel() &&
                    (!popup_frames.empty() || tooltip_shown);
                struct OverlayScope {
                    Impl& host;
                    Drawing* previous;
                    bool active{};
                    ~OverlayScope() { if (active) host.paint_drawing = previous; }
                } overlay_scope{*this, paint_drawing};
                bool overlay_began{};
                if (overlay_layer) {
                    redraw = !drawing().end();
                    if (!redraw) {
                        present_composition();
                        if (!composition_overlay_ready) {
                            composition_overlay.initialize(options.visual_style);
                            composition_overlay_ready = true;
                        }
                        composition_overlay.set_visual_style(options.visual_style);
                        paint_drawing = &composition_overlay;
                        overlay_scope.active = true;
                        overlay_began = drawing().begin_composition(window, static_cast<float>(dpi), D2D1::ColorF(0, 0.0f));
                    }
                } else if (presenter && presenter->overlay_visible()) {
                    presenter->overlay(nullptr, 0, false);
                }
                const bool paint_overlays = composed && (!overlay_layer || overlay_began);
                if (paint_overlays) for (const auto& [entry, generation] : popup_frames) {
                    if (closing || !entry.popup->current(generation)) continue;
                    const auto* peer = find_peer(entry.popup.get());
                    if (peer && !peer->overlay_surface()) paint_popup(entry, false);
                }
                if (paint_overlays && tooltip_shown) {
                    for (const auto& peer : peers) if (peer->control->id() == tooltip_target) {
                        const bool navigation_card = dynamic_cast<NavigationList*>(peer->control.get()) != nullptr;
                        if (tooltip_styling || navigation_card) {
                            const auto* face = tooltip_styling ? tooltip_styling->effective(StylePart::root, style_states::open) : nullptr;
                            const auto* text = tooltip_styling ? tooltip_styling->effective(StylePart::text, style_states::open) : nullptr;
                            drawing().styled_surface(tooltip_bounds, palette, face ? *face : PartStyleValues{},
                                palette.surface, palette.border, 6, {1, 1, 1, 1});
                            const auto padding = face && face->padding ? *face->padding :
                                navigation_card ? Insets{12, 12, 12, 12} : Insets{10, 2, 10, 2};
                            const auto border = face && face->border_thickness ? *face->border_thickness : Insets{1, 1, 1, 1};
                            const Rect area{tooltip_bounds.x + padding.left + border.left, tooltip_bounds.y + padding.top + border.top,
                                std::max(0.0f, tooltip_bounds.width - padding.left - padding.right - border.left - border.right),
                                std::max(0.0f, tooltip_bounds.height - padding.top - padding.bottom - border.top - border.bottom)};
                            const auto ink = text && text->foreground && !palette.high_contrast ?
                                D2D1::ColorF(text->foreground->resolve(palette.mode)) : palette.text;
                            drawing().push_clip(tooltip_bounds);
                            auto typography = text ? *text : PartStyleValues{};
                            typography.wrapping = true;
                            drawing().styled_text(peer->control->help_text(), area, ink, typography, TextStyle::caption);
                            drawing().pop_clip();
                        } else {
                            drawing().surface_frame(tooltip_bounds, palette);
                            drawing().text(peer->control->help_text(), {tooltip_bounds.x + 10, tooltip_bounds.y + 2,
                                std::max(0.0f, tooltip_bounds.width - 20), tooltip_bounds.height - 4}, palette.text, true);
                        }
                    }
                }
                if (overlay_layer) {
                    if (overlay_began) {
                        const bool overlay_lost = !drawing().end();
                        redraw = redraw || overlay_lost;
                        if (!overlay_lost)
                            presenter->overlay(composition_overlay.composition_swap_chain(),
                                composition_overlay.composition_generation(), true);
                        apply_hit_region();
                    }
                    paint_drawing = overlay_scope.previous;
                    overlay_scope.active = false;
                } else {
                    redraw = !composed || !drawing().end();
                    if (!redraw && options.composition && presenter) present_composition();
                }
                if (composed && !closing) for (const auto& [entry, generation] : popup_frames) {
                    auto* surface = find_peer(entry.popup.get());
                    if (closing || !surface || !surface->overlay_surface() || !entry.popup->current(generation)) continue;
                    struct DrawingScope {
                        Impl& host;
                        Drawing* previous;
                        DrawingScope(Impl& value, Drawing& next) : host(value), previous(value.paint_drawing) { host.paint_drawing = &next; }
                        ~DrawingScope() { host.paint_drawing = previous; }
                    } selection(*this, *surface->overlay_surface());
                    drawing().set_visual_style(options.visual_style);
                    const auto bounds = surface->paint_bounds;
                    if (!drawing().begin(surface->window, static_cast<float>(dpi),
                        entry.popup->window_background() ? palette.background : palette.surface, {-bounds.x, -bounds.y})) continue;
                    std::vector<Drawing::NativeWindow> popup_native;
                    for (const auto& peer : peers) if (peer->native() && onscreen(*peer) &&
                        popup_owner(peer.get()) == entry.popup->id()) {
                        auto clip = clipped_bounds(*peer);
                        const float scale = dpi / 96.0f;
                        RECT pixels{static_cast<LONG>(std::lround(clip.x * scale)), static_cast<LONG>(std::lround(clip.y * scale)),
                            static_cast<LONG>(std::lround((clip.x + clip.width) * scale)), static_cast<LONG>(std::lround((clip.y + clip.height) * scale))};
                        MapWindowPoints(window, surface->window, reinterpret_cast<POINT*>(&pixels), 2);
                        popup_native.push_back({peer->window, pixels});
                    }
                    const auto captured = capture_native(popup_native);
                    if (captured) {
                        if (!closing && entry.popup->current(generation)) paint_popup(entry, true);
                        redraw = !drawing().end() || redraw;
                    } else redraw = true;
                }
                ++paints;
                if (!redraw && split_middle) ++split_intermediate_paints;
                if (!redraw && reveal_middle) ++reveal_intermediate_paints;
                // Transparent custom HWNDs retain input and UIA, but not targets.
                // WS_CLIPCHILDREN protects opaque native EDIT pixels.
                for (const auto& peer : peers) {
                    if (!peer->native()) ValidateRect(peer->window, nullptr);
                    if (peer->caption_window()) ValidateRect(peer->caption_window(), nullptr);
                }
            }
        } catch (...) { EndPaint(window, &paint); throw; }
        EndPaint(window, &paint);
        painting = false;
        if (std::exchange(destroy_after_paint, false)) { destroy(); return; }
        if (redraw) invalidate(Invalidation::paint);
    }
    bool sync_images(bool window_shown = true) {
        bool changed{};
        std::vector<std::uint64_t> retained;
        std::vector<RowImageWork> work;
        for (const auto& peer : peers) {
            if (!peer->image_sync_required()) continue;
            Rect rect = peer->control->bounds();
            const auto intersect = [&](Rect clip) {
                const auto right = std::min(rect.x + rect.width, clip.x + clip.width);
                const auto bottom = std::min(rect.y + rect.height, clip.y + clip.height);
                rect.x = std::max(rect.x, clip.x); rect.y = std::max(rect.y, clip.y);
                rect.width = std::max(0.0f, right - rect.x);
                rect.height = std::max(0.0f, bottom - rect.y);
            };
            intersect(root->bounds());
            for (auto* parent = peer->parent; parent; parent = parent->parent) intersect(viewport(*parent));
            const bool shown = window_shown && IsWindowVisible(window) && !IsIconic(window) &&
                visible(*peer) && rect.width > 0 && rect.height > 0;
            if (auto* image = peer->image_peer()) {
                image->sync(shown, wake);
                if (image->pixels) retained.push_back(image->pixels->id);
            } else {
                RowImageWork item{peer.get(), rect, shown};
                if (peer->image_work(item)) work.push_back(std::move(item));
            }
        }
        std::size_t remaining = 48;
        // Preserve hidden cancellation before visible admission across all families.
        for (const bool shown : {false, true}) for (auto& item : work) {
            if (item.shown != shown) continue;
            auto* peer = item.peer;
            changed = peer->sync_images(std::move(item), dpi, wake, retained, remaining) || changed;
            has_images = has_images || peer->metric(PeerMetric::retained_images) != 0;
        }
        drawing().keep_images(retained);
        if (composition_overlay_ready) composition_overlay.keep_images(retained);
        for (const auto& peer : peers) if (peer->overlay_surface()) peer->overlay_surface()->keep_images(retained);
        return changed;
    }
    void deliver_images() {
        if (!ready || closing) return;
        window_icon.deliver(window);
        if (!ready || closing || layout_pending) return;
        bool changed{};
        for (const auto& peer : peers) if (peer->image_peer()) changed = peer->image_peer()->deliver() || changed;
        changed = sync_images() || changed;
        if (changed) invalidate(Invalidation::paint);
    }
    void paint_control(Peer& peer, bool include_focus = true) {
        const bool fluent = palette.style == VisualStyle::winui;
        peer.paint({drawing(), palette, enabled(peer),
            include_focus && peer.control->focused() && (!fluent || keyboard_focus_visible)});
    }
    void activated(Peer& peer, bool invoked) {
        if (invoked && peer.control->role() == ControlRole::combo_box) open_combo(peer);
        update();
        if (invoked && window && peer.control->role() == ControlRole::button && !dynamic_cast<MenuBar::Heading*>(peer.control.get()))
            peer.publish_invoked();
    }
    bool focus(Peer& peer, bool activate_window) {
        if (!ready || closing || !peer.control->focusable() || !enabled(peer)) return false;
        if (!in_top_popup(peer)) return false;
        if (const auto split = dynamic_cast<SplitView*>(peer.control.get()); split && split->divider().width <= 0) return false;
        if (peer.control->bounds().width <= 0 || peer.control->bounds().height <= 0) return false;
        for (auto* parent = peer.parent; parent; parent = parent->parent) {
            const auto view = viewport(*parent);
            if ((view.width <= 0 || view.height <= 0) && !opening_reveal(*parent)) return false;
        }
        reveal(peer);
        EnableWindow(peer.window, TRUE);
        if (activate_window) {
            if (IsIconic(window)) ShowWindow(window, SW_RESTORE);
            SetForegroundWindow(window);
        }
        SetFocus(peer.window);
        if (window && !closing && peer.runtime_host() && peer.runtime_host()->active())
            if (auto* web = dynamic_cast<WebContent*>(peer.control.get())) web->focus_content();
        return GetFocus() == peer.window || (native_document_peer(peer) && IsChild(peer.window, GetFocus())) ||
            (peer.runtime_host() && peer.runtime_host()->contains_native(GetFocus()));
    }
    Peer* edit_at(HWND source, LPARAM position) const {
        POINT point{GET_X_LPARAM(position), GET_Y_LPARAM(position)};
        MapWindowPoints(source, window, &point, 1);
        const float x = point.x * 96.0f / dpi, y = point.y * 96.0f / dpi;
        for (auto it = peers.rbegin(); it != peers.rend(); ++it) {
            auto& peer = **it;
            if (!text_input_peer(peer) || (GetParent(peer.window) != source && peer.window != source) ||
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
        for (auto& [id, state] : content_picking) {
            state.mouse_down = state.pointer_down = false;
            state.pending.reset();
        }
        if (window && std::any_of(content_picking.begin(), content_picking.end(),
            [](const auto& item) { return item.second.highlight.has_value(); })) InvalidateRect(window, nullptr, FALSE);
        hide_tooltip();
        std::vector<Peer*> snapshot;
        for (const auto& peer : peers) snapshot.push_back(peer.get());
        for (auto* peer : snapshot) {
            peer->cancel_input();
            peer->control->pointer_move(false);
            if (GetCapture() == peer->window) ReleaseCapture();
        }
    }
    bool miller_wheel(Peer& peer, double delta) {
        for (auto* ancestor = &peer; ancestor; ancestor = ancestor->parent)
            if (auto* columns = dynamic_cast<MillerColumns*>(ancestor->control.get())) {
                if (enabled(peer) && visible(peer)) columns->scroll_horizontal(delta);
                return true;
            }
        return false;
    }
    LRESULT control_message(Peer& peer, HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
        auto& control = *peer.control;
        switch (message) {
        case WM_COMMAND:
        case WM_NOTIFY:
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLOREDIT:
        case WM_NEXTDLGCTL:
            return SendMessageW(window, message, wparam, lparam);
        case WM_MOUSEWHEEL:
            hide_tooltip();
            if ((GET_KEYSTATE_WPARAM(wparam) & MK_SHIFT) &&
                miller_wheel(peer, -GET_WHEEL_DELTA_WPARAM(wparam) * 0.8)) return 0;
            break;
        case WM_MOUSEHWHEEL:
            hide_tooltip();
            if (miller_wheel(peer, GET_WHEEL_DELTA_WPARAM(wparam) * 0.8)) return 0;
            break;
        case WM_ERASEBKGND: return 1;
        case WM_PAINT:
            ValidateRect(hwnd, nullptr);
            if (peer.overlay_surface() && !painting) invalidate(Invalidation::paint);
            return 0;
        case WM_GETOBJECT:
            if (static_cast<LONG>(lparam) == UiaRootObjectId && peer.provider)
                return UiaReturnRawElementProvider(hwnd, wparam, lparam, peer.provider);
            break;
        case WM_SETFOCUS:
            menu_bar_focused(peer, reinterpret_cast<HWND>(wparam));
            last_focus = hwnd;
            reveal(peer);
            control.set_focused(true);
            return 0;
        case WM_KILLFOCUS:
            peer.input(message, wparam, lparam, {dpi, palette});
            control.set_focused(false);
            if (GetCapture() == hwnd) ReleaseCapture();
            return 0;
        case WM_MOUSEMOVE:
            if (menu_bar_hover(peer, row_pointer_inside(hwnd, lparam))) return 0;
            break;
        case WM_LBUTTONDOWN:
            peer.cancel_wheel();
            if (!enabled(peer) || !visible(peer)) return 0;
            if (peer.suppress_popup_click) { SetFocus(hwnd); return 0; }
            hide_tooltip();
            if (focus_edit_at(hwnd, lparam)) return 0;
            if (peer.parent)
                if (auto* columns = dynamic_cast<MillerColumns*>(peer.parent->control.get());
                    columns && columns->focus_pointer(control,
                        {GET_X_LPARAM(lparam) * 96.0f / dpi, GET_Y_LPARAM(lparam) * 96.0f / dpi})) return 0;
            break;
        case WM_KEYDOWN:
            if (!enabled(peer) || !visible(peer)) return 0;
            if (menu_bar_key(peer, wparam, lparam)) return 0;
            hide_tooltip();
            if (peer.parent && (wparam == VK_LEFT || wparam == VK_RIGHT))
                if (auto* breadcrumb = dynamic_cast<Breadcrumb*>(peer.parent->control.get())) {
                    if (auto* next = breadcrumb->adjacent(control, wparam == VK_LEFT ? -1 : 1))
                        if (auto* target = find_peer(next)) focus(*target, false);
                    return 0;
                }
            if (wparam == VK_ESCAPE && !popups.empty()) {
                auto popup = popups.back().popup;
                dismiss_popup(*popup, PopupDismissReason::cancel);
                return 0;
            }
            if (wparam == VK_ESCAPE && peer.adaptive && peer.adaptive->overlay_active()) {
                peer.adaptive->set_navigation_open(false);
                return 0;
            }
            break;
        case WM_KEYUP:
            if (const auto result = peer.input(message, wparam, lparam, {dpi, palette})) return *result;
            if (wparam == VK_SPACE || wparam == VK_RETURN) {
                activated(peer, control.key_up(wparam == VK_SPACE ? ActivationKey::space : ActivationKey::enter));
                return 0;
            }
            break;
        case WM_CHAR:
            if (wparam == VK_SPACE || wparam == VK_RETURN) return 0;
            break;
        case control_action_message:
            if (wparam != control.id()) return UIA_E_ELEMENTNOTAVAILABLE;
            if (lparam == 6) { reveal(peer); return S_OK; }
            if (!enabled(peer) || !visible(peer)) return UIA_E_ELEMENTNOTENABLED;
            if (lparam == 1) return focus(peer, true) ? S_OK : UIA_E_INVALIDOPERATION;
            if (const auto result = peer.input(message, wparam, lparam, {dpi, palette})) return *result;
            return UIA_E_INVALIDOPERATION;
        case foundation_action_message:
            if (const auto result = peer.input(message, wparam, lparam, {dpi, palette})) return *result;
            return UIA_E_INVALIDOPERATION;
        case WM_NCDESTROY:
            peer.input(message, wparam, lparam, {dpi, palette});
            control.cancel();
            control.set_focused(false);
            peer.disconnect_accessibility();
            UiaReturnRawElementProvider(hwnd, 0, 0, nullptr);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            peer.window = nullptr;
            return DefWindowProcW(hwnd, message, wparam, lparam);
        }
        if (const auto result = peer.input(message, wparam, lparam, {dpi, palette})) return *result;
        if (message == WM_MOUSEWHEEL && scroll_wheel(peer, wparam)) return 0;
        if (message == WM_KEYDOWN) {
            if (wparam == VK_ESCAPE) {
                control.cancel();
                if (GetCapture() == hwnd) ReleaseCapture();
                return 0;
            }
            if (wparam == VK_SPACE || wparam == VK_RETURN) {
                const auto accepted = control.key_down(wparam == VK_SPACE ? ActivationKey::space : ActivationKey::enter,
                    (lparam & (1LL << 30)) != 0);
                activated(peer, accepted && wparam == VK_RETURN);
                return 0;
            }
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
        if (auto result = inspect_pointer(hwnd, message, wparam, lparam)) return *result;
        if (frameless() && message == WM_NCCALCSIZE) return 0;
        if (frameless() && message == WM_NCHITTEST) return window && drag_hit(lparam) ? HTCAPTION : HTCLIENT;
        if (titlebar && message == WM_NCCALCSIZE && wparam) {
            auto& params = *reinterpret_cast<NCCALCSIZE_PARAMS*>(lparam);
            const auto top = params.rgrc[0].top;
            DefWindowProcW(hwnd, message, wparam, lparam);
            params.rgrc[0].top = top + (IsZoomed(hwnd) ? GetSystemMetricsForDpi(SM_CYSIZEFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi) : 0);
            return 0;
        }
        if (titlebar && message == WM_NCHITTEST) return caption_hit(hwnd, lparam);
        if (message == WM_NCHITTEST && drag_hit(lparam)) return HTCAPTION;
        if (auto result = navigation_message(hwnd, message, wparam, lparam)) return *result;
        switch (message) {
        case WM_ENTERSIZEMOVE:
            if (tab_move) tab_move->entered = true;
            break;
        case WM_EXITSIZEMOVE:
            if (tab_move) tab_move->ending = true;
            break;
        case WM_WINDOWPOSCHANGING:
            if (tab_move && tab_move->full_drag) position_tab_window(*reinterpret_cast<WINDOWPOS*>(lparam));
            break;
        case WM_MOVING:
            if (tab_move) {
                if (!tab_move->full_drag && !tab_move->ending)
                    move_tab_window(*reinterpret_cast<RECT*>(lparam), false);
                return TRUE;
            }
            break;
        case WM_TIMER:
            if (wparam == animation_timer) {
                if (animation_timer_running && ready) {
                    ++animation_tick_count;
                    animation_frame_pending = true;
                    sync_animations(false, true);
                }
                return 0;
            }
            if (wparam == progress_timer) {
                sync_progress_animation();
                if (progress_animating) {
                    const auto elapsed = std::chrono::duration<float>(std::chrono::steady_clock::now() - progress_epoch).count();
                    progress_phase = std::fmod(elapsed / 1.4f, 1.0f);
                    ++progress_frames;
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                return 0;
            }
            if (wparam == tooltip_timer) {
                KillTimer(hwnd, tooltip_timer);
                if (!IsWindowVisible(hwnd) || IsIconic(hwnd)) { hide_tooltip(); return 0; }
                for (const auto& peer : peers) if (peer->control->id() == tooltip_target && visible(*peer) && enabled(*peer)) {
                    if (!tooltip_current(*peer) || peer->control->help_text().empty()) break;
                    const auto now = std::chrono::steady_clock::now();
                    if (dynamic_cast<NavigationList*>(peer->control.get()) && now < tooltip_due) {
                        const auto remaining = std::chrono::ceil<std::chrono::milliseconds>(tooltip_due - now).count();
                        win32_require(SetTimer(hwnd, tooltip_timer, static_cast<UINT>(remaining), nullptr) != 0,
                            "Continue tooltip delay");
                        return 0;
                    }
                    const auto control = peer->control;
                    if (auto* list = dynamic_cast<NavigationList*>(control.get())) list->request_hover_help();
                    if (!window || closing || !tooltip_current(*peer) || !visible(*peer) || !enabled(*peer) ||
                        control->help_text().empty()) break;
                    place_tooltip(*peer);
                    tooltip_shown = true; invalidate(Invalidation::paint); return 0;
                }
                hide_tooltip(); return 0;
            }
            break;
        case WM_MOUSEWHEEL: {
            hide_tooltip();
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
        case WM_CANCELMODE:
            if (tab_move && tab_move->entered && !tab_move->ending) tab_move->cancelled = true;
            cancel_input(); return DefWindowProcW(hwnd, message, wparam, lparam);
        case WM_CAPTURECHANGED:
            if (tab_move && tab_move->entered && !tab_move->ending && lparam && reinterpret_cast<HWND>(lparam) != hwnd)
                tab_move->cancelled = true;
            break;
        case WM_ACTIVATE:
            caption_active = LOWORD(wparam) != WA_INACTIVE;
            if (!caption_active && tab_move && tab_move->entered && !tab_move->ending &&
                (!tab_move->joined || tab_move->joined->host->window != reinterpret_cast<HWND>(lparam)))
                tab_move->cancelled = true;
            if (titlebar) titlebar->set_active(caption_active);
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
                sync_animations(true);
                stop_progress_animation();
                cancel_input();
                if (!popups.empty()) { auto popup = popups.front().popup; dismiss_popup(*popup, PopupDismissReason::hidden, false); }
            }
            else invalidate(Invalidation::paint);
            break;
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: if (ready) paint(); else return DefWindowProcW(hwnd, message, wparam, lparam); return 0;
        case WM_SIZE:
            if (titlebar) titlebar->set_maximized(wparam == SIZE_MAXIMIZED);
            hide_tooltip();
            if (wparam == SIZE_MINIMIZED) {
                sync_animations(true);
                stop_progress_animation();
                cancel_input();
                if (!popups.empty()) { auto popup = popups.front().popup; dismiss_popup(*popup, PopupDismissReason::hidden, false); }
            }
            for (const auto& peer : peers) if (text_input_peer(*peer)) text_input_peer(*peer)->dismiss_suggestions();
            for (auto& task : samples) task->suspend(wparam == SIZE_MINIMIZED);
            if (wparam == SIZE_MINIMIZED) for (const auto& peer : peers) {
                if (peer->runtime_host()) peer->runtime_host()->suspend();
                if (peer->swap_chain_host()) peer->swap_chain_host()->suspend();
                if (auto* map = dynamic_cast<MapView*>(peer->control.get())) map->cancel_request();
            }
            if (wparam != SIZE_MINIMIZED) invalidate(Invalidation::layout);
            else if (ready) sync_images(false);
            return 0;
        case WM_SHOWWINDOW:
            if (!wparam && ready) sync_images(false);
            if (!wparam) {
                sync_animations(true);
                stop_progress_animation();
                cancel_input();
                if (!popups.empty()) { auto popup = popups.front().popup; dismiss_popup(*popup, PopupDismissReason::hidden, false); }
            }
            for (auto& task : samples) task->suspend(!wparam || IsIconic(hwnd));
            if (!wparam) for (const auto& peer : peers) {
                if (peer->runtime_host()) peer->runtime_host()->suspend();
                if (peer->swap_chain_host()) peer->swap_chain_host()->suspend();
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
            for (const auto& peer : peers) if (text_input_peer(*peer)) text_input_peer(*peer)->dismiss_suggestions();
            for (const auto& peer : peers) peer->native_moved();
            return 0;
        case update_message: if (pending) update(); return 0;
        case WM_DPICHANGED: {
            cancel_input();
            dpi = HIWORD(wparam);
            if (icon_configured) apply_icons(file_type_icons(icon_extension, icon_directory, dpi));
            else window_icon.refresh(window, dpi, wake);
            apply_theme();
            const auto& suggested = *reinterpret_cast<RECT*>(lparam);
            SetWindowPos(hwnd, nullptr, suggested.left, suggested.top,
                suggested.right - suggested.left, suggested.bottom - suggested.top, SWP_NOZORDER | SWP_NOACTIVATE);
            invalidate(Invalidation::layout);
            return 0;
        }
        case WM_SETTINGCHANGE:
        case WM_SYSCOLORCHANGE:
        case WM_DWMCOMPOSITIONCHANGED:
        case WM_THEMECHANGED: if (ready) apply_theme(); break;
        case WM_DISPLAYCHANGE:
            drawing().discard();
            invalidate(Invalidation::paint);
            return 0;
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLOREDIT: {
            const auto dc = reinterpret_cast<HDC>(wparam);
            const bool editable = message == WM_CTLCOLOREDIT ||
                (reinterpret_cast<HWND>(lparam) && GetDlgCtrlID(reinterpret_cast<HWND>(lparam)) >= 100);
            bool on_surface{};
            auto editable_fill = palette.field;
            auto native_ink = IsWindowEnabled(reinterpret_cast<HWND>(lparam)) ? palette.text : palette.disabled;
            bool styled_field{};
            for (const auto& peer : peers) {
                const auto* native = peer->native_field();
                if (!native) continue;
                const bool caption = peer->caption_window() == reinterpret_cast<HWND>(lparam);
                if (!caption && peer->window != reinterpret_cast<HWND>(lparam)) continue;
                const auto colors = native->native_colors(caption, palette);
                on_surface = colors.surface;
                native_ink = colors.ink;
                editable_fill = colors.fill;
                styled_field = colors.styled;
                break;
            }
            SetTextColor(dc, platform::native_color(native_ink));
            SetBkColor(dc, platform::native_color(editable ? editable_fill : on_surface ? palette.surface : palette.background));
            if (editable && (palette.style == VisualStyle::winui || styled_field)) {
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
                if (auto* field = peer->native_field(); field && peer->window == notification->hwndFrom)
                    return enabled(*peer) ? field->native_notify(*notification) : 0;
            break;
        }
        case WM_COMMAND:
            for (const auto& peer : peers) if (auto* field = peer->native_field();
                field && peer->window == reinterpret_cast<HWND>(lparam)) {
                if (HIWORD(wparam) == EN_CHANGE && enabled(*peer)) field->native_change();
                if (field->text_input()) {
                    if (HIWORD(wparam) == EN_SETFOCUS || HIWORD(wparam) == EN_KILLFOCUS)
                        peer->control->set_focused(HIWORD(wparam) == EN_SETFOCUS);
                    if (HIWORD(wparam) == EN_SETFOCUS) { last_focus = peer->window; reveal(*peer); }
                }
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
            if (wparam == 36) return static_cast<LRESULT>(reveal_intermediate_paints);
            if (wparam == 35) return static_cast<LRESULT>(split_intermediate_paints);
            if (wparam == 34) return static_cast<LRESULT>(animation_tick_count);
            if (wparam == 33) return animation_timer_running;
            if (wparam == 37) return progress_animating;
            if (wparam == 38) return static_cast<LRESULT>(progress_frames);
            if (wparam == 30) return static_cast<LRESULT>(drawing().native_buffer_bytes());
            if (wparam == 31) return static_cast<LRESULT>(drawing().native_bitmap_bytes());
            if (wparam == 32) return static_cast<LRESULT>(drawing().scene_paths());
            if (wparam == 39) return static_cast<LRESULT>(presenter ? presenter->backdrop_visuals() : 0);
            if (wparam == 40) return options.composition && presenter ? 1 : 0;
            if (wparam == 41) return static_cast<LRESULT>(root_drawing.composition_generation());
            if (wparam == 42) return static_cast<LRESULT>(presenter ? presenter->panel_visuals() : 0);
            if (wparam == 43) return presenter && presenter->overlay_visible() ? 1 : 0;
            if (wparam == 22 || wparam == 23) {
                std::size_t count{};
                for (const auto& peer : peers)
                    count += peer->metric(wparam == 22 ? PeerMetric::thumbnails : PeerMetric::ready_thumbnails);
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
                for (const auto& peer : peers) if (const auto* state = basic_peer(*peer); state && state->text_layout) ++count;
                return static_cast<LRESULT>(count);
            }
            if (wparam == 0) {
                return static_cast<LRESULT>(paints);
            }
            if (wparam == 1 || wparam == 8 || wparam == 9) {
                for (const auto& peer : peers) if (peer->family == HostFamily::file_list) {
                    const auto& list = static_cast<FileList&>(*peer->control);
                    return static_cast<LRESULT>(wparam == 1 ? peer->metric(PeerMetric::painted_rows) :
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
            stop_progress_animation();
            closing = true;
            sync_animations(true);
            close_posts();
            window_icon.set_source({}, window, dpi, wake);
            ImageResources::clear_unused();
            for (auto& task : samples) task->cancel();
            ready = false;
            drawing().discard();
            composition_overlay.discard();
            presenter.reset();
            for (auto& task : tasks) task->cancel();
            detach();
            if (!application_managed) {
                PostQuitMessage(failed ? 1 : 0);
                quit_posted = true;
            }
            return 0;
        case WM_NCDESTROY:
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            window = nullptr;
            if (application_managed) wake->signal();
            break;
        }
        return DefWindowProcW(hwnd, message, wparam, lparam);
    }
};

Window::Window(WindowOptions options) : impl_(std::make_shared<Impl>(std::move(options))) { impl_->public_window = this; }
Window::~Window() { impl_->public_window = nullptr; impl_->teardown(); }
void Window::on_tab_drag(std::function<bool(const TabDragEvent&)> callback) {
    if (GetCurrentThreadId() != impl_->owner_thread) throw std::logic_error("Configure tab dragging on the UI thread");
    if (impl_->closing) throw std::logic_error("The Window is closed");
    if (callback && (!impl_->application_managed || !impl_->titlebar))
        throw std::logic_error("Tab dragging requires an Application window with a custom title bar");
    if (impl_->tab_move) throw std::logic_error("Do not replace the tab drag handler during a gesture");
    impl_->tab_drag_handler = std::move(callback);
}
WindowPlacement Window::placement() const {
    if (GetCurrentThreadId() != impl_->owner_thread) throw std::logic_error("Read window placement on the UI thread");
    if (impl_->closing) throw std::logic_error("The Window is closed");
    if (impl_->tab_move && !impl_->tab_move->torn) return impl_->tab_move->placement;
    return impl_->read_placement();
}
void Window::set_placement(WindowPlacement placement) {
    const auto impl = impl_;
    if (GetCurrentThreadId() != impl->owner_thread) throw std::logic_error("Set window placement on the UI thread");
    if (impl->closing) throw std::logic_error("The Window is closed");
    if (placement.width <= 0 || placement.height <= 0 || placement.width > 65536 || placement.height > 65536 ||
        placement.x < -1000000 || placement.y < -1000000 || placement.x > 1000000 || placement.y > 1000000)
        throw std::invalid_argument("Window placement is outside the supported screen range");
    impl->initial_placement = placement;
    if (!impl->window) return;
    Impl::InputScope scope(*impl);
    if (IsZoomed(impl->window) || IsIconic(impl->window)) ShowWindow(impl->window, SW_SHOWNOACTIVATE);
    win32_require(SetWindowPos(impl->window, nullptr, placement.x, placement.y, placement.width, placement.height,
        SWP_NOZORDER | SWP_NOACTIVATE) != 0, "Set window bounds");
    if (placement.maximized) {
        WINDOWPLACEMENT value{sizeof(value)};
        win32_require(GetWindowPlacement(impl->window, &value) != 0, "Read window placement");
        value.showCmd = SW_SHOWMAXIMIZED;
        win32_require(SetWindowPlacement(impl->window, &value) != 0, "Maximize window");
    }
}
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
void Window::set_file_type_icon(std::wstring extension, bool directory) {
    if (GetCurrentThreadId() != impl_->owner_thread) throw std::logic_error("Set window icons on the UI thread");
    if (impl_->closing || (impl_->used && !impl_->ready)) throw std::logic_error("The Window is closed");
    if (extension.size() > 255 || (!extension.empty() && extension.front() != L'.') ||
        extension.find_first_of(L"\\/:*?\"<>|") != std::wstring::npos ||
        std::any_of(extension.begin(), extension.end(), [](wchar_t c) { return c < 32; }) ||
        (directory && !extension.empty()))
        throw std::invalid_argument("Use an extension beginning with a dot, or an empty stock-icon extension");
    auto icons = impl_->window ? file_type_icons(extension, directory, impl_->dpi) : nullptr;
    impl_->window_icon.set_source({}, impl_->window, impl_->dpi, impl_->wake);
    if (icons) impl_->apply_icons(std::move(icons));
    impl_->icon_extension = std::move(extension);
    impl_->icon_directory = directory;
    impl_->icon_configured = true;
}
void Window::set_icon_source(std::wstring path) {
    if (GetCurrentThreadId() != impl_->owner_thread) throw std::logic_error("Set window icon on its UI thread");
    if (impl_->closing || (impl_->used && !impl_->ready)) throw std::logic_error("The Window is closed");
    impl_->window_icon.set_source(std::move(path), impl_->window, impl_->dpi, impl_->wake);
    impl_->icon_configured = false;
    impl_->window_icons.reset();
}
void Window::on_icon_error(std::function<void(const std::wstring&)> callback) {
    if (GetCurrentThreadId() != impl_->owner_thread) throw std::logic_error("Set window icon callback on its UI thread");
    impl_->window_icon.on_error = std::move(callback);
}
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
void Window::replace_content(ContentHost& host, std::shared_ptr<Element> content) {
    replace_content(host, std::move(content), {}, {});
}
void Window::replace_content(ContentHost& host, std::shared_ptr<Element> content,
    std::vector<ContentInspectionTarget> targets, std::function<void(std::uint32_t)> picked) {
    const auto impl = impl_;
    if (GetCurrentThreadId() != impl->owner_thread)
        throw std::logic_error("Replace content on the window UI thread");
    if (impl->closing || (impl->used && !impl->ready))
        throw std::logic_error("The Window is closed");
    if (impl->input_depth || impl->replacing || impl->syncing || impl->native_file_dialog)
        throw std::logic_error("Post content replacement outside native input callbacks");
    std::set<std::uint64_t> existing;
    Impl::content_ids(impl->root, existing);
    for (const auto& entry : impl->popups) Impl::content_ids(entry.popup, existing);
    if (!existing.contains(host.id()))
        throw std::invalid_argument("ContentHost must belong to this window");
    {
        std::lock_guard lock(host_claim_mutex);
        for (const auto id : existing) {
            const auto claim = host_claims.find(id);
            if (claim != host_claims.end() && claim->second != impl.get())
                throw std::invalid_argument("ContentHost belongs to another live Window");
        }
    }
    auto registration = inspection::Registration::prepare(content, std::move(targets), std::move(picked));
    auto path = inspection::host_path(impl->root, host, impl->root->bounds(), false);
    if (!path) {
        for (const auto& entry : impl->popups) {
            path = inspection::host_path(entry.popup, host, impl->root->bounds(), false);
            if (path) break;
        }
        if (path && !registration.targets.empty())
            throw std::invalid_argument("Content inspection is not supported inside popups");
    }
    if (!path) throw std::invalid_argument("ContentHost must have a stable retained path in this window");
    const auto previous_state = impl->content_picking.find(host.id());
    if (previous_state != impl->content_picking.end() && previous_state->second.enabled) {
        impl->validate_inspection_ancestors(*path);
        inspection::supported(content, &host);
    }
    const bool same_content = host.content() == content;
    if (content && !same_content) {
        host.validate_adoption(content);
        std::set<std::uint64_t> candidate;
        Impl::content_ids(content, candidate, true);
        for (const auto id : candidate) if (existing.contains(id))
            throw std::invalid_argument("Replacement content already belongs to this window");
        std::lock_guard lock(host_claim_mutex);
        for (const auto id : candidate) {
            const auto claim = host_claims.find(id);
            if (claim != host_claims.end() && claim->second != impl.get())
                throw std::invalid_argument("Replacement content belongs to another live Window");
        }
    }
    const auto previous = host.content();
    std::set<std::uint64_t> retired;
    Impl::content_ids(previous, retired);
    impl->content_picking.try_emplace(host.id());
    if (same_content) {
        auto& state = impl->content_picking.at(host.id());
        state.host = path->host;
        state.registration = std::move(registration);
        ++state.generation;
        state.pending.reset();
        state.highlight.reset();
        if (impl->window) InvalidateRect(impl->window, nullptr, FALSE);
        return;
    }
    impl->replacing = true;
    impl->replacement_host = &host;
    struct Reset {
        Impl& impl;
        ~Reset() { impl.replacing = false; impl.replacement_host = nullptr; }
    } reset{*impl};
    try {
        host.replace(std::move(content));
        for (std::size_t i = 0; i < impl->popups.size();) {
            if (retired.contains(impl->popups[i].anchor->id())) {
                const auto popup = impl->popups[i].popup;
                impl->dismiss_popup(*popup, PopupDismissReason::hidden, false);
            } else ++i;
        }
        impl->layout_pending = true;
        impl->update();
        if (impl->failed || impl->closing || (impl->used && !impl->ready))
            throw std::runtime_error("Content replacement did not complete because the window closed");
        auto& state = impl->content_picking.at(host.id());
        if (state.enabled) impl->validate_inspection_native(host);
        state.host = path->host;
        state.registration = std::move(registration);
        ++state.generation;
        state.pending.reset();
        state.highlight.reset();
        impl->prune_inspection_hosts();
    } catch (...) {
        impl->fail();
        throw;
    }
}
void Window::set_content_pointer_picking(ContentHost& host, bool enabled) {
    const auto impl = impl_;
    impl->inspection_api_ready();
    const auto path = inspection::host_path(impl->root, host, impl->root->bounds(), false);
    if (!path) throw std::invalid_argument("ContentHost must belong to this window");
    if (enabled) {
        impl->inspection_idle();
        impl->validate_inspection_ancestors(*path);
        inspection::supported(host.content(), &host);
        impl->validate_inspection_region(host);
        impl->validate_inspection_native(host);
    }
    auto& state = impl->content_picking[host.id()];
    state.host = path->host;
    state.enabled = enabled;
    state.pending.reset();
}
std::optional<std::uint32_t> Window::hit_test_content(ContentHost& host, Point position) {
    const auto impl = impl_;
    impl->inspection_api_ready();
    if (!std::isfinite(position.x) || !std::isfinite(position.y))
        throw std::invalid_argument("Inspection coordinates must be finite");
    const auto path = inspection::host_path(impl->root, host, impl->root->bounds(), false);
    if (!path) throw std::invalid_argument("ContentHost must belong to this window");
    impl->inspection_idle();
    impl->validate_inspection_ancestors(*path);
    inspection::supported(host.content(), &host);
    if (impl->layout_pending) impl->update();
    impl->validate_inspection_region(host);
    impl->validate_inspection_native(host);
    const auto found = impl->content_picking.find(host.id());
    if (found == impl->content_picking.end()) return {};
    return impl->inspection_hit(host, position, found->second.registration).key;
}
ContentHighlightResult Window::highlight_content(ContentHost& host, std::optional<std::uint32_t> key) {
    const auto impl = impl_;
    if (GetCurrentThreadId() != impl->owner_thread || !impl->ready || impl->closing || !impl->window ||
        impl->input_depth || impl->replacing || impl->syncing)
        throw std::logic_error("Highlight content on the running UI thread outside native input");
    if (!impl->root || !inspection::host_path(impl->root, host, impl->root->bounds(), false))
        throw std::invalid_argument("ContentHost must belong to this window");
    const auto found = impl->content_picking.find(host.id());
    if (key && (found == impl->content_picking.end() ||
        std::none_of(found->second.registration.targets.begin(), found->second.registration.targets.end(),
            [&](const auto& target) { return target.key == *key; })))
        throw std::invalid_argument("Highlight key is not registered in the current content");
    if (found != impl->content_picking.end()) found->second.highlight.reset();
    InvalidateRect(impl->window, nullptr, FALSE);
    if (!key) return ContentHighlightResult::cleared;
    if (impl->layout_pending) impl->update();
    if (!impl->ready || impl->closing || !impl->window)
        throw std::runtime_error("The window closed while preparing highlight layout");
    auto& state = impl->content_picking.at(host.id());
    inspection::Outline ring;
    const auto result = impl->content_highlight_geometry(host, state, *key, ring);
    if (result == ContentHighlightResult::applied) state.highlight = key;
    return result;
}
void Window::set_theme(ThemeMode theme) {
    if (impl_->options.theme == theme) return;
    impl_->options.theme = theme;
    impl_->apply_theme();
}
void Window::set_presentation(std::string_view font_family, float font_size, bool smooth_scrolling, bool animations) {
    if (GetCurrentThreadId() != impl_->owner_thread) throw std::logic_error("Set presentation on its UI thread");
    if (impl_->closing || (impl_->used && !impl_->ready)) throw std::logic_error("The Window is closed");
    if (!std::isfinite(font_size) || font_size < 8 || font_size > 32)
        throw std::invalid_argument("Font size must be between 8 and 32 DIPs");
    auto family = make_style_font_family(font_family);
    if (family->name.size() > 128) throw std::invalid_argument("Font family exceeds 128 UTF-16 units");
    impl_->presentation_font = std::move(family);
    impl_->presentation_font_size = font_size;
    impl_->smooth_scrolling = smooth_scrolling;
    impl_->presentation_animations = animations;
    for (const auto& peer : impl_->peers) impl_->apply_typography(*peer->control);
    impl_->invalidate(Invalidation::layout);
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
void Window::set_show_activated(bool value) {
    if (GetCurrentThreadId() != impl_->owner_thread) throw std::logic_error("Set initial activation on its UI thread");
    if (impl_->used || impl_->closing) throw std::logic_error("Set initial activation before Application::run");
    impl_->options.show_activated = value;
}
void Window::set_transparent(bool value) {
    if (GetCurrentThreadId() != impl_->owner_thread) throw std::logic_error("Set transparency on the Window UI thread");
    if (impl_->used || impl_->closing) throw std::logic_error("Set transparency before showing the Window");
    if (value && impl_->titlebar) throw std::invalid_argument("A transparent window cannot use the XUI title bar");
    impl_->options.transparent = value;
    if (!value) impl_->drag_region.reset();
}
void Window::set_backdrop(WindowBackdrop backdrop) {
    if (GetCurrentThreadId() != impl_->owner_thread) throw std::logic_error("Set the backdrop on the Window UI thread");
    if (impl_->closing || (impl_->used && !impl_->ready)) throw std::logic_error("The Window is closed");
    if (backdrop < WindowBackdrop::none || backdrop > WindowBackdrop::acrylic)
        throw std::invalid_argument("Invalid window backdrop");
    // The layered presentation is chosen when the HWND is created.
    if (backdrop != WindowBackdrop::none && impl_->used && impl_->layered())
        throw std::invalid_argument("A shown layered window cannot add a system backdrop");
    if (backdrop != WindowBackdrop::none && impl_->options.composition)
        throw std::invalid_argument("A composition window cannot use a system backdrop");
    if (impl_->options.backdrop == backdrop) return;
    impl_->options.backdrop = backdrop;
    if (impl_->ready && impl_->window) impl_->apply_theme();
}
WindowBackdrop Window::backdrop() const { return impl_->options.backdrop; }
bool Window::backdrop_active() const {
    return impl_->backdrop_presented && impl_->ready && impl_->window && !impl_->closing;
}
void Window::set_composition(bool value) {
    if (GetCurrentThreadId() != impl_->owner_thread) throw std::logic_error("Set composition on the Window UI thread");
    if (impl_->used || impl_->closing) throw std::logic_error("Set composition before showing the Window");
    if (value && impl_->options.backdrop != WindowBackdrop::none)
        throw std::invalid_argument("A composition window cannot use a system backdrop");
    impl_->options.composition = value;
}
bool Window::composition() const { return impl_->options.composition; }
HWND Window::native_window() const {
    if (GetCurrentThreadId() != impl_->owner_thread) throw std::logic_error("Read native window on the Window UI thread");
    if (!impl_->ready || impl_->closing || !impl_->window) throw std::logic_error("The native window is not open");
    return impl_->window;
}
void Window::on_native_created(std::function<void(HWND)> callback) {
    if (GetCurrentThreadId() != impl_->owner_thread) throw std::logic_error("Configure the native window on its UI thread");
    if (impl_->used || impl_->closing) throw std::logic_error("Configure the native window before showing it");
    impl_->native_created = std::move(callback);
}
void Window::set_drag_region(Rect region) {
    if (GetCurrentThreadId() != impl_->owner_thread) throw std::logic_error("Set the drag region on its UI thread");
    if (impl_->used || impl_->closing) throw std::logic_error("Set the drag region before showing the Window");
    if (!impl_->options.transparent) throw std::invalid_argument("A drag region requires a transparent window");
    if (!std::isfinite(region.x) || !std::isfinite(region.y) || !std::isfinite(region.width) ||
        !std::isfinite(region.height) || region.x < 0 || region.y < 0 || region.width <= 0 || region.height <= 0 ||
        region.x + region.width > 16000 || region.y + region.height > 16000)
        throw std::invalid_argument("The drag region must have finite positive bounds");
    impl_->drag_region = region;
}
void Window::set_tooltip_style(std::shared_ptr<const ControlStyle> style) {
    if (GetCurrentThreadId() != impl_->owner_thread) throw std::logic_error("Set tooltip styles on the window UI thread");
    if (impl_->closing || (impl_->used && !impl_->ready)) throw std::logic_error("The Window is closed");
    if (!style && !impl_->tooltip_styling) return;
    auto next = impl_->tooltip_styling ? nullptr : std::make_unique<ControlStyleAttachment>(StyleTarget::tooltip);
    auto* attachment = next ? next.get() : impl_->tooltip_styling.get();
    if (!attachment->assign_style(std::move(style), impl_->tooltip_shown ? style_states::open : 0)) return;
    if (next) impl_->tooltip_styling = std::move(next);
    impl_->tooltip_style_changed();
}
std::shared_ptr<const ControlStyle> Window::tooltip_style() const {
    return impl_->tooltip_styling ? impl_->tooltip_styling->style() : nullptr;
}
void Window::set_tooltip_style_values(StylePart part, PartStyleValues values) {
    if (GetCurrentThreadId() != impl_->owner_thread) throw std::logic_error("Set tooltip styles on the window UI thread");
    if (impl_->closing || (impl_->used && !impl_->ready)) throw std::logic_error("The Window is closed");
    validate_part_values(StyleTarget::tooltip, part, values);
    if (values.empty() && !impl_->tooltip_styling) return;
    auto next = impl_->tooltip_styling ? nullptr : std::make_unique<ControlStyleAttachment>(StyleTarget::tooltip);
    auto* attachment = next ? next.get() : impl_->tooltip_styling.get();
    if (!attachment->assign_local(part, std::move(values), impl_->tooltip_shown ? style_states::open : 0)) return;
    if (next) impl_->tooltip_styling = std::move(next);
    impl_->tooltip_style_changed();
}
const PartStyleValues& Window::tooltip_style_values(StylePart part) const {
    if (GetCurrentThreadId() != impl_->owner_thread) throw std::logic_error("Read tooltip styles on the window UI thread");
    validate_part(StyleTarget::tooltip, part);
    static const PartStyleValues empty;
    return impl_->tooltip_styling ? impl_->tooltip_styling->local(part) : empty;
}
const PartStyleValues* Window::effective_tooltip_style_values(StylePart part) const {
    if (GetCurrentThreadId() != impl_->owner_thread) throw std::logic_error("Read tooltip styles on the window UI thread");
    validate_part(StyleTarget::tooltip, part);
    return impl_->tooltip_styling ? impl_->tooltip_styling->effective(part, impl_->tooltip_shown ? style_states::open : 0) : nullptr;
}
const std::wstring& Window::error() const { return impl_->error; }
bool Window::focus(Control& control, bool select_all) {
    const auto impl = impl_;
    Impl::InputScope scope(*impl);
    if (!impl->ready || impl->closing || !control.focusable()) return false;
    if (impl->replacing && !impl->retains(control)) return false;
    if (impl->layout_pending) impl->update();
    if (!impl->ready || impl->closing) return false;
    // Focus can collect a WinUI clear button and reallocate the peer vector.
    // InputScope retains the peer itself across those synchronous updates.
    auto* peer = impl->find_peer(&control);
    if (!peer || !impl->focus(*peer, false)) return false;
    if (Impl::text_input_peer(*peer) && select_all) SendMessageW(peer->window, EM_SETSEL, 0, -1);
    return true;
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
bool Window::show_item_commands(std::shared_ptr<CommandSurface> surface, VirtualCollection& anchor, ItemKey key) {
    const auto impl = impl_;
    if (GetCurrentThreadId() != impl->owner_thread) throw std::logic_error("Show commands on the window UI thread");
    if (!surface || !surface->menu()->commands()) throw std::invalid_argument("Command surface has no commands");
    Impl::InputScope scope(*impl);
    auto* peer = impl->find_peer(&anchor);
    if (!peer || !impl->enabled(*peer) || !impl->onscreen(*peer) || !impl->in_top_popup(*peer)) return false;
    const auto source = anchor.source();
    if (!source) return false;
    const auto index = source->find(key);
    if (!index || *index >= source->size() || source->key(*index) != key || !source->item(*index).enabled) return false;
    const auto rows = anchor.visible_content();
    const auto found = std::find_if(rows.begin(), rows.end(), [&](const auto& row) {
        return row.key == key && row.index == *index;
    });
    if (anchor.source() != source || found == rows.end() || !found->content.enabled ||
        found->content.separator || found->pending || found->error) return false;
    peer = impl->find_peer(&anchor);
    if (!peer || !impl->ready || impl->closing || !impl->enabled(*peer) ||
        !impl->onscreen(*peer) || !impl->in_top_popup(*peer)) return false;
    const auto row = found->bounds;
    if (found->content.action.empty() || row.width < 160 || row.height <= 16) return false;
    auto bounds = Rect{row.x + row.width - 70, row.y + 8, 64, row.height - 16};
    const auto intersect = [&](Rect clip) {
        const auto right = std::min(bounds.x + bounds.width, clip.x + clip.width);
        const auto bottom = std::min(bounds.y + bounds.height, clip.y + clip.height);
        bounds.x = std::max(bounds.x, clip.x); bounds.y = std::max(bounds.y, clip.y);
        bounds.width = std::max(0.0f, right - bounds.x); bounds.height = std::max(0.0f, bottom - bounds.y);
    };
    intersect(anchor.content_viewport());
    bounds.x += anchor.bounds().x; bounds.y += anchor.bounds().y;
    intersect(impl->clipped_bounds(*peer));
    if (bounds.width <= 0 || bounds.height <= 0) return false;
    const std::weak_ptr<const ItemsSource> weak_source = source;
    const std::weak_ptr<Control> weak_anchor = peer->control;
    surface->set_current([weak_source, weak_anchor, key] {
        const auto source = weak_source.lock();
        const auto control = weak_anchor.lock();
        const auto collection = std::dynamic_pointer_cast<VirtualCollection>(control);
        if (!source || !collection || !collection->enabled() || !collection->visible() ||
            collection->source() != source) return false;
        const auto index = source->find(key);
        return index && *index < source->size() && source->key(*index) == key && source->item(*index).enabled;
    });
    impl->show_commands(surface, anchor, {}, bounds);
    return surface->popup()->is_open();
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
    if (impl->has_content_picking()) throw std::invalid_argument("Close pointer picking before opening Shell commands");
    Impl::InputScope scope(*impl);
    auto* peer = impl->find_peer(&anchor);
    if (!peer || !impl->ready || impl->closing || !impl->enabled(*peer)) throw std::logic_error("Shell command owner is unavailable");
    POINT point{}; GetCursorPos(&point);
    RECT bounds{}; GetWindowRect(peer->window, &bounds);
    if (!PtInRect(&bounds, point)) point = {bounds.left + 12, bounds.top + 12};
    track_shell_commands(impl->window, paths, {static_cast<float>(point.x), static_cast<float>(point.y)});
}
void Window::prefetch_shell_commands(std::wstring path) {
    const auto impl = impl_;
    if (GetCurrentThreadId() != impl->owner_thread) throw std::logic_error("Prefetch Shell commands on the window UI thread");
    if (!impl->ready || impl->closing) throw std::logic_error("Shell prefetch requires an open Window");
    if (path.size() > 32767 || path.find(L'\0') != std::wstring::npos)
        throw std::invalid_argument("Invalid Shell prefetch path");
    impl->shell_prefetch.reset();
    if (!path.empty()) impl->shell_prefetch = AsyncShellMenu::prefetch(impl->window, std::move(path));
}
void Window::close() {
    auto impl = impl_;
    if (GetCurrentThreadId() != impl->owner_thread) throw std::logic_error("Close the Window on its UI thread");
    Impl::InputScope input_scope(*impl);
    impl->closing = true;
    if (impl->native_file_dialog) impl->native_file_dialog->cancel();
    impl->close_posts();
    for (auto& task : impl->samples) task->cancel();
    for (auto& task : impl->tasks) task->cancel();
    for (const auto& peer : impl->peers) {
        if (peer->runtime_host()) peer->runtime_host()->cancel_owner();
        if (peer->swap_chain_host()) peer->swap_chain_host()->cancel_owner();
        if (auto* map = dynamic_cast<MapView*>(peer->control.get())) map->cancel_request();
    }
    if (impl->window) win32_require(PostMessageW(impl->window, WM_CLOSE, 0, 0) != FALSE, "Close native window");
    if (impl->application_managed) impl->wake->signal();
}
WindowState Window::state() const {
    if (GetCurrentThreadId() != impl_->owner_thread) throw std::logic_error("Read Window state on its UI thread");
    return impl_->closed_notified ? WindowState::closed : impl_->closing ? WindowState::closing :
        impl_->ready ? WindowState::open : WindowState::created;
}
void Window::on_closed(std::function<void()> callback) {
    if (GetCurrentThreadId() != impl_->owner_thread) throw std::logic_error("Subscribe on the Window UI thread");
    if (impl_->closed_notified) throw std::logic_error("The Window is closed");
    impl_->closed_callback = std::move(callback);
}
void Window::on_key(std::function<bool(const KeyEvent&)> callback) { impl_->key = std::move(callback); }
bool Window::post(std::function<void()> callback) {
    if (!callback) throw std::invalid_argument("A posted callback is required");
    const auto impl = impl_;
    std::lock_guard lock(impl->post_mutex);
    if (impl->posts_closed) return false;
    impl->posts.push_back(std::move(callback));
    impl->wake->signal();
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
    if (impl_->has_content_picking()) throw std::invalid_argument("Close pointer picking before opening a modal dialog");
    if (!impl_->ready || impl_->closing) return false;
    return MessageBoxW(impl_->window, message.c_str(), title.c_str(),
        MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES;
}
std::shared_ptr<ViewTask> Window::create_view_task(ViewWorker::Loader loader, std::function<void(ViewResult)> receive) {
    if (impl_->closing || (impl_->used && !impl_->ready)) throw std::logic_error("The Window is closed");
    std::erase_if(impl_->tasks, [](const auto& task) { return task->cancelled; });
    auto state = std::make_shared<ViewTask::Impl>();
    state->receive = std::move(receive);
    state->worker = std::make_shared<ViewWorker>(std::move(loader), [wake = impl_->wake] { wake->signal(); });
    state->worker->start();
    impl_->tasks.push_back(state);
    return std::shared_ptr<ViewTask>(new ViewTask(std::move(state)));
}
void Window::copy_text(const std::wstring& text) {
    if (!impl_->ready || impl_->closing || !impl_->window)
        throw std::logic_error("Clipboard text requires an open Window");
    set_clipboard_text(text);
}
void Window::set_clipboard_text(const std::wstring& text) {
    const auto impl = impl_;
    if (GetCurrentThreadId() != impl->owner_thread || impl->closing || (impl->used && !impl->ready))
        throw std::logic_error("Use clipboard text on the open Window's UI thread");
    Impl::InputScope scope(*impl);
    files::set_text(text);
}
void Window::set_file_clipboard(const std::vector<std::wstring>& paths, FileTransferEffect effect) {
    const auto impl = impl_;
    if (GetCurrentThreadId() != impl->owner_thread || impl->closing || (impl->used && !impl->ready))
        throw std::logic_error("Use file clipboard on the open Window's UI thread");
    Impl::InputScope scope(*impl);
    files::set_clipboard(paths, effect);
}
std::optional<FileClipboardContent> Window::get_file_clipboard() {
    const auto impl = impl_;
    if (GetCurrentThreadId() != impl->owner_thread || impl->closing || (impl->used && !impl->ready))
        throw std::logic_error("Use file clipboard on the open Window's UI thread");
    Impl::InputScope scope(*impl);
    return files::get_clipboard();
}
bool Window::transfer_files(const std::vector<std::wstring>& paths, const std::wstring& destination, FileTransferEffect effect) {
    const auto impl = impl_;
    if (GetCurrentThreadId() != impl->owner_thread || impl->closing || (impl->used && !impl->ready))
        throw std::logic_error("Transfer files on the open Window's UI thread");
    Impl::InputScope scope(*impl);
    return files::transfer(impl->window, paths, destination, effect, [impl] { return impl->closing || impl->failed; });
}
std::optional<bool> Window::paste_files(const std::wstring& destination) {
    const auto impl = impl_;
    if (GetCurrentThreadId() != impl->owner_thread || impl->closing || (impl->used && !impl->ready))
        throw std::logic_error("Paste files on the open Window's UI thread");
    Impl::InputScope scope(*impl);
    return files::paste(impl->window, destination, [impl] { return impl->closing || impl->failed; });
}
struct Application::Impl : std::enable_shared_from_this<Application::Impl> {
    const DWORD thread = GetCurrentThreadId();
    platform::Runtime runtime;
    std::shared_ptr<TaskWake> wake = std::make_shared<TaskWake>();
    std::vector<std::shared_ptr<Window::Impl>> windows;
    std::vector<std::weak_ptr<Window::Impl>> created;
    std::mutex mutex;
    std::vector<std::function<void()>> posts;
    HWND dispatcher{};
    bool used{}, pumping{}, accepting{}, stopping{}, posts_closed{}, finished{}, drained{};
    std::wstring error;

    void guard() const {
        if (GetCurrentThreadId() != thread) throw std::logic_error("Use the application UI thread");
    }
    void record(const std::wstring& message) {
        if (!error.empty()) error += L"\n";
        error += message.empty() ? L"A window callback failed." : message;
    }
    static LRESULT CALLBACK procedure(HWND hwnd, UINT message, WPARAM w, LPARAM l) noexcept {
        auto* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (self && message == WM_APP + 71) { self->accept(); return 0; }
        return DefWindowProcW(hwnd, message, w, l);
    }
    Impl() {
        WNDCLASSEXW cls{sizeof(cls)};
        cls.hInstance = GetModuleHandleW(nullptr);
        cls.lpfnWndProc = procedure;
        cls.lpszClassName = L"Xui.Dispatcher.1";
        if (!RegisterClassExW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            win32_require(false, "Register application dispatcher");
        dispatcher = CreateWindowExW(0, cls.lpszClassName, L"", 0, 0, 0, 0, 0,
            HWND_MESSAGE, nullptr, cls.hInstance, this);
        win32_require(dispatcher != nullptr, "Create application dispatcher");
        wake->connect(dispatcher);
    }
    void stop() {
        stopping = true;
        const auto snapshot = created;
        for (const auto& weak : snapshot) if (auto window = weak.lock(); window && !window->closed_notified)
            window->destroy();
        wake->signal();
    }
    void accept() noexcept {
        wake->accepted();
        if (!pumping || accepting || finished) return;
        accepting = true;
        try {
            auto snapshot = windows;
            for (const auto& window : snapshot) {
                if (window->ready && !window->closing) window->deliver();
                if (!window->window && !window->input_depth && !window->dispatch_depth && !window->closed_notified) {
                    window->complete();
                    if (window->failed) record(window->options.title + L": " + window->error);
                    std::erase(windows, window);
                }
            }
            std::vector<std::function<void()>> queued;
            {
                std::lock_guard lock(mutex);
                const auto count = std::min<std::size_t>(posts.size(), 64);
                queued.insert(queued.end(), std::make_move_iterator(posts.begin()), std::make_move_iterator(posts.begin() + count));
                posts.erase(posts.begin(), posts.begin() + count);
                if (!posts.empty()) wake->signal();
            }
            for (auto& callback : queued) callback();
            bool empty{};
            {
                std::lock_guard lock(mutex);
                empty = windows.empty() && posts.empty();
            }
            if (empty) {
                const auto pending = created;
                for (const auto& weak : pending) if (auto window = weak.lock(); window && !window->closed_notified && !window->window) {
                    window->complete();
                    if (window->failed) record(window->options.title + L": " + window->error);
                }
                {
                    std::lock_guard lock(mutex);
                    empty = windows.empty() && posts.empty();
                    if (empty) posts_closed = true;
                }
                if (empty) {
                    stopping = true;
                    finished = true;
                    PostQuitMessage(error.empty() ? 0 : 1);
                }
            }
        } catch (...) {
            try {
                try { throw; }
                catch (const std::exception& failure) { record(exception_message(failure)); }
                catch (...) { record(L"An application callback failed."); }
                stop();
            } catch (...) { PostQuitMessage(1); }
        }
        accepting = false;
    }
    ~Impl() {
        wake->connect(nullptr);
        if (dispatcher) DestroyWindow(dispatcher);
    }
};

Application::Application() {
    if (running || application_context) throw std::logic_error("An application already owns this UI thread");
    impl_ = std::make_shared<Impl>();
    application_context = true;
}
Application::~Application() {
    if (!impl_) return;
    impl_->guard();
    impl_->stop();
    for (const auto& weak : impl_->created)
        if (auto window = weak.lock()) window->teardown();
    if (!impl_->drained && !NativeRuntimeHost::drain_shutdown())
        OutputDebugStringW(L"XUI: Native runtime shutdown timed out during application destruction.\n");
    impl_.reset();
    application_context = false;
}
std::shared_ptr<Window> Application::create_window(WindowOptions options) {
    impl_->guard();
    if (impl_->stopping) throw std::logic_error("The application is stopping");
    auto window = std::make_shared<Window>(std::move(options));
    window->impl_->application_managed = true;
    window->impl_->application = impl_;
    window->impl_->wake = impl_->wake;
    impl_->created.push_back(window->impl_);
    return window;
}
void Application::show(Window& window) {
    impl_->guard();
    const auto host = window.impl_;
    if (impl_->stopping) throw std::logic_error("The application is stopping");
    if (host->application.lock() != impl_) throw std::logic_error("Window belongs to another application");
    if (host->used || host->closing) throw std::logic_error("A Window can show only once");
    impl_->windows.push_back(host);
    try { host->create(); }
    catch (...) {
        host->fail();
        impl_->wake->signal();
        throw;
    }
    impl_->wake->signal();
}
bool Application::post(std::function<void()> callback) {
    if (!callback) throw std::invalid_argument("An application callback is required");
    std::lock_guard lock(impl_->mutex);
    if (impl_->posts_closed) return false;
    impl_->posts.push_back(std::move(callback));
    impl_->wake->signal();
    return true;
}
void Application::shutdown() { impl_->guard(); impl_->stop(); }
const std::wstring& Application::error() const { return impl_->error; }
int Application::run() {
    const auto self = impl_;
    self->guard();
    if (self->used || running) throw std::logic_error("The application can run only once without nesting");
    self->used = true;
    self->pumping = true;
    running = true;
    struct Reset { Impl& self; ~Reset() { self.pumping = false; running = false; } } reset{*self};
    int result{};
    try {
        self->wake->signal();
        result = self->runtime.run([&](MSG& msg) {
            const auto snapshot = self->windows;
            for (const auto& window : snapshot) {
                if (!window->ready || window->closing) continue;
                if (msg.hwnd == window->window || IsChild(window->window, msg.hwnd)) {
                    try { return window->translate(msg); }
                    catch (...) { window->fail(); return true; }
                }
            }
            return false;
        }, self->wake->event, [&] { self->accept(); }, [&] {
            if (std::none_of(self->windows.begin(), self->windows.end(), [](const auto& window) {
                return window->animation_timer_running || window->animation_frame_pending;
            })) return;
            const auto snapshot = self->windows;
            for (const auto& window : snapshot) {
                try { window->present_animation_frame(); }
                catch (...) { window->fail(); }
            }
        });
    } catch (const std::exception& failure) { self->record(exception_message(failure)); result = 1; }
    self->finished = true;
    self->stop();
    for (const auto& weak : self->created) if (auto window = weak.lock(); window && !window->closed_notified) {
        window->complete();
        if (window->failed) self->record(window->options.title + L": " + window->error);
    }
    self->windows.clear();
    std::vector<std::function<void()>> removed;
    { std::lock_guard lock(self->mutex); self->posts_closed = true; removed.swap(self->posts); }
    self->drained = true;
    if (!NativeRuntimeHost::drain_shutdown()) {
        self->record(L"Owned web runtime shutdown did not complete within thirty seconds");
        result = 1;
    }
    return self->error.empty() ? result : 1;
}

int Application::run(Window& window) {
    // A command can delete its public Window. Retain the backend until dispatch
    // unwinds. An active native file dialog defers HWND teardown until its modal call returns.
    const auto impl = window.impl_;
    if (running || application_context || impl->application_managed) {
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
                        impl->deliver_content_picks();
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
                }, [&] {
                    try { impl->present_animation_frame(); }
                    catch (...) { impl->fail(); }
                });
            impl->quit_posted = false;
            impl->teardown();
            impl->complete();
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

#include "application_file_dialog.inc"

}
