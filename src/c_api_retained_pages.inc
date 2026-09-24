namespace {
constexpr uint32_t retained_pages_kind = 0x10013;
std::vector<xui::RetainedPageEntry> page_entries(const xui_page_entry* entries, uint32_t count, uint64_t selected) {
    require(count <= 4096 && (entries || !count), XUI_INVALID_ARGUMENT, "Invalid retained page entry span.");
    std::vector<xui::RetainedPageEntry> result; result.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        require(entries[i].size == sizeof(xui_page_entry), XUI_VERSION_MISMATCH, "Retained page entry size mismatch.");
        require(entries[i].enabled <= 1, XUI_INVALID_ARGUMENT, "Invalid retained page enabled value.");
        result.push_back({entries[i].id, decode(entries[i].title), entries[i].enabled != 0});
    }
    xui::RetainedPages::validate_entries(result, selected);
    return result;
}
bool fresh_page_node(const std::shared_ptr<Node>& node) {
    return node->content_scope && node->owner->building_content == node->content_scope &&
        (!node->owner->append_first || node->handle >= node->owner->append_first);
}
bool page_context(const std::shared_ptr<Node>& node) {
    editable(node->owner);
    require(node->content_scope, XUI_INVALID_ARGUMENT, "Retained pages require a content arena.");
    if (fresh_page_node(node)) {
        require(!node->owner->source_callbacks && !node->owner->secret_callbacks &&
            node->owner->callbacks == node->owner->posted_callbacks, XUI_BUSY,
            "Construct retained pages outside native callbacks.");
        return true;
    }
    mutable_scope(get(node->content_scope, content_scope_kind));
    return false;
}
void validate_pages(const std::shared_ptr<Node>& node, const std::vector<xui::RetainedPageEntry>& entries,
    uint64_t selected, bool visible) {
    if (!page_context(node))
        inspect_content([&] { node->owner->window->validate_retained_pages(as<xui::RetainedPages>(node), entries, selected, visible); });
}
void set_retained_page_visibility(const std::shared_ptr<Node>& node, bool visible) {
    auto& pages = as<xui::RetainedPages>(node);
    if (page_context(node)) pages.set_visible(visible);
    else inspect_content([&] { node->owner->window->set_retained_pages_visible(pages, visible); });
}
std::shared_ptr<Node> page_selector(xui_handle handle) {
    auto node = get(handle);
    require(node->kind == XUI_TAB_STRIP || node->kind == XUI_NAVIGATION_VIEW, XUI_WRONG_KIND,
        "Expected a TabStrip or NavigationView page selector.");
    return node;
}
void validate_selector(const std::shared_ptr<Node>& node, const std::vector<xui::RetainedPageEntry>& entries, uint64_t selected) {
    editable(node->owner);
    if (node->kind == XUI_TAB_STRIP)
        require(std::all_of(entries.begin(), entries.end(), [](const auto& entry) { return entry.enabled; }),
            XUI_INVALID_ARGUMENT, "This retained TabStrip version does not support disabled entries.");
    if (node->page_selector_linked) {
        const auto pages = node->page_host.lock();
        require(pages != nullptr, XUI_CLOSED, "The linked retained pages have been released.");
        if (node->owner->window->contains_element(*pages->element))
            inspect_content([&] { node->owner->window->validate_retained_pages(as<xui::RetainedPages>(pages),
                entries, selected, as<xui::RetainedPages>(pages).visible()); });
    }
}
void validate_retained_page_links(const std::shared_ptr<Node>& scope, const std::shared_ptr<xui::Element>& root) {
    for (auto handle : scope->owner->handles) {
        auto node = get(handle);
        if (node->content_scope != scope->handle || !node->element ||
            (node->kind != retained_pages_kind && !node->page_selector_linked) ||
            !xui::Window::subtree_contains(root, *node->element)) continue;
        if (node->kind == retained_pages_kind) inspect_content([&] { as<xui::RetainedPages>(node).validate_complete(); });
        if (node->page_selector_linked) {
            const auto pages = node->page_host.lock();
            require(pages && xui::Window::subtree_contains(root, *pages->element), XUI_INVALID_ARGUMENT,
                "Linked page selectors and their page host must be in the same attachment root.");
        }
    }
}
void validate_retained_page_retirement(const std::vector<std::shared_ptr<Node>>& removed) {
    if (removed.empty()) return;
    for (auto handle : removed.front()->owner->handles) {
        const auto selector = get(handle);
        if (!selector->page_selector_linked || std::find(removed.begin(), removed.end(), selector) != removed.end() ||
            !selector->owner->window->contains_element(*selector->element)) continue;
        const auto pages = selector->page_host.lock();
        require(std::find(removed.begin(), removed.end(), pages) == removed.end(), XUI_BUSY,
            "Detach linked page selectors before retiring their retained page host.");
    }
}
void validate_retained_page_insertion(const std::shared_ptr<Node>& root) {
    for (auto handle : root->owner->handles) {
        const auto selector = get(handle);
        if (!selector->page_selector_linked || !xui::Window::subtree_contains(root->element, *selector->element)) continue;
        const auto pages = selector->page_host.lock();
        require(pages && (root->owner->window->contains_element(*pages->element) ||
            xui::Window::subtree_contains(root->element, *pages->element)), XUI_INVALID_ARGUMENT,
            "Insert a selector with its page host, or after that host is attached.");
    }
}
}
uint32_t XUI_CALL xui_retained_pages_version() noexcept { return XUI_RETAINED_PAGES_VERSION; }
xui_status XUI_CALL xui_retained_pages_create(xui_handle window, xui_string name, xui_handle* pages) noexcept {
    return boundary([&] {
        require(pages, XUI_INVALID_ARGUMENT, "Missing retained pages output."); *pages = 0;
        const auto node = get(window, XUI_WINDOW); topology(node->owner);
        require(node->owner->building_content, XUI_BUSY, "Create retained pages in a content candidate or append.");
        *pages = insert(node->owner, retained_pages_kind, std::make_shared<xui::RetainedPages>(decode(name)));
    }, true);
}
xui_status XUI_CALL xui_retained_pages_validate(xui_handle handle, const xui_page_entry* entries, uint32_t count, uint64_t selected) noexcept {
    return boundary([&] {
        const auto node = get(handle, retained_pages_kind);
        const auto values = page_entries(entries, count, selected);
        validate_pages(node, values, selected, as<xui::RetainedPages>(node).visible());
    }, true);
}
xui_status XUI_CALL xui_retained_pages_set(xui_handle handle, const xui_page_entry* entries, uint32_t count, uint64_t selected) noexcept {
    return boundary([&] {
        const auto node = get(handle, retained_pages_kind);
        auto values = page_entries(entries, count, selected);
        if (page_context(node)) as<xui::RetainedPages>(node).set_entries(std::move(values), selected, false);
        else inspect_content([&] { node->owner->window->set_retained_pages(as<xui::RetainedPages>(node), std::move(values), selected); });
    }, true);
}
xui_status XUI_CALL xui_retained_pages_validate_visible(xui_handle handle, uint32_t visible) noexcept {
    return boundary([&] {
        require(visible <= 1, XUI_INVALID_ARGUMENT, "Invalid retained page visibility.");
        const auto node = get(handle, retained_pages_kind);
        const auto& pages = as<xui::RetainedPages>(node);
        validate_pages(node, pages.entries(), pages.selected(), visible != 0);
    }, true);
}
xui_status XUI_CALL xui_retained_pages_set_visible(xui_handle handle, uint32_t visible) noexcept {
    return boundary([&] {
        require(visible <= 1, XUI_INVALID_ARGUMENT, "Invalid retained page visibility.");
        set_retained_page_visibility(get(handle, retained_pages_kind), visible != 0);
    }, true);
}
xui_status XUI_CALL xui_retained_pages_get_state(xui_handle handle, uint64_t* selected, uint32_t* visible, uint32_t* count) noexcept {
    return boundary([&] {
        require(selected && visible && count, XUI_INVALID_ARGUMENT, "Missing retained page state output.");
        const auto& pages = as<xui::RetainedPages>(get(handle, retained_pages_kind));
        *selected = pages.selected(); *visible = pages.visible() ? 1u : 0u; *count = static_cast<uint32_t>(pages.entries().size());
    });
}
xui_status XUI_CALL xui_retained_pages_insert(xui_handle handle, uint32_t index, uint64_t id, xui_handle child) noexcept {
    return boundary([&] {
        const auto node = get(handle, retained_pages_kind), root = get(child);
        same(node, root);
        require(root->element && !root->attached && !root->content_incomplete, XUI_INVALID_ARGUMENT,
            "Inserted page roots must be detached and complete.");
        validate_portable_reveal_insertion(node, root->element);
        if (page_context(node)) as<xui::RetainedPages>(node).insert(index, id, root->element);
        else {
            validate_retained_page_insertion(root);
            inspect_content([&] { node->owner->window->retained_pages_insert(as<xui::RetainedPages>(node), index, id, root->element); });
        }
        root->attached = true;
    }, true);
}
xui_status XUI_CALL xui_retained_pages_validate_move(xui_handle handle, xui_handle child, uint32_t index) noexcept {
    return boundary([&] {
        const auto node = get(handle, retained_pages_kind), root = get(child); same(node, root);
        require(root->element != nullptr, XUI_WRONG_KIND, "Expected a page root.");
        if (page_context(node)) as<xui::RetainedPages>(node).index_of(*root->element);
        else inspect_content([&] { node->owner->window->retained_pages_validate_move(as<xui::RetainedPages>(node), *root->element, index); });
    }, true);
}
xui_status XUI_CALL xui_retained_pages_move(xui_handle handle, xui_handle child, uint32_t index) noexcept {
    return boundary([&] {
        const auto node = get(handle, retained_pages_kind), root = get(child); same(node, root);
        require(root->element != nullptr, XUI_WRONG_KIND, "Expected a page root.");
        if (page_context(node)) as<xui::RetainedPages>(node).move(*root->element, index);
        else inspect_content([&] { node->owner->window->retained_pages_move(as<xui::RetainedPages>(node), *root->element, index); });
    }, true);
}
xui_status XUI_CALL xui_retained_pages_remove(xui_handle handle, xui_handle child) noexcept {
    return boundary([&] {
        const auto node = get(handle, retained_pages_kind), root = get(child); same(node, root);
        require(root->element != nullptr, XUI_WRONG_KIND, "Expected a page root.");
        const auto fresh = page_context(node);
        if (fresh) as<xui::RetainedPages>(node).index_of(*root->element);
        else inspect_content([&] { node->owner->window->retained_pages_validate_remove(as<xui::RetainedPages>(node), *root->element); });
        std::vector<std::shared_ptr<Node>> removed;
        for (auto child_handle : node->owner->handles) {
            const auto candidate = get(child_handle);
            if (candidate->content_scope == node->content_scope && candidate->element &&
                xui::Window::subtree_contains(root->element, *candidate->element)) removed.push_back(candidate);
        }
        validate_retained_page_retirement(removed);
        for (const auto& candidate : removed) clear_content_callbacks(candidate);
        if (fresh) as<xui::RetainedPages>(node).remove(*root->element);
        else inspect_content([&] { node->owner->window->retained_pages_remove(as<xui::RetainedPages>(node), *root->element); });
        root->attached = false;
    }, true);
}
xui_status XUI_CALL xui_page_selector_connect(xui_handle handle, xui_handle pages) noexcept {
    return boundary([&] {
        const auto node = page_selector(handle), target = get(pages, retained_pages_kind);
        editable(node->owner);
        require(node->owner == target->owner && node->content_scope && node->content_scope == target->content_scope,
            XUI_INVALID_ARGUMENT, "Page selectors and hosts require the same window and content arena.");
        if (!fresh_page_node(node)) mutation_context(node->owner);
        require(!node->page_selector_linked || node->page_host.lock() == target, XUI_INVALID_ARGUMENT,
            "A page selector cannot be rebound to a different host.");
        node->page_host = target; node->page_selector_linked = true;
    });
}
xui_status XUI_CALL xui_page_selector_validate(xui_handle handle, const xui_page_entry* entries, uint32_t count, uint64_t selected) noexcept {
    return boundary([&] { validate_selector(page_selector(handle), page_entries(entries, count, selected), selected); }, true);
}
xui_status XUI_CALL xui_page_selector_set(xui_handle handle, const xui_page_entry* entries, uint32_t count, uint64_t selected) noexcept {
    return boundary([&] {
        const auto node = page_selector(handle);
        auto values = page_entries(entries, count, selected);
        if (node->page_selector_linked && !fresh_page_node(node)) mutation_context(node->owner);
        validate_selector(node, values, selected);
        if (node->page_selector_linked && !fresh_page_node(node)) {
            const auto target = node->page_host.lock();
            require(target && as<xui::RetainedPages>(target).entries() == values &&
                as<xui::RetainedPages>(target).selected() == selected, XUI_INVALID_ARGUMENT,
                "Project the authoritative current page metadata into its selector.");
        }
        require(!node->page_projection, XUI_BUSY, "Page metadata projection cannot reenter.");
        node->page_projection = true;
        struct Reset { Node& node; ~Reset() { node.page_projection = false; } } reset{*node};
        if (node->kind == XUI_TAB_STRIP) {
            std::vector<xui::TabItem> tabs; tabs.reserve(values.size());
            for (auto& entry : values) tabs.push_back({entry.id, std::move(entry.title)});
            as<xui::TabStrip>(node).set_tabs(std::move(tabs), selected ? std::optional<uint64_t>{selected} : std::nullopt);
        } else {
            std::vector<xui::NavigationItem> items; items.reserve(values.size());
            for (auto& entry : values) {
                xui::NavigationItem item{}; item.key = {entry.id, 0}; item.label = std::move(entry.title); item.enabled = entry.enabled;
                items.push_back(std::move(item));
            }
            auto& navigation = as<xui::NavigationView>(node);
            navigation.set_search_visible(false);
            navigation.set_filter({});
            navigation.set_items(std::move(items));
            navigation.set_selection(selected ? std::optional<xui::ItemKey>{{selected, 0}} : std::nullopt);
        }
    }, true);
}
xui_status XUI_CALL xui_page_selector_set_closable(xui_handle handle, uint32_t closable) noexcept {
    return boundary([&] {
        require(closable <= 1, XUI_INVALID_ARGUMENT, "Invalid TabStrip closability.");
        const auto node = get(handle, XUI_TAB_STRIP); editable(node->owner);
        node->page_closable = closable != 0;
        wire_feature(node);
        node->element->invalidate(xui::Invalidation::layout);
    });
}
xui_status XUI_CALL xui_page_selector_get_closable(xui_handle handle, uint32_t* closable) noexcept {
    return boundary([&] {
        require(closable, XUI_INVALID_ARGUMENT, "Missing TabStrip closability output.");
        *closable = as<xui::TabStrip>(get(handle, XUI_TAB_STRIP)).closable() ? 1u : 0u;
    });
}
xui_status XUI_CALL xui_retained_pages_get_page_id(xui_handle handle, uint32_t index, uint64_t* id) noexcept {
    return boundary([&] {
        require(id, XUI_INVALID_ARGUMENT, "Missing retained page identity output.");
        const auto& pages = as<xui::RetainedPages>(get(handle, retained_pages_kind));
        require(index < pages.page_count(), XUI_INVALID_ARGUMENT, "Retained page index is out of range.");
        *id = pages.page_id(index);
    });
}
xui_status XUI_CALL xui_page_selector_get_state(xui_handle handle, uint64_t* selected, uint32_t* count) noexcept {
    return boundary([&] {
        require(selected && count, XUI_INVALID_ARGUMENT, "Missing page selector state output.");
        const auto node = page_selector(handle);
        if (node->kind == XUI_TAB_STRIP) {
            const auto& tabs = as<xui::TabStrip>(node);
            *selected = tabs.selected().value_or(0); *count = static_cast<uint32_t>(tabs.tabs().size());
        } else {
            const auto& navigation = as<xui::NavigationView>(node);
            *selected = navigation.selected() ? navigation.selected()->id : 0;
            *count = static_cast<uint32_t>(navigation.entries().size());
        }
    });
}
