#ifndef XUI_TEXT_H
#define XUI_TEXT_H
#include "xui.h"
#ifdef __cplusplus
extern "C" {
#endif
/* UI thread only. UTF-16 offsets clamp to the text length and are ordered.
   A caret inside a surrogate pair moves left; a range expands to whole pairs.
   The live EDIT selection is read/written synchronously. Before attachment,
   the selection is retained until the native input is created. */
XUI_API xui_status XUI_CALL xui_text_input_selection_get(xui_handle input,
    uint64_t* start, uint64_t* end) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_text_input_selection_set(xui_handle input,
    uint64_t start, uint64_t end) XUI_NOEXCEPT;
#ifdef __cplusplus
}
#endif
#endif
