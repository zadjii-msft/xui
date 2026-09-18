use super::*;

impl NavigationView {
    pub fn set_duration(&self, milliseconds: u32) -> Result<()> {
        check(unsafe { sys::xui_navigation_view_set_duration(self.handle, milliseconds) })
    }
    pub fn duration(&self) -> Result<u32> {
        let mut value = 0;
        check(unsafe { sys::xui_navigation_view_get_duration(self.handle, &mut value) })?;
        Ok(value)
    }
    pub fn animating(&self) -> Result<bool> {
        let mut value = 0;
        check(unsafe { sys::xui_navigation_view_get_animating(self.handle, &mut value) })?;
        Ok(value != 0)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn navigation_animation_contract() -> Result<()> {
        let window = Window::new("Navigation duration", 600., 400.)?;
        let view = window.navigation_view("Navigation")?;
        assert_eq!(view.duration()?, 0);
        assert!(!view.animating()?);
        for duration in [0, 180, 10000] {
            view.set_duration(duration)?;
            assert_eq!(view.duration()?, duration);
        }
        for duration in [10001, u32::MAX] {
            assert!(view.set_duration(duration).is_err());
            assert_eq!(view.duration()?, 10000);
        }
        assert_ne!(
            unsafe { sys::xui_navigation_view_get_duration(view.handle, std::ptr::null_mut()) },
            0
        );
        let input = window.text_input("Not navigation")?;
        let mut value = 0;
        assert_ne!(
            unsafe { sys::xui_navigation_view_get_animating(input.handle, &mut value) },
            0
        );
        Ok(())
    }
}
