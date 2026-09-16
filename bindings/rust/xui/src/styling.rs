use super::*;

/// Opaque 0xRRGGBB colors for the light and dark themes.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ThemeColor {
    pub light: u32,
    pub dark: u32,
}
impl ThemeColor {
    pub const fn new(light: u32, dark: u32) -> Self {
        Self { light, dark }
    }
    fn validate(self) -> Result<()> {
        if self.light > 0xffffff || self.dark > 0xffffff {
            return Err(invalid("Colors must be 0xRRGGBB values."));
        }
        Ok(())
    }
}
impl From<u32> for ThemeColor {
    fn from(color: u32) -> Self {
        Self::new(color, color)
    }
}

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Insets {
    pub left: f32,
    pub top: f32,
    pub right: f32,
    pub bottom: f32,
}
impl Insets {
    pub const fn uniform(value: f32) -> Self {
        Self {
            left: value,
            top: value,
            right: value,
            bottom: value,
        }
    }
}

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct ButtonStyleValues {
    pub background: Option<ThemeColor>,
    pub foreground: Option<ThemeColor>,
    pub border_brush: Option<ThemeColor>,
    pub border_thickness: Option<Insets>,
    pub padding: Option<Insets>,
    pub corner_radius: Option<f32>,
}
impl ButtonStyleValues {
    fn native(self) -> Result<sys::ButtonStyleValues> {
        let mut value = sys::ButtonStyleValues {
            size: size_of::<sys::ButtonStyleValues>() as u32,
            version: 0x10000,
            ..Default::default()
        };
        for (bit, color, target) in [
            (1, self.background, &mut value.background),
            (2, self.foreground, &mut value.foreground),
            (4, self.border_brush, &mut value.border_brush),
        ] {
            if let Some(color) = color {
                color.validate()?;
                value.mask |= bit;
                *target = sys::ThemeColor {
                    light: color.light,
                    dark: color.dark,
                };
            }
        }
        fn dimension(value: f32) -> Result<()> {
            if !value.is_finite() || !(0.0..=32768.0).contains(&value) {
                return Err(invalid(
                    "Style dimensions must be finite and between 0 and 32768 DIPs.",
                ));
            }
            Ok(())
        }
        for (bit, edges, target) in [
            (8, self.border_thickness, &mut value.border_thickness),
            (16, self.padding, &mut value.padding),
        ] {
            if let Some(edges) = edges {
                for edge in [edges.left, edges.top, edges.right, edges.bottom] {
                    dimension(edge)?;
                }
                value.mask |= bit;
                *target = sys::StyleInsets {
                    left: edges.left,
                    top: edges.top,
                    right: edges.right,
                    bottom: edges.bottom,
                };
            }
        }
        if let Some(radius) = self.corner_radius {
            dimension(radius)?;
            value.mask |= 32;
            value.corner_radius = radius;
        }
        Ok(value)
    }
    fn from_native(v: sys::ButtonStyleValues) -> Self {
        fn color(v: sys::ThemeColor) -> ThemeColor {
            ThemeColor::new(v.light, v.dark)
        }
        fn insets(v: sys::StyleInsets) -> Insets {
            Insets {
                left: v.left,
                top: v.top,
                right: v.right,
                bottom: v.bottom,
            }
        }
        Self {
            background: (v.mask & 1 != 0).then(|| color(v.background)),
            foreground: (v.mask & 2 != 0).then(|| color(v.foreground)),
            border_brush: (v.mask & 4 != 0).then(|| color(v.border_brush)),
            border_thickness: (v.mask & 8 != 0).then(|| insets(v.border_thickness)),
            padding: (v.mask & 16 != 0).then(|| insets(v.padding)),
            corner_radius: (v.mask & 32 != 0).then_some(v.corner_radius),
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u32)]
pub enum ButtonStyleState {
    Focused,
    Checked,
    Hovered,
    Pressed,
    Disabled,
}
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct ButtonStyleRule {
    pub state: ButtonStyleState,
    pub values: ButtonStyleValues,
}
struct StyleDefinition {
    values: ButtonStyleValues,
    rules: Vec<ButtonStyleRule>,
    based_on: Option<ButtonStyle>,
    depth: usize,
    identities: RefCell<Vec<(Weak<Inner>, u64)>>,
}
/// An immutable definition with native handles scoped to each application.
#[derive(Clone)]
pub struct ButtonStyle(Rc<StyleDefinition>);
impl ButtonStyle {
    pub fn new(
        values: ButtonStyleValues,
        rules: &[ButtonStyleRule],
        based_on: Option<&ButtonStyle>,
    ) -> Result<Self> {
        values.native()?;
        if rules.len() > 256 {
            return Err(invalid("A Button style supports at most 256 rules."));
        }
        for rule in rules {
            rule.values.native()?;
        }
        let depth = based_on.map_or(1, |base| base.0.depth + 1);
        if depth > 16 {
            return Err(invalid("Button style inheritance exceeds 16 layers."));
        }
        Ok(Self(Rc::new(StyleDefinition {
            values,
            rules: rules.to_vec(),
            based_on: based_on.cloned(),
            depth,
            identities: RefCell::new(Vec::new()),
        })))
    }
    pub fn values(&self) -> ButtonStyleValues {
        self.0.values
    }
    pub fn rules(&self) -> &[ButtonStyleRule] {
        &self.0.rules
    }
    pub fn based_on(&self) -> Option<&ButtonStyle> {
        self.0.based_on.as_ref()
    }
    fn identity(&self, owner: &Rc<Inner>) -> Option<u64> {
        let mut identities = self.0.identities.borrow_mut();
        identities.retain(|(window, _)| window.strong_count() != 0);
        identities
            .iter()
            .find(|(window, _)| window.ptr_eq(&Rc::downgrade(owner)))
            .map(|(_, identity)| *identity)
    }
    fn remember(&self, owner: &Rc<Inner>, identity: u64) {
        let mut identities = self.0.identities.borrow_mut();
        if let Some((_, cached)) = identities
            .iter_mut()
            .find(|(window, _)| window.ptr_eq(&Rc::downgrade(owner)))
        {
            *cached = identity;
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
                sys::xui_button_style_reacquire(owner.handle, identity, &mut handle)
            })?;
            if handle != 0 {
                let applied = apply(handle);
                let released = owner.check(unsafe { sys::xui_button_style_release(handle) });
                return applied.and(released);
            }
        }
        let rules = self
            .0
            .rules
            .iter()
            .map(|rule| {
                Ok(sys::ButtonStyleRule {
                    size: size_of::<sys::ButtonStyleRule>() as u32,
                    state: rule.state as u32,
                    values: rule.values.native()?,
                })
            })
            .collect::<Result<Vec<_>>>()?;
        let values = self.0.values.native()?;
        let mut create = |base| {
            let options = sys::ButtonStyleOptions {
                size: size_of::<sys::ButtonStyleOptions>() as u32,
                version: 0x10000,
                values,
                rules: rules.as_ptr(),
                rule_count: rules.len() as u32,
                reserved: 0,
                based_on: base,
            };
            let mut handle = 0;
            owner.check(unsafe {
                sys::xui_button_style_create(owner.handle, &options, &mut handle)
            })?;
            self.remember(owner, handle);
            let applied = apply(handle);
            let released = owner.check(unsafe { sys::xui_button_style_release(handle) });
            applied.and(released)
        };
        if let Some(base) = &self.0.based_on {
            base.with_native_handle(owner, &mut create)
        } else {
            create(0)
        }
    }
}
impl Button {
    pub fn set_style(&self, style: Option<&ButtonStyle>) -> Result<()> {
        let mut apply = |handle| {
            self.owner
                .check(unsafe { sys::xui_button_set_style(self.handle, handle) })
        };
        if let Some(style) = style {
            if let Some(identity) = style.identity(&self.owner) {
                let mut applied = 0;
                self.owner.check(unsafe {
                    sys::xui_button_try_set_style(self.handle, identity, &mut applied)
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
    pub fn set_style_values(&self, values: ButtonStyleValues) -> Result<()> {
        let value = values.native()?;
        self.owner
            .check(unsafe { sys::xui_button_set_style_values(self.handle, &value) })
    }
    pub fn style_values(&self) -> Result<ButtonStyleValues> {
        self.read_style_values(false)
    }
    pub fn effective_style_values(&self) -> Result<ButtonStyleValues> {
        self.read_style_values(true)
    }
    fn read_style_values(&self, effective: bool) -> Result<ButtonStyleValues> {
        let mut value = ButtonStyleValues::default().native()?;
        self.owner.check(unsafe {
            sys::xui_button_get_style_values(self.handle, effective as u32, &mut value)
        })?;
        Ok(ButtonStyleValues::from_native(value))
    }
}

#[derive(Clone, Debug)]
pub enum ColorResource {
    Color(ThemeColor),
    Alias(String),
}
impl From<ThemeColor> for ColorResource {
    fn from(value: ThemeColor) -> Self {
        Self::Color(value)
    }
}
impl From<&str> for ColorResource {
    fn from(value: &str) -> Self {
        Self::Alias(value.to_owned())
    }
}
struct ScopeDefinition {
    colors: HashMap<String, ThemeColor>,
    parent: Option<ResourceScope>,
    depth: usize,
}
#[derive(Clone)]
pub struct ResourceScope(Rc<ScopeDefinition>);
impl ResourceScope {
    pub fn new(
        entries: impl IntoIterator<Item = (String, ColorResource)>,
        parent: Option<&ResourceScope>,
    ) -> Result<Self> {
        let depth = parent.map_or(1, |p| p.0.depth + 1);
        if depth > 16 {
            return Err(invalid("Resource scopes exceed 16 layers."));
        }
        let mut source = HashMap::new();
        for (name, value) in entries {
            Self::name(&name)?;
            if source.len() == 256 {
                return Err(invalid("A resource scope supports at most 256 entries."));
            }
            match &value {
                ColorResource::Color(color) => color.validate()?,
                ColorResource::Alias(alias) => Self::name(alias)?,
            }
            if source.insert(name, value).is_some() {
                return Err(invalid("Duplicate resource name."));
            }
        }
        fn resolve(
            name: &str,
            source: &HashMap<String, ColorResource>,
            parent: Option<&ResourceScope>,
            resolved: &mut HashMap<String, ThemeColor>,
            visiting: &mut std::collections::HashSet<String>,
        ) -> Result<ThemeColor> {
            if let Some(color) = resolved.get(name) {
                return Ok(*color);
            }
            let Some(value) = source.get(name) else {
                return parent
                    .ok_or_else(|| invalid("Color resource was not found."))?
                    .resolve(name);
            };
            if !visiting.insert(name.to_owned()) {
                return Err(invalid("Color resource aliases contain a cycle."));
            }
            let color = match value {
                ColorResource::Color(color) => *color,
                ColorResource::Alias(alias) => resolve(alias, source, parent, resolved, visiting)?,
            };
            visiting.remove(name);
            resolved.insert(name.to_owned(), color);
            Ok(color)
        }
        let mut colors = HashMap::new();
        let mut visiting = std::collections::HashSet::new();
        for name in source.keys() {
            resolve(name, &source, parent, &mut colors, &mut visiting)?;
        }
        Ok(Self(Rc::new(ScopeDefinition {
            colors,
            parent: parent.cloned(),
            depth,
        })))
    }
    fn name(name: &str) -> Result<()> {
        if name.is_empty() || name.chars().count() > 1024 || name.contains('\0') {
            return Err(invalid(
                "Resource names must contain 1 to 1024 characters without NUL.",
            ));
        }
        Ok(())
    }
    pub fn resolve(&self, name: &str) -> Result<ThemeColor> {
        Self::name(name)?;
        if let Some(color) = self.0.colors.get(name) {
            return Ok(*color);
        }
        self.0
            .parent
            .as_ref()
            .ok_or_else(|| invalid("Color resource was not found."))?
            .resolve(name)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn immutable_definitions_and_resources() -> Result<()> {
        let parent = ResourceScope::new(
            [("accent".into(), ThemeColor::new(0x123456, 0x654321).into())],
            None,
        )?;
        let scope = ResourceScope::new([("alias".into(), "accent".into())], Some(&parent))?;
        assert_eq!(scope.resolve("alias")?, ThemeColor::new(0x123456, 0x654321));
        assert!(scope.resolve("missing").is_err());
        assert!(
            ResourceScope::new([("a".into(), "b".into()), ("b".into(), "a".into())], None).is_err()
        );
        assert!(ResourceScope::new([("a".into(), "missing".into())], None).is_err());
        assert!(
            ResourceScope::new(
                [("a".into(), 0.into()), ("a".into(), 0.into())]
                    .map(|(k, v): (String, ThemeColor)| (k, v.into())),
                None
            )
            .is_err()
        );
        let bad = ButtonStyleValues {
            corner_radius: Some(f32::NAN),
            ..Default::default()
        };
        assert!(ButtonStyle::new(bad, &[], None).is_err());
        let bad = ButtonStyleValues {
            background: Some(0xff000000.into()),
            ..Default::default()
        };
        assert!(ButtonStyle::new(bad, &[], None).is_err());
        let mut base = ButtonStyle::new(ButtonStyleValues::default(), &[], None)?;
        for _ in 1..16 {
            base = ButtonStyle::new(ButtonStyleValues::default(), &[], Some(&base))?;
        }
        assert!(ButtonStyle::new(ButtonStyleValues::default(), &[], Some(&base)).is_err());
        Ok(())
    }
    #[test]
    fn button_styles_preserve_locals_and_share_across_windows() -> Result<()> {
        let values = ButtonStyleValues {
            background: Some(0x123456.into()),
            corner_radius: Some(4.0),
            ..Default::default()
        };
        let style = ButtonStyle::new(values, &[], None)?;
        let w = Window::new("Styles", 300., 200.)?;
        let button = w.button("Styled")?;
        let local = ButtonStyleValues {
            corner_radius: Some(7.),
            ..Default::default()
        };
        button.set_style_values(local)?;
        button.set_style(Some(&style))?;
        assert_eq!(button.style_values()?, local);
        assert_eq!(button.effective_style_values()?.corner_radius, Some(7.));
        assert_eq!(
            button.effective_style_values()?.background,
            values.background
        );
        assert!(
            button
                .set_style_values(ButtonStyleValues {
                    corner_radius: Some(-1.),
                    ..Default::default()
                })
                .is_err()
        );
        assert_eq!(button.style_values()?, local);
        button.set_style(None)?;
        assert_eq!(button.effective_style_values()?, local);
        button.set_style_values(ButtonStyleValues::default())?;
        assert_eq!(
            button.effective_style_values()?,
            ButtonStyleValues::default()
        );
        let other = Window::new("Other", 300., 200.)?;
        let second = other.button("Shared")?;
        second.set_style(Some(&style))?;
        assert_eq!(second.effective_style_values()?, values);
        let weak = other.downgrade();
        drop(other);
        assert!(weak.upgrade().is_some());
        drop(style);
        assert_eq!(second.effective_style_values()?, values);
        drop(second);
        assert!(weak.upgrade().is_none());
        Ok(())
    }

    #[test]
    fn native_layouts() {
        assert_eq!(size_of::<sys::ThemeColor>(), 8);
        assert_eq!(size_of::<sys::StyleInsets>(), 16);
        assert_eq!(size_of::<sys::ButtonStyleValues>(), 80);
        assert_eq!(size_of::<sys::ButtonStyleRule>(), 88);
        assert_eq!(size_of::<sys::ButtonStyleOptions>(), 112);
        assert_eq!(std::mem::offset_of!(sys::ButtonStyleOptions, rules), 88);
        assert_eq!(std::mem::offset_of!(sys::ButtonStyleOptions, based_on), 104);
    }

    #[test]
    fn shared_definitions_and_same_assignment() -> Result<()> {
        let w = Window::new("Shared definitions", 300., 200.)?;
        let buttons = (0..256)
            .map(|_| w.button("Shared"))
            .collect::<Result<Vec<_>>>()?;
        let values = ButtonStyleValues {
            background: Some(0x123456.into()),
            padding: Some(Insets::uniform(3.)),
            ..Default::default()
        };
        let parent = ButtonStyle::new(values, &[], None)?;
        let style = ButtonStyle::new(
            ButtonStyleValues {
                corner_radius: Some(4.),
                ..Default::default()
            },
            &[],
            Some(&parent),
        )?;
        buttons[0].set_style(Some(&style))?;
        let identity = style.identity(&w.0).unwrap();
        let cloned = style.clone();
        for _ in 0..32 {
            for button in &buttons {
                button.set_style(Some(&cloned))?;
                assert_eq!(
                    button.effective_style_values()?,
                    ButtonStyleValues {
                        corner_radius: Some(4.),
                        ..values
                    }
                );
                assert_eq!(style.identity(&w.0), Some(identity));
            }
        }
        buttons[0].set_style(None)?;
        buttons[0].set_style(Some(&style))?;
        assert_eq!(style.identity(&w.0), Some(identity));

        println!(
            "Rust shared style: 256 buttons, 8192 updates, unchanged native definition identity."
        );

        let distinct = ButtonStyle::new(style.values(), &[], Some(&parent))?;
        buttons[0].set_style(Some(&distinct))?;
        assert_ne!(distinct.identity(&w.0), Some(identity));
        for button in &buttons {
            button.set_style(None)?;
        }
        buttons[0].set_style(Some(&style))?;
        assert_ne!(style.identity(&w.0), Some(identity));
        buttons[0].set_style(None)?;

        let representative = w.button("Dropped representative")?;
        representative.set_style(Some(&style))?;
        let identity = style.identity(&w.0);
        drop(representative);
        buttons[0].set_style(Some(&style))?;
        assert_eq!(style.identity(&w.0), identity);
        let other = Window::new("Other", 300., 200.)?;
        other.button("Shared")?.set_style(Some(&style))?;
        assert_ne!(style.identity(&other.0), identity);
        let weak = other.downgrade();
        drop(other);
        assert!(weak.upgrade().is_none());
        assert_eq!(style.identity(&w.0), identity);
        assert_eq!(style.0.identities.borrow().len(), 1);
        Ok(())
    }

    #[test]
    fn inherited_states_and_local_precedence() -> Result<()> {
        let w = Window::new("Style states", 300., 200.)?;
        let values = ButtonStyleValues {
            background: Some(ThemeColor::new(0x123456, 0x654321)),
            foreground: Some(0x101010.into()),
            border_brush: Some(0x202020.into()),
            border_thickness: Some(Insets {
                left: 1.,
                top: 2.,
                right: 3.,
                bottom: 4.,
            }),
            padding: Some(Insets {
                left: 4.,
                top: 3.,
                right: 2.,
                bottom: 1.,
            }),
            corner_radius: Some(2.),
        };
        let base = ButtonStyle::new(
            values,
            &[
                ButtonStyleRule {
                    state: ButtonStyleState::Checked,
                    values: ButtonStyleValues {
                        background: Some(0x333333.into()),
                        padding: Some(Insets::uniform(6.)),
                        ..Default::default()
                    },
                },
                ButtonStyleRule {
                    state: ButtonStyleState::Disabled,
                    values: ButtonStyleValues {
                        background: Some(0x444444.into()),
                        ..Default::default()
                    },
                },
            ],
            None,
        )?;
        let derived = ButtonStyle::new(
            ButtonStyleValues {
                corner_radius: Some(5.),
                ..Default::default()
            },
            &[
                ButtonStyleRule {
                    state: ButtonStyleState::Checked,
                    values: ButtonStyleValues {
                        foreground: Some(0x555555.into()),
                        ..Default::default()
                    },
                },
                ButtonStyleRule {
                    state: ButtonStyleState::Checked,
                    values: ButtonStyleValues {
                        foreground: Some(0x666666.into()),
                        ..Default::default()
                    },
                },
                ButtonStyleRule {
                    state: ButtonStyleState::Disabled,
                    values: ButtonStyleValues {
                        border_brush: Some(0x777777.into()),
                        ..Default::default()
                    },
                },
            ],
            Some(&base),
        )?;
        let button = w.button("States")?;
        button.behavior(ButtonBehavior::Toggle)?;
        button.set_style(Some(&derived))?;
        let normal = ButtonStyleValues {
            corner_radius: Some(5.),
            ..values
        };
        assert_eq!(button.effective_style_values()?, normal);
        button.invoke()?;
        assert!(button.is_checked()?);
        let selected = ButtonStyleValues {
            background: Some(0x333333.into()),
            foreground: Some(0x666666.into()),
            padding: Some(Insets::uniform(6.)),
            ..normal
        };
        assert_eq!(button.effective_style_values()?, selected);
        button.enabled(false)?;
        let disabled = ButtonStyleValues {
            background: Some(0x444444.into()),
            border_brush: Some(0x777777.into()),
            ..selected
        };
        assert_eq!(button.effective_style_values()?, disabled);
        button.set_style_values(ButtonStyleValues {
            background: Some(0.into()),
            corner_radius: Some(0.),
            ..Default::default()
        })?;
        let local = ButtonStyleValues {
            background: Some(0.into()),
            corner_radius: Some(0.),
            ..disabled
        };
        assert_eq!(button.effective_style_values()?, local);
        w.set_theme(Theme::HighContrast)?;
        assert_eq!(button.effective_style_values()?, local);
        button.set_style_values(ButtonStyleValues::default())?;
        button.enabled(true)?;
        button.invoke()?;
        assert_eq!(button.effective_style_values()?, normal);
        Ok(())
    }

    // The ABI has no handle counter. Wrong-thread tokens belong to concurrent tests.
    fn live_style_handles(first: u64, last: u64) -> usize {
        (first + 1..last)
            .filter(|&handle| {
                let status =
                    unsafe { sys::xui_button_get_style_values(handle, 0, std::ptr::null_mut()) };
                assert!(
                    status == 2 || status == 3 || status == 4,
                    "Unexpected status {status} for {handle}"
                );
                status == 3
            })
            .count()
    }

    #[test]
    fn bounded_native_handle_lifetime() -> Result<()> {
        let w = Window::new("Style lifetime", 300., 200.)?;
        let button = w.button("Lifetime")?;
        let first = button.handle;
        for i in 0..512 {
            let base = ButtonStyle::new(
                ButtonStyleValues {
                    background: Some(i.into()),
                    ..Default::default()
                },
                &[],
                None,
            )?;
            let style = ButtonStyle::new(
                ButtonStyleValues {
                    corner_radius: Some(3.),
                    ..Default::default()
                },
                &[],
                Some(&base),
            )?;
            button.set_style(Some(&style))?;
            let weak = Rc::downgrade(&style.0);
            drop(style);
            drop(base);
            assert!(weak.upgrade().is_none());
            assert_eq!(
                button.effective_style_values()?,
                ButtonStyleValues {
                    background: Some(i.into()),
                    corner_radius: Some(3.),
                    ..Default::default()
                }
            );
            button.set_style(None)?;
            button.set_style_values(ButtonStyleValues {
                padding: Some(Insets::uniform(1.)),
                ..Default::default()
            })?;
            button.set_style_values(ButtonStyleValues::default())?;
            assert_eq!(
                button.effective_style_values()?,
                ButtonStyleValues::default()
            );
        }
        let last = w.button("End of lifetime interval")?.handle;
        let live = live_style_handles(first, last);
        println!(
            "Rust native style handles: {} issued, {live} live after 512 apply/clear/drop cycles.",
            last - first - 1
        );
        assert_eq!(live, 0);
        assert!(last - first > 1024);

        let shared = ButtonStyle::new(
            ButtonStyleValues {
                padding: Some(Insets::uniform(2.)),
                ..Default::default()
            },
            &[],
            None,
        )?;
        for _ in 0..16 {
            let other = Window::new("Shared", 300., 200.)?;
            other.button("Shared")?.set_style(Some(&shared))?;
            button.set_style(Some(&shared))?;
            drop(other);
            assert_eq!(button.effective_style_values()?, shared.values());
            button.set_style(None)?;
        }
        Ok(())
    }

    #[test]
    fn failed_application_releases_all_inheritance_handles() -> Result<()> {
        let w = Window::new("Style failure", 300., 200.)?;
        let label = w.label("Not a button")?;
        let invalid_button = Button(Element {
            owner: w.0.clone(),
            handle: label.handle,
        });
        let first = label.handle;
        let mut style = ButtonStyle::new(ButtonStyleValues::default(), &[], None)?;
        for _ in 1..16 {
            style = ButtonStyle::new(ButtonStyleValues::default(), &[], Some(&style))?;
        }
        for _ in 0..32 {
            let error = invalid_button.set_style(Some(&style)).unwrap_err();
            assert_eq!(error.status, 3);
            assert!(error.message.contains("Wrong handle kind"));
        }
        let button = w.button("Retry")?;
        assert_eq!(live_style_handles(first, button.handle), 0);
        button.set_style(Some(&style))?;
        assert_eq!(
            button.effective_style_values()?,
            ButtonStyleValues::default()
        );
        Ok(())
    }

    struct MutationSource {
        owner: Weak<Inner>,
        button: u64,
        style: ButtonStyle,
        calls: Rc<std::cell::Cell<usize>>,
    }
    impl ReadOnlyImmutableSource for MutationSource {
        fn count(&self) -> u64 {
            1
        }
        fn key(&self, _index: u64) -> Result<ItemKey> {
            Ok(ItemKey { id: 1, version: 0 })
        }
        fn find(&self, key: ItemKey) -> Result<Option<u64>> {
            self.calls.set(self.calls.get() + 1);
            let button = Button(Element {
                owner: self.owner.upgrade().unwrap(),
                handle: self.button,
            });
            let fresh = ButtonStyle::new(ButtonStyleValues::default(), &[], Some(&self.style))?;
            for style in [Some(&fresh), Some(&self.style), None] {
                assert_eq!(button.set_style(style).unwrap_err().status, 7);
            }
            assert_eq!(
                button
                    .set_style_values(ButtonStyleValues::default())
                    .unwrap_err()
                    .status,
                7
            );
            Ok((key.id == 1 && key.version == 0).then_some(0))
        }
        fn item(&self, _index: u64, _column: u64) -> Result<ItemContent> {
            Ok(ItemContent {
                primary: "Row".into(),
                secondary: String::new(),
                enabled: true,
                progress: None,
                checked: None,
            })
        }
    }

    #[test]
    fn failed_mutations_preserve_values_and_allow_retry() -> Result<()> {
        let w = Window::new("Failure atomicity", 300., 200.)?;
        let button = w.button("Styled")?;
        let values = ButtonStyleValues {
            background: Some(0x123456.into()),
            ..Default::default()
        };
        let style = ButtonStyle::new(values, &[], None)?;
        let local = ButtonStyleValues {
            corner_radius: Some(3.),
            ..Default::default()
        };
        button.set_style(Some(&style))?;
        button.set_style_values(local)?;
        let calls = Rc::new(std::cell::Cell::new(0));
        let source = w.immutable_source(MutationSource {
            owner: Rc::downgrade(&w.0),
            button: button.handle,
            style: style.clone(),
            calls: calls.clone(),
        })?;
        let items = w.items_view("Callback")?;
        items.set_source(&source)?;
        items.select(ItemKey { id: 1, version: 0 })?;
        assert!(calls.get() > 0);
        assert_eq!(button.style_values()?, local);
        assert_eq!(
            button.effective_style_values()?,
            ButtonStyleValues {
                corner_radius: Some(3.),
                ..values
            }
        );
        button.set_style(Some(&style))?;
        button.set_style(None)?;
        assert_eq!(button.effective_style_values()?, local);
        Ok(())
    }

    #[test]
    fn owner_drop_revokes_handles_without_retaining_windows() -> Result<()> {
        let style = ButtonStyle::new(
            ButtonStyleValues {
                padding: Some(Insets::uniform(2.)),
                ..Default::default()
            },
            &[],
            None,
        )?;
        let (window, button, weak) = {
            let w = Window::new("Disposed", 300., 200.)?;
            let button = w.button("Styled")?;
            button.set_style(Some(&style))?;
            (w.0.handle, button.handle, w.downgrade())
        };
        assert!(weak.upgrade().is_none());
        assert_eq!(unsafe { sys::xui_button_set_style(button, 0) }, 2);
        let mut values = ButtonStyleValues::default().native()?;
        assert_eq!(
            unsafe { sys::xui_button_get_style_values(button, 1, &mut values) },
            2
        );
        let options = sys::ButtonStyleOptions {
            size: size_of::<sys::ButtonStyleOptions>() as u32,
            version: 0x10000,
            values,
            ..Default::default()
        };
        let mut handle = 99;
        assert_eq!(
            unsafe { sys::xui_button_style_create(window, &options, &mut handle) },
            2
        );
        assert_eq!(handle, 0);
        let replacement = Window::new("Replacement", 300., 200.)?;
        let second = replacement.button("Styled again")?;
        second.set_style(Some(&style))?;
        assert_eq!(second.effective_style_values()?, style.values());
        Ok(())
    }
}
