#ifndef XUI_CONTENT_H
#define XUI_CONTENT_H
#include "xui.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Explicit mutable composition. Ordinary tree construction remains before-run only.
   All operations require the creating UI thread. Content scopes are not sandboxes. */
XUI_API xui_status XUI_CALL xui_content_host_create(xui_handle window, xui_handle* host) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_content_begin(xui_handle host, xui_handle* scope) XUI_NOEXCEPT;
/* Commit materializes the root before returning. It retires the previous scope.
   Model errors preserve old content. Native materialization errors can close the window. */
XUI_API xui_status XUI_CALL xui_content_commit(xui_handle scope, xui_handle root) XUI_NOEXCEPT;
/* Abort an uncommitted candidate, or clear this scope if it is still current. */
XUI_API xui_status XUI_CALL xui_content_release(xui_handle scope) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_content_clear(xui_handle host) XUI_NOEXCEPT;
/* Scope context attributes resources created by a scoped callback. It does not permit topology changes. */
XUI_API xui_status XUI_CALL xui_content_context(xui_handle window, xui_handle scope, xui_handle* previous) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_content_owner(xui_handle target, xui_handle* scope) XUI_NOEXCEPT;
/* Diagnostics for ownership tests. Counts include all live ABI handles for this window. */
XUI_API xui_status XUI_CALL xui_content_handle_count(xui_handle window, uint32_t* count) XUI_NOEXCEPT;
#ifdef __cplusplus
}
#endif
#endif
