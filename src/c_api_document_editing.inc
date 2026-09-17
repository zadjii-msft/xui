xui_status XUI_CALL xui_document_replace_range(xui_handle handle, uint64_t start, uint64_t end,
    xui_string expected_text, xui_string replacement, uint64_t* selection_start, uint64_t* selection_end) noexcept {
    return boundary([&] {
        require(selection_start && selection_end && selection_start != selection_end,
            XUI_INVALID_ARGUMENT, "Distinct document selection outputs are required.");
        auto node = get(handle, XUI_MULTILINE_TEXT);
        editable(node->owner);
        require(start <= end && end <= xui::DocumentText::document_limit,
            XUI_INVALID_ARGUMENT, "Invalid document replacement range.");
        auto expected = decode(expected_text);
        auto value = decode(replacement);
        xui::TextSelection selection;
        try {
            selection = as<xui::MultilineText>(node).replace_range(
                {static_cast<std::size_t>(start), static_cast<std::size_t>(end)}, expected, std::move(value));
        } catch (const std::length_error& e) {
            error(XUI_INVALID_ARGUMENT, e.what());
            throw Failure{XUI_INVALID_ARGUMENT, last_message.data()};
        } catch (const std::invalid_argument& e) {
            error(XUI_INVALID_ARGUMENT, e.what());
            throw Failure{XUI_INVALID_ARGUMENT, last_message.data()};
        } catch (const std::logic_error& e) {
            error(XUI_BUSY, e.what());
            throw Failure{XUI_BUSY, last_message.data()};
        }
        require(!node->owner->callback_failure, XUI_CALLBACK_FAILED, "Document change callback failed.");
        *selection_start = selection.start;
        *selection_end = selection.end;
    });
}
xui_status XUI_CALL xui_syntax_highlighting_available(uint32_t* available) noexcept {
    return boundary([&] {
        require(available, XUI_INVALID_ARGUMENT, "A syntax availability output is required.");
        *available = xui::syntax_highlighting_available() ? 1u : 0u;
    });
}
xui_status XUI_CALL xui_document_syntax_language(xui_handle handle, xui_string language) noexcept {
    return boundary([&] {
        auto node = get(handle, XUI_MULTILINE_TEXT);
        editable(node->owner);
        const auto value = encode(decode(language));
        try {
            xui::set_syntax_language(as<xui::MultilineText>(node), value);
        } catch (const std::invalid_argument& e) {
            throw InspectionFailure{XUI_INVALID_ARGUMENT, e.what()};
        }
    });
}
xui_status XUI_CALL xui_document_syntax_path(xui_handle handle, xui_string path) noexcept {
    return boundary([&] {
        auto node = get(handle, XUI_MULTILINE_TEXT);
        editable(node->owner);
        const auto value = decode(path);
        xui::set_syntax_language(as<xui::MultilineText>(node), xui::syntax_language_for_path(value));
    });
}
