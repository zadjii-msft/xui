#ifndef XUI_FEATURES_H
#define XUI_FEATURES_H
/* Included by xui.h. Existing ABI records and the 1.0 negotiation stay unchanged. */
#define XUI_FEATURE_VERSION 0x00010001u
/* Stage-1 Button styles. Colors are opaque 0xRRGGBB values in light/dark order.
   Dimensions are finite DIPs in [0,32768]. Absent fields must contain zero.
   Records and rule arrays are copied before return. Existing records are unchanged. */
#define XUI_BUTTON_STYLE_VERSION 0x00010000u
enum {
    XUI_BUTTON_STYLE_BACKGROUND = 1, XUI_BUTTON_STYLE_FOREGROUND = 2,
    XUI_BUTTON_STYLE_BORDER_BRUSH = 4, XUI_BUTTON_STYLE_BORDER_THICKNESS = 8,
    XUI_BUTTON_STYLE_PADDING = 16, XUI_BUTTON_STYLE_CORNER_RADIUS = 32
};
enum {
    XUI_BUTTON_STYLE_FOCUSED = 0, XUI_BUTTON_STYLE_CHECKED = 1,
    XUI_BUTTON_STYLE_HOVERED = 2, XUI_BUTTON_STYLE_PRESSED = 3,
    XUI_BUTTON_STYLE_DISABLED = 4
};
typedef struct xui_theme_color {
    uint32_t light, dark;
} xui_theme_color;
typedef struct xui_style_insets {
    float left, top, right, bottom;
} xui_style_insets;
typedef struct xui_button_style_values {
    uint32_t size, version, mask, reserved;
    xui_theme_color background, foreground, border_brush;
    xui_style_insets border_thickness, padding;
    float corner_radius;
    uint32_t reserved_end;
} xui_button_style_values;
typedef struct xui_button_style_rule {
    uint32_t size, state;
    xui_button_style_values values;
} xui_button_style_rule;
typedef struct xui_button_style_options {
    uint32_t size, version;
    xui_button_style_values values;
    const xui_button_style_rule* rules;
    uint32_t rule_count, reserved;
    xui_handle based_on;
} xui_button_style_options;
/* Styles belong to one window. At most 256 rules and 16 inheritance layers.
   A base must be a live style handle from the same window.
   Release removes the caller's handle. Buttons retain the applied definition.
   Derived styles retain copied inherited values, not the base definition. */
XUI_API xui_status XUI_CALL xui_button_style_create(xui_handle window,
    const xui_button_style_options* options, xui_handle* result) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_button_style_release(xui_handle style) XUI_NOEXCEPT;
/* A create result is also a weak identity, usable after releasing its handle.
   Reacquire returns a new releasable handle to the exact definition, or zero if
   it is no longer retained in this window. It never rebuilds a definition.
   Unknown, zero, expired, or other-window identities are successful cache misses. */
XUI_API xui_status XUI_CALL xui_button_style_reacquire(xui_handle window,
    xui_handle identity, xui_handle* result) XUI_NOEXCEPT;
/* Applies a retained identity without allocating a handle or rebuilding values.
   Sets applied=1 on a hit (including an unchanged assignment), otherwise zero.
   A miss preserves the button. Both helpers enforce normal mutation guards. */
XUI_API xui_status XUI_CALL xui_button_try_set_style(xui_handle button,
    xui_handle identity, uint32_t* applied) XUI_NOEXCEPT;
/* A zero style clears the shared style, but preserves local values. */
XUI_API xui_status XUI_CALL xui_button_set_style(xui_handle button, xui_handle style) XUI_NOEXCEPT;
/* An empty values record clears local overrides. Invalid input changes nothing. */
XUI_API xui_status XUI_CALL xui_button_set_style_values(xui_handle button,
    const xui_button_style_values* values) XUI_NOEXCEPT;
/* effective=0 returns local overrides. effective=1 returns the merged state values,
   before platform defaults and high-contrast protection. */
XUI_API xui_status XUI_CALL xui_button_get_style_values(xui_handle button,
    uint32_t effective, xui_button_style_values* values) XUI_NOEXCEPT;
enum {
    XUI_RANGE_INPUT = 10, XUI_RADIO_GROUP, XUI_COMBO_BOX, XUI_NUMERIC_INPUT,
    XUI_EXPANDER, XUI_PROGRESS, XUI_POPUP, XUI_SPLIT_BUTTON,
    XUI_ITEMS_VIEW, XUI_TREE_VIEW, XUI_GRID, XUI_WRAP, XUI_ADAPTIVE_LAYOUT,
    XUI_COMMAND_BAR, XUI_COMMAND_SURFACE, XUI_BREADCRUMB, XUI_NAVIGATION_PANE,
    XUI_LOCATION_PICKER, XUI_VIEW_PICKER, XUI_MULTILINE_TEXT, XUI_RICH_TEXT,
    XUI_PASSWORD_INPUT, XUI_DATE_TIME_PICKER, XUI_INLINE_STATUS, XUI_COLOR_PICKER,
    XUI_CONTENT_DIALOG, XUI_VECTOR_CANVAS, XUI_MAP_VIEW, XUI_MEDIA_PLAYBACK,
    XUI_WEB_CONTENT, XUI_TAB_STRIP, XUI_SPLIT_VIEW, XUI_PAGE_VIEW, XUI_DATA_GRID,
    XUI_HISTORY_CHART, XUI_NAVIGATION_VIEW
};
enum { XUI_PREVIEW = 7, XUI_CANCEL = 8, XUI_ACTION = 9, XUI_DISMISS = 10, XUI_REQUEST = 11, XUI_FILTER_OPEN = 12, XUI_FOCUS_ENTERED = 13 };
enum {
    XUI_F_RANGE = 1, XUI_F_VALUE, XUI_F_ORIENTATION, XUI_F_REVERSED,
    XUI_F_EXPANDED, XUI_F_PROGRESS_STATE, XUI_F_HELP, XUI_F_TOOLTIP_DELAY,
    XUI_F_BUTTON_BEHAVIOR, XUI_F_BUTTON_CHECKED, XUI_F_REPEAT_TIMING,
    XUI_F_DOCUMENT_TEXT, XUI_F_READ_ONLY, XUI_F_MAXIMUM_LENGTH, XUI_F_TEXT_SELECTION,
    XUI_F_PASSWORD, XUI_F_PASSWORD_REVEAL, XUI_F_DATE, XUI_F_COLOR,
    XUI_F_STATUS, XUI_F_DISMISSIBLE, XUI_F_MAP_VIEW, XUI_F_MEDIA_SOURCE,
    XUI_F_VOLUME, XUI_F_WEB_HTML, XUI_F_WEB_PROFILE, XUI_F_ITEM_SIZE,
    XUI_F_PRESENTATION, XUI_F_OFFSET, XUI_F_WRAP_WIDTH, XUI_F_BREAKPOINT,
    XUI_F_NAVIGATION_EXTENT, XUI_F_NAVIGATION_OPEN, XUI_F_COMPACT_NAVIGATION,
    XUI_F_SPLIT_RATIO, XUI_F_PAGE, XUI_F_VISIBLE, XUI_F_HOST_STATE,
    XUI_F_MAP_PAN, XUI_F_MEDIA_POSITION, XUI_F_SELECTION_STATE, XUI_F_FOCUSED, XUI_F_IS_OPEN,
    XUI_F_SECOND_VISIBLE, XUI_F_BUTTON_ICON
};
enum {
    XUI_A_SELECT = 1, XUI_A_CHANGE_VALUE, XUI_A_STEP, XUI_A_TEXT_COMMAND,
    XUI_A_DISMISS, XUI_A_SHOW, XUI_A_ACCEPT, XUI_A_CANCEL, XUI_A_PLAY,
    XUI_A_PAUSE, XUI_A_STOP, XUI_A_UNLOAD, XUI_A_RELOAD, XUI_A_FOCUS,
    XUI_A_SELECT_ALL, XUI_A_COLLECTION_STEP, XUI_A_GRID_NAVIGATE
};
/* GRID_NAVIGATE: first = previous/next/page previous/page next/first/last (0..5),
   second = control (1) | shift (2). Moves selection without moving input focus. */
typedef struct xui_navigation_entry {
    uint32_t size, flags; /* disabled=1, not-selectable=2, collapsed=4 */
    uint64_t id, parent;
    xui_string label, keywords;
} xui_navigation_entry;
typedef struct xui_item_visual {
    uint32_t size, icon;
    xui_string image_path;
} xui_item_visual;
/* Optional parallel visual records. Existing navigation records remain unchanged. */
XUI_API xui_status XUI_CALL xui_navigation_items_visual(xui_handle target,
    const xui_navigation_entry* items, const xui_item_visual* visuals, uint32_t count) XUI_NOEXCEPT;
typedef struct xui_key_event {
    uint32_t size, virtual_key, modifiers, reserved; /* control=1, shift=2, alt=4 */
    xui_handle target;
} xui_key_event;
typedef xui_status (XUI_CALL *xui_key_handler)(void*, const xui_key_event*, uint32_t*);
typedef struct xui_navigation_event {
    uint32_t size, direction, has_position, reserved; /* back=0, forward=1 */
    xui_handle target;
    float x, y; /* Window-local DIPs: pointer position or source control center. */
} xui_navigation_event;
typedef xui_status (XUI_CALL *xui_navigation_handler)(void*, const xui_navigation_event*, uint32_t*);
typedef xui_status (XUI_CALL *xui_post_callback)(void*, uint32_t);
XUI_API xui_status XUI_CALL xui_navigation_items(xui_handle target,
    const xui_navigation_entry* items, uint32_t count) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_window_title(xui_handle window, xui_string title) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_window_key_handler(xui_handle window,
    xui_key_handler callback, void* context) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_window_navigation_handler(xui_handle window,
    xui_navigation_handler callback, void* context) XUI_NOEXCEPT;
/* The only cross-thread window operation. On success callback runs exactly once:
   execute=1 on the UI thread, or execute=0 when discarded. Rejection does not call it.
   Callbacks must not throw. Close rejects further posts with XUI_CLOSED. */
XUI_API xui_status XUI_CALL xui_window_post(xui_handle window,
    xui_post_callback callback, void* context) XUI_NOEXCEPT;
typedef struct xui_feature_options {
    uint32_t size, version;
    xui_string name;
    xui_handle content, second;
    uint32_t mode, reserved;
} xui_feature_options;
/* Numeric fields have operation-specific meanings. All unused fields must be zero.
   TextSelection offsets count UTF-16 code units. Split surrogate pairs are rejected.
   XUI_F_SELECTION_STATE is read-only: a indicates focus; b counts compact terms.
   first and second contain the focused key's id and version when a is nonzero. */
typedef struct xui_feature_value {
    uint32_t size, version;
    double a, b, c, d;
    uint64_t first, second;
    xui_string text;
} xui_feature_value;
typedef struct xui_choice {
    uint32_t size, flags; /* bit 0: disabled */
    uint64_t id, version;
    xui_string text;
} xui_choice;
typedef struct xui_text_run {
    uint32_t size, flags; /* bold=1, italic=2, underline=4 */
    xui_string text, link;
} xui_text_run;
typedef xui_status (XUI_CALL *xui_secret_receiver)(void*, const char*, uint32_t);
XUI_API uint32_t XUI_CALL xui_feature_version(void) XUI_NOEXCEPT;
/* Bit 0: this build includes WebView2. No runtime starts from this query. */
XUI_API uint64_t XUI_CALL xui_capabilities(void) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_feature_create(xui_handle window, uint32_t kind,
    const xui_feature_options* options, xui_handle* result) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_feature_set(xui_handle target, uint32_t property,
    const xui_feature_value* value) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_feature_get(xui_handle target, uint32_t property,
    xui_feature_value* value) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_feature_action(xui_handle target, uint32_t action,
    uint64_t first, uint64_t second) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_choices(xui_handle target, const xui_choice* items,
    uint32_t count, uint64_t selected, uint32_t has_selection) XUI_NOEXCEPT;
/* Borrowed child handles remain valid only while their window lives.
   Window indices: 0 = title tabs, 1 = leading button, 2 = secondary title tabs.
   Window children require a custom title bar. NavigationView index 0 returns its search input.
   Secondary tabs and the leading button are hidden by default. */
XUI_API xui_status XUI_CALL xui_feature_child(xui_handle target, uint32_t index,
    xui_handle* result) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_panel_add(xui_handle target, xui_handle child,
    uint32_t row, uint32_t column, uint32_t row_span, uint32_t column_span) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_popup_show(xui_handle target, xui_handle anchor) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_rich_runs(xui_handle target, const xui_text_run* runs,
    uint32_t count) XUI_NOEXCEPT;
/* Plaintext is valid only during this callback. The callback must not throw or retain it. */
XUI_API xui_status XUI_CALL xui_password_read(xui_handle target,
    xui_secret_receiver receiver, void* context) XUI_NOEXCEPT;
/* Immutable source callbacks run on the UI thread. They must be bounded and nonblocking.
   Query 0: key at index. Query 1: content at index/column. Query 2: find (id,version).
   Query 3: has children. UINT64_MAX denotes an absent find result.
   Strings exclude a trailing NUL and have a 1024-byte limit each. No row array is copied. */
typedef struct xui_source_row {
    uint32_t size, flags; /* disabled=1, has-progress=2, checked=4, has-check=8 */
    uint64_t index, id, version;
    double progress;
    uint32_t primary_length, secondary_length;
    char primary[1024], secondary[1024];
} xui_source_row;
typedef xui_status (XUI_CALL *xui_source_query)(void*, uint32_t, uint64_t, uint64_t, xui_source_row*);
typedef void (XUI_CALL *xui_context_ref)(void*);
typedef struct xui_source_options {
    uint32_t size, version;
    uint64_t count;
    void* context;
    xui_source_query query;
    xui_context_ref retain, release;
} xui_source_options;
XUI_API xui_status XUI_CALL xui_source_create(xui_handle window,
    const xui_source_options* options, xui_handle* result) XUI_NOEXCEPT;
/* Runs on the UI thread for visible visuals only. Strings are copied before return.
   The first call has capacity zero. required is the full UTF-8 byte count.
   Insufficient capacity returns XUI_BUFFER_TOO_SMALL. No truncation is permitted.
   Uses the source options context and its existing retain/release lifetime. */
typedef xui_status (XUI_CALL *xui_source_visual_query)(void*, uint64_t, uint64_t,
    uint32_t*, char*, uint32_t, uint32_t*);
XUI_API xui_status XUI_CALL xui_source_create_visual(xui_handle window,
    const xui_source_options* options, xui_source_visual_query visual, xui_handle* result) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_source_attach(xui_handle target, xui_handle source) XUI_NOEXCEPT;
/* Release the caller's snapshot handle. Attached controls and selection terms retain their own source references. */
XUI_API xui_status XUI_CALL xui_source_release(xui_handle source) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_collection_contains(xui_handle target,
    uint64_t id, uint64_t version, uint32_t* selected) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_tree_expand(xui_handle target, uint64_t id,
    uint64_t version, uint32_t expanded) XUI_NOEXCEPT;
/* The request event value is an opaque, owner-bound token. Completion consumes it.
   Cancellation and owner disposal revoke delivery. There is no worker-thread UI access. */
XUI_API xui_status XUI_CALL xui_tree_complete(xui_handle target, xui_handle request,
    xui_handle source, xui_string error) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_request_cancel(xui_handle request) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_request_info(xui_handle request, xui_feature_value* value) XUI_NOEXCEPT;
typedef struct xui_grid_track {
    uint32_t size, sizing;
    float value, minimum, maximum;
    uint32_t reserved;
} xui_grid_track;
XUI_API xui_status XUI_CALL xui_grid_tracks(xui_handle target,
    const xui_grid_track* rows, uint32_t row_count,
    const xui_grid_track* columns, uint32_t column_count) XUI_NOEXCEPT;
typedef struct xui_column {
    uint32_t size, flags; /* numeric=1, filterable=2, checkable=4 */
    xui_string name;
    float width;
    uint32_t reserved;
} xui_column;
XUI_API xui_status XUI_CALL xui_grid_columns(xui_handle target,
    const xui_column* columns, uint32_t count) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_grid_column_width(xui_handle target,
    uint32_t column, float width) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_grid_column_order(xui_handle target,
    const uint32_t* order, uint32_t count) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_grid_filter(xui_handle target,
    uint32_t source_column, xui_string query) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_grid_sort(xui_handle target,
    uint32_t source_column, uint32_t descending) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_grid_check(xui_handle target,
    uint64_t id, uint64_t version, uint32_t checked) XUI_NOEXCEPT;
typedef struct xui_command_record {
    uint32_t size, kind;
    uint64_t id, parent;
    xui_string label, hint, pin_label;
    uint32_t flags, icon; /* disabled=1, checked=2, has-check=4 */
} xui_command_record;
/* Independent of the control event subscription. Request supplies items synchronously.
   Action carries the selected command ID. Menus support flat actions and separators.
   A null callback revokes the menu. Callbacks use the window UI thread. */
XUI_API xui_status XUI_CALL xui_context_menu_bind(xui_handle target,
    xui_callback callback, void* context) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_context_menu_items(xui_handle target,
    const xui_command_record* commands, uint32_t count) XUI_NOEXCEPT;
/* During Request only. Copies 0..256 paths for native Shell verbs in the same menu.
   Nonempty paths require a selected row and one common filesystem parent.
   Selection, source replacement, or subscription changes cancel pending actions.
   Discovery does not invoke a verb. Native Shell Open keeps its system behavior. */
XUI_API xui_status XUI_CALL xui_context_menu_shell_paths(xui_handle target,
    const xui_string* paths, uint32_t count) XUI_NOEXCEPT;
/* During Request only: 0 = native Windows menu (default), 1 = XUI Shell commands
   with an explicit Windows-menu fallback. Existing subscriptions remain native. */
XUI_API xui_status XUI_CALL xui_context_menu_presentation(xui_handle target, uint32_t presentation) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_commands_set(xui_handle target,
    const xui_command_record* commands, uint32_t count) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_command_invoke(xui_handle target,
    uint64_t id, uint32_t pin) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_command_bind(xui_handle target,
    uint64_t id, uint32_t key, uint32_t modifiers) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_shell_show(xui_handle anchor,
    const xui_string* paths, uint32_t count) XUI_NOEXCEPT;
typedef struct xui_scene_point { float x, y; } xui_scene_point;
typedef struct xui_shape {
    uint32_t size, flags; /* closed=1, interactive=2, has-clip=4 */
    uint64_t id;
    const xui_scene_point* points;
    uint32_t count, reserved;
    float fill_red, fill_green, fill_blue, fill_alpha;
    float stroke_red, stroke_green, stroke_blue, stroke_alpha;
    float stroke_width, clip_x, clip_y, clip_width, clip_height;
    uint32_t reserved2;
    double m11, m12, m21, m22, dx, dy;
    xui_string name;
} xui_shape;
XUI_API xui_status XUI_CALL xui_canvas_scene(xui_handle target,
    const xui_shape* shapes, uint32_t count) XUI_NOEXCEPT;
typedef struct xui_map_marker {
    uint32_t size, reserved;
    uint64_t id;
    double latitude, longitude;
    xui_string name;
} xui_map_marker;
XUI_API xui_status XUI_CALL xui_map_markers(xui_handle target,
    const xui_map_marker* markers, uint32_t count) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_map_request(xui_handle target, xui_handle* request) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_map_complete(xui_handle target, xui_handle request,
    const xui_map_marker* markers, uint32_t count) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_window_create_features(const xui_window_options* options,
    uint32_t custom_titlebar, xui_handle* result) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_dialog_validation(xui_handle target, xui_string message) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_web_origins(xui_handle target,
    const xui_string* origins, uint32_t count) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_web_navigate(xui_handle target, xui_string uri) XUI_NOEXCEPT;
/* Native-owned result token. No foreign callback remains pinned during JavaScript.
   BUSY means pending. CLOSED means canceled by stop/unload/hide/owner close.
   request_cancel releases a pending or completed result without a completion callback. */
XUI_API xui_status XUI_CALL xui_web_evaluate(xui_handle target, xui_string script, xui_handle* request) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_web_result(xui_handle request, char* buffer,
    uint32_t capacity, uint32_t* required, uint32_t* is_error) XUI_NOEXCEPT;
#endif
