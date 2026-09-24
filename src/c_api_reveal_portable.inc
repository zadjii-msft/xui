namespace {
struct PortableRevealState { bool open; unsigned duration; xui::RevealDirection direction; bool initial; };
PortableRevealState portable_reveal_state(const xui_portable_reveal_state* state) {
    require(state && state->size == sizeof(*state) && state->version == XUI_PORTABLE_REVEAL_VERSION,
        XUI_VERSION_MISMATCH, "Portable Reveal state version mismatch.");
    require(state->open <= 1 && state->initial <= 1 && state->duration_ms <= 400 && state->direction <= 1,
        XUI_INVALID_ARGUMENT, "Invalid portable Reveal state.");
    return {state->open != 0, state->duration_ms,
        state->direction ? xui::RevealDirection::right : xui::RevealDirection::bottom, state->initial != 0};
}
void validate_portable_reveal_insertion(const std::shared_ptr<Node>& parent,
    const std::shared_ptr<xui::Element>& child, float flex) {
    require(parent->element && child, XUI_WRONG_KIND, "Expected an element insertion parent and child.");
    if (const auto reveal = std::dynamic_pointer_cast<xui::Reveal>(child); reveal && reveal->portable())
        require(flex == 0, XUI_INVALID_ARGUMENT, "Portable Reveal does not support nonzero outer flex.");
    for (auto handle : parent->owner->handles) {
        const auto candidate = get(handle);
        const auto reveal = std::dynamic_pointer_cast<xui::Reveal>(candidate->element);
        if (reveal && reveal->portable() && xui::Window::subtree_contains(reveal->content(), *parent->element))
            inspect_content([&] { xui::Reveal::validate_portable_content(child); });
    }
}
void validate_portable_reveal_virtualization(const std::shared_ptr<Node>& scroll) {
    for (auto handle : scroll->owner->handles) {
        const auto candidate = get(handle);
        const auto reveal = std::dynamic_pointer_cast<xui::Reveal>(candidate->element);
        require(!reveal || !reveal->portable() || !xui::Window::subtree_contains(reveal->content(), *scroll->element),
            XUI_INVALID_ARGUMENT, "Leased virtual viewports are unsupported inside portable Reveal.");
    }
}
void validate_portable_reveal(const std::shared_ptr<Node>& node, PortableRevealState value) {
    editable(node->owner);
    require(!as<xui::Reveal>(node).portable_cancelled(), XUI_CLOSED, "The portable Reveal is retired.");
    require(node->authored_flex == 0, XUI_INVALID_ARGUMENT, "Portable Reveal does not support nonzero outer flex.");
    inspect_content([&] { node->owner->window->validate_portable_reveal(as<xui::Reveal>(node),
        value.open, value.duration, value.direction, value.initial); });
}
}
uint32_t XUI_CALL xui_portable_reveal_version() noexcept { return XUI_PORTABLE_REVEAL_VERSION; }
xui_status XUI_CALL xui_portable_reveal_validate_state(xui_handle handle, const xui_portable_reveal_state* state) noexcept {
    return boundary([&] { validate_portable_reveal(get(handle, reveal_kind), portable_reveal_state(state)); });
}
xui_status XUI_CALL xui_portable_reveal_can_set_open(xui_handle handle, uint32_t open, uint32_t* allowed) noexcept {
    return boundary([&] {
        require(open <= 1 && allowed, XUI_INVALID_ARGUMENT, "Invalid Reveal preflight arguments.");
        const auto node = get(handle, reveal_kind); editable(node->owner);
        require(!as<xui::Reveal>(node).portable_cancelled(), XUI_CLOSED, "The portable Reveal is retired.");
        *allowed = inspect_content([&] { return node->owner->window->can_set_portable_reveal_open(as<xui::Reveal>(node), open != 0); }) ? 1u : 0u;
    });
}
xui_status XUI_CALL xui_portable_reveal_apply_state(xui_handle handle, const xui_portable_reveal_state* state) noexcept {
    return boundary([&] {
        const auto node = get(handle, reveal_kind); const auto value = portable_reveal_state(state);
        validate_portable_reveal(node, value);
        require(!node->owner->source_callbacks && !node->owner->secret_callbacks &&
            node->owner->callbacks == node->owner->posted_callbacks &&
            (!node->owner->content_context || node->owner->building_content == node->content_scope),
            XUI_BUSY, "Apply portable Reveal state outside native/scoped callbacks.");
        if (node->owner->append_first)
            require(node->handle >= node->owner->append_first, XUI_BUSY, "Append only configures fresh Reveals.");
        inspect_content([&] { node->owner->window->apply_portable_reveal(as<xui::Reveal>(node),
            value.open, value.duration, value.direction, value.initial); });
    });
}
xui_status XUI_CALL xui_portable_reveal_get_presentation(xui_handle handle, xui_portable_reveal_presentation* value) noexcept {
    return boundary([&] {
        require(value && value->size == sizeof(*value) && value->version == XUI_PORTABLE_REVEAL_VERSION,
            XUI_VERSION_MISMATCH, "Portable Reveal presentation version mismatch.");
        const auto& reveal = as<xui::Reveal>(get(handle, reveal_kind));
        require(reveal.portable(), XUI_INVALID_ARGUMENT, "Reveal has not enabled portable presentation.");
        *value = {sizeof(*value), XUI_PORTABLE_REVEAL_VERSION, reveal.progress(), reveal.animating() ? 1u : 0u};
    });
}
xui_status XUI_CALL xui_portable_reveal_cancel(xui_handle handle) noexcept {
    return boundary([&] {
        const auto node = get(handle, reveal_kind);
        require(!node->owner->source_callbacks && !node->owner->secret_callbacks, XUI_BUSY,
            "Cancel Reveal outside read-only callbacks.");
        if (node->owner->window->state() < xui::WindowState::closing)
            require(node->owner->callbacks == node->owner->posted_callbacks, XUI_BUSY, "Post Reveal disposal outside native callbacks.");
        node->owner->window->cancel_portable_reveal(as<xui::Reveal>(node));
    });
}
