#include "xui/xui.h"
#include <stddef.h>
#include <stdio.h>

_Static_assert(sizeof(xui_string) == 16, "string ABI");
_Static_assert(sizeof(xui_window_options) == 40, "options ABI");
_Static_assert(sizeof(xui_property) == 56, "property ABI");
_Static_assert(sizeof(xui_event) == 24, "event ABI");
_Static_assert(sizeof(xui_file_item) == 48, "item ABI");
_Static_assert(offsetof(xui_property, integer) == 48, "property offset");
_Static_assert(offsetof(xui_string, length) == 8, "string offset");
_Static_assert(sizeof(xui_style_property) == 80, "generic style property ABI");
_Static_assert(offsetof(xui_style_property, state) == 24, "generic style state offset");
_Static_assert(offsetof(xui_style_property, number) == 56, "generic style number offset");
_Static_assert(offsetof(xui_style_property, text) == 64, "generic style text offset");
_Static_assert(sizeof(xui_control_style_options) == 40, "generic style options ABI");
_Static_assert(XUI_BUTTON_ICON_DRIVE == 21 && XUI_BUTTON_ICON_OPEN == 22, "button icon ABI");
int main(void) {
    xui_handle window = 0;
    xui_window_options options = {0};
    options.size = sizeof(options);
    options.version = XUI_ABI_VERSION;
    options.width = 100;
    options.height = 100;
    if (xui_abi_version() != XUI_ABI_VERSION) return 1;
    if (xui_window_create(&options, &window) != XUI_OK) return 2;
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
