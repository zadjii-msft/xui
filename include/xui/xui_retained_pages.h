#ifndef XUI_RETAINED_PAGES_H
#define XUI_RETAINED_PAGES_H
#include "xui.h"
#ifdef __cplusplus
extern "C" {
#endif
#define XUI_RETAINED_PAGES_VERSION 0x00010000u
typedef struct xui_page_entry {
    uint32_t size, enabled;
    uint64_t id;
    xui_string title;
} xui_page_entry;
/* Opt-in native retained pages, not legacy PageView or an ordinary Stack.
   Each page owns an internal ContentView; the supplied child handle remains the
   authored root. Inactive editor HWNDs/undo survive but input and AX are hidden.
   IDs are nonzero, unique, and <= INT64_MAX-100; titles are 1..1024 UTF-16 units.
   Up to 4096 entries; enabled/visible are 0 or 1. Selected=0 means no page.
   A selected ID must exist and be enabled. Axis constraints apply to this host.
   Before live changes, Validate rejects active composition or hiding a focused
   descendant. No automatic focus transfer occurs. Set repeats validation.
   Validate allows future entries/counts before planned structural edits.
   During fresh construction Set can precede Insert; attachment requires a
   complete matching entry/child ID order. Live Set requires that exact order. */
XUI_API uint32_t XUI_CALL xui_retained_pages_version(void) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_retained_pages_create(xui_handle window, xui_string name,
    xui_handle* pages) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_retained_pages_validate(xui_handle pages,
    const xui_page_entry* entries, uint32_t count, uint64_t selected) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_retained_pages_set(xui_handle pages,
    const xui_page_entry* entries, uint32_t count, uint64_t selected) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_retained_pages_validate_visible(xui_handle pages, uint32_t visible) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_retained_pages_set_visible(xui_handle pages, uint32_t visible) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_retained_pages_get_state(xui_handle pages,
    uint64_t* selected, uint32_t* visible, uint32_t* count) XUI_NOEXCEPT;
/* Reads the actual constructed root order, not pending metadata order. */
XUI_API xui_status XUI_CALL xui_retained_pages_get_page_id(xui_handle pages, uint32_t index, uint64_t* id) XUI_NOEXCEPT;
/* Same-window/arena only. Fresh construction or unscoped committed mutations;
   append may build fresh pages only. EndAppend precedes live insertion.
   Remove prunes native peers/callbacks; authored handles remain scope-owned for
   reverse ReleaseElement. ValidateMove accepts future nonnegative indices;
   Move requires a final index less than the current child count. */
XUI_API xui_status XUI_CALL xui_retained_pages_insert(xui_handle pages, uint32_t index,
    uint64_t id, xui_handle child) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_retained_pages_remove(xui_handle pages, xui_handle child) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_retained_pages_validate_move(xui_handle pages,
    xui_handle child, uint32_t future_index) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_retained_pages_move(xui_handle pages,
    xui_handle child, uint32_t index) XUI_NOEXCEPT;
/* Existing TabStrip or NavigationView handles, linked in the same arena/window.
   Validate can precede Connect. Set silently projects authoritative page
   metadata; once linked it must match the page host's current metadata.
   Native user events retain existing Selection/Click/Cancel IDs.
   This bounded version explicitly rejects disabled TabStrip entries with
   INVALID_ARGUMENT; NavigationView supports disabled entries. Legacy
   selectors and PageView retain their existing behavior when not linked. */
XUI_API xui_status XUI_CALL xui_page_selector_connect(xui_handle selector, xui_handle pages) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_page_selector_validate(xui_handle selector,
    const xui_page_entry* entries, uint32_t count, uint64_t selected) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_page_selector_set(xui_handle selector,
    const xui_page_entry* entries, uint32_t count, uint64_t selected) XUI_NOEXCEPT;
/* TabStrip only. Controls the real native close affordance and CloseRequested
   event availability independently of whether an event sink is subscribed.
   Existing unconfigured TabStrips retain legacy callback-driven closability. */
XUI_API xui_status XUI_CALL xui_page_selector_set_closable(xui_handle selector, uint32_t closable) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_page_selector_get_closable(xui_handle selector, uint32_t* closable) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_page_selector_get_state(xui_handle selector, uint64_t* selected, uint32_t* count) XUI_NOEXCEPT;
#ifdef __cplusplus
}
#endif
#endif
