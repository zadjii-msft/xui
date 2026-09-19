#ifndef XUI_C_API_H
#define XUI_C_API_H
#include <stdint.h>

#if defined(_WIN32)
#if defined(XUI_BUILD_DLL)
#define XUI_API __declspec(dllexport)
#else
#define XUI_API __declspec(dllimport)
#endif
#define XUI_CALL __cdecl
#else
#define XUI_API
#define XUI_CALL
#endif
#ifdef __cplusplus
#define XUI_NOEXCEPT noexcept
extern "C" {
#else
#define XUI_NOEXCEPT
#endif

#define XUI_ABI_VERSION 0x00010000u
#define XUI_MAX_STRING_BYTES 1048576u
#define XUI_MAX_BATCH 4096u
typedef uint64_t xui_handle;
typedef int32_t xui_status;
enum {
    XUI_OK = 0, XUI_INVALID_ARGUMENT = 1, XUI_INVALID_HANDLE = 2,
    XUI_WRONG_KIND = 3, XUI_WRONG_THREAD = 4, XUI_VERSION_MISMATCH = 5,
    XUI_BUFFER_TOO_SMALL = 6, XUI_BUSY = 7, XUI_CALLBACK_FAILED = 8,
    XUI_NATIVE_ERROR = 9, XUI_OUT_OF_MEMORY = 10, XUI_CLOSED = 11
};
enum {
    XUI_WINDOW = 1, XUI_STACK = 2, XUI_LABEL = 3, XUI_BUTTON = 4,
    XUI_TOGGLE = 5, XUI_TEXT_INPUT = 6, XUI_SCROLL_VIEW = 7,
    XUI_IMAGE = 8, XUI_FILE_LIST = 9
};
enum {
    XUI_TEXT = 1, XUI_NAME = 2, XUI_ENABLED = 3, XUI_CHECKED = 4,
    XUI_FIXED_SIZE = 5, XUI_MIN_SIZE = 6, XUI_MAX_SIZE = 7,
    XUI_AUTO_SIZE = 8, XUI_SPACING = 9, XUI_PADDING = 10,
    XUI_SCROLL_OFFSET = 11, XUI_AUTOMATION_ID = 12, XUI_THEME = 13,
    XUI_PREFERRED_SIZE = 14
};
enum {
    XUI_CLICK = 1, XUI_CHANGE = 2, XUI_SUBMIT = 3, XUI_KEY = 4,
    XUI_SELECTION = 5, XUI_VIEW = 6
};
typedef struct xui_string {
    const char* data;
    uint32_t length;
    uint32_t reserved;
} xui_string;
/* Applies UI typography to current and future controls. Font size: [8,32] DIPs.
   Motion preferences cannot override Windows reduced motion or high contrast. */
XUI_API xui_status XUI_CALL xui_window_set_presentation(xui_handle window, xui_string font_family,
    float font_size, uint32_t smooth_scrolling, uint32_t animations) XUI_NOEXCEPT;
/* Pointer-only activation; modifier selection and keyboard behavior are unchanged.
   Thumbnail fill crops images without changing their aspect ratio. */
XUI_API xui_status XUI_CALL xui_control_set_presentation(xui_handle control,
    uint32_t single_click, uint32_t thumbnail_fill) XUI_NOEXCEPT;
/* Overrides window presentation typography for this control. Zero inherits the window;
   otherwise font_size must be a finite value in [8,32] DIPs. */
XUI_API xui_status XUI_CALL xui_control_presentation_font_size(xui_handle control, float font_size) XUI_NOEXCEPT;
typedef struct xui_window_options {
    uint32_t size;
    uint32_t version;
    xui_string title;
    float width;
    float height;
    uint32_t theme;
    uint32_t reserved;
} xui_window_options;
typedef struct xui_property {
    uint32_t size;
    uint32_t property;
    xui_handle target;
    xui_string text;
    float a, b, c, d;
    uint64_t integer;
} xui_property;
typedef struct xui_event {
    uint32_t size;
    uint32_t kind;
    xui_handle source;
    uint64_t value;
} xui_event;
typedef struct xui_file_item {
    uint32_t size;
    uint32_t directory;
    uint64_t id;
    xui_string name;
    xui_string path;
} xui_file_item;
/* The callback returns XUI_OK or an application failure code. It must not throw. */
typedef xui_status (XUI_CALL *xui_callback)(void* context, const xui_event* event);
typedef xui_status (XUI_CALL *xui_application_post_callback)(void* context, uint32_t execute);

/* Application and lifecycle calls use the creating STA. Post also accepts worker calls. */
XUI_API xui_status XUI_CALL xui_application_create(xui_handle* application) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_application_window_create(xui_handle application,
    const xui_window_options* options, uint32_t custom_titlebar, xui_handle* window) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_application_show(xui_handle application, xui_handle window) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_application_run(xui_handle application) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_application_shutdown(xui_handle application) XUI_NOEXCEPT;
/* Accepted posts execute once (1) or release once (0). Rejected posts retain caller ownership.
   Release can occur on the posting thread and must not call UI APIs. */
XUI_API xui_status XUI_CALL xui_application_post(xui_handle application,
    xui_application_post_callback callback, void* context) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_application_destroy(xui_handle application) XUI_NOEXCEPT;
/* State: created=0, open=1, closing=2, closed=3. Closed event kind is 100. */
XUI_API xui_status XUI_CALL xui_window_state(xui_handle window, uint32_t* state) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_window_closed(xui_handle window,
    xui_callback callback, void* context) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_window_error(xui_handle window, char* buffer,
    uint32_t capacity, uint32_t* required) XUI_NOEXCEPT;

/* Outer bounds in physical screen pixels. Negative screen coordinates are valid.
   Initialize size before get or set. Set accepts x/y in [-1000000,1000000] and width/height in [1,65536].
   Maximized is 0 or 1.
   Get requires an open window or a previously set placement. Unknown initial placement returns XUI_NATIVE_ERROR. */
typedef struct xui_window_placement {
    uint32_t size;
    int32_t x, y, width, height;
    uint32_t maximized;
} xui_window_placement;
XUI_API xui_status XUI_CALL xui_window_get_placement(xui_handle window,
    xui_window_placement* placement) XUI_NOEXCEPT;
/* Set also works before show. Coordinates describe the normal, non-maximized bounds. */
XUI_API xui_status XUI_CALL xui_window_set_placement(xui_handle window,
    const xui_window_placement* placement) XUI_NOEXCEPT;
enum {
    XUI_TAB_DRAG_REORDER = 0, XUI_TAB_DRAG_TEAR_OUT = 1, XUI_TAB_DRAG_DROP = 2,
    XUI_TAB_DRAG_CANCEL = 3, XUI_TAB_DRAG_COMPLETED = 4, XUI_TAB_DRAG_QUERY_DROP = 5,
    XUI_TAB_DRAG_JOIN = 6, XUI_TAB_DRAG_LEAVE = 7
};
typedef struct xui_tab_drag_event {
    uint32_t size, kind;
    uint32_t source_strip, target_strip;
    uint64_t tab_id;
    xui_handle target;
    uint64_t index;
} xui_tab_drag_event;
/* Source-window callback. Strips are 0 or 1. Target is a same-application window, or zero.
   Index is the insertion slot before removal, in [0, target tab count].
   Return XUI_OK and set accepted to 0 or 1. Errors close the source window.
   Tear-out must retain this HWND, the same strip, and the dragged tab ID.
   Move remaining models to other windows synchronously. Never transfer native controls.
   Reorder always uses the source strip. Drop can target the other strip in the same window.
   Drop occurs at button release, including an other-strip drop before tear-out.
   Query-drop validates the hovered target and index without mutation. Only accepted queries show an insertion indicator.
   Join temporarily transfers the model on hover; repeated Join can reorder within that destination.
   Rejecting Join keeps release-only Drop behavior. External Join follows Tear-out.
   Leave precedes retarget or Cancel. Accept Leave only after returning the tab to the initiator strip.
   Source strip and tab ID always identify the initiator; target fields identify the joined destination.
   Drop while joined commits the existing transfer, rather than transferring the tab again.
   Retain every window and control tree until Completed. Never reparent native controls.
   Escape sends cancel, not drop.
   Completed retires drag state after the move loop, including a joined commit. */
typedef xui_status (XUI_CALL *xui_tab_drag_handler)(void* context,
    const xui_tab_drag_event* event, uint32_t* accepted);
/* Opt-in requires an Application window with a custom title bar.
   A null callback revokes synchronously. Context remains caller-owned.
   Replacement and revocation fail during an active gesture, without changing the installed handler. */
XUI_API xui_status XUI_CALL xui_window_tab_drag_handler(xui_handle window,
    xui_tab_drag_handler callback, void* context) XUI_NOEXCEPT;

XUI_API uint32_t XUI_CALL xui_abi_version(void) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_error_copy(char* buffer, uint32_t capacity,
    uint32_t* required, xui_status* error) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_window_create(const xui_window_options* options,
    xui_handle* window) XUI_NOEXCEPT;
/* Destroy revokes all child handles and callbacks. Running/callback destruction returns BUSY. */
XUI_API xui_status XUI_CALL xui_window_destroy(xui_handle window) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_window_run(xui_handle window) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_window_close(xui_handle window) XUI_NOEXCEPT;
/* Extension only (".txt"), not a path. Empty uses a stock file/folder icon. */
XUI_API xui_status XUI_CALL xui_window_file_type_icon(xui_handle window,
    xui_string extension, uint32_t directory) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_window_callback_error(xui_handle window,
    xui_status* callback_status) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_create(xui_handle window, uint32_t kind,
    xui_string name, xui_handle content, xui_handle* result) XUI_NOEXCEPT;
/* Tab insertion motion: 0..10000 milliseconds, default zero. Initial population is immediate.
   Deletion, reorder, overflow, and geometry changes settle instead of animating incompatible slots. */
XUI_API xui_status XUI_CALL xui_tab_set_duration(xui_handle target, uint32_t milliseconds) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_tab_get_duration(xui_handle target, uint32_t* milliseconds) XUI_NOEXCEPT;
/* Reveal retains one unattached child from the same window and active content scope.
   Defaults: closed, duration zero. Duration is 0..10000 ms; zero disables motion.
   Closing disables interaction immediately. Fixed layout retains the full slot until closing completes;
   expand layout measures the child's natural extent times progress on the direction axis. */
typedef enum xui_reveal_layout {
    XUI_REVEAL_LAYOUT_FIXED = 0, XUI_REVEAL_LAYOUT_EXPAND = 1
} xui_reveal_layout;
typedef enum xui_reveal_direction {
    XUI_REVEAL_DIRECTION_BOTTOM = 0, XUI_REVEAL_DIRECTION_TOP = 1,
    XUI_REVEAL_DIRECTION_LEFT = 2, XUI_REVEAL_DIRECTION_RIGHT = 3
} xui_reveal_direction;
XUI_API xui_status XUI_CALL xui_reveal_create(xui_handle window, xui_handle content,
    xui_string name, xui_handle* result) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_reveal_set_open(xui_handle target, uint32_t open) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_reveal_get_open(xui_handle target, uint32_t* open) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_reveal_set_duration(xui_handle target, uint32_t milliseconds) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_reveal_get_duration(xui_handle target, uint32_t* milliseconds) XUI_NOEXCEPT;
/* Defaults: fixed layout, bottom direction. Changing either settles active motion. */
XUI_API xui_status XUI_CALL xui_reveal_set_layout(xui_handle target, uint32_t layout) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_reveal_get_layout(xui_handle target, uint32_t* layout) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_reveal_set_direction(xui_handle target, uint32_t direction) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_reveal_get_direction(xui_handle target, uint32_t* direction) XUI_NOEXCEPT;
/* Progress is 0..1. Boolean values are zero or one. */
XUI_API xui_status XUI_CALL xui_reveal_get_progress(xui_handle target, float* progress) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_reveal_get_animating(xui_handle target, uint32_t* animating) XUI_NOEXCEPT;
/* Expander body motion: 0..10000 milliseconds, default zero. */
XUI_API xui_status XUI_CALL xui_expander_set_duration(xui_handle target, uint32_t milliseconds) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_expander_get_duration(xui_handle target, uint32_t* milliseconds) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_expander_get_progress(xui_handle target, float* progress) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_expander_get_animating(xui_handle target, uint32_t* animating) XUI_NOEXCEPT;
/* Determinate progress motion: 0..10000 milliseconds, default zero. */
XUI_API xui_status XUI_CALL xui_progress_set_duration(xui_handle target, uint32_t milliseconds) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_progress_get_duration(xui_handle target, uint32_t* milliseconds) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_progress_get_presented_value(xui_handle target, double* value) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_progress_get_animating(xui_handle target, uint32_t* animating) XUI_NOEXCEPT;
/* Navigation main-branch disclosure motion: 0..10000 milliseconds, default zero. */
XUI_API xui_status XUI_CALL xui_navigation_view_set_duration(xui_handle target, uint32_t milliseconds) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_navigation_view_get_duration(xui_handle target, uint32_t* milliseconds) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_navigation_view_get_animating(xui_handle target, uint32_t* animating) XUI_NOEXCEPT;
/* Split motion is opt-in: 0..10000 milliseconds, default zero.
   Logical visibility changes immediately; progress reports retained presentation. */
XUI_API xui_status XUI_CALL xui_split_view_set_transition_duration(xui_handle target, uint32_t milliseconds) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_split_view_get_transition_duration(xui_handle target, uint32_t* milliseconds) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_split_view_get_progress(xui_handle target, float* progress) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_split_view_get_animating(xui_handle target, uint32_t* animating) XUI_NOEXCEPT;
/* Topology is immutable after run starts. Axis: zero horizontal, one vertical. */
XUI_API xui_status XUI_CALL xui_stack_create(xui_handle window, uint32_t axis,
    xui_handle* result) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_stack_add(xui_handle stack, xui_handle child, float flex) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_window_content(xui_handle window, xui_handle stack) XUI_NOEXCEPT;
/* Validates the entire batch before mutation. No callback or message dispatch during apply. */
XUI_API xui_status XUI_CALL xui_update(xui_handle window, const xui_property* properties,
    uint32_t count) XUI_NOEXCEPT;
/* A null callback revokes synchronously. One subscription per handle. */
XUI_API xui_status XUI_CALL xui_subscribe(xui_handle target, xui_callback callback,
    void* context) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_text_copy(xui_handle target, char* buffer,
    uint32_t capacity, uint32_t* required) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_focus(xui_handle target, uint32_t select_all) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_invoke(xui_handle target) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_image_source(xui_handle image, xui_string path,
    uint32_t width, uint32_t height) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_image_shell_source(xui_handle image, xui_string path,
    uint32_t width, uint32_t height) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_image_state(xui_handle image, uint32_t* state) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_list_items(xui_handle list,
    const xui_file_item* items, uint32_t count) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_list_filter(xui_handle list, xui_string query) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_list_select(xui_handle list, uint32_t index) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_list_state(xui_handle list, uint32_t* count,
    uint64_t* selected_id, uint32_t* has_selection) XUI_NOEXCEPT;

#include "xui_features.h"
#include "xui_file_transfer.h"
#include "xui_document_editing.h"
#include "xui_content.h"
#include "xui_file_dialog.h"

#ifdef __cplusplus
}
#endif
#endif
