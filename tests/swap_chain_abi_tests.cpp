#include "xui/xui_swap_chain.h"
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <source_location>

namespace {
static_assert(sizeof(xui_swap_chain_metrics) == 20);
static_assert(sizeof(xui_swap_chain_visible_pixel_bounds) == 20);
void expect(xui_status actual, xui_status expected,
    const std::source_location where = std::source_location::current()) {
    if (actual != expected) throw std::runtime_error("Swap chain ABI status at line " +
        std::to_string(where.line()) + ": " + std::to_string(actual));
}
void require(bool value) { if (!value) throw std::runtime_error("Swap chain ABI assertion failed."); }
struct Context { xui_handle window{}, panel{}; unsigned calls{}; bool fail{}; };
xui_status XUI_CALL changed(void* pointer, const xui_event* event) {
    auto& context = *static_cast<Context*>(pointer);
    if (event->kind != XUI_VIEW) return XUI_INVALID_ARGUMENT;
    xui_swap_chain_metrics metrics{sizeof(metrics)};
    if (xui_swap_chain_get_metrics(context.panel, &metrics)) return XUI_NATIVE_ERROR;
    xui_swap_chain_visible_pixel_bounds bounds{sizeof(bounds)};
    if (xui_swap_chain_get_visible_pixel_bounds(context.panel, &bounds)) return XUI_NATIVE_ERROR;
    if (metrics.visible ? bounds.width <= 0 || bounds.height <= 0 : bounds.width != 0 || bounds.height != 0)
        return XUI_NATIVE_ERROR;
    void* window{};
    if (xui_swap_chain_get_window(context.panel, &window) || !window || !metrics.pixel_width ||
        !metrics.pixel_height || metrics.rasterization_scale <= 0) return XUI_NATIVE_ERROR;
    ++context.calls;
    if (context.fail) return XUI_CALLBACK_FAILED;
    return xui_window_close(context.window);
}
void run(bool fail) {
    Context context{};
    context.fail = fail;
    xui_window_options options{sizeof(options), XUI_ABI_VERSION, {"Swap chain ABI", 14, 0}, 400, 300};
    expect(xui_window_create(&options, &context.window), XUI_OK);
    expect(xui_create(context.window, XUI_SWAP_CHAIN_PANEL, {"Terminal", 8, 0}, 0, &context.panel), XUI_OK);
    xui_swap_chain_metrics metrics{sizeof(metrics)};
    expect(xui_swap_chain_get_metrics(context.panel, &metrics), XUI_OK);
    require(!metrics.visible && !metrics.pixel_width && metrics.rasterization_scale == 1);
    struct LegacyBuffer { xui_swap_chain_metrics metrics{sizeof(xui_swap_chain_metrics)}; uint32_t guard{0x51A7BEEF}; } legacy;
    expect(xui_swap_chain_get_metrics(context.panel, &legacy.metrics), XUI_OK);
    require(legacy.guard == 0x51A7BEEF);
    xui_swap_chain_visible_pixel_bounds bounds{sizeof(bounds)};
    expect(xui_swap_chain_get_visible_pixel_bounds(context.panel, &bounds), XUI_OK);
    require(bounds.x == 0 && bounds.y == 0 && bounds.width == 0 && bounds.height == 0);
    expect(xui_swap_chain_get_visible_pixel_bounds(context.panel, nullptr), XUI_INVALID_ARGUMENT);
    bounds.size = 0;
    expect(xui_swap_chain_get_visible_pixel_bounds(context.panel, &bounds), XUI_VERSION_MISMATCH);
    bounds.size = sizeof(bounds);
    expect(xui_swap_chain_get_visible_pixel_bounds(context.window, &bounds), XUI_WRONG_KIND);
    expect(xui_swap_chain_get_visible_pixel_bounds(0, &bounds), XUI_INVALID_HANDLE);
    expect(xui_swap_chain_get_metrics(context.panel, nullptr), XUI_INVALID_ARGUMENT);
    metrics.size = 0;
    expect(xui_swap_chain_get_metrics(context.panel, &metrics), XUI_VERSION_MISMATCH);
    metrics.size = sizeof(metrics);
    expect(xui_swap_chain_get_metrics(context.window, &metrics), XUI_WRONG_KIND);
    expect(xui_swap_chain_get_metrics(0, &metrics), XUI_INVALID_HANDLE);
    expect(xui_swap_chain_set(context.panel, nullptr), XUI_OK);
    expect(xui_swap_chain_set_surface(context.panel, nullptr), XUI_OK);
    expect(xui_swap_chain_native_input(context.panel, 2), XUI_INVALID_ARGUMENT);
    expect(xui_swap_chain_native_input(context.window, 1), XUI_WRONG_KIND);
    expect(xui_swap_chain_native_input(context.panel, 1), XUI_OK);
    void* hwnd = reinterpret_cast<void*>(1);
    expect(xui_swap_chain_get_window(context.panel, &hwnd), XUI_OK);
    require(!hwnd);
    xui_status worker_status{};
    std::thread worker([&] { worker_status = xui_swap_chain_get_metrics(context.panel, &metrics); });
    worker.join();
    expect(worker_status, XUI_WRONG_THREAD);
    std::thread bounds_worker([&] { worker_status = xui_swap_chain_get_visible_pixel_bounds(context.panel, &bounds); });
    bounds_worker.join();
    expect(worker_status, XUI_WRONG_THREAD);
    xui_handle root{};
    expect(xui_stack_create(context.window, 1, &root), XUI_OK);
    expect(xui_stack_add(root, context.panel, 1), XUI_OK);
    expect(xui_window_content(context.window, root), XUI_OK);
    expect(xui_subscribe(context.panel, changed, &context), XUI_OK);
    expect(xui_window_run(context.window), fail ? XUI_CALLBACK_FAILED : XUI_OK);
    require(context.calls == 1);
    expect(xui_swap_chain_get_window(context.panel, &hwnd), XUI_OK);
    require(!hwnd);
    expect(xui_swap_chain_get_visible_pixel_bounds(context.panel, &bounds), XUI_OK);
    require(bounds.x == 0 && bounds.y == 0 && bounds.width == 0 && bounds.height == 0);
    expect(xui_swap_chain_set_surface(context.panel, nullptr), XUI_CLOSED);
    expect(xui_swap_chain_native_input(context.panel, 0), XUI_CLOSED);
    expect(xui_window_destroy(context.window), XUI_OK);
    expect(xui_swap_chain_get_metrics(context.panel, &metrics), XUI_INVALID_HANDLE);
    expect(xui_swap_chain_get_visible_pixel_bounds(context.panel, &bounds), XUI_INVALID_HANDLE);
}
}
int main() {
    try { run(false); run(true); std::cout << "Swap chain ABI lifetime and errors passed\n"; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
