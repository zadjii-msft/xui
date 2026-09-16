# Tutorials

Build a small task card with a native text input, a completion toggle, and shared styles.
The main path uses `.xui` for layout and C# for behavior.
It teaches the same ownership and event rules used by the handwritten bindings.

The application keeps data in memory.
It does not write tasks to disk or start background work.
The final [sample source](sample/) contains the complete application.

## Before you start

Use Windows and the toolchain described in [CONTRIBUTING](../../../CONTRIBUTING.md#requirements).
Build a native DLL for your application's architecture.
The [tutorial sample procedure](../../../CONTRIBUTING.md#gitbook-tutorial-sample) contains the build and run commands.

You need basic C# knowledge, including methods, properties, and events.
No XAML knowledge is required.
The `.xui` compiler is part of this repository, not a published package.

## Learning path

| Chapter | What you build | What you learn |
| --- | --- | --- |
| [1. Your first window](01-first-window.md) | A window with a title and button | Project integration, the window owner, and the generated component |
| [2. State, input, and validation](02-state-and-input.md) | An editable task card | State dependencies, callbacks, native input, and explicit validation |
| [3. Layout and existing controls](03-layout-and-content.md) | A structured card with progress | Stack, Grid, DIPs, and `Content` |
| [4. Reusable components](04-components.md) | Two independent task cards | Parameters, `Root`, attachment, and ownership |
| [5. Styles and themes](05-styling.md) | A styled task card | Resources, parts, states, local overrides, and high contrast |
| [6. Collections and background work](06-collections-and-work.md) | A separate virtual-list exercise | Stable identities, immutable sources, and cancellation boundaries |
| [7. Reload and diagnose](07-reload-and-errors.md) | A controlled edit loop | In-place updates, structural replacement, and error recovery |
| [8. Ship an accessible application](08-delivery.md) | A release-ready checklist | Native input checks, deployment, and application limits |

Chapters 1 and 2 provide complete versions of `TaskCard.xui`.
Later chapters identify replacements and additions.
Only one version of each component belongs in a project at a time.
The [final task card](sample/TaskCard.xui) combines chapters 1 through 5.
Chapter 6 is an independent exercise, not a hidden dependency of that sample.

For other languages, start with [C++](../languages/cpp.md), [C](../languages/c.md), or [Rust](../languages/rust.md).
Use the [control catalog](../controls/README.md) to choose the next feature.
