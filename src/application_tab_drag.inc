    struct TabMove {
        unsigned strip{};
        std::uint64_t id{};
        RECT original{}, band{};
        WindowPlacement placement;
        float grab_x{}, grab_y{};
        bool torn{}, cancelled{}, single{}, entered{};
        std::weak_ptr<TabStrip> indicator;
        std::weak_ptr<Impl> preview_target;
    };
    std::optional<TabMove> tab_move;
    inline static thread_local Impl* moving_tabs{};

    std::optional<unsigned> titlebar_strip(const TabStrip* strip) const {
        if (!titlebar) return {};
        if (titlebar->tabs().get() == strip) return 0;
        if (titlebar->secondary_tabs().get() == strip) return 1;
        return {};
    }
    std::shared_ptr<TabStrip> drag_strip(unsigned index) const {
        return index == 0 ? titlebar->tabs() : titlebar->secondary_tabs();
    }
    WindowPlacement read_placement() const {
        if (!window) {
            if (initial_placement) return *initial_placement;
            throw std::logic_error("Show the Window or set its placement before reading it");
        }
        RECT rect{};
        win32_require(GetWindowRect(window, &rect) != 0, "Read window bounds");
        const bool maximized = IsZoomed(window) != FALSE;
        if (maximized || IsIconic(window)) {
            WINDOWPLACEMENT placement{sizeof(placement)};
            win32_require(GetWindowPlacement(window, &placement) != 0, "Read restored window bounds");
            rect = placement.rcNormalPosition;
            MONITORINFO monitor{sizeof(monitor)};
            win32_require(GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor) != 0,
                "Read window monitor");
            OffsetRect(&rect, monitor.rcWork.left - monitor.rcMonitor.left, monitor.rcWork.top - monitor.rcMonitor.top);
        }
        return {rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, maximized};
    }
    bool notify_tab_drag(TabDragKind kind, Window* target = nullptr, unsigned target_strip = 0, std::size_t index = 0) {
        if (!tab_move || !tab_drag_handler || closing) return false;
        const auto callback = tab_drag_handler;
        return callback({kind, tab_move->strip, tab_move->id, target, target_strip, index});
    }
    bool current_drag_tab() const {
        if (!tab_move || !window || closing || !tab_drag_handler) return false;
        const auto strip = drag_strip(tab_move->strip);
        const auto peer = find_peer(strip.get());
        return peer && visible(*peer) && enabled(*peer) &&
            std::any_of(strip->tabs().begin(), strip->tabs().end(), [&](const auto& tab) { return tab.id == tab_move->id; });
    }
    void clear_tab_indicator() {
        if (tab_move) {
            if (auto strip = tab_move->indicator.lock()) strip->set_drop_indicator({});
            tab_move->indicator.reset();
        }
    }
    struct TabDropTarget {
        std::shared_ptr<Impl> host;
        unsigned strip{};
        std::size_t index{};
    };
    std::optional<TabDropTarget> tab_drop_target(POINT point) {
        // Walk real top-level Z order, including foreign windows, so occluded strips cannot accept a drop.
        for (HWND hwnd = GetTopWindow(nullptr); hwnd; hwnd = GetWindow(hwnd, GW_HWNDNEXT)) {
            if ((hwnd == window && tab_move->torn) || !IsWindowVisible(hwnd) || IsIconic(hwnd)) continue;
            RECT rect{};
            if (!GetWindowRect(hwnd, &rect) || !PtInRect(&rect, point)) continue;
            BOOL cloaked{};
            if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked) continue;
            if (GetWindowThreadProcessId(hwnd, nullptr) != owner_thread) return {};
            wchar_t name[64]{};
            if (!GetClassNameW(hwnd, name, 64) || std::wstring_view(name) != window_class) return {};
            auto* other = reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            if (!other || !other->ready || other->closing || !other->titlebar || !other->tab_drag_handler ||
                !other->public_window || !IsWindowEnabled(hwnd) || !other->popups.empty() ||
                other->application.expired() || other->application.lock() != application.lock()) return {};
            const auto host = other->shared_from_this();
            for (unsigned index = 0; index != 2; ++index) {
                if (other == this && index == tab_move->strip) continue;
                const auto strip = other->drag_strip(index);
                const auto peer = other->find_peer(strip.get());
                if (!peer || !other->visible(*peer) || !other->enabled(*peer)) continue;
                RECT band{};
                win32_require(GetWindowRect(peer->window, &band) != 0, "Locate tab drop strip");
                if (!PtInRect(&band, point)) continue;
                const float x = (point.x - band.left) * 96.0f / other->dpi;
                const auto slot = strip->insertion_index(x);
                if (notify_tab_drag(TabDragKind::query_drop, other->public_window, index, slot) &&
                    current_drag_tab() && !other->closing && other->tab_drag_handler &&
                    slot <= strip->tabs().size())
                    return TabDropTarget{host, index, slot};
                return {};
            }
            return {};
        }
        return {};
    }
    void move_tab_window(RECT& rect) {
        if (!current_drag_tab()) {
            tab_move->cancelled = true;
            win32_require(PostMessageW(window, WM_CANCELMODE, 0, 0) != 0, "Cancel obsolete tab drag");
            return;
        }
        POINT point{};
        win32_require(GetCursorPos(&point) != 0, "Read tab drag position");
        clear_tab_indicator();
        auto target = tab_drop_target(point);
        if (target) {
            auto strip = target->host->drag_strip(target->strip);
            strip->set_drop_indicator(target->index);
            tab_move->indicator = strip;
            if (tab_move->torn && tab_move->preview_target.lock() != target->host) {
                win32_require(SetWindowPos(target->host->window, HWND_TOP, 0, 0, 0, 0,
                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE) != 0, "Show tab merge preview");
                tab_move->preview_target = target->host;
            }
            target->host->update();
        } else if (!tab_move->preview_target.expired()) {
            win32_require(SetWindowPos(window, HWND_TOP, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE) != 0, "Show detached tab window");
            tab_move->preview_target.reset();
        }
        if (!tab_move->torn) {
            auto band = tab_move->band;
            const int margin = MulDiv(12, dpi, 96);
            InflateRect(&band, margin, margin);
            if (!tab_move->single && PtInRect(&band, point)) {
                const auto strip = drag_strip(tab_move->strip);
                const auto slot = strip->insertion_index((point.x - tab_move->band.left) * 96.0f / dpi);
                const auto it = std::find_if(strip->tabs().begin(), strip->tabs().end(),
                    [&](const auto& tab) { return tab.id == tab_move->id; });
                const auto index = static_cast<std::size_t>(it - strip->tabs().begin());
                if (slot != index && slot != index + 1) {
                    notify_tab_drag(TabDragKind::reorder, public_window, tab_move->strip, slot);
                    if (!closing) update();
                }
                rect = tab_move->original;
                return;
            }
            // Crossing straight into the other pane can merge without creating a window.
            if (target && target->host.get() == this) { rect = tab_move->original; return; }
            if (!notify_tab_drag(TabDragKind::tear_out) || closing) { rect = tab_move->original; return; }
            tab_move->torn = true;
            if (!current_drag_tab()) throw std::logic_error("Tear-out must retain the dragged tab in its source strip");
            update();
            rect.right = rect.left + tab_move->placement.width;
            rect.bottom = rect.top + tab_move->placement.height;
        }
        const auto strip = drag_strip(tab_move->strip);
        const auto peer = find_peer(strip.get());
        RECT strip_rect{}, outer{};
        win32_require(GetWindowRect(peer->window, &strip_rect) != 0 && GetWindowRect(window, &outer) != 0,
            "Anchor torn-out tab");
        const auto it = std::find_if(strip->tabs().begin(), strip->tabs().end(),
            [&](const auto& tab) { return tab.id == tab_move->id; });
        const auto bounds = strip->tab_bounds(static_cast<std::size_t>(it - strip->tabs().begin()));
        const int x = strip_rect.left - outer.left +
            static_cast<int>((bounds.x + std::min(tab_move->grab_x, std::max(0.0f, bounds.width - 8))) * dpi / 96);
        const int y = strip_rect.top - outer.top + static_cast<int>(tab_move->grab_y * dpi / 96);
        OffsetRect(&rect, point.x - x - rect.left, point.y - y - rect.top);
    }
    static LRESULT CALLBACK tab_move_filter(int code, WPARAM wparam, LPARAM lparam) noexcept {
        const auto result = CallNextHookEx(nullptr, code, wparam, lparam);
        if (code == HC_ACTION && result == 0 && wparam == VK_ESCAPE && !(static_cast<std::uintptr_t>(lparam) & (1u << 31)) &&
            moving_tabs && moving_tabs->tab_move)
            moving_tabs->tab_move->cancelled = true;
        return result;
    }
    void begin_tab_move(Peer& peer, POINT point) {
        if (tab_move || moving_tabs || !peer.pressed_tab || !popups.empty()) return;
        const auto strip = std::static_pointer_cast<TabStrip>(peer.control);
        const auto source = titlebar_strip(strip.get());
        if (!source) return;
        const auto id = *peer.pressed_tab;
        const auto it = std::find_if(strip->tabs().begin(), strip->tabs().end(), [&](const auto& tab) { return tab.id == id; });
        if (it == strip->tabs().end()) { peer.pressed_tab.reset(); return; }
        TabMove move;
        move.strip = *source; move.id = id;
        move.single = strip->tabs().size() == 1 &&
            (!drag_strip(1 - *source)->visible() || drag_strip(1 - *source)->tabs().empty());
        move.placement = read_placement();
        win32_require(GetWindowRect(window, &move.original) != 0 && GetWindowRect(peer.window, &move.band) != 0,
            "Start tab window drag");
        const auto bounds = strip->tab_bounds(static_cast<std::size_t>(it - strip->tabs().begin()));
        move.grab_x = (peer.tab_press.x - move.band.left) * 96.0f / dpi - bounds.x;
        move.grab_y = (peer.tab_press.y - move.band.top) * 96.0f / dpi;
        const auto lifetime = shared_from_this();
        tab_move = move;
        moving_tabs = this;
        struct Cleanup {
            Impl& host;
            HHOOK hook{};
            ~Cleanup() {
                if (hook) UnhookWindowsHookEx(hook);
                host.clear_tab_indicator();
                host.tab_move.reset();
                moving_tabs = nullptr;
            }
        } cleanup{*this};
        cleanup.hook = SetWindowsHookExW(WH_KEYBOARD, tab_move_filter, nullptr, owner_thread);
        win32_require(cleanup.hook != nullptr, "Observe tab drag cancellation");
        peer.pressed_tab.reset();
        if (GetCapture() == peer.window) ReleaseCapture();
        hide_tooltip();
        // This HWND owns exactly one user32 move loop from initial reorder through tear-out.
        SendMessageW(window, WM_SYSCOMMAND, SC_MOVE | HTCAPTION, MAKELPARAM(point.x, point.y));
        clear_tab_indicator();
        if (closing) return;
        bool merged{};
        if (tab_move->cancelled) {
            notify_tab_drag(TabDragKind::cancel);
            if (!closing) win32_require(SetWindowPos(window, HWND_TOP, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE) != 0, "Restore cancelled tab window");
        } else if (current_drag_tab()) {
            win32_require(GetCursorPos(&point) != 0, "Read released tab position");
            if (auto target = tab_drop_target(point))
                merged = notify_tab_drag(TabDragKind::drop, target->host->public_window, target->strip, target->index);
        }
        if (!closing) {
            const auto original = tab_move->placement;
            const bool restore = tab_move->cancelled || !tab_move->torn;
            notify_tab_drag(TabDragKind::completed);
            if (!closing && restore && public_window) public_window->set_placement(original);
            if (!closing && tab_move->torn && !merged && !tab_move->cancelled)
                win32_require(SetWindowPos(window, HWND_TOP, 0, 0, 0, 0,
                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE) != 0, "Finish detached tab window");
        }
    }
