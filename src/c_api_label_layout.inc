#include "xui/xui_label_layout.h"

uint32_t XUI_CALL xui_label_layout_version() noexcept { return XUI_LABEL_LAYOUT_VERSION; }
xui_status XUI_CALL xui_label_layout_set(xui_handle handle, const xui_label_layout* layout) noexcept {
    return boundary([&] {
        require(layout && layout->size == sizeof(*layout) && layout->version == XUI_LABEL_LAYOUT_VERSION,
            XUI_VERSION_MISMATCH, "Label text layout version mismatch.");
        require(layout->mode <= 2 && !layout->reserved && layout->overflow <= 1 && layout->maximum_lines <= 32 &&
            (layout->mode != 0 || (!layout->overflow && !layout->maximum_lines)) &&
            (layout->mode != 1 || !layout->maximum_lines) && (layout->mode != 2 || !layout->overflow),
            XUI_INVALID_ARGUMENT, "Invalid Label text layout.");
        const auto node = get(handle, XUI_LABEL); editable(node->owner);
        std::optional<xui::LabelTextLayout> value;
        if (layout->mode) value = xui::LabelTextLayout{layout->mode == 2,
            static_cast<xui::LabelOverflow>(layout->overflow), layout->maximum_lines};
        as<xui::Label>(node).set_text_layout(value);
    }, true);
}
xui_status XUI_CALL xui_label_layout_get(xui_handle handle, xui_label_layout* layout) noexcept {
    return boundary([&] {
        require(layout && layout->size == sizeof(*layout) && layout->version == XUI_LABEL_LAYOUT_VERSION,
            XUI_VERSION_MISMATCH, "Label text layout version mismatch.");
        const auto& value = as<xui::Label>(get(handle, XUI_LABEL)).text_layout();
        *layout = {sizeof(*layout), XUI_LABEL_LAYOUT_VERSION, value ? value->wrapping ? 2u : 1u : 0u,
            value ? static_cast<uint32_t>(value->overflow) : 0u, value ? value->maximum_lines : 0u, 0};
    });
}
