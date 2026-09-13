use super::*;
use std::cell::Cell;
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
