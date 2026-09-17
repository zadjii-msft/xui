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
/* Candidate-owned source identities. Keys must be unique and dense, starting at zero.
   Registration borrows the elements. Commit checks membership in the candidate root. */
typedef struct xui_content_inspection_target {
    uint32_t key, reserved;
    xui_handle element;
} xui_content_inspection_target;
XUI_API xui_status XUI_CALL xui_content_inspection_targets(xui_handle scope,
    const xui_content_inspection_target* targets, uint32_t count,
    xui_callback picked, void* context) XUI_NOEXCEPT;
/* Pointer picking consumes primary gestures only. Keyboard and UIA remain native.
   Busy or unsupported surfaces fail explicitly without changing the mode. */
XUI_API xui_status XUI_CALL xui_content_pointer_picking(xui_handle host, uint32_t enabled) XUI_NOEXCEPT;
/* Coordinates are window-client DIPs. A miss succeeds with found=0 and key=0. */
XUI_API xui_status XUI_CALL xui_content_hit_test(xui_handle host, float x, float y,
    uint32_t* key, uint32_t* found) XUI_NOEXCEPT;
#ifdef __cplusplus
}
#endif
#endif
