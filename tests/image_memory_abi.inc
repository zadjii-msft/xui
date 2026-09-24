#include "xui/xui_image_memory.h"
#include "image_memory_fixture.hpp"
namespace image_memory_abi {
struct Fixture {
    xui_handle window{}, outer{}, host{}, scope{}, root{}, image{}, input{};
    HWND frame{}, picture{}, editor{};
    unsigned notifications{};
    bool done{};
    xui::Rect original{};
    xui_image_memory_options options{sizeof(options), XUI_IMAGE_MEMORY_VERSION, 1, 0, 3, 2, 3, 2, 64, 64, 0};
    xui_image_memory_state read() {
        xui_image_memory_state result{sizeof(result), XUI_IMAGE_MEMORY_VERSION};
        ok(xui_image_memory_get_state(image, &result)); return result;
    }
    static xui_status XUI_CALL changed(void* context, const xui_image_memory_state* value, xui_string error) {
        auto& self = *static_cast<Fixture*>(context);
        expect(value && value->size == sizeof(*value) && value->version == XUI_IMAGE_MEMORY_VERSION &&
            !value->reserved && value->generation >= 1 && value->generation <= 3);
        ++self.notifications;
        expect(xui_image_memory_cancel(self.image) == XUI_BUSY);
        if (value->state == 2) expect(error.length == 0 && value->source_width == 3 && value->source_height == 2 &&
            value->pixel_width == (value->generation == 1 ? 3u : 2u) && value->pixel_height == (value->generation == 1 ? 2u : 1u));
        else expect(value->state == 3 && value->generation == 3 && error.data && error.length && error.length < 4096);
        ok(xui_window_post(self.window, advance, context)); return XUI_OK;
    }
    static xui_status XUI_CALL advance(void* context, uint32_t execute) {
        if (execute) static_cast<Fixture*>(context)->step();
        return XUI_OK;
    }
    void set(std::span<const uint8_t> bytes) {
        ok(xui_image_memory_set(image, &options, bytes.data(), static_cast<uint32_t>(bytes.size()), changed, this));
    }
    void step() {
        const auto state = read();
        if (notifications == 1) {
            frame = FindWindowW(L"Xui.Window.1", L"Memory image ABI"); expect(frame != nullptr);
            picture = retained_pages_abi::Fixture::named(frame, L"Owned image alt");
            editor = retained_pages_abi::Fixture::named(frame, L"Retained image neighbor");
            expect(state.state == 2 && !IsWindowVisible(picture));
            auto visible = value(); visible.first = 1; ok(xui_feature_set(image, XUI_F_VISIBLE, &visible));
            SendMessageW(frame, WM_APP + 12, 0, 0);
            original = grid_layout_abi::bounds(image); expect(original.width == 192 && original.height == 144);
            ok(xui_focus(input, 0)); SendMessageW(editor, EM_SETSEL, 1, 4);
            options.generation = 2; options.output_width = 2; options.output_height = 1;
            options.hint_width = options.hint_height = 2;
            set(image_memory_fixture::png);
            const auto bounds = grid_layout_abi::bounds(image);
            expect(bounds.x == original.x && bounds.y == original.y && bounds.width == original.width && bounds.height == original.height);
        } else if (notifications == 2) {
            expect(state.state == 2 && state.pixel_width == 2 && state.pixel_height == 1 && GetFocus() == editor);
            DWORD first{}, last{}; SendMessageW(editor, EM_GETSEL, reinterpret_cast<WPARAM>(&first), reinterpret_cast<LPARAM>(&last));
            expect(first == 1 && last == 4 && IsWindow(picture));
            const auto bounds = grid_layout_abi::bounds(image);
            expect(bounds.width == original.width && bounds.height == original.height);
            options.generation = 3;
            auto malformed = image_memory_fixture::png; malformed[50] ^= 1; set(malformed);
        } else if (notifications == 3) {
            expect(state.state == 3 && !state.pixel_width && !state.pixel_height);
            options.generation = 4; set(image_memory_fixture::png);
            ok(xui_image_memory_cancel(image)); ok(xui_image_memory_cancel(image));
            expect(read().state == 0);
            ok(xui_window_post(window, [](void* context, uint32_t execute) -> xui_status {
                if (!execute) return XUI_OK;
                auto& self = *static_cast<Fixture*>(context);
                expect(self.notifications == 3);
                ok(xui_content_release(self.scope));
                uint32_t count{}; ok(xui_content_handle_count(self.window, &count)); expect(count == 3);
                self.done = true; return xui_window_close(self.window);
            }, this));
        } else expect(false);
    }
    void run() {
        xui_window_options config{sizeof(config), XUI_ABI_VERSION, text("Memory image ABI"), 640, 480};
        ok(xui_window_create(&config, &window)); ok(xui_stack_create(window, 1, &outer));
        ok(xui_content_host_create(window, &host)); ok(xui_stack_add(outer, host, 1)); ok(xui_window_content(window, outer));
        ok(xui_content_begin(host, &scope)); ok(xui_stack_create(window, 1, &root));
        ok(xui_create(window, XUI_IMAGE, text("Owned image alt"), 0, &image));
        ok(xui_create(window, XUI_TEXT_INPUT, text("Image neighbor"), 0, &input));
        xui_property contents{sizeof(contents), XUI_TEXT, input, text("Retained image neighbor")}; ok(xui_update(window, &contents, 1));
        ok(xui_image_memory_cancel(image)); expect(read().state == 0 && read().generation == 0);
        const xui_axis_constraints axes{sizeof(axes), XUI_AXIS_CONSTRAINTS_VERSION,
            {XUI_AXIS_OVERRIDE | XUI_AXIS_MAXIMUM, 0, 0, 192}, {XUI_AXIS_OVERRIDE}};
        ok(xui_element_set_axis_constraints(image, &axes));
        auto hidden = value(); hidden.first = 0; ok(xui_feature_set(image, XUI_F_VISIBLE, &hidden));
        ok(xui_stack_add(root, image, 0)); ok(xui_stack_add(root, input, 0));
        auto copy = image_memory_fixture::png; set(copy); copy.fill(0); expect(notifications == 0);
        auto bad = options; bad.output_width = 2;
        expect(xui_image_memory_set(image, &bad, image_memory_fixture::png.data(), 84, changed, this) == XUI_INVALID_ARGUMENT);
        bad = options; bad.version = 0;
        expect(xui_image_memory_set(image, &bad, image_memory_fixture::png.data(), 84, changed, this) == XUI_VERSION_MISMATCH);
        expect(xui_image_memory_set(input, &options, image_memory_fixture::png.data(), 84, changed, this) == XUI_WRONG_KIND);
        expect(xui_image_memory_set(image, &options, nullptr, 84, changed, this) == XUI_INVALID_ARGUMENT);
        expect(xui_image_memory_set(image, &options, image_memory_fixture::png.data(), 84, nullptr, this) == XUI_INVALID_ARGUMENT);
        std::thread wrong([&] {
            xui_image_memory_state state{sizeof(state), XUI_IMAGE_MEMORY_VERSION};
            expect(xui_image_memory_get_state(image, &state) == XUI_WRONG_THREAD);
        }); wrong.join();
        ok(xui_content_commit(scope, root)); ok(xui_window_run(window)); expect(done);
        expect(xui_image_memory_cancel(image) == XUI_INVALID_HANDLE);
        ok(xui_window_destroy(window));
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        for (;;) {
            xui_image_memory_statistics stats{sizeof(stats), XUI_IMAGE_MEMORY_VERSION};
            ok(xui_image_memory_get_statistics(&stats)); expect(stats.encoded_bytes <= stats.encoded_limit && stats.encoded_peak <= stats.encoded_limit);
            if (!stats.encoded_bytes) break;
            expect(std::chrono::steady_clock::now() < deadline); Sleep(1);
        }
    }
};
void run() {
    static_assert(sizeof(xui_image_memory_options) == 48 && sizeof(xui_image_memory_state) == 40 &&
        sizeof(xui_image_memory_statistics) == 32);
    expect(xui_image_memory_version() == XUI_IMAGE_MEMORY_VERSION);
    Fixture fixture; fixture.run();
}
}
