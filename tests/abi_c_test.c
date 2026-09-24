#include "xui/xui.h"
#include "xui/xui_presentation.h"
#include "xui/xui_retained_pages.h"
#include "xui/xui_label_layout.h"
#include "xui/xui_reveal_portable.h"
#include "xui/xui_image_memory.h"
#include <stddef.h>
#include <stdio.h>

_Static_assert(sizeof(xui_string) == 16, "string ABI");
_Static_assert(sizeof(xui_window_options) == 40, "options ABI");
_Static_assert(sizeof(xui_property) == 56, "property ABI");
_Static_assert(sizeof(xui_event) == 24, "event ABI");
_Static_assert(sizeof(xui_file_item) == 48, "item ABI");
_Static_assert(sizeof(xui_window_placement) == 24, "window placement ABI");
_Static_assert(offsetof(xui_window_placement, maximized) == 20, "placement maximized offset");
_Static_assert(sizeof(xui_tab_drag_event) == 40, "tab drag event ABI");
_Static_assert(offsetof(xui_tab_drag_event, tab_id) == 16, "tab drag ID offset");
_Static_assert(offsetof(xui_tab_drag_event, target) == 24, "tab drag target offset");
_Static_assert(offsetof(xui_tab_drag_event, index) == 32, "tab drag index offset");
_Static_assert(XUI_TAB_DRAG_REORDER == 0 && XUI_TAB_DRAG_TEAR_OUT == 1 && XUI_TAB_DRAG_DROP == 2 &&
    XUI_TAB_DRAG_CANCEL == 3 && XUI_TAB_DRAG_COMPLETED == 4 && XUI_TAB_DRAG_QUERY_DROP == 5 &&
    XUI_TAB_DRAG_JOIN == 6 && XUI_TAB_DRAG_LEAVE == 7, "tab drag kind ABI");
_Static_assert(offsetof(xui_property, integer) == 48, "property offset");
_Static_assert(offsetof(xui_string, length) == 8, "string offset");
_Static_assert(sizeof(xui_style_property) == 80, "generic style property ABI");
_Static_assert(offsetof(xui_style_property, state) == 24, "generic style state offset");
_Static_assert(offsetof(xui_style_property, number) == 56, "generic style number offset");
_Static_assert(offsetof(xui_style_property, text) == 64, "generic style text offset");
_Static_assert(sizeof(xui_control_style_options) == 40, "generic style options ABI");
_Static_assert(XUI_BUTTON_ICON_DRIVE == 21 && XUI_BUTTON_ICON_OPEN == 22, "button icon ABI");
_Static_assert(sizeof(xui_window_theme_options) == 40, "window theme ABI");
_Static_assert(sizeof(xui_content_viewport) == 16, "content viewport ABI");
_Static_assert(sizeof(xui_page_entry) == 32, "retained page entry ABI");
_Static_assert(sizeof(xui_label_layout) == 24, "Label layout ABI");
_Static_assert(sizeof(xui_portable_reveal_state) == 24, "portable Reveal state ABI");
_Static_assert(sizeof(xui_portable_reveal_presentation) == 16, "portable Reveal presentation ABI");
_Static_assert(sizeof(xui_image_memory_options) == 48, "memory image options ABI");
_Static_assert(sizeof(xui_image_memory_state) == 40, "memory image state ABI");
_Static_assert(sizeof(xui_image_memory_statistics) == 32, "memory image statistics ABI");
_Static_assert(offsetof(xui_window_theme_options, foreground) == 16, "theme foreground offset");
_Static_assert(offsetof(xui_window_theme_options, background) == 24, "theme background offset");
_Static_assert(offsetof(xui_window_theme_options, accent) == 32, "theme accent offset");
int main(void) {
    xui_handle window = 0;
    xui_window_options options = {0};
    options.size = sizeof(options);
    options.version = XUI_ABI_VERSION;
    options.width = 100;
    options.height = 100;
    if (xui_abi_version() != XUI_ABI_VERSION) return 1;
    if (xui_combo_box_selection_version() != XUI_COMBO_SELECTION_VERSION) return 8;
    if (xui_window_create(&options, &window) != XUI_OK) return 2;
    xui_window_theme_options theme = {0};
    theme.size = sizeof(theme); theme.version = XUI_WINDOW_THEME_VERSION;
    if (xui_window_theme_version() != XUI_WINDOW_THEME_VERSION ||
        xui_window_theme_get(window, &theme) != XUI_OK ||
        xui_window_theme_set(window, &theme) != XUI_OK) return 7;
    xui_handle image = 0;
    xui_string name = {"Preview", 7, 0}, path = {".", 1, 0};
    if (xui_create(window, XUI_IMAGE, name, 0, &image) != XUI_OK) return 4;
    if (xui_image_shell_source(image, path, 160, 160) != XUI_OK) return 5;
    path.length = 0;
    if (xui_image_shell_source(image, path, 160, 160) != XUI_OK) return 6;
    if (xui_window_destroy(window) != XUI_OK) return 3;
    puts("C header, layout, import and lifecycle passed.");
    return 0;
}
