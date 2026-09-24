#ifndef XUI_TEXT_H
#define XUI_TEXT_H
#include "xui.h"
#ifdef __cplusplus
extern "C" {
#endif
enum { XUI_INPUT_NORMAL = 0, XUI_INPUT_EMAIL = 1, XUI_INPUT_URL = 2, XUI_INPUT_TELEPHONE = 3, XUI_INPUT_NUMBER = 4 };
#define XUI_FORMS_VERSION 0x00010000u
/* Qualifies immutable advisory purposes, MultilineText/PasswordInput axis
   measurement, and explicit terminal password cleanup. Probe required exports. */
XUI_API uint32_t XUI_CALL xui_forms_version(void) XUI_NOEXCEPT;
/* Immutable advisory keyboard/input hint. Does not filter, parse, normalize or
   transform content. Existing xui_create(XUI_TEXT_INPUT) defaults to Normal. */
XUI_API xui_status XUI_CALL xui_text_input_create_with_purpose(xui_handle window,
    xui_string name, uint32_t purpose, xui_handle* input) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_text_input_get_purpose(xui_handle input, uint32_t* purpose) XUI_NOEXCEPT;
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
