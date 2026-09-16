#ifndef XUI_FILE_TRANSFER_H
#define XUI_FILE_TRANSFER_H
/* Included by xui.h. All paths are absolute UTF-8 filesystem paths.
   Limits: 4096 paths, 32767 UTF-16 units/path, 16 MiB aggregate CF_HDROP.
   Window calls require its UI thread. Native errors use the normal error API. */
enum { XUI_FILE_NONE = 0, XUI_FILE_COPY = 1, XUI_FILE_MOVE = 2 };
typedef xui_status (XUI_CALL *xui_file_receiver)(void* context,
    const xui_string* paths, uint32_t count, uint32_t effect);
typedef xui_status (XUI_CALL *xui_file_drop_handler)(void* context,
    uint64_t id, uint64_t version, uint32_t has_key, const xui_string* paths,
    uint32_t count, uint32_t requested, uint32_t perform, uint32_t* effect);
XUI_API xui_status XUI_CALL xui_window_set_file_clipboard(xui_handle window,
    const xui_string* paths, uint32_t count, uint32_t effect) XUI_NOEXCEPT;
/* Receiver runs once, with borrowed spans; zero count means no file clipboard. */
XUI_API xui_status XUI_CALL xui_window_get_file_clipboard(xui_handle window,
    xui_file_receiver receiver, void* context) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_window_set_clipboard_text(xui_handle window, xui_string text) XUI_NOEXCEPT;
/* completed: 1 for complete, 0 for user cancellation/skips. Earlier work is not rolled back. */
XUI_API xui_status XUI_CALL xui_window_transfer_files(xui_handle window,
    const xui_string* paths, uint32_t count, xui_string destination, uint32_t effect, uint32_t* completed) XUI_NOEXCEPT;
/* result: 0 no files, 1 complete, 2 cancelled/skipped. Retains and notifies the original IDataObject. */
XUI_API xui_status XUI_CALL xui_window_paste_files(xui_handle window,
    xui_string destination, uint32_t* result) XUI_NOEXCEPT;
/* Source callback receives XUI_REQUEST at threshold, then XUI_ACTION with completed effect.
   During Request, call xui_grid_file_drag_paths once. Empty paths cancel the drag.
   None means cancelled/rejected; never delete files in the completion callback. */
XUI_API xui_status XUI_CALL xui_grid_file_drag_bind(xui_handle target,
    xui_callback callback, void* context) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_grid_file_drag_paths(xui_handle target,
    const xui_string* paths, uint32_t count) XUI_NOEXCEPT;
/* perform=0: query only, no paths; perform=1: synchronous drop with borrowed paths.
   has_key=0 means empty body, never a header/scrollbar. Return only Copy, Move, or None.
   A drop returns its requested effect only after the entire operation succeeds. */
XUI_API xui_status XUI_CALL xui_grid_file_drop_bind(xui_handle target,
    xui_file_drop_handler callback, void* context) XUI_NOEXCEPT;
#endif
