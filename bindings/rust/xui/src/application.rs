use super::*;
use std::sync::{
    Arc,
    atomic::{AtomicU64, Ordering},
};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum WindowState {
    Created,
    Open,
    Closing,
    Closed,
}

/// Same-STA document windows. Keep callback captures alive after `show` returns.
pub struct Application {
    handle: u64,
    windows: RefCell<Vec<Window>>,
    errors: RefCell<Vec<Error>>,
    sender: Arc<AtomicU64>,
}
impl Application {
    pub fn new() -> Result<Self> {
        let mut handle = 0;
        check(unsafe { sys::xui_application_create(&mut handle) })?;
        Ok(Self {
            handle,
            windows: RefCell::new(Vec::new()),
            errors: RefCell::new(Vec::new()),
            sender: Arc::new(AtomicU64::new(handle)),
        })
    }
    pub fn create_window(&self, title: &str, width: f32, height: f32) -> Result<Window> {
        let options = sys::Options {
            size: size_of::<sys::Options>() as u32,
            version: sys::ABI_VERSION,
            title: text(title)?,
            width,
            height,
            ..Default::default()
        };
        let mut handle = 0;
        check(unsafe {
            sys::xui_application_window_create(self.handle, &options, 0, &mut handle)
        })?;
        let window = Window(Rc::new(Inner {
            handle,
            subscriptions: RefCell::new(HashMap::new()),
            callback_error: RefCell::new(None),
        }));
        self.windows.borrow_mut().push(window.clone());
        Ok(window)
    }
    pub fn show(&self, window: &Window) -> Result<()> {
        check(unsafe { sys::xui_application_show(self.handle, window.0.handle) })
    }
    pub fn run(&self) -> Result<()> {
        let result = check(unsafe { sys::xui_application_run(self.handle) });
        self.collect_closed()?;
        if let Some(error) = self.errors.borrow().first() {
            return Err(error.clone());
        }
        result
    }
    pub fn shutdown(&self) -> Result<()> {
        check(unsafe { sys::xui_application_shutdown(self.handle) })
    }
    /// Releases the application's roots for closed windows; elements can retain their own arenas.
    pub fn collect_closed(&self) -> Result<()> {
        let mut windows = self.windows.borrow_mut();
        let mut index = 0;
        while index < windows.len() {
            if windows[index].state()? == WindowState::Closed {
                if let Some(error) = windows[index].0.callback_error.borrow().as_ref() {
                    self.errors.borrow_mut().push(error.clone());
                }
                windows.remove(index);
            } else {
                index += 1;
            }
        }
        Ok(())
    }
    pub fn post(&self, action: impl FnOnce() -> Result<()> + 'static) -> Result<bool> {
        post(self.handle, Box::new(action))
    }
    pub fn dispatcher(&self) -> ApplicationDispatcher {
        ApplicationDispatcher(self.sender.clone())
    }
}
impl Drop for Application {
    fn drop(&mut self) {
        self.sender.store(0, Ordering::Release);
        let shutdown = unsafe { sys::xui_application_shutdown(self.handle) };
        if shutdown != 0 {
            eprintln!("XUI application shutdown failed: {shutdown}");
        }
        // A never-run application can contain shown windows. Complete their retirement first.
        if self
            .windows
            .get_mut()
            .iter()
            .any(|w| matches!(w.state(), Ok(WindowState::Open | WindowState::Closing)))
        {
            let status = unsafe { sys::xui_application_run(self.handle) };
            if status != 0 {
                eprintln!("XUI application retirement failed: {status}");
            }
        }
        self.windows.get_mut().clear();
        let status = unsafe { sys::xui_application_destroy(self.handle) };
        if status != 0 {
            eprintln!("XUI application destruction failed: {status}");
        }
    }
}
/// Thread-safe sender. It never extends application lifetime.
#[derive(Clone)]
pub struct ApplicationDispatcher(Arc<AtomicU64>);
impl ApplicationDispatcher {
    pub fn post(&self, action: impl FnOnce() -> Result<()> + Send + 'static) -> Result<bool> {
        let handle = self.0.load(Ordering::Acquire);
        if handle == 0 {
            return Ok(false);
        }
        post(handle, Box::new(action))
    }
}
struct Posted(Option<Box<dyn FnOnce() -> Result<()>>>);
fn post(handle: u64, action: Box<dyn FnOnce() -> Result<()>>) -> Result<bool> {
    let context = Box::into_raw(Box::new(Posted(Some(action))));
    let status = unsafe { sys::xui_application_post(handle, Some(deliver), context.cast()) };
    if status == 0 {
        return Ok(true);
    }
    unsafe {
        drop(Box::from_raw(context));
    }
    if status == 2 || status == 11 {
        return Ok(false);
    }
    check(status)?;
    Ok(false)
}
unsafe extern "C" fn deliver(context: *mut c_void, execute: u32) -> i32 {
    let result = catch_unwind(AssertUnwindSafe(|| {
        let mut posted = unsafe { Box::from_raw(context.cast::<Posted>()) };
        if execute != 0 {
            if let Some(action) = posted.0.take() {
                return action();
            }
        }
        Ok(())
    }));
    match result {
        Ok(Ok(())) => 0,
        Ok(Err(error)) => {
            eprintln!("XUI application callback: {error}");
            8
        }
        Err(payload) => {
            discard_panic(payload);
            8
        }
    }
}
impl Window {
    pub fn state(&self) -> Result<WindowState> {
        let mut state = 0;
        check(unsafe { sys::xui_window_state(self.0.handle, &mut state) })?;
        match state {
            0 => Ok(WindowState::Created),
            1 => Ok(WindowState::Open),
            2 => Ok(WindowState::Closing),
            3 => Ok(WindowState::Closed),
            _ => Err(invalid("Unknown window state.")),
        }
    }
    pub fn on_closed(&self, mut action: impl FnMut() -> Result<()> + 'static) -> Result<()> {
        let slot = Rc::new(Slot {
            owner: Rc::downgrade(&self.0),
            action: RefCell::new(Box::new(move |_| action())),
        });
        self.0.subscriptions.borrow_mut().reserve(1);
        check(unsafe {
            sys::xui_window_closed(
                self.0.handle,
                Some(trampoline),
                Rc::as_ptr(&slot) as *mut c_void,
            )
        })?;
        self.0.subscriptions.borrow_mut().insert(u64::MAX, slot);
        Ok(())
    }
}
