# Rust applications

The `xui` crate supplies safe wrappers over the native C ABI.
The `xui-sys` crate contains raw declarations.
Raw calls remain `unsafe`.
Both crates use the same Windows engine as C++ and C#.

The repository sample uses Rust edition 2024 and the Windows MSVC toolchain.
This binding does not generate Rust from `.xui`.

## A minimal application

This complete `src\main.rs` creates a label and an Apply button.
The callback borrows the native owner through a weak element reference.
It returns errors through `xui::Result`.

```rust
use xui::{Axis, Result, Window};

fn main() -> Result<()> {
    let window = Window::new("Rust application", 480., 240.)?;
    let root = window.stack(Axis::Vertical)?;
    root.padding(20.)?;
    root.spacing(12.)?;

    let status = window.label("Ready")?;
    status.automation_id("status")?;
    let apply = window.button("Apply")?;
    apply.automation_id("apply")?;

    let weak_status = status.downgrade();
    apply.on_event(move |event| {
        if event.kind == 1 {
            if let Some(status) = weak_status.upgrade() {
                status.set_text("Applied")?;
            }
        }
        Ok(())
    })?;

    root.add(&status, 0.)?;
    root.add(&apply, 0.)?;
    window.set_content(&root)?;
    window.run()
}
```

Event kind `1` is the ABI's `XUI_CLICK`.
The baseline safe `Event` uses a numeric `kind` and a `u64` value.
The callback filters the event kind rather than treating every event as activation.

## Project integration

An application uses the repository's `xui` crate as a path dependency.
[`sample\Cargo.toml`](../../../bindings/rust/sample/Cargo.toml) shows the dependency and edition.
This guide does not assume a published crate.

`xui-sys` uses `XUI_LIB_DIR` to locate the native import library.
Without that variable, its build script uses the repository's `build\arm64\Release` path.
The application also needs the native DLL at runtime.

The sample's [`build.rs`](../../../bindings/rust/sample/build.rs) embeds the native-control manifest through MSVC linker arguments.
An external application needs equivalent manifest configuration.
That configuration preserves common-controls v6 and per-monitor DPI behavior.

The [Rust build procedure](../../../CONTRIBUTING.md#rust) describes target selection, environment variables, and checks.
The repository's Cargo configuration defaults to ARM64 and the static CRT.
An x64 application requires an explicit x64 target and matching native binaries.

## Controls, layout, and events

| Task | Rust API |
| --- | --- |
| Create a control | `window.button("Apply")?` |
| Create a horizontal row | `window.stack(Axis::Horizontal)?` |
| Share remaining space | `root.add(&child, 1.)?` |
| Retain a scrolling subtree | `window.scroll_view(&content, "Preferences")?` |
| Set dimensions | `fixed_size`, `preferred_size`, `minimum_size`, `maximum_size` |
| Read or change text | `text` and `set_text` |
| Subscribe to a control | `on_event`, with a `FnMut(Event) -> Result<()>` closure |
| Revoke a control subscription | `unsubscribe` |
| Request closure | `window.close()` |

TextInput emits change kind `2` and submit kind `3`.
Its committed text comes from `text()`.
Toggle change events carry the checked state as a nonzero value.
The [complete Rust sample](../../../bindings/rust/sample/src/main.rs) shows these patterns with weak references.

The extension adds typed feature factories and value records.
The [feature sample](../../../bindings/rust/sample/src/features.rs) shows a virtual source, range events, a dialog, and an offline map.
The [coverage table](../bindings.md#current-coverage) and [advanced gaps](../bindings.md#advanced-api-gaps) define the supported API.

## Styles

`ControlStyle`, `PartStyleValues`, `PartStyle`, and `ControlStyleRule` expose the shared style model.
The [Toggle examples](../control-styling.md#toggle-pilot) include the Rust constructors and attachment methods.
The [Button example](../bindings.md#binding-examples) shows the separate `ButtonStyle` API.
The [inventory](../control-styling-inventory.md) describes supported properties and native boundaries.

A style definition does not retain a window.
The native control retains its applied definition after the Rust definition drops.
Local values remain separate from shared definitions.
Styles do not add Shell menu styling or arbitrary templates.

## Ownership and callback cycles

`Window` and controls retain an `Rc`-owned native arena.
The final owner destroys that arena on its UI thread.
Dropping a Window wrapper does not destroy controls that still retain its owner.
No control has an independent native destruction operation.

A registered closure belongs to the same owner.
A strong control or Window capture inside that closure can create an `Rc` cycle.
The sample uses `downgrade()` and `upgrade()` to avoid that cycle.
`Window::downgrade` supplies the same pattern for callbacks that request closure.

Controls are neither `Send` nor `Sync`.
All source callbacks, completions, and property calls require the creating UI thread.
The binding provides no automatic worker dispatcher.
Background work must return immutable results through an application-owned UI queue.
The caller must not initialize COM as MTA.

## Errors and cancellation

Native status failures become `Error { status, message }`.
Application methods return `Result`.
A callback can return an error instead of panicking.
Callback failure requests closure and causes the enclosing native operation to fail.

The trampoline contains unwinding panics and preserves an error message.
It cannot recover from a process-aborting panic configuration.
Same-source recursive event delivery fails instead of recursively borrowing the closure.

Tree and map request objects cancel on drop.
Successful completion consumes their tokens.
Window closure revokes pending delivery.
Password access uses a scoped `with_password` byte slice.
The [binding ownership contract](../bindings.md#ownership-and-data-limits) defines these limits.

## Deployment and next steps

The executable needs `xui.dll` beside it or on its DLL search path.
`xui.lib` is a link-time input, not a runtime replacement for the DLL.
The executable and DLL must use the same architecture.
Static CRT selection does not embed the XUI DLL.

- [Deployment reference](../../../CONTRIBUTING.md#nativeaot-and-deployment)
- [Tutorials](../tutorials/README.md)
- [Control catalog](../controls/README.md)
- [Rust ownership contract](../bindings.md#rust-ownership-and-use)
- [Language comparison](README.md)
