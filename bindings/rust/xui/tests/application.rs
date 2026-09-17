use xui::{Application, Axis, Result, WindowState};

#[test]
fn independent_windows_and_worker_sender() -> Result<()> {
    let app = Application::new()?;
    let a = app.create_window("Rust opener", 300.0, 200.0)?;
    let b = app.create_window("Rust survivor", 300.0, 200.0)?;
    for window in [&a, &b] {
        let root = window.stack(Axis::Vertical)?;
        let label = window.label("Independent native window")?;
        root.add(&label, 1.0)?;
        window.set_content(&root)?;
        app.show(window)?;
    }
    let weak_b = b.downgrade();
    a.on_closed(move || {
        let b = weak_b.upgrade().expect("Application retains survivor");
        assert_eq!(b.state()?, WindowState::Open);
        b.close()
    })?;
    let weak_a = a.downgrade();
    app.post(move || weak_a.upgrade().expect("Application retains opener").close())?;
    let sender = app.dispatcher();
    std::thread::spawn(move || sender.post(|| Ok(()))).join().unwrap()?;
    app.run()?;
    assert_eq!(a.state()?, WindowState::Closed);
    assert_eq!(b.state()?, WindowState::Closed);
    Ok(())
}
