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
#define XUI_CONTENT_VIEWPORT_VERSION 0x00010000u
typedef struct xui_content_viewport {
    uint32_t size, version;
    float width, height;
} xui_content_viewport;
typedef xui_status (XUI_CALL *xui_content_viewport_callback)(void* context, const xui_content_viewport* viewport);
/* Actual inner space allocated to this host's root, in DIPs after host insets.
   Subscribe atomically returns the initial snapshot (zero before layout); it does
   not invoke the callback inline. Later size changes are posted/coalesced on the
   UI thread, including internal parent/inset changes without a window resize.
   Callbacks are read-only metadata. Each subscription is independent and owned
   by the host's window; release it before unmounting. Release remains valid while
   closing/closed and suppresses queued callbacks. Older DLLs need export probes. */
XUI_API uint32_t XUI_CALL xui_content_viewport_version(void) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_content_viewport_subscribe(xui_handle host,
    xui_content_viewport_callback callback, void* context, xui_content_viewport* initial,
    xui_handle* subscription) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_content_viewport_release(xui_handle subscription) XUI_NOEXCEPT;
/* Optional additive exports: bindings must probe them before advertising mutable
   content support in an older DLL with the same ABI version.
   Only current committed arenas, outside native/scoped callbacks, may append.
   During append, ordinary constructors/Add may operate on fresh nodes only.
   End always clears construction context; keep=0 retires every new append handle.
   keep=1 retains them for insertion. Native failures may require releasing the arena. */
XUI_API xui_status XUI_CALL xui_content_begin_append(xui_handle scope) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_content_end_append(xui_handle scope, uint32_t keep) XUI_NOEXCEPT;
/* Read-only whole-edit preflight, before committing application model changes.
   Active EDIT/RichEdit composition anywhere in the window returns XUI_BUSY.
   Insert/remove/move repeat this guard. No surviving element/HWND is recreated. */
XUI_API xui_status XUI_CALL xui_content_validate_mutation(xui_handle scope) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_content_stack_insert(xui_handle stack, uint32_t index,
    xui_handle child, float flex) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_content_stack_remove(xui_handle stack, xui_handle child) XUI_NOEXCEPT;
/* Validation accepts future indices; execution requires index < current count. */
XUI_API xui_status XUI_CALL xui_content_stack_validate_move(xui_handle stack,
    xui_handle child, uint32_t future_index) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_content_stack_move(xui_handle stack,
    xui_handle child, uint32_t index) XUI_NOEXCEPT;
/* Remove prunes peers and callbacks, but retains model handles. Release detached
   elements in reverse construction order, then their parents. Reachable elements
   in any live window/popup/control subtree cannot be released individually. */
XUI_API xui_status XUI_CALL xui_content_release_element(xui_handle scope, xui_handle element) XUI_NOEXCEPT;
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
typedef enum xui_content_highlight_result {
    XUI_HIGHLIGHT_APPLIED = 0, XUI_HIGHLIGHT_CLEARED = 1, XUI_HIGHLIGHT_NOT_VISIBLE = 2,
    XUI_HIGHLIGHT_OCCLUDED_NATIVE = 3, XUI_HIGHLIGHT_UNSUPPORTED_SURFACE = 4
} xui_content_highlight_result;
/* Current-layout result only. Later unsafe layout hides the original-perimeter outline.
   No native window regions, styles, or input behavior change. clear must be 0 or 1. */
XUI_API xui_status XUI_CALL xui_content_highlight(xui_handle scope, uint32_t key,
    uint32_t clear, uint32_t* result) XUI_NOEXCEPT;
#ifdef __cplusplus
}
#endif
#endif
