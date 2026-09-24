namespace {
struct ImageMemoryResource {
    uint64_t generation{};
    xui_image_memory_callback callback{};
    void* context{};
    uint32_t sent{};
};
xui_image_memory_state image_memory_snapshot(const xui::Image& image) {
    const auto value = image.memory_state();
    return {sizeof(xui_image_memory_state), XUI_IMAGE_MEMORY_VERSION, value.generation,
        value.presentation_failed ? 4u : static_cast<uint32_t>(value.state),
        value.source.width, value.source.height, value.pixels.width, value.pixels.height, 0};
}
void clear_image_memory_callbacks(const std::shared_ptr<Node>& node) {
    if (node->kind != XUI_IMAGE) return;
    if (node->resource) {
        const auto resource = std::static_pointer_cast<ImageMemoryResource>(node->resource);
        resource->callback = nullptr; resource->context = nullptr;
    }
    auto& image = as<xui::Image>(node);
    if (image.memory_backed()) { image.on_memory_state({}); image.cancel_memory(); }
}
void queue_image_memory(const std::weak_ptr<Node>& weak_node, const std::weak_ptr<ImageMemoryResource>& weak_resource) {
    const auto node = weak_node.lock(); const auto resource = weak_resource.lock();
    if (!node || !resource || !resource->callback || node->resource != resource || !node->owner->window ||
        node->owner->closed || node->owner->window->state() >= xui::WindowState::closing) return;
    const auto& image = as<xui::Image>(node);
    const auto snapshot = image_memory_snapshot(image);
    if (snapshot.generation != resource->generation || snapshot.state < 2 || snapshot.state > 4 ||
        (resource->sent & (1u << snapshot.state))) return;
    require(image.error().size() <= 4096 && image.error().find(L'\0') == std::wstring::npos, XUI_NATIVE_ERROR,
        "Invalid native image error message.");
    auto error_text = encode(image.error());
    resource->sent |= 1u << snapshot.state;
    if (!node->owner->window->post([weak_node, weak_resource, snapshot, error_text = std::move(error_text)] {
        const auto node = weak_node.lock(); const auto resource = weak_resource.lock();
        if (!node || !resource || !resource->callback || node->resource != resource || !node->owner->window ||
            node->owner->closed || node->owner->window->state() >= xui::WindowState::closing ||
            as<xui::Image>(node).memory_state().generation != snapshot.generation ||
            as<xui::Image>(node).memory_state().state == xui::ImageStatus::empty) return;
        const xui_string error{error_text.data(), static_cast<uint32_t>(error_text.size()), 0};
        readonly_notification(node->owner, [&] { return resource->callback(resource->context, &snapshot, error); });
    })) {
        require(node->owner->window->state() >= xui::WindowState::closing, XUI_NATIVE_ERROR,
            "Native image completion could not be posted.");
    }
}
}
uint32_t XUI_CALL xui_image_memory_version() noexcept { return XUI_IMAGE_MEMORY_VERSION; }
xui_status XUI_CALL xui_image_memory_set(xui_handle handle, const xui_image_memory_options* options,
    const uint8_t* bytes, uint32_t length, xui_image_memory_callback callback, void* context) noexcept {
    return boundary([&] {
        require(options && options->size == sizeof(*options) && options->version == XUI_IMAGE_MEMORY_VERSION,
            XUI_VERSION_MISMATCH, "Memory image options version mismatch.");
        require(!options->reserved && options->format <= 1 && bytes && length &&
            length <= xui::ImageLimits::file_bytes && callback, XUI_INVALID_ARGUMENT, "Invalid memory image payload.");
        const auto node = get(handle, XUI_IMAGE); editable(node->owner);
        require(node->owner->callbacks == node->owner->posted_callbacks, XUI_BUSY,
            "Set memory images outside native callbacks.");
        xui::ImageMemoryPlan plan{options->generation, static_cast<xui::ImageMemoryFormat>(options->format),
            {options->source_width, options->source_height}, {options->output_width, options->output_height},
            {options->hint_width, options->hint_height}};
        auto resource = std::make_shared<ImageMemoryResource>();
        resource->generation = options->generation; resource->callback = callback; resource->context = context;
        std::function<void()> notify = [weak_node = std::weak_ptr<Node>(node), weak_resource = std::weak_ptr<ImageMemoryResource>(resource)] {
            queue_image_memory(weak_node, weak_resource);
        };
        auto& image = as<xui::Image>(node);
        image.set_memory_source({reinterpret_cast<const std::byte*>(bytes), length}, plan);
        if (node->resource) {
            const auto previous = std::static_pointer_cast<ImageMemoryResource>(node->resource);
            previous->callback = nullptr; previous->context = nullptr;
        }
        node->resource = std::move(resource);
        image.on_memory_state(std::move(notify));
        node->owner->window->refresh_image_memory();
    }, true);
}
xui_status XUI_CALL xui_image_memory_cancel(xui_handle handle) noexcept {
    return boundary([&] {
        const auto node = get(handle, XUI_IMAGE);
        require(!node->owner->source_callbacks && !node->owner->secret_callbacks, XUI_BUSY,
            "Cancel memory images outside read-only callbacks.");
        if (node->owner->window->state() < xui::WindowState::closing)
            require(node->owner->callbacks == node->owner->posted_callbacks, XUI_BUSY,
                "Post memory image cancellation outside native callbacks.");
        clear_image_memory_callbacks(node);
        as<xui::Image>(node).cancel_memory();
        node->owner->window->refresh_image_memory();
    });
}
xui_status XUI_CALL xui_image_memory_get_state(xui_handle handle, xui_image_memory_state* state) noexcept {
    return boundary([&] {
        require(state && state->size == sizeof(*state) && state->version == XUI_IMAGE_MEMORY_VERSION,
            XUI_VERSION_MISMATCH, "Memory image state version mismatch.");
        const auto& image = as<xui::Image>(get(handle, XUI_IMAGE));
        require(image.memory_backed() || image.source().empty(), XUI_INVALID_ARGUMENT, "This Image uses a legacy file source.");
        *state = image_memory_snapshot(image);
    });
}
xui_status XUI_CALL xui_image_memory_get_statistics(xui_image_memory_statistics* statistics) noexcept {
    return boundary([&] {
        require(statistics && statistics->size == sizeof(*statistics) && statistics->version == XUI_IMAGE_MEMORY_VERSION,
            XUI_VERSION_MISMATCH, "Memory image statistics version mismatch.");
        const auto value = xui::ImageResources::statistics();
        *statistics = {sizeof(*statistics), XUI_IMAGE_MEMORY_VERSION, value.encoded_bytes, value.encoded_peak, xui::ImageLimits::encoded_bytes};
    });
}
