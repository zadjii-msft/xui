#ifndef XUI_LABEL_LAYOUT_H
#define XUI_LABEL_LAYOUT_H
#include "xui.h"
#ifdef __cplusplus
extern "C" {
#endif
#define XUI_LABEL_LAYOUT_VERSION 0x00010000u
typedef struct xui_label_layout {
    uint32_t size, version, mode, overflow, maximum_lines, reserved;
} xui_label_layout;
/* Mode 0 inherits unchanged legacy wrapping/style state; other fields must be 0.
   Mode 1 is SingleLine: overflow 0 clips, 1 uses character ellipsis; max lines=0.
   Mode 2 wraps: overflow must be 0 (clip); maximum_lines is 0 (uncapped) or 1..32.
   SingleLine rejects CR/LF/NEL/LS/PS in the current text and subsequent Text/Name
   updates before mutation. No content normalization/truncation; accessibility
   retains the complete text. Label only. Getter captures the requested overlay.
   Optional capability: probe all three exports and version before advertising. */
XUI_API uint32_t XUI_CALL xui_label_layout_version(void) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_label_layout_set(xui_handle label, const xui_label_layout* layout) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_label_layout_get(xui_handle label, xui_label_layout* layout) XUI_NOEXCEPT;
#ifdef __cplusplus
}
#endif
#endif
