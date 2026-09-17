use super::*;
use std::cell::Cell;
#[test]
fn shell_image_and_open_icon() -> Result<()> {
    let window = Window::new("Shell image", 300., 300.)?;
    let image = window.image("Preview")?;
    image.shell_source(".", 160, 160)?;
    assert_eq!(image.status()?, 1);
    for dimension in [0, 1025, u32::MAX] {
        assert!(image.shell_source(".", dimension, 160).is_err());
        assert!(image.shell_source(".", 160, dimension).is_err());
    }
    assert!(image.shell_source("a\0b", 160, 160).is_err());
    assert!(image.shell_source(&"x".repeat(32768), 160, 160).is_err());
    image.source(".", 160, 160)?;
    image.shell_source("", 160, 160)?;
    assert_eq!(image.status()?, 0);
    image.image_shell_source(".", 160, 160)?;
    image.unload()?;
    assert_eq!(image.status()?, 0);
    assert_eq!(ButtonIcon::Drive as u32, 21);
    assert_eq!(ButtonIcon::Open as u32, 22);
    let button = window.button("Open")?;
    button.set_icon(ButtonIcon::Open)?;
    assert_eq!(button.feature_get(45)?.first, 22);
    Ok(())
}
#[test]
fn retained_navigation_style_bridges() -> Result<()> {
    let window = Window::with_titlebar("Navigation bridges", 500., 400.)?;
    let navigation = window.navigation_view("Navigation")?;
    let children = [
        window.titlebar()?, window.titlebar_title()?.0, window.titlebar_minimize()?.0,
        window.titlebar_maximize()?.0, window.titlebar_close()?.0, navigation.search()?.0,
        navigation.toggle_button()?.0, navigation.items()?, navigation.header_items()?,
        navigation.footer_items()?, navigation.title()?.0, navigation.empty_message()?.0,
    ];
    for child in &children {
        let values = PartStyleValues { background: Some(ThemeColor::new(0x123456, 0x123456)), ..Default::default() };
        child.set_control_style_values(StylePart::Root, values.clone())?;
        child.set_control_style(None)?;
        assert_eq!(child.control_style_values(StylePart::Root, true)?.background, values.background);
        child.set_control_style_values(StylePart::Root, PartStyleValues::default())?;
    }
    assert_eq!(window.titlebar()?.handle, window.titlebar()?.handle);
    assert_eq!(navigation.items()?.handle, navigation.items()?.handle);
    let expanded = navigation.expanded()?;
    navigation.toggle_button()?.on_event(|_| Ok(()))?;
    navigation.toggle_button()?.invoke()?;
    assert_ne!(navigation.expanded()?, expanded);
    navigation.toggle_button()?.unsubscribe()?;
    navigation.toggle_button()?.invoke()?;
    assert_eq!(navigation.expanded()?, expanded);

    let breadcrumb = window.breadcrumb("Path")?;
    breadcrumb.set_segments(&[Choice { id: 20, text: "Leaf".into(), enabled: true, version: 9 }])?;
    let segment = breadcrumb.segment_button(ItemKey { id: 20, version: 9 })?;
    assert_eq!(segment.handle, breadcrumb.segment_button(ItemKey { id: 20, version: 9 })?.handle);
    assert!(breadcrumb.segment_button(ItemKey { id: 20, version: 8 }).is_err());
    let navigated = Rc::new(Cell::new(0));
    let observed = navigated.clone();
    breadcrumb.on_event(move |e| { observed.set(e.value); Ok(()) })?;
    let segment_clicks = Rc::new(Cell::new(0));
    let observed = segment_clicks.clone();
    segment.on_event(move |_| { observed.set(observed.get() + 1); Ok(()) })?;
    segment.invoke()?; assert_eq!(navigated.get(), 20); assert_eq!(segment_clicks.get(), 1);
    breadcrumb.set_segments(&[Choice { id: 20, text: "Renamed leaf".into(), enabled: true, version: 9 }])?;
    assert_eq!(segment.handle, breadcrumb.segment_button(ItemKey { id: 20, version: 9 })?.handle);
    navigated.set(0); segment.invoke()?; assert_eq!(navigated.get(), 20); assert_eq!(segment_clicks.get(), 2);
    segment.unsubscribe()?;
    navigated.set(0); segment.invoke()?; assert_eq!(navigated.get(), 20);
    breadcrumb.overflow_button()?;
    let observed = segment_clicks.clone();
    segment.on_event(move |_| { observed.set(observed.get() + 1); Ok(()) })?;
    breadcrumb.set_segments(&[Choice { id: 20, text: "Replacement".into(), enabled: true, version: 10 }])?;
    navigated.set(0);
    assert!(segment.invoke().is_err());
    assert_eq!(navigated.get(), 0); assert_eq!(segment_clicks.get(), 2);
    segment.unsubscribe()?;
    let observed = segment_clicks.clone();
    segment.on_event(move |_| { observed.set(observed.get() + 1); Ok(()) })?;
    assert!(segment.invoke().is_err());
    assert_eq!(navigated.get(), 0); assert_eq!(segment_clicks.get(), 2);
    assert!(breadcrumb.segment_button(ItemKey { id: 20, version: 9 }).is_err());
    assert_ne!(segment.handle, breadcrumb.segment_button(ItemKey { id: 20, version: 10 })?.handle);

    let bar = window.command_bar("Commands")?;
    let mut command = Command { id: 42, parent: 0, label: "Command".into(), kind: CommandKind::Action,
        enabled: true, checked: Some(true), shortcut_hint: String::new(), pin_label: String::new() };
    bar.set_commands(&[command.clone()])?;
    let button = bar.command_button(42)?;
    let button_values = PartStyleValues {
        background: Some(ThemeColor::new(0x123456, 0x654321)), ..Default::default()
    };
    button.set_control_style_values(StylePart::Root, button_values.clone())?;
    bar.overflow_button()?;
    let actions = Rc::new(Cell::new(0));
    let observed = actions.clone();
    bar.on_event(move |e| { assert_eq!(e.value, 42); observed.set(observed.get() + 1); Ok(()) })?;
    let clicks = Rc::new(Cell::new(0));
    let observed = clicks.clone();
    button.on_event(move |_| { observed.set(observed.get() + 1); Ok(()) })?;
    for state in [Some(true), None, Some(false), Some(true)] {
        command.checked = state; bar.set_commands(&[command.clone()])?;
        let previous_actions = actions.get(); let previous_clicks = clicks.get();
        button.invoke()?;
        assert_eq!(actions.get(), previous_actions + 1);
        assert_eq!(clicks.get(), previous_clicks + 1);
        assert_eq!(button.is_checked()?, state == Some(true));
        assert_eq!(button.handle, bar.command_button(42)?.handle);
        assert_eq!(button.control_style_values(StylePart::Root, true)?.background, button_values.background);
        button.unsubscribe()?; button.invoke()?;
        assert_eq!(actions.get(), previous_actions + 2);
        assert_eq!(clicks.get(), previous_clicks + 1);
        for _ in 0..2 {
            let observed = clicks.clone();
            button.on_event(move |_| { observed.set(observed.get() + 1); Ok(()) })?;
        }
    }
    assert!(bar.command_button(99).is_err());
    button.unsubscribe()?;
    for state in [Some(true), None, Some(false)] {
        command.checked = state;
        bar.set_commands(&[command.clone()])?;
        let previous_actions = actions.get();
        let previous_clicks = clicks.get();
        button.invoke()?;
        assert_eq!(actions.get(), previous_actions + 1);
        assert_eq!(clicks.get(), previous_clicks);
        assert_eq!(button.is_checked()?, state == Some(true));
        assert_eq!(button.handle, bar.command_button(42)?.handle);
        assert_eq!(button.control_style_values(StylePart::Root, true)?.background, button_values.background);
    }
    let observed = clicks.clone();
    button.on_event(move |_| { observed.set(observed.get() + 1); Ok(()) })?;
    let retired_actions = actions.get(); let retired_clicks = clicks.get();
    bar.set_commands(&[])?;
    assert!(button.invoke().is_err());
    assert_eq!(actions.get(), retired_actions); assert_eq!(clicks.get(), retired_clicks);
    command.enabled = false; bar.set_commands(&[command.clone()])?;
    let replacement = bar.command_button(42)?;
    assert_ne!(replacement.handle, button.handle);
    let replacement_clicks = Rc::new(Cell::new(0));
    let observed = replacement_clicks.clone();
    replacement.on_event(move |_| { observed.set(observed.get() + 1); Ok(()) })?;
    button.unsubscribe()?;
    let observed = clicks.clone();
    button.on_event(move |_| { observed.set(observed.get() + 1); Ok(()) })?;
    assert!(button.invoke().is_err()); assert!(replacement.invoke().is_err());
    assert_eq!(actions.get(), retired_actions); assert_eq!(clicks.get(), retired_clicks);
    assert_eq!(replacement_clicks.get(), 0);
    command.enabled = true; bar.set_commands(&[command])?;
    assert!(button.invoke().is_err()); replacement.invoke()?;
    assert_eq!(actions.get(), retired_actions + 1); assert_eq!(clicks.get(), retired_clicks);
    assert_eq!(replacement_clicks.get(), 1);
    drop(window);
    assert_eq!(replacement.handle, bar.command_button(42)?.handle);
    Ok(())
}
#[test]
fn retained_facade_style_accessors() -> Result<()> {
    let window = Window::new("Retained facade styles", 400., 300.)?;
    let body = window.stack(Axis::Vertical)?;
    let dialog = window.content_dialog("Dialog", &body)?;
    let surface = window.command_surface("Commands")?;
    let location = window.location_picker("Location")?;
    let items = window.items_view("Items")?;
    let view = window.view_picker("View", &items)?;
    let pane = window.navigation_pane("Navigation")?;
    assert_eq!(pane.group()?.handle, pane.group()?.handle);
    assert_eq!(pane.progress()?.handle, pane.progress()?.handle);
    let title = dialog.title()?;
    assert_eq!(title.handle, dialog.title()?.handle);
    let menu = surface.menu()?;
    assert_eq!(menu.handle, surface.menu()?.handle);
    let children = [
        title.0, dialog.validation()?.0, dialog.body()?.0, dialog.footer()?.0,
        surface.editor()?.0, surface.title()?.0, surface.status()?.0, surface.close_button()?.0,
        surface.content()?.0, surface.results()?.0, menu,
        location.content()?.0, location.footer()?.0, location.toolbar()?.0,
        view.content()?.0, pane.status()?.0, pane.content()?.0, pane.group()?.0, pane.progress()?.0,
    ];
    for child in &children {
        let values = PartStyleValues { background: Some(ThemeColor::new(0x123456, 0x123456)), ..Default::default() };
        child.set_control_style_values(StylePart::Root, values.clone())?;
        child.set_control_style(None)?;
        assert_eq!(child.control_style_values(StylePart::Root, true)?.background, values.background);
        child.set_control_style_values(StylePart::Root, PartStyleValues::default())?;
        assert_eq!(child.control_style_values(StylePart::Root, false)?, PartStyleValues::default());
    }
    drop(window);
    assert_eq!(children[0].control_style_values(StylePart::Root, false)?, PartStyleValues::default());
    Ok(())
}
#[test]
fn retained_choice_style_accessors() -> Result<()> {
    let window = Window::new("Retained choice styles", 400., 300.)?;
    assert!(window.combo_box("Noneditable", false)?.editor()?.is_none());
    let combo = window.combo_box("Editable", true)?;
    let editor = combo.editor()?.expect("Editable ComboBox has an editor");
    assert_eq!(editor.handle, combo.editor()?.unwrap().handle);
    let picker = window.color_picker("Color")?;
    let red = picker.channel(0)?;
    assert_eq!(red.handle, picker.channel(0)?.handle);
    assert!(picker.channel(4).is_err());
    assert!(picker.swatch_button(5).is_err());
    assert!(picker.swatch_button(u32::MAX).is_err());
    let changes = Rc::new(Cell::new(0));
    for _ in 0..2 {
        let changes = changes.clone();
        red.on_change(move |_| { changes.set(changes.get() + 1); Ok(()) })?;
    }
    red.change_value(21.)?;
    assert_eq!(picker.value()?.red, 21);
    assert_eq!(changes.get(), 1);
    let increase = red.increase_button()?;
    let clicks = Rc::new(Cell::new(0));
    for _ in 0..2 {
        let clicks = clicks.clone();
        increase.on_event(move |_| { clicks.set(clicks.get() + 1); Ok(()) })?;
    }
    increase.invoke()?;
    assert_eq!(picker.value()?.red, 22);
    assert_eq!(changes.get(), 2);
    assert_eq!(clicks.get(), 1);
    let swatch = picker.swatch_button(2)?;
    assert_eq!(swatch.handle, picker.swatch_button(2)?.handle);
    swatch.on_event(|_| Ok(()))?;
    swatch.on_event(|_| Ok(()))?;
    swatch.invoke()?;
    assert_eq!(picker.value()?, RgbaColor { red: 220, green: 45, blue: 45, alpha: 255 });
    let status = window.inline_status("Status")?;
    let children = [
        editor.0, combo.popup()?.0, combo.choices()?.0, red.editor()?.0,
        red.decrease_button()?.0, increase.0, red.0,
        status.action_button()?.0, status.dismiss_button()?.0,
    ];
    for child in &children {
        let values = PartStyleValues { background: Some(ThemeColor::new(0x123456, 0x654321)), ..Default::default() };
        child.set_control_style_values(StylePart::Root, values.clone())?;
        child.set_control_style(None)?;
        assert_eq!(child.control_style_values(StylePart::Root, true)?.background, values.background);
        child.set_control_style_values(StylePart::Root, PartStyleValues::default())?;
    }
    drop(window);
    assert_eq!(children[0].control_style_values(StylePart::Root, false)?, PartStyleValues::default());
    Ok(())
}
struct Million {
    count: u64,
    calls: Rc<Cell<usize>>,
    fail: bool,
    dropped: Rc<Cell<bool>>,
}
impl Drop for Million {
    fn drop(&mut self) {
        self.dropped.set(true);
    }
}
impl ReadOnlyImmutableSource for Million {
    fn count(&self) -> u64 {
        self.count
    }
    fn key(&self, index: u64) -> Result<ItemKey> {
        assert!(!self.fail, "source panic sentinel");
        self.calls.set(self.calls.get() + 1);
        Ok(ItemKey {
            id: index + 1,
            version: 7,
        })
    }
    fn find(&self, key: ItemKey) -> Result<Option<u64>> {
        self.calls.set(self.calls.get() + 1);
        Ok(if key.version == 7 && key.id > 0 && key.id <= self.count {
            Some(key.id - 1)
        } else {
            None
        })
    }
    fn item(&self, index: u64, _column: u64) -> Result<ItemContent> {
        self.calls.set(self.calls.get() + 1);
        Ok(ItemContent {
            primary: format!("Row {index}"),
            secondary: "日本語 😀".into(),
            enabled: true,
            progress: None,
            checked: None,
        })
    }
    fn has_children(&self, key: ItemKey) -> Result<bool> {
        Ok(key.id == 1)
    }
}
#[test]
fn all_features_and_bounded_sources() -> Result<()> {
    let w = Window::with_titlebar("Feature tests", 600., 600.)?;
    let tabs = w.tab_strip("Colored tabs")?;
    assert!(!tabs.new_tab_button_visible()?);
    tabs.set_new_tab_button_visible(true)?;
    assert!(tabs.new_tab_button_visible()?);
    tabs.set_new_tab_button_visible(false)?;
    assert!(!tabs.new_tab_button_visible()?);
    let colors = TabColors {
        row_background: Some(0x123456),
        selected_background: Some(0),
        selected_text: Some(0xffffff),
        inactive_background: Some(0x234567),
        inactive_text: Some(0xeeeeee),
        hover_background: Some(0x345678),
        border: Some(0x456789),
    };
    tabs.set_colors(colors)?;
    assert_eq!(tabs.colors()?, colors);
    assert!(
        tabs.set_colors(TabColors {
            border: Some(0xff123456),
            ..colors
        })
        .is_err()
    );
    assert_eq!(tabs.colors()?, colors);
    tabs.set_colors(TabColors::default())?;
    assert_eq!(tabs.colors()?, TabColors::default());
    let range = w.range_input("Range")?;
    range.set_range(NumericRange {
        minimum: -10.,
        maximum: 10.,
        small_step: 0.5,
        large_step: 2.,
    })?;
    range.set_value(2.5)?;
    assert_eq!(range.value()?, 2.5);
    assert!(range.set_value(f64::NAN).is_err());
    assert_eq!(range.value()?, 2.5);
    range.set_orientation(Axis::Vertical)?;
    range.set_reversed(true)?;
    let count = Rc::new(Cell::new(0));
    let observed = count.clone();
    range.on_event(move |_| {
        observed.set(observed.get() + 1);
        Ok(())
    })?;
    range.change_value(3.)?;
    assert_eq!(count.get(), 1);
    let choices = [
        Choice {
            id: 1,
            text: "One".into(),
            enabled: true,
            version: 0,
        },
        Choice {
            id: 2,
            text: "Two 😀".into(),
            enabled: true,
            version: 0,
        },
    ];
    let radio = w.radio_group("Radio")?;
    radio.set_items(&choices, Some(1))?;
    radio.select(2)?;
    let combo = w.combo_box("Combo", true)?;
    combo.set_items(&choices, Some(1))?;
    combo.select(2)?;
    let numeric = w.numeric_input("Numeric")?;
    numeric.set_value(2.)?;
    numeric.step(true)?;
    assert_eq!(numeric.value()?, 3.);
    let expander = w.expander("Expander", &*w.stack(Axis::Vertical)?)?;
    expander.set_expanded(false)?;
    assert!(!expander.expanded()?);
    let progress = w.progress("Progress")?;
    progress.set_value(40.)?;
    progress.set_state(ProgressState::Paused)?;
    assert_eq!(progress.state()?, ProgressState::Paused);
    let _popup = w.popup("Popup", &*w.stack(Axis::Vertical)?)?;
    let split = w.split_button("Split")?;
    split.primary()?.invoke()?;
    split.secondary()?.invoke()?;
    let calls = Rc::new(Cell::new(0));
    let dropped = Rc::new(Cell::new(false));
    let source = w.immutable_source(Million {
        count: 1000000,
        calls: calls.clone(),
        fail: false,
        dropped: dropped.clone(),
    })?;
    let items = w.items_view("Items")?;
    items.set_source(&source)?;
    items.select_all()?;
    assert_eq!(items.selection()?.storage_terms, 1);
    assert!(items.contains(ItemKey {
        id: 999999,
        version: 7
    })?);
    items.select(ItemKey {
        id: 999999,
        version: 7,
    })?;
    assert_eq!(
        items.selection()?.focused,
        Some(ItemKey {
            id: 999999,
            version: 7
        })
    );
    assert!(calls.get() < 100);
    items.set_presentation(ItemsPresentation::Tiles)?;
    items.item_size(180., 60.)?;
    let tree = w.tree_view("Tree")?;
    tree.set_source(&source)?;
    let empty = w.immutable_source(Million {
        count: 0,
        calls: calls.clone(),
        fail: false,
        dropped: Rc::new(Cell::new(false)),
    })?;
    let requests = Rc::new(RefCell::new(None));
    let observed = requests.clone();
    tree.on_request(move |r| {
        *observed.borrow_mut() = Some(r);
        Ok(())
    })?;
    tree.expand(ItemKey { id: 1, version: 7 }, true)?;
    requests.borrow_mut().take().unwrap().complete(&empty, "")?;
    let grid = w.grid("Grid")?;
    grid.set_tracks(&[GridTrack::default()], &[GridTrack::default()])?;
    grid.add(&*w.label("Cell")?, 0, 0, 1, 1)?;
    let wrap = w.wrap("Wrap")?;
    wrap.set_item_width(100.)?;
    wrap.add(&*w.label("Tile")?)?;
    let adaptive = w.adaptive_layout(
        "Adaptive",
        &*w.stack(Axis::Vertical)?,
        &*w.stack(Axis::Vertical)?,
    )?;
    adaptive.set_breakpoint(500.)?;
    adaptive.set_navigation_extent(180.)?;
    adaptive.set_compact_navigation(CompactNavigation::Overlay)?;
    adaptive.set_navigation_open(true)?;
    let command = Command {
        id: 1,
        parent: 0,
        label: "Apply".into(),
        kind: CommandKind::Action,
        enabled: true,
        checked: None,
        shortcut_hint: "Ctrl+K".into(),
        pin_label: "Pin".into(),
    };
    let bar = w.command_bar("Bar")?;
    bar.set_commands(std::slice::from_ref(&command))?;
    bar.bind(
        1,
        75,
        KeyModifiers {
            control: true,
            ..Default::default()
        },
    )?;
    let action = Rc::new(Cell::new(0));
    let observed = action.clone();
    bar.on_event(move |e| {
        observed.set(e.kind);
        Ok(())
    })?;
    bar.invoke(1, true)?;
    assert_eq!(action.get(), 9);
    w.command_surface("Surface")?.set_commands(&[command])?;
    w.breadcrumb("Path")?.set_segments(&choices)?;
    w.navigation_pane("Navigation")?.set_source(&source)?;
    let location = w.location_picker("Location")?;
    location.editor()?.set_text("Local")?;
    location.navigation()?.set_source(&source)?;
    w.view_picker("View", &items)?.size()?.set_value(64.)?;
    let doc = w.multiline_text("Document")?;
    doc.set_document("A😀Z")?;
    doc.set_selection(TextSelection { start: 1, end: 3 })?;
    assert_eq!(doc.text()?, "A😀Z");
    assert!(
        doc.set_selection(TextSelection { start: 1, end: 2 })
            .is_err()
    );
    doc.set_read_only(true)?;
    assert!(doc.read_only()?);
    let rich = w.rich_text("Rich")?;
    rich.set_runs(&[TextRun {
        text: "Bold".into(),
        bold: true,
        italic: false,
        underline: false,
        link: "".into(),
    }])?;
    assert_eq!(rich.text()?, "Bold");
    let password = w.password_input("Password")?;
    password.set_maximum_length(32)?;
    password.set_password("safe")?;
    password.with_password(|bytes| {
        assert_eq!(bytes, b"safe");
        Ok(())
    })?;
    let date = w.date_time_picker("Date", DateTimePresentation::Calendar)?;
    date.set_value(LocalDateTime {
        year: 2028,
        month: 2,
        day: 29,
        hour: 12,
        minute: 34,
        second: 56,
    })?;
    assert_eq!(date.value()?.day, 29);
    let status = w.inline_status("Status")?;
    status.set_dismissible(true)?;
    status.set_message("Done", StatusSeverity::Success)?;
    status.dismiss()?;
    status.show()?;
    let color = w.color_picker("Color")?;
    let rgba = RgbaColor {
        red: 1,
        green: 2,
        blue: 3,
        alpha: 4,
    };
    color.set_value(rgba)?;
    assert_eq!(color.value()?, rgba);
    w.content_dialog("Dialog", &*w.stack(Axis::Vertical)?)?
        .set_validation_message("Required")?;
    w.vector_canvas("Canvas")?.set_scene(&[VectorShape {
        id: 1,
        points: vec![
            ScenePoint { x: 0., y: 0. },
            ScenePoint { x: 40., y: 0. },
            ScenePoint { x: 40., y: 40. },
        ],
        name: "Triangle".into(),
        closed: true,
        interactive: true,
        stroke_width: 1.,
        ..Default::default()
    }])?;
    let map = w.map_view("Map")?;
    map.set_view(
        GeoPoint {
            latitude: 47.,
            longitude: -122.,
        },
        3.,
    )?;
    map.set_markers(&[MapMarker {
        id: 1,
        location: GeoPoint {
            latitude: 47.,
            longitude: -122.,
        },
        name: "Here".into(),
    }])?;
    assert_eq!(map.view()?.1, 3.);
    let stale = map.request_overlay()?;
    let current = map.request_overlay()?;
    assert!(stale.complete(&[]).is_err());
    current.complete(&[])?;
    let media = w.media_playback("Media")?;
    assert_eq!(media.state()?, HostState::Idle);
    media.set_volume(0.25)?;
    media.unload()?;
    let web = w.web_content("Web")?;
    assert_eq!(web.state()?, HostState::Idle);
    web.set_allowed_origins(&["https://example.invalid"])?;
    let evaluation = web.evaluate("1+1")?;
    assert!(evaluation.try_result()?.unwrap().is_error);
    web.stop()?;
    assert!(evaluation.try_result().is_err());
    w.tab_strip("Tabs")?.set_items(&choices, Some(1))?;
    w.split_view(
        "Split",
        &*w.stack(Axis::Vertical)?,
        &*w.stack(Axis::Vertical)?,
    )?
    .set_ratio(0.4)?;
    let pages = w.page_view("Pages")?;
    pages.add(&*w.stack(Axis::Vertical)?)?;
    pages.set_selected_page(0)?;
    let table = w.data_grid("Data")?;
    table.set_columns(&[
        GridColumn {
            name: "Name".into(),
            width: 120.,
            numeric: false,
            filterable: true,
            checkable: true,
        },
        GridColumn {
            name: "Value".into(),
            width: 120.,
            numeric: true,
            filterable: false,
            checkable: false,
        },
    ])?;
    table.set_source(&source)?;
    table.set_column_order(&[1, 0])?;
    table.set_column_width(1, 160.)?;
    table.select_all()?;
    w.history_chart("History")?.append(5.)?;
    assert!(calls.get() < 1000);
    assert!(!dropped.get());
    Ok(())
}
#[test]
fn feature_source_panic_and_release() -> Result<()> {
    let dropped = Rc::new(Cell::new(false));
    let weak;
    {
        let w = Window::new("Source lifetime", 600., 600.)?;
        weak = w.downgrade();
        let source = w.immutable_source(Million {
            count: 1000000,
            calls: Rc::new(Cell::new(0)),
            fail: true,
            dropped: dropped.clone(),
        })?;
        let items = w.items_view("Failure")?;
        assert!(items.set_source(&source).is_err());
        assert_ne!(w.callback_status()?, 0);
    }

    assert!(weak.upgrade().is_none());
    assert!(dropped.get());
    let w = Window::new("Password failure", 600., 600.)?;
    let password = w.password_input("Secret")?;
    password.set_password("secret")?;
    assert!(
        password
            .with_password(|_| panic!("secret receiver sentinel"))
            .is_err()
    );
    assert_ne!(w.callback_status()?, 0);
    Ok(())
}

#[test]
fn replaced_source_releases_its_pin() -> Result<()> {
    let w = Window::new("Replacement", 600., 600.)?;
    let items = w.items_view("Items")?;
    let dropped = Rc::new(Cell::new(false));
    let source = w.immutable_source(Million {
        count: 1000000,
        calls: Rc::new(Cell::new(0)),
        fail: false,
        dropped: dropped.clone(),
    })?;
    items.set_source(&source)?;
    drop(source);
    assert!(!dropped.get());
    let empty = w.immutable_source(Million {
        count: 0,
        calls: Rc::new(Cell::new(0)),
        fail: false,
        dropped: Rc::new(Cell::new(false)),
    })?;
    items.set_source(&empty)?;
    assert!(dropped.get());
    Ok(())
}
