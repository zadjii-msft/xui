#ifndef XUI_DOCUMENT_EDITING_H
#define XUI_DOCUMENT_EDITING_H
/* Included by xui.h. Plain native document editing. Strings are strict UTF-8.
   Offsets count UTF-16 units. expected_text is the complete document with CR
   paragraphs. Replacement normalizes LF/CRLF. A successful edit emits one
   change event and is one undo action. No-op, stale, unavailable and pending
   text-property targets are errors. Distinct output offsets are written only
   on success. Call on the creating UI thread. */
XUI_API xui_status XUI_CALL xui_document_replace_range(xui_handle document,
    uint64_t start, uint64_t end, xui_string expected_text, xui_string replacement,
    uint64_t* selection_start, uint64_t* selection_end) XUI_NOEXCEPT;
#endif
