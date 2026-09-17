use super::*;
macro_rules! preview_enum {
    ($name:ident { $($case:ident),+ }) => {
        #[derive(Clone, Copy, Debug, PartialEq, Eq)]
        #[repr(u32)]
        pub enum $name { $($case),+ }
        impl TryFrom<u32> for $name {
            type Error = Error;
            fn try_from(value: u32) -> Result<Self> {
                $(if value == Self::$case as u32 { return Ok(Self::$case); })+
                Err(Error { status: 7, message: "Unknown native preview status.".into() })
            }
        }
    }
}
preview_enum!(PreviewState {
    Idle,
    Loading,
    Accepted,
    Unsupported,
    Failed,
    Retiring
});
preview_enum!(PreviewReason {
    None,
    NoHandler,
    Restricted,
    UnsupportedProvider,
    MissingProvider,
    InitializationFailed,
    RenderFailed,
    TimedOut,
    BrokerFailed,
    ResourceLimit,
    Cancelled,
    Hidden,
    UnsupportedArchitecture,
    ActivationFailed
});
preview_enum!(PreviewPhase {
    None,
    Policy,
    Discovery,
    Activation,
    Initialize,
    Render,
    Resize,
    Focus,
    Unload
});
preview_enum!(PreviewCleanup {
    None,
    Pending,
    Unloaded,
    BrokerTerminated,
    ProviderUnknown
});
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct PreviewStatus {
    pub generation: u64,
    pub state: PreviewState,
    pub reason: PreviewReason,
    pub phase: PreviewPhase,
    pub hresult: i32,
    pub cleanup: PreviewCleanup,
}
/// Explicit installed-provider preview. Read-only streams are not a network or content sandbox.
#[derive(Clone)]
pub struct ShellPreview(pub Element);
impl Window {
    pub fn shell_preview(&self, name: &str) -> Result<ShellPreview> {
        let mut handle = 0;
        check(unsafe { sys::xui_shell_preview_create(self.0.handle, text(name)?, &mut handle) })?;
        Ok(ShellPreview(Element {
            owner: self.0.clone(),
            handle,
        }))
    }
}
impl ShellPreview {
    pub fn element(&self) -> &Element {
        &self.0
    }
    pub fn load_local(&self, path: &str) -> Result<u64> {
        let mut generation = 0;
        check(unsafe {
            sys::xui_shell_preview_load_local(self.0.handle, text(path)?, &mut generation)
        })?;
        Ok(generation)
    }
    pub fn cancel(&self, generation: u64) -> Result<()> {
        check(unsafe { sys::xui_shell_preview_cancel(self.0.handle, generation) })
    }
    pub fn unload(&self) -> Result<()> {
        check(unsafe { sys::xui_shell_preview_unload(self.0.handle) })
    }
    pub fn focus_content(&self, reverse: bool) -> Result<()> {
        check(unsafe { sys::xui_shell_preview_focus_content(self.0.handle, reverse as u32) })
    }
    pub fn status(&self) -> Result<PreviewStatus> {
        let mut value = sys::PreviewStatus {
            size: std::mem::size_of::<sys::PreviewStatus>() as u32,
            version: 1,
            ..Default::default()
        };
        check(unsafe { sys::xui_shell_preview_get_status(self.0.handle, &mut value) })?;
        Ok(PreviewStatus {
            generation: value.generation,
            state: value.state.try_into()?,
            reason: value.reason.try_into()?,
            phase: value.phase.try_into()?,
            hresult: value.hresult,
            cleanup: value.cleanup.try_into()?,
        })
    }
    pub fn on_changed(
        &self,
        mut action: impl FnMut(PreviewStatus) -> Result<()> + 'static,
    ) -> Result<()> {
        let weak = Rc::downgrade(&self.0.owner);
        let handle = self.0.handle;
        subscribe(&self.0.owner, handle, move |event| {
            if event.kind == 2 {
                if let Some(owner) = weak.upgrade() {
                    let status = ShellPreview(Element { owner, handle }).status()?;
                    if status.generation == event.value {
                        action(status)?;
                    }
                }
            }
            Ok(())
        })
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn preview_generations_and_callbacks() -> Result<()> {
        let window = Window::new("Preview binding", 480.0, 320.0)?;
        let preview = window.shell_preview("Windows preview")?;
        assert_eq!(preview.status()?.state, PreviewState::Idle);
        let calls = Rc::new(std::cell::Cell::new(0));
        let observed = calls.clone();
        preview.on_changed(move |_| {
            observed.set(observed.get() + 1);
            Ok(())
        })?;
        let first = preview.load_local("C:\\authored.txt")?;
        let second = preview.load_local("C:\\replacement.txt")?;
        assert!(second > first);
        preview.cancel(first)?;
        assert_eq!(preview.status()?.generation, second);
        preview.cancel(second)?;
        assert_eq!(preview.status()?.state, PreviewState::Idle);
        assert_eq!(calls.get(), 3);
        Ok(())
    }
}
