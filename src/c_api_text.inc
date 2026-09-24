#include "xui/xui_text.h"

uint32_t XUI_CALL xui_forms_version() noexcept { return XUI_FORMS_VERSION; }
xui_status XUI_CALL xui_text_input_create_with_purpose(xui_handle window,
    xui_string name, uint32_t purpose, xui_handle* input) noexcept {
    return boundary([&] {
        require(input, XUI_INVALID_ARGUMENT, "Missing input output."); *input = 0;
        auto node = get(window, XUI_WINDOW); topology(node->owner);
        require(purpose <= XUI_INPUT_NUMBER, XUI_INVALID_ARGUMENT, "Invalid text input purpose.");
        *input = insert(node->owner, XUI_TEXT_INPUT,
            std::make_shared<xui::TextInput>(decode(name), static_cast<xui::TextInputPurpose>(purpose)));
    });
}
xui_status XUI_CALL xui_text_input_get_purpose(xui_handle input, uint32_t* purpose) noexcept {
    return boundary([&] {
        require(purpose, XUI_INVALID_ARGUMENT, "Missing input purpose output.");
        *purpose = static_cast<uint32_t>(as<xui::TextInput>(get(input, XUI_TEXT_INPUT)).purpose());
    });
}

xui_status XUI_CALL xui_text_input_selection_get(xui_handle input, uint64_t* start, uint64_t* end) noexcept {
    return boundary([&] {
        require(start && end, XUI_INVALID_ARGUMENT, "Missing text selection outputs.");
        auto n = get(input, XUI_TEXT_INPUT); editable(n->owner);
        const auto value = as<xui::TextInput>(n).selection();
        *start = value.start; *end = value.end;
    });
}
xui_status XUI_CALL xui_text_input_selection_set(xui_handle input, uint64_t start, uint64_t end) noexcept {
    return boundary([&] {
        auto n = get(input, XUI_TEXT_INPUT); editable(n->owner);
        auto& control = as<xui::TextInput>(n);
        const auto length = static_cast<uint64_t>(control.text().size());
        control.set_selection({static_cast<std::size_t>(std::min(start, length)),
            static_cast<std::size_t>(std::min(end, length))});
    });
}
