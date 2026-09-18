#ifndef XUI_LAYOUT_H
#define XUI_LAYOUT_H
#include "xui.h"
#ifdef __cplusplus
extern "C" {
#endif
#define XUI_TAB_COLORS_VERSION 0x00010000u
enum {
    XUI_TAB_ROW_BACKGROUND = 1u, XUI_TAB_SELECTED_BACKGROUND = 2u, XUI_TAB_SELECTED_TEXT = 4u,
    XUI_TAB_INACTIVE_BACKGROUND = 8u, XUI_TAB_INACTIVE_TEXT = 16u,
    XUI_TAB_HOVER_BACKGROUND = 32u, XUI_TAB_BORDER = 64u
};
/* Each mask bit enables the corresponding 0xRRGGBB value. Other values must be zero.
   A zero mask resets all colors to the theme. High contrast ignores overrides. */
typedef struct xui_tab_colors {
    uint32_t size, version, mask;
    uint32_t row_background, selected_background, selected_text;
    uint32_t inactive_background, inactive_text, hover_background, border;
} xui_tab_colors;
XUI_API xui_status XUI_CALL xui_tab_set_colors(xui_handle tabs, const xui_tab_colors* colors) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_tab_get_colors(xui_handle tabs, xui_tab_colors* colors) XUI_NOEXCEPT;
/* Opt-in native "New tab" button. Activation emits XUI_ACTION with id zero.
   visible is 0 or 1. The default is 0. */
XUI_API xui_status XUI_CALL xui_tab_set_new_button(xui_handle tabs, uint32_t visible) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_tab_get_new_button(xui_handle tabs, uint32_t* visible) XUI_NOEXCEPT;
typedef enum xui_visual_style {
    XUI_STYLE_CLASSIC = 0,
    XUI_STYLE_WINUI = 1
} xui_visual_style;
/* Visual style is independent of the light/dark/high-contrast theme.
   Existing window creation functions continue to default to classic. */
XUI_API xui_status XUI_CALL xui_window_visual_style_set(xui_handle window, uint32_t style) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_window_visual_style_get(xui_handle window, uint32_t* style) XUI_NOEXCEPT;
/* Before run only. Zero shows without activation or initial keyboard focus.
   The default is one. Does not prevent subsequent user activation. */
XUI_API xui_status XUI_CALL xui_window_show_activated(xui_handle window, uint32_t activated) XUI_NOEXCEPT;
/* Pane bounds determine the tab bands after each content layout. The OS window
   title is independent of show_title. Pane handles must belong to the window. */
XUI_API xui_status XUI_CALL xui_window_titlebar_layout(xui_handle window,
    xui_handle first_pane, xui_handle second_pane, uint32_t show_title) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_navigation_header(xui_handle navigation, uint32_t visible) XUI_NOEXCEPT;
/* 0=below, 1=above, 2=right, 3=left, 4=center in the visible client viewport,
   5=below and horizontally centered on the anchor (flips above if necessary). */
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
