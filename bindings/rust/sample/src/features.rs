use std::{cell::Cell, rc::Rc};
use xui::*;
struct Rows(Rc<Cell<usize>>);
impl ReadOnlyImmutableSource for Rows {
    fn count(&self) -> u64 {
        1000000
    }
    fn key(&self, index: u64) -> Result<ItemKey> {
        Ok(ItemKey {
            id: index + 1,
            version: 1,
        })
    }
    fn find(&self, key: ItemKey) -> Result<Option<u64>> {
        Ok(
            if key.version == 1 && key.id > 0 && key.id <= self.count() {
                Some(key.id - 1)
            } else {
                None
            },
        )
    }
    fn item(&self, index: u64, _column: u64) -> Result<ItemContent> {
        self.0.set(self.0.get() + 1);
        Ok(ItemContent {
            primary: format!("Record {}", index + 1),
            secondary: "Immutable virtual source".into(),
            enabled: true,
            progress: Some((index % 100) as f64 / 100.),
            checked: None,
        })
    }
}
pub fn run(fail: bool) -> Result<()> {
    let w = Window::new("XUI feature bindings", 700., 800.)?;
    let root = w.stack(Axis::Vertical)?;
    root.padding(16.)?;
    root.spacing(8.)?;
    let status = w.label("F6: dialog. Escape: cancel. F8: change range. F12: close.")?;
    let combo = w.combo_box("Presentation", false)?;
    combo.set_items(
        &[
            Choice {
                id: 1,
                text: "List".into(),
                enabled: true,
                version: 0,
            },
            Choice {
                id: 2,
                text: "Tiles".into(),
                enabled: true,
                version: 0,
            },
        ],
        Some(1),
    )?;
    let range = w.range_input("Zoom")?;
    range.set_range(NumericRange {
        minimum: 0.,
        maximum: 100.,
        small_step: 1.,
        large_step: 10.,
    })?;
    range.set_value(20.)?;
    let items = w.items_view("One million rows")?;
    let fetched = Rc::new(Cell::new(0));
    items.set_source(&w.immutable_source(Rows(fetched.clone()))?)?;
    let map = w.map_view("Offline map")?;
    map.set_view(
        GeoPoint {
            latitude: 47.6,
            longitude: -122.3,
        },
        4.,
    )?;
    map.set_markers(&[MapMarker {
        id: 1,
        location: GeoPoint {
            latitude: 47.6,
            longitude: -122.3,
        },
        name: "Seattle".into(),
    }])?;
    map.fixed_size(650., 180.)?;
    let edit = w.button("Edit document")?;
    let form = w.stack(Axis::Vertical)?;
    form.spacing(8.)?;
    let document = w.multiline_text("Notes")?;
    document.set_document("Authored text 😀")?;
    document.fixed_size(390., 80.)?;
    let color = w.color_picker("Accent")?;
    color.set_value(RgbaColor {
        red: 30,
        green: 100,
        blue: 220,
        alpha: 255,
    })?;
    form.add(&document, 0.)?;
    form.add(&color, 0.)?;
    let dialog = w.content_dialog("Document and color", &form)?;
    let weak = status.downgrade();
    dialog.on_event(move |e| {
        weak.upgrade()
            .unwrap()
            .set_text(if e.value == 0 { "Accepted" } else { "Canceled" })
    })?;
    let d = dialog.weak();
    let a = edit.downgrade();
    edit.on_event(move |_| d.upgrade().unwrap().show(&a.upgrade().unwrap()))?;
    let weak = status.downgrade();
    range.on_event(move |e| {
        if e.kind == 2 {
            assert!(!fail, "Feature callback sentinel");
            weak.upgrade().unwrap().set_text("Range changed")?;
        }
        Ok(())
    })?;
    let weak = items.weak();
    combo.on_event(move |e| {
        weak.upgrade().unwrap().set_presentation(if e.value == 1 {
            ItemsPresentation::List
        } else {
            ItemsPresentation::Tiles
        })
    })?;
    root.add(&status, 0.)?;
    root.add(&combo, 0.)?;
    root.add(&range, 0.)?;
    root.add(&edit, 0.)?;
    root.add(&map, 0.)?;
    root.add(&items, 1.)?;
    w.set_content(&root)?;
    let d = dialog.weak();
    let a = edit.downgrade();
    let r = range.weak();
    let close = w.downgrade();
    w.on_key(move |e| {
        match e.value & 0xffff {
            0x75 => d.upgrade().unwrap().show(&a.upgrade().unwrap())?,
            0x77 => r.upgrade().unwrap().change_value(25.)?,
            0x7b => close.upgrade().unwrap().close()?,
            _ => (),
        }
        Ok(())
    })?;
    w.run()?;
    assert!(
        fetched.get() > 0 && fetched.get() < 4096,
        "Virtual source visible-query budget."
    );
    println!("Million-row source fetched {} visible rows.", fetched.get());
    Ok(())
}
