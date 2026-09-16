use super::*;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u32)]
pub enum StyleTarget {
    Toggle,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u32)]
pub enum StylePart {
    Root,
    Label,
    Indicator,
    Mark,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u64)]
pub enum StyleState {
    Focused = 1,
    Checked = 2,
    Hovered = 4,
    Pressed = 8,
    Disabled = 16,
}

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct PartStyleValues {
    pub background: Option<ThemeColor>,
    pub foreground: Option<ThemeColor>,
    pub border_brush: Option<ThemeColor>,
    pub border_thickness: Option<Insets>,
    pub padding: Option<Insets>,
    pub corner_radius: Option<f32>,
    pub size: Option<f32>,
}
impl PartStyleValues {
    fn append(
        self,
        part: StylePart,
        state: u64,
        output: &mut Vec<sys::StyleProperty>,
    ) -> Result<()> {
        let allowed = match part {
            StylePart::Root => 63,
            StylePart::Indicator => 109,
            _ => 2,
        };
        let mut record = |property, value_type, color, insets, number| -> Result<()> {
            if property & allowed == 0 {
                return Err(invalid("Unsupported property on this style part."));
            }
            output.push(sys::StyleProperty {
                size: size_of::<sys::StyleProperty>() as u32,
                version: 0x10000,
                property,
                value_type,
                part: part as u32,
                state,
                color,
                insets,
                number,
                ..Default::default()
            });
            Ok(())
        };
        for (property, value) in [
            (1, self.background),
            (2, self.foreground),
            (4, self.border_brush),
        ] {
            if let Some(c) = value {
                if c.light > 0xffffff || c.dark > 0xffffff {
                    return Err(invalid("Colors must be RGB24."));
                }
                record(
                    property,
                    1,
                    sys::ThemeColor {
                        light: c.light,
                        dark: c.dark,
                    },
                    Default::default(),
                    0.0,
                )?;
            }
        }
        fn dimension(value: f32) -> Result<()> {
            if !value.is_finite() || !(0.0..=32768.0).contains(&value) {
                return Err(invalid(
                    "Style dimensions must be finite DIPs from zero through 32768.",
                ));
            }
            Ok(())
        }
        for (property, value) in [(8, self.border_thickness), (16, self.padding)] {
            if let Some(e) = value {
                for v in [e.left, e.top, e.right, e.bottom] {
                    dimension(v)?;
                }
                record(
                    property,
                    2,
                    Default::default(),
                    sys::StyleInsets {
                        left: e.left,
                        top: e.top,
                        right: e.right,
                        bottom: e.bottom,
                    },
                    0.0,
                )?;
            }
        }
        for (property, value) in [(32, self.corner_radius), (64, self.size)] {
            if let Some(v) = value {
                dimension(v)?;
                record(
                    property,
                    3,
                    Default::default(),
                    Default::default(),
                    v as f64,
                )?;
            }
        }
        Ok(())
    }
    fn from_native(records: &[sys::StyleProperty]) -> Result<Self> {
        let mut result = Self::default();
        for r in records {
            let c = ThemeColor::new(r.color.light, r.color.dark);
            let e = Insets {
                left: r.insets.left,
                top: r.insets.top,
                right: r.insets.right,
                bottom: r.insets.bottom,
            };
            match r.property {
                1 => result.background = Some(c),
                2 => result.foreground = Some(c),
                4 => result.border_brush = Some(c),
                8 => result.border_thickness = Some(e),
                16 => result.padding = Some(e),
                32 => result.corner_radius = Some(r.number as f32),
                64 => result.size = Some(r.number as f32),
                _ => return Err(invalid("Unknown native style property.")),
            }
        }
        Ok(result)
    }
}
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct PartStyle {
    pub part: StylePart,
    pub values: PartStyleValues,
}
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct ControlStyleRule {
    pub part: StylePart,
    pub state: StyleState,
    pub values: PartStyleValues,
}
struct ControlStyleDefinition {
    target: StyleTarget,
    parts: Vec<PartStyle>,
    rules: Vec<ControlStyleRule>,
    based_on: Option<ControlStyle>,
    depth: usize,
    identities: RefCell<Vec<(Weak<Inner>, u64)>>,
}
/// Immutable values, shared between controls without retaining their windows.
#[derive(Clone)]
pub struct ControlStyle(Rc<ControlStyleDefinition>);
impl ControlStyle {
    pub fn new(
        target: StyleTarget,
        parts: &[PartStyle],
        rules: &[ControlStyleRule],
        based_on: Option<&Self>,
    ) -> Result<Self> {
        let depth = based_on.map_or(1, |b| b.0.depth + 1);
        if depth > 16 || parts.len() > 64 || rules.len() > 256 {
            return Err(invalid("Control style limits exceeded."));
        }
        if based_on.is_some_and(|b| b.0.target != target) {
            return Err(invalid("Style base target mismatch."));
        }
        let mut keys = Vec::new();
        let mut records = Vec::new();
        for p in parts {
            if keys.contains(&(p.part, 0)) {
                return Err(invalid("Duplicate style part."));
            }
            keys.push((p.part, 0));
            p.values.append(p.part, 0, &mut records)?;
        }
        for r in rules {
            if keys.contains(&(r.part, r.state as u64)) {
                return Err(invalid("Duplicate style state for this part."));
            }
            keys.push((r.part, r.state as u64));
            r.values.append(r.part, r.state as u64, &mut records)?;
        }
        Ok(Self(Rc::new(ControlStyleDefinition {
            target,
            parts: parts.to_vec(),
            rules: rules.to_vec(),
            based_on: based_on.cloned(),
            depth,
            identities: RefCell::new(Vec::new()),
        })))
    }
    pub fn target(&self) -> StyleTarget {
        self.0.target
    }
    pub fn parts(&self) -> &[PartStyle] {
        &self.0.parts
    }
    pub fn rules(&self) -> &[ControlStyleRule] {
        &self.0.rules
    }
    pub fn based_on(&self) -> Option<&Self> {
        self.0.based_on.as_ref()
    }
    fn identity(&self, owner: &Rc<Inner>) -> Option<u64> {
        let mut identities = self.0.identities.borrow_mut();
        identities.retain(|(window, _)| window.strong_count() != 0);
        identities
            .iter()
            .find(|(window, _)| window.ptr_eq(&Rc::downgrade(owner)))
            .map(|(_, id)| *id)
    }
    fn remember(&self, owner: &Rc<Inner>, identity: u64) {
        let mut identities = self.0.identities.borrow_mut();
        if let Some((_, id)) = identities
            .iter_mut()
            .find(|(window, _)| window.ptr_eq(&Rc::downgrade(owner)))
        {
            *id = identity;
        } else {
            identities.push((Rc::downgrade(owner), identity));
        }
    }
    fn with_native_handle(
        &self,
        owner: &Rc<Inner>,
        apply: &mut dyn FnMut(u64) -> Result<()>,
    ) -> Result<()> {
        if let Some(identity) = self.identity(owner) {
            let mut handle = 0;
            owner.check(unsafe {
                sys::xui_control_style_reacquire(owner.handle, identity, &mut handle)
            })?;
            if handle != 0 {
                let applied = apply(handle);
                let released = owner.check(unsafe { sys::xui_control_style_release(handle) });
                return applied.and(released);
            }
        }
        let mut records = Vec::new();
        for p in &self.0.parts {
            p.values.append(p.part, 0, &mut records)?;
        }
        for r in &self.0.rules {
            r.values.append(r.part, r.state as u64, &mut records)?;
        }
        let mut create = |base| {
            let options = sys::ControlStyleOptions {
                size: size_of::<sys::ControlStyleOptions>() as u32,
                version: 0x10000,
                target: self.0.target as u32,
                properties: records.as_ptr(),
                property_count: records.len() as u32,
                based_on: base,
                ..Default::default()
            };
            let mut handle = 0;
            owner.check(unsafe {
                sys::xui_control_style_create(owner.handle, &options, &mut handle)
            })?;
            self.remember(owner, handle);
            let applied = apply(handle);
            let released = owner.check(unsafe { sys::xui_control_style_release(handle) });
            applied.and(released)
        };
        if let Some(base) = &self.0.based_on {
            base.with_native_handle(owner, &mut create)
        } else {
            create(0)
        }
    }
}
impl Element {
    pub fn set_control_style(&self, style: Option<&ControlStyle>) -> Result<()> {
        let mut apply = |handle| {
            self.owner
                .check(unsafe { sys::xui_control_set_style(self.handle, handle) })
        };
        if let Some(style) = style {
            if let Some(identity) = style.identity(&self.owner) {
                let mut applied = 0;
                self.owner.check(unsafe {
                    sys::xui_control_try_set_style(self.handle, identity, &mut applied)
                })?;
                if applied != 0 {
                    return Ok(());
                }
            }
            style.with_native_handle(&self.owner, &mut apply)
        } else {
            apply(0)
        }
    }
    pub fn set_control_style_values(&self, part: StylePart, values: PartStyleValues) -> Result<()> {
        let mut records = Vec::new();
        values.append(part, 0, &mut records)?;
        self.owner.check(unsafe {
            sys::xui_control_set_style_values(
                self.handle,
                part as u32,
                records.as_ptr(),
                records.len() as u32,
            )
        })
    }
    pub fn control_style_values(
        &self,
        part: StylePart,
        effective: bool,
    ) -> Result<PartStyleValues> {
        let mut count = 0;
        self.owner.check(unsafe {
            sys::xui_control_get_style_values(
                self.handle,
                part as u32,
                effective as u32,
                std::ptr::null_mut(),
                0,
                &mut count,
            )
        })?;
        let mut records = vec![sys::StyleProperty::default(); count as usize];
        self.owner.check(unsafe {
            sys::xui_control_get_style_values(
                self.handle,
                part as u32,
                effective as u32,
                records.as_mut_ptr(),
                count,
                &mut count,
            )
        })?;
        PartStyleValues::from_native(&records)
    }
}
impl Toggle {
    pub fn set_style(&self, style: Option<&ControlStyle>) -> Result<()> {
        self.set_control_style(style)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn generic_style_schema_and_records() -> Result<()> {
        let values = PartStyleValues {
            size: Some(18.0),
            background: Some(ThemeColor::new(0, 0xffffff)),
            ..Default::default()
        };
        let mut records = Vec::new();
        values.append(StylePart::Indicator, 0, &mut records)?;
        assert_eq!(PartStyleValues::from_native(&records)?, values);
        assert_eq!(records[0].color.dark, 0xffffff);
        assert!(values.append(StylePart::Label, 0, &mut Vec::new()).is_err());
        assert!(
            PartStyleValues {
                size: Some(f32::NAN),
                ..Default::default()
            }
            .append(StylePart::Indicator, 0, &mut Vec::new())
            .is_err()
        );
        let part = PartStyle {
            part: StylePart::Indicator,
            values,
        };
        assert!(ControlStyle::new(StyleTarget::Toggle, &[part, part], &[], None).is_err());
        let mut style = ControlStyle::new(StyleTarget::Toggle, &[part], &[], None)?;
        for _ in 1..16 {
            style = ControlStyle::new(StyleTarget::Toggle, &[], &[], Some(&style))?;
        }
        assert!(ControlStyle::new(StyleTarget::Toggle, &[], &[], Some(&style)).is_err());
        Ok(())
    }

    #[test]
    fn generic_toggle_native_lifecycle() -> Result<()> {
        let style = ControlStyle::new(
            StyleTarget::Toggle,
            &[
                PartStyle {
                    part: StylePart::Root,
                    values: PartStyleValues {
                        foreground: Some(0x123456.into()),
                        ..Default::default()
                    },
                },
                PartStyle {
                    part: StylePart::Indicator,
                    values: PartStyleValues {
                        size: Some(18.0),
                        background: Some(0.into()),
                        ..Default::default()
                    },
                },
            ],
            &[ControlStyleRule {
                part: StylePart::Indicator,
                state: StyleState::Checked,
                values: PartStyleValues {
                    background: Some(0x654321.into()),
                    ..Default::default()
                },
            }],
            None,
        )?;
        let window = Window::new("Generic Toggle", 300.0, 200.0)?;
        let toggle = window.toggle("Styled")?;
        toggle.set_style(Some(&style))?;
        assert_eq!(
            toggle.effective_style_values(StylePart::Label)?.foreground,
            Some(0x123456.into())
        );
        toggle.checked(true)?;
        assert_eq!(
            toggle
                .effective_style_values(StylePart::Indicator)?
                .background,
            Some(0x654321.into())
        );
        let local = PartStyleValues {
            foreground: Some(0.into()),
            ..Default::default()
        };
        toggle.set_style_values(StylePart::Root, local)?;
        toggle.set_style(None)?;
        assert_eq!(
            toggle.effective_style_values(StylePart::Label)?.foreground,
            Some(0.into())
        );
        toggle.set_style_values(StylePart::Root, PartStyleValues::default())?;
        assert_eq!(
            toggle.effective_style_values(StylePart::Label)?,
            PartStyleValues::default()
        );
        toggle.set_style(Some(&style))?;
        let marker = window.button("Before")?.handle;
        for _ in 0..256 {
            toggle.set_style(Some(&style))?;
        }
        assert_eq!(window.button("After")?.handle, marker + 1);
        assert!(
            window
                .button("Wrong target")?
                .set_control_style(Some(&style))
                .is_err()
        );
        let other = Window::new("Other", 300.0, 200.0)?;
        let shared = other.toggle("Shared")?;
        shared.set_style(Some(&style))?;
        assert_eq!(
            shared.effective_style_values(StylePart::Indicator)?.size,
            Some(18.0)
        );
        Ok(())
    }
}
impl Toggle {
    pub fn set_style_values(&self, part: StylePart, values: PartStyleValues) -> Result<()> {
        self.set_control_style_values(part, values)
    }
    pub fn style_values(&self, part: StylePart) -> Result<PartStyleValues> {
        self.control_style_values(part, false)
    }
    pub fn effective_style_values(&self, part: StylePart) -> Result<PartStyleValues> {
        self.control_style_values(part, true)
    }
}
