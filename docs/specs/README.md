# XUI handbook

These documents describe XUI for application authors.
The root [README](../../README.md) introduces the framework.
[CONTRIBUTING](../../CONTRIBUTING.md) contains build and test instructions.

## Learn XUI

XUI is a Windows desktop framework with retained controls, native text editing, and explicit ownership.
It supports C++ and a C ABI, with C# and Rust wrappers.
The `.xui` language generates C#; it is not XAML or a cross-language runtime.
APIs are still evolving.

- [Tutorials](tutorials/README.md): Build a task card, add native input, compose controls, apply styles, and prepare a release.
- [All controls](controls/README.md): Choose a control and find its usage, language availability, and contract.
- [Language guides](languages/README.md): Start with C++, C, C#, Rust, or declarative `.xui` with C#.
- [Book contents](SUMMARY.md): Browse the complete GitBook navigation.

The handbook and the references below use the same Markdown source.
Retype supplies a local preview and static output for GitHub Pages.
See [preview and deployment](../../CONTRIBUTING.md#retype-preview-and-github-pages) for commands and the publication approval requirement.
The repository also retains [GitBook Git Sync configuration](../../CONTRIBUTING.md#gitbook-documentation).
No hosted site is required to read these pages.

## Start here

- [Packages and deployment](packages.md): NuGet, Cargo, native-only C++ integration, sample and Designer archives, and local DLL selection.
- [Declarative XUI](xui-language.md): Components, C# state, project integration, hot reload, and VS Code or Microsoft Edit syntax support.
- [Application composition and lifecycle](application.md): C++ examples, controls, sizing, scrolling, ownership, native file dialogs, and accessibility.
- [C ABI, C#, and Rust](bindings.md): Binding coverage, examples, data limits, and error contracts.
- [WinUI-style appearance](winui-style.md): Optional style selection, supported controls, and platform boundaries.
- [Control styles and color resources](control-styling.md): Shared schemas, named parts, typography, state rules, declarative authoring, and binding contracts.
- [Control styling inventory](control-styling-inventory.md): Delivered coverage, exact value limits, retained-child paths, and remaining native/binding gaps.

## Control contracts

- [Foundation controls](foundation-controls.md): Binary and tri-state choices, selectors, badges, ranges, numeric inputs, popups, and animated progress.
- [Collections and asynchronous data](collections.md): Virtual lists, grids, trees, Miller columns, selection, and worker delivery.
- [Context menus, tabs, and input](menus-and-input.md): Native menus, tab dragging between windows, split panes, folder suggestions, and keyboard behavior.
- [Commands and navigation](commands-and-navigation.md): Menu bars, command surfaces, palettes, breadcrumbs, Shell commands, and title bars.
- [Images and thumbnails](images.md): Decode limits, caching, cancellation, and Shell icons.
- [Documents, dialogs, and color](documents.md): Native editors, syntax highlighting, password access, modal content, and form controls.
- [Scenes and native hosts](scenes-and-hosts.md): Vector shapes, offline maps, media, and optional web content.

The control references describe the C++ API unless stated otherwise.
The [binding reference](bindings.md) takes precedence for C# and Rust coverage.
Public headers and binding source define the available symbols.

## Samples

- [XUI Designer](designer.md)
- [Control gallery](gallery.md)
- [C# and C++ file explorers](file-explorers.md): Navigation, columns, commands, and C# file previews.
- [Minesweeper](../../bindings/dotnet/Minesweeper/README.md)
- [Task Manager](task-manager.md)
- [Thumbnail sample](thumbnail-sample.md)

## Design and technical reports

- [Styles and templates](styling-and-templates-design.md): Application-authored presentation, staged delivery, native-host boundaries, and performance acceptance.
- [WinUI-style design proposal](winui-design-plan.md): Visual targets, implementation stages, and acceptance gates.
- [Windows GUI memory report](windows-gui-memory.md): Measurement methods, graphics allocations, and the limits of the recorded evidence.

Proposals distinguish implemented behavior from future work.
Historical measurements describe their recorded environment, not every Windows system.
Implementation plans and handoffs are in the [maintainer index](../llm/README.md).
