#ifndef XUI_FEATURES_H
#define XUI_FEATURES_H
/* Included by xui.h. Existing ABI records and the 1.0 negotiation stay unchanged. */
#define XUI_FEATURE_VERSION 0x00010001u
enum {
    XUI_BUTTON_ICON_NONE = 0, XUI_BUTTON_ICON_BACK = 1, XUI_BUTTON_ICON_FORWARD = 2,
    XUI_BUTTON_ICON_UP = 3, XUI_BUTTON_ICON_REFRESH = 4, XUI_BUTTON_ICON_SPLIT = 5,
    XUI_BUTTON_ICON_THEME = 6, XUI_BUTTON_ICON_ADD = 7, XUI_BUTTON_ICON_MINIMIZE = 8,
    XUI_BUTTON_ICON_MAXIMIZE = 9, XUI_BUTTON_ICON_RESTORE = 10, XUI_BUTTON_ICON_CLOSE = 11,
    XUI_BUTTON_ICON_MORE = 12, XUI_BUTTON_ICON_NAVIGATION = 13, XUI_BUTTON_ICON_HOME = 14,
    XUI_BUTTON_ICON_FOLDER = 15, XUI_BUTTON_ICON_SETTINGS = 16, XUI_BUTTON_ICON_SEARCH = 17,
    XUI_BUTTON_ICON_LIBRARY = 18, XUI_BUTTON_ICON_HISTORY = 19, XUI_BUTTON_ICON_BOOKMARK = 20,
    XUI_BUTTON_ICON_DRIVE = 21, XUI_BUTTON_ICON_OPEN = 22,
    XUI_BUTTON_ICON_SAVE = 23, XUI_BUTTON_ICON_SAVE_AS = 24,
    XUI_BUTTON_ICON_UNDO = 25, XUI_BUTTON_ICON_REDO = 26,
    XUI_BUTTON_ICON_CHEVRON_UP = 27, XUI_BUTTON_ICON_CHEVRON_DOWN = 28,
    XUI_BUTTON_ICON_FOLDERS_FIRST = 29, XUI_BUTTON_ICON_FILES_FIRST = 30, XUI_BUTTON_ICON_MIXED = 31
};
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
#define XUI_CONTROL_STYLE_VERSION 0x00010000u
enum {
    XUI_STYLE_TARGET_TOGGLE = 0, XUI_STYLE_TARGET_BUTTON, XUI_STYLE_TARGET_LABEL, XUI_STYLE_TARGET_TEXT_INPUT,
    XUI_STYLE_TARGET_MULTILINE_TEXT, XUI_STYLE_TARGET_RICH_TEXT, XUI_STYLE_TARGET_PASSWORD_INPUT, XUI_STYLE_TARGET_DATE_TIME_PICKER,
    XUI_STYLE_TARGET_RADIO_GROUP, XUI_STYLE_TARGET_CHOICE_LIST, XUI_STYLE_TARGET_COMBO_BOX, XUI_STYLE_TARGET_NUMERIC_INPUT,
    XUI_STYLE_TARGET_RANGE_INPUT, XUI_STYLE_TARGET_PROGRESS, XUI_STYLE_TARGET_INLINE_STATUS, XUI_STYLE_TARGET_COLOR_PICKER,
    XUI_STYLE_TARGET_STACK, XUI_STYLE_TARGET_GRID, XUI_STYLE_TARGET_WRAP, XUI_STYLE_TARGET_ADAPTIVE_LAYOUT,
    XUI_STYLE_TARGET_PAGE_VIEW, XUI_STYLE_TARGET_CONTENT_VIEW, XUI_STYLE_TARGET_SCROLL_VIEW, XUI_STYLE_TARGET_SPLIT_VIEW,
    XUI_STYLE_TARGET_EXPANDER, XUI_STYLE_TARGET_POPUP, XUI_STYLE_TARGET_FILE_LIST, XUI_STYLE_TARGET_ITEMS_VIEW,
    XUI_STYLE_TARGET_TREE_VIEW, XUI_STYLE_TARGET_NAVIGATION_LIST, XUI_STYLE_TARGET_COMMAND_MENU, XUI_STYLE_TARGET_DATA_GRID,
    XUI_STYLE_TARGET_HISTORY_CHART, XUI_STYLE_TARGET_NAVIGATION_VIEW, XUI_STYLE_TARGET_BREADCRUMB, XUI_STYLE_TARGET_NAVIGATION_PANE,
    XUI_STYLE_TARGET_LOCATION_PICKER, XUI_STYLE_TARGET_VIEW_PICKER, XUI_STYLE_TARGET_TAB_STRIP, XUI_STYLE_TARGET_SPLIT_BUTTON,
    XUI_STYLE_TARGET_COMMAND_BAR, XUI_STYLE_TARGET_COMMAND_SURFACE, XUI_STYLE_TARGET_COMMAND_PALETTE, XUI_STYLE_TARGET_SHELL_MENU,
    XUI_STYLE_TARGET_CONTENT_DIALOG, XUI_STYLE_TARGET_TITLE_BAR, XUI_STYLE_TARGET_TOOLTIP, XUI_STYLE_TARGET_IMAGE,
    XUI_STYLE_TARGET_VECTOR_CANVAS, XUI_STYLE_TARGET_MAP_VIEW, XUI_STYLE_TARGET_MEDIA_PLAYBACK, XUI_STYLE_TARGET_WEB_CONTENT
};
enum {
    XUI_STYLE_ROOT = 0, XUI_STYLE_LABEL, XUI_STYLE_INDICATOR, XUI_STYLE_MARK, XUI_STYLE_TEXT_PART,
    XUI_STYLE_ICON, XUI_STYLE_CONTENT, XUI_STYLE_HEADER, XUI_STYLE_FOOTER, XUI_STYLE_SEPARATOR, XUI_STYLE_FIELD,
    XUI_STYLE_PLACEHOLDER, XUI_STYLE_CLEAR_ACTION, XUI_STYLE_SHORTCUT, XUI_STYLE_REVEAL_ACTION,
    XUI_STYLE_DECREMENT, XUI_STYLE_INCREMENT, XUI_STYLE_ITEM, XUI_STYLE_SELECTED_MARKER, XUI_STYLE_ARROW,
    XUI_STYLE_POPUP, XUI_STYLE_TRACK, XUI_STYLE_FILL, XUI_STYLE_THUMB, XUI_STYLE_CAPTION, XUI_STYLE_MESSAGE,
    XUI_STYLE_ACTION, XUI_STYLE_DISMISS, XUI_STYLE_STRIPE, XUI_STYLE_PREVIEW, XUI_STYLE_CHECKERBOARD,
    XUI_STYLE_SWATCH, XUI_STYLE_CHANNEL, XUI_STYLE_ROW, XUI_STYLE_SECONDARY_TEXT, XUI_STYLE_FOCUS_MARKER,
    XUI_STYLE_SCROLLBAR, XUI_STYLE_SCROLLBAR_TRACK, XUI_STYLE_SCROLLBAR_THUMB, XUI_STYLE_TILE,
    XUI_STYLE_GROUP_HEADER, XUI_STYLE_DISCLOSURE, XUI_STYLE_PENDING, XUI_STYLE_ERROR_PART, XUI_STYLE_CELL,
    XUI_STYLE_GRID_LINE, XUI_STYLE_SORT_ICON, XUI_STYLE_FILTER_ICON, XUI_STYLE_REORDER_MARKER,
    XUI_STYLE_ALTERNATING_ROW, XUI_STYLE_TITLE, XUI_STYLE_PLOT, XUI_STYLE_PANE, XUI_STYLE_DIVIDER, XUI_STYLE_GRIP,
    XUI_STYLE_TAB, XUI_STYLE_CLOSE_ACTION, XUI_STYLE_ADD_ACTION, XUI_STYLE_OVERFLOW, XUI_STYLE_SEARCH,
    XUI_STYLE_PRIMARY_ACTION, XUI_STYLE_CANCEL_ACTION, XUI_STYLE_SELECTION, XUI_STYLE_EMPTY_PART,
    XUI_STYLE_VALIDATION, XUI_STYLE_BADGE, XUI_STYLE_TOOLBAR, XUI_STYLE_COORDINATE, XUI_STYLE_STATUS,
    XUI_STYLE_PRIMARY_TEXT, XUI_STYLE_HEADING, XUI_STYLE_FIRST_PANE, XUI_STYLE_SECOND_PANE,
    XUI_STYLE_CHECKERBOARD_LIGHT, XUI_STYLE_CHECKERBOARD_DARK, XUI_STYLE_CHANNEL_LABEL, XUI_STYLE_CHANNEL_FIELD,
    XUI_STYLE_CAPTION_BUTTON, XUI_STYLE_CAPTION_CLOSE
};
enum { XUI_STYLE_COLOR = 1, XUI_STYLE_INSETS = 2, XUI_STYLE_NUMBER = 3, XUI_STYLE_TEXT = 4 };
enum {
    XUI_STYLE_BACKGROUND = 1, XUI_STYLE_FOREGROUND = 2, XUI_STYLE_BORDER_BRUSH = 4,
    XUI_STYLE_BORDER_THICKNESS = 8, XUI_STYLE_PADDING = 16, XUI_STYLE_CORNER_RADIUS = 32, XUI_STYLE_SIZE = 64,
    XUI_STYLE_FONT_FAMILY = 128, XUI_STYLE_FONT_SIZE = 256, XUI_STYLE_FONT_WEIGHT = 512, XUI_STYLE_FONT_STYLE = 1024,
    XUI_STYLE_HORIZONTAL_ALIGNMENT = 2048, XUI_STYLE_VERTICAL_ALIGNMENT = 4096, XUI_STYLE_SPACING = 8192,
    XUI_STYLE_ROW_HEIGHT = 16384, XUI_STYLE_HEADER_HEIGHT = 32768, XUI_STYLE_INDENTATION = 65536,
    XUI_STYLE_THICKNESS = 131072, XUI_STYLE_WIDTH = 262144, XUI_STYLE_HEIGHT = 524288,
    XUI_STYLE_ROW_GAP = 1048576, XUI_STYLE_COLUMN_GAP = 2097152, XUI_STYLE_MAXIMUM_LINES = 4194304,
    XUI_STYLE_WRAPPING = 8388608
};
enum {
    XUI_STYLE_FOCUSED = 1, XUI_STYLE_CHECKED = 2, XUI_STYLE_HOVERED = 4,
    XUI_STYLE_PRESSED = 8, XUI_STYLE_DISABLED = 16
};
#define XUI_STYLE_STATE_SELECTED (UINT64_C(1) << 5)
#define XUI_STYLE_STATE_EXPANDED (UINT64_C(1) << 6)
#define XUI_STYLE_STATE_INVALID (UINT64_C(1) << 7)
#define XUI_STYLE_STATE_LOADING (UINT64_C(1) << 8)
#define XUI_STYLE_STATE_ERROR (UINT64_C(1) << 9)
#define XUI_STYLE_STATE_EMPTY (UINT64_C(1) << 10)
#define XUI_STYLE_STATE_OPEN (UINT64_C(1) << 11)
#define XUI_STYLE_STATE_DRAGGING (UINT64_C(1) << 12)
#define XUI_STYLE_STATE_MINIMUM (UINT64_C(1) << 13)
#define XUI_STYLE_STATE_MAXIMUM (UINT64_C(1) << 14)
#define XUI_STYLE_STATE_INDETERMINATE (UINT64_C(1) << 15)
#define XUI_STYLE_STATE_PAUSED (UINT64_C(1) << 16)
#define XUI_STYLE_STATE_UNKNOWN (UINT64_C(1) << 17)
#define XUI_STYLE_STATE_INFORMATION (UINT64_C(1) << 18)
#define XUI_STYLE_STATE_SUCCESS (UINT64_C(1) << 19)
#define XUI_STYLE_STATE_WARNING (UINT64_C(1) << 20)
#define XUI_STYLE_STATE_DISMISSED (UINT64_C(1) << 21)
#define XUI_STYLE_STATE_REVEALED (UINT64_C(1) << 22)
#define XUI_STYLE_STATE_READ_ONLY (UINT64_C(1) << 23)
#define XUI_STYLE_STATE_SORTED (UINT64_C(1) << 24)
#define XUI_STYLE_STATE_DESCENDING (UINT64_C(1) << 25)
#define XUI_STYLE_STATE_FILTERED (UINT64_C(1) << 26)
#define XUI_STYLE_STATE_MIXED (UINT64_C(1) << 27)
#define XUI_STYLE_STATE_FILTER_PENDING (UINT64_C(1) << 28)
#define XUI_STYLE_STATE_SELECTED_DESCENDANT (UINT64_C(1) << 29)
#define XUI_STYLE_STATE_COMPACT (UINT64_C(1) << 30)
#define XUI_STYLE_STATE_OVERFLOWED (UINT64_C(1) << 31)
#define XUI_STYLE_STATE_CURRENT (UINT64_C(1) << 32)
#define XUI_STYLE_STATE_ACTIVE (UINT64_C(1) << 33)
#define XUI_STYLE_STATE_INACTIVE (UINT64_C(1) << 34)
#define XUI_STYLE_STATE_MAXIMIZED (UINT64_C(1) << 35)
#define XUI_STYLE_STATE_READY (UINT64_C(1) << 36)
#define XUI_STYLE_STATE_IDLE (UINT64_C(1) << 37)
#define XUI_STYLE_STATE_PLAYING (UINT64_C(1) << 38)
#define XUI_STYLE_STATE_STOPPED (UINT64_C(1) << 39)
#define XUI_STYLE_STATE_SUSPENDED (UINT64_C(1) << 40)
#define XUI_STYLE_STATE_SCROLLABLE (UINT64_C(1) << 41)
#define XUI_STYLE_STATE_DETERMINATE (UINT64_C(1) << 42)
/* One typed property on one part. State zero denotes an ordinary value.
   All inactive carriers and reserved fields must be zero. Font family uses UTF-8
   text. Colors preserve both themes. Dimensions use finite DIPs [0,32768].
   Unknown targets, parts, properties, types, and state bits are errors. */
typedef struct xui_style_property {
    uint32_t size, version, property, value_type, part, reserved;
    uint64_t state;
    xui_theme_color color;
    xui_style_insets insets;
    double number;
    xui_string text;
} xui_style_property;
typedef struct xui_control_style_options {
    uint32_t size, version, target, reserved;
    const xui_style_property* properties;
    uint32_t property_count, reserved_end;
    xui_handle based_on;
} xui_control_style_options;
/* At most 2048 property records and 16 inheritance layers. Duplicate
   (part,state,property) records are errors. Inputs are copied before return.
   Styles and live handles belong to one window. Release preserves attachments.
   A create result is also a weak identity. Expired/unknown identities miss. */
XUI_API xui_status XUI_CALL xui_control_style_create(xui_handle window,
    const xui_control_style_options* options, xui_handle* result) XUI_NOEXCEPT;
/* Pure schema lookup. Unsupported targets or parts are errors. No window is required. */
XUI_API xui_status XUI_CALL xui_control_style_get_schema(uint32_t target, uint32_t part,
    uint64_t* properties, uint64_t* states, uint64_t* state_properties) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_control_style_get_limits(uint32_t target, uint32_t part,
    float* maximum_font_size, uint32_t* maximum_font_family_utf16, uint32_t* font_styles,
    uint32_t* horizontal_alignments, uint32_t* vertical_alignments) XUI_NOEXCEPT;
/* Tooltip styling uses the Window host, not an Element handle. Zero style clears the definition. */
XUI_API xui_status XUI_CALL xui_window_set_tooltip_style(xui_handle window, xui_handle style) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_window_try_set_tooltip_style(xui_handle window, xui_handle identity,
    uint32_t* applied) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_window_set_tooltip_style_values(xui_handle window, uint32_t part,
    const xui_style_property* properties, uint32_t count) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_window_get_tooltip_style_values(xui_handle window, uint32_t part, uint32_t effective,
    xui_style_property* properties, uint32_t capacity, uint32_t* count) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_control_style_release(xui_handle style) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_control_style_reacquire(xui_handle window,
    xui_handle identity, xui_handle* result) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_control_try_set_style(xui_handle control,
    xui_handle identity, uint32_t* applied) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_control_set_style(xui_handle control, xui_handle style) XUI_NOEXCEPT;
/* Replaces all local properties of one part. An empty span clears that part.
   Each record must name this part and state zero. Invalid input changes nothing. */
XUI_API xui_status XUI_CALL xui_control_set_style_values(xui_handle control, uint32_t part,
    const xui_style_property* properties, uint32_t count) XUI_NOEXCEPT;
/* effective=0 reads locals; effective=1 reads merged values before platform policy.
   count receives the required capacity. A zero-capacity query succeeds.
   With insufficient capacity, no records change and XUI_BUFFER_TOO_SMALL results.
   Output records contain their own size/version and state zero.
   Output text is borrowed until the next style mutation or owner destruction. */
XUI_API xui_status XUI_CALL xui_control_get_style_values(xui_handle control, uint32_t part,
    uint32_t effective, xui_style_property* properties, uint32_t capacity, uint32_t* count) XUI_NOEXCEPT;
enum {
    XUI_RANGE_INPUT = 10, XUI_RADIO_GROUP, XUI_COMBO_BOX, XUI_NUMERIC_INPUT,
    XUI_EXPANDER, XUI_PROGRESS, XUI_POPUP, XUI_SPLIT_BUTTON,
    XUI_ITEMS_VIEW, XUI_TREE_VIEW, XUI_GRID, XUI_WRAP, XUI_ADAPTIVE_LAYOUT,
    XUI_COMMAND_BAR, XUI_COMMAND_SURFACE, XUI_BREADCRUMB, XUI_NAVIGATION_PANE,
    XUI_LOCATION_PICKER, XUI_VIEW_PICKER, XUI_MULTILINE_TEXT, XUI_RICH_TEXT,
    XUI_PASSWORD_INPUT, XUI_DATE_TIME_PICKER, XUI_INLINE_STATUS, XUI_COLOR_PICKER,
    XUI_CONTENT_DIALOG, XUI_VECTOR_CANVAS, XUI_MAP_VIEW, XUI_MEDIA_PLAYBACK,
    XUI_WEB_CONTENT, XUI_TAB_STRIP, XUI_SPLIT_VIEW, XUI_PAGE_VIEW, XUI_DATA_GRID,
    XUI_HISTORY_CHART, XUI_NAVIGATION_VIEW, XUI_MILLER_COLUMNS,
    /* Retained Element exposure only; xui_feature_create does not create this kind. */
    XUI_RETAINED_ELEMENT,
    XUI_TOGGLE_SWITCH = 48, XUI_TOGGLE_BUTTON, XUI_PROGRESS_RING,
    XUI_CHECK_BOX, XUI_HYPERLINK_BUTTON, XUI_SELECTOR_BAR, XUI_INFO_BADGE, XUI_MENU_BAR,
    XUI_SWAP_CHAIN_PANEL
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
    XUI_F_SECOND_VISIBLE, XUI_F_BUTTON_ICON,
    XUI_F_CHECKED, XUI_F_PROGRESS_CAPACITY,
    XUI_F_CHECK_STATE, XUI_F_THREE_STATE, XUI_F_SELECTED,
    XUI_F_BADGE_KIND, XUI_F_BADGE_COUNT, XUI_F_BADGE_ICON
};
/* CHECKED: first = boolean, for Toggle and ToggleSwitch.
   PROGRESS_CAPACITY: a = used, b = total, text = unit, for Progress and ProgressRing.
   ToggleButton uses BUTTON_CHECKED; its toggle notification is CHANGE (boolean).
   ProgressRing starts indeterminate. Existing feature and style IDs are unchanged. */
/* CHECK_STATE: first = unchecked (0), checked (1), indeterminate (2).
   THREE_STATE: first = boolean. Both belong to CheckBox.
   SELECTED: setter first = enabled choice ID (nonzero), second unused.
   Getter first = choice ID, second = presence flag (0/1), for SelectorBar.
   Choice snapshots preserve a surviving selection or select the first enabled item
   when no selection is supplied. Empty snapshots have no selection.
   BADGE_KIND is read-only: dot (0), count (1), icon (2).
   BADGE_COUNT and BADGE_ICON select their presentation when set.
   CheckBox emits CHANGE with CheckState; SelectorBar emits SELECTION with ID.
   HyperlinkButton emits CLICK only; it never launches a URI.
   MenuBar command actions use the existing CLICK/ACTION command events. */
enum {
    XUI_A_SELECT = 1, XUI_A_CHANGE_VALUE, XUI_A_STEP, XUI_A_TEXT_COMMAND,
    XUI_A_DISMISS, XUI_A_SHOW, XUI_A_ACCEPT, XUI_A_CANCEL, XUI_A_PLAY,
    XUI_A_PAUSE, XUI_A_STOP, XUI_A_UNLOAD, XUI_A_RELOAD, XUI_A_FOCUS,
    XUI_A_SELECT_ALL, XUI_A_COLLECTION_STEP, XUI_A_GRID_NAVIGATE, XUI_A_SET_DOT
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
/* Icon values match C++ ButtonIcon and C# ButtonIcon:
   none=0, back=1, forward=2, up=3, refresh=4, split=5, theme=6, add=7,
   minimize=8, maximize=9, restore=10, close=11, more=12, menu=13, home=14,
   folder=15, settings=16, search=17, library=18, history=19, bookmark=20, drive=21,
   open=22, save=23, save_as=24, undo=25, redo=26, chevron_up=27, chevron_down=28,
   folders_first=29, files_first=30, mixed=31.
   This range also applies to XUI_F_BUTTON_ICON, command records, and source visuals.
   Button icons do not change the accessible name or register command handlers. */
/* Optional parallel visual records. Existing navigation records remain unchanged. */
XUI_API xui_status XUI_CALL xui_navigation_items_visual(xui_handle target,
    const xui_navigation_entry* items, const xui_item_visual* visuals, uint32_t count) XUI_NOEXCEPT;
/* Navigation emits PREVIEW on hover changes (0 on exit), REQUEST after the tooltip delay.
   Help updates apply only to the currently hovered identity; stale updates return applied=0. */
XUI_API xui_status XUI_CALL xui_navigation_hover_help(xui_handle target,
    uint64_t id, xui_string text, uint32_t* applied) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_navigation_hover_delay(xui_handle target, uint32_t milliseconds) XUI_NOEXCEPT;
typedef struct xui_key_event {
    uint32_t size, virtual_key, modifiers, reserved; /* control=1, shift=2, alt=4; reserved bit 0: text-producing key outside an editor */
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
/* Shared asynchronous Shell thumbnail/icon lookup. Empty clears; replacement cancels old delivery.
   Error text is borrowed UTF-8, valid only for the UI-thread callback. Null revokes the callback. */
typedef xui_status (XUI_CALL *xui_window_icon_error_callback)(void*, const char*, uint32_t);
XUI_API xui_status XUI_CALL xui_window_set_icon_source(xui_handle window, xui_string path) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_window_on_icon_error(xui_handle window,
    xui_window_icon_error_callback callback, void* context) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_window_key_handler(xui_handle window,
    xui_key_handler callback, void* context) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_window_navigation_handler(xui_handle window,
    xui_navigation_handler callback, void* context) XUI_NOEXCEPT;
/* The only cross-thread window operation. On success callback runs exactly once:
   execute=1 on the UI thread, or execute=0 when discarded. Rejection does not call it.
   Callbacks must not throw. Close rejects further posts with XUI_CLOSED. */
XUI_API xui_status XUI_CALL xui_window_post(xui_handle window,
    xui_post_callback callback, void* context) XUI_NOEXCEPT;
/* Miller columns retain immutable sources. All handles must belong to one window.
   Set replaces the complete column path silently. Maximum depth is 32. */
typedef struct xui_miller_column {
    uint32_t size, has_selection;
    xui_string title;
    xui_handle source;
    uint64_t selected_id, selected_version;
} xui_miller_column;
typedef struct xui_miller_event {
    uint32_t size, kind, column, reserved;
    uint64_t id, version;
} xui_miller_event;
typedef xui_status (XUI_CALL *xui_miller_callback)(void*, const xui_miller_event*);
XUI_API xui_status XUI_CALL xui_miller_set_columns(xui_handle target,
    const xui_miller_column* columns, uint32_t count) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_miller_state(xui_handle target,
    uint32_t* count, uint32_t* active, double* width) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_miller_active(xui_handle target, uint32_t column) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_miller_width(xui_handle target, double width) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_miller_scroll_state(xui_handle target,
    double* offset, double* maximum) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_miller_scroll(xui_handle target, double offset) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_miller_subscribe(xui_handle target,
    xui_miller_callback callback, void* context) XUI_NOEXCEPT;
typedef struct xui_feature_options {
    uint32_t size, version;
    xui_string name;
    xui_handle content, second;
    uint32_t mode, reserved;
} xui_feature_options;
/* XUI_COMMAND_SURFACE mode: 0 creates a searchable palette; 1 creates a compact
   menu flyout without a title, editor, close button, or keyboard footer.
   Both modes accept checked commands with icons. Other mode values are invalid. */
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
/* Tab-only optional parallel visuals. A null visuals pointer creates text-only tabs.
   Choice version/flags keep their legacy tab behavior; the choice ABI does not change. */
XUI_API xui_status XUI_CALL xui_tab_items_visual(xui_handle target, const xui_choice* items,
    const xui_item_visual* visuals, uint32_t count, uint64_t selected, uint32_t has_selection) XUI_NOEXCEPT;
/* Borrowed child handles remain valid only while their window lives.
   Window indices: 0 = title tabs, 1 = leading button, 2 = secondary title tabs.
   Window: titlebar root 3, title 4, minimize 5, maximize 6, close 7.
   Window children require a custom title bar.
   NavigationView: search 0, toggle 1, items 2, header items 3, footer items 4, title 5, empty message 6.
   Titlebar roots and NavigationLists use XUI_RETAINED_ELEMENT with their actual style targets.
   Breadcrumb and CommandBar: overflow button 0. Dynamic buttons use keyed functions below.
   TabStrip: new-tab button 0, including while hidden.
   Secondary tabs and the leading button are hidden by default.
   ContentDialog: primary 0, cancel 1, title 2, validation 3, body 4, footer 5.
   CommandSurface: editor 0, title 1, status 2, close button 3, content 4, results 5, menu 6.
   LocationPicker: editor 0, navigation 1, content 2, footer 3, toolbar 4.
   ViewPicker: choices 0, size 1, content 2.
   NavigationPane: items 0, status 1, content 2, group 3, progress 4.
   SplitButton: primary 0, secondary 1.
   ComboBox: optional editor 0, popup 1, choices 2 (ChoiceList style target).
   A noneditable ComboBox returns XUI_OK and handle 0 for editor 0.
   NumericInput: editor 0, decrease button 1, increase button 2.
   InlineStatus: action button 0, dismiss button 1.
   ColorPicker: red 0, green 1, blue 2, alpha 3, swatch buttons at 4 + swatch index.
   An unavailable swatch index returns XUI_INVALID_ARGUMENT.
   Children preserve their native targets. CommandSurface menu is exposed as
   XUI_RETAINED_ELEMENT, not ItemsView. Repeated lookup returns the same handle.
   Facade root handles continue to style their real Popup, not a facade-specific target. */
XUI_API xui_status XUI_CALL xui_feature_child(xui_handle target, uint32_t index,
    xui_handle* result) XUI_NOEXCEPT;
/* Keyed Button handles retain actual native identity, not an ordinal.
   Missing keys return XUI_INVALID_ARGUMENT and clear result. Breadcrumb keys include version.
   CommandBar subscriptions survive snapshot refresh and preserve native command dispatch. */
XUI_API xui_status XUI_CALL xui_breadcrumb_segment_button(xui_handle target,
    uint64_t id, uint64_t version, xui_handle* result) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_command_bar_button(xui_handle target,
    uint64_t id, xui_handle* result) XUI_NOEXCEPT;
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
/* DataGrid, TabStrip, and virtual collections, including borrowed Miller column lists.
   Independent of the control event subscription. Request supplies items synchronously.
   TabStrip Request carries the right-clicked tab ID, or the selected tab ID for keyboard requests.
   Pointer requests do not select or activate tabs. Empty strip space has no tab menu.
   Tab replacement or reordering cancels pending actions.
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
