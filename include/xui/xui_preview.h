#ifndef XUI_PREVIEW_H
#define XUI_PREVIEW_H
#define XUI_PREVIEW_VERSION 1u
enum { XUI_SHELL_PREVIEW = 80 };
enum { XUI_PREVIEW_IDLE, XUI_PREVIEW_LOADING, XUI_PREVIEW_ACCEPTED,
    XUI_PREVIEW_UNSUPPORTED, XUI_PREVIEW_FAILED, XUI_PREVIEW_RETIRING };
enum { XUI_PREVIEW_NONE, XUI_PREVIEW_NO_HANDLER, XUI_PREVIEW_RESTRICTED,
    XUI_PREVIEW_UNSUPPORTED_PROVIDER, XUI_PREVIEW_MISSING_PROVIDER, XUI_PREVIEW_INITIALIZATION_FAILED,
    XUI_PREVIEW_RENDER_FAILED, XUI_PREVIEW_TIMED_OUT, XUI_PREVIEW_BROKER_FAILED, XUI_PREVIEW_RESOURCE_LIMIT,
    XUI_PREVIEW_CANCELLED, XUI_PREVIEW_HIDDEN, XUI_PREVIEW_UNSUPPORTED_ARCHITECTURE, XUI_PREVIEW_ACTIVATION_FAILED };
enum { XUI_PREVIEW_PHASE_NONE, XUI_PREVIEW_POLICY, XUI_PREVIEW_DISCOVERY, XUI_PREVIEW_ACTIVATION,
    XUI_PREVIEW_INITIALIZE, XUI_PREVIEW_RENDER, XUI_PREVIEW_RESIZE, XUI_PREVIEW_FOCUS, XUI_PREVIEW_UNLOAD };
enum { XUI_PREVIEW_CLEANUP_NONE, XUI_PREVIEW_CLEANUP_PENDING, XUI_PREVIEW_UNLOADED,
    XUI_PREVIEW_BROKER_TERMINATED, XUI_PREVIEW_PROVIDER_UNKNOWN };
typedef struct xui_preview_status {
    uint32_t size, version;
    uint64_t generation;
    uint32_t state, reason, phase;
    int32_t hresult;
    uint32_t cleanup, reserved;
} xui_preview_status;
/* Explicit installed-handler opt-in, not a sandbox. XUI_CHANGE carries the generation.
   Read the typed status inside the callback. No HWND or COM interface crosses this API.
   Unload revokes delivery immediately; shared provider termination is not guaranteed. */
XUI_API xui_status XUI_CALL xui_shell_preview_create(xui_handle window,
    xui_string name, xui_handle* control) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_shell_preview_load_local(xui_handle control,
    xui_string path, uint64_t* generation) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_shell_preview_cancel(xui_handle control, uint64_t generation) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_shell_preview_unload(xui_handle control) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_shell_preview_focus_content(xui_handle control, uint32_t reverse) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_shell_preview_get_status(xui_handle control, xui_preview_status* status) XUI_NOEXCEPT;
#endif
