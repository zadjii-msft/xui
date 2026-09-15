#ifndef XUI_LAYOUT_H
#define XUI_LAYOUT_H
#include "xui.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef enum xui_visual_style {
    XUI_STYLE_CLASSIC = 0,
    XUI_STYLE_WINUI = 1
} xui_visual_style;
/* Visual style is independent of the light/dark/high-contrast theme.
   Existing window creation functions continue to default to classic. */
XUI_API xui_status XUI_CALL xui_window_visual_style_set(xui_handle window, uint32_t style) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_window_visual_style_get(xui_handle window, uint32_t* style) XUI_NOEXCEPT;
/* Pane bounds determine the tab bands after each content layout. The OS window
   title is independent of show_title. Pane handles must belong to the window. */
XUI_API xui_status XUI_CALL xui_window_titlebar_layout(xui_handle window,
    xui_handle first_pane, xui_handle second_pane, uint32_t show_title) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_navigation_header(xui_handle navigation, uint32_t visible) XUI_NOEXCEPT;
/* 0=below, 1=above, 2=right, 3=left, 4=center in the visible client viewport. */
XUI_API xui_status XUI_CALL xui_popup_placement(xui_handle popup, uint32_t placement) XUI_NOEXCEPT;
/* Use the window background for the popup frame and its unstyled children. */
XUI_API xui_status XUI_CALL xui_popup_window_background(xui_handle popup, uint32_t enabled) XUI_NOEXCEPT;
/* Render ItemsView secondary text as trailing '+'-separated shortcut keycaps. */
XUI_API xui_status XUI_CALL xui_items_trailing_shortcut_badges(xui_handle items, uint32_t enabled) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_text_input_caption(xui_handle input, uint32_t visible) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_text_input_placeholder(xui_handle input, xui_string text) XUI_NOEXCEPT;
/* Last arranged client coordinates, in DIPs. */
XUI_API xui_status XUI_CALL xui_element_bounds(xui_handle element,
    float* x, float* y, float* width, float* height) XUI_NOEXCEPT;
#ifdef __cplusplus
}
#endif
#endif
