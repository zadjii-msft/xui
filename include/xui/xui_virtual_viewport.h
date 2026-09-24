#ifndef XUI_VIRTUAL_VIEWPORT_H
#define XUI_VIRTUAL_VIEWPORT_H
#include "xui.h"
#ifdef __cplusplus
extern "C" {
#endif
#define XUI_VIRTUAL_VIEWPORT_VERSION 0x00010000u
typedef struct xui_virtual_viewport_options {
    uint32_t size, version, item_count;
    float row_height;
    uint64_t source_version;
} xui_virtual_viewport_options;
typedef struct xui_virtual_viewport_rect { float offset, width, height, extent; } xui_virtual_viewport_rect;
typedef struct xui_virtual_viewport_request {
    uint32_t size, version;
    uint64_t epoch, committed_source_version, requested_source_version;
    xui_virtual_viewport_rect committed, requested;
    uint32_t blocked, reserved;
} xui_virtual_viewport_request;
enum { XUI_VIRTUAL_READY = 0, XUI_VIRTUAL_COMMITTED = 0, XUI_VIRTUAL_SUPERSEDED = 1, XUI_VIRTUAL_BLOCKED = 2 };
typedef xui_status (XUI_CALL *xui_virtual_viewport_callback)(void*, const xui_virtual_viewport_request*);
#define XUI_VIRTUAL_ITEM_VERSION 0x00010000u
typedef struct xui_virtual_item_info {
    uint32_t size, version, index, count;
    uint64_t source_version;
    const uint16_t* key;
    uint32_t key_length, reserved;
} xui_virtual_item_info;
/* Ready update only, after row insertion. Key is copied as ordinal UTF-16 code
   units, without normalization or truncation (1..4096 units, no NUL).
   Ordinary Stack row roots retain one key identity; index/source may change.
   Native removal/release automatically retires the mounted row registration. */
XUI_API xui_status XUI_CALL xui_virtual_viewport_set_item(xui_handle lease, xui_handle row,
    const xui_virtual_item_info* info) XUI_NOEXCEPT;
XUI_API uint32_t XUI_CALL xui_virtual_viewport_version(void) XUI_NOEXCEPT;
/* Opt-in, fixed-pitch, UI-thread lease owned by the ScrollView's content arena.
   Count <= INT32_MAX; positive source versions and epochs <= INT64_MAX.
   Row pitch must span at least eight float ULPs at the declared extent and fit 0xffffff physical
   pixels in extent, with a native row height in [1,32767] physical pixels.
   Callbacks are posted, coalesced snapshots, never inline from these APIs.
   Initial committed geometry is zero. Declared extent is independent of the
   temporarily staged child tree. All exports must be probed before capability. */
XUI_API xui_status XUI_CALL xui_virtual_viewport_begin(xui_handle scroll,
    const xui_virtual_viewport_options* options, xui_virtual_viewport_callback callback,
    void* context, xui_handle* lease) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_virtual_viewport_set_extent(xui_handle lease,
    uint32_t item_count, uint64_t source_version) XUI_NOEXCEPT;
/* Explicit intent always advances the request epoch, even at the same normalized
   offset. Delivery is posted/coalesced, never inline; Ready queues later intent.
   Ordinary native wheel/ScrollView offset changes remain change-only. */
XUI_API xui_status XUI_CALL xui_virtual_viewport_request_offset(xui_handle lease, float offset) XUI_NOEXCEPT;
/* Ready reserves the exact requested logical geometry/source across a synchronous
   non-yielding staging turn. Later intent queues behind it. Parent shrink clips
   physically, even if the last logical committed rectangle is larger.
   Native observers may see valid intermediate child mutations; this is not
   an atomic OS accessibility snapshot or a rollback of native/model edits. */
XUI_API xui_status XUI_CALL xui_virtual_viewport_try_begin_update(xui_handle lease,
    uint64_t epoch, uint32_t* result) XUI_NOEXCEPT;
/* After Ready, commit publishes the reserved target or reports a terminal native
   failure requiring owner detach. It never reports normal Busy/Stale after edits.
   The caller must have realized the complete target and protected retained pins. */
XUI_API xui_status XUI_CALL xui_virtual_viewport_try_commit(xui_handle lease,
    uint64_t epoch, uint32_t* result) XUI_NOEXCEPT;
/* Optional settled-layout capability, requiring its own export probe.
   After Commit and old-row pruning, flush the exact last committed epoch.
   No Ready update may be active. Current native geometry/retirement must settle;
   future requested intent remains queued and notifications remain posted.
   This is not a vsync, future-animation, or atomic OS accessibility guarantee.
   Failure to settle requires owner detach. Include this work in transaction
   timings rather than stopping at the logical Commit callback. */
XUI_API xui_status XUI_CALL xui_virtual_viewport_flush_committed(xui_handle lease,
    uint64_t expected_epoch) XUI_NOEXCEPT;
/* Cancel is safe only before staging commits model changes. It retains latest
   intent without an immediate retry; next input/focus/IME transition can retry. */
XUI_API xui_status XUI_CALL xui_virtual_viewport_cancel(xui_handle lease, uint64_t epoch) XUI_NOEXCEPT;
/* Terminal: cancels callbacks and hides the sparse viewport, never falls back to
   ordinary scrolling. Valid handles can release while the window is closing. */
XUI_API xui_status XUI_CALL xui_virtual_viewport_release(xui_handle lease) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_virtual_viewport_get_request(xui_handle lease,
    xui_virtual_viewport_request* request) XUI_NOEXCEPT;

#define XUI_CONTROL_INTERACTION_VERSION 0x00010000u
typedef struct xui_control_interaction { uint32_t size, version, has_focus, is_composing; } xui_control_interaction;
typedef xui_status (XUI_CALL *xui_control_interaction_callback)(void*, const xui_control_interaction*);
/* Actual self-focus, not descendant/cached focus. Composition is native EDIT or
   RichEdit state. A separate slot from Changed/Submit. Null unsubscribes.
   Posted snapshots are revoked before node/scope retirement frees callback roots. */
XUI_API xui_status XUI_CALL xui_control_interaction_get(xui_handle control,
    xui_control_interaction* interaction) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_control_interaction_subscribe(xui_handle control,
    xui_control_interaction_callback callback, void* context) XUI_NOEXCEPT;
#ifdef __cplusplus
}
#endif
#endif
