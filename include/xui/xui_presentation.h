#ifndef XUI_PRESENTATION_H
#define XUI_PRESENTATION_H
#include "xui.h"
#ifdef __cplusplus
extern "C" {
#endif
#define XUI_WINDOW_THEME_VERSION 0x00010000u
enum { XUI_THEME_FOREGROUND = 1u, XUI_THEME_BACKGROUND = 2u, XUI_THEME_ACCENT = 4u };
typedef struct xui_window_theme_options {
    uint32_t size, version, theme, mask;
    xui_theme_color foreground, background, accent;
} xui_window_theme_options;
/* Requested native mode: Dark=0, Light=1, HighContrast=2, System=3.
   Mask bits distinguish an unset role from explicit RGB black. Unset pairs must
   be zero. Both light/dark values are RGB24. Validation precedes all mutation.
   Semantic foreground/accent remain below authored styles and native state
   feedback; high contrast ignores all optional RGB resources.
   Background changes the owned window canvas, not every control surface/field.
   Capture/restore this exact requested snapshot, never resolved System colors. */
XUI_API uint32_t XUI_CALL xui_window_theme_version(void) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_window_theme_get(xui_handle window, xui_window_theme_options* options) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_window_theme_set(xui_handle window, const xui_window_theme_options* options) XUI_NOEXCEPT;
#ifdef __cplusplus
}
#endif
#endif
