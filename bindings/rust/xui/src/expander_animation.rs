use super::*;

impl Expander {
    pub fn set_duration(&self, milliseconds: u32) -> Result<()> {
        check(unsafe { sys::xui_expander_set_duration(self.handle, milliseconds) })
    }
    pub fn duration(&self) -> Result<u32> {
        let mut value = 0;
        check(unsafe { sys::xui_expander_get_duration(self.handle, &mut value) })?;
        Ok(value)
    }
    pub fn progress(&self) -> Result<f32> {
        let mut value = 0.;
        check(unsafe { sys::xui_expander_get_progress(self.handle, &mut value) })?;
        Ok(value)
    }
    pub fn animating(&self) -> Result<bool> {
        let mut value = 0;
        check(unsafe { sys::xui_expander_get_animating(self.handle, &mut value) })?;
        Ok(value != 0)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn expander_animation_contract() -> Result<()> {
        let window = Window::new("Animated expander", 600., 400.)?;
        let input = window.text_input("Body")?;
        let expander = window.expander("Details", &input)?;
        assert_eq!(expander.duration()?, 0);
        assert_eq!(expander.progress()?, 1.);
        assert!(!expander.animating()?);
        expander.set_expanded(false)?;
        assert_eq!(expander.progress()?, 0.);
        expander.set_duration(180)?;
        expander.set_expanded(true)?;
        assert!(expander.expanded()? && expander.animating()?);
        assert!(expander.set_duration(10001).is_err());
        assert_eq!(expander.duration()?, 180);
        assert!(expander.animating()?);
        expander.set_duration(0)?;
        assert_eq!(expander.progress()?, 1.);
        assert!(!expander.animating()?);
        assert!(window.expander("Duplicate", &input).is_err());
        assert_ne!(
            unsafe { sys::xui_expander_get_progress(expander.handle, std::ptr::null_mut()) },
            0
        );
        let mut value = 0.;
        assert_ne!(
            unsafe { sys::xui_expander_get_progress(input.handle, &mut value) },
            0
        );
        Ok(())
    }
}
