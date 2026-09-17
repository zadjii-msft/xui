use super::*;
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct PreviewStatus {
    pub size: u32,
    pub version: u32,
    pub generation: u64,
    pub state: u32,
    pub reason: u32,
    pub phase: u32,
    pub hresult: i32,
    pub cleanup: u32,
    pub reserved: u32,
}
unsafe extern "C" {
    pub fn xui_shell_preview_create(window: Handle, name: Text, control: *mut Handle) -> Status;
    pub fn xui_shell_preview_load_local(
        control: Handle,
        path: Text,
        generation: *mut u64,
    ) -> Status;
    pub fn xui_shell_preview_cancel(control: Handle, generation: u64) -> Status;
    pub fn xui_shell_preview_unload(control: Handle) -> Status;
    pub fn xui_shell_preview_focus_content(control: Handle, reverse: u32) -> Status;
    pub fn xui_shell_preview_get_status(control: Handle, status: *mut PreviewStatus) -> Status;
}
