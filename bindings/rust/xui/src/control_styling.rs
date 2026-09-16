use super::*;

include!("control_style_catalog.g.rs");

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u32)]
pub enum StyleFontStyle { Normal, Italic, Oblique }
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u32)]
pub enum StyleAlignment { Start, Center, End, Stretch }

#[derive(Clone, Debug, Default, PartialEq)]
pub struct PartStyleValues {
    pub background: Option<ThemeColor>,
    pub foreground: Option<ThemeColor>,
    pub border_brush: Option<ThemeColor>,
    pub border_thickness: Option<Insets>,
    pub padding: Option<Insets>,
    pub corner_radius: Option<f32>,
    pub size: Option<f32>,
    pub font_family: Option<std::sync::Arc<str>>,
    pub font_size: Option<f32>,
    pub font_weight: Option<u32>,
    pub font_style: Option<StyleFontStyle>,
    pub horizontal_alignment: Option<StyleAlignment>,
    pub vertical_alignment: Option<StyleAlignment>,
    pub spacing: Option<f32>,
    pub row_height: Option<f32>,
    pub header_height: Option<f32>,
    pub indentation: Option<f32>,
    pub thickness: Option<f32>,
    pub width: Option<f32>,
    pub height: Option<f32>,
    pub row_gap: Option<f32>,
    pub column_gap: Option<f32>,
    pub maximum_lines: Option<u32>,
    pub wrapping: Option<bool>,
}
impl PartStyleValues {
    fn append(
        &self,
        part: StylePart,
        state: u64,
        target: Option<StyleTarget>,
        output: &mut Vec<sys::StyleProperty>,
    ) -> Result<()> {
        let limits = target.map(|target| style_limits(target, part)).unwrap_or((32768.0, 1024, 7, 15, 15));
        if self.font_family.as_ref().is_some_and(|family| family.encode_utf16().count() > limits.1) {
            return Err(invalid("Font family exceeds the part's UTF-16 limit."));
        }
        if self.font_size.is_some_and(|size| size > limits.0) {
            return Err(invalid("Font size exceeds the part's limit."));
        }
        if self.font_style.is_some_and(|style| limits.2 & (1u32 << style as u32) == 0) {
            return Err(invalid("Font style is not supported on this part."));
        }
        if self.horizontal_alignment.is_some_and(|alignment| limits.3 & (1u32 << alignment as u32) == 0)
            || self.vertical_alignment.is_some_and(|alignment| limits.4 & (1u32 << alignment as u32) == 0) {
            return Err(invalid("Alignment is not supported on this part."));
        }
        let allowed = if let Some(target) = target {
            let (allowed, states, state_properties) = style_schema(target, part).ok_or_else(|| invalid("Unsupported style target or part."))?;
            if state & !states != 0 { return Err(invalid("Unsupported state on this style part.")); }
            if state == 0 { allowed } else { state_properties }
        } else {
            u64::MAX
        };
        let mut record = |property, value_type, color, insets, number| -> Result<()> {
            if property as u64 & allowed == 0 {
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
        if self.font_size == Some(0.0) { return Err(invalid("Font size must be positive.")); }
        if self.row_height == Some(0.0) { return Err(invalid("Row height must be positive.")); }
        if self.font_weight.is_some_and(|weight| !(1..=999).contains(&weight)) { return Err(invalid("Font weight must be between 1 and 999.")); }
        if self.maximum_lines.is_some_and(|lines| lines > 32768) { return Err(invalid("Invalid maximum line count.")); }
        for (property, value) in [(32, self.corner_radius), (64, self.size), (256, self.font_size),
            (8192, self.spacing), (16384, self.row_height), (32768, self.header_height), (65536, self.indentation),
            (131072, self.thickness), (262144, self.width), (524288, self.height), (1048576, self.row_gap),
            (2097152, self.column_gap)] {
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
        for (property, value) in [(512, self.font_weight), (1024, self.font_style.map(|v| v as u32)),
            (2048, self.horizontal_alignment.map(|v| v as u32)), (4096, self.vertical_alignment.map(|v| v as u32)),
            (4194304, self.maximum_lines), (8388608, self.wrapping.map(u32::from))] {
            if let Some(value) = value { record(property, 3, Default::default(), Default::default(), value as f64)?; }
        }
        if let Some(family) = &self.font_family {
            if family.is_empty() || family.len() > 1024 || family.contains('\0') { return Err(invalid("Invalid font family.")); }
            if allowed & 128 == 0 { return Err(invalid("Unsupported font family on this part.")); }
            output.push(sys::StyleProperty { size: size_of::<sys::StyleProperty>() as u32, version: 0x10000,
                property: 128, value_type: 4, part: part as u32, state,
                text: sys::Text { data: family.as_ptr(), length: family.len() as u32, reserved: 0 }, ..Default::default() });
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
                128 => {
                    if r.text.data.is_null() || r.text.length > 1024 { return Err(invalid("Invalid native font family.")); }
                    let bytes = unsafe { std::slice::from_raw_parts(r.text.data, r.text.length as usize) };
                    result.font_family = Some(std::str::from_utf8(bytes).map_err(|_| invalid("Invalid native UTF-8."))?.into());
                },
                256 => result.font_size = Some(r.number as f32),
                512 => result.font_weight = Some(r.number as u32),
                1024 => result.font_style = Some(match r.number as u32 { 0 => StyleFontStyle::Normal,
                    1 => StyleFontStyle::Italic, 2 => StyleFontStyle::Oblique, _ => return Err(invalid("Unknown native font style.")) }),
                2048 | 4096 => {
                    let value = Some(match r.number as u32 { 0 => StyleAlignment::Start, 1 => StyleAlignment::Center,
                        2 => StyleAlignment::End, 3 => StyleAlignment::Stretch, _ => return Err(invalid("Unknown native alignment.")) });
                    if r.property == 2048 { result.horizontal_alignment = value; } else { result.vertical_alignment = value; }
                },
                8192 => result.spacing = Some(r.number as f32),
                16384 => result.row_height = Some(r.number as f32),
                32768 => result.header_height = Some(r.number as f32),
                65536 => result.indentation = Some(r.number as f32),
                131072 => result.thickness = Some(r.number as f32),
                262144 => result.width = Some(r.number as f32),
                524288 => result.height = Some(r.number as f32),
                1048576 => result.row_gap = Some(r.number as f32),
                2097152 => result.column_gap = Some(r.number as f32),
                4194304 => result.maximum_lines = Some(r.number as u32),
                8388608 => result.wrapping = Some(r.number != 0.0),
                _ => return Err(invalid("Unknown native style property.")),
            }
        }
        Ok(result)
    }
}
#[derive(Clone, Debug, PartialEq)]
pub struct PartStyle {
    pub part: StylePart,
    pub values: PartStyleValues,
}
#[derive(Clone, Debug, PartialEq)]
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
        if !style_target_supported(target) { return Err(invalid("Unsupported style target.")); }
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
            p.values.append(p.part, 0, Some(target), &mut records)?;
        }
        for r in rules {
            if keys.contains(&(r.part, r.state as u64)) {
                return Err(invalid("Duplicate style state for this part."));
            }
            keys.push((r.part, r.state as u64));
            r.values.append(r.part, r.state as u64, Some(target), &mut records)?;
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
            p.values.append(p.part, 0, Some(self.0.target), &mut records)?;
        }
        for r in &self.0.rules {
            r.values.append(r.part, r.state as u64, Some(self.0.target), &mut records)?;
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
impl Window {
    pub fn set_tooltip_style(&self, style: Option<&ControlStyle>) -> Result<()> {
        let mut apply = |handle| self.0.check(unsafe { sys::xui_window_set_tooltip_style(self.0.handle, handle) });
        if let Some(style) = style {
            if style.target() != StyleTarget::Tooltip { return Err(invalid("Expected a Tooltip style.")); }
            if let Some(identity) = style.identity(&self.0) {
                let mut applied = 0;
                self.0.check(unsafe {
                    sys::xui_window_try_set_tooltip_style(self.0.handle, identity, &mut applied)
                })?;
                if applied != 0 { return Ok(()); }
            }
            style.with_native_handle(&self.0, &mut apply)
        } else {
            apply(0)
        }
    }
    pub fn set_tooltip_style_values(&self, part: StylePart, values: PartStyleValues) -> Result<()> {
        let mut records = Vec::new();
        values.append(part, 0, Some(StyleTarget::Tooltip), &mut records)?;
        self.0.check(unsafe {
            sys::xui_window_set_tooltip_style_values(self.0.handle, part as u32, records.as_ptr(), records.len() as u32)
        })
    }
    pub fn tooltip_style_values(&self, part: StylePart, effective: bool) -> Result<PartStyleValues> {
        let mut count = 0;
        self.0.check(unsafe {
            sys::xui_window_get_tooltip_style_values(self.0.handle, part as u32, effective as u32,
                std::ptr::null_mut(), 0, &mut count)
        })?;
        let mut records = vec![sys::StyleProperty::default(); count as usize];
        self.0.check(unsafe {
            sys::xui_window_get_tooltip_style_values(self.0.handle, part as u32, effective as u32,
                records.as_mut_ptr(), count, &mut count)
        })?;
        PartStyleValues::from_native(&records)
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
        values.append(part, 0, None, &mut records)?;
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
    fn row_height_requires_positive_dimension() {
        for height in [0.0, -0.0, -1.0, f32::NAN, f32::INFINITY, 32769.0] {
            let values = PartStyleValues { row_height: Some(height), ..Default::default() };
            assert!(values.append(StylePart::Root, 0, None, &mut Vec::new()).is_err());
        }
        for height in [0.5, 32768.0] {
            let values = PartStyleValues { row_height: Some(height), ..Default::default() };
            assert!(values.append(StylePart::Root, 0, None, &mut Vec::new()).is_ok());
        }
    }

    #[test]
    fn generic_style_schema_and_records() -> Result<()> {
        let values = PartStyleValues {
            size: Some(18.0),
            background: Some(ThemeColor::new(0, 0xffffff)),
            ..Default::default()
        };
        let mut records = Vec::new();
        values.append(StylePart::Indicator, 0, Some(StyleTarget::Toggle), &mut records)?;
        assert_eq!(PartStyleValues::from_native(&records)?, values);
        assert_eq!(records[0].color.dark, 0xffffff);
        assert!(values.append(StylePart::Label, 0, Some(StyleTarget::Toggle), &mut Vec::new()).is_err());
        assert!(
            PartStyleValues {
                size: Some(f32::NAN),
                ..Default::default()
            }
            .append(StylePart::Indicator, 0, Some(StyleTarget::Toggle), &mut Vec::new())
            .is_err()
        );
        let part = PartStyle {
            part: StylePart::Indicator,
            values,
        };
        assert!(ControlStyle::new(StyleTarget::Toggle, &[part.clone(), part.clone()], &[], None).is_err());
        let mut style = ControlStyle::new(StyleTarget::Toggle, &[part], &[], None)?;
        for _ in 1..16 {
            style = ControlStyle::new(StyleTarget::Toggle, &[], &[], Some(&style))?;
        }
        assert!(ControlStyle::new(StyleTarget::Toggle, &[], &[], Some(&style)).is_err());
        Ok(())
    }

    #[test]
    fn tooltip_native_lifecycle() -> Result<()> {
        let window = Window::new("Tooltip style", 300.0, 200.0)?;
        let style = ControlStyle::new(StyleTarget::Tooltip, &[
            PartStyle { part: StylePart::Root, values: PartStyleValues {
                background: Some(0x112233.into()), ..Default::default()
            }},
            PartStyle { part: StylePart::Text, values: PartStyleValues {
                font_family: Some("Segoe UI".into()), font_size: Some(18.0), ..Default::default()
            }},
        ], &[], None)?;
        window.set_tooltip_style(Some(&style))?;
        assert_eq!(window.tooltip_style_values(StylePart::Text, true)?.font_family.as_deref(), Some("Segoe UI"));
        window.set_tooltip_style_values(StylePart::Root, PartStyleValues {
            foreground: Some(0.into()), ..Default::default()
        })?;
        let wrong = ControlStyle::new(StyleTarget::Toggle, &[], &[], None)?;
        assert!(window.set_tooltip_style(Some(&wrong)).is_err());
        window.set_tooltip_style(None)?;
        assert_eq!(window.tooltip_style_values(StylePart::Root, true)?.foreground, Some(0.into()));
        window.set_tooltip_style(Some(&style))?;
        window.set_tooltip_style_values(StylePart::Root, PartStyleValues::default())?;
        assert_eq!(window.tooltip_style_values(StylePart::Root, true)?.background, Some(0x112233.into()));
        window.set_tooltip_style(None)?;
        assert_eq!(window.tooltip_style_values(StylePart::Root, true)?, PartStyleValues::default());
        let other = Window::new("Shared tooltip", 300.0, 200.0)?;
        other.set_tooltip_style(Some(&style))?;
        assert_eq!(other.tooltip_style_values(StylePart::Text, true)?.font_size, Some(18.0));
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
