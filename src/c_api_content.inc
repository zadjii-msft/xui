namespace {
constexpr uint32_t content_host_kind = 104;
constexpr uint32_t content_scope_kind = 105;
struct ContentScope {
    xui_handle host{};
    bool committed{};
    std::vector<xui::ContentInspectionTarget> inspection_targets;
};
template<class F> auto inspect_content(F&& body) {
    try { return body(); }
    catch (const std::invalid_argument& error) { throw InspectionFailure{XUI_INVALID_ARGUMENT, error.what()}; }
    catch (const std::logic_error& error) { throw InspectionFailure{XUI_BUSY, error.what()}; }
}
std::shared_ptr<ContentScope> content_scope(const std::shared_ptr<Node>& node) {
    require(node->kind == content_scope_kind, XUI_WRONG_KIND, "Expected a content scope.");
    return std::static_pointer_cast<ContentScope>(node->resource);
}
void retire_content(const std::shared_ptr<Node>& scope) {
    const auto state = scope->owner;
    std::vector<std::shared_ptr<Node>> removed;
    removed.reserve(state->handles.size());
    {
        std::lock_guard lock(registry_mutex);
        for (auto h : state->handles) {
            const auto found = registry.find(h);
            if (found == registry.end() || (found->second->content_scope != scope->handle && h != scope->handle)) continue;
            auto& n = found->second;
            n->callback = nullptr; n->context = nullptr;
            n->menu_callback = nullptr; n->menu_context = nullptr;
            n->file_drag_callback = nullptr; n->file_drop_callback = nullptr;
            n->miller_callback = nullptr;
            removed.push_back(std::move(n));
            registry.erase(found);
        }
        std::erase_if(state->handles, [&](xui_handle h) { return !registry.contains(h); });
    }
    if (state->building_content == scope->handle) state->building_content = 0;
    if (state->content_context == scope->handle) state->content_context = 0;
    // Resource destructors can call foreign release callbacks. Never hold the registry mutex here.
    removed.clear();
    if (state->button_styles) std::erase_if(*state->button_styles, [](const auto& item) { return item.second.value.expired(); });
    if (state->control_styles) std::erase_if(*state->control_styles, [](const auto& item) { return item.second.expired(); });
}
void clear_content(const std::shared_ptr<Node>& host) {
    require(!host->owner->source_callbacks && !host->owner->secret_callbacks, XUI_BUSY,
        "Read-only callbacks cannot release content.");
    require(!host->owner->building_content, XUI_BUSY, "Finish the candidate before clearing content.");
    if (!host->current_content) return;
    auto scope = get(host->current_content, content_scope_kind);
    if (!host->owner->closed)
        host->owner->window->replace_content(as<xui::ContentHost>(host), {});
    host->current_content = 0;
    retire_content(scope);
}
}
xui_status XUI_CALL xui_content_host_create(xui_handle window, xui_handle* result) noexcept {
    return boundary([&] {
        require(result, XUI_INVALID_ARGUMENT, "Missing host output."); *result = 0;
        auto n = get(window, XUI_WINDOW);
        require(!n->owner->used && !n->owner->building_content, XUI_BUSY, "Create the content host before run, outside a candidate.");
        editable(n->owner);
        *result = insert(n->owner, content_host_kind, std::make_shared<xui::ContentHost>());
    });
}
xui_status XUI_CALL xui_content_begin(xui_handle host, xui_handle* result) noexcept {
    return boundary([&] {
        require(result, XUI_INVALID_ARGUMENT, "Missing scope output."); *result = 0;
        auto n = get(host, content_host_kind); editable(n->owner);
        auto state = n->owner;
        require(!state->building_content && !state->content_context, XUI_BUSY, "Content updates cannot nest or start in scoped callbacks.");
        auto value = std::make_shared<ContentScope>(); value->host = host;
        auto handle = insert(state, content_scope_kind);
        get(handle)->resource = std::move(value);
        state->building_content = state->content_context = handle;
        *result = handle;
    });
}
xui_status XUI_CALL xui_content_commit(xui_handle scope, xui_handle root) noexcept {
    return boundary([&] {
        auto n = get(scope, content_scope_kind); auto state = n->owner; editable(state);
        auto value = content_scope(n);
        require(!value->committed && state->building_content == scope && state->content_context == scope,
            XUI_BUSY, "Only the active candidate can commit.");
        auto child = get(root);
        require(child->owner == state && child->content_scope == scope && child->element && !child->attached,
            XUI_INVALID_ARGUMENT, "The root must be an unattached element from this candidate.");
        auto host = get(value->host, content_host_kind);
        auto previous = host->current_content ? get(host->current_content, content_scope_kind) : nullptr;
        state->building_content = state->content_context = 0;
        try {
            if (value->inspection_targets.empty())
                state->window->replace_content(as<xui::ContentHost>(host), child->element);
            else inspect_content([&] {
                state->window->replace_content(as<xui::ContentHost>(host), child->element,
                    value->inspection_targets, [weak = std::weak_ptr<Node>(n)](uint32_t key) {
                        dispatch(weak, XUI_SELECTION, key);
                    });
            });
        }
        catch (...) {
            state->building_content = state->content_context = scope;
            throw;
        }
        child->attached = true;
        value->committed = true;
        host->current_content = scope;
        state->building_content = state->content_context = 0;
        if (previous) retire_content(previous);
    });
}
xui_status XUI_CALL xui_content_release(xui_handle scope) noexcept {
    return boundary([&] {
        auto n = get(scope, content_scope_kind); auto value = content_scope(n);
        require(!n->owner->source_callbacks && !n->owner->secret_callbacks, XUI_BUSY,
            "Read-only callbacks cannot release content.");
        require(!n->owner->content_context || n->owner->building_content == scope, XUI_BUSY,
            "Return from the scoped callback before releasing content.");
        if (value->committed) clear_content(get(value->host, content_host_kind));
        else retire_content(n);
    });
}
xui_status XUI_CALL xui_content_clear(xui_handle host) noexcept {
    return boundary([&] {
        auto n = get(host, content_host_kind);
        require(!n->owner->content_context, XUI_BUSY, "Return from the scoped callback before clearing content.");
        clear_content(n);
    });
}
xui_status XUI_CALL xui_content_context(xui_handle window, xui_handle scope, xui_handle* previous) noexcept {
    return boundary([&] {
        require(previous, XUI_INVALID_ARGUMENT, "Missing context output.");
        auto n = get(window, XUI_WINDOW);
        if (scope) {
            auto s = get(scope, content_scope_kind);
            require(s->owner == n->owner, XUI_INVALID_ARGUMENT, "Scope belongs to another window.");
        }
        require(!n->owner->building_content || scope == n->owner->building_content,
            XUI_BUSY, "Finish the active candidate before changing context.");
        *previous = n->owner->content_context;
        n->owner->content_context = scope;
    });
}
xui_status XUI_CALL xui_content_owner(xui_handle target, xui_handle* scope) noexcept {
    return boundary([&] {
        require(scope, XUI_INVALID_ARGUMENT, "Missing scope output.");
        *scope = get(target)->content_scope;
    });
}
xui_status XUI_CALL xui_content_handle_count(xui_handle window, uint32_t* count) noexcept {
    return boundary([&] {
        require(count, XUI_INVALID_ARGUMENT, "Missing count output.");
        *count = static_cast<uint32_t>(get(window, XUI_WINDOW)->owner->handles.size());
    });
}
xui_status XUI_CALL xui_content_inspection_targets(xui_handle scope,
    const xui_content_inspection_target* targets, uint32_t count, xui_callback picked, void* context) noexcept {
    return boundary([&] {
        auto n = get(scope, content_scope_kind);
        auto value = content_scope(n);
        editable(n->owner);
        require(!value->committed && n->owner->building_content == scope && n->owner->content_context == scope,
            XUI_BUSY, "Register inspection targets in the active candidate.");
        require(targets && count > 0 && count <= 65536, XUI_INVALID_ARGUMENT, "Invalid inspection target span.");
        std::vector<xui::ContentInspectionTarget> prepared;
        std::vector<bool> keys(count);
        prepared.reserve(count);
        for (uint32_t i = 0; i < count; ++i) {
            const auto& target = targets[i];
            require(!target.reserved && target.key < count && !keys[target.key], XUI_INVALID_ARGUMENT,
                "Inspection keys must be unique and dense, starting at zero.");
            keys[target.key] = true;
            auto element = get(target.element);
            require(element->owner == n->owner && element->content_scope == scope && element->element,
                XUI_INVALID_ARGUMENT, "Inspection targets must be elements from this candidate.");
            prepared.push_back({target.key, element->element});
        }
        value->inspection_targets = std::move(prepared);
        n->callback = picked;
        n->context = picked ? context : nullptr;
    });
}
xui_status XUI_CALL xui_content_pointer_picking(xui_handle host, uint32_t enabled) noexcept {
    return boundary([&] {
        require(enabled <= 1, XUI_INVALID_ARGUMENT, "Invalid pointer-picking mode.");
        auto n = get(host, content_host_kind);
        editable(n->owner);
        require(!n->owner->building_content && !n->owner->content_context, XUI_BUSY,
            "Change pointer-picking mode outside content construction and callbacks.");
        inspect_content([&] { n->owner->window->set_content_pointer_picking(as<xui::ContentHost>(n), enabled != 0); });
    });
}
xui_status XUI_CALL xui_content_hit_test(xui_handle host, float x, float y, uint32_t* key, uint32_t* found) noexcept {
    return boundary([&] {
        require(key && found && std::isfinite(x) && std::isfinite(y), XUI_INVALID_ARGUMENT, "Invalid hit-test arguments.");
        *key = *found = 0;
        auto n = get(host, content_host_kind);
        require(!n->owner->closed, XUI_CLOSED, "The window is closed.");
        const auto result = inspect_content([&] { return n->owner->window->hit_test_content(as<xui::ContentHost>(n), {x, y}); });
        if (result) { *key = *result; *found = 1; }
    });
}
xui_status XUI_CALL xui_content_highlight(xui_handle scope, uint32_t key, uint32_t clear, uint32_t* result) noexcept {
    return boundary([&] {
        require(result && clear <= 1, XUI_INVALID_ARGUMENT, "Invalid highlight arguments.");
        auto n = get(scope, content_scope_kind);
        auto value = content_scope(n);
        editable(n->owner);
        auto host = get(value->host, content_host_kind);
        require(value->committed && host->current_content == scope && !n->owner->building_content && !n->owner->content_context,
            XUI_BUSY, "Highlight committed content outside construction and scoped callbacks.");
        *result = static_cast<uint32_t>(inspect_content([&] {
            return n->owner->window->highlight_content(as<xui::ContentHost>(host),
                clear ? std::nullopt : std::optional<uint32_t>(key));
        }));
    });
}
