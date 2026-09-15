use std::{
    any::Any,
    cell::RefCell,
    collections::HashMap,
    ffi::c_void,
    fmt,
    panic::{AssertUnwindSafe, catch_unwind},
    rc::{Rc, Weak},
};
use xui_sys as sys;
mod features;
mod features_generated;
pub use features::*;
pub use features_generated::*;
mod styling;
pub use styling::*;
#[cfg(test)]
mod feature_tests;

#[derive(Debug, Clone)]
pub struct Error {
    pub status: i32,
    pub message: String,
}
impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "XUI {}: {}", self.status, self.message)
    }
}
impl std::error::Error for Error {}
pub type Result<T> = std::result::Result<T, Error>;
fn invalid(message: &str) -> Error {
    Error {
        status: 1,
        message: message.into(),
    }
}
fn check(status: i32) -> Result<()> {
    if status == 0 {
        return Ok(());
    }
    let mut bytes = [0u8; 1024];
    let mut count = 0;
    let mut code = 0;
    unsafe {
        sys::xui_error_copy(bytes.as_mut_ptr(), 1024, &mut count, &mut code);
    }
    Err(Error {
        status,
        message: String::from_utf8_lossy(&bytes[..count.min(1024) as usize]).into_owned(),
    })
}
fn text(s: &str) -> Result<sys::Text> {
    if s.len() > 1048576 || s.contains('\0') {
        return Err(invalid("Invalid UTF-8 string length or embedded NUL."));
    }
    Ok(sys::Text {
        data: s.as_ptr(),
        length: s.len() as u32,
        reserved: 0,
    })
}
struct Inner {
    handle: u64,
    subscriptions: RefCell<HashMap<u64, Rc<Slot>>>,
    callback_error: RefCell<Option<Error>>,
}
impl Inner {
    fn check(&self, status: i32) -> Result<()> {
        if status == 8
            && let Some(error) = self.callback_error.borrow().as_ref()
        {
            return Err(error.clone());
        }
        check(status)
    }
}
impl Drop for Inner {
    fn drop(&mut self) {
        // Rc makes the last owner UI-thread-only; run and each trampoline retain an owner.
        let status = unsafe { sys::xui_window_destroy(self.handle) };
        if status != 0 {
            // Never free callback contexts while native code can still call them.
            let slots = std::mem::take(self.subscriptions.get_mut());
            std::mem::forget(slots);
        }
    }
}
/// UI-thread-owned window. Elements retain its arena until their last owner drops.
///
/// ```compile_fail
/// fn requires_send<T: Send>() {}
/// requires_send::<xui::Window>();
/// ```
/// ```compile_fail
/// fn requires_sync<T: Sync>() {}
/// requires_sync::<xui::Element>();
/// ```
#[derive(Clone)]
pub struct Window(Rc<Inner>);
pub struct WeakWindow(Weak<Inner>);
impl WeakWindow {
    pub fn upgrade(&self) -> Option<Window> {
        self.0.upgrade().map(Window)
    }
}
#[derive(Clone, Copy)]
#[repr(u32)]
pub enum Axis {
    Horizontal,
    Vertical,
}
#[derive(Clone, Copy)]
#[repr(u32)]
pub enum Theme {
    Dark,
    Light,
    HighContrast,
}
#[derive(Clone, Copy, Debug)]
pub struct Event {
    pub kind: u32,
    pub value: u64,
}
impl Window {
    pub fn new(title: &str, width: f32, height: f32) -> Result<Self> {
        if unsafe { sys::xui_abi_version() } != sys::ABI_VERSION {
            return Err(Error {
                status: 5,
                message: "ABI version mismatch.".into(),
            });
        }
        let options = sys::Options {
            size: size_of::<sys::Options>() as u32,
            version: sys::ABI_VERSION,
            title: text(title)?,
            width,
            height,
            ..Default::default()
        };
        let mut handle = 0;
        check(unsafe { sys::xui_window_create(&options, &mut handle) })?;
        Ok(Self(Rc::new(Inner {
            handle,
            subscriptions: RefCell::new(HashMap::new()),
            callback_error: RefCell::new(None),
        })))
    }
    pub fn downgrade(&self) -> WeakWindow {
        WeakWindow(Rc::downgrade(&self.0))
    }
    pub fn stack(&self, axis: Axis) -> Result<Stack> {
        let mut handle = 0;
        check(unsafe { sys::xui_stack_create(self.0.handle, axis as u32, &mut handle) })?;
        Ok(Stack(Element {
            owner: self.0.clone(),
            handle,
        }))
    }
    fn create(&self, kind: u32, name: &str, content: Option<&Element>) -> Result<Element> {
        if let Some(c) = content {
            c.belongs(&self.0)?;
        }
        let mut handle = 0;
        check(unsafe {
            sys::xui_create(
                self.0.handle,
                kind,
                text(name)?,
                content.map_or(0, |c| c.handle),
                &mut handle,
            )
        })?;
        Ok(Element {
            owner: self.0.clone(),
            handle,
        })
    }
    pub fn label(&self, text: &str) -> Result<Label> {
        self.create(3, text, None).map(Label)
    }
    pub fn button(&self, text: &str) -> Result<Button> {
        self.create(4, text, None).map(Button)
    }
    pub fn toggle(&self, text: &str) -> Result<Toggle> {
        self.create(5, text, None).map(Toggle)
    }
    pub fn text_input(&self, name: &str) -> Result<TextInput> {
        self.create(6, name, None).map(TextInput)
    }
    pub fn scroll_view(&self, content: &Element, name: &str) -> Result<ScrollView> {
        self.create(7, name, Some(content)).map(ScrollView)
    }
    pub fn image(&self, name: &str) -> Result<Image> {
        self.create(8, name, None).map(Image)
    }
    pub fn file_list(&self, name: &str) -> Result<FileList> {
        self.create(9, name, None).map(FileList)
    }
    pub fn set_content(&self, root: &Stack) -> Result<()> {
        root.belongs(&self.0)?;
        check(unsafe { sys::xui_window_content(self.0.handle, root.handle) })
    }
    pub fn run(&self) -> Result<()> {
        self.0.check(unsafe { sys::xui_window_run(self.0.handle) })
    }
    pub fn close(&self) -> Result<()> {
        check(unsafe { sys::xui_window_close(self.0.handle) })
    }
    pub fn callback_status(&self) -> Result<i32> {
        let mut status = 0;
        check(unsafe { sys::xui_window_callback_error(self.0.handle, &mut status) })?;
        Ok(status)
    }
    pub fn set_theme(&self, value: Theme) -> Result<()> {
        let p = sys::Property {
            size: size_of::<sys::Property>() as u32,
            property: 13,
            target: self.0.handle,
            integer: value as u64,
            ..Default::default()
        };
        check(unsafe { sys::xui_update(self.0.handle, &p, 1) })
    }
    pub fn update(&self, properties: &[Property<'_>]) -> Result<()> {
        if properties.len() > 4096 {
            return Err(invalid("A batch supports at most 4096 properties."));
        }
        let raw: Result<Vec<_>> = properties
            .iter()
            .map(|p| {
                p.target.belongs(&self.0)?;
                Ok(sys::Property {
                    size: size_of::<sys::Property>() as u32,
                    property: p.kind as u32,
                    target: p.target.handle,
                    text: text(p.text)?,
                    a: p.a,
                    b: p.b,
                    c: p.c,
                    d: p.d,
                    integer: p.integer,
                })
            })
            .collect();
        let raw = raw?;
        check(unsafe { sys::xui_update(self.0.handle, raw.as_ptr(), raw.len() as u32) })
    }
    pub fn on_key(&self, action: impl FnMut(Event) -> Result<()> + 'static) -> Result<()> {
        subscribe(&self.0, self.0.handle, action)
    }
    pub fn unsubscribe_key(&self) -> Result<()> {
        unsubscribe(&self.0, self.0.handle)
    }
}
#[derive(Clone)]
pub struct Element {
    owner: Rc<Inner>,
    handle: u64,
}
pub struct WeakElement {
    owner: Weak<Inner>,
    handle: u64,
}
impl WeakElement {
    pub fn upgrade(&self) -> Option<Element> {
        self.owner.upgrade().map(|owner| Element {
            owner,
            handle: self.handle,
        })
    }
}
#[derive(Clone, Copy)]
#[repr(u32)]
pub enum PropertyKind {
    Text = 1,
    Name,
    Enabled,
    Checked,
    FixedSize,
    MinimumSize,
    MaximumSize,
    AutoSize,
    Spacing,
    Padding,
    ScrollOffset,
    AutomationId,
    Theme,
    PreferredSize,
}
#[derive(Clone, Copy)]
pub struct Property<'a> {
    pub target: &'a Element,
    pub kind: PropertyKind,
    pub text: &'a str,
    pub a: f32,
    pub b: f32,
    pub c: f32,
    pub d: f32,
    pub integer: u64,
}
impl<'a> Property<'a> {
    pub fn new(target: &'a Element, kind: PropertyKind) -> Self {
        Self {
            target,
            kind,
            text: "",
            a: 0.,
            b: 0.,
            c: 0.,
            d: 0.,
            integer: 0,
        }
    }
    pub fn text(target: &'a Element, value: &'a str) -> Self {
        Self {
            text: value,
            ..Self::new(target, PropertyKind::Text)
        }
    }
}
impl Element {
    fn belongs(&self, owner: &Rc<Inner>) -> Result<()> {
        if !Rc::ptr_eq(&self.owner, owner) {
            return Err(invalid("Elements belong to different windows."));
        }
        Ok(())
    }
    pub fn downgrade(&self) -> WeakElement {
        WeakElement {
            owner: Rc::downgrade(&self.owner),
            handle: self.handle,
        }
    }
    pub fn set_text(&self, value: &str) -> Result<()> {
        Window(self.owner.clone()).update(&[Property::text(self, value)])
    }
    pub fn text(&self) -> Result<String> {
        let mut count = 0;
        let status =
            unsafe { sys::xui_text_copy(self.handle, std::ptr::null_mut(), 0, &mut count) };
        if status != 6 {
            check(status)?;
        }
        let mut bytes = vec![0; count as usize];
        check(unsafe { sys::xui_text_copy(self.handle, bytes.as_mut_ptr(), count, &mut count) })?;
        String::from_utf8(bytes).map_err(|_| invalid("Native output is not UTF-8."))
    }
    fn string_property(&self, kind: PropertyKind, value: &str) -> Result<()> {
        Window(self.owner.clone()).update(&[Property {
            text: value,
            ..Property::new(self, kind)
        }])
    }
    pub fn automation_id(&self, value: &str) -> Result<()> {
        self.string_property(PropertyKind::AutomationId, value)
    }
    pub fn set_name(&self, value: &str) -> Result<()> {
        self.string_property(PropertyKind::Name, value)
    }
    pub fn enabled(&self, value: bool) -> Result<()> {
        self.boolean(PropertyKind::Enabled, value)
    }
    pub fn auto_size(&self, value: bool) -> Result<()> {
        self.boolean(PropertyKind::AutoSize, value)
    }
    fn boolean(&self, kind: PropertyKind, value: bool) -> Result<()> {
        Window(self.owner.clone()).update(&[Property {
            integer: value as u64,
            ..Property::new(self, kind)
        }])
    }
    fn dimensions(&self, kind: PropertyKind, width: f32, height: f32) -> Result<()> {
        Window(self.owner.clone()).update(&[Property {
            a: width,
            b: height,
            ..Property::new(self, kind)
        }])
    }
    pub fn fixed_size(&self, w: f32, h: f32) -> Result<()> {
        self.dimensions(PropertyKind::FixedSize, w, h)
    }
    pub fn preferred_size(&self, w: f32, h: f32) -> Result<()> {
        self.dimensions(PropertyKind::PreferredSize, w, h)
    }
    pub fn minimum_size(&self, w: f32, h: f32) -> Result<()> {
        self.dimensions(PropertyKind::MinimumSize, w, h)
    }
    pub fn maximum_size(&self, w: f32, h: f32) -> Result<()> {
        self.dimensions(PropertyKind::MaximumSize, w, h)
    }
    pub fn focus(&self, select_all: bool) -> Result<()> {
        check(unsafe { sys::xui_focus(self.handle, select_all as u32) })
    }
    pub fn on_event(&self, action: impl FnMut(Event) -> Result<()> + 'static) -> Result<()> {
        subscribe(&self.owner, self.handle, action)
    }
    pub fn unsubscribe(&self) -> Result<()> {
        unsubscribe(&self.owner, self.handle)
    }
    pub fn image_status(&self) -> Result<u32> {
        let mut status = 0;
        check(unsafe { sys::xui_image_state(self.handle, &mut status) })?;
        Ok(status)
    }
    pub fn image_source(&self, path: &str, width: u32, height: u32) -> Result<()> {
        check(unsafe { sys::xui_image_source(self.handle, text(path)?, width, height) })
    }
}
macro_rules! control {
    ($($name:ident),*) => {$(
        #[derive(Clone)]
        pub struct $name(Element);
        impl std::ops::Deref for $name { type Target = Element; fn deref(&self) -> &Element { &self.0 } }
    )*};
}
control!(
    Stack, Label, Button, Toggle, TextInput, ScrollView, Image, FileList
);
impl Stack {
    pub fn add(&self, child: &Element, flex: f32) -> Result<()> {
        child.belongs(&self.owner)?;
        check(unsafe { sys::xui_stack_add(self.handle, child.handle, flex) })
    }
    pub fn spacing(&self, value: f32) -> Result<()> {
        self.dimensions(PropertyKind::Spacing, value, 0.)
    }
    pub fn padding(&self, value: f32) -> Result<()> {
        Window(self.owner.clone()).update(&[Property {
            a: value,
            b: value,
            c: value,
            d: value,
            ..Property::new(self, PropertyKind::Padding)
        }])
    }
}
impl Button {
    pub fn invoke(&self) -> Result<()> {
        self.owner.check(unsafe { sys::xui_invoke(self.handle) })
    }
}
impl Toggle {
    pub fn checked(&self, value: bool) -> Result<()> {
        self.boolean(PropertyKind::Checked, value)
    }
    pub fn invoke(&self) -> Result<()> {
        self.owner.check(unsafe { sys::xui_invoke(self.handle) })
    }
}
impl ScrollView {
    pub fn offset(&self, value: f32) -> Result<()> {
        self.dimensions(PropertyKind::ScrollOffset, value, 0.)
    }
}
impl Image {
    pub fn source(&self, path: &str, width: u32, height: u32) -> Result<()> {
        check(unsafe { sys::xui_image_source(self.handle, text(path)?, width, height) })
    }
    pub fn unload(&self) -> Result<()> {
        self.source("", 192, 144)
    }
    pub fn status(&self) -> Result<u32> {
        let mut status = 0;
        check(unsafe { sys::xui_image_state(self.handle, &mut status) })?;
        Ok(status)
    }
}
pub struct FileItem<'a> {
    pub id: u64,
    pub name: &'a str,
    pub path: &'a str,
    pub directory: bool,
}
impl FileList {
    pub fn set_items(&self, items: &[FileItem<'_>]) -> Result<()> {
        if items.len() > 1000000 {
            return Err(invalid("Too many file items."));
        }
        let raw: Result<Vec<_>> = items
            .iter()
            .map(|item| {
                Ok(sys::FileItem {
                    size: size_of::<sys::FileItem>() as u32,
                    directory: item.directory as u32,
                    id: item.id,
                    name: text(item.name)?,
                    path: text(item.path)?,
                })
            })
            .collect();
        let raw = raw?;
        self.owner
            .check(unsafe { sys::xui_list_items(self.handle, raw.as_ptr(), raw.len() as u32) })
    }
    pub fn filter(&self, query: &str) -> Result<()> {
        self.owner
            .check(unsafe { sys::xui_list_filter(self.handle, text(query)?) })
    }
    pub fn select(&self, index: Option<u32>) -> Result<()> {
        self.owner
            .check(unsafe { sys::xui_list_select(self.handle, index.unwrap_or(u32::MAX)) })
    }
    pub fn state(&self) -> Result<(u32, Option<u64>)> {
        let (mut count, mut id, mut selected) = (0, 0, 0);
        check(unsafe { sys::xui_list_state(self.handle, &mut count, &mut id, &mut selected) })?;
        Ok((count, (selected != 0).then_some(id)))
    }
}
type Action = Box<dyn FnMut(Event) -> Result<()>>;
struct Slot {
    owner: Weak<Inner>,
    action: RefCell<Action>,
}
fn discard_panic(payload: Box<dyn Any + Send>) {
    if let Err(nested) = catch_unwind(AssertUnwindSafe(|| drop(payload))) {
        // A hostile destructor must not unwind through the foreign callback.
        std::mem::forget(nested);
    }
}
fn subscribe(
    owner: &Rc<Inner>,
    handle: u64,
    action: impl FnMut(Event) -> Result<()> + 'static,
) -> Result<()> {
    let slot = Rc::new(Slot {
        owner: Rc::downgrade(owner),
        action: RefCell::new(Box::new(action)),
    });
    // Reserve before registration so allocation cannot invalidate a published callback.
    owner.subscriptions.borrow_mut().reserve(1);
    check(unsafe {
        sys::xui_subscribe(handle, Some(trampoline), Rc::as_ptr(&slot) as *mut c_void)
    })?;
    let old = owner.subscriptions.borrow_mut().insert(handle, slot);
    drop(old);
    Ok(())
}
fn unsubscribe(owner: &Rc<Inner>, handle: u64) -> Result<()> {
    check(unsafe { sys::xui_subscribe(handle, None, std::ptr::null_mut()) })?;
    let old = owner.subscriptions.borrow_mut().remove(&handle);
    drop(old);
    Ok(())
}
unsafe extern "C" fn trampoline(context: *mut c_void, value: *const sys::Event) -> i32 {
    // Native dispatch is synchronous and UI-thread-only. Revoke precedes release.
    let result = catch_unwind(AssertUnwindSafe(|| {
        let ptr = context as *const Slot;
        unsafe {
            Rc::increment_strong_count(ptr);
        }
        let slot = unsafe { Rc::from_raw(ptr) };
        let Some(owner) = slot.owner.upgrade() else {
            return 8;
        };
        let value = unsafe { *value };
        let outcome = catch_unwind(AssertUnwindSafe(|| {
            let mut callback = slot.action.try_borrow_mut().map_err(|_| Error {
                status: 8,
                message: "Recursive callback.".into(),
            })?;
            callback(Event {
                kind: value.kind,
                value: value.value,
            })
        }));
        let error = match outcome {
            Ok(Ok(())) => return 0,
            Ok(Err(e)) => Error {
                status: 8,
                message: e.to_string(),
            },
            Err(payload) => {
                let message = payload
                    .downcast_ref::<&str>()
                    .copied()
                    .or_else(|| payload.downcast_ref::<String>().map(String::as_str))
                    .unwrap_or("Unknown Rust panic.")
                    .to_owned();
                discard_panic(payload);
                Error { status: 8, message }
            }
        };
        *owner.callback_error.borrow_mut() = Some(error);
        8
    }));
    match result {
        Ok(status) => status,
        Err(payload) => {
            discard_panic(payload);
            8
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn panicking_payload_destructor_stays_inside_trampoline() -> Result<()> {
        struct Payload;
        impl Drop for Payload {
            fn drop(&mut self) {
                panic!("payload destructor");
            }
        }
        let w = Window::new("test", 100., 100.)?;
        let button = w.button("test")?;
        button.on_event(|_| std::panic::panic_any(Payload))?;
        assert_eq!(button.invoke().unwrap_err().status, 8);
        Ok(())
    }
    #[test]
    fn last_owner_and_callback_replacement() -> Result<()> {
        let w = Window::new("test", 100., 100.)?;
        let weak_window = w.downgrade();
        let button = w.button("test")?;
        let weak_button = button.downgrade();
        let calls = Rc::new(RefCell::new(0));
        let counter = calls.clone();
        button.on_event(move |_| {
            let counter = counter.clone();
            weak_button.upgrade().unwrap().on_event(move |_| {
                *counter.borrow_mut() += 1;
                Ok(())
            })
        })?;
        button.invoke()?;
        button.invoke()?;
        assert_eq!(*calls.borrow(), 1);
        drop(w);
        assert!(weak_window.upgrade().is_some());
        drop(button);
        assert!(weak_window.upgrade().is_none());
        Ok(())
    }
    #[test]
    fn unicode_batch_ownership_and_lists() -> Result<()> {
        let w = Window::new("test", 600., 720.)?;
        let label = w.label("日本語 😀")?;
        assert_eq!(label.text()?, "日本語 😀");
        assert!(label.set_text("\0").is_err());
        let bad = Property {
            integer: 1,
            ..Property::new(&label, PropertyKind::Checked)
        };
        assert!(w.update(&[Property::text(&label, "changed"), bad]).is_err());
        assert_eq!(label.text()?, "日本語 😀");
        let other = Window::new("other", 100., 100.)?;
        assert!(other.stack(Axis::Vertical)?.add(&label, 0.).is_err());
        let list = w.file_list("Files")?;
        list.set_items(&[
            FileItem {
                id: 0,
                name: "zero 😀",
                path: "",
                directory: false,
            },
            FileItem {
                id: 8,
                name: "eight",
                path: "",
                directory: false,
            },
        ])?;
        list.select(Some(0))?;
        assert_eq!(list.state()?, (2, Some(0)));
        list.filter("eight")?;
        assert_eq!(list.state()?, (1, Some(0)));
        assert!(list.select(Some(99)).is_err());
        list.select(None)?;
        assert_eq!(list.state()?, (1, None));
        Ok(())
    }
    #[test]
    fn callback_revoke_drop_and_panic() -> Result<()> {
        let w = Window::new("test", 100., 100.)?;
        let button = w.button("test")?;
        let weak = button.downgrade();
        let calls = Rc::new(RefCell::new(0));
        let counter = calls.clone();
        button.on_event(move |_| {
            *counter.borrow_mut() += 1;
            weak.upgrade().unwrap().unsubscribe()
        })?;
        button.invoke()?;
        button.invoke()?;
        assert_eq!(*calls.borrow(), 1);
        button.on_event(|_| panic!("panic sentinel"))?;
        let error = button.invoke().unwrap_err();
        assert_eq!(error.status, 8);
        assert!(error.message.contains("panic sentinel"));
        Ok(())
    }
    #[test]
    fn recursive_callback_is_error() -> Result<()> {
        let w = Window::new("test", 100., 100.)?;
        let button = w.button("test")?;
        let weak = button.downgrade();
        button.on_event(move |_| {
            let b = weak.upgrade().unwrap();
            b.owner.check(unsafe { sys::xui_invoke(b.handle) })
        })?;
        assert_eq!(button.invoke().unwrap_err().status, 8);
        Ok(())
    }
}
