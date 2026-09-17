# `.xui` language and development reload

This plan records the initial implementation and its acceptance evidence.
The [language guide](../specs/xui-language.md) describes the current syntax and expanded control set.
Use [CONTRIBUTING](../../CONTRIBUTING.md) for build and test commands.

## Designer source map

The [designer guide](../specs/designer.md) describes the standalone development tool.
`bindings/dotnet/Designer/DesignerLayout.xui` defines its shell.
`DesignerApplication.cs` owns native documents, file operations, recovery drafts, and the bounded compiler queue.
`DesignerWorkspace.cs` owns a bounded parse queue and one cancellable visual edit operation.
It parses exact native editor snapshots and applies edits with the native range-replacement API.
It rejects stale source or revision results before the native call.
`DesignerHierarchy.cs` owns revision-scoped TreeView keys and releases immutable source handles after attachment.
`DesignerInspector.cs` connects the declarative inspector to supported literal arguments and explicit expression limits.
`DesignerHierarchyLayout.xui` and `DesignerInspectorLayout.xui` define the side panes.
`DesignerBuilderSmoke.cs` runs the dedicated `--builder-smoke` sequence against the real native controls.
`Designer.LayoutTests` links the production layouts for native geometry and editor-state tests without the preview compiler.
`Designer.WorkspaceTests` runs the shared builder smoke with the production controllers and source model, without a preview host.
`PreviewCompiler.cs` runs `XuiGenerator` and Roslyn, with semantic discovery of the generated component.
It emits `Build(Window)` and `Root(object)` wrappers for the component and its unattached root.
The `XUI_DESIGNER` parse option enables source-preorder metadata in the generated component.
The wrapper also exposes typed `NodeCount(object)` and `Node(object, int)` entry points.
It does not execute authored code during compilation.

`PreviewHost.cs` owns a stable `ContentHost` inside the designer window.
It constructs a candidate within `ContentUpdate` before replacing the previous content.
The host retains the generated component until scope retirement, then requests collectible assembly unloading.
Authored tasks or static references can prevent collection.
The host checks the source version before construction and before commit.
Successful commit completes native materialization and layout before status delivery.
The host reports construction and scoped managed event exceptions to the editor.
The preview shares the editor UI thread and does not isolate authored code.
The host binds the metadata entry points once per candidate and clears those delegates during retirement.
`PreviewNodeSnapshot.cs` defines value-only snapshots of the current arranged node bounds, binding type, and numeric control identity.
All snapshot reads check UI-thread access and the exact applied source version.
The [snapshot contract](../specs/designer.md#applied-preview-snapshots) distinguishes arranged bounds from visible geometry.

`Designer.Tests` covers compilation, diagnostics, cancellation, input limits, and the generated wrapper.
`DesignerTemplates.cs` exposes the embedded example catalog to the workspace.
`Designer.TemplateTests` compiles every catalog entry without a native DLL.
`DesignerDocumentStore.cs` supplies the shell's file and recovery model.
It compares raw file hashes before replacement and writes each destination through a temporary file.
Recovery metadata connects a source hash to the original path and file hash.
A partial snapshot reports an error instead of restoring stale file identity.
`Designer.DocumentTests` covers this model without a native DLL.
`DesignerRecoveryDialog.cs` supplies the shell's native recovery dialog.
`DesignerRecoveryLayout.xui` defines its content.
The dialog shows bounded source previews, requires deletion approval, and prevents recovery over dirty source.
`Designer.RecoveryTests` exercises its native controls against isolated draft files.
`DesignerFileSmoke.cs` runs `--file-smoke` against the complete application with an isolated recovery directory.
The application keeps file errors in a separate status label so compiler diagnostics remain available.
`DesignerDiagnostics.cs` maps compiler messages to revision-scoped source selections for the next workspace.
It uses the reported compiler coordinates and preserves exact native paragraph offsets.
Generated-file locations, invalid coordinates, and stale source cannot produce a source selection.
`Designer.DiagnosticsTests` covers this model and real compiler output without a native DLL.
`DesignerDiagnosticNavigator.cs` adds native diagnostic navigation through `DesignerDiagnosticsLayout.xui`.
It checks the current source revision and displayed diagnostic text before changing either native selection.
`Designer.NavigationTests` covers its buttons, F8 routing, Unicode selections, and invalidation.
`DesignerWorkspace.cs` connects the hierarchy and inspector to native source transactions.
Its grouping actions call `VisualDocument.WrapNode` and `UnwrapNode` through the same cancellation and revision checks as property edits.
`DesignerInspectorLayout.xui` defines the wrap and unwrap controls.
`Designer.GroupingTests` covers the native buttons, hierarchy-only shortcuts, source selection, and undo.
The designer's `--smoke` mode covers the native editor and preview lifecycle.
`xui_abi_features_tests --activation` covers the opt-in no-activation window contract.
The normal window activation default remains unchanged.

### Applied node-map evidence

The node-map tranche passed 89 compiler assertions, 780 source assertions, and 1,456 embedded preview assertions on ARM64 Release.
The desktop fixture compares parser preorder with native control text and runtime binding types.
It covers nested Stack, Grid, and external Content nodes, plus a non-Stack replacement root.
Its bounds check compares the snapshot with the actual label HWND in client coordinates at the current DPI.
One hundred repeated map reads leave native handle counts and peer identities unchanged.
The suite rejects wrong-thread reads and invalid IDs.
It preserves the old map after construction failure and rejects maps for pending, failed, or retired revisions.
Retained value snapshots do not prevent collection of the retired preview assembly context.
The designer `--smoke` also passed.

### Visual source tools

`Xui.Generator/Parser.cs` records authored node and argument ranges during the existing parse.
`XuiSourceParser.cs` exposes an immutable projection without emission, runtime construction, or another grammar.
It adds bounded input, nesting, Unicode, cancellation, and token-trivia diagnostics for visual tooling.
The ordinary generator retains its existing grammar and emission path.

`Designer/VisualDocument.cs` owns revision-scoped node selection and exact source replacement proposals.
Its compilation helper runs the existing generator and emits to memory, without loading an assembly.
It does not depend on `PreviewCompiler`, `PreviewHost`, or native editor controls.
The UI must discard proposals after any source or revision change.
The [public contract](../specs/designer.md#source-editing-api) defines ranges, approvals, placement rules, and editor integration.

`Designer.SourceTests` links this model directly and rejects native DLL loading.
The suite covers all templates, literal and expression boundaries, raw C# strings, comments, Unicode, line endings, and stale-source refusal.
It also covers structural edits, fixed arity, ownership, Grid placement, and resulting assembly emission.
The source suite passed 646 assertions on Windows on September 16, 2026.
The same change passed 41,813 generator assertions and 78 existing designer compiler assertions.
These results use designer baseline `ae3ddea` and the source-tools tranche.
Commands are in [CONTRIBUTING](../../CONTRIBUTING.md#xui-designer).

`XuiGenerator.cs` also emits an opt-in `XUI_DESIGNER` element lookup and node count.
The existing `Collect` traversal supplies the same preorder as the parser projection.
Each lookup returns its existing node field after the window access check.
`DesignerMetadataTests.cs` compiles the existing generator-test fakes as fixture resources.
The tests compare element identity with every authored reference, including Content and non-Stack roots.
They also cover thread access, invalid IDs, unchanged field sets, and complete member omission without the symbol.
With these checks, the source suite passed 780 assertions on September 16, 2026.
The generator and designer compiler suites retained their previous counts.
A managed Designer build with `XUI_DESIGNER` also passed against the actual bindings.

`VisualDocument.WrapNode` and `UnwrapNode` add revision-safe grouping and ungrouping.
They retain subtree text without reindentation and transfer exact authored placement arguments.
Comma removal uses Roslyn tokens between existing argument spans, not a second grammar.
Unwrapping refuses conflicting child placement, wrapper configuration, identity loss, and discarded header comments.
`Program.Wrapping.cs` covers these rules, raw and verbatim strings, native CR selection, root changes, and fixed-arity parents.
The grouping tranche passed 1,402 source assertions, 41,813 generator assertions, and a managed Designer build on September 16, 2026.

`Emitter.Map(Expression)` emits enhanced `#line` spans only for unchanged authored source slices.
Generated-prefix offsets keep inline Grid constructors and Stack flex expressions accurate.
Grid track tuples emit each authored expression under a separate mapping.
Synthesized expressions and columns beyond the directive limit retain the original line-only mapping.
`Program.Diagnostics.cs` checks exact UTF-16 start and end locations, line endings, nested expressions, raw strings, and long-source compatibility.
It links the unchanged `PreviewCompiler.cs` to check actual preview diagnostic text.
The diagnostic tranche passed 1,561 source assertions, 41,813 generator assertions, 78 designer compiler assertions, and a managed Designer build on September 16, 2026.

`Designer.Preview.Tests` covers native embedded layout, scoped ownership, repeated replacement, stale versions, managed callback errors, and editor preservation.
The designer's `--smoke` mode covers the editor, preview recovery, and file behavior.
`xui_abi_features_tests --activation` covers the opt-in no-activation window contract.
The normal window activation default remains unchanged.

### Embedded preview evidence

On 2026-09-16, the ARM64 Release build passed the native and managed embedded preview regressions.
The worktree started from designer baseline `ae3ddea`.
`Designer.Tests` passed 83 compiler assertions.
`Designer.Preview.Tests` passed 1,282 assertions.
The latter includes 100 replacements, 20 failed candidates, resource retirement, collectible assembly unloading, scoped callback recovery, and pending-close cycles.
The existing designer `--smoke` passed with the minimal embedded shell.

`xui_content_host_window_tests` passed 200 native replacements across Classic and WinUI.
It also covered editor state, popup retirement, inactive pages, map and runtime retirement, stale UIA providers, and fatal materialization errors.
The core regression passed.
Existing ABI regressions passed 347 base assertions, 263,627 feature assertions, and 19 activation assertions.

The foundation-window regression exceeded its native-phase deadline in this environment.
The same failure reproduced with the baseline `ae3ddea` application implementation.
Active WebView2 content was not part of these checks.
These results do not establish safety against arbitrary authored code or independent process isolation.

### Visual workspace evidence

On 2026-09-17, the combined ARM64 Release shell passed the full designer `--builder-smoke` with 16 assertions.
The existing `--smoke` also passed, including its expected compiler, construction, and file errors.
`Designer.WorkspaceTests` passed the same 16 assertions without authored preview execution.
`Designer.LayoutTests` passed eight native geometry and editor-state assertions.
`Designer.Tests` passed 83 compiler assertions.

The controller increment is `be127a4`.
This run includes the embedded preview core `df83ca9` and the native tree correction `ac2ffdc`.
The latter restores selection notifications when a collapsed ancestor replaces a descendant as the focused tree node.
The shared smoke reproduces the prior inspector mismatch and requires the corrected behavior.
It also covers deep tree expansion, surrogate-safe labels, native keyboard focus, one-line CR offsets, and bursts of source changes.

### File and recovery integration evidence

On 2026-09-17, the ARM64 Release application passed 15 `--file-smoke` assertions.
The sequence covers automatic drafts, native undo cleanup, recovery copies, original-draft retention, disk conflicts, and Save to a new path.
It also covers persistent file errors, preserved compiler diagnostics, and native source repairs after Open loads invalid syntax.
The existing `--smoke` passed, and `--builder-smoke` passed 16 assertions.
`Designer.DocumentTests` passed 49 assertions.
`Designer.RecoveryTests` passed 17 native dialog assertions.
`Designer.LayoutTests` passed eight native geometry and editor-state assertions.

## Goal

Developers author a retained XUI application with a small declarative language.
The build compiles `.xui` files into ordinary C#.
The release application uses the existing XUI controls, layout, input, and accessibility.
It has no additional virtual tree, reconciler, expression interpreter, or layout engine.

The development loop detects `.xui` edits.
Supported edits update the current window without a restart.
Unsupported edits require an explicit restart or a legal window replacement.
The application must never report a successful reload while it displays an obsolete tree.

The performance baseline is equivalent handwritten C# that uses the same bindings.
It is not the static C++ application.
NativeAOT publishing remains separate from the managed development loop.

## Initial language

```text
namespace Demo;

component Counter {
    state int Count = 0;

    view {
        VStack(spacing: 8, padding: 16) {
            Text($"Count: {Count}", id: "count");
            Button("Increment", click: Increment, id: "increment");
        }
    }

    code csharp {
        void Increment() => Count++;
    }
}
```

The namespace is optional.
Each file declares one component.
The initial controls are `VStack`, `HStack`, `Text`, `Button`, `Toggle`, and `TextInput`.
The compiler resolves their arguments to typed XUI calls.
The `id` argument supplies the automation ID of a control.
Stacks accept `spacing` and `padding`.

State initializers run when the component is created.
State changes update the expressions that depend on that state.
The compiler must define its dependency rules.
It must reject unsupported dependency patterns instead of adding a runtime scan.

The `code csharp` block contains C# members.
The initial implementation accepts methods in that block.
Fields use `state` declarations.
The parser must distinguish code braces from braces in strings, interpolations, and comments.
C# errors must refer to the original `.xui` file and line.
Unknown controls, arguments, and language constructs must produce build errors.

The first version supports a fixed control tree.
It does not promise dynamic child collections, arbitrary row templates, or a complete replacement for XAML.
Additional controls can extend the compiler after the initial contracts pass their acceptance checks.

## Compilation contract

The compiler processes each input independently where possible.
Generated names remain stable across edits to unrelated components.
Generated output has deterministic ordering.
The build does not rewrite unchanged generated files.

The build integration includes `.xui` inputs in the watch list.
It excludes generated outputs from that list.
It handles input creation, deletion, renaming, and syntax errors.
A deleted input must not leave an obsolete compiled component.
An input-list manifest invalidates compilation after input deletion or renaming.
The manifest changes only when the input list changes.
It has a separate path for each target framework and runtime identifier.

The generated construction code calls the current `Window` factories and attaches children once.
Named event handlers attach once.
Generated state setters call only the relevant property-update methods.
The compiler preserves the error and thread contracts of those methods.

Release output excludes development registries, reload metadata, and refresh subscriptions.
The application does not need the parser or compiler at runtime.
The generated code must build with the existing AOT-compatible bindings.

## Reload contract

### Supported in-place edits

The first live-edit targets are authored text, stack spacing, stack padding, and supported control properties.
Supported C# method-body edits affect later calls to those methods.
Changes to supported property expressions use the current component state.

An accepted .NET code update does not automatically refresh native controls.
A development-only update handler requests a generated refresh on the creating UI thread.
The refresh does not rerun state initializers or attach handlers again.
It must not reset user input that the application owns.

Stable generated names help .NET associate old and new methods.
The UI refresh also needs a shape check.
A valid .NET code delta can still describe an invalid live XUI tree.
Explicit ID expressions form part of that shape.
Controls without IDs use positional identity.

### Structural edits

Adding a control, changing its type, and changing parent-child relationships are structural edits.
The initial implementation does not patch those relationships in place.
It reports the required fallback explicitly.

The baseline fallback is a process restart through the development tools.
An alternative is a development host that replaces the entire window.
Window replacement must call `Close`, wait for `Run` to return, and dispose the old window before it creates the replacement.
Neither approach bypasses the native topology guard.

Process restart resets transient state unless the application saves it.
Window replacement can retain an explicit application-state model.
Neither approach automatically retains native focus, selection, scrolling, or resource state.
The user guide must identify the actual behavior.

### Errors and shutdown

Invalid source must produce a diagnostic and no partial update.
A later valid edit must recover without a manual cleanup of generated files.
Refresh errors must reach the existing error channel or a visible development diagnostic.

Recoverable errors retain the generated declarations.
The generator emits both its diagnostic and a mapped C# `#error` directive.
The C# directive blocks hot deltas that the SDK otherwise permits despite generator diagnostics.
This mechanism has no last-good-output cache.

Some errors prevent the generator from emitting the component declarations.
The SDK can request a process restart for those errors.
A corrected file starts the application again, with fresh state.
The user guide distinguishes this behavior from in-place recovery.

Reload requests must not access a closed window.
Window shutdown must revoke development subscriptions and queued work.
The release application must not poll for edits.

## VS Code package

The extension registers the `.xui` file association.
It supplies a TextMate grammar, bracket pairs, comments, indentation rules, and snippets.
The grammar distinguishes the UI language from embedded C#.

Tokenizer checks cover keywords, control names, named arguments, state declarations, and namespace declarations.
They also cover nested C# blocks, interpolated strings, comments with braces, and the transition back to UI syntax.

The extension produces an installable `.vsix`.
The package excludes development dependencies and test fixtures.
Syntax support does not imply a language server, semantic completion, or a designer.
The implementation does not publish the extension or change global VS Code configuration.

## Delivery stages

1. Define the grammar, generated API, update rules, and failure behavior.
2. Implement the parser, code generator, diagnostics, and build integration.
3. Add an application sample and restart-on-save.
4. Implement development-only live updates and explicit structural fallback.
5. Implement and package the VS Code extension.
6. Run the acceptance checks and correct failures.
7. Document the commands, supported edits, and remaining limits.

The compiler and extension can proceed in parallel after the grammar is stable.
Native build preparation can also proceed in parallel.
The end-to-end edit checks require the compiler, reload host, and native library.

## Acceptance checks

### Compiler and build

- A clean build produces an application from `.xui` inputs.
- Multiple inputs produce distinct components without unstable output.
- A no-op build preserves unchanged generated output.
- Adding, deleting, and renaming inputs updates the build correctly.
- Invalid language syntax produces a file-and-line diagnostic.
- Invalid embedded C# produces a diagnostic in the original input.
- Braces inside comments and strings do not end a block.
- Unsupported controls and arguments produce explicit diagnostics.
- State setters update their dependent controls without rebuilding the tree.
- Release output has no development reload machinery.

### Native application and watch loop

- The sample opens a real XUI window.
- A button event changes generated state and the native label.
- A `.xui` text edit updates the existing window and preserves state.
- A spacing edit updates existing layout.
- A supported handler edit changes subsequent event behavior.
- Repeated edits do not multiply event subscriptions.
- A structural edit uses the documented fallback.
- An invalid edit reports an error instead of a partial update.
- A valid edit after an invalid edit recovers.
- Closing the application leaves no orphan watcher or application process.
- The existing managed binding checks still pass.
- The focused native ABI checks still pass after any native bridge change.

### VS Code package

- Tokenizer checks exercise the real TextMate grammar.
- Embedded C# highlighting survives nested blocks and string braces.
- The extension package includes its grammar, snippets, and language configuration.
- The package excludes dependency directories and fixtures.
- The installation command uses the generated package.

### Performance evidence

The initial checks inspect generated calls and release dependencies.
They compare the generated update path with handwritten C#.
They record the complete save-to-visible-change loop where the test environment permits it.

The project must not claim a latency bound or memory improvement without measurements.
A large application still pays its compilation and startup costs after a process restart.
Per-file generation does not eliminate those costs.

## Existing code anchors

- `bindings\dotnet\Xui\Window.cs`: factories, property batches, subscriptions, and UI-thread ownership.
- `bindings\dotnet\Xui\Controls.cs`: typed controls, fluent setters, and events.
- `bindings\dotnet\Xui\Features.cs`: feature controls and immutable collection sources.
- `bindings\features.json`: existing control metadata.
- `bindings\generate_features.py`: existing binding-generation precedent.
- `src\c_api.cpp`: topology checks, handle ownership, and property updates.
- `src\c_api_features.inc`: feature construction and attachment checks.

## Progress

The compiler, development host, VS Code package, and integration checks are complete.
The native ABI and its topology rules remain unchanged.
The [user guide](../specs/xui-language.md) describes the syntax and reload behavior.
[CONTRIBUTING](../../CONTRIBUTING.md) contains the build and watch commands.

Acceptance checks passed on Windows ARM64 with .NET SDK 10.0.401:

- The generator suite passed 92 assertions.
- The real MSBuild suite passed 9 assertions, including last-input deletion and unchanged no-op timestamps.
- The native watch suite passed 56 assertions.
- The VS Code package passed 16 tokenizer and configuration tests.
- The VSIX content check found the expected eight runtime files and no development dependencies.
- The existing C# suites passed 28 wrapper assertions and 79 feature assertions.
- Both focused native ABI suites passed.
- The final NativeAOT sample published and passed native initial-text, button, state-update, and shutdown checks.

The watch suite checked native text, bounds, input values, event behavior, and process lifecycle.
It did not use successful reload messages as a substitute for native UI checks.
It also checked that invalid edits produced no layout changes or extra completion markers.
The suite covered structural replacement, severe-error restart, and both recovery paths.

The generator checks include native text/name aliasing, reserved identifiers, generic hidden dependencies, and state-free components.
Release checks reject references to the development host, compiler, and Roslyn.
The development host uses a Win32 message dispatcher instead of a native ABI extension.

The successful watch artifacts are under `build\xui-language-check\59a4faa0f6a34718b229d2bf0ce9f88e`.
That directory contains the fixture, watcher logs, build log, and edit durations.
The local extension package is `integrations\vscode-xui\dist\xui-0.1.0.vsix`.
The extension is not published or installed globally.

The initial scope remains fixed compositions and the six control names listed in this plan.
Dynamic child collections, arbitrary row templates, a language server, and a visual designer are outside this delivery.
