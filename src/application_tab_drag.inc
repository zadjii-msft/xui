    struct TabDropTarget {
        std::shared_ptr<Impl> host;
        unsigned strip{};
        std::size_t index{};
    };
    struct TabMove {
        unsigned strip{};
        std::uint64_t id{};
        RECT original{}, band{};
        WindowPlacement placement;
        int detached_width{}, detached_height{};
        float grab_x{}, grab_y{};
        bool torn{}, cancelled{}, single{}, entered{}, ending{}, adjusting{}, full_drag{}, activate{};
        std::optional<TabDropTarget> joined;
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
        const auto* host = tab_move->joined ? tab_move->joined->host.get() : this;
        if (!host->window || host->closing || !host->titlebar) return false;
        const auto strip = host->drag_strip(tab_move->joined ? tab_move->joined->strip : tab_move->strip);
        const auto peer = host->find_peer(strip.get());
        return peer && host->visible(*peer) && host->enabled(*peer) &&
            std::any_of(strip->tabs().begin(), strip->tabs().end(), [&](const auto& tab) { return tab.id == tab_move->id; });
    }
    void clear_tab_indicator() {
        if (tab_move) {
            if (auto strip = tab_move->indicator.lock()) strip->set_drop_indicator({});
            tab_move->indicator.reset();
        }
    }
    static bool tab_window_contains(HWND hwnd, POINT point, const RECT& rect) {
        const auto style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        if (style & WS_EX_LAYERED) {
            if (style & WS_EX_TRANSPARENT) return false;
            BYTE alpha{};
            DWORD flags{};
            COLORREF color{};
            if (GetLayeredWindowAttributes(hwnd, &color, &alpha, &flags) && (flags & LWA_ALPHA) && alpha == 0)
                return false;
        }
        const auto region = CreateRectRgn(0, 0, 0, 0);
        win32_require(region != nullptr, "Read tab target window region");
        const auto kind = GetWindowRgn(hwnd, region);
        const bool contains = kind == ERROR || PtInRegion(region, point.x - rect.left, point.y - rect.top);
        DeleteObject(region);
        return contains;
    }
    static bool tab_window_arranged(HWND hwnd) {
        // This documented API has no import library on older SDKs.
        using IsArranged = BOOL(WINAPI*)(HWND);
        static const auto arranged = reinterpret_cast<IsArranged>(
            GetProcAddress(GetModuleHandleW(L"user32.dll"), "IsWindowArranged"));
        return IsZoomed(hwnd) || (arranged && arranged(hwnd));
    }
    std::optional<TabDropTarget> tab_drop_target(POINT point) {
        // Walk real top-level Z order, including foreign windows, so occluded strips cannot accept a drop.
        for (HWND hwnd = GetTopWindow(nullptr); hwnd; hwnd = GetWindow(hwnd, GW_HWNDNEXT)) {
            if ((hwnd == window && tab_move->torn) || !IsWindowVisible(hwnd) || IsIconic(hwnd)) continue;
            RECT rect{};
            if (!GetWindowRect(hwnd, &rect) || !PtInRect(&rect, point)) continue;
            BOOL cloaked{};
            if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked) continue;
            if (!tab_window_contains(hwnd, point, rect)) continue;
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
    bool leave_tab_join() {
        if (!tab_move->joined) return true;
        const auto target = *tab_move->joined;
        if (!notify_tab_drag(TabDragKind::leave, target.host->public_window, target.strip, target.index)) return false;
        tab_move->joined.reset();
        if (closing) return false;
        update();
        if (target.host->window && !target.host->closing) target.host->update();
        if (!current_drag_tab()) throw std::logic_error("Leave must restore the dragged tab to its source strip");
        return true;
    }
    void preview_tab_target(const std::optional<TabDropTarget>& target) {
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
            const auto previous = tab_move->preview_target.lock();
            if (previous && previous->window && !previous->closing)
                win32_require(SetWindowPos(previous->window, window, 0, 0, 0, 0,
                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE) != 0, "Lower previous tab host");
            win32_require(SetWindowPos(window, HWND_TOP, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE) != 0, "Show detached tab window");
            tab_move->preview_target.reset();
        }
    }
    void move_tab_window(RECT& rect, bool live_join) {
        if (tab_move->cancelled) return;
        if (tab_move->joined && !current_drag_tab()) {
            if (!leave_tab_join()) throw std::logic_error("Cannot recover the tab from its unavailable hover target");
            tab_move->cancelled = true;
            win32_require(PostMessageW(window, WM_CANCELMODE, 0, 0) != 0, "Cancel unavailable tab target");
            return;
        }
        if (!current_drag_tab()) {
            tab_move->cancelled = true;
            win32_require(PostMessageW(window, WM_CANCELMODE, 0, 0) != 0, "Cancel obsolete tab drag");
            return;
        }
        POINT point{};
        win32_require(GetCursorPos(&point) != 0, "Read tab drag position");
        clear_tab_indicator();
        auto target = tab_drop_target(point);
        if (tab_move->joined && (!target || target->host != tab_move->joined->host ||
            target->strip != tab_move->joined->strip)) {
            if (!leave_tab_join()) throw std::logic_error("Cannot leave the tab hover target");
            rect.right = rect.left + tab_move->detached_width;
            rect.bottom = rect.top + tab_move->detached_height;
            target = tab_drop_target(point);
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
            if (target && target->host.get() == this) {
                preview_tab_target(target);
                rect = tab_move->original;
                return;
            }
            if (!notify_tab_drag(TabDragKind::tear_out) || closing) { rect = tab_move->original; return; }
            tab_move->torn = true;
            if (!current_drag_tab()) throw std::logic_error("Tear-out must retain the dragged tab in its source strip");
            update();
            rect.right = rect.left + tab_move->detached_width;
            rect.bottom = rect.top + tab_move->detached_height;
            // The source no longer occludes a strip beneath the dragged tab.
            target = tab_drop_target(point);
            if (!target)
                win32_require(SetWindowPos(window, HWND_TOP, 0, 0, 0, 0,
                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE) != 0, "Raise detached tab above remaining content");
        }
        if (live_join && target && target->host.get() != this &&
            notify_tab_drag(TabDragKind::join, target->host->public_window, target->strip, target->index)) {
            tab_move->joined = target;
            if (!current_drag_tab()) throw std::logic_error("Join must put the dragged tab in its target strip");
            update();
        }
        preview_tab_target(target);
        if (tab_move->joined) {
            win32_require(GetWindowRect(tab_move->joined->host->window, &rect) != 0, "Locate joined tab host");
            ++rect.bottom;
            return;
        }
        anchor_tab_window(rect, point);
    }
    void anchor_tab_window(RECT& rect, POINT point) {
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
    void show_detached_tab(POINT point) {
        RECT rect{0, 0, tab_move->detached_width, tab_move->detached_height};
        anchor_tab_window(rect, point);
        win32_require(SetWindowPos(window, HWND_TOP, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
            SWP_NOACTIVATE | SWP_SHOWWINDOW) != 0, "Restore detached tab window");
    }
    void position_tab_window(WINDOWPOS& position) {
        if (!tab_move || !tab_move->entered || tab_move->ending || tab_move->adjusting ||
            tab_move->cancelled || ((position.flags & SWP_NOMOVE) && (position.flags & SWP_NOSIZE))) return;
        tab_move->adjusting = true;
        struct Reset { bool& value; ~Reset() { value = false; } } reset{tab_move->adjusting};
        if (tab_window_arranged(window)) {
            if (tab_move->joined) {
                if (!leave_tab_join()) throw std::logic_error("Cannot return the joined tab for native snap");
                position.flags = (position.flags & ~SWP_HIDEWINDOW) | SWP_SHOWWINDOW;
            }
            return;
        }
        RECT rect{};
        win32_require(GetWindowRect(window, &rect) != 0, "Read moving tab window");
        const auto width = rect.right - rect.left, height = rect.bottom - rect.top;
        if (!(position.flags & SWP_NOMOVE)) { rect.left = position.x; rect.top = position.y; }
        rect.right = rect.left + ((position.flags & SWP_NOSIZE) ? width : position.cx);
        rect.bottom = rect.top + ((position.flags & SWP_NOSIZE) ? height : position.cy);
        const bool was_joined = tab_move->joined.has_value();
        move_tab_window(rect, true);
        position.x = rect.left; position.y = rect.top;
        position.cx = rect.right - rect.left; position.cy = rect.bottom - rect.top;
        position.flags &= ~(SWP_NOMOVE | SWP_NOSIZE);
        if (tab_move->joined)
            position.flags = (position.flags & ~SWP_SHOWWINDOW) | SWP_HIDEWINDOW;
        else if (was_joined)
            position.flags = (position.flags & ~SWP_HIDEWINDOW) | SWP_SHOWWINDOW;
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
        move.detached_width = move.placement.width;
        move.detached_height = move.placement.height;
        if (tab_window_arranged(window) && !IsZoomed(window)) {
            WINDOWPLACEMENT placement{sizeof(placement)};
            win32_require(GetWindowPlacement(window, &placement) != 0, "Read detached window size before native unsnap");
            move.detached_width = placement.rcNormalPosition.right - placement.rcNormalPosition.left;
            move.detached_height = placement.rcNormalPosition.bottom - placement.rcNormalPosition.top;
        }
        BOOL full_drag{};
        win32_require(SystemParametersInfoW(SPI_GETDRAGFULLWINDOWS, 0, &full_drag, 0) != 0, "Read native window drag mode");
        move.full_drag = full_drag != FALSE;
        move.activate = GetForegroundWindow() == window;
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
        SendMessageW(window, WM_NCLBUTTONDOWN, HTCAPTION, MAKELPARAM(point.x, point.y));
        tab_move->ending = true;
        clear_tab_indicator();
        if (closing) return;
        if (!tab_move->cancelled && tab_move->joined && tab_window_arranged(window)) {
            if (!leave_tab_join()) throw std::logic_error("Cannot return the joined tab after native snap");
            win32_require(SetWindowPos(window, HWND_TOP, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW) != 0, "Show natively arranged tab window");
        }
        bool merged{};
        std::shared_ptr<Impl> destination;
        if (tab_move->cancelled) {
            if (!leave_tab_join()) throw std::logic_error("Cannot return the joined tab after cancellation");
            notify_tab_drag(TabDragKind::cancel);
            if (!closing) win32_require(SetWindowPos(window, HWND_TOP, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW) != 0, "Restore cancelled tab window");
        } else {
            win32_require(GetCursorPos(&point) != 0, "Read released tab position");
            auto target = current_drag_tab() && !tab_window_arranged(window) ? tab_drop_target(point) : std::nullopt;
            if (tab_move->joined && (!target || target->host != tab_move->joined->host ||
                target->strip != tab_move->joined->strip)) {
                if (!leave_tab_join()) throw std::logic_error("Cannot return the tab from its stale drop target");
                show_detached_tab(point);
                target = tab_drop_target(point);
            }
            if (target) {
                merged = notify_tab_drag(TabDragKind::drop, target->host->public_window, target->strip, target->index);
                if (merged) destination = target->host;
            }
            if (!merged && tab_move->joined) {
                if (!leave_tab_join()) throw std::logic_error("Cannot return the tab after a rejected drop");
                show_detached_tab(point);
            }
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
        if (destination && destination->window && !destination->closing)
            win32_require(SetWindowPos(destination->window, HWND_TOP, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | (tab_move->activate ? 0 : SWP_NOACTIVATE)) != 0, "Finish tab merge");
    }
