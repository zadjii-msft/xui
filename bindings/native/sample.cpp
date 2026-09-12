#include "xui/xui.h"
#include <windows.h>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
xui_string text(const std::string& s) { return {s.data(), static_cast<uint32_t>(s.size()), 0}; }
void check(xui_status status) {
    if (!status) return;
    char message[1024]{}; uint32_t count{}; xui_status code{};
    xui_error_copy(message, sizeof(message), &count, &code);
    throw std::runtime_error(std::string(message, count));
}
struct App {
    xui_handle window{}, root{}, label{}, input{}, button{}, toggle{}, scroll{}, image{}, list{};
    std::string image_path;
    bool callback_fail{};
    ~App() { if (window) xui_window_destroy(window); }
    xui_handle stack() { xui_handle h{}; check(xui_stack_create(window, 1, &h)); return h; }
    xui_handle create(uint32_t kind, const std::string& name, xui_handle content = 0) {
        xui_handle h{}; check(xui_create(window, kind, text(name), content, &h)); return h;
    }
    void property(xui_handle h, uint32_t kind, const std::string& value = {}, float a = 0, float b = 0) {
        xui_property p{sizeof(p), kind, h, text(value), a, b};
        if (kind == XUI_PADDING) p.b = p.c = p.d = a;
        check(xui_update(window, &p, 1));
    }
    void add(xui_handle parent, xui_handle child, float flex = 0) { check(xui_stack_add(parent, child, flex)); }
    static xui_status XUI_CALL callback(void* context, const xui_event* e) noexcept {
        try {
            auto& a = *static_cast<App*>(context);
            if (e->source == a.button) {
                if (a.callback_fail) throw std::runtime_error("GUI callback sentinel");
                a.property(a.label, XUI_TEXT, "Applied");
            }
            if (e->source == a.toggle) a.property(a.label, XUI_TEXT, e->value ? "Enabled" : "Disabled");
            if (e->source == a.input) a.property(a.label, XUI_TEXT, e->kind == XUI_SUBMIT ? "Submitted" : "Edited");
            if (e->kind == XUI_KEY && (e->value & 0xffff) == VK_F6) a.property(a.label, XUI_TEXT, "Keyboard");
            if (e->kind == XUI_KEY && (e->value & 0xffff) == VK_F7) {
                uint32_t state{}; check(xui_image_state(a.image, &state));
                a.property(a.label, XUI_TEXT, state == 2 ? "Image ready" : "Image pending");
            }
            if (e->kind == XUI_KEY && (e->value & 0xffff) == VK_F8 && !a.image_path.empty()) {
                check(xui_image_source(a.image, {}, 192, 144));
                check(xui_image_source(a.image, text(a.image_path), 193, 145));
            }
            if (e->kind == XUI_KEY && (e->value & 0xffff) == VK_F12) check(xui_window_close(a.window));
            return XUI_OK;
        } catch (...) { return XUI_CALLBACK_FAILED; }
    }
};
}
int wmain(int argc, wchar_t** argv) {
    try {
        App a;
        const std::string title = "XUI bindings";
        xui_window_options options{sizeof(options), XUI_ABI_VERSION, text(title), 600, 720};
        check(xui_window_create(&options, &a.window));
        a.root = a.stack(); a.property(a.root, XUI_PADDING, {}, 20); a.property(a.root, XUI_SPACING, {}, 10);
        a.label = a.create(XUI_LABEL, "Ready — 日本語 😀 — a long Unicode label with native retained layout");
        a.property(a.label, XUI_AUTOMATION_ID, "status");
        a.input = a.create(XUI_TEXT_INPUT, "Workspace name"); a.property(a.input, XUI_AUTOMATION_ID, "input");
        a.property(a.input, XUI_TEXT, "Alpha 😀");
        a.button = a.create(XUI_BUTTON, "Apply"); a.property(a.button, XUI_AUTOMATION_ID, "apply");
        a.toggle = a.create(XUI_TOGGLE, "Enable previews"); a.property(a.toggle, XUI_AUTOMATION_ID, "toggle");
        const auto form = a.stack(); a.property(form, XUI_SPACING, {}, 8);
        for (int i = 0; i < 8; ++i) a.add(form, a.create(XUI_LABEL, "Preference " + std::to_string(i) + " — Unicode 日本語"));
        a.scroll = a.create(XUI_SCROLL_VIEW, "Preferences", form); a.property(a.scroll, XUI_AUTOMATION_ID, "scroll");
        a.property(a.scroll, XUI_FIXED_SIZE, {}, 560, 120);
        a.image = a.create(XUI_IMAGE, "Preview"); a.property(a.image, XUI_AUTOMATION_ID, "image");
        a.property(a.image, XUI_FIXED_SIZE, {}, 192, 96);
        a.list = a.create(XUI_FILE_LIST, "Files"); a.property(a.list, XUI_AUTOMATION_ID, "files");
        std::vector<std::string> names; names.reserve(60);
        std::vector<xui_file_item> rows;
        for (int i = 0; i < 60; ++i) {
            char name[32]; std::snprintf(name, sizeof(name), "Entry-%03d.txt", i); names.emplace_back(name);
            rows.push_back({sizeof(xui_file_item), 0, static_cast<uint64_t>(i + 1), text(names.back()), {}});
        }
        check(xui_list_items(a.list, rows.data(), static_cast<uint32_t>(rows.size())));
        for (auto h : {a.label, a.input, a.button, a.toggle, a.scroll, a.image}) a.add(a.root, h);
        a.add(a.root, a.list, 1); check(xui_window_content(a.window, a.root));
        for (auto h : {a.window, a.button, a.toggle, a.input}) check(xui_subscribe(h, &App::callback, &a));
        bool throughput{};
        for (int i = 1; i < argc; ++i) {
            if (std::wstring_view(argv[i]) == L"--throughput") throughput = true;
            else if (std::wstring_view(argv[i]) == L"--callback-fail") a.callback_fail = true;
            else {
                const int units = static_cast<int>(wcslen(argv[i]));
                const int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argv[i], units, nullptr, 0, nullptr, nullptr);
                if (bytes <= 0) throw std::runtime_error("Invalid image path.");
                a.image_path.resize(bytes);
                WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argv[i], units, a.image_path.data(), bytes, nullptr, nullptr);
                check(xui_image_source(a.image, text(a.image_path), 192, 144));
            }
        }
        if (throughput) {
            std::vector<std::string> values; values.reserve(64);
            std::vector<xui_property> properties;
            for (int i = 0; i < 64; ++i) {
                values.push_back("Update " + std::to_string(i));
                properties.push_back({sizeof(xui_property), XUI_TEXT, a.label, text(values.back())});
            }
            auto start = std::chrono::steady_clock::now();
            for (int j = 0; j < 1000; ++j) for (const auto& p : properties) check(xui_update(a.window, &p, 1));
            const auto single = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            start = std::chrono::steady_clock::now();
            for (int j = 0; j < 1000; ++j) check(xui_update(a.window, properties.data(), 64));
            const auto batch = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            std::cout << "{\"mutations\":64000,\"single_ms\":" << single << ",\"batch_ms\":" << batch << "}\n";
            return 0;
        }
        check(xui_window_run(a.window));
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
