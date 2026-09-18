#![allow(non_camel_case_types)]
use std::ffi::c_void;
mod features;
pub use features::*;
pub type Handle = u64;
pub type Status = i32;
pub const ABI_VERSION: u32 = 0x10000;
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct TabColors {
    pub size: u32,
    pub version: u32,
    pub mask: u32,
    pub row_background: u32,
    pub selected_background: u32,
    pub selected_text: u32,
    pub inactive_background: u32,
    pub inactive_text: u32,
    pub hover_background: u32,
    pub border: u32,
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Text {
    pub data: *const u8,
    pub length: u32,
    pub reserved: u32,
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Options {
    pub size: u32,
    pub version: u32,
    pub title: Text,
    pub width: f32,
    pub height: f32,
    pub theme: u32,
    pub reserved: u32,
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Property {
    pub size: u32,
    pub property: u32,
    pub target: Handle,
    pub text: Text,
    pub a: f32,
    pub b: f32,
    pub c: f32,
    pub d: f32,
    pub integer: u64,
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Event {
    pub size: u32,
    pub kind: u32,
    pub source: Handle,
    pub value: u64,
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct FileItem {
    pub size: u32,
    pub directory: u32,
    pub id: u64,
    pub name: Text,
    pub path: Text,
}
pub type Callback = Option<unsafe extern "C" fn(*mut c_void, *const Event) -> Status>;
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct WindowPlacement {
    pub size: u32,
    pub x: i32,
    pub y: i32,
    pub width: i32,
    pub height: i32,
    pub maximized: u32,
}
pub const TAB_DRAG_REORDER: u32 = 0;
pub const TAB_DRAG_TEAR_OUT: u32 = 1;
pub const TAB_DRAG_DROP: u32 = 2;
pub const TAB_DRAG_CANCEL: u32 = 3;
pub const TAB_DRAG_COMPLETED: u32 = 4;
pub const TAB_DRAG_QUERY_DROP: u32 = 5;
pub const TAB_DRAG_JOIN: u32 = 6;
pub const TAB_DRAG_LEAVE: u32 = 7;
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct TabDragEvent {
    pub size: u32,
    pub kind: u32,
    pub source_strip: u32,
    pub target_strip: u32,
    pub tab_id: u64,
    pub target: Handle,
    pub index: u64,
}
pub type TabDragHandler = Option<unsafe extern "C" fn(*mut c_void, *const TabDragEvent, *mut u32) -> Status>;
unsafe extern "C" {
    pub fn xui_navigation_view_set_duration(target: Handle, milliseconds: u32) -> Status;
    pub fn xui_navigation_view_get_duration(target: Handle, milliseconds: *mut u32) -> Status;
    pub fn xui_navigation_view_get_animating(target: Handle, animating: *mut u32) -> Status;
    pub fn xui_progress_set_duration(target: Handle, milliseconds: u32) -> Status;
    pub fn xui_progress_get_duration(target: Handle, milliseconds: *mut u32) -> Status;
    pub fn xui_progress_get_presented_value(target: Handle, value: *mut f64) -> Status;
    pub fn xui_progress_get_animating(target: Handle, animating: *mut u32) -> Status;
    pub fn xui_expander_set_duration(target: Handle, milliseconds: u32) -> Status;
    pub fn xui_expander_get_duration(target: Handle, milliseconds: *mut u32) -> Status;
    pub fn xui_expander_get_progress(target: Handle, progress: *mut f32) -> Status;
    pub fn xui_expander_get_animating(target: Handle, animating: *mut u32) -> Status;
    pub fn xui_split_view_set_transition_duration(target: Handle, milliseconds: u32) -> Status;
    pub fn xui_split_view_get_transition_duration(target: Handle, milliseconds: *mut u32) -> Status;
    pub fn xui_split_view_get_progress(target: Handle, progress: *mut f32) -> Status;
    pub fn xui_split_view_get_animating(target: Handle, animating: *mut u32) -> Status;
    pub fn xui_reveal_create(window: Handle, content: Handle, name: Text, result: *mut Handle) -> Status;
    pub fn xui_reveal_set_open(target: Handle, open: u32) -> Status;
    pub fn xui_reveal_get_open(target: Handle, open: *mut u32) -> Status;
    pub fn xui_reveal_set_duration(target: Handle, milliseconds: u32) -> Status;
    pub fn xui_reveal_get_duration(target: Handle, milliseconds: *mut u32) -> Status;
    pub fn xui_reveal_set_layout(target: Handle, layout: u32) -> Status;
    pub fn xui_reveal_get_layout(target: Handle, layout: *mut u32) -> Status;
    pub fn xui_reveal_set_direction(target: Handle, direction: u32) -> Status;
    pub fn xui_reveal_get_direction(target: Handle, direction: *mut u32) -> Status;
    pub fn xui_reveal_get_progress(target: Handle, progress: *mut f32) -> Status;
    pub fn xui_reveal_get_animating(target: Handle, animating: *mut u32) -> Status;
    pub fn xui_application_create(application: *mut Handle) -> Status;
    pub fn xui_application_window_create(application: Handle, options: *const Options, custom_titlebar: u32, window: *mut Handle) -> Status;
    pub fn xui_application_show(application: Handle, window: Handle) -> Status;
    pub fn xui_application_run(application: Handle) -> Status;
    pub fn xui_application_shutdown(application: Handle) -> Status;
    pub fn xui_application_destroy(application: Handle) -> Status;
    pub fn xui_application_post(application: Handle, callback: Option<unsafe extern "C" fn(*mut c_void, u32) -> Status>, context: *mut c_void) -> Status;
    pub fn xui_window_state(window: Handle, state: *mut u32) -> Status;
    pub fn xui_window_closed(window: Handle, callback: Callback, context: *mut c_void) -> Status;
    pub fn xui_window_error(window: Handle, buffer: *mut u8, capacity: u32, required: *mut u32) -> Status;
    pub fn xui_window_get_placement(window: Handle, placement: *mut WindowPlacement) -> Status;
    pub fn xui_window_set_placement(window: Handle, placement: *const WindowPlacement) -> Status;
    pub fn xui_window_tab_drag_handler(window: Handle, callback: TabDragHandler, context: *mut c_void) -> Status;
    pub fn xui_tab_set_colors(tabs: Handle, colors: *const TabColors) -> Status;
    pub fn xui_tab_get_colors(tabs: Handle, colors: *mut TabColors) -> Status;
    pub fn xui_tab_set_new_button(tabs: Handle, visible: u32) -> Status;
    pub fn xui_tab_set_duration(tabs: Handle, milliseconds: u32) -> Status;
    pub fn xui_tab_get_duration(tabs: Handle, milliseconds: *mut u32) -> Status;
    pub fn xui_tab_get_new_button(tabs: Handle, visible: *mut u32) -> Status;
    pub fn xui_split_set_first_visible(split: Handle, visible: u32) -> Status;
    pub fn xui_split_get_first_visible(split: Handle, visible: *mut u32) -> Status;
    pub fn xui_abi_version() -> u32;
    pub fn xui_error_copy(
        buffer: *mut u8,
        capacity: u32,
        required: *mut u32,
        status: *mut Status,
    ) -> Status;
    pub fn xui_window_create(options: *const Options, window: *mut Handle) -> Status;
    pub fn xui_window_destroy(window: Handle) -> Status;
    pub fn xui_window_run(window: Handle) -> Status;
    pub fn xui_window_close(window: Handle) -> Status;
    pub fn xui_window_file_type_icon(window: Handle, extension: Text, directory: u32) -> Status;
    pub fn xui_window_callback_error(window: Handle, status: *mut Status) -> Status;
    pub fn xui_create(
        window: Handle,
        kind: u32,
        name: Text,
        content: Handle,
        result: *mut Handle,
    ) -> Status;
    pub fn xui_stack_create(window: Handle, axis: u32, result: *mut Handle) -> Status;
    pub fn xui_stack_add(stack: Handle, child: Handle, flex: f32) -> Status;
    pub fn xui_window_content(window: Handle, stack: Handle) -> Status;
    pub fn xui_update(window: Handle, properties: *const Property, count: u32) -> Status;
    pub fn xui_subscribe(target: Handle, callback: Callback, context: *mut c_void) -> Status;
    pub fn xui_text_copy(
        target: Handle,
        buffer: *mut u8,
        capacity: u32,
        required: *mut u32,
    ) -> Status;
    pub fn xui_focus(target: Handle, select_all: u32) -> Status;
    pub fn xui_invoke(target: Handle) -> Status;
    pub fn xui_image_source(image: Handle, path: Text, width: u32, height: u32) -> Status;
    pub fn xui_image_shell_source(image: Handle, path: Text, width: u32, height: u32) -> Status;
    pub fn xui_image_state(image: Handle, state: *mut u32) -> Status;
    pub fn xui_list_items(list: Handle, items: *const FileItem, count: u32) -> Status;
    pub fn xui_list_filter(list: Handle, query: Text) -> Status;
    pub fn xui_list_select(list: Handle, index: u32) -> Status;
    pub fn xui_list_state(
        list: Handle,
        count: *mut u32,
        id: *mut u64,
        has_selection: *mut u32,
    ) -> Status;
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn layouts() {
        assert_eq!(size_of::<Text>(), 16);
        assert_eq!(size_of::<Options>(), 40);
        assert_eq!(size_of::<Property>(), 56);
        assert_eq!(size_of::<Event>(), 24);
        assert_eq!(size_of::<FileItem>(), 48);
        assert_eq!(size_of::<WindowPlacement>(), 24);
        assert_eq!(size_of::<TabDragEvent>(), 40);
        assert_eq!([TAB_DRAG_REORDER, TAB_DRAG_TEAR_OUT, TAB_DRAG_DROP, TAB_DRAG_CANCEL,
            TAB_DRAG_COMPLETED, TAB_DRAG_QUERY_DROP, TAB_DRAG_JOIN, TAB_DRAG_LEAVE], [0, 1, 2, 3, 4, 5, 6, 7]);
        assert_eq!(std::mem::offset_of!(WindowPlacement, maximized), 20);
        assert_eq!(std::mem::offset_of!(TabDragEvent, tab_id), 16);
        assert_eq!(std::mem::offset_of!(TabDragEvent, target), 24);
        assert_eq!(std::mem::offset_of!(TabDragEvent, index), 32);
        assert_eq!(std::mem::offset_of!(Property, integer), 48);
        assert_eq!(unsafe { xui_abi_version() }, ABI_VERSION);
    }
}
