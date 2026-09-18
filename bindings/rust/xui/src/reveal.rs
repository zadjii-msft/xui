use super::*;

#[repr(u32)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum RevealLayout {
    Fixed = 0,
    Expand = 1,
}
#[repr(u32)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum RevealDirection {
    Bottom = 0,
    Top = 1,
    Left = 2,
    Right = 3,
}

impl Window {
    pub fn reveal(&self, content: &Element, name: &str) -> Result<Reveal> {
        content.belongs(&self.0)?;
        let mut handle = 0;
        check(unsafe {
            sys::xui_reveal_create(self.0.handle, content.handle, text(name)?, &mut handle)
        })?;
        Ok(Reveal(Element {
            owner: self.0.clone(),
            handle,
        }))
    }
}

#[derive(Clone)]
pub struct Reveal(Element);
pub struct WeakReveal(WeakElement);
impl WeakReveal {
    pub fn upgrade(&self) -> Option<Reveal> {
        self.0.upgrade().map(Reveal)
    }
}
impl std::ops::Deref for Reveal {
    type Target = Element;
    fn deref(&self) -> &Element {
        &self.0
    }
}
impl Reveal {
    pub fn weak(&self) -> WeakReveal {
        WeakReveal(self.0.downgrade())
    }
    pub fn set_open(&self, value: bool) -> Result<()> {
        check(unsafe { sys::xui_reveal_set_open(self.handle, value as u32) })
    }
    pub fn open(&self) -> Result<bool> {
        let mut value = 0;
        check(unsafe { sys::xui_reveal_get_open(self.handle, &mut value) })?;
        Ok(value != 0)
    }
    pub fn set_duration(&self, milliseconds: u32) -> Result<()> {
        check(unsafe { sys::xui_reveal_set_duration(self.handle, milliseconds) })
    }
    pub fn duration(&self) -> Result<u32> {
        let mut value = 0;
        check(unsafe { sys::xui_reveal_get_duration(self.handle, &mut value) })?;
        Ok(value)
    }
    pub fn set_layout(&self, value: RevealLayout) -> Result<()> {
        check(unsafe { sys::xui_reveal_set_layout(self.handle, value as u32) })
    }
    pub fn layout(&self) -> Result<RevealLayout> {
        let mut value = 0;
        check(unsafe { sys::xui_reveal_get_layout(self.handle, &mut value) })?;
        match value {
            0 => Ok(RevealLayout::Fixed),
            1 => Ok(RevealLayout::Expand),
            _ => Err(invalid("Unknown Reveal layout.")),
        }
    }
    pub fn set_direction(&self, value: RevealDirection) -> Result<()> {
        check(unsafe { sys::xui_reveal_set_direction(self.handle, value as u32) })
    }
    pub fn direction(&self) -> Result<RevealDirection> {
        let mut value = 0;
        check(unsafe { sys::xui_reveal_get_direction(self.handle, &mut value) })?;
        match value {
            0 => Ok(RevealDirection::Bottom),
            1 => Ok(RevealDirection::Top),
            2 => Ok(RevealDirection::Left),
            3 => Ok(RevealDirection::Right),
            _ => Err(invalid("Unknown Reveal direction.")),
        }
    }
    pub fn progress(&self) -> Result<f32> {
        let mut value = 0.;
        check(unsafe { sys::xui_reveal_get_progress(self.handle, &mut value) })?;
        Ok(value)
    }
    pub fn animating(&self) -> Result<bool> {
        let mut value = 0;
        check(unsafe { sys::xui_reveal_get_animating(self.handle, &mut value) })?;
        Ok(value != 0)
    }
}
