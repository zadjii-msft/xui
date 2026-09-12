use xui::*;
fn main() -> Result<()> {
    let args: Vec<_> = std::env::args().skip(1).collect();
    let window = Window::new("XUI bindings", 600., 720.)?;
    let root = window.stack(Axis::Vertical)?;
    root.padding(20.)?;
    root.spacing(10.)?;
    let label =
        window.label("Ready — 日本語 😀 — a long Unicode label with native retained layout")?;
    label.automation_id("status")?;
    let input = window.text_input("Workspace name")?;
    input.automation_id("input")?;
    input.set_text("Alpha 😀")?;
    let button = window.button("Apply")?;
    button.automation_id("apply")?;
    let toggle = window.toggle("Enable previews")?;
    toggle.automation_id("toggle")?;
    let form = window.stack(Axis::Vertical)?;
    form.spacing(8.)?;
    for i in 0..8 {
        let row = window.label(&format!("Preference {i} — Unicode 日本語"))?;
        form.add(&row, 0.)?;
    }
    let scroll = window.scroll_view(&form, "Preferences")?;
    scroll.automation_id("scroll")?;
    scroll.fixed_size(560., 120.)?;
    let image = window.image("Preview")?;
    image.automation_id("image")?;
    image.fixed_size(192., 96.)?;
    let list = window.file_list("Files")?;
    list.automation_id("files")?;
    let names: Vec<_> = (0..60).map(|i| format!("Entry-{i:03}.txt")).collect();
    let items: Vec<_> = names
        .iter()
        .enumerate()
        .map(|(i, name)| FileItem {
            id: i as u64 + 1,
            name,
            path: "",
            directory: false,
        })
        .collect();
    list.set_items(&items)?;
    root.add(&label, 0.)?;
    root.add(&input, 0.)?;
    root.add(&button, 0.)?;
    root.add(&toggle, 0.)?;
    root.add(&scroll, 0.)?;
    root.add(&image, 0.)?;
    root.add(&list, 1.)?;
    window.set_content(&root)?;
    let weak = label.downgrade();
    let callback_fail = args.iter().any(|s| s == "--callback-fail");
    button.on_event(move |_| {
        assert!(!callback_fail, "GUI callback sentinel");
        weak.upgrade().unwrap().set_text("Applied")
    })?;
    let weak = label.downgrade();
    toggle.on_event(move |e| {
        weak.upgrade()
            .unwrap()
            .set_text(if e.value != 0 { "Enabled" } else { "Disabled" })
    })?;
    let weak = label.downgrade();
    let weak_input = input.downgrade();
    input.on_event(move |e| {
        let _committed_text = weak_input.upgrade().unwrap().text()?;
        weak.upgrade()
            .unwrap()
            .set_text(if e.kind == 3 { "Submitted" } else { "Edited" })
    })?;
    let weak = label.downgrade();
    let close = window.downgrade();
    let weak_image = image.downgrade();
    let image_path = args.first().filter(|s| !s.starts_with("--")).cloned();
    window.on_key(move |e| {
        if e.value & 0xffff == 0x75 {
            weak.upgrade().unwrap().set_text("Keyboard")?;
        }
        if e.value & 0xffff == 0x76 {
            let image = weak_image.upgrade().unwrap();
            weak.upgrade()
                .unwrap()
                .set_text(if image.image_status()? == 2 {
                    "Image ready"
                } else {
                    "Image pending"
                })?;
        }
        if e.value & 0xffff == 0x77
            && let Some(path) = &image_path
        {
            let image = weak_image.upgrade().unwrap();
            image.image_source("", 192, 144)?;
            image.image_source(path, 193, 145)?;
        }
        if e.value & 0xffff == 0x7b {
            close.upgrade().unwrap().close()?;
        }
        Ok(())
    })?;
    if let Some(path) = args.first().filter(|s| !s.starts_with("--")) {
        image.source(path, 192, 144)?;
    }
    if args.iter().any(|s| s == "--throughput") {
        let names: Vec<_> = (0..64).map(|i| format!("Update {i}")).collect();
        let properties: Vec<_> = names.iter().map(|s| Property::text(&label, s)).collect();
        let start = std::time::Instant::now();
        for _ in 0..1000 {
            for p in &properties {
                window.update(&[*p])?;
            }
        }
        let single = start.elapsed().as_secs_f64() * 1000.;
        let start = std::time::Instant::now();
        for _ in 0..1000 {
            window.update(&properties)?;
        }
        println!(
            "{{\"mutations\":64000,\"single_ms\":{single:.3},\"batch_ms\":{:.3}}}",
            start.elapsed().as_secs_f64() * 1000.
        );
        return Ok(());
    }
    window.run()
}
