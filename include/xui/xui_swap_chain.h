#ifndef XUI_SWAP_CHAIN_H
#define XUI_SWAP_CHAIN_H
#include "xui.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xui_swap_chain_metrics {
    uint32_t size, pixel_width, pixel_height;
    float rasterization_scale;
    uint32_t visible;
} xui_swap_chain_metrics;
/* Create with xui_create(XUI_SWAP_CHAIN_PANEL). XUI_VIEW reports changed metrics.
   All calls require the owner UI thread. The callback uses xui_subscribe.
   Graphics pointers and handles are borrowed for the duration of each setter.
   The host retains a COM reference after attachment. Null detaches either path. */
XUI_API xui_status XUI_CALL xui_swap_chain_set(xui_handle panel, void* swap_chain) XUI_NOEXCEPT;
/* This is a DCompositionCreateSurfaceHandle handle, not a shared texture handle.
   The caller owns the handle and can close it after successful attachment. */
XUI_API xui_status XUI_CALL xui_swap_chain_set_surface(xui_handle panel, void* surface) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_swap_chain_get_metrics(xui_handle panel,
    xui_swap_chain_metrics* metrics) XUI_NOEXCEPT;
/* Borrowed HWND, null before attachment and after native teardown. Never destroy it. */
XUI_API xui_status XUI_CALL xui_swap_chain_get_window(xui_handle panel, void** window) XUI_NOEXCEPT;
/* Opts into Tab/Page key delivery to the child HWND and enables its tab stop.
   Window shortcuts and modal routing still run first. No input adapter is installed by XUI. */
XUI_API xui_status XUI_CALL xui_swap_chain_native_input(xui_handle panel, uint32_t enabled) XUI_NOEXCEPT;
#ifdef __cplusplus
}
#endif
#endif
