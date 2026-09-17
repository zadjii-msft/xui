# Maintainer notes

This directory contains context for maintainers and coding agents.
It is not the entry point for application authors.
Public behavior belongs in [docs/specs](../specs/README.md).
Build and test commands belong in [CONTRIBUTING](../../CONTRIBUTING.md).

## Before a change

1. Read the relevant public contract.
2. Inspect the current source before relying on a handoff.
3. Use the source maps and test descriptions for the affected area.
4. Preserve unrelated worktree changes and existing behavioral safeguards.
5. Update the public contract and the relevant maintainer note separately.

## Implementation context

- [Documentation site maintenance](../../CONTRIBUTING.md#retype-preview-and-github-pages): Retype source selection, generated navigation, checks, and GitHub Pages deployment.
- [Native architecture](architecture.md): Window hosting, tab drag ownership, drawing, accessibility, lists, and worker ownership.
- [Test coverage and protocols](testing.md): Regression scope, sample and Designer release checks, fixture behavior, and measurement methods.
- [WinUI maintainer handoff](winui-maintainer-handoff.md): Current choice, badge, menu, toggle, and progress notes, historical source ownership, and regression procedures.
- [Declarative language plan](xui-language-plan.md): Compiler and reload contracts, editor syntax packages, delivery stages, and acceptance evidence.
- [Designer source map](xui-language-plan.md#designer-source-map): Native editor, runtime compilation, and preview ownership.
- [Control roadmap](control-roadmap.md): Reference research, family coverage, and remaining work.
- [Shell menu discovery](shell-menu-discovery.md): Worker lifetime, cancellation, and safe menu replacement.
- [Control styling implementation and evidence](control-styling.md): Shared styles, binding lifetimes, family coverage, native paint checks, and performance evidence.

The language guide describes the current syntax.
The original language plan records an earlier, narrower control set.
Handoff branch names, uncommitted-state warnings, and local build paths describe the handoff date, not this checkout.

## Historical evidence

- [Explorer history](explorer-history.md): Folder identity, Find input, navigation menus and hover cards, tab icons, compact headers, and thumbnails.
- [Rendering history](rendering-history.md): Complete text frames and native context menus.
- [Task Manager history](task-manager-history.md): Grid delivery, retained resources, and graphics-memory investigations.
- [Earlier milestones](milestone-history.md): Suggestions, navigation, images, Windows integration, layout, and performance baselines.
- [Binding history](bindings-history.md): ABI compatibility, independent failures, deployment sizes, and measurements.

These archives preserve earlier README reports, including unsuccessful runs and their limitations.
They are not current test results.
Paths under `build` identify local evidence that can be absent from a fresh checkout.
The [memory report](../specs/windows-gui-memory.md) explains the corrected graphics conclusions.

## Where new information belongs

Keep the README brief and sample-first.
Put public contracts and design proposals in `docs/specs`.
Put reusable build procedures in `CONTRIBUTING.md`.
Put source maps, handoffs, unresolved work, and dated evidence here.

Link each new document from the appropriate index.
Extend a relevant document instead of creating another unstructured status log.
Record the date, commit or build, commands, observed result, and remaining uncertainty for new evidence.
Never treat a successful API call as proof of visible output, focus, cleanup, or performance.
