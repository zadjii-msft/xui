use super::*;

impl Progress {
    pub fn set_duration(&self, milliseconds: u32) -> Result<()> {
        check(unsafe { sys::xui_progress_set_duration(self.handle, milliseconds) })
    }
    pub fn duration(&self) -> Result<u32> {
        let mut value = 0;
        check(unsafe { sys::xui_progress_get_duration(self.handle, &mut value) })?;
        Ok(value)
    }
    pub fn presented_value(&self) -> Result<f64> {
        let mut value = 0.;
        check(unsafe { sys::xui_progress_get_presented_value(self.handle, &mut value) })?;
        Ok(value)
    }
    pub fn animating(&self) -> Result<bool> {
        let mut value = 0;
        check(unsafe { sys::xui_progress_get_animating(self.handle, &mut value) })?;
        Ok(value != 0)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn progress_animation_contract() -> Result<()> {
        let window = Window::new("Animated progress", 600., 400.)?;
        let progress = window.progress("Work")?;
        assert_eq!(progress.duration()?, 0);
        progress.set_value(25.)?;
        assert_eq!(progress.presented_value()?, 25.);
        assert!(!progress.animating()?);
        progress.set_duration(10000)?;
        progress.set_value(75.)?;
        assert_eq!(progress.value()?, 75.);
        assert_eq!(progress.presented_value()?, 25.);
        assert!(progress.animating()?);
        for invalid in [10001, u32::MAX] {
            assert!(progress.set_duration(invalid).is_err());
            assert_eq!(progress.duration()?, 10000);
            assert!(progress.animating()?);
        }
        for invalid in [f64::NAN, f64::INFINITY, -1., 101.] {
            assert!(progress.set_value(invalid).is_err());
            assert_eq!(progress.value()?, 75.);
            assert_eq!(progress.presented_value()?, 25.);
        }
        progress.set_value(50.)?;
        assert_eq!(progress.presented_value()?, 25.);
        progress.set_duration(0)?;
        assert_eq!(progress.presented_value()?, 50.);
        assert!(!progress.animating()?);
        progress.set_duration(180)?;
        progress.set_value(100.)?;
        progress.set_state(ProgressState::Indeterminate)?;
        assert_eq!(progress.presented_value()?, 100.);
        assert!(!progress.animating()?);
        progress.set_value(40.)?;
        assert_eq!(progress.presented_value()?, 40.);
        assert!(!progress.animating()?);
        assert_ne!(
            unsafe { sys::xui_progress_get_presented_value(progress.handle, std::ptr::null_mut()) },
            0
        );
        assert_ne!(
            unsafe { sys::xui_progress_get_duration(progress.handle, std::ptr::null_mut()) },
            0
        );
        assert_ne!(
            unsafe { sys::xui_progress_get_animating(progress.handle, std::ptr::null_mut()) },
            0
        );
        let input = window.text_input("Wrong kind")?;
        assert_ne!(
            unsafe { sys::xui_progress_set_duration(input.handle, 180) },
            0
        );
        let mut value = 0.;
        assert_ne!(
            unsafe { sys::xui_progress_get_presented_value(input.handle, &mut value) },
            0
        );
        assert_ne!(unsafe { sys::xui_progress_set_duration(0, 180) }, 0);
        Ok(())
    }
}
