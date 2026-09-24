namespace {
constexpr uint32_t virtual_viewport_kind = 0x10011;
struct VirtualLeaseResource {
    std::weak_ptr<Node> scroll;
    std::shared_ptr<xui::VirtualViewport> state;
    xui_virtual_viewport_callback callback{};
    void* context{};
    bool queued{};
    std::uint64_t generation{1};
    ~VirtualLeaseResource() { if (state) state->close(); }
    void close() noexcept {
        callback = nullptr; context = nullptr; ++generation;
        if (state) state->close();
    }
};
std::shared_ptr<VirtualLeaseResource> virtual_resource(const std::shared_ptr<Node>& node) {
    require(node->kind == virtual_viewport_kind, XUI_WRONG_KIND, "Expected a virtual viewport lease.");
    return std::static_pointer_cast<VirtualLeaseResource>(node->resource);
}
std::shared_ptr<Node> virtual_scroll(const std::shared_ptr<Node>& lease) {
    const auto resource = virtual_resource(lease);
    const auto scroll = resource->scroll.lock();
    require(scroll && resource->state && !resource->state->closed(), XUI_CLOSED, "The virtual viewport lease is closed.");
    require(as<xui::ScrollView>(scroll).virtual_viewport() == resource->state, XUI_CLOSED, "The virtual viewport lease was replaced.");
    return scroll;
}
xui_virtual_viewport_rect virtual_rect(const xui::VirtualViewportRect& value) {
    return {value.offset, value.width, value.height, value.extent};
}
xui_virtual_viewport_request virtual_request(const xui::VirtualViewportRequest& value) {
    return {sizeof(xui_virtual_viewport_request), XUI_VIRTUAL_VIEWPORT_VERSION, value.epoch,
        value.committed_source_version, value.requested_source_version, virtual_rect(value.committed),
        virtual_rect(value.requested), value.blocked ? 1u : 0u, 0};
}
template<class F> void readonly_notification(const std::shared_ptr<State>& state, F&& callback) {
    ++state->callbacks; ++state->source_callbacks;
    xui_status result{};
    try { result = callback(); } catch (...) { result = XUI_CALLBACK_FAILED; }
    --state->source_callbacks; --state->callbacks;
    if (result) {
        state->callback_failure = result;
        state->window->close();
    }
}
void queue_virtual_request(const std::weak_ptr<Node>& weak) {
    const auto node = weak.lock();
    if (!node || !node->owner->window || node->owner->closed) return;
    const auto resource = virtual_resource(node);
    if (resource->queued || !resource->callback || resource->state->closed()) return;
    resource->queued = true;
    const auto generation = resource->generation;
    if (!node->owner->window->post([weak, generation] {
        const auto node = weak.lock();
        if (!node || !node->owner->window || node->owner->closed) return;
        const auto resource = virtual_resource(node);
        resource->queued = false;
        if (resource->generation != generation || !resource->callback || resource->state->closed() ||
            resource->state->updating() || node->owner->window->state() >= xui::WindowState::closing) return;
        const auto request = virtual_request(resource->state->request());
        readonly_notification(node->owner, [&] { return resource->callback(resource->context, &request); });
    })) {
        resource->queued = false;
        require(node->owner->window->state() >= xui::WindowState::closing, XUI_NATIVE_ERROR,
            "Virtual viewport notification could not be posted.");
    }
}
void clear_virtual_callbacks(const std::shared_ptr<Node>& node) {
    if (node->interaction_callback && node->owner->window && node->element)
        node->owner->window->observe_interaction(std::dynamic_pointer_cast<xui::Control>(node->element), {});
    node->interaction_callback = nullptr; node->interaction_context = nullptr; ++node->interaction_generation;
    if (auto lease = node->virtual_lease.lock()) clear_virtual_callbacks(lease);
    node->virtual_lease.reset();
    if (auto lease = node->virtual_item_lease.lock(); lease && node->element) {
        const auto resource = virtual_resource(lease);
        if (resource->state) resource->state->remove_item(node->element->id());
    }
    node->virtual_item_lease.reset();
    if (node->kind == virtual_viewport_kind && node->resource) virtual_resource(node)->close();
}
void erase_virtual_handle(const std::shared_ptr<Node>& node) {
    std::lock_guard lock(registry_mutex);
    registry.erase(node->handle);
    std::erase(node->owner->handles, node->handle);
}
}
uint32_t XUI_CALL xui_virtual_viewport_version() noexcept { return XUI_VIRTUAL_VIEWPORT_VERSION; }
xui_status XUI_CALL xui_virtual_viewport_set_item(xui_handle handle, xui_handle row, const xui_virtual_item_info* info) noexcept {
    return boundary([&] {
        require(info && info->size == sizeof(*info) && info->version == XUI_VIRTUAL_ITEM_VERSION,
            XUI_VERSION_MISMATCH, "Virtual item version mismatch.");
        require(!info->reserved && info->key && info->key_length && info->key_length <= 4096,
            XUI_INVALID_ARGUMENT, "Virtual row keys require 1..4096 UTF-16 code units.");
        auto lease = get(handle, virtual_viewport_kind); auto scroll = virtual_scroll(lease);
        auto item = get(row, XUI_STACK); same(lease, item); editable(lease->owner); mutation_context(lease->owner);
        const auto resource = virtual_resource(lease);
        require(resource->state->updating(), XUI_BUSY, "Register virtual rows inside a Ready update.");
        require(xui::Window::subtree_contains(as<xui::ScrollView>(scroll).content(), *item->element),
            XUI_INVALID_ARGUMENT, "A virtual row must be retained by its leased ScrollView.");
        if (const auto previous = item->virtual_item_lease.lock(); previous && previous != lease)
            require(virtual_resource(previous)->state->closed(), XUI_INVALID_ARGUMENT, "A row belongs to another live viewport lease.");
        xui::VirtualItemInfo value{{}, info->index, info->count, info->source_version};
        value.key.reserve(info->key_length);
        for (uint32_t i = 0; i < info->key_length; ++i) {
            require(info->key[i] != 0, XUI_INVALID_ARGUMENT, "Virtual row keys must not contain NUL.");
            value.key.push_back(static_cast<char16_t>(info->key[i]));
        }
        resource->state->set_item(std::static_pointer_cast<xui::Stack>(item->element), std::move(value));
        item->virtual_item_lease = lease;
    }, true);
}
xui_status XUI_CALL xui_virtual_viewport_begin(xui_handle scroll, const xui_virtual_viewport_options* options,
    xui_virtual_viewport_callback callback, void* context, xui_handle* result) noexcept {
    return boundary([&] {
        require(result, XUI_INVALID_ARGUMENT, "Missing virtual viewport lease output."); *result = 0;
        require(options && options->size == sizeof(*options) && options->version == XUI_VIRTUAL_VIEWPORT_VERSION,
            XUI_VERSION_MISMATCH, "Virtual viewport options version mismatch.");
        require(callback, XUI_INVALID_ARGUMENT, "A virtual viewport callback is required.");
        auto target = get(scroll, XUI_SCROLL_VIEW);
        editable(target->owner); mutation_context(target->owner);
        validate_portable_reveal_virtualization(target);
        inspect_content([&] { target->owner->window->validate_content_mutation(); });
        require(target->owner->window->contains_element(*target->element), XUI_INVALID_ARGUMENT,
            "Virtual viewports require an attached ScrollView.");
        const auto previous = as<xui::ScrollView>(target).virtual_viewport();
        require(!previous || previous->closed(), XUI_BUSY, "The ScrollView already owns a virtual viewport lease.");
        auto resource = std::make_shared<VirtualLeaseResource>();
        resource->scroll = target; resource->callback = callback; resource->context = context;
        const auto handle = insert(target->owner, virtual_viewport_kind);
        auto lease = get(handle);
        lease->content_scope = target->content_scope;
        lease->resource = resource;
        try {
            std::weak_ptr<Node> weak = lease;
            std::function<void()> notify = [weak] { queue_virtual_request(weak); };
            resource->state = as<xui::ScrollView>(target).begin_virtual_viewport(options->item_count,
                options->row_height, options->source_version);
            resource->state->on_request(std::move(notify));
            target->virtual_lease = lease;
            *result = handle;
        } catch (...) {
            resource->close();
            erase_virtual_handle(lease);
            throw;
        }
    }, true);
}
xui_status XUI_CALL xui_virtual_viewport_set_extent(xui_handle handle, uint32_t count, uint64_t source) noexcept {
    return boundary([&] {
        auto lease = get(handle, virtual_viewport_kind); virtual_scroll(lease); editable(lease->owner);
        virtual_resource(lease)->state->set_extent(count, source);
    }, true);
}
xui_status XUI_CALL xui_virtual_viewport_request_offset(xui_handle handle, float offset) noexcept {
    return boundary([&] {
        auto lease = get(handle, virtual_viewport_kind); virtual_scroll(lease); editable(lease->owner);
        virtual_resource(lease)->state->request_offset(offset);
    }, true);
}
xui_status XUI_CALL xui_virtual_viewport_try_begin_update(xui_handle handle, uint64_t epoch, uint32_t* result) noexcept {
    return boundary([&] {
        require(result && epoch && epoch <= INT64_MAX, XUI_INVALID_ARGUMENT, "Invalid virtual viewport begin arguments.");
        auto lease = get(handle, virtual_viewport_kind); auto scroll = virtual_scroll(lease);
        editable(lease->owner); mutation_context(lease->owner);
        *result = static_cast<uint32_t>(inspect_content([&] {
            return lease->owner->window->begin_virtual_update(as<xui::ScrollView>(scroll), epoch);
        }));
    });
}
xui_status XUI_CALL xui_virtual_viewport_try_commit(xui_handle handle, uint64_t epoch, uint32_t* result) noexcept {
    return boundary([&] {
        require(result && epoch && epoch <= INT64_MAX, XUI_INVALID_ARGUMENT, "Invalid virtual viewport commit arguments.");
        auto lease = get(handle, virtual_viewport_kind); auto scroll = virtual_scroll(lease);
        editable(lease->owner); mutation_context(lease->owner);
        *result = static_cast<uint32_t>(lease->owner->window->commit_virtual_update(as<xui::ScrollView>(scroll), epoch));
    }, true);
}
xui_status XUI_CALL xui_virtual_viewport_cancel(xui_handle handle, uint64_t epoch) noexcept {
    return boundary([&] {
        auto lease = get(handle, virtual_viewport_kind); virtual_scroll(lease);
        require(!lease->owner->source_callbacks && !lease->owner->secret_callbacks, XUI_BUSY, "Read-only callbacks cannot cancel viewport updates.");
        virtual_resource(lease)->state->cancel(epoch);
    }, true);
}
xui_status XUI_CALL xui_virtual_viewport_flush_committed(xui_handle handle, uint64_t epoch) noexcept {
    return boundary([&] {
        require(epoch && epoch <= INT64_MAX, XUI_INVALID_ARGUMENT, "Invalid committed viewport epoch.");
        auto lease = get(handle, virtual_viewport_kind);
        auto scroll = virtual_scroll(lease);
        editable(lease->owner); mutation_context(lease->owner);
        lease->owner->window->flush_virtual_viewport(as<xui::ScrollView>(scroll), epoch);
    }, true);
}
xui_status XUI_CALL xui_virtual_viewport_release(xui_handle handle) noexcept {
    return boundary([&] {
        auto lease = get(handle, virtual_viewport_kind);
        require(!lease->owner->source_callbacks && !lease->owner->secret_callbacks, XUI_BUSY, "Read-only callbacks cannot release viewports.");
        auto resource = virtual_resource(lease);
        resource->close();
        const auto scroll = resource->scroll.lock();
        try {
            if (scroll && lease->owner->window && as<xui::ScrollView>(scroll).virtual_viewport() == resource->state)
                lease->owner->window->close_virtual_viewport(as<xui::ScrollView>(scroll));
        } catch (...) { erase_virtual_handle(lease); throw; }
        erase_virtual_handle(lease);
    });
}
xui_status XUI_CALL xui_virtual_viewport_get_request(xui_handle handle, xui_virtual_viewport_request* request) noexcept {
    return boundary([&] {
        require(request && request->size == sizeof(*request) && request->version == XUI_VIRTUAL_VIEWPORT_VERSION,
            XUI_VERSION_MISMATCH, "Virtual viewport request version mismatch.");
        auto lease = get(handle, virtual_viewport_kind); virtual_scroll(lease);
        *request = virtual_request(virtual_resource(lease)->state->request());
    });
}
xui_status XUI_CALL xui_control_interaction_get(xui_handle handle, xui_control_interaction* interaction) noexcept {
    return boundary([&] {
        require(interaction && interaction->size == sizeof(*interaction) && interaction->version == XUI_CONTROL_INTERACTION_VERSION,
            XUI_VERSION_MISMATCH, "Control interaction version mismatch.");
        auto node = get(handle);
        const auto value = node->owner->window->interaction(control(node));
        interaction->has_focus = value.has_focus ? 1u : 0u;
        interaction->is_composing = value.is_composing ? 1u : 0u;
    });
}
xui_status XUI_CALL xui_control_interaction_subscribe(xui_handle handle, xui_control_interaction_callback callback, void* context) noexcept {
    return boundary([&] {
        auto node = get(handle); control(node);
        if (callback) editable(node->owner);
        require(node->interaction_generation != UINT64_MAX, XUI_NATIVE_ERROR, "Interaction generation exhausted.");
        const auto generation = node->interaction_generation + 1;
        std::function<void(xui::ControlInteraction)> listener;
        if (callback) listener = [weak = std::weak_ptr<Node>(node), generation](xui::ControlInteraction value) {
            auto node = weak.lock();
            if (!node || !node->owner->window || node->owner->closed || node->interaction_generation != generation ||
                !node->interaction_callback || node->owner->window->state() >= xui::WindowState::closing) return;
            const xui_control_interaction snapshot{sizeof(snapshot), XUI_CONTROL_INTERACTION_VERSION,
                value.has_focus ? 1u : 0u, value.is_composing ? 1u : 0u};
            readonly_notification(node->owner, [&] { return node->interaction_callback(node->interaction_context, &snapshot); });
        };
        node->owner->window->observe_interaction(std::dynamic_pointer_cast<xui::Control>(node->element), std::move(listener));
        node->interaction_generation = generation;
        node->interaction_callback = callback;
        node->interaction_context = callback ? context : nullptr;
    });
}
