// Included inside Window::Impl. Bind during collect(), before its existing-peer
// return. Translate after native IME checks and before application shortcuts.
std::weak_ptr<MenuBar> active_menu_bar;
std::weak_ptr<CommandSurface> active_menu_bar_surface;
HWND menu_bar_return_focus{};

std::shared_ptr<MenuBar> menu_bar_owner(const Peer& peer) const {
    const auto* heading = dynamic_cast<const MenuBar::Heading*>(peer.control.get());
    if (!heading || !peer.parent) return {};
    auto bar = std::dynamic_pointer_cast<MenuBar>(peer.parent->control);
    return bar && bar->id() == heading->owner_id() &&
        bar->command_button(heading->command_id()) == peer.control ? bar : nullptr;
}

void bind_menu_bar(const std::shared_ptr<Control>& control) {
    const auto bar = std::dynamic_pointer_cast<MenuBar>(control);
    if (!bar) return;
    bar->synchronize_headings();
    std::weak_ptr<Impl> host = shared_from_this();
    bar->set_open_handler([host, weak = std::weak_ptr<MenuBar>(bar)](CommandId id) {
        const auto self = host.lock();
        const auto value = weak.lock();
        if (self && value) self->open_menu_bar(value, id, true);
    });
}

bool menu_bar_popup_anchor(const Peer& peer) const {
    const auto bar = active_menu_bar.lock();
    const auto surface = active_menu_bar_surface.lock();
    const auto* heading = dynamic_cast<const MenuBar::Heading*>(peer.control.get());
    return bar && surface && surface->popup()->is_open() && heading &&
        heading->owner_id() == bar->id() && surface->menu()->parent() == heading->command_id();
}

// Call from WM_SETFOCUS before changing last_focus.
void menu_bar_focused(Peer& peer, HWND previous) {
    const auto bar = menu_bar_owner(peer);
    if (!bar) return;
    bool from_menu{};
    for (const auto& candidate : peers) if (candidate->window == previous) {
        from_menu = menu_bar_owner(*candidate) == bar;
        // The closed entry is already erased when dismiss_popup restores focus.
        if (popup_owner(candidate.get()) && active_menu_bar.lock() == bar) from_menu = true;
        break;
    }
    if (!from_menu && previous && previous != peer.window && IsChild(window, previous))
        menu_bar_return_focus = previous;
    active_menu_bar = bar;
    bar->set_current(static_cast<MenuBar::Heading&>(*peer.control).command_id());
}

void menu_bar_closed(const std::shared_ptr<MenuBar>& bar, const std::shared_ptr<CommandSurface>& surface,
    PopupDismissReason reason) {
    surface->cancel();
    if (active_menu_bar_surface.lock() != surface) return;
    active_menu_bar_surface.reset();
    if (bar->expanded() == surface->menu()->parent()) bar->set_expanded({});
    if (reason == PopupDismissReason::outside && !closing && window) {
        // Light dismissal runs before the new pointer target receives focus.
        if (const auto heading = bar->heading(surface->menu()->parent()))
            if (auto* peer = find_peer(heading.get()); peer && enabled(*peer) && onscreen(*peer)) focus(*peer, false);
    }
}

void open_menu_bar(const std::shared_ptr<MenuBar>& bar, CommandId id, bool toggle = false) {
    if (!ready || closing || !window || !bar) return;
    auto heading = bar->heading(id);
    auto* peer = heading ? find_peer(heading.get()) : nullptr;
    const auto snapshot = bar->commands();
    if (!peer || !snapshot || !heading->current_submenu() || !enabled(*peer) || !onscreen(*peer)) return;
    const auto existing = active_menu_bar_surface.lock();
    const bool same = existing && existing->popup()->is_open() && active_menu_bar.lock() == bar &&
        existing->menu()->parent() == id && existing->menu()->commands() == snapshot;
    if (same) {
        if (toggle) dismiss_popup(*existing->popup(), PopupDismissReason::cancel);
        return;
    }
    if (existing && existing->popup()->is_open())
        dismiss_popup(*existing->popup(), PopupDismissReason::cancel, false);
    if (closing || !window || bar->commands() != snapshot) return;
    peer = find_peer(heading.get());
    if (!peer || !enabled(*peer) || !onscreen(*peer)) return;
    // Keep a containing popup, but remove unrelated children in its scope.
    dismiss_above(popup_owner(peer), PopupDismissReason::cancel);
    if (closing || !window || bar->commands() != snapshot || !heading->current_submenu()) return;
    const auto previous = GetFocus();
    if (active_menu_bar.lock() != bar) {
        active_menu_bar = bar;
        menu_bar_return_focus = previous;
    }
    if (!bar->set_current(id) || !focus(*peer, false)) return;
    auto surface = std::make_shared<CommandSurface>(heading->display_label(), false);
    surface->set_commands(snapshot, id);
    surface->popup()->set_placement(PopupPlacement::below);
    std::weak_ptr<MenuBar> weak = bar;
    std::weak_ptr<MenuBar::Heading> weak_heading = heading;
    surface->set_current([weak, weak_heading, snapshot, id] {
        const auto owner = weak.lock();
        const auto value = weak_heading.lock();
        return owner && value && owner->commands() == snapshot && owner->heading(id) == value &&
            owner->enabled() && owner->visible() && value->enabled() && value->visible() &&
            snapshot->enabled(id) && owner->expanded() == id;
    });
    std::weak_ptr<Impl> host = shared_from_this();
    surface->popup()->on_dismiss([host, weak, source = std::weak_ptr<CommandSurface>(surface)](PopupDismissReason reason) {
        const auto self = host.lock();
        const auto owner = weak.lock();
        const auto value = source.lock();
        if (self && owner && value) self->menu_bar_closed(owner, value, reason);
        else if (value) value->cancel();
    });
    active_menu_bar_surface = surface;
    bar->set_expanded(id);
    try {
        show_commands(surface, *heading);
    } catch (...) {
        if (active_menu_bar_surface.lock() == surface) {
            active_menu_bar_surface.reset();
            bar->set_expanded({});
        }
        throw;
    }
}

bool menu_bar_expand(Peer& peer, bool expand) {
    const auto bar = menu_bar_owner(peer);
    if (!bar || !enabled(peer) || !onscreen(peer)) return false;
    const auto id = static_cast<MenuBar::Heading&>(*peer.control).command_id();
    if (expand) open_menu_bar(bar, id);
    else if (const auto surface = active_menu_bar_surface.lock();
        surface && active_menu_bar.lock() == bar && surface->menu()->parent() == id)
        dismiss_popup(*surface->popup(), PopupDismissReason::cancel);
    return true;
}

void leave_menu_bar(bool restore) {
    const auto destination = menu_bar_return_focus;
    const auto surface = active_menu_bar_surface.lock();
    if (surface && surface->popup()->is_open()) dismiss_popup(*surface->popup(), PopupDismissReason::cancel);
    active_menu_bar.reset();
    active_menu_bar_surface.reset();
    menu_bar_return_focus = nullptr;
    if (restore && window && !closing && IsWindow(destination) && IsChild(window, destination) &&
        IsWindowVisible(destination) && IsWindowEnabled(destination)) SetFocus(destination);
}

bool translate_menu_bar(MSG& msg) {
    if ((msg.message != WM_KEYDOWN && msg.message != WM_SYSKEYDOWN) ||
        (msg.hwnd != window && !IsChild(window, msg.hwnd))) return false;
    const bool control = GetKeyState(VK_CONTROL) < 0;
    const bool shift = GetKeyState(VK_SHIFT) < 0;
    const bool alt = msg.message == WM_SYSKEYDOWN || GetKeyState(VK_MENU) < 0;
    Peer* target{};
    for (const auto& peer : peers) if (peer->window == msg.hwnd) { target = peer.get(); break; }
    auto bar = target ? menu_bar_owner(*target) : nullptr;
    const auto surface = active_menu_bar_surface.lock();
    const bool menu_open = surface && surface->popup()->is_open();
    const auto candidate_bar = [&]() -> std::shared_ptr<MenuBar> {
        for (const auto& peer : peers) {
            const auto value = std::dynamic_pointer_cast<MenuBar>(peer->control);
            if (value && enabled(*peer) && onscreen(*peer) && in_top_popup(*peer) && value->adjacent(0, 1)) return value;
        }
        return {};
    };
    if (msg.wParam == VK_F10 && !control && !shift && !alt) {
        if (msg.lParam & (1LL << 30)) return true;
        if (bar || menu_open) { leave_menu_bar(true); return true; }
        bar = candidate_bar();
        if (!bar) return false;
        auto heading = bar->current() ? bar->heading(*bar->current()) : nullptr;
        if (!heading || !heading->enabled() || !heading->visible()) heading = bar->adjacent(0, 1);
        if (auto* next = heading ? find_peer(heading.get()) : nullptr) {
            menu_bar_return_focus = GetFocus();
            active_menu_bar = bar;
            focus(*next, false);
            return true;
        }
        return false;
    }
    if (alt && !control && msg.wParam != VK_MENU && msg.wParam != VK_F10) {
        if (!bar) bar = menu_open ? active_menu_bar.lock() : candidate_bar();
        if (!bar) return false;
        const auto character = MapVirtualKeyExW(static_cast<UINT>(msg.wParam), MAPVK_VK_TO_CHAR, GetKeyboardLayout(0));
        if (character & 0x80000000u) return false;
        const auto heading = bar->mnemonic_heading(static_cast<wchar_t>(character));
        if (!heading) return false;
        if (!(msg.lParam & (1LL << 30))) open_menu_bar(bar, heading->command_id());
        return true;
    }
    if (control || alt || (shift && msg.wParam != VK_TAB)) return false;
    bool menu_target{};
    if (menu_open && target) {
        for (const auto& entry : popups) if (entry.popup->id() == popup_owner(target) && entry.commands) {
            // A child surface inherits the root's current predicate.
            menu_target = entry.commands->menu()->commands() == surface->menu()->commands() &&
                entry.commands->menu().get() == target->control.get();
            break;
        }
    }
    if (!bar && menu_target) bar = active_menu_bar.lock();
    if (!bar || (!menu_target && (!target || !enabled(*target) || !onscreen(*target)))) return false;
    if (msg.wParam == VK_TAB) { leave_menu_bar(false); traverse(shift); return true; }
    if (msg.wParam == VK_ESCAPE && !menu_open) { leave_menu_bar(true); return true; }
    if (menu_target) {
        if (msg.wParam != VK_LEFT && msg.wParam != VK_RIGHT) return false;
        auto* menu = static_cast<CommandMenu*>(target->control.get());
        if (msg.wParam == VK_LEFT && menu != surface->menu().get()) return false;
        if (msg.wParam == VK_RIGHT) {
            const auto selected = menu->selection().focused();
            const auto* record = selected ? menu->commands()->find(selected->id) : nullptr;
            if (record && record->kind == CommandKind::submenu && menu->commands()->enabled(record->id)) return false;
        }
    }
    if (msg.wParam == VK_LEFT || msg.wParam == VK_RIGHT || msg.wParam == VK_HOME || msg.wParam == VK_END) {
        const auto next = msg.wParam == VK_HOME ? bar->adjacent(0, 1) :
            msg.wParam == VK_END ? bar->adjacent(0, -1) :
            bar->adjacent(bar->current().value_or(0), msg.wParam == VK_LEFT ? -1 : 1);
        if (next) {
            if (menu_open) open_menu_bar(bar, next->command_id());
            else if (auto* peer = find_peer(next.get())) { bar->set_current(next->command_id()); focus(*peer, false); }
        }
        return true;
    }
    if (!menu_target && (msg.wParam == VK_DOWN || msg.wParam == VK_RETURN || msg.wParam == VK_SPACE)) {
        if (!(msg.lParam & (1LL << 30)) && bar->current()) open_menu_bar(bar, *bar->current());
        return true;
    }
    return false;
}

// Direct SendMessage keyboard input must use the same path as the message loop.
bool menu_bar_key(Peer& peer, WPARAM key_code, LPARAM flags) {
    MSG message{};
    message.hwnd = peer.window;
    message.message = WM_KEYDOWN;
    message.wParam = key_code;
    message.lParam = flags;
    return translate_menu_bar(message);
}

// Call near the start of WM_MOUSEMOVE, using the existing inside() hit test.
bool menu_bar_hover(Peer& peer, bool inside) {
    if (!inside || GetCapture() || !enabled(peer) || !onscreen(peer)) return false;
    const auto bar = menu_bar_owner(peer);
    const auto surface = active_menu_bar_surface.lock();
    if (!bar || active_menu_bar.lock() != bar || !surface || !surface->popup()->is_open()) return false;
    const auto id = static_cast<MenuBar::Heading&>(*peer.control).command_id();
    if (surface->menu()->parent() == id) return false;
    open_menu_bar(bar, id);
    return true;
}
