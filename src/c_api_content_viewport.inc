namespace {
constexpr uint32_t content_viewport_kind = 0x10012;
struct ContentViewportResource {
    std::weak_ptr<Node> host;
    std::uint64_t observation{};
    xui_content_viewport_callback callback{};
    void* context{};
};
void clear_content_viewport_callbacks(const std::shared_ptr<Node>& node) {
    if (node->kind != content_viewport_kind || !node->resource) return;
    const auto resource = std::static_pointer_cast<ContentViewportResource>(node->resource);
    resource->callback = nullptr; resource->context = nullptr;
    if (resource->observation && node->owner->window)
        node->owner->window->release_content_viewport(std::exchange(resource->observation, 0));
}
}
uint32_t XUI_CALL xui_content_viewport_version() noexcept { return XUI_CONTENT_VIEWPORT_VERSION; }
xui_status XUI_CALL xui_content_viewport_subscribe(xui_handle host, xui_content_viewport_callback callback,
    void* context, xui_content_viewport* initial, xui_handle* subscription) noexcept {
    return boundary([&] {
        require(subscription, XUI_INVALID_ARGUMENT, "Missing content viewport subscription output."); *subscription = 0;
        require(initial && initial->size == sizeof(*initial) && initial->version == XUI_CONTENT_VIEWPORT_VERSION,
            XUI_VERSION_MISMATCH, "Content viewport snapshot version mismatch.");
        require(callback, XUI_INVALID_ARGUMENT, "A content viewport callback is required.");
        auto target = get(host, content_host_kind); editable(target->owner); mutation_context(target->owner);
        auto resource = std::make_shared<ContentViewportResource>();
        resource->host = target; resource->callback = callback; resource->context = context;
        const auto handle = insert(target->owner, content_viewport_kind);
        auto node = get(handle); node->resource = resource;
        // The persistent host, not its replaceable current arena, owns this observer.
        node->content_scope = target->content_scope;
        try {
            std::function<void(xui::Size)> listener = [weak = std::weak_ptr<Node>(node)](xui::Size size) {
                const auto node = weak.lock();
                if (!node || !node->owner->window || node->owner->closed ||
                    node->owner->window->state() >= xui::WindowState::closing) return;
                const auto resource = std::static_pointer_cast<ContentViewportResource>(node->resource);
                if (!resource->callback || resource->host.expired()) return;
                const xui_content_viewport snapshot{sizeof(snapshot), XUI_CONTENT_VIEWPORT_VERSION, size.width, size.height};
                readonly_notification(node->owner, [&] { return resource->callback(resource->context, &snapshot); });
            };
            xui::Size size;
            resource->observation = target->owner->window->observe_content_viewport(
                std::static_pointer_cast<xui::ContentHost>(target->element), std::move(listener), size);
            *initial = {sizeof(*initial), XUI_CONTENT_VIEWPORT_VERSION, size.width, size.height};
            *subscription = handle;
        } catch (...) {
            clear_content_viewport_callbacks(node);
            erase_virtual_handle(node);
            throw;
        }
    }, true);
}
xui_status XUI_CALL xui_content_viewport_release(xui_handle subscription) noexcept {
    return boundary([&] {
        auto node = get(subscription, content_viewport_kind);
        require(!node->owner->source_callbacks && !node->owner->secret_callbacks, XUI_BUSY,
            "Release content viewport subscriptions outside read-only callbacks.");
        clear_content_viewport_callbacks(node);
        erase_virtual_handle(node);
    });
}
