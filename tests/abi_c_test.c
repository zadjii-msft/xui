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
int main(void) {
    xui_handle window = 0;
    xui_window_options options = {0};
    options.size = sizeof(options);
    options.version = XUI_ABI_VERSION;
    options.width = 100;
    options.height = 100;
    if (xui_abi_version() != XUI_ABI_VERSION) return 1;
    if (xui_window_create(&options, &window) != XUI_OK) return 2;
    if (xui_window_destroy(window) != XUI_OK) return 3;
    puts("C header, layout, import and lifecycle passed.");
    return 0;
}
