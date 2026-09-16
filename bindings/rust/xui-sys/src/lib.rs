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
unsafe extern "C" {
    pub fn xui_tab_set_colors(tabs: Handle, colors: *const TabColors) -> Status;
    pub fn xui_tab_get_colors(tabs: Handle, colors: *mut TabColors) -> Status;
    pub fn xui_tab_set_new_button(tabs: Handle, visible: u32) -> Status;
    pub fn xui_tab_get_new_button(tabs: Handle, visible: *mut u32) -> Status;
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
        assert_eq!(std::mem::offset_of!(Property, integer), 48);
        assert_eq!(unsafe { xui_abi_version() }, ABI_VERSION);
    }
}
