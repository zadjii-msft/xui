#include <xui/xui.h>

int main(void) {
    return xui_abi_version() == XUI_ABI_VERSION ? 0 : 1;
}
