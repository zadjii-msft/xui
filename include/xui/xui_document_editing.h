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
XUI_API xui_status XUI_CALL xui_syntax_highlighting_available(uint32_t* available) XUI_NOEXCEPT;
/* language is an exact LSH ID; empty disables syntax. A path selects a built-in
   mapping only, without file access. Unknown extensions use plain text. */
XUI_API xui_status XUI_CALL xui_document_syntax_language(xui_handle document, xui_string language) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_document_syntax_path(xui_handle document, xui_string path) XUI_NOEXCEPT;
#endif
