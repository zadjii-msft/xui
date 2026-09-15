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

- [Native architecture](architecture.md): Window hosting, drawing, accessibility, lists, and worker ownership.
- [Test coverage and protocols](testing.md): Regression scope, fixture behavior, and measurement methods.
- [WinUI maintainer handoff](winui-maintainer-handoff.md): Source ownership, fidelity gaps, and regression procedures.
- [Declarative language plan](xui-language-plan.md): Compiler and reload contracts, delivery stages, and acceptance evidence.
- [Control roadmap](control-roadmap.md): Reference research, family coverage, and remaining work.
- [Shell menu discovery](shell-menu-discovery.md): Worker lifetime, cancellation, and safe menu replacement.
- [Button styling foundation](control-styling.md): Sparse styles, binding lifetimes, native paint checks, and performance evidence.

The language guide describes the current syntax.
The original language plan records an earlier, narrower control set.
Handoff branch names, uncommitted-state warnings, and local build paths describe the handoff date, not this checkout.

## Historical evidence

- [Explorer history](explorer-history.md): Compact headers, WIC thumbnails, and Shell icons.
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
