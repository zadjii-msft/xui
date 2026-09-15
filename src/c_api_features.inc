namespace {
void prepare_tree(const std::shared_ptr<Node>& n);
void prepare_miller(const std::shared_ptr<Node>& n);
bool dispatch_menu(const std::shared_ptr<Node>& node, uint32_t kind, uint64_t value = 0) {
    auto state = node->owner;
    if (!node->menu_callback || state->closed || state->callback_failure) return false;
    ++state->callbacks;
    const xui_event event{sizeof(xui_event), kind, node->handle, value};
    xui_status status{};
    try { status = node->menu_callback(node->menu_context, &event); }
    catch (...) { status = XUI_CALLBACK_FAILED; }
    --state->callbacks;
    if (status) { state->callback_failure = status; state->window->close(); return false; }
    return !state->closed && !state->callback_failure;
}
template<class T> T& feature(const std::shared_ptr<Node>& n) {
    auto* value = dynamic_cast<T*>(n->element.get());
    require(value != nullptr, XUI_WRONG_KIND, "Wrong feature kind.");
    return *value;
}
template<class T> std::shared_ptr<T> resource(const std::shared_ptr<Node>& n, uint32_t kind) {
    require(n->kind == kind, XUI_WRONG_KIND, "Wrong composition kind.");
    return std::static_pointer_cast<T>(n->resource);
}
uint64_t number_bits(double v) { return std::bit_cast<uint64_t>(v); }
void wire_feature(const std::shared_ptr<Node>& n) {
    std::weak_ptr<Node> weak = n;
    switch (n->kind) {
    case XUI_RANGE_INPUT:
        as<xui::RangeInput>(n).on_change([weak](double v) { dispatch(weak, XUI_CHANGE, number_bits(v)); });
        as<xui::RangeInput>(n).on_preview([weak](double v) { dispatch(weak, XUI_PREVIEW, number_bits(v)); });
        as<xui::RangeInput>(n).on_cancel([weak](double v) { dispatch(weak, XUI_CANCEL, number_bits(v)); }); break;
    case XUI_NUMERIC_INPUT: as<xui::NumericInput>(n).on_change([weak](double v) { dispatch(weak, XUI_CHANGE, number_bits(v)); }); break;
    case XUI_RADIO_GROUP: as<xui::RadioGroup>(n).on_change([weak](uint64_t v) { dispatch(weak, XUI_SELECTION, v); }); break;
    case XUI_COMBO_BOX: as<xui::ComboBox>(n).on_change([weak](uint64_t v) { dispatch(weak, XUI_SELECTION, v); }); break;
    case XUI_EXPANDER: as<xui::Expander>(n).on_change([weak](bool v) { dispatch(weak, XUI_CHANGE, v); }); break;
    case XUI_POPUP: as<xui::Popup>(n).on_dismiss([weak](xui::PopupDismissReason v) { dispatch(weak, XUI_DISMISS, static_cast<uint64_t>(v)); }); break;
    case XUI_MULTILINE_TEXT: case XUI_RICH_TEXT:
        feature<xui::DocumentText>(n).on_change([weak](const std::wstring&) { dispatch(weak, XUI_CHANGE); }); break;
    case XUI_PASSWORD_INPUT: as<xui::PasswordInput>(n).on_change([weak] { dispatch(weak, XUI_CHANGE); }); break;
    case XUI_DATE_TIME_PICKER: as<xui::DateTimePicker>(n).on_change([weak](xui::DateTimeValue) { dispatch(weak, XUI_CHANGE); }); break;
    case XUI_COLOR_PICKER: as<xui::ColorPicker>(n).on_change([weak](xui::RgbaColor v) {
        dispatch(weak, XUI_CHANGE, v.red | uint64_t(v.green) << 8 | uint64_t(v.blue) << 16 | uint64_t(v.alpha) << 24);
    }); break;
    case XUI_INLINE_STATUS: as<xui::InlineStatus>(n).on_dismiss([weak] { dispatch(weak, XUI_DISMISS); }); break;
    case XUI_CONTENT_DIALOG: resource<xui::ContentDialog>(n, XUI_CONTENT_DIALOG)->on_result([weak](xui::DialogResult v) { dispatch(weak, XUI_DISMISS, static_cast<uint64_t>(v)); }); break;
    case XUI_VECTOR_CANVAS: case XUI_MAP_VIEW: feature<xui::VectorCanvas>(n).on_select([weak](uint64_t id) { dispatch(weak, XUI_SELECTION, id); }); break;
    case XUI_MEDIA_PLAYBACK: case XUI_WEB_CONTENT:
        feature<xui::RuntimeHost>(n).on_state([weak](xui::HostState state) { dispatch(weak, XUI_CHANGE, static_cast<uint64_t>(state)); }); break;
    case XUI_ITEMS_VIEW: case XUI_TREE_VIEW:
        if (!n->miller_owner.expired()) break;
        feature<xui::VirtualCollection>(n).on_selection([weak] { dispatch(weak, XUI_SELECTION); });
        feature<xui::VirtualCollection>(n).on_activate([weak](xui::ItemKey key) { dispatch(weak, XUI_CLICK, key.id); });
        feature<xui::VirtualCollection>(n).on_action([weak](xui::ItemKey key) { dispatch(weak, XUI_ACTION, key.id); }); break;
    case XUI_BREADCRUMB: as<xui::Breadcrumb>(n).on_navigate([weak](xui::ItemKey key) { dispatch(weak, XUI_SELECTION, key.id); }); break;
    case XUI_NAVIGATION_PANE: as<xui::NavigationPane>(n).on_navigate([weak](xui::ItemKey key) { dispatch(weak, XUI_SELECTION, key.id); }); break;
    case XUI_NAVIGATION_VIEW:
        as<xui::NavigationView>(n).on_select([weak](xui::ItemKey key) { dispatch(weak, XUI_SELECTION, key.id); });
        as<xui::NavigationView>(n).on_activate([weak](xui::ItemKey key) { dispatch(weak, XUI_CLICK, key.id); }); break;
    case XUI_MILLER_COLUMNS: break;
    case XUI_TAB_STRIP:
        as<xui::TabStrip>(n).on_select([weak](uint64_t id) { dispatch(weak, XUI_SELECTION, id); });
        as<xui::TabStrip>(n).on_activate([weak](uint64_t id) { dispatch(weak, XUI_CLICK, id); });
        as<xui::TabStrip>(n).on_close([weak](uint64_t id) { dispatch(weak, XUI_CANCEL, id); }); break;
    case XUI_DATA_GRID:
        as<xui::DataGrid>(n).on_select([weak] { dispatch(weak, XUI_SELECTION); });
        as<xui::DataGrid>(n).on_activate([weak] {
            if (auto node = weak.lock()) {
                auto selected = as<xui::DataGrid>(node).selection().focused();
                if (selected) dispatch(weak, XUI_CLICK, selected->id);
            }
        });
        as<xui::DataGrid>(n).on_sort([weak](size_t column, bool descending) { dispatch(weak, XUI_VIEW, column | uint64_t(descending) << 32); });
        as<xui::DataGrid>(n).on_filter_open([weak](size_t column) { dispatch(weak, XUI_FILTER_OPEN, column); }); break;
    case XUI_COMMAND_BAR: case XUI_COMMAND_SURFACE: break; // CommandSet actions dispatch directly.
    case XUI_SPLIT_VIEW:
        as<xui::SplitView>(n).on_expanded([weak](bool expanded) {
            if (auto node = weak.lock()) node->owner->window->post([weak, expanded] {
                if (auto current = weak.lock(); current && as<xui::SplitView>(current).expanded() == expanded)
                    dispatch(weak, XUI_VIEW, expanded);
            });
        }); break;
    default: throw Failure{XUI_WRONG_KIND, "This feature has no events."};
    }
}
void fields(const xui_feature_value& v, unsigned mask) {
    require(v.size == sizeof(v) && v.version == XUI_FEATURE_VERSION, XUI_VERSION_MISMATCH, "Feature value version mismatch.");
    require(std::isfinite(v.a) && std::isfinite(v.b) && std::isfinite(v.c) && std::isfinite(v.d),
        XUI_INVALID_ARGUMENT, "Feature numbers must be finite.");
    require((mask & 1 || v.a == 0) && (mask & 2 || v.b == 0) && (mask & 4 || v.c == 0) &&
        (mask & 8 || v.d == 0) && (mask & 16 || v.first == 0) && (mask & 32 || v.second == 0) &&
        !v.text.reserved && (mask & 64 || (!v.text.data && !v.text.length)),
        XUI_INVALID_ARGUMENT, "Unused fields must be zero.");
}
void limit(uint64_t v, uint64_t maximum) { require(v <= maximum, XUI_INVALID_ARGUMENT, "Value is out of range."); }
void finite_float(double v) { require(std::abs(v) <= 1e7, XUI_INVALID_ARGUMENT, "Layout value is out of range."); }
void selection_boundary(const std::wstring& text, uint64_t index) {
    require(index <= text.size(), XUI_INVALID_ARGUMENT, "Selection exceeds the document.");
    require(!index || index == text.size() || !(text[index - 1] >= 0xd800 && text[index - 1] <= 0xdbff &&
        text[index] >= 0xdc00 && text[index] <= 0xdfff), XUI_INVALID_ARGUMENT, "Selection splits a UTF-16 surrogate pair.");
}
std::shared_ptr<Node> menu_node(xui_handle target) {
    auto node = get(target);
    require(node->kind == XUI_DATA_GRID || dynamic_cast<xui::VirtualCollection*>(node->element.get()),
        XUI_WRONG_KIND, "Context menus require a grid or virtual collection.");
    editable(node->owner);
    return node;
}
std::optional<xui::ItemKey> menu_selection(const std::shared_ptr<Node>& node) {
    return node->kind == XUI_DATA_GRID ? as<xui::DataGrid>(node).selected() :
        feature<xui::VirtualCollection>(node).selection().focused();
}
std::shared_ptr<const xui::CollectionIndex> menu_source(const std::shared_ptr<Node>& node) {
    if (node->kind == XUI_DATA_GRID) return as<xui::DataGrid>(node).source();
    return feature<xui::VirtualCollection>(node).source();
}
}
uint32_t XUI_CALL xui_feature_version() noexcept { return XUI_FEATURE_VERSION; }
xui_status XUI_CALL xui_context_menu_bind(xui_handle target, xui_callback callback, void* context) noexcept {
    return boundary([&] {
        auto node = menu_node(target);
        require(!node->menu_requesting, XUI_BUSY, "Cannot replace a context menu during its request.");
        node->menu_callback = callback; node->menu_context = context;
        node->menu_items.clear(); node->menu_shell_paths.clear(); node->menu_current = {}; ++node->menu_revision;
        control(node).invalidate(xui::Invalidation::paint);
        if (!callback) { control(node).on_context_menu({}); return; }
        std::weak_ptr<Node> weak = node;
        control(node).on_context_menu_content([weak] {
            xui::ContextMenuContent result;
            auto node = weak.lock();
            if (!node) return result;
            require(!node->menu_requesting, XUI_BUSY, "A context menu request cannot reenter.");
            const auto selected = menu_selection(node);
            const auto revision = node->menu_revision;
            const std::weak_ptr<const xui::CollectionIndex> source = menu_source(node);
            const bool source_present = menu_source(node) != nullptr;
            result.current = [weak, selected, revision, source, source_present] {
                if (auto node = weak.lock(); node && !node->owner->closed && !node->owner->callback_failure) {
                    if (node->menu_revision == revision && (!source_present || !source.expired()) &&
                        control(node).visible() && menu_source(node) == source.lock() && menu_selection(node) == selected &&
                        (!selected || (menu_source(node) && menu_source(node)->find(*selected)))) return true;
                }
                OutputDebugStringW(L"XUI: Context menu cancelled because its owner, source, selection, or subscription changed.\n");
                return false;
            };
            node->menu_items.clear(); node->menu_shell_paths.clear(); node->menu_current = result.current;
            node->menu_presentation = xui::ShellMenuPresentation::windows;
            node->menu_requesting = true;
            const bool success = dispatch_menu(node, XUI_REQUEST);
            node->menu_requesting = false; node->menu_current = {};
            if (success) {
                result.items.swap(node->menu_items); result.shell_paths.swap(node->menu_shell_paths);
                result.presentation = node->menu_presentation;
            }
            else { node->menu_items.clear(); node->menu_shell_paths.clear(); }
            return result;
        });
    });
}
xui_status XUI_CALL xui_context_menu_items(xui_handle target, const xui_command_record* commands, uint32_t count) noexcept {
    return boundary([&] {
        auto node = menu_node(target);
        require(node->menu_requesting, XUI_BUSY, "Menu items require an active context menu request.");
        require(count <= XUI_MAX_BATCH && (commands || !count), XUI_INVALID_ARGUMENT, "Invalid context menu span.");
        std::vector<xui::MenuItem> items; items.reserve(count);
        std::set<uint64_t> ids;
        const auto current = node->menu_current;
        std::weak_ptr<Node> weak = node;
        for (uint32_t i = 0; i < count; ++i) {
            const auto& command = commands[i];
            require(command.size == sizeof(command), XUI_VERSION_MISMATCH, "Context command size mismatch.");
            require((command.kind == 0 || command.kind == 2) && !command.parent && !(command.flags & ~7u) && !command.icon,
                XUI_INVALID_ARGUMENT, "Context menus support flat actions and separators.");
            require(command.kind == 2 || (command.id && ids.insert(command.id).second), XUI_INVALID_ARGUMENT, "Invalid or duplicate command ID.");
            require(decode(command.pin_label).empty(), XUI_INVALID_ARGUMENT, "Context menus do not support pin actions.");
            auto label = decode(command.label), hint = decode(command.hint);
            if (!hint.empty()) label += L"\t" + hint;
            items.push_back({std::move(label), [weak, current, id = command.id] {
                if (!current || !current()) return;
                if (auto node = weak.lock()) {
                    dispatch_menu(node, XUI_ACTION, id);
                }
            }, !(command.flags & 1), bool(command.flags & 2), command.kind == 2});
        }
        node->menu_items = std::move(items);
    });
}
xui_status XUI_CALL xui_context_menu_shell_paths(xui_handle target, const xui_string* paths, uint32_t count) noexcept {
    return boundary([&] {
        auto node = menu_node(target);
        require(node->menu_requesting, XUI_BUSY, "Shell paths require an active context menu request.");
        require(count <= 256 && (paths || !count), XUI_INVALID_ARGUMENT, "Invalid Shell path span.");
        require(!count || menu_selection(node).has_value(), XUI_INVALID_ARGUMENT, "Shell paths require a selected row.");
        std::vector<std::wstring> values; values.reserve(count);
        for (uint32_t i = 0; i < count; ++i) {
            auto path = decode(paths[i]);
            require(!path.empty() && path.size() <= 32767, XUI_INVALID_ARGUMENT, "Shell paths require 1..32767 UTF-16 units.");
            values.push_back(std::move(path));
        }
        node->menu_shell_paths = std::move(values);
    });
}
xui_status XUI_CALL xui_context_menu_presentation(xui_handle target, uint32_t presentation) noexcept {
    return boundary([&] {
        auto node = menu_node(target);
        require(node->menu_requesting, XUI_BUSY, "Presentation requires an active context menu request.");
        limit(presentation, 1);
        node->menu_presentation = static_cast<xui::ShellMenuPresentation>(presentation);
    });
}
xui_status XUI_CALL xui_navigation_items(xui_handle target, const xui_navigation_entry* items, uint32_t count) noexcept {
    return xui_navigation_items_visual(target, items, nullptr, count);
}
xui_status XUI_CALL xui_navigation_items_visual(xui_handle target, const xui_navigation_entry* items,
    const xui_item_visual* visuals, uint32_t count) noexcept {
    return boundary([&] {
        auto n = get(target, XUI_NAVIGATION_VIEW); editable(n->owner);
        require(count <= XUI_MAX_BATCH && (items || !count), XUI_INVALID_ARGUMENT, "Invalid navigation item span.");
        std::vector<xui::NavigationItem> entries; entries.reserve(count);
        for (uint32_t i = 0; i < count; ++i) {
            const auto& item = items[i];
            require(item.size == sizeof(item), XUI_VERSION_MISMATCH, "Navigation item size mismatch.");
            require(item.id && !(item.flags & ~7u), XUI_INVALID_ARGUMENT, "Invalid navigation item.");
            xui::NavigationItem entry{};
            entry.key = {item.id, 0};
            if (item.parent) entry.parent = xui::ItemKey{item.parent, 0};
            entry.label = decode(item.label); entry.keywords = decode(item.keywords);
            if (visuals) {
                require(visuals[i].size == sizeof(xui_item_visual), XUI_VERSION_MISMATCH, "Visual record size mismatch.");
                limit(visuals[i].icon, static_cast<uint64_t>(xui::ButtonIcon::drive));
                entry.icon = static_cast<xui::ButtonIcon>(visuals[i].icon);
                entry.image_path = decode(visuals[i].image_path);
                require(entry.image_path.size() <= 32767, XUI_INVALID_ARGUMENT, "Image path exceeds 32767 UTF-16 units.");
            }
            entry.enabled = !(item.flags & 1); entry.selectable = !(item.flags & 2); entry.expanded = !(item.flags & 4);
            entries.push_back(std::move(entry));
        }
        try { as<xui::NavigationView>(n).set_items(std::move(entries)); }
        catch (const std::invalid_argument&) { throw Failure{XUI_INVALID_ARGUMENT, "Invalid navigation hierarchy or item."}; }
        callback_result(n->owner);
    });
}
xui_status XUI_CALL xui_window_title(xui_handle window, xui_string title) noexcept {
    return boundary([&] { auto n = get(window, XUI_WINDOW); editable(n->owner); n->owner->window->set_title(decode(title)); });
}
xui_status XUI_CALL xui_window_key_handler(xui_handle window, xui_key_handler callback, void* context) noexcept {
    return boundary([&] {
        auto n = get(window, XUI_WINDOW); editable(n->owner);
        n->key_handler = callback; n->key_context = context; wire(n);
    });
}
xui_status XUI_CALL xui_window_navigation_handler(xui_handle window, xui_navigation_handler callback, void* context) noexcept {
    return boundary([&] {
        auto n = get(window, XUI_WINDOW); editable(n->owner);
        n->navigation_handler = callback; n->navigation_context = context;
        if (!callback) { n->owner->window->on_navigation({}); return; }
        n->owner->window->on_navigation([weak = std::weak_ptr<Node>(n)](const xui::NavigationEvent& e) {
            const auto node = weak.lock();
            if (!node || !node->navigation_handler || node->owner->closed || node->owner->callback_failure) return false;
            auto position = e.position;
            if (!position && e.target) {
                const auto bounds = e.target->bounds();
                position = xui::Point{bounds.x + bounds.width / 2, bounds.y + bounds.height / 2};
            }
            const xui_navigation_event event{sizeof(xui_navigation_event), static_cast<uint32_t>(e.direction),
                position ? 1u : 0u, 0, event_target(node->owner, e.target),
                position ? position->x : 0, position ? position->y : 0};
            uint32_t handled{};
            ++node->owner->callbacks;
            xui_status status{};
            try { status = node->navigation_handler(node->navigation_context, &event, &handled); }
            catch (...) { status = XUI_CALLBACK_FAILED; }
            --node->owner->callbacks;
            if (status || handled > 1) {
                node->owner->callback_failure = status ? status : XUI_INVALID_ARGUMENT;
                node->owner->window->close(); return true;
            }
            return handled != 0;
        });
    });
}
xui_status XUI_CALL xui_window_post(xui_handle window, xui_post_callback callback, void* context) noexcept {
    return boundary([&] {
        require(callback != nullptr, XUI_INVALID_ARGUMENT, "Missing post callback.");
        struct Delivery {
            xui_post_callback callback{};
            void* context{};
            std::atomic<unsigned> state{};
            ~Delivery() { if (state == 1) { try { callback(context, 0); } catch (...) {} } }
        };
        auto delivery = std::make_shared<Delivery>();
        delivery->callback = callback; delivery->context = context;
        std::lock_guard lock(registry_mutex);
        auto found = registry.find(window);
        require(found != registry.end(), XUI_INVALID_HANDLE, "Invalid or stale window.");
        auto n = found->second;
        require(n->kind == XUI_WINDOW, XUI_WRONG_KIND, "Expected a window.");
        std::weak_ptr<Node> weak = n;
        const bool accepted = n->owner->window->post([weak, delivery] {
            auto node = weak.lock();
            if (!node) return;
            delivery->state = 2;
            auto state = node->owner;
            ++state->callbacks;
            xui_status result{};
            try { result = delivery->callback(delivery->context, 1); }
            catch (...) { result = XUI_CALLBACK_FAILED; }
            --state->callbacks;
            if (result) { state->callback_failure = result; state->window->close(); }
        });
        require(accepted, XUI_CLOSED, "The window is closed.");
        unsigned pending = 0;
        delivery->state.compare_exchange_strong(pending, 1);
    });
}
uint64_t XUI_CALL xui_capabilities() noexcept {
#if XUI_ENABLE_WEBVIEW2
    return 1;
#else
    return 0;
#endif
}
xui_status XUI_CALL xui_feature_create(xui_handle window, uint32_t kind, const xui_feature_options* o, xui_handle* out) noexcept {
    return boundary([&] {
        require(out != nullptr, XUI_INVALID_ARGUMENT, "Missing output handle."); *out = 0;
        require(o && o->size == sizeof(*o) && o->version == XUI_FEATURE_VERSION, XUI_VERSION_MISMATCH, "Feature options version mismatch.");
        require(!o->reserved, XUI_INVALID_ARGUMENT, "Reserved fields must be zero.");
        auto n = get(window, XUI_WINDOW); topology(n->owner);
        auto name = decode(o->name);
        const bool one = kind == XUI_EXPANDER || kind == XUI_POPUP || kind == XUI_CONTENT_DIALOG;
        const bool two = kind == XUI_ADAPTIVE_LAYOUT || kind == XUI_SPLIT_VIEW;
        require((one || two || kind == XUI_VIEW_PICKER) == bool(o->content) && two == bool(o->second), XUI_INVALID_ARGUMENT, "Invalid composition children.");
        require((kind == XUI_COMBO_BOX && o->mode <= 1) || (kind == XUI_DATE_TIME_PICKER && o->mode <= 2) || !o->mode,
            XUI_INVALID_ARGUMENT, "Invalid creation mode.");
        std::shared_ptr<Node> c, s;
        if (o->content) {
            c = get(o->content); same(n, c);
            require(c->element && (kind == XUI_VIEW_PICKER || !c->attached), XUI_INVALID_ARGUMENT, "Content already has a parent.");
        }
        if (o->second) {
            s = get(o->second); same(n, s);
            require(s != c && s->element && !s->attached, XUI_INVALID_ARGUMENT, "Invalid second child.");
        }
        std::shared_ptr<xui::Element> e;
        std::shared_ptr<void> r;
        switch (kind) {
        case XUI_RANGE_INPUT: e = std::make_shared<xui::RangeInput>(name); break;
        case XUI_RADIO_GROUP: e = std::make_shared<xui::RadioGroup>(name); break;
        case XUI_COMBO_BOX: e = std::make_shared<xui::ComboBox>(name, o->mode != 0); break;
        case XUI_NUMERIC_INPUT: e = std::make_shared<xui::NumericInput>(name); break;
        case XUI_EXPANDER: e = std::make_shared<xui::Expander>(name, c->element); break;
        case XUI_PROGRESS: e = std::make_shared<xui::Progress>(name); break;
        case XUI_POPUP: e = std::make_shared<xui::Popup>(c->element, name); break;
        case XUI_SPLIT_BUTTON: e = std::make_shared<xui::SplitButton>(name, L"More"); break;
        case XUI_ITEMS_VIEW: e = std::make_shared<xui::ItemsView>(name); break;
        case XUI_TREE_VIEW: e = std::make_shared<xui::TreeView>(name); break;
        case XUI_GRID: e = std::make_shared<xui::Grid>(); break;
        case XUI_WRAP: e = std::make_shared<xui::Wrap>(); break;
        case XUI_ADAPTIVE_LAYOUT: e = std::make_shared<xui::AdaptiveLayout>(c->element, s->element); break;
        case XUI_COMMAND_BAR: e = std::make_shared<xui::CommandBar>(name); break;
        case XUI_COMMAND_SURFACE: { auto v = std::make_shared<xui::CommandSurface>(name); e = v->popup(); r = v; break; }
        case XUI_BREADCRUMB: e = std::make_shared<xui::Breadcrumb>(name); break;
        case XUI_NAVIGATION_PANE: e = std::make_shared<xui::NavigationPane>(name); break;
        case XUI_LOCATION_PICKER: { auto v = std::make_shared<xui::LocationPicker>(name); e = v->popup(); r = v; break; }
        case XUI_VIEW_PICKER: {
            feature<xui::ItemsView>(c);
            auto v = std::make_shared<xui::ViewPicker>(std::static_pointer_cast<xui::ItemsView>(c->element)); e = v->popup(); r = v; break;
        }
        case XUI_MULTILINE_TEXT: e = std::make_shared<xui::MultilineText>(name); break;
        case XUI_RICH_TEXT: e = std::make_shared<xui::RichText>(name); break;
        case XUI_PASSWORD_INPUT: e = std::make_shared<xui::PasswordInput>(name); break;
        case XUI_DATE_TIME_PICKER: e = std::make_shared<xui::DateTimePicker>(name, static_cast<xui::DateTimePresentation>(o->mode)); break;
        case XUI_INLINE_STATUS: e = std::make_shared<xui::InlineStatus>(name); break;
        case XUI_COLOR_PICKER: e = std::make_shared<xui::ColorPicker>(name); break;
        case XUI_CONTENT_DIALOG: { auto v = std::make_shared<xui::ContentDialog>(name, c->element); e = v->popup(); r = v; break; }
        case XUI_VECTOR_CANVAS: e = std::make_shared<xui::VectorCanvas>(name); break;
        case XUI_MAP_VIEW: e = std::make_shared<xui::MapView>(name); break;
        case XUI_MEDIA_PLAYBACK: e = std::make_shared<xui::MediaPlayback>(name); break;
        case XUI_WEB_CONTENT: e = std::make_shared<xui::WebContent>(name); break;
        case XUI_TAB_STRIP: e = std::make_shared<xui::TabStrip>(name); break;
        case XUI_SPLIT_VIEW: e = std::make_shared<xui::SplitView>(c->element, s->element, name); break;
        case XUI_PAGE_VIEW: e = std::make_shared<xui::PageView>(); break;
        case XUI_DATA_GRID: e = std::make_shared<xui::DataGrid>(name); break;
        case XUI_HISTORY_CHART: e = std::make_shared<xui::HistoryChart>(name); break;
        case XUI_NAVIGATION_VIEW: e = std::make_shared<xui::NavigationView>(name); break;
        case XUI_MILLER_COLUMNS: e = std::make_shared<xui::MillerColumns>(name); break;
        default: throw Failure{XUI_WRONG_KIND, "Unknown feature kind."};
        }
        *out = insert(n->owner, kind, std::move(e));
        get(*out)->resource = std::move(r);
        if (kind == XUI_TREE_VIEW) prepare_tree(get(*out));
        if (kind == XUI_MILLER_COLUMNS) prepare_miller(get(*out));
        if (c && kind != XUI_VIEW_PICKER) c->attached = true;
        if (s) s->attached = true;
    });
}
xui_status XUI_CALL xui_feature_set(xui_handle target, uint32_t property, const xui_feature_value* value) noexcept {
    return boundary([&] {
        require(value != nullptr, XUI_INVALID_ARGUMENT, "Missing feature value.");
        const auto& v = *value;
        auto n = get(target); editable(n->owner);
        switch (property) {
        case XUI_F_RANGE: {
            fields(v, 15); xui::NumericRange range{v.a, v.b, v.c, v.d}; range.validate();
            if (n->kind == XUI_RANGE_INPUT) as<xui::RangeInput>(n).set_range(range);
            else if (n->kind == XUI_NUMERIC_INPUT) as<xui::NumericInput>(n).set_range(range);
            else feature<xui::Progress>(n).set_range(v.a, v.b);
            break;
        }
        case XUI_F_VALUE:
            fields(v, 1);
            if (n->kind == XUI_RANGE_INPUT) as<xui::RangeInput>(n).set_value(v.a);
            else if (n->kind == XUI_NUMERIC_INPUT) as<xui::NumericInput>(n).set_value(v.a);
            else if (n->kind == XUI_HISTORY_CHART) as<xui::HistoryChart>(n).append(v.a);
            else feature<xui::Progress>(n).set_value(v.a);
            break;
        case XUI_F_ORIENTATION: fields(v, 16); limit(v.first, 1); feature<xui::RangeInput>(n).set_orientation(static_cast<xui::Axis>(v.first)); break;
        case XUI_F_REVERSED: fields(v, 16); limit(v.first, 1); feature<xui::RangeInput>(n).set_reversed(v.first != 0); break;
        case XUI_F_EXPANDED:
            fields(v, 16); limit(v.first, 1);
            if (n->kind == XUI_NAVIGATION_VIEW) as<xui::NavigationView>(n).set_expanded(v.first != 0);
            else feature<xui::Expander>(n).set_expanded(v.first != 0);
            break;
        case XUI_F_PROGRESS_STATE: fields(v, 16); limit(v.first, 4); feature<xui::Progress>(n).set_state(static_cast<xui::ProgressState>(v.first)); break;
        case XUI_F_HELP: fields(v, 64); control(n).set_help_text(decode(v.text)); break;
        case XUI_F_TOOLTIP_DELAY: fields(v, 16); limit(v.first, 60000); control(n).set_tooltip_delay(static_cast<unsigned>(v.first)); break;
        case XUI_F_BUTTON_BEHAVIOR: fields(v, 16); limit(v.first, 3); feature<xui::Button>(n).set_behavior(static_cast<xui::ButtonBehavior>(v.first)); break;
        case XUI_F_BUTTON_ICON: fields(v, 16); limit(v.first, static_cast<uint64_t>(xui::ButtonIcon::drive)); feature<xui::Button>(n).set_icon(static_cast<xui::ButtonIcon>(v.first)); break;
        case XUI_F_BUTTON_CHECKED: fields(v, 16); limit(v.first, 1); feature<xui::Button>(n).set_checked(v.first != 0); break;
        case XUI_F_REPEAT_TIMING: fields(v, 48); limit(v.first, 60000); limit(v.second, 60000); feature<xui::Button>(n).set_repeat_timing(static_cast<unsigned>(v.first), static_cast<unsigned>(v.second)); break;
        case XUI_F_DOCUMENT_TEXT: fields(v, 64); feature<xui::DocumentText>(n).set_text(decode(v.text)); break;
        case XUI_F_READ_ONLY: fields(v, 16); limit(v.first, 1); feature<xui::DocumentText>(n).set_read_only(v.first != 0); break;
        case XUI_F_MAXIMUM_LENGTH:
            fields(v, 16);
            if (n->kind == XUI_PASSWORD_INPUT) { limit(v.first, 4096); as<xui::PasswordInput>(n).set_maximum_length(v.first); }
            else { limit(v.first, 1048576); feature<xui::DocumentText>(n).set_maximum_length(v.first); } break;
        case XUI_F_TEXT_SELECTION: {
            fields(v, 48); auto& doc = feature<xui::DocumentText>(n);
            selection_boundary(doc.text(), v.first); selection_boundary(doc.text(), v.second);
            require(v.first <= v.second, XUI_INVALID_ARGUMENT, "Selection start exceeds its end.");
            doc.set_selection({static_cast<size_t>(v.first), static_cast<size_t>(v.second)}); break;
        }
        case XUI_F_PASSWORD: {
            fields(v, 64); auto& p = feature<xui::PasswordInput>(n);
            auto secret = decode(v.text);
            struct Erase { std::wstring& value; ~Erase() { if (!value.empty()) SecureZeroMemory(value.data(), value.size() * sizeof(wchar_t)); } } erase{secret};
            require(secret.size() <= p.maximum_length(), XUI_INVALID_ARGUMENT, "Password exceeds its limit.");
            p.set_password(secret); break;
        }
        case XUI_F_PASSWORD_REVEAL: fields(v, 16); limit(v.first, 1); feature<xui::PasswordInput>(n).set_reveal_policy(v.first ? xui::PasswordRevealPolicy::explicit_request : xui::PasswordRevealPolicy::never); break;
        case XUI_F_DATE: {
            fields(v, 48); limit(v.first, 99991231); limit(v.second, 235959);
            xui::DateTimeValue date{int(v.first / 10000), int(v.first / 100 % 100), int(v.first % 100),
                int(v.second / 10000), int(v.second / 100 % 100), int(v.second % 100)};
            date.validate(); feature<xui::DateTimePicker>(n).set_value(date); break;
        }
        case XUI_F_COLOR: fields(v, 16); limit(v.first, UINT32_MAX); feature<xui::ColorPicker>(n).set_value({
            uint8_t(v.first), uint8_t(v.first >> 8), uint8_t(v.first >> 16), uint8_t(v.first >> 24)}); break;
        case XUI_F_STATUS: fields(v, 80); limit(v.first, 3); feature<xui::InlineStatus>(n).set_message(decode(v.text), static_cast<xui::StatusSeverity>(v.first)); break;
        case XUI_F_DISMISSIBLE: fields(v, 16); limit(v.first, 1); feature<xui::InlineStatus>(n).set_dismissible(v.first != 0); break;
        case XUI_F_MAP_VIEW: fields(v, 7); feature<xui::MapView>(n).set_view({v.a, v.b}, v.c); break;
        case XUI_F_MAP_PAN: fields(v, 3); feature<xui::MapView>(n).pan(v.a, v.b); break;
        case XUI_F_MEDIA_SOURCE: fields(v, 64); feature<xui::MediaPlayback>(n).load_local(decode(v.text)); break;
        case XUI_F_VOLUME: fields(v, 1); feature<xui::MediaPlayback>(n).set_volume(v.a); break;
        case XUI_F_MEDIA_POSITION: fields(v, 1); feature<xui::MediaPlayback>(n).seek(v.a); break;
        case XUI_F_WEB_HTML: fields(v, 64); require(xui_capabilities() & 1, XUI_NATIVE_ERROR, "WebView2 is disabled in this build."); feature<xui::WebContent>(n).set_html(decode(v.text)); break;
        case XUI_F_WEB_PROFILE: fields(v, 64); feature<xui::WebContent>(n).set_profile_root(decode(v.text)); break;
        case XUI_F_ITEM_SIZE: fields(v, 3); finite_float(v.a); finite_float(v.b); feature<xui::VirtualCollection>(n).set_item_size({float(v.a), float(v.b)}); break;
        case XUI_F_PRESENTATION: fields(v, 16); limit(v.first, 2); feature<xui::VirtualCollection>(n).set_presentation(static_cast<xui::ItemsPresentation>(v.first)); break;
        case XUI_F_OFFSET:
            fields(v, 1);
            if (n->kind == XUI_DATA_GRID) as<xui::DataGrid>(n).set_offset(v.a, as<xui::DataGrid>(n).horizontal_offset());
            else feature<xui::VirtualCollection>(n).set_offset(v.a);
            break;
        case XUI_F_WRAP_WIDTH: fields(v, 1); finite_float(v.a); feature<xui::Wrap>(n).set_item_width(float(v.a)); break;
        case XUI_F_BREAKPOINT: fields(v, 1); finite_float(v.a); feature<xui::AdaptiveLayout>(n).set_breakpoint(float(v.a)); break;
        case XUI_F_NAVIGATION_EXTENT: fields(v, 1); finite_float(v.a); feature<xui::AdaptiveLayout>(n).set_navigation_extent(float(v.a)); break;
        case XUI_F_NAVIGATION_OPEN: fields(v, 16); limit(v.first, 1); feature<xui::AdaptiveLayout>(n).set_navigation_open(v.first != 0); break;
        case XUI_F_COMPACT_NAVIGATION: fields(v, 16); limit(v.first, 1); feature<xui::AdaptiveLayout>(n).set_compact_navigation(static_cast<xui::CompactNavigation>(v.first)); break;
        case XUI_F_SPLIT_RATIO: fields(v, 1); finite_float(v.a); feature<xui::SplitView>(n).set_ratio(float(v.a)); break;
        case XUI_F_SECOND_VISIBLE: fields(v, 16); limit(v.first, 1); feature<xui::SplitView>(n).set_secondary_visible(v.first != 0); break;
        case XUI_F_PAGE: fields(v, 16); feature<xui::PageView>(n).select(v.first); break;
        case XUI_F_VISIBLE: fields(v, 16); limit(v.first, 1); control(n).set_visible(v.first != 0); break;
        default: throw Failure{XUI_INVALID_ARGUMENT, "Unknown feature property."};
        }
        callback_result(n->owner);
    });
}
xui_status XUI_CALL xui_feature_get(xui_handle target, uint32_t property, xui_feature_value* v) noexcept {
    return boundary([&] {
        require(v != nullptr, XUI_INVALID_ARGUMENT, "Missing output value.");
        fields(*v, 0); auto n = get(target);
        switch (property) {
        case XUI_F_VALUE:
            if (n->kind == XUI_RANGE_INPUT) v->a = as<xui::RangeInput>(n).value();
            else if (n->kind == XUI_NUMERIC_INPUT) v->a = as<xui::NumericInput>(n).value();
            else v->a = feature<xui::Progress>(n).value(); break;
        case XUI_F_RANGE: {
            xui::NumericRange r;
            if (n->kind == XUI_RANGE_INPUT) r = as<xui::RangeInput>(n).range();
            else if (n->kind == XUI_NUMERIC_INPUT) r = as<xui::NumericInput>(n).range();
            else r = feature<xui::Progress>(n).range();
            v->a = r.minimum; v->b = r.maximum; v->c = r.small_step; v->d = r.large_step; break;
        }
        case XUI_F_EXPANDED:
            v->first = n->kind == XUI_SPLIT_VIEW ? as<xui::SplitView>(n).expanded() :
                n->kind == XUI_NAVIGATION_VIEW ? as<xui::NavigationView>(n).expanded() : feature<xui::Expander>(n).expanded();
            break;
        case XUI_F_BUTTON_ICON: v->first = static_cast<uint64_t>(feature<xui::Button>(n).icon()); break;
        case XUI_F_FOCUSED: v->first = control(n).focused(); break;
        case XUI_F_VISIBLE: v->first = control(n).visible(); break;
        case XUI_F_IS_OPEN: v->first = feature<xui::Popup>(n).is_open(); break;
        case XUI_F_SECOND_VISIBLE: v->first = feature<xui::SplitView>(n).secondary_visible(); break;
        case XUI_F_SPLIT_RATIO: v->a = feature<xui::SplitView>(n).ratio(); break;
        case XUI_F_OFFSET: v->a = n->kind == XUI_DATA_GRID ? as<xui::DataGrid>(n).offset() : feature<xui::VirtualCollection>(n).offset(); break;
        case XUI_F_BUTTON_CHECKED: v->first = feature<xui::Button>(n).checked(); break;
        case XUI_F_PROGRESS_STATE: v->first = static_cast<uint64_t>(feature<xui::Progress>(n).state()); break;
        case XUI_F_TEXT_SELECTION: { auto s = feature<xui::DocumentText>(n).selection(); v->first = s.start; v->second = s.end; break; }
        case XUI_F_READ_ONLY: v->first = feature<xui::DocumentText>(n).read_only(); break;
        case XUI_F_PASSWORD: v->first = feature<xui::PasswordInput>(n).length(); break;
        case XUI_F_DATE: { auto d = feature<xui::DateTimePicker>(n).value(); v->first = d.year * 10000 + d.month * 100 + d.day; v->second = d.hour * 10000 + d.minute * 100 + d.second; break; }
        case XUI_F_COLOR: { auto c = feature<xui::ColorPicker>(n).value(); v->first = c.red | uint64_t(c.green) << 8 | uint64_t(c.blue) << 16 | uint64_t(c.alpha) << 24; break; }
        case XUI_F_MAP_VIEW: { auto& m = feature<xui::MapView>(n); v->a = m.center().latitude; v->b = m.center().longitude; v->c = m.zoom(); break; }
        case XUI_F_HOST_STATE: v->first = static_cast<uint64_t>(feature<xui::RuntimeHost>(n).state()); break;
        case XUI_F_MEDIA_POSITION: v->a = feature<xui::MediaPlayback>(n).position(); v->b = feature<xui::MediaPlayback>(n).duration(); break;
        case XUI_F_PAGE: v->first = feature<xui::PageView>(n).selected(); break;
        case XUI_F_SELECTION_STATE: {
            const auto& selection = n->kind == XUI_DATA_GRID ? as<xui::DataGrid>(n).selection() : feature<xui::VirtualCollection>(n).selection();
            auto focus = selection.focused(); v->a = focus.has_value(); v->b = static_cast<double>(selection.storage_size());
            if (focus) { v->first = focus->id; v->second = focus->version; }
            break;
        }
        default: throw Failure{XUI_INVALID_ARGUMENT, "This property has no getter."};
        }
    });
}
xui_status XUI_CALL xui_feature_action(xui_handle target, uint32_t action, uint64_t first, uint64_t second) noexcept {
    return boundary([&] {
        auto n = get(target); editable(n->owner);
        require(action == XUI_A_SELECT || action == XUI_A_GRID_NAVIGATE || !second, XUI_INVALID_ARGUMENT, "Unused argument must be zero.");
        require(action <= XUI_A_TEXT_COMMAND || action == XUI_A_COLLECTION_STEP || action == XUI_A_GRID_NAVIGATE || !first, XUI_INVALID_ARGUMENT, "Unused argument must be zero.");
        switch (action) {
        case XUI_A_SELECT:
            if (n->kind == XUI_RADIO_GROUP) { require(!second, XUI_INVALID_ARGUMENT, "Unused argument."); as<xui::RadioGroup>(n).select(first); }
            else if (n->kind == XUI_COMBO_BOX) { require(!second, XUI_INVALID_ARGUMENT, "Unused argument."); as<xui::ComboBox>(n).select(first); }
            else if (n->kind == XUI_TAB_STRIP) { require(!second, XUI_INVALID_ARGUMENT, "Unused argument."); as<xui::TabStrip>(n).select(first); }
            else if (n->kind == XUI_NAVIGATION_VIEW) { require(!second, XUI_INVALID_ARGUMENT, "Unused argument."); as<xui::NavigationView>(n).select({first, 0}); }
            else if (n->kind == XUI_VECTOR_CANVAS || n->kind == XUI_MAP_VIEW) feature<xui::VectorCanvas>(n).select(first);
            else if (n->kind == XUI_DATA_GRID) as<xui::DataGrid>(n).select({first, second});
            else feature<xui::VirtualCollection>(n).select({first, second}); break;
        case XUI_A_CHANGE_VALUE: {
            const auto v = std::bit_cast<double>(first);
            require(std::isfinite(v), XUI_INVALID_ARGUMENT, "Value must be finite.");
            if (n->kind == XUI_RANGE_INPUT) as<xui::RangeInput>(n).change_value(v);
            else feature<xui::NumericInput>(n).change_value(v); break;
        }
        case XUI_A_STEP: limit(first, 1); feature<xui::NumericInput>(n).step(first ? 1 : -1); break;
        case XUI_A_TEXT_COMMAND: limit(first, 5); feature<xui::DocumentText>(n).command(static_cast<xui::TextCommand>(first)); break;
        case XUI_A_DISMISS:
            if (n->kind == XUI_INLINE_STATUS) as<xui::InlineStatus>(n).dismiss();
            else n->owner->window->dismiss_popup(feature<xui::Popup>(n)); break;
        case XUI_A_SHOW: feature<xui::InlineStatus>(n).show(); break;
        case XUI_A_ACCEPT: resource<xui::ContentDialog>(n, XUI_CONTENT_DIALOG)->accept(); break;
        case XUI_A_CANCEL: resource<xui::ContentDialog>(n, XUI_CONTENT_DIALOG)->cancel(); break;
        case XUI_A_PLAY: feature<xui::MediaPlayback>(n).play(); break;
        case XUI_A_PAUSE: feature<xui::MediaPlayback>(n).pause(); break;
        case XUI_A_STOP: if (n->kind == XUI_WEB_CONTENT) { ++n->web_generation; as<xui::WebContent>(n).stop(); } else feature<xui::MediaPlayback>(n).stop(); break;
        case XUI_A_UNLOAD: if (n->kind == XUI_WEB_CONTENT) as<xui::WebContent>(n).unload(); else feature<xui::MediaPlayback>(n).unload(); break;
        case XUI_A_RELOAD: feature<xui::WebContent>(n).reload(); break;
        case XUI_A_FOCUS: feature<xui::WebContent>(n).focus_content(); break;
        case XUI_A_SELECT_ALL: if (n->kind == XUI_DATA_GRID) as<xui::DataGrid>(n).select_all(); else feature<xui::VirtualCollection>(n).select_all(); break;
        case XUI_A_COLLECTION_STEP:
            require(!second && first <= UINT32_MAX, XUI_INVALID_ARGUMENT, "Invalid step.");
            feature<xui::VirtualCollection>(n).step(std::bit_cast<int32_t>(uint32_t(first))); break;
        case XUI_A_GRID_NAVIGATE: {
            limit(first, 5); limit(second, 3);
            auto& grid = feature<xui::DataGrid>(n);
            const auto gesture = second & 2 ? (second & 1 ? xui::SelectionGesture::add_range : xui::SelectionGesture::extend) :
                second & 1 ? xui::SelectionGesture::focus_only : xui::SelectionGesture::replace;
            if (first >= 4) grid.edge(first == 5, gesture);
            else {
                const auto distance = first >= 2 ? std::max(1, static_cast<int>(grid.viewport_height() / xui::DataGrid::row_height)) : 1;
                grid.step((first % 2 ? 1 : -1) * distance, gesture);
            }
            break;
        }
        default: throw Failure{XUI_INVALID_ARGUMENT, "Unknown feature action."};
        }
        callback_result(n->owner);
    });
}
xui_status XUI_CALL xui_choices(xui_handle target, const xui_choice* items, uint32_t count, uint64_t selected, uint32_t has) noexcept {
    return boundary([&] {
        auto n = get(target); editable(n->owner);
        require(count <= XUI_MAX_BATCH && (items || !count) && has <= 1 && (has || !selected), XUI_INVALID_ARGUMENT, "Invalid choices.");
        std::vector<xui::ChoiceItem> choices; std::vector<xui::TabItem> tabs; std::vector<xui::PathSegment> segments;
        std::set<uint64_t> ids;
        for (uint32_t i = 0; i < count; ++i) {
            const auto& v = items[i];
            require(v.size == sizeof(v), XUI_VERSION_MISMATCH, "Choice size mismatch.");
            require(v.flags <= 1 && ids.insert(v.id).second, XUI_INVALID_ARGUMENT, "Invalid flags or duplicate choice ID.");
            auto text = decode(v.text);
            choices.push_back({v.id, text, v.flags == 0}); tabs.push_back({v.id, text}); segments.push_back({{v.id, v.version}, std::move(text)});
        }
        require(!has || ids.contains(selected), XUI_INVALID_ARGUMENT, "Selected ID is absent.");
        std::optional<uint64_t> id = has ? std::optional<uint64_t>(selected) : std::nullopt;
        if (n->kind == XUI_RADIO_GROUP) as<xui::RadioGroup>(n).set_items(std::move(choices), id);
        else if (n->kind == XUI_COMBO_BOX) as<xui::ComboBox>(n).set_items(std::move(choices), id);
        else if (n->kind == XUI_TAB_STRIP) as<xui::TabStrip>(n).set_tabs(std::move(tabs), id);
        else feature<xui::Breadcrumb>(n).set_segments(std::move(segments));
    });
}
xui_status XUI_CALL xui_feature_child(xui_handle target, uint32_t index, xui_handle* out) noexcept {
    return boundary([&] {
        require(out != nullptr, XUI_INVALID_ARGUMENT, "Missing output handle."); *out = 0;
        auto n = get(target);
        std::shared_ptr<xui::Element> e; uint32_t kind{};
        switch (n->kind) {
        case XUI_WINDOW: {
            limit(index, 2);
            const auto& titlebar = n->owner->window->titlebar();
            require(titlebar != nullptr, XUI_WRONG_KIND, "The window has no custom title bar.");
            if (index == 1) { e = titlebar->leading(); kind = XUI_BUTTON; }
            else { e = index == 2 ? titlebar->secondary_tabs() : titlebar->tabs(); kind = XUI_TAB_STRIP; }
            break;
        }
        case XUI_NAVIGATION_VIEW: limit(index, 0); e = as<xui::NavigationView>(n).search(); kind = XUI_TEXT_INPUT; break;
        case XUI_MILLER_COLUMNS:
            limit(index, xui::MillerColumns::maximum_columns - 1);
            e = as<xui::MillerColumns>(n).column_list(index); kind = XUI_ITEMS_VIEW; break;
        case XUI_SPLIT_BUTTON: limit(index, 1); e = index ? as<xui::SplitButton>(n).secondary() : as<xui::SplitButton>(n).primary(); kind = XUI_BUTTON; break;
        case XUI_CONTENT_DIALOG: limit(index, 1); { auto r = resource<xui::ContentDialog>(n, n->kind); e = index ? r->cancel_button() : r->primary(); kind = XUI_BUTTON; } break;
        case XUI_LOCATION_PICKER: limit(index, 1); { auto r = resource<xui::LocationPicker>(n, n->kind); if (index) { e = r->navigation(); kind = XUI_NAVIGATION_PANE; } else { e = r->editor(); kind = XUI_TEXT_INPUT; } } break;
        case XUI_VIEW_PICKER: limit(index, 1); { auto r = resource<xui::ViewPicker>(n, n->kind); if (index) { e = r->size(); kind = XUI_RANGE_INPUT; } else { e = r->choices(); kind = XUI_RADIO_GROUP; } } break;
        case XUI_NAVIGATION_PANE: limit(index, 0); e = as<xui::NavigationPane>(n).items(); kind = XUI_ITEMS_VIEW; break;
        default: throw Failure{XUI_WRONG_KIND, "This composition has no exposed children."};
        }
        for (auto h : n->children) if (get(h)->element == e) { *out = h; return; }
        n->children.reserve(n->children.size() + 1);
        *out = insert(n->owner, kind, e); get(*out)->attached = true; n->children.push_back(*out);
        if (n->kind == XUI_MILLER_COLUMNS) {
            std::weak_ptr<Node> weak = n;
            get(*out)->miller_owner = n;
            get(*out)->prior_focus = [weak, index] {
                if (auto parent = weak.lock(); parent && index < as<xui::MillerColumns>(parent).columns().size())
                    as<xui::MillerColumns>(parent).set_active_column(index);
            };
        }
        if (n->kind == XUI_NAVIGATION_VIEW) {
            std::weak_ptr<Node> weak = n;
            get(*out)->prior_change = [weak](const std::wstring& text) {
                if (auto parent = weak.lock()) as<xui::NavigationView>(parent).set_filter(text);
            };
        }
        if (n->kind == XUI_CONTENT_DIALOG) {
            std::weak_ptr<Node> weak = n;
            get(*out)->prior_action = [weak, index] {
                if (auto parent = weak.lock()) {
                    auto dialog = resource<xui::ContentDialog>(parent, XUI_CONTENT_DIALOG);
                    if (index) dialog->cancel(); else dialog->accept();
                }
            };
        }
    });
}
xui_status XUI_CALL xui_panel_add(xui_handle target, xui_handle child, uint32_t row, uint32_t column, uint32_t rows, uint32_t columns) noexcept {
    return boundary([&] {
        auto n = get(target); auto c = get(child); same(n, c); topology(n->owner);
        require(n != c && c->element && !c->attached, XUI_INVALID_ARGUMENT, "Invalid child.");
        if (n->kind == XUI_GRID) as<xui::Grid>(n).add(c->element, row, column, rows, columns);
        else {
            require(!row && !column && rows == 1 && columns == 1, XUI_INVALID_ARGUMENT, "Only Grid supports cell coordinates.");
            if (n->kind == XUI_PAGE_VIEW) as<xui::PageView>(n).add_page(c->element);
            else feature<xui::Wrap>(n).add(c->element);
        }
        c->attached = true;
    });
}
xui_status XUI_CALL xui_popup_show(xui_handle target, xui_handle anchor) noexcept {
    return boundary([&] {
        auto n = get(target); auto a = get(anchor); same(n, a); editable(n->owner); auto& c = control(a);
        if (n->kind == XUI_CONTENT_DIALOG) n->owner->window->show_dialog(resource<xui::ContentDialog>(n, n->kind), c);
        else if (n->kind == XUI_COMMAND_SURFACE) n->owner->window->show_commands(resource<xui::CommandSurface>(n, n->kind), c);
        else if (n->kind == XUI_LOCATION_PICKER) n->owner->window->show_location_picker(resource<xui::LocationPicker>(n, n->kind), c);
        else { feature<xui::Popup>(n); n->owner->window->show_popup(std::static_pointer_cast<xui::Popup>(n->element), c); }
        callback_result(n->owner);
    });
}
xui_status XUI_CALL xui_rich_runs(xui_handle target, const xui_text_run* runs, uint32_t count) noexcept {
    return boundary([&] {
        auto n = get(target, XUI_RICH_TEXT); editable(n->owner);
        require(count <= XUI_MAX_BATCH && (runs || !count), XUI_INVALID_ARGUMENT, "Invalid text run span.");
        std::vector<xui::TextRun> values; values.reserve(count);
        size_t length{};
        for (uint32_t i = 0; i < count; ++i) {
            const auto& r = runs[i];
            require(r.size == sizeof(r), XUI_VERSION_MISMATCH, "Text run size mismatch.");
            require(r.flags <= 7, XUI_INVALID_ARGUMENT, "Invalid text run flags.");
            auto text = decode(r.text); length += text.size();
            require(length <= as<xui::RichText>(n).maximum_length(), XUI_INVALID_ARGUMENT, "Document exceeds its limit.");
            values.push_back({std::move(text), bool(r.flags & 1), bool(r.flags & 2), bool(r.flags & 4), decode(r.link)});
        }
        as<xui::RichText>(n).set_runs(std::move(values));
    });
}
xui_status XUI_CALL xui_password_read(xui_handle target, xui_secret_receiver receiver, void* context) noexcept {
    return boundary([&] {
        auto n = get(target, XUI_PASSWORD_INPUT); editable(n->owner);
        require(receiver && !n->owner->secret_callbacks, XUI_BUSY, "A secret receiver cannot reenter another secret receiver.");
        auto s = n->owner;
        as<xui::PasswordInput>(n).with_password([&](std::wstring_view text) {
            auto bytes = encode(text);
            struct Erase { std::string& v; ~Erase() { if (!v.empty()) SecureZeroMemory(v.data(), v.size()); } } erase{bytes};
            ++s->callbacks; ++s->secret_callbacks; xui_status result{};
            try { result = receiver(context, bytes.data(), static_cast<uint32_t>(bytes.size())); } catch (...) { result = XUI_CALLBACK_FAILED; }
            --s->callbacks; --s->secret_callbacks;
            if (result) { s->callback_failure = result; s->window->close(); }
        });
        callback_result(s);
    });
}

namespace {
constexpr uint32_t source_kind = 100, request_kind = 101;
struct ForeignSource final : xui::ItemsSource {
    xui_source_options options;
    xui_source_visual_query visual_query{};
    std::weak_ptr<const xui::ItemsSource> miller_source;
    std::weak_ptr<State> owner;
    ForeignSource(xui_source_options value, const std::shared_ptr<State>& state) : options(value), owner(state) {
        options.retain(options.context);
    }
    ~ForeignSource() override { try { options.release(options.context); } catch (...) {} }
    size_t size() const override { return static_cast<size_t>(options.count); }
    xui_source_row read(uint32_t operation, uint64_t first, uint64_t second = 0) const {
        auto s = owner.lock();
        require(s && !s->closed && s->window, XUI_CLOSED, "The snapshot owner is closed.");
        require(s->thread == GetCurrentThreadId(), XUI_WRONG_THREAD, "Source access requires the UI thread.");
        require(!s->source_callbacks, XUI_BUSY, "A source callback cannot reenter its source.");
        xui_source_row row{}; row.size = sizeof(row);
        ++s->callbacks; ++s->source_callbacks; xui_status status{};
        try { status = options.query(options.context, operation, first, second, &row); }
        catch (...) { status = XUI_CALLBACK_FAILED; }
        --s->callbacks; --s->source_callbacks;
        if (status) { s->callback_failure = status; s->window->close(); throw Failure{XUI_CALLBACK_FAILED, "An immutable source callback failed."}; }
        require(row.size == sizeof(row) && row.flags <= 15 && row.primary_length <= 1024 && row.secondary_length <= 1024 &&
            std::isfinite(row.progress), XUI_INVALID_ARGUMENT, "Invalid source callback output.");
        return row;
    }
    xui::ItemKey key(size_t index) const override {
        require(index < size(), XUI_INVALID_ARGUMENT, "Source index is out of range.");
        auto r = read(0, index); return {r.id, r.version};
    }
    std::optional<size_t> find(xui::ItemKey key) const override {
        auto r = read(2, key.id, key.version);
        require(r.index == UINT64_MAX || r.index < size(), XUI_INVALID_ARGUMENT, "Source find returned an invalid index.");
        return r.index == UINT64_MAX ? std::nullopt : std::optional<size_t>(r.index);
    }
    xui::ItemContent item(size_t index) const override {
        require(index < size(), XUI_INVALID_ARGUMENT, "Source index is out of range.");
        auto r = read(1, index);
        xui::ItemContent item;
        item.primary = decode({r.primary, r.primary_length, 0}); item.secondary = decode({r.secondary, r.secondary_length, 0});
        item.enabled = !(r.flags & 1);
        if (r.flags & 2) { require(r.progress >= 0 && r.progress <= 1, XUI_INVALID_ARGUMENT, "Inline progress must be in [0,1]."); item.progress = r.progress; }
        if (r.flags & 8) item.checked = bool(r.flags & 4);
        return item;
    }
    bool selectable(size_t index) const override { return index < size(); }
    xui::ItemVisual read_visual(size_t index, size_t column = 0) const {
        if (!visual_query) return {};
        require(index < size(), XUI_INVALID_ARGUMENT, "Visual index is out of range.");
        auto state = owner.lock();
        require(state && !state->closed && state->window, XUI_CLOSED, "The snapshot owner is closed.");
        require(state->thread == GetCurrentThreadId(), XUI_WRONG_THREAD, "Source access requires the UI thread.");
        require(!state->source_callbacks, XUI_BUSY, "A source callback cannot reenter its source.");
        uint32_t icon{}, required{};
        const auto query = [&](char* buffer, uint32_t capacity) {
            ++state->callbacks; ++state->source_callbacks;
            xui_status status{};
            try { status = visual_query(options.context, index, column, &icon, buffer, capacity, &required); }
            catch (...) { status = XUI_CALLBACK_FAILED; }
            --state->callbacks; --state->source_callbacks;
            if (status && !(status == XUI_BUFFER_TOO_SMALL && !capacity)) {
                state->callback_failure = status; state->window->close();
                throw Failure{XUI_CALLBACK_FAILED, "An immutable visual callback failed."};
            }
            require(icon <= static_cast<uint32_t>(xui::ButtonIcon::drive) && required <= XUI_MAX_STRING_BYTES,
                XUI_INVALID_ARGUMENT, "Invalid visual callback output.");
        };
        query(nullptr, 0);
        const uint32_t length = required;
        std::vector<char> bytes(length);
        if (length) { query(bytes.data(), length); require(required == length, XUI_INVALID_ARGUMENT, "Immutable visual path changed between reads."); }
        auto path = decode({bytes.data(), length, 0});
        require(path.size() <= 32767, XUI_INVALID_ARGUMENT, "Image path exceeds 32767 UTF-16 units.");
        return {static_cast<xui::ButtonIcon>(icon), std::move(path)};
    }
    xui::ItemVisual visual(size_t index) const override { return read_visual(index); }
};
struct ForeignMillerSource final : xui::ItemsSource {
    std::shared_ptr<ForeignSource> source;
    explicit ForeignMillerSource(std::shared_ptr<ForeignSource> value) : source(std::move(value)) {}
    size_t size() const override { return source->size(); }
    xui::ItemKey key(size_t index) const override { return source->key(index); }
    std::optional<size_t> find(xui::ItemKey key) const override { return source->find(key); }
    xui::ItemContent item(size_t index) const override { return source->item(index); }
    xui::ItemVisual visual(size_t index) const override { return source->visual(index); }
    xui::ItemHierarchy hierarchy(size_t index) const override {
        const auto node = key(index);
        xui::ItemHierarchy result;
        result.expandable = source->read(3, node.id, node.version).index != 0;
        return result;
    }
};
struct ForeignTree final : xui::TreeSource {
    std::shared_ptr<ForeignSource> source;
    explicit ForeignTree(std::shared_ptr<ForeignSource> s) : source(std::move(s)) {}
    std::shared_ptr<const xui::ItemsSource> roots() const override { return source; }
    bool has_children(xui::ItemKey node) const override { return source->read(3, node.id, node.version).index != 0; }
};
struct ForeignGrid final : xui::GridSource {
    std::shared_ptr<ForeignSource> source;
    explicit ForeignGrid(std::shared_ptr<ForeignSource> s) : source(std::move(s)) {}
    size_t size() const override { return source->size(); }
    xui::RowKey key(size_t index) const override { return source->key(index); }
    std::optional<size_t> find(xui::RowKey key) const override { return source->find(key); }
    std::wstring text(size_t row, size_t column) const override {
        auto r = source->read(1, row, column); return decode({r.primary, r.primary_length, 0});
    }
    xui::ItemVisual visual(size_t row, size_t column) const override { return source->read_visual(row, column); }
};
struct WebResult { uint64_t revision{}, generation{}; bool done{}, failed{}; std::string text; };
using RequestValue = std::variant<xui::TreeRequest, xui::MapRequest, WebResult>;
struct Request {
    std::weak_ptr<Node> target;
    RequestValue value;
};
void revoke(const std::shared_ptr<Node>& n) {
    auto& handles = n->owner->handles;
    handles.erase(std::remove(handles.begin(), handles.end(), n->handle), handles.end());
    std::lock_guard lock(registry_mutex); registry.erase(n->handle);
}
xui_handle request_token(const std::shared_ptr<Node>& n, RequestValue value) {
    auto r = std::make_shared<Request>(); r->target = n; r->value = std::move(value);
    auto h = insert(n->owner, request_kind); get(h)->resource = r; return h;
}
void prepare_tree(const std::shared_ptr<Node>& n) {
    std::weak_ptr<Node> weak = n;
    as<xui::TreeView>(n).on_request([weak](xui::TreeRequest request) {
        if (auto node = weak.lock()) {
            auto token = request_token(node, request);
            dispatch(weak, XUI_REQUEST, token);
        }
    });
}
void prepare_miller(const std::shared_ptr<Node>& n) {
    std::weak_ptr<Node> weak = n;
    const auto send = [weak](uint32_t kind, size_t column, xui::ItemKey key) {
        auto node = weak.lock();
        if (!node || node->owner->closed || node->owner->callback_failure) return;
        auto state = node->owner;
        if (node->dispatching) {
            state->callback_failure = XUI_BUSY; state->window->close(); return;
        }
        if (node->miller_callback) {
            const xui_miller_event event{sizeof(xui_miller_event), kind, static_cast<uint32_t>(column), 0, key.id, key.version};
            ++state->callbacks; ++node->dispatching;
            xui_status result{};
            try { result = node->miller_callback(node->miller_context, &event); }
            catch (...) { result = XUI_CALLBACK_FAILED; }
            --state->callbacks; --node->dispatching;
            if (result) { state->callback_failure = result; state->window->close(); }
        }
        // A managed handler can replace the path; never deliver a stale child action afterward.
        auto& columns = as<xui::MillerColumns>(node);
        if (state->closed || state->callback_failure || column >= columns.columns().size()) return;
        const auto list = columns.column_list(column);
        if (!list->source() || !list->source()->find(key) || list->selection().focused() != key) return;
        const auto children = node->children;
        for (auto handle : children) {
            const auto child = get(handle);
            if (child->element == list) { dispatch(child, kind, key.id); break; }
        }
    };
    auto& columns = as<xui::MillerColumns>(n);
    columns.on_selection([send](size_t column, xui::ItemKey key) { send(XUI_SELECTION, column, key); });
    columns.on_activate([send](size_t column, xui::ItemKey key) { send(XUI_CLICK, column, key); });
}
bool feature_key(const std::shared_ptr<Node>& n, const xui::KeyEvent& e) {
    // Copy handles because a command can create controls before the first run.
    auto handles = n->owner->handles;
    for (auto h : handles) {
        auto node = get(h);
        if (node->commands && node->bindings.invoke(*node->commands,
            {static_cast<uint16_t>(e.key), e.control, e.shift, e.alt})) return true;
    }
    return false;
}
xui::MapOverlay marker_overlay(const xui_map_marker* markers, uint32_t count) {
    require(count <= 256 && (markers || !count), XUI_INVALID_ARGUMENT, "At most 256 interactive markers are supported.");
    xui::MapOverlay result;
    std::set<uint64_t> ids;
    for (uint32_t i = 0; i < count; ++i) {
        const auto& m = markers[i];
        require(m.size == sizeof(m), XUI_VERSION_MISMATCH, "Map marker size mismatch.");
        require(!m.reserved && ids.insert(m.id).second && std::isfinite(m.latitude) && std::isfinite(m.longitude),
            XUI_INVALID_ARGUMENT, "Invalid marker coordinates or duplicate ID.");
        result.markers.push_back({m.id, {m.latitude, m.longitude}, decode(m.name)});
    }
    return result;
}
}
xui_status XUI_CALL xui_source_create(xui_handle window, const xui_source_options* options, xui_handle* out) noexcept {
    return xui_source_create_visual(window, options, nullptr, out);
}
xui_status XUI_CALL xui_source_create_visual(xui_handle window, const xui_source_options* options,
    xui_source_visual_query visual, xui_handle* out) noexcept {
    return boundary([&] {
        require(out != nullptr, XUI_INVALID_ARGUMENT, "Missing output handle."); *out = 0;
        auto n = get(window, XUI_WINDOW); editable(n->owner);
        require(options && options->size == sizeof(*options) && options->version == XUI_FEATURE_VERSION, XUI_VERSION_MISMATCH, "Source options version mismatch.");
        require(options->count <= 1000000000 && options->query && options->retain && options->release,
            XUI_INVALID_ARGUMENT, "Invalid immutable source callbacks or count.");
        auto source = std::make_shared<ForeignSource>(*options, n->owner);
        source->visual_query = visual;
        *out = insert(n->owner, source_kind); get(*out)->resource = std::move(source);
    });
}
xui_status XUI_CALL xui_source_attach(xui_handle target, xui_handle source) noexcept {
    return boundary([&] {
        auto n = get(target); auto s = get(source, source_kind); same(n, s); editable(n->owner);
        auto value = resource<ForeignSource>(s, source_kind);
        if (n->kind == XUI_ITEMS_VIEW) feature<xui::ItemsView>(n).set_items(value);
        else if (n->kind == XUI_NAVIGATION_PANE) as<xui::NavigationPane>(n).set_items(value);
        else if (n->kind == XUI_TREE_VIEW) as<xui::TreeView>(n).set_tree(std::make_shared<ForeignTree>(value));
        else feature<xui::DataGrid>(n).set_source(std::make_shared<ForeignGrid>(value));
        callback_result(n->owner);
    });
}
xui_status XUI_CALL xui_source_release(xui_handle source) noexcept {
    return boundary([&] { revoke(get(source, source_kind)); });
}
xui_status XUI_CALL xui_miller_set_columns(xui_handle target, const xui_miller_column* columns, uint32_t count) noexcept {
    return boundary([&] {
        auto n = get(target, XUI_MILLER_COLUMNS); editable(n->owner);
        require(count <= xui::MillerColumns::maximum_columns && (columns || !count),
            XUI_INVALID_ARGUMENT, "Invalid Miller column span.");
        std::vector<xui::MillerColumn> values;
        values.reserve(count);
        for (uint32_t i = 0; i < count; ++i) {
            const auto& column = columns[i];
            require(column.size == sizeof(column) && column.has_selection <= 1 &&
                (column.has_selection || (!column.selected_id && !column.selected_version)),
                XUI_INVALID_ARGUMENT, "Invalid Miller column record.");
            auto s = get(column.source, source_kind); same(n, s);
            auto source = resource<ForeignSource>(s, source_kind);
            auto items = source->miller_source.lock();
            if (!items) {
                items = std::make_shared<ForeignMillerSource>(source);
                source->miller_source = items;
            }
            const auto selected = column.has_selection ?
                std::optional{xui::ItemKey{column.selected_id, column.selected_version}} : std::nullopt;
            require(!selected || items->find(*selected).has_value(), XUI_INVALID_ARGUMENT, "Selected item is not in its column.");
            values.push_back({decode(column.title), std::move(items), selected});
        }
        as<xui::MillerColumns>(n).set_columns(std::move(values));
        callback_result(n->owner);
    });
}
xui_status XUI_CALL xui_miller_state(xui_handle target, uint32_t* count, uint32_t* active, double* width) noexcept {
    return boundary([&] {
        require(count && active && width, XUI_INVALID_ARGUMENT, "Missing Miller state output.");
        auto n = get(target, XUI_MILLER_COLUMNS);
        const auto& columns = as<xui::MillerColumns>(n);
        *count = static_cast<uint32_t>(columns.columns().size());
        *active = static_cast<uint32_t>(columns.active_column());
        *width = columns.column_width();
    });
}
xui_status XUI_CALL xui_miller_active(xui_handle target, uint32_t column) noexcept {
    return boundary([&] {
        auto n = get(target, XUI_MILLER_COLUMNS); editable(n->owner);
        require(column < as<xui::MillerColumns>(n).columns().size(), XUI_INVALID_ARGUMENT, "Column index is out of range.");
        as<xui::MillerColumns>(n).set_active_column(column);
    });
}
xui_status XUI_CALL xui_miller_width(xui_handle target, double width) noexcept {
    return boundary([&] {
        auto n = get(target, XUI_MILLER_COLUMNS); editable(n->owner);
        require(std::isfinite(width) && width >= 120 && width <= 2000, XUI_INVALID_ARGUMENT, "Column width must be 120 to 2000 DIPs.");
        as<xui::MillerColumns>(n).set_column_width(static_cast<float>(width));
    });
}
xui_status XUI_CALL xui_miller_scroll_state(xui_handle target, double* offset, double* maximum) noexcept {
    return boundary([&] {
        require(offset && maximum, XUI_INVALID_ARGUMENT, "Missing Miller scroll state output.");
        auto n = get(target, XUI_MILLER_COLUMNS);
        const auto& columns = as<xui::MillerColumns>(n);
        *offset = columns.horizontal_offset(); *maximum = columns.maximum_horizontal();
    });
}
xui_status XUI_CALL xui_miller_scroll(xui_handle target, double offset) noexcept {
    return boundary([&] {
        auto n = get(target, XUI_MILLER_COLUMNS); editable(n->owner);
        auto& columns = as<xui::MillerColumns>(n);
        require(std::isfinite(offset) && offset >= 0 && offset <= columns.maximum_horizontal(),
            XUI_INVALID_ARGUMENT, "Horizontal offset must be within the Miller scroll range.");
        columns.set_horizontal_offset(offset);
    });
}
xui_status XUI_CALL xui_miller_subscribe(xui_handle target, xui_miller_callback callback, void* context) noexcept {
    return boundary([&] {
        auto n = get(target, XUI_MILLER_COLUMNS); editable(n->owner);
        n->miller_callback = callback; n->miller_context = context;
    });
}
xui_status XUI_CALL xui_collection_contains(xui_handle target, uint64_t id, uint64_t version, uint32_t* selected) noexcept {
    return boundary([&] {
        require(selected != nullptr, XUI_INVALID_ARGUMENT, "Missing selection output.");
        auto n = get(target); editable(n->owner);
        const auto& selection = n->kind == XUI_DATA_GRID ? as<xui::DataGrid>(n).selection() : feature<xui::VirtualCollection>(n).selection();
        *selected = selection.contains({id, version});
        callback_result(n->owner);
    });
}
xui_status XUI_CALL xui_tree_expand(xui_handle target, uint64_t id, uint64_t version, uint32_t expanded) noexcept {
    return boundary([&] {
        auto n = get(target, XUI_TREE_VIEW); editable(n->owner); limit(expanded, 1);
        as<xui::TreeView>(n).disclose({id, version}, expanded != 0); callback_result(n->owner);
    });
}
xui_status XUI_CALL xui_tree_complete(xui_handle target, xui_handle token, xui_handle source, xui_string error_text) noexcept {
    return boundary([&] {
        auto n = get(target, XUI_TREE_VIEW); auto r = get(token, request_kind); auto s = get(source, source_kind);
        same(n, r); same(n, s);
        if (n->owner->closed) { revoke(r); throw Failure{XUI_CLOSED, "The request owner is closed."}; }
        auto request = resource<Request>(r, request_kind);
        require(request->target.lock() == n && std::holds_alternative<xui::TreeRequest>(request->value),
            XUI_INVALID_ARGUMENT, "The request belongs to another control.");
        auto message = decode(error_text);
        bool accepted = as<xui::TreeView>(n).complete(std::get<xui::TreeRequest>(request->value), resource<ForeignSource>(s, source_kind), std::move(message));
        revoke(r);
        require(accepted, XUI_CLOSED, "The tree request is stale or canceled.");
        callback_result(n->owner);
    });
}
xui_status XUI_CALL xui_request_cancel(xui_handle token) noexcept {
    return boundary([&] {
        auto r = get(token, request_kind); auto request = resource<Request>(r, request_kind);
        require(!r->owner->source_callbacks, XUI_BUSY, "Immutable source callbacks cannot cancel UI requests.");
        if (auto n = request->target.lock(); n && !n->owner->closed) {
            if (auto* tree = std::get_if<xui::TreeRequest>(&request->value);
                tree && !tree->cancellation.stop_requested())
                as<xui::TreeView>(n).disclose(tree->node, false);
            else if (auto* map = std::get_if<xui::MapRequest>(&request->value);
                map && !map->stop.stop_requested())
                as<xui::MapView>(n).cancel_request();
        }
        revoke(r);
    });
}
xui_status XUI_CALL xui_request_info(xui_handle token, xui_feature_value* value) noexcept {
    return boundary([&] {
        require(value != nullptr, XUI_INVALID_ARGUMENT, "Missing request information.");
        fields(*value, 0); auto n = get(token, request_kind); editable(n->owner);
        auto r = resource<Request>(n, request_kind);
        if (auto* tree = std::get_if<xui::TreeRequest>(&r->value)) {
            require(!tree->cancellation.stop_requested(), XUI_CLOSED, "The tree request is canceled.");
            value->first = tree->node.id; value->second = tree->node.version;
        } else if (auto* map = std::get_if<xui::MapRequest>(&r->value)) {
            require(!map->stop.stop_requested(), XUI_CLOSED, "The map request is canceled.");
            value->a = map->center.latitude; value->b = map->center.longitude; value->c = map->zoom;
        } else throw Failure{XUI_WRONG_KIND, "This request has no location."};
    });
}
xui_status XUI_CALL xui_grid_tracks(xui_handle target, const xui_grid_track* rows, uint32_t row_count,
    const xui_grid_track* columns, uint32_t column_count) noexcept {
    return boundary([&] {
        auto n = get(target, XUI_GRID); editable(n->owner);
        auto tracks = [](const xui_grid_track* values, uint32_t count) {
            require(values && count && count <= 256, XUI_INVALID_ARGUMENT, "A grid needs 1..256 tracks.");
            std::vector<xui::GridTrack> result;
            for (uint32_t i = 0; i < count; ++i) {
                auto& v = values[i];
                require(v.size == sizeof(v), XUI_VERSION_MISMATCH, "Track size mismatch.");
                require(!v.reserved && v.sizing <= 2 && std::isfinite(v.value) && std::isfinite(v.minimum) && std::isfinite(v.maximum) &&
                    v.value >= 0 && v.minimum >= 0 && v.maximum >= v.minimum, XUI_INVALID_ARGUMENT, "Invalid grid track.");
                result.push_back({static_cast<xui::TrackSizing>(v.sizing), v.value, v.minimum, v.maximum});
            }
            return result;
        };
        auto r = tracks(rows, row_count), c = tracks(columns, column_count);
        as<xui::Grid>(n).set_tracks(std::move(r), std::move(c));
    });
}
xui_status XUI_CALL xui_grid_columns(xui_handle target, const xui_column* columns, uint32_t count) noexcept {
    return boundary([&] {
        auto n = get(target, XUI_DATA_GRID); editable(n->owner);
        require(columns && count && count <= 256, XUI_INVALID_ARGUMENT, "A data grid needs 1..256 columns.");
        std::vector<xui::GridColumn> result;
        for (uint32_t i = 0; i < count; ++i) {
            const auto& c = columns[i];
            require(c.size == sizeof(c), XUI_VERSION_MISMATCH, "Column size mismatch.");
            require(!c.reserved && c.flags <= 7 && std::isfinite(c.width) && c.width >= 48 && c.width <= 2000, XUI_INVALID_ARGUMENT, "Invalid data column.");
            result.push_back({decode(c.name), c.width, bool(c.flags & 1), bool(c.flags & 2), bool(c.flags & 4)});
        }
        as<xui::DataGrid>(n).set_columns(std::move(result));
    });
}
xui_status XUI_CALL xui_grid_column_width(xui_handle target, uint32_t column, float width) noexcept {
    return boundary([&] { auto n = get(target, XUI_DATA_GRID); editable(n->owner); as<xui::DataGrid>(n).set_column_width(column, width); });
}
xui_status XUI_CALL xui_grid_column_order(xui_handle target, const uint32_t* order, uint32_t count) noexcept {
    return boundary([&] {
        auto n = get(target, XUI_DATA_GRID); editable(n->owner);
        require(count <= 256 && (order || !count), XUI_INVALID_ARGUMENT, "Invalid column order span.");
        std::vector<size_t> values; for (uint32_t i = 0; i < count; ++i) values.push_back(order[i]);
        as<xui::DataGrid>(n).set_column_order(std::move(values));
    });
}
xui_status XUI_CALL xui_grid_filter(xui_handle target, uint32_t column, xui_string query) noexcept {
    return boundary([&] { auto n = get(target, XUI_DATA_GRID); editable(n->owner); as<xui::DataGrid>(n).set_filter(column, decode(query)); });
}
xui_status XUI_CALL xui_grid_sort(xui_handle target, uint32_t column, uint32_t descending) noexcept {
    return boundary([&] { auto n = get(target, XUI_DATA_GRID); editable(n->owner); limit(descending, 1); as<xui::DataGrid>(n).set_sort(column, descending != 0); });
}
xui_status XUI_CALL xui_grid_check(xui_handle target, uint64_t id, uint64_t version, uint32_t checked) noexcept {
    return boundary([&] { auto n = get(target, XUI_DATA_GRID); editable(n->owner); limit(checked, 1); as<xui::DataGrid>(n).set_checked({id, version}, checked != 0); callback_result(n->owner); });
}
xui_status XUI_CALL xui_commands_set(xui_handle target, const xui_command_record* commands, uint32_t count) noexcept {
    return boundary([&] {
        auto n = get(target); editable(n->owner);
        require(n->kind == XUI_COMMAND_BAR || n->kind == XUI_COMMAND_SURFACE, XUI_WRONG_KIND, "Expected a command surface.");
        require(count <= XUI_MAX_BATCH && (commands || !count), XUI_INVALID_ARGUMENT, "Invalid commands span.");
        std::vector<xui::CommandRecord> records; records.reserve(count);
        std::weak_ptr<Node> weak = n;
        for (uint32_t i = 0; i < count; ++i) {
            const auto& v = commands[i];
            require(v.size == sizeof(v), XUI_VERSION_MISMATCH, "Command size mismatch.");
            require(v.kind <= 2 && v.flags <= 7 && v.icon <= 13, XUI_INVALID_ARGUMENT, "Invalid command enum.");
            xui::CommandRecord r; r.id = v.id; r.parent = v.parent; r.label = decode(v.label);
            r.enabled = !(v.flags & 1); if (v.flags & 4) r.checked = bool(v.flags & 2);
            r.icon = static_cast<xui::ButtonIcon>(v.icon); r.kind = static_cast<xui::CommandKind>(v.kind);
            auto hint = decode(v.hint); if (!hint.empty()) r.shortcut_hints.push_back(std::move(hint));
            r.pin_label = decode(v.pin_label);
            if (r.kind == xui::CommandKind::action) r.action = [weak, id = v.id] { dispatch(weak, XUI_CLICK, id); };
            if (!r.pin_label.empty()) r.pin = [weak, id = v.id] { dispatch(weak, XUI_ACTION, id); };
            records.push_back(std::move(r));
        }
        auto set = std::make_shared<xui::CommandSet>(std::move(records));
        if (n->kind == XUI_COMMAND_BAR) as<xui::CommandBar>(n).set_commands(set);
        else resource<xui::CommandSurface>(n, n->kind)->set_commands(set);
        n->commands = std::move(set);
    });
}
xui_status XUI_CALL xui_command_invoke(xui_handle target, uint64_t id, uint32_t pin) noexcept {
    return boundary([&] {
        auto n = get(target); editable(n->owner); limit(pin, 1);
        require(n->commands && n->commands->invoke(id, pin != 0), XUI_INVALID_ARGUMENT, "Command is absent or disabled.");
        callback_result(n->owner);
    });
}
xui_status XUI_CALL xui_command_bind(xui_handle target, uint64_t id, uint32_t key, uint32_t modifiers) noexcept {
    return boundary([&] {
        auto n = get(target); editable(n->owner); limit(key, 255); limit(modifiers, 7);
        require(key && n->commands && n->commands->find(id), XUI_INVALID_ARGUMENT, "Invalid shortcut or command ID.");
        n->bindings.bind({static_cast<uint16_t>(key), bool(modifiers & 1), bool(modifiers & 2), bool(modifiers & 4)}, id);
        wire(get(n->owner->handles.front(), XUI_WINDOW));
    });
}
xui_status XUI_CALL xui_shell_show(xui_handle anchor, const xui_string* paths, uint32_t count) noexcept {
    return boundary([&] {
        auto n = get(anchor); editable(n->owner);
        require(paths && count && count <= 256, XUI_INVALID_ARGUMENT, "Shell selection needs 1..256 paths.");
        std::vector<std::wstring> values; for (uint32_t i = 0; i < count; ++i) values.push_back(decode(paths[i]));
        n->owner->window->show_shell_commands(control(n), values);
    });
}
xui_status XUI_CALL xui_canvas_scene(xui_handle target, const xui_shape* shapes, uint32_t count) noexcept {
    return boundary([&] {
        auto n = get(target, XUI_VECTOR_CANVAS); editable(n->owner);
        require(count <= 4096 && (shapes || !count), XUI_INVALID_ARGUMENT, "Invalid vector shape span.");
        std::vector<xui::VectorShape> result; result.reserve(count);
        uint64_t points{};
        for (uint32_t i = 0; i < count; ++i) {
            const auto& v = shapes[i];
            require(v.size == sizeof(v), XUI_VERSION_MISMATCH, "Shape size mismatch.");
            points += v.count;
            require(!v.reserved && !v.reserved2 && v.flags <= 7 && points <= 65536 && (v.points || !v.count), XUI_INVALID_ARGUMENT, "Invalid shape flags or points.");
            xui::VectorShape shape; shape.id = v.id; shape.closed = v.flags & 1; shape.interactive = v.flags & 2;
            shape.name = decode(v.name);
            shape.fill = {v.fill_red, v.fill_green, v.fill_blue, v.fill_alpha};
            shape.stroke = {v.stroke_red, v.stroke_green, v.stroke_blue, v.stroke_alpha}; shape.stroke_width = v.stroke_width;
            shape.transform = {v.m11, v.m12, v.m21, v.m22, v.dx, v.dy};
            if (v.flags & 4) shape.clip = xui::Rect{v.clip_x, v.clip_y, v.clip_width, v.clip_height};
            else require(v.clip_x == 0 && v.clip_y == 0 && v.clip_width == 0 && v.clip_height == 0, XUI_INVALID_ARGUMENT, "Unused clip must be zero.");
            shape.points.reserve(v.count);
            for (uint32_t j = 0; j < v.count; ++j) shape.points.push_back({v.points[j].x, v.points[j].y});
            result.push_back(std::move(shape));
        }
        auto scene = std::make_shared<xui::VectorScene>(std::move(result));
        as<xui::VectorCanvas>(n).set_scene(std::move(scene));
    });
}
xui_status XUI_CALL xui_map_markers(xui_handle target, const xui_map_marker* markers, uint32_t count) noexcept {
    return boundary([&] { auto n = get(target, XUI_MAP_VIEW); editable(n->owner); as<xui::MapView>(n).set_overlay(marker_overlay(markers, count)); });
}
xui_status XUI_CALL xui_map_request(xui_handle target, xui_handle* request) noexcept {
    return boundary([&] {
        require(request != nullptr, XUI_INVALID_ARGUMENT, "Missing request output."); *request = 0;
        auto n = get(target, XUI_MAP_VIEW); editable(n->owner);
        *request = request_token(n, as<xui::MapView>(n).request_overlay());
    });
}
xui_status XUI_CALL xui_map_complete(xui_handle target, xui_handle token, const xui_map_marker* markers, uint32_t count) noexcept {
    return boundary([&] {
        auto n = get(target, XUI_MAP_VIEW); auto r = get(token, request_kind); same(n, r);
        if (n->owner->closed) { revoke(r); throw Failure{XUI_CLOSED, "The request owner is closed."}; }
        auto request = resource<Request>(r, request_kind);
        require(request->target.lock() == n && std::holds_alternative<xui::MapRequest>(request->value), XUI_INVALID_ARGUMENT, "The request belongs to another control.");
        auto overlay = marker_overlay(markers, count);
        bool accepted = as<xui::MapView>(n).complete(std::get<xui::MapRequest>(request->value), std::move(overlay));
        revoke(r); require(accepted, XUI_CLOSED, "The map request is stale or canceled.");
    });
}
xui_status XUI_CALL xui_window_create_features(const xui_window_options* options, uint32_t custom_titlebar, xui_handle* out) noexcept {
    return boundary([&] {
        require(out != nullptr, XUI_INVALID_ARGUMENT, "Missing window output."); *out = 0;
        require(options && options->size == sizeof(*options) && options->version == XUI_ABI_VERSION, XUI_VERSION_MISMATCH, "Window options version mismatch.");
        require(custom_titlebar <= 1 && !options->reserved && options->theme <= 2 && std::isfinite(options->width) &&
            std::isfinite(options->height) && options->width > 0 && options->height > 0 && options->width <= 32768 && options->height <= 32768,
            XUI_INVALID_ARGUMENT, "Invalid window options.");
        auto state = std::make_shared<State>();
        xui::WindowOptions value{decode(options->title), {options->width, options->height}, static_cast<xui::ThemeMode>(options->theme)};
        value.custom_titlebar = custom_titlebar != 0; state->window = std::make_unique<xui::Window>(std::move(value));
        *out = insert(state, XUI_WINDOW);
    });
}
xui_status XUI_CALL xui_dialog_validation(xui_handle target, xui_string message) noexcept {
    return boundary([&] {
        auto n = get(target, XUI_CONTENT_DIALOG); editable(n->owner); auto text = decode(message);
        resource<xui::ContentDialog>(n, n->kind)->on_validate([text = std::move(text)] { return text; });
    });
}
xui_status XUI_CALL xui_web_origins(xui_handle target, const xui_string* origins, uint32_t count) noexcept {
    return boundary([&] {
        auto n = get(target, XUI_WEB_CONTENT); editable(n->owner);
        require(count <= 16 && (origins || !count), XUI_INVALID_ARGUMENT, "Invalid origin allowlist.");
        std::vector<std::wstring> values; for (uint32_t i = 0; i < count; ++i) values.push_back(decode(origins[i]));
        as<xui::WebContent>(n).set_allowed_origins(std::move(values));
    });
}
xui_status XUI_CALL xui_web_navigate(xui_handle target, xui_string uri) noexcept {
    return boundary([&] {
        auto n = get(target, XUI_WEB_CONTENT); editable(n->owner);
        require(xui_capabilities() & 1, XUI_NATIVE_ERROR, "WebView2 is disabled in this build.");
        as<xui::WebContent>(n).navigate(decode(uri)); callback_result(n->owner);
    });
}
xui_status XUI_CALL xui_web_evaluate(xui_handle target, xui_string script, xui_handle* token) noexcept {
    return boundary([&] {
        require(token != nullptr, XUI_INVALID_ARGUMENT, "Missing result token."); *token = 0;
        auto n = get(target, XUI_WEB_CONTENT); editable(n->owner); auto text = decode(script);
        require(text.size() <= 65536, XUI_INVALID_ARGUMENT, "JavaScript exceeds 65536 UTF-16 units.");
        auto& web = as<xui::WebContent>(n);
        const auto h = request_token(n, WebResult{web.revision(), n->web_generation});
        auto r = get(h); std::weak_ptr<Request> weak = resource<Request>(r, request_kind);
        try {
            web.evaluate(std::move(text), [weak](std::wstring value, std::wstring error_text) noexcept {
                try {
                auto request = weak.lock(); if (!request) return;
                auto node = request->target.lock(); if (!node || node->owner->closed) return;
                auto& result = std::get<WebResult>(request->value);
                if (node->web_generation != result.generation || as<xui::WebContent>(node).revision() != result.revision) return;
                result.failed = !error_text.empty();
                auto text = encode(result.failed ? error_text : value);
                if (text.size() > XUI_MAX_STRING_BYTES) { result.failed = true; text = "JavaScript result exceeds 1 MiB."; }
                result.text = std::move(text); result.done = true;
                } catch (...) {
                    if (auto request = weak.lock()) {
                        auto& result = std::get<WebResult>(request->value);
                        result.failed = true; result.done = true; result.text.clear();
                    }
                }
            });
        } catch (...) { revoke(r); throw; }
        *token = h;
    });
}
xui_status XUI_CALL xui_web_result(xui_handle token, char* buffer, uint32_t capacity, uint32_t* required, uint32_t* is_error) noexcept {
    return boundary([&] {
        require(required && is_error && (buffer || !capacity), XUI_INVALID_ARGUMENT, "Invalid result output span.");
        *required = 0; *is_error = 0;
        auto r = get(token, request_kind); editable(r->owner); auto request = resource<Request>(r, request_kind);
        require(std::holds_alternative<WebResult>(request->value), XUI_WRONG_KIND, "Expected a JavaScript request.");
        auto n = request->target.lock(); require(n != nullptr, XUI_CLOSED, "The browser was destroyed.");
        auto& result = std::get<WebResult>(request->value);
        require(n->web_generation == result.generation && as<xui::WebContent>(n).revision() == result.revision,
            XUI_CLOSED, "The JavaScript request was canceled.");
        require(result.done, XUI_BUSY, "The JavaScript result is pending.");
        *required = static_cast<uint32_t>(result.text.size()); *is_error = result.failed;
        require(capacity >= *required, XUI_BUFFER_TOO_SMALL, "The result buffer is too small.");
        if (*required) std::memcpy(buffer, result.text.data(), *required);
    });
}
