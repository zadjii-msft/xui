#ifndef XUI_FILE_DIALOG_H
#define XUI_FILE_DIALOG_H
/* Included by xui.h. Strings are strict UTF-8 and copied before the dialog opens.
   A filter pattern is "*" or "*.*" or semicolon-separated "*.extension" entries.
   The first filter is selected. Default extensions omit the leading dot.
   A suggested name is a leaf filename. Initial directories are absolute paths. */
#define XUI_FILE_DIALOG_VERSION 0x00010000u
typedef struct xui_file_dialog_filter {
    xui_string name, pattern;
} xui_file_dialog_filter;
typedef struct xui_file_dialog_options {
    uint32_t size, version;
    xui_string title;
    const xui_file_dialog_filter* filters;
    uint32_t filter_count, reserved;
    xui_string default_extension, suggested_name, initial_directory;
} xui_file_dialog_options;
/* Called once on success, including cancellation. No callback on native failure.
   accepted is 0 for cancellation (empty path), or 1 for a filesystem path.
   The path is borrowed only for the callback. The callback must not throw.
   Selection does not read or write the selected file. */
typedef xui_status (XUI_CALL *xui_file_dialog_receiver)(void*, uint32_t accepted, xui_string path);
XUI_API xui_status XUI_CALL xui_window_open_file_dialog(xui_handle window,
    const xui_file_dialog_options* options, xui_file_dialog_receiver receiver, void* context) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_window_save_file_dialog(xui_handle window,
    const xui_file_dialog_options* options, xui_file_dialog_receiver receiver, void* context) XUI_NOEXCEPT;
#endif
