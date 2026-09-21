#ifndef XUI_SHELL_ACTIONS_H
#define XUI_SHELL_ACTIONS_H
#include "xui.h"
#ifdef __cplusplus
extern "C" {
#endif
/* UI-thread snapshot. COM objects stay on the shared Shell STA. Destroy cancels
   without joining. IDs are valid only in this snapshot; only verb is durable.
   All text pointers passed to receivers are borrowed until the callback returns. */
typedef uint32_t (XUI_CALL *xui_shell_current)(void* context);
typedef xui_status (XUI_CALL *xui_shell_action_receiver)(void* context, uint64_t id, uint64_t version,
    xui_string label, xui_string verb, uint32_t enabled);
XUI_API xui_status XUI_CALL xui_shell_actions_create(xui_handle window, const xui_string* paths, uint32_t count,
    xui_shell_current current, void* context, xui_handle* result) XUI_NOEXCEPT;
/* ready: 0 loading, 1 discovered, 2 action finished. A null receiver polls status only.
   Read reports discovery and invocation errors. */
XUI_API xui_status XUI_CALL xui_shell_actions_read(xui_handle session, xui_shell_action_receiver receiver,
    void* context, uint32_t* ready) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_shell_actions_invoke(xui_handle session, uint64_t id, uint64_t version) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_shell_actions_windows(xui_handle session) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_shell_actions_destroy(xui_handle session) XUI_NOEXCEPT;
#ifdef __cplusplus
}
#endif
#endif
