#include "xui/xui.h"
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <source_location>
#include <thread>
namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
xui_string text(const char* value) { return {value, static_cast<uint32_t>(std::strlen(value)), 0}; }
void status(xui_status actual, xui_status expected, const std::source_location location = std::source_location::current()) {
    if (actual != expected) throw std::runtime_error("Unexpected document ABI status " + std::to_string(actual) +
        " at line " + std::to_string(location.line()));
}
}
int main() {
    try {
        xui_handle window{}, document{}, rich{};
        xui_window_options options{sizeof(options), XUI_ABI_VERSION, text("Document ABI"), 500, 300};
        status(xui_window_create(&options, &window), XUI_OK);
        xui_feature_options feature{sizeof(feature), XUI_FEATURE_VERSION, text("Document")};
        status(xui_feature_create(window, XUI_MULTILINE_TEXT, &feature, &document), XUI_OK);
        status(xui_feature_create(window, XUI_RICH_TEXT, &feature, &rich), XUI_OK);
        uint64_t start = 77, end = 88;
        const auto replace = [&](xui_handle target, xui_string value) {
            return xui_document_replace_range(target, 0, 0, text(""), value, &start, &end);
        };
        status(replace(document, text("X")), XUI_BUSY);
        char message[1024]{}; uint32_t required{}; xui_status code{};
        status(xui_error_copy(message, sizeof(message), &required, &code), XUI_OK);
        require(code == XUI_BUSY && required != 0, "Detached failure has explicit last error");
        status(replace(rich, text("X")), XUI_WRONG_KIND);
        status(replace(0, text("X")), XUI_INVALID_HANDLE);
        status(replace(document, {"\xc0\x80", 2, 0}), XUI_INVALID_ARGUMENT);
        status(replace(document, {"\xed\xa0\x80", 3, 0}), XUI_INVALID_ARGUMENT);
        status(replace(document, {"a\0b", 3, 0}), XUI_INVALID_ARGUMENT);
        status(replace(document, {nullptr, 1, 0}), XUI_INVALID_ARGUMENT);
        status(replace(document, {"X", XUI_MAX_STRING_BYTES + 1, 0}), XUI_INVALID_ARGUMENT);
        status(xui_document_replace_range(document, 0, 0, text(""), text("X"), nullptr, &end), XUI_INVALID_ARGUMENT);
        status(xui_document_replace_range(document, 0, 0, text(""), text("X"), &start, &start), XUI_INVALID_ARGUMENT);
        status(xui_document_replace_range(document, 0, UINT64_MAX, text(""), text("X"), &start, &end), XUI_INVALID_ARGUMENT);
        xui_status threaded{};
        std::thread worker([&] { threaded = replace(document, text("X")); }); worker.join();
        status(threaded, XUI_WRONG_THREAD);
        require(start == 77 && end == 88, "Rejected ABI edit leaves output offsets untouched");
        status(xui_window_close(window), XUI_OK);
        status(replace(document, text("X")), XUI_BUSY); // Before run, no native window exists to close.
        status(xui_window_destroy(window), XUI_OK);
        status(replace(document, text("X")), XUI_INVALID_HANDLE);
        std::cout << "Document ABI errors, UTF-8 spans, thread affinity and outputs passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
