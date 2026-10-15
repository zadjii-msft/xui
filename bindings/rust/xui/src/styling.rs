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
    pub(super) fn validate(self) -> Result<()> {
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
    fn immutable_resources() -> Result<()> {
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
        assert!(
            ResourceScope::new([("bad".into(), ThemeColor::from(0xff000000).into())], None)
                .is_err()
        );
        let child = ResourceScope::new(
            [("accent".into(), ThemeColor::from(0).into())],
            Some(&scope),
        )?;
        assert_eq!(child.resolve("accent")?, 0.into());
        assert_eq!(child.resolve("alias")?, ThemeColor::new(0x123456, 0x654321));
        let mut deepest = parent.clone();
        for _ in 1..16 {
            deepest = ResourceScope::new([], Some(&deepest))?;
        }
        assert!(ResourceScope::new([], Some(&deepest)).is_err());
        assert_eq!(
            parent.resolve("accent")?,
            ThemeColor::new(0x123456, 0x654321)
        );
        Ok(())
    }
}
