    bool has_content_picking() const {
        return std::any_of(content_picking.begin(), content_picking.end(),
            [](const auto& item) { return item.second.enabled && !item.second.host.expired(); });
    }
    ContentHighlightResult content_highlight_geometry(const ContentHost& host, const ContentPicking& state,
        std::uint32_t target_key, inspection::Outline& ring) const {
        using Result = ContentHighlightResult;
        const auto found = std::find_if(state.registration.targets.begin(), state.registration.targets.end(),
            [target_key](const auto& target) { return target.key == target_key; });
        const auto target = found == state.registration.targets.end() ? nullptr : found->element.lock();
        if (!target || !ready || closing || layout_pending || syncing || replacing ||
            !IsWindowVisible(window) || IsIconic(window)) return Result::not_visible;
        GUITHREADINFO info{sizeof(info)};
        if (native_file_dialog || !IsWindowEnabled(window) || !popups.empty() || tooltip_shown ||
            (GetGUIThreadInfo(owner_thread, &info) && (info.flags & (GUI_INMENUMODE | GUI_INMOVESIZE))))
            return Result::unsupported_surface;
        const auto path = inspection::host_path(root, host, root->bounds(), true);
        const auto target_clip = inspection::element_clip(host.content(), *target, root->bounds());
        if (!path || !target_clip) return Result::not_visible;
        try {
            validate_inspection_ancestors(*path);
            inspection::supported(host.content(), &host);
            validate_inspection_region(host);
            validate_inspection_native(host);
        } catch (const std::invalid_argument&) { return Result::unsupported_surface; }
        RECT client{};
        win32_require(GetClientRect(window, &client) != 0, "Read highlight viewport");
        const float scale = dpi / 96.0f;
        const auto clip = inspection::intersect(inspection::intersect(path->clip, host.bounds()),
            inspection::intersect(*target_clip, {0, 0, client.right / scale, client.bottom / scale}));
        ring = inspection::outline(target->bounds(), clip, scale);
        if (!ring.visible()) return Result::not_visible;
        std::vector<HWND> surfaces;
        const auto collect = [](HWND hwnd, LPARAM data) -> BOOL {
            reinterpret_cast<std::vector<HWND>*>(data)->push_back(hwnd); return TRUE;
        };
        EnumChildWindows(window, collect, reinterpret_cast<LPARAM>(&surfaces));
        EnumThreadWindows(owner_thread, collect, reinterpret_cast<LPARAM>(&surfaces));
        for (const auto hwnd : surfaces) {
            if (hwnd == window || !IsWindowVisible(hwnd) ||
                (!IsChild(window, hwnd) && GetAncestor(hwnd, GA_ROOTOWNER) != window)) continue;
            const auto peer = std::find_if(peers.begin(), peers.end(),
                [hwnd](const auto& value) { return value->window == hwnd || value->caption == hwnd; });
            if (peer != peers.end() && !(*peer)->native() && !(*peer)->list && !(*peer)->runtime &&
                hwnd != (*peer)->caption) continue;
            RECT bounds{};
            win32_require(GetWindowRect(hwnd, &bounds) != 0, "Read opaque highlight surface");
            MapWindowPoints(nullptr, window, reinterpret_cast<POINT*>(&bounds), 2);
            if (ring.overlaps({bounds.left / scale, bounds.top / scale,
                (bounds.right - bounds.left) / scale, (bounds.bottom - bounds.top) / scale}))
                return peer == peers.end() ? Result::unsupported_surface : Result::occluded_native;
        }
        return Result::applied;
    }
    void paint_content_highlights() {
        for (const auto& [id, state] : content_picking) if (state.highlight) {
            const auto host = state.host.lock();
            inspection::Outline ring;
            if (host && content_highlight_geometry(*host, state, *state.highlight, ring) == ContentHighlightResult::applied)
                for (const auto segment : ring.segments)
                    if (segment.width > 0 && segment.height > 0) drawing.fill(segment, palette.accent);
        }
    }
    void inspection_api_ready() const {
        if (GetCurrentThreadId() != owner_thread)
            throw std::logic_error("Inspect content on the window UI thread");
        if (!ready || closing || !window)
            throw std::logic_error("Pointer inspection requires a running window");
        if (input_depth || replacing || syncing || native_file_dialog)
            throw std::logic_error("Post pointer inspection outside native input or modal callbacks");
    }
    void inspection_idle() const {
        if (native_file_dialog || !IsWindowEnabled(window))
            throw std::logic_error("Close the native modal dialog before inspecting content");
        if (!popups.empty())
            throw std::invalid_argument("Pointer inspection does not support open popup surfaces");
        GUITHREADINFO info{sizeof(info)};
        if (GetGUIThreadInfo(owner_thread, &info) &&
            (info.flags & (GUI_INMENUMODE | GUI_INMOVESIZE | GUI_POPUPMENUMODE | GUI_SYSTEMMENUMODE)))
            throw std::logic_error("Finish native menu or move input before inspecting content");
        if (GetCapture()) throw std::logic_error("Release native pointer capture before inspecting content");
        for (const auto& peer : peers)
            if ((peer->edit && peer->edit->composing()) || (peer->document && peer->document->composing()))
                throw std::logic_error("Finish native text composition before inspecting content");
    }
    void validate_inspection_ancestors(const inspection::HostPath& path) const {
        for (const auto& ancestor : path.ancestors) {
            inspection::supported_node(ancestor);
            if (dynamic_cast<ContentHost*>(ancestor.get()) || dynamic_cast<AdaptiveLayout*>(ancestor.get()) ||
                dynamic_cast<RuntimeHost*>(ancestor.get()) || dynamic_cast<Popup*>(ancestor.get()) ||
                dynamic_cast<FileList*>(ancestor.get()) || dynamic_cast<DateTimePicker*>(ancestor.get()))
                throw std::invalid_argument("Pointer inspection does not support this native or overlay ancestry");
        }
    }
    void validate_inspection_region(const ContentHost& host) const {
        const auto path = inspection::host_path(root, host, root->bounds(), true);
        if (!path) return;
        const auto area = inspection::intersect(path->clip, host.bounds());
        for (const auto& ancestor : path->ancestors)
            for (const auto& child : inspection::children(ancestor, root->bounds(), true)) {
                if (inspection::host_path(child.element, host, root->bounds(), false)) continue;
                if (const auto control = std::dynamic_pointer_cast<Control>(child.element); control && !control->visible()) continue;
                const auto overlap = inspection::intersect(area, inspection::intersect(child.clip, child.element->bounds()));
                if (overlap.width > 0 && overlap.height > 0)
                    throw std::invalid_argument("Pointer inspection does not support overlapping content-host sibling surfaces");
            }
    }
    void validate_active_inspection() const {
        if (replacing) return;
        for (const auto& [id, state] : content_picking) if (state.enabled) {
            if (const auto host = state.host.lock()) {
                const auto path = inspection::host_path(root, *host, root->bounds(), false);
                if (!path) throw std::invalid_argument("Pointer inspection host is no longer attached");
                validate_inspection_ancestors(*path);
                inspection::supported(host->content(), host.get());
                validate_inspection_region(*host);
                validate_inspection_native(*host);
            }
        }
    }
    const Control* inspection_control(const Peer& peer) const {
        const auto owner = peer.clear_owner.lock();
        return owner ? owner.get() : peer.control.get();
    }
    void validate_inspection_native(const ContentHost& host) const {
        std::set<std::uint64_t> members;
        content_ids(host.content(), members);
        const auto known = [&](HWND hwnd) {
            return std::any_of(peers.begin(), peers.end(), [&](const auto& peer) {
                return (peer->window == hwnd || peer->caption == hwnd) &&
                    members.contains(inspection_control(*peer)->id());
            });
        };
        for (const auto& peer : peers) if (members.contains(inspection_control(*peer)->id())) {
            const auto expected = peer->parent ? peer->parent->window : window;
            DWORD_PTR subclass{};
            if (!IsWindow(peer->window) || GetParent(peer->window) != expected ||
                !IsChild(window, peer->window) ||
                !GetWindowSubclass(peer->window, navigation_procedure, 3, &subclass) ||
                subclass != reinterpret_cast<DWORD_PTR>(peer.get()) ||
                (peer->caption && (GetParent(peer->caption) != expected ||
                    !GetWindowSubclass(peer->caption, navigation_procedure, 3, &subclass) ||
                    subclass != reinterpret_cast<DWORD_PTR>(peer.get()))))
                throw std::invalid_argument("Pointer inspection requires the original native ancestry");
            std::vector<HWND> descendants;
            EnumChildWindows(peer->window, [](HWND hwnd, LPARAM parameter) -> BOOL {
                reinterpret_cast<std::vector<HWND>*>(parameter)->push_back(hwnd);
                return TRUE;
            }, reinterpret_cast<LPARAM>(&descendants));
            for (const auto descendant : descendants) if (!known(descendant))
                throw std::invalid_argument("Pointer inspection encountered an unproven native descendant");
        }
        std::vector<HWND> all;
        EnumChildWindows(window, [](HWND hwnd, LPARAM parameter) -> BOOL {
            reinterpret_cast<std::vector<HWND>*>(parameter)->push_back(hwnd);
            return TRUE;
        }, reinterpret_cast<LPARAM>(&all));
        for (const auto hwnd : all) {
            if (!IsWindowVisible(hwnd) || std::any_of(peers.begin(), peers.end(), [hwnd](const auto& peer) {
                return peer->window == hwnd || peer->caption == hwnd;
            })) continue;
            RECT rect{};
            GetWindowRect(hwnd, &rect);
            MapWindowPoints(nullptr, window, reinterpret_cast<POINT*>(&rect), 2);
            const Rect bounds{rect.left * 96.0f / dpi, rect.top * 96.0f / dpi,
                (rect.right - rect.left) * 96.0f / dpi, (rect.bottom - rect.top) * 96.0f / dpi};
            const auto overlap = inspection::intersect(host.bounds(), bounds);
            if (overlap.width > 0 && overlap.height > 0)
                throw std::invalid_argument("Pointer inspection intersects an unproven native surface");
        }
    }
    static bool inspection_window_contains(HWND hwnd, POINT screen) {
        RECT bounds{};
        if (!GetWindowRect(hwnd, &bounds) || !PtInRect(&bounds, screen)) return false;
        const auto region = CreateRectRgn(0, 0, 0, 0);
        win32_require(region != nullptr, "Read pointer inspection clipping");
        const auto type = GetWindowRgn(hwnd, region);
        const bool inside = type == ERROR || PtInRegion(region, screen.x - bounds.left, screen.y - bounds.top);
        DeleteObject(region);
        return inside;
    }
    HWND inspection_native_at(HWND parent, POINT screen, Point position) const {
        RECT client{};
        if (!GetClientRect(parent, &client)) return parent;
        MapWindowPoints(parent, nullptr, reinterpret_cast<POINT*>(&client), 2);
        if (!PtInRect(&client, screen)) return parent;
        // GW_CHILD/GW_HWNDNEXT is native front-to-back sibling hit order.
        for (auto child = GetWindow(parent, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
            if (!IsWindowVisible(child) || !inspection_window_contains(child, screen)) continue;
            const auto peer = std::find_if(peers.begin(), peers.end(), [child](const auto& value) {
                return value->window == child || value->caption == child;
            });
            if (peer != peers.end()) {
                const auto clip = inspection::element_clip(root, *inspection_control(**peer), root->bounds());
                if (!clip || !inspection::contains(*clip, position)) continue;
            }
            return inspection_native_at(child, screen, position);
        }
        return parent;
    }
    inspection::Hit inspection_hit(const ContentHost& host, Point position,
        const inspection::Registration& registration) const {
        const auto path = inspection::host_path(root, host, root->bounds(), true);
        if (!path || !inspection::contains(inspection::intersect(path->clip, host.bounds()), position)) return {};
        POINT screen{static_cast<LONG>(std::lround(position.x * dpi / 96.0f)),
            static_cast<LONG>(std::lround(position.y * dpi / 96.0f))};
        ClientToScreen(window, &screen);
        const auto native = inspection_native_at(window, screen, position);
        const Control* target{};
        if (native != window) {
            const auto found = std::find_if(peers.begin(), peers.end(), [&](const auto& peer) {
                return peer->window == native || peer->caption == native;
            });
            if (found == peers.end())
                throw std::invalid_argument("Pointer inspection encountered an unproven native surface");
            target = inspection_control(**found);
            const bool ancestor = std::any_of(path->ancestors.begin(), path->ancestors.end(),
                [&](const auto& element) { return element.get() == target; });
            if (ancestor) target = nullptr;
        }
        if (!host.content())
            return !target && inspection::contains(host.bounds(), position) ? inspection::Hit{true, {}} : inspection::Hit{};
        return inspection::hit(host.content(), position, path->clip, registration, target);
    }
    bool inspection_source(const ContentHost& host, HWND source) const {
        if (source == window) return false;
        const auto found = std::find_if(peers.begin(), peers.end(), [&](const auto& peer) {
            return peer->window == source || peer->caption == source;
        });
        if (found == peers.end()) return false;
        std::set<std::uint64_t> ids;
        content_ids(host.content(), ids);
        return ids.contains(inspection_control(**found)->id());
    }
    std::optional<LRESULT> inspect_pointer(HWND source, UINT message, WPARAM wparam, LPARAM lparam) {
        if (message == WM_CANCELMODE || message == WM_POINTERCAPTURECHANGED) {
            for (auto& [id, state] : content_picking) {
                const auto host = state.host.lock();
                if (host && (state.mouse_down || state.pointer_down) &&
                    (source == window || inspection_source(*host, source))) {
                    state.mouse_down = state.pointer_down = false;
                    state.pending.reset();
                }
            }
            return {};
        }
        const bool down = message == WM_LBUTTONDOWN || message == WM_LBUTTONDBLCLK ||
            message == WM_NCLBUTTONDOWN || message == WM_NCLBUTTONDBLCLK;
        const bool up = message == WM_LBUTTONUP || message == WM_NCLBUTTONUP;
        const bool pointer = message == WM_POINTERDOWN || message == WM_POINTERUP || message == WM_POINTERUPDATE;
        const bool activate = message == WM_MOUSEACTIVATE || message == WM_POINTERACTIVATE;
        const bool move = message == WM_MOUSEMOVE && (wparam & MK_LBUTTON);
        const bool active = std::any_of(content_picking.begin(), content_picking.end(), [](const auto& entry) {
            return entry.second.enabled || entry.second.mouse_down || entry.second.pointer_down;
        });
        if ((!down && !up && !pointer && !activate && !move) || !active ||
            !ready || closing || (source != window && !IsChild(window, source))) return {};
        if (pointer && !IS_POINTER_PRIMARY_WPARAM(wparam)) return {};
        validate_active_inspection();
        POINT pixel{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        if (activate) {
            const auto packed = GetMessagePos();
            pixel = {GET_X_LPARAM(packed), GET_Y_LPARAM(packed)};
            ScreenToClient(window, &pixel);
        } else if (pointer || message == WM_NCLBUTTONDOWN || message == WM_NCLBUTTONUP || message == WM_NCLBUTTONDBLCLK) {
            ScreenToClient(window, &pixel);
        } else MapWindowPoints(source, window, &pixel, 1);
        const Point position{pixel.x * 96.0f / dpi, pixel.y * 96.0f / dpi};
        const auto extra = static_cast<ULONG_PTR>(GetMessageExtraInfo());
        const bool promoted = (extra & 0xffffff00) == 0xff515700;
        const bool begin_pointer = message == WM_POINTERDOWN && IS_POINTER_FIRSTBUTTON_WPARAM(wparam);
        const bool begin_mouse = down && !promoted;
        for (auto& [id, state] : content_picking) {
            const auto host = state.host.lock();
            if (!host) continue;
            if (begin_mouse || activate) state.mouse_down = false;
            if (begin_pointer || activate) state.pointer_down = false;
            if (!state.enabled && !state.mouse_down && !state.pointer_down) continue;
            const bool owned = inspection_source(*host, source);
            if (const auto capture = GetCapture(); capture && !inspection_source(*host, capture)) {
                state.mouse_down = state.pointer_down = false;
                continue;
            }
            if (GetCapture() && !state.mouse_down && !state.pointer_down) {
                if (owned) throw std::logic_error("Finish captured native input before pointer inspection");
                continue;
            }
            const auto hit = !state.enabled || replacing || syncing ? inspection::Hit{} :
                inspection_hit(*host, position, state.registration);
            if (!owned && !hit.matched && !state.mouse_down && !state.pointer_down) continue;
            if (activate) return message == WM_MOUSEACTIVATE ? MA_NOACTIVATE : PA_NOACTIVATE;
            if (replacing || syncing) return 0;
            if (up) state.mouse_down = false;
            if (message == WM_POINTERUP) state.pointer_down = false;
            const bool fresh = begin_pointer ? !state.pointer_down : begin_mouse && !state.mouse_down;
            if (begin_pointer) state.pointer_down = true;
            if (begin_mouse) state.mouse_down = true;
            if (fresh && hit.key && !state.pending) {
                state.pending = hit.key;
                win32_require(SetEvent(wake->event) != 0, "Schedule content inspection callback");
            }
            return 0;
        }
        return {};
    }
    void prune_inspection_hosts() {
        std::set<std::uint64_t> live;
        content_ids(root, live);
        std::erase_if(content_picking, [&](const auto& entry) {
            return entry.second.host.expired() || !live.contains(entry.first);
        });
    }
    void deliver_content_picks() {
        if (!ready || closing || input_depth || replacing || syncing || native_file_dialog) return;
        std::vector<std::pair<std::uint64_t, std::uint64_t>> deliveries;
        for (const auto& [id, state] : content_picking)
            if (state.enabled && state.pending) deliveries.emplace_back(id, state.generation);
        for (const auto [id, generation] : deliveries) {
            if (!ready || closing) break;
            const auto found = content_picking.find(id);
            if (found == content_picking.end()) continue;
            auto& state = found->second;
            if (!state.enabled || state.generation != generation || !state.pending || state.host.expired()) continue;
            const auto target_key = std::exchange(state.pending, {});
            const auto callback = state.registration.picked;
            if (callback) callback(*target_key);
        }
    }
