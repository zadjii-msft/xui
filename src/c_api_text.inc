#include "xui/xui_text.h"

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
