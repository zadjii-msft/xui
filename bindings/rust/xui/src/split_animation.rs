use super::*;

impl SplitView {
    pub fn set_transition_duration(&self, milliseconds: u32) -> Result<()> {
        check(unsafe { sys::xui_split_view_set_transition_duration(self.handle, milliseconds) })
    }
}
#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn split_animation_contract() -> Result<()> {
        let window = Window::new("Animated panes", 1000., 600.)?;
        let first = window.text_input("First")?;
        let second = window.text_input("Second")?;
        let split = window.split_view("Panes", &first, &second)?;
        assert_eq!(split.transition_duration()?, 0);
        assert_eq!(split.progress()?, 1.);
        assert!(!split.animating()?);
        split.set_second_visible(false)?;
        assert_eq!(split.progress()?, 0.);
        split.set_transition_duration(180)?;
        split.set_second_visible(true)?;
        assert!(split.animating()?);
        assert!(split.set_transition_duration(10001).is_err());
        assert_eq!(split.transition_duration()?, 180);
        assert!(split.animating()?);
        split.set_transition_duration(0)?;
        assert_eq!(split.progress()?, 1.);
        assert!(!split.animating()?);
        assert_ne!(
            unsafe { sys::xui_split_view_get_progress(split.handle, std::ptr::null_mut()) },
            0
        );
        let mut progress = 0.;
        assert_ne!(
            unsafe { sys::xui_split_view_get_progress(first.handle, &mut progress) },
            0
        );
        Ok(())
    }
}
impl SplitView {
    pub fn transition_duration(&self) -> Result<u32> {
        let mut value = 0;
        check(unsafe { sys::xui_split_view_get_transition_duration(self.handle, &mut value) })?;
        Ok(value)
    }
    pub fn progress(&self) -> Result<f32> {
        let mut value = 0.;
        check(unsafe { sys::xui_split_view_get_progress(self.handle, &mut value) })?;
        Ok(value)
    }
    pub fn animating(&self) -> Result<bool> {
        let mut value = 0;
        check(unsafe { sys::xui_split_view_get_animating(self.handle, &mut value) })?;
        Ok(value != 0)
    }
}
