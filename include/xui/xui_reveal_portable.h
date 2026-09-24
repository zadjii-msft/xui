#ifndef XUI_REVEAL_PORTABLE_H
#define XUI_REVEAL_PORTABLE_H
#include "xui.h"
#ifdef __cplusplus
extern "C" {
#endif
#define XUI_PORTABLE_REVEAL_VERSION 0x00010000u
typedef struct xui_portable_reveal_state {
    uint32_t size, version, open, duration_ms, direction, initial;
} xui_portable_reveal_state;
typedef struct xui_portable_reveal_presentation {
    uint32_t size, version;
    float progress;
    uint32_t animating;
} xui_portable_reveal_presentation;
/* Opt-in on an existing Reveal handle; legacy Reveal behavior is unchanged.
   Duration=0..400, direction=0 Bottom/1 Right, open/initial=0 or 1.
   First Apply requires initial=1 before native attachment and settles the target.
   Later Apply uses initial=0 and changes Open/Motion atomically. Motion changes
   settle the old logical target before configuring and applying a new target.
   Unchanged motion reverses from current progress; equal state does not restart.
   Actual clip=min(parent allocation, full finite child extent*progress).
   The child stays full-sized on the animation axis. Fixed parent slots may
   reserve unused space; the zero-sized closed element remains a layout participant.
   Outer size constraints and nonzero flex are unsupported.
   Unsupported native accessibility provider families fail explicitly before
   insertion/attachment: RichEdit document_text, FileList, runtime/native plugin
   hosts, and leased virtual viewports. PasswordInput uses EDIT and is supported.
   Closed content is logically input/AX-inert even while exit pixels remain. */
XUI_API uint32_t XUI_CALL xui_portable_reveal_version(void) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_portable_reveal_validate_state(xui_handle reveal,
    const xui_portable_reveal_state* state) XUI_NOEXCEPT;
/* False is only the focused/composing-descendant close veto, never unsupported. */
XUI_API xui_status XUI_CALL xui_portable_reveal_can_set_open(xui_handle reveal,
    uint32_t open, uint32_t* allowed) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_portable_reveal_apply_state(xui_handle reveal,
    const xui_portable_reveal_state* state) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_portable_reveal_get_presentation(xui_handle reveal,
    xui_portable_reveal_presentation* presentation) XUI_NOEXCEPT;
/* UI-thread terminal ownership cancellation before unmount. Stops this motion,
   preserves the requested target, raises no authored event, and forbids reuse.
   Valid while closing/closed; other animations in the same window are unaffected. */
XUI_API xui_status XUI_CALL xui_portable_reveal_cancel(xui_handle reveal) XUI_NOEXCEPT;
#ifdef __cplusplus
}
#endif
#endif
