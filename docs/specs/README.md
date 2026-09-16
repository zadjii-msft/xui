# Guides and specifications

These documents describe XUI for application authors.
The root [README](../../README.md) introduces the framework.
[CONTRIBUTING](../../CONTRIBUTING.md) contains build and test instructions.

## Start here

- [Declarative XUI](xui-language.md): Components, C# state, project integration, and hot reload.
- [Application composition and lifecycle](application.md): C++ examples, controls, sizing, scrolling, ownership, and accessibility.
- [C ABI, C#, and Rust](bindings.md): Binding coverage, examples, data limits, and error contracts.
- [WinUI-style appearance](winui-style.md): Optional style selection, supported controls, and platform boundaries.
- [Control styles and color resources](control-styling.md): Shared schemas, named parts, typography, state rules, declarative authoring, and binding contracts.
- [Control styling inventory](control-styling-inventory.md): Delivered coverage, exact value limits, retained-child paths, and remaining native/binding gaps.

## Control contracts

- [Foundation controls](foundation-controls.md): Choices, ranges, numeric inputs, popups, and progress.
- [Collections and asynchronous data](collections.md): Virtual lists, grids, trees, selection, and worker delivery.
- [Context menus, tabs, and input](menus-and-input.md): Native menus, split panes, folder suggestions, and keyboard behavior.
- [Commands and navigation](commands-and-navigation.md): Command surfaces, palettes, breadcrumbs, Shell commands, and title bars.
- [Images and thumbnails](images.md): Decode limits, caching, cancellation, and Shell icons.
- [Documents, dialogs, and color](documents.md): Native editors, password access, modal content, and form controls.
- [Scenes and native hosts](scenes-and-hosts.md): Vector shapes, offline maps, media, and optional web content.

The control references describe the C++ API unless stated otherwise.
The [binding reference](bindings.md) takes precedence for C# and Rust coverage.
Public headers and binding source define the available symbols.

## Samples

- [Control gallery](gallery.md)
- [C# and C++ file explorers](file-explorers.md)
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
