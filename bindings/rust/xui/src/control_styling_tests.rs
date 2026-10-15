use super::*;

fn root_style(
    values: PartStyleValues,
    rules: &[ControlStyleRule],
    based_on: Option<&ControlStyle>,
) -> Result<ControlStyle> {
    ControlStyle::new(
        StyleTarget::Button,
        &[PartStyle {
            part: StylePart::Root,
            values,
        }],
        rules,
        based_on,
    )
}

#[test]
fn immutable_definitions_and_validation() -> Result<()> {
    for values in [
        PartStyleValues {
            corner_radius: Some(f32::NAN),
            ..Default::default()
        },
        PartStyleValues {
            background: Some(0xff000000.into()),
            ..Default::default()
        },
        PartStyleValues {
            padding: Some(Insets::uniform(-1.0)),
            ..Default::default()
        },
    ] {
        assert!(root_style(values, &[], None).is_err());
    }
    let scope = ResourceScope::new(
        [("accent".into(), ThemeColor::new(0x123456, 0x654321).into())],
        None,
    )?;
    let mut parts = [PartStyle {
        part: StylePart::Root,
        values: PartStyleValues {
            background: Some(scope.resolve("accent")?),
            font_family: Some("Segoe UI".into()),
            ..Default::default()
        },
    }];
    let mut rules = [ControlStyleRule {
        part: StylePart::Root,
        state: StyleState::Checked,
        values: PartStyleValues {
            foreground: Some(0.into()),
            ..Default::default()
        },
    }];
    let mut base = ControlStyle::new(StyleTarget::Button, &parts, &rules, None)?;
    parts[0].values.background = Some(0xffffff.into());
    rules[0].values.foreground = Some(0xffffff.into());
    assert_eq!(
        base.parts()[0].values.background,
        Some(scope.resolve("accent")?)
    );
    assert_eq!(base.rules()[0].values.foreground, Some(0.into()));
    assert_eq!(
        base.parts()[0].values.font_family.as_deref(),
        Some("Segoe UI")
    );
    assert!(base.based_on().is_none());
    for _ in 1..16 {
        let derived = root_style(PartStyleValues::default(), &[], Some(&base))?;
        assert!(Rc::ptr_eq(&derived.based_on().unwrap().0, &base.0));
        base = derived;
    }
    assert!(root_style(PartStyleValues::default(), &[], Some(&base)).is_err());
    Ok(())
}

#[test]
fn native_layouts() {
    assert_eq!(size_of::<sys::ThemeColor>(), 8);
    assert_eq!(size_of::<sys::StyleInsets>(), 16);
    assert_eq!(size_of::<sys::StyleProperty>(), 80);
    assert_eq!(size_of::<sys::ControlStyleOptions>(), 40);
    assert_eq!(std::mem::offset_of!(sys::StyleProperty, state), 24);
    assert_eq!(std::mem::offset_of!(sys::StyleProperty, text), 64);
    assert_eq!(
        std::mem::offset_of!(sys::ControlStyleOptions, properties),
        16
    );
    assert_eq!(std::mem::offset_of!(sys::ControlStyleOptions, based_on), 32);
}

#[test]
fn button_and_toggle_variants_share_element_styling() -> Result<()> {
    let w = Window::new("Shared Element styling", 300., 200.)?;
    let button = w.button("Button")?;
    let toggle_button = w.toggle_button("Toggle button")?;
    let hyperlink = w.hyperlink_button("Hyperlink")?;
    let toggle = w.toggle("Toggle")?;
    let switch = w.toggle_switch("Switch")?;
    let checkbox = w.check_box("Checkbox")?;
    let values = PartStyleValues {
        foreground: Some(ThemeColor::new(0x123456, 0x654321)),
        font_family: Some("Segoe UI".into()),
        font_size: Some(18.),
        ..Default::default()
    };
    let button_style = root_style(values.clone(), &[], None)?;
    let toggle_style = ControlStyle::new(
        StyleTarget::Toggle,
        &[PartStyle {
            part: StylePart::Root,
            values: values.clone(),
        }],
        &[],
        None,
    )?;
    let local = PartStyleValues {
        foreground: Some(0.into()),
        ..Default::default()
    };
    for (elements, style) in [
        ([&button.0, &toggle_button.0, &hyperlink.0], &button_style),
        ([&toggle.0, &switch.0, &checkbox.0], &toggle_style),
    ] {
        for element in elements {
            let handle = element.handle;
            element.set_control_style(Some(style))?;
            assert_eq!(
                element.control_style_values(StylePart::Label, true)?,
                values
            );
            element.set_control_style_values(StylePart::Label, local.clone())?;
            assert_eq!(
                element.control_style_values(StylePart::Label, false)?,
                local
            );
            assert_eq!(
                element
                    .control_style_values(StylePart::Label, true)?
                    .foreground,
                Some(0.into())
            );
            element.set_control_style(None)?;
            assert_eq!(element.control_style_values(StylePart::Label, true)?, local);
            element.set_control_style_values(StylePart::Label, PartStyleValues::default())?;
            assert_eq!(
                element.control_style_values(StylePart::Label, true)?,
                PartStyleValues::default()
            );
            assert_eq!(element.handle, handle);
        }
    }
    toggle_button.invoke()?;
    assert!(toggle_button.checked()?);
    switch.invoke()?;
    assert!(switch.checked()?);
    checkbox.invoke()?;
    assert_eq!(checkbox.state()?, CheckState::Checked);
    Ok(())
}

#[test]
fn button_styles_preserve_locals_and_share_across_windows() -> Result<()> {
    let values = PartStyleValues {
        background: Some(0x123456.into()),
        corner_radius: Some(4.0),
        ..Default::default()
    };
    let style = root_style(values.clone(), &[], None)?;
    let w = Window::new("Styles", 300., 200.)?;
    let button = w.button("Styled")?;
    let local = PartStyleValues {
        corner_radius: Some(7.),
        ..Default::default()
    };
    button.set_control_style_values(StylePart::Root, local.clone())?;
    button.set_control_style(Some(&style))?;
    assert_eq!(button.control_style_values(StylePart::Root, false)?, local);
    assert_eq!(
        button
            .control_style_values(StylePart::Root, true)?
            .corner_radius,
        Some(7.)
    );
    assert_eq!(
        button
            .control_style_values(StylePart::Root, true)?
            .background,
        values.background
    );
    assert!(
        button
            .set_control_style_values(
                StylePart::Root,
                PartStyleValues {
                    corner_radius: Some(-1.),
                    ..Default::default()
                }
            )
            .is_err()
    );
    assert_eq!(button.control_style_values(StylePart::Root, false)?, local);
    let error = button
        .set_control_style_values(
            StylePart::Root,
            PartStyleValues {
                size: Some(20.),
                ..Default::default()
            },
        )
        .unwrap_err();
    assert_eq!(error.status, 1);
    assert_eq!(button.control_style_values(StylePart::Root, false)?, local);
    button.set_control_style(None)?;
    assert_eq!(button.control_style_values(StylePart::Root, true)?, local);
    button.set_control_style_values(StylePart::Root, PartStyleValues::default())?;
    assert_eq!(
        button.control_style_values(StylePart::Root, true)?,
        PartStyleValues::default()
    );
    let other = Window::new("Other", 300., 200.)?;
    let second = other.button("Shared")?;
    second.set_control_style(Some(&style))?;
    assert_eq!(second.control_style_values(StylePart::Root, true)?, values);
    let weak = other.downgrade();
    drop(other);
    assert!(weak.upgrade().is_some());
    drop(style);
    assert_eq!(second.control_style_values(StylePart::Root, true)?, values);
    drop(second);
    assert!(weak.upgrade().is_none());
    Ok(())
}

#[test]
fn shared_definitions_and_same_assignment() -> Result<()> {
    let w = Window::new("Shared definitions", 300., 200.)?;
    let buttons = (0..256)
        .map(|_| w.button("Shared"))
        .collect::<Result<Vec<_>>>()?;
    let values = PartStyleValues {
        background: Some(0x123456.into()),
        padding: Some(Insets::uniform(3.)),
        ..Default::default()
    };
    let parent = root_style(values.clone(), &[], None)?;
    let style = root_style(
        PartStyleValues {
            corner_radius: Some(4.),
            ..Default::default()
        },
        &[],
        Some(&parent),
    )?;
    buttons[0].set_control_style(Some(&style))?;
    let identity = style.identity(&w.0).unwrap();
    let cloned = style.clone();
    let marker = w.button("Before repeated assignments")?.handle;
    for _ in 0..32 {
        for button in &buttons {
            button.set_control_style(Some(&cloned))?;
            assert_eq!(
                button.control_style_values(StylePart::Root, true)?,
                PartStyleValues {
                    corner_radius: Some(4.),
                    ..values.clone()
                }
            );
            assert_eq!(style.identity(&w.0), Some(identity));
        }
    }
    assert_eq!(w.button("After repeated assignments")?.handle, marker + 1);
    buttons[0].set_control_style(None)?;
    buttons[0].set_control_style(Some(&style))?;
    assert_eq!(style.identity(&w.0), Some(identity));

    let distinct = ControlStyle::new(StyleTarget::Button, style.parts(), &[], Some(&parent))?;
    buttons[0].set_control_style(Some(&distinct))?;
    assert_ne!(distinct.identity(&w.0), Some(identity));
    for button in &buttons {
        button.set_control_style(None)?;
    }
    buttons[0].set_control_style(Some(&style))?;
    assert_ne!(style.identity(&w.0), Some(identity));
    buttons[0].set_control_style(None)?;

    let representative = w.button("Dropped representative")?;
    representative.set_control_style(Some(&style))?;
    let identity = style.identity(&w.0);
    drop(representative);
    buttons[0].set_control_style(Some(&style))?;
    assert_eq!(style.identity(&w.0), identity);
    let other = Window::new("Other", 300., 200.)?;
    other.button("Shared")?.set_control_style(Some(&style))?;
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
    let values = PartStyleValues {
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
        ..Default::default()
    };
    let base = root_style(
        values.clone(),
        &[
            ControlStyleRule {
                part: StylePart::Root,
                state: StyleState::Checked,
                values: PartStyleValues {
                    background: Some(0x333333.into()),
                    padding: Some(Insets::uniform(6.)),
                    ..Default::default()
                },
            },
            ControlStyleRule {
                part: StylePart::Root,
                state: StyleState::Disabled,
                values: PartStyleValues {
                    background: Some(0x444444.into()),
                    ..Default::default()
                },
            },
        ],
        None,
    )?;
    let checked = ControlStyleRule {
        part: StylePart::Root,
        state: StyleState::Checked,
        values: PartStyleValues {
            foreground: Some(0x666666.into()),
            ..Default::default()
        },
    };
    assert!(
        root_style(
            PartStyleValues::default(),
            &[checked.clone(), checked.clone()],
            Some(&base)
        )
        .is_err()
    );
    let derived = root_style(
        PartStyleValues {
            corner_radius: Some(5.),
            ..Default::default()
        },
        &[
            checked,
            ControlStyleRule {
                part: StylePart::Root,
                state: StyleState::Disabled,
                values: PartStyleValues {
                    border_brush: Some(0x777777.into()),
                    ..Default::default()
                },
            },
        ],
        Some(&base),
    )?;
    let button = w.button("States")?;
    button.behavior(ButtonBehavior::Toggle)?;
    button.set_control_style(Some(&derived))?;
    let normal = PartStyleValues {
        corner_radius: Some(5.),
        ..values
    };
    assert_eq!(button.control_style_values(StylePart::Root, true)?, normal);
    button.invoke()?;
    assert!(button.is_checked()?);
    let selected = PartStyleValues {
        background: Some(0x333333.into()),
        foreground: Some(0x666666.into()),
        padding: Some(Insets::uniform(6.)),
        ..normal.clone()
    };
    assert_eq!(
        button.control_style_values(StylePart::Root, true)?,
        selected
    );
    button.enabled(false)?;
    let disabled = PartStyleValues {
        background: Some(0x444444.into()),
        border_brush: Some(0x777777.into()),
        ..selected
    };
    assert_eq!(
        button.control_style_values(StylePart::Root, true)?,
        disabled
    );
    button.set_control_style_values(
        StylePart::Root,
        PartStyleValues {
            background: Some(0.into()),
            corner_radius: Some(0.),
            ..Default::default()
        },
    )?;
    let local = PartStyleValues {
        background: Some(0.into()),
        corner_radius: Some(0.),
        ..disabled
    };
    assert_eq!(button.control_style_values(StylePart::Root, true)?, local);
    w.set_theme(Theme::HighContrast)?;
    assert_eq!(button.control_style_values(StylePart::Root, true)?, local);
    button.set_control_style_values(StylePart::Root, PartStyleValues::default())?;
    button.enabled(true)?;
    button.invoke()?;
    assert_eq!(button.control_style_values(StylePart::Root, true)?, normal);
    Ok(())
}

// The ABI has no handle counter. Wrong-thread tokens belong to concurrent tests.
fn live_style_handles(first: u64, last: u64) -> usize {
    (first + 1..last)
        .filter(|&handle| {
            let mut count = 0;
            let status = unsafe {
                sys::xui_control_get_style_values(
                    handle,
                    StylePart::Root as u32,
                    0,
                    std::ptr::null_mut(),
                    0,
                    &mut count,
                )
            };
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
        let base = root_style(
            PartStyleValues {
                background: Some(i.into()),
                ..Default::default()
            },
            &[],
            None,
        )?;
        let style = root_style(
            PartStyleValues {
                corner_radius: Some(3.),
                ..Default::default()
            },
            &[],
            Some(&base),
        )?;
        button.set_control_style(Some(&style))?;
        let weak = Rc::downgrade(&style.0);
        drop(style);
        drop(base);
        assert!(weak.upgrade().is_none());
        assert_eq!(
            button.control_style_values(StylePart::Root, true)?,
            PartStyleValues {
                background: Some(i.into()),
                corner_radius: Some(3.),
                ..Default::default()
            }
        );
        button.set_control_style(None)?;
        button.set_control_style_values(
            StylePart::Root,
            PartStyleValues {
                padding: Some(Insets::uniform(1.)),
                ..Default::default()
            },
        )?;
        button.set_control_style_values(StylePart::Root, PartStyleValues::default())?;
        assert_eq!(
            button.control_style_values(StylePart::Root, true)?,
            PartStyleValues::default()
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

    let values = PartStyleValues {
        padding: Some(Insets::uniform(2.)),
        ..Default::default()
    };
    let shared = root_style(values.clone(), &[], None)?;
    for _ in 0..16 {
        let other = Window::new("Shared", 300., 200.)?;
        other.button("Shared")?.set_control_style(Some(&shared))?;
        button.set_control_style(Some(&shared))?;
        drop(other);
        assert_eq!(button.control_style_values(StylePart::Root, true)?, values);
        button.set_control_style(None)?;
    }
    Ok(())
}

#[test]
fn failed_application_releases_all_inheritance_handles() -> Result<()> {
    let w = Window::new("Style failure", 300., 200.)?;
    let label = w.label("Not a button")?;
    let first = label.handle;
    let mut style = root_style(PartStyleValues::default(), &[], None)?;
    for _ in 1..16 {
        style = root_style(PartStyleValues::default(), &[], Some(&style))?;
    }
    for _ in 0..32 {
        let error = label.set_control_style(Some(&style)).unwrap_err();
        assert_eq!(error.status, 1);
        assert!(error.message.contains("style target"));
        assert_eq!(
            label.control_style_values(StylePart::Root, true)?,
            PartStyleValues::default()
        );
    }
    let button = w.button("Retry")?;
    assert_eq!(live_style_handles(first, button.handle), 0);
    button.set_control_style(Some(&style))?;
    assert_eq!(
        button.control_style_values(StylePart::Root, true)?,
        PartStyleValues::default()
    );
    Ok(())
}

struct MutationSource {
    owner: Weak<Inner>,
    button: u64,
    style: ControlStyle,
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
        let fresh = root_style(PartStyleValues::default(), &[], Some(&self.style))?;
        for style in [Some(&fresh), Some(&self.style), None] {
            assert_eq!(button.set_control_style(style).unwrap_err().status, 7);
        }
        assert_eq!(
            button
                .set_control_style_values(StylePart::Root, PartStyleValues::default())
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
    let values = PartStyleValues {
        background: Some(0x123456.into()),
        ..Default::default()
    };
    let style = root_style(values.clone(), &[], None)?;
    let local = PartStyleValues {
        corner_radius: Some(3.),
        ..Default::default()
    };
    button.set_control_style(Some(&style))?;
    button.set_control_style_values(StylePart::Root, local.clone())?;
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
    assert_eq!(button.control_style_values(StylePart::Root, false)?, local);
    assert_eq!(
        button.control_style_values(StylePart::Root, true)?,
        PartStyleValues {
            corner_radius: Some(3.),
            ..values
        }
    );
    button.set_control_style(Some(&style))?;
    button.set_control_style(None)?;
    assert_eq!(button.control_style_values(StylePart::Root, true)?, local);
    Ok(())
}

#[test]
fn owner_drop_revokes_handles_without_retaining_windows() -> Result<()> {
    let values = PartStyleValues {
        padding: Some(Insets::uniform(2.)),
        ..Default::default()
    };
    let style = root_style(values.clone(), &[], None)?;
    let (window, button, weak) = {
        let w = Window::new("Disposed", 300., 200.)?;
        let button = w.button("Styled")?;
        button.set_control_style(Some(&style))?;
        (w.0.handle, button.handle, w.downgrade())
    };
    assert!(weak.upgrade().is_none());
    assert_eq!(unsafe { sys::xui_control_set_style(button, 0) }, 2);
    let mut count = 0;
    assert_eq!(
        unsafe {
            sys::xui_control_get_style_values(
                button,
                StylePart::Root as u32,
                1,
                std::ptr::null_mut(),
                0,
                &mut count,
            )
        },
        2
    );
    let options = sys::ControlStyleOptions {
        size: size_of::<sys::ControlStyleOptions>() as u32,
        version: 0x10000,
        target: StyleTarget::Button as u32,
        ..Default::default()
    };
    let mut handle = 99;
    assert_eq!(
        unsafe { sys::xui_control_style_create(window, &options, &mut handle) },
        2
    );
    assert_eq!(handle, 0);
    let replacement = Window::new("Replacement", 300., 200.)?;
    let second = replacement.button("Styled again")?;
    second.set_control_style(Some(&style))?;
    assert_eq!(second.control_style_values(StylePart::Root, true)?, values);
    Ok(())
}

#[test]
fn shared_styles_preserve_thread_and_window_guards() -> Result<()> {
    let w = Window::new("Guards", 300., 200.)?;
    let button = w.button("Styled")?;
    let style = root_style(
        PartStyleValues {
            background: Some(0x123456.into()),
            ..Default::default()
        },
        &[],
        None,
    )?;
    let handle = button.handle;
    std::thread::spawn(move || {
        assert_eq!(unsafe { sys::xui_control_set_style(handle, 0) }, 4);
        let mut count = 0;
        assert_eq!(
            unsafe {
                sys::xui_control_get_style_values(
                    handle,
                    StylePart::Root as u32,
                    1,
                    std::ptr::null_mut(),
                    0,
                    &mut count,
                )
            },
            4
        );
    })
    .join()
    .unwrap();
    let other = Window::new("Other owner", 300., 200.)?;
    style.with_native_handle(&w.0, &mut |native| {
        let other_button = other.button("Different window")?;
        assert_eq!(
            unsafe { sys::xui_control_set_style(other_button.handle, native) },
            1
        );
        Ok(())
    })?;
    button.set_control_style(Some(&style))?;
    assert_eq!(
        button
            .control_style_values(StylePart::Root, true)?
            .background,
        Some(0x123456.into())
    );
    Ok(())
}
