# `.xui` language and development reload

This plan records the initial implementation and its acceptance evidence.
The [language guide](../specs/xui-language.md) describes the current syntax and expanded control set.
Use [CONTRIBUTING](../../CONTRIBUTING.md) for build and test commands.

## Designer source map

The [designer guide](../specs/designer.md) describes the standalone development tool.
`bindings/dotnet/Designer/DesignerLayout.xui` defines its shell.
Its theme-aware toolbar groups file and preview commands above the workspace.
The style button posts `Window.SetVisualStyle` through the dispatcher without a new preview compilation.
Layout and selection fixtures cover both styles, source undo, and retained preview state.
The Output pane starts collapsed with a left-aligned toggle in its status row.
On expansion, that row becomes the header above the diagnostics.
The shell shares one theme-aware panel style across the hierarchy, inspector, and Output.
Common toolbar commands use native icons with their original accessible names.
The toggle posts through the supplied dispatcher before it moves focus or changes layout.
This avoids a nested native focus event inside the button callback.
The application expands Output for compile, preview, and file errors.
`DesignerApplication.cs` owns native documents, file operations, recovery drafts, and the bounded compiler queue.

`DesignerApplication.Commands.cs` maps stable command IDs to existing application actions and current availability checks.
`DesignerCommandPalette.cs` uses the native `CommandSurface` for search, keyboard navigation, dismissal, and focus restoration.
It posts opening and execution through the window dispatcher and checks availability again before each action.
Reopening or disposal cancels a queued command. A pending command prevents duplicate dispatch.

The shell leaves pointer picking before opening the palette, then leaves popup keys to the native router.
`Designer.CommandTests` exercises real native search input and both visual styles.
The application selection smoke covers command shortcuts, file confirmation, and focus inside the inspector.

`DesignerGoTo.cs` and `DesignerGoToLayout.xui` provide the native source-location dialog.
The controller shares `DesignerDiagnostics.Locate` for UTF-16 line and column boundaries.
It captures the source text and application revision when the dialog opens.
Submission checks current field values, and the posted navigation checks the revision and source again after dismissal.
Reopening or disposal cancels queued navigation.
The application refreshes dialog validation after each revision change, including programmatic document replacement.
The source-only Ctrl+G route leaves hierarchy grouping unchanged.
`Designer.NavigationTests` covers native input, Enter/Escape, validation, exact caret positions, cancellation, stale revisions, and undo in both visual styles.
The application selection smoke covers the header action, command palette, focus routing, pointer-mode exit, hierarchy synchronization, and retained preview state.

`DesignerSourceIndentation.cs` handles Enter and leading-whitespace Tab shortcuts only in the focused source editor.
It uses native range replacement for single-action undo and reports rejected edits through Output.
The window's native key router excludes IME composition and modal dialogs before these shortcuts.
`Designer.IndentationTests` covers native text, caret positions, change callbacks, undo, focus, and length-limit errors.
`DesignerSourceComments.cs` handles source-only Ctrl+/ and the palette's Toggle line comments action.
It reads exact native CR-separated lines and excludes a final line touched only at the selection's end.
Comment prefixes follow each line's spaces and tabs. Blank lines remain unchanged.
The controller maps UTF-16 selection endpoints through the prefix edits and uses one snapshot-checked native replacement.
It checks the configured source limit before replacement and reports native refusals without retrying a partial edit.
`Designer.IndentationTests/Program.Comments.cs` covers both visual styles, mixed comments, Unicode, selection boundaries, no-ops, undo/redo, focus, and length limits.
The application selection smoke covers palette dispatch, source shortcuts, compiler updates, native preview removal/restoration, and successive undo operations.

`DesignerWorkspace.cs` owns a bounded parse queue and one cancellable visual edit operation.
It parses exact native editor snapshots and applies edits with the native range-replacement API.
It rejects stale source or revision results before the native call.
`DesignerHierarchy.cs` owns revision-scoped TreeView keys and releases immutable source handles after attachment.
It applies 28-DIP rows, 16-DIP indentation, and reduced row padding through local style values.
Its native query searches complete node kinds and authored argument names/values with ordinal, case-insensitive terms.
`RefreshSearch` orders matches by source span without replacing the tree source or its keys.
`MoveSearch` uses the existing ancestor expansion and selection path, then publishes one selection event.
`SetSearchCurrent` disables navigation as soon as the workspace observes a source change.
Publication rematches the retained query against new node instances and revision-scoped keys.
The query's Enter/Shift+Enter/Escape route precedes source Find, without taking keys from other fields or an open command palette.
`Designer.WorkspaceTests/Program.HierarchySearch.cs` covers native query input, collapsed ancestors, matching, focus, revisions, invalid source, and undo in both styles.
The application selection smoke covers the focus command, exact source selection, independent Escape handling, and retained preview state.
`DesignerInspector.cs` connects the declarative inspector to supported literal arguments and explicit expression limits.
`FindArguments` matches every query term against one property name and can restrict results to authored arguments.
`FilterArguments` retains keys from the complete sorted name list rather than assigning result-index keys.
It keeps the active property as a marked current entry outside the results, without calling `ShowArgument`.
An explicit different-property selection resets the editor through the existing path.
Reselecting the active property preserves its draft.
The clear action clears the query and authored-only state, while selection and source changes retain both filters.
`Designer.WorkspaceTests/Program.PropertySearch.cs` covers native input, stable keys, all draft modes, authored expressions, revisions, and undo in both styles.
The application selection smoke checks the property-search command, visible native query, retained dimension draft, and unchanged preview geometry.
The inspector also filters the control palette by enum name and a short template description.
`FindTemplates` uses ordinal case-insensitive matching for every whitespace-separated query term.
Filtered choices retain enum-based native keys instead of result indices.
An empty result clears the selected template, and the workspace rejects insertion without a template.
Filtering and description updates do not replace source or reset property input.
The shared builder smoke covers filtered insertion, retained queries, exact native undo, and the preceding source edit.
`InsertSibling` resolves the selected node's variable-child parent and delegates to the existing indexed `InsertControl` proposal.
The inspector retains the current parent for before/after button availability and disables both actions when the palette has no selection.
The workspace captures the template and Grid cell before it starts the existing cancellable edit workflow.
`Designer.SourceTests/Program.SiblingInsertion.cs` covers positions, line endings, identities, Grid constraints, fixed-child containers, revisions, and cancellation.
`Designer.WorkspaceTests/Program.SiblingInsertion.cs` covers native buttons, selection, filtering, nested parents, refusals, undo/redo, and concurrent source typing in both styles.
The application selection smoke checks the new preview control's native text, arranged order, selection, and undo.

`VisualDocument.TryFindEmptyGridCell` reuses Grid placement validation and collects occupied rectangles.
It searches column intervals in row order and skips full rows to the next occupied row-span ending.
This avoids a scan of every cell in large explicit track arrays.
The query rejects unknown tracks, expression placement, overlaps, invalid bounds, stale revisions, and full Grids.
It does not compile or execute authored code.
`DesignerWorkspace.FindEmptyGridCell` fills the native coordinate fields after source and busy-state checks.
The selected Grid takes precedence over its parent. Otherwise, the helper uses the immediate Grid parent.
`Designer.SourceTests/Program.EmptyGridCell.cs` covers occupancy combinations, spans, large arrays, refusals, cancellation, and compiled insertion.
`Designer.WorkspaceTests/Program.GridCellSearch.cs` covers native fields, target selection, refusals, button visibility, compiled insertion, and undo in both styles.
The application selection smoke covers the resulting native preview control and separate insertion undo operations.

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
`DesignerPreviewViewport.cs` wraps that host without replacing its content or changing source.
`DesignerPreviewViewportLayout.xui` supplies the preset selector, custom dimensions, and actual-size feedback.
The controller samples arranged dimensions at 250-ms intervals and stops that work on disposal or window closure.
Disposal runs on the creating UI thread and leaves the borrowed preview content attached and alive.
The application disposes the viewport controller before the preview host and window.
Native scrolling is vertical-only. Excessive requested widths fit the available pane and produce an explicit notice.
`Designer.ViewportTests` covers sizing, invalid input, retained control identity and state, and a 438-DIP pane.
The application selection smoke also covers fixed-size picking and unchanged preview versions across size changes.

`PreviewHost` constructs a candidate within `ContentUpdate` before replacing the previous content.
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
`PreviewPick.cs` defines versioned source identities for native pointer selection.
`PreviewHost` registers temporary inspection targets through the same scoped candidate.
Its pick handler rejects retired candidates and superseded source versions before it notifies the shell.
Inspection observers use window context so replacement posts do not inherit the retiring preview scope.
The [pointer-selection contract](../specs/designer.md#versioned-pointer-selection) leaves keyboard input and accessibility actions native.

`Designer.Tests` covers compilation, diagnostics, cancellation, input limits, and the generated wrapper.
`DesignerTemplates.cs` exposes the embedded example catalog to the workspace.
`Designer.TemplateTests` compiles every catalog entry without a native DLL.
`DesignerDocumentStore.cs` supplies the shell's file and recovery model.
`DesignerFileActions.cs` connects the file store to native choosers and explicit dirty-document confirmation.
It retains the file-store conflict policy and checks source revisions after native modal calls.
It compares raw file hashes before replacement and writes each destination through a temporary file.
Recovery metadata connects a source hash to the original path and file hash.
A partial snapshot reports an error instead of restoring stale file identity.
`Designer.DocumentTests` covers this model without a native DLL.
`DesignerRecoveryDialog.cs` supplies the shell's native recovery dialog.
`DesignerRecoveryLayout.xui` defines its content.
The dialog shows bounded source previews, requires deletion approval, and prevents recovery over dirty source.
`Designer.RecoveryTests` exercises its native controls against isolated draft files.
`DesignerDiscardDialog.cs` supplies explicit discard or cancel approval through `DesignerDiscardLayout.xui`.
It defers approved actions until the native dialog closes, then checks the exact source and revision again.
`Designer.DiscardTests` covers native undo preservation, concurrent requests, and stale approval.
`DesignerFileSmoke.cs` runs `--file-smoke` against the complete application with an isolated recovery directory.
`DesignerFileDialogProbe.cs` drives only the smoke process's native chooser through its owner and UI thread.
`--file-close-smoke` isolates owner closure and checks native HWND teardown after `Window.Run`.
This opt-in diagnostic currently fails. It is separate from ordinary file-workflow acceptance.
The application keeps file feedback inside Output, separate from compiler diagnostics.
File operations also update the Output status row.
`DesignerDiagnostics.cs` maps compiler messages to revision-scoped source selections for the next workspace.
It uses the reported compiler coordinates and preserves exact native paragraph offsets.
Generated-file locations, invalid coordinates, and stale source cannot produce a source selection.
`Designer.DiagnosticsTests` covers this model and real compiler output without a native DLL.
`DesignerDiagnosticNavigator.cs` adds native diagnostic navigation through `DesignerDiagnosticsLayout.xui`.

`DesignerSourceSearch.cs` supplies a collapsible native Find panel around the existing source editor.
`DesignerSourceSearchLayout.xui` keeps the query, case toggle, match status, and source document in one declarative component.
The closed panel has a zero-height Grid row and hidden controls.
Ctrl+F opens the panel, while Escape or its close button hides it without replacing the editor.
F3 and Shift+F3 also open the panel and navigate the retained query.
Every navigation reads current native text and selection rather than cached offsets.
`Designer.SearchTests` covers literal ordinal matching, case selection, native CR and UTF-16 positions, query keys, and untouched source undo.
It also covers panel visibility, retained query state, focus restoration, and reclaimed editor space.
The search controller also owns whole-word matching and the collapsible replacement row.
Ctrl+H focuses the native replacement field.
Single replacement requires a current exact match selection.
Bulk replacement computes one bounded candidate from one source snapshot before it calls `ReplaceRange`.
The controller preserves text outside the first and last match and treats replacement text literally.
Identical replacements bypass native editing, and length or read-only errors reach both Find status and application Output.
The native search fixture covers Unicode boundaries, replacement expansion and deletion, no-op undo preservation, and length-limit refusals.
The selection smoke checks replacement through the application, parser, compiler, preview, and native undo.
`Designer.LayoutTests` covers the toolbar band, panel styles, Output header position, focus, and native editor state across panel changes.

The application now composes the Find toolbar and versioned pointer picking with its native source editor.
`DesignerApplication.Picking.cs` puts routine mode and outline feedback in tooltips.
Picking and outline failures remain visible in the Output status row.
`DesignerSelectionSmoke.cs` sends mouse messages only to preview peers on the owning UI thread.
It checks exact source selection, hierarchy identity, stale versions, native undo, and restoration of authored actions.
The fixture reads changed captions through retained native control state, not stale HWND window text.
The full application passed the selection fixture on September 17, 2026, after the requested 3 AM iteration minimum.
The combined run also passed 32 file assertions, 16 builder assertions, 26 property assertions, and the original preview-recovery smoke.
It checks the current source revision and displayed diagnostic text before changing either native selection.
`Designer.NavigationTests` covers its buttons, F8 routing, Unicode selections, and invalidation.
`DesignerWorkspace.cs` connects the hierarchy and inspector to native source transactions.
Its grouping actions call `VisualDocument.WrapNode` and `UnwrapNode` through the same cancellation and revision checks as property edits.
`DesignerInspectorLayout.xui` defines the wrap and unwrap controls.
`Designer.GroupingTests` covers the native buttons, hierarchy-only shortcuts, source selection, and undo.

`DesignerLiteralCodec.cs` uses Roslyn string tokens to convert between literal source and native property text.
Its dimension helpers accept unnamed pairs of finite, non-negative numeric literals within the single-precision range.
They reject expressions, comments, directives, and named tuples instead of rewriting them.
`EncodeDimensions` replaces only the numeric expressions and retains tuple trivia and exact no-op source.
`Designer.SourceTests/Program.DimensionCodec.cs` covers this contract and real compiler integration.
`TryDecodeBoolean` accepts only true/false literals with whitespace trivia.
`EncodeBoolean` preserves exact no-ops and surrounding whitespace, and rejects output beyond the source limit.
`Designer.SourceTests/Program.BooleanCodec.cs` covers rejected syntax, length limits, and compiled argument edits.
`TryDecodeInsets` accepts scalar or four-sided style literals from 0 through 32768 DIPs, matching the style compiler.
It rejects signed literals, expressions, named tuples, and comments rather than changing their meaning.
`EncodeInsets` preserves no-ops, tuple whitespace, and scalar syntax when all edge spellings remain equal.
`Designer.SourceTests/Program.InsetsCodec.cs` covers conversions, field errors, cultures, source limits, and compiled edits across control kinds.

`DesignerInspector.cs` exposes string conversion through an opt-in text-mode toggle.
Its dimension-mode toggle replaces the raw value area with two native text fields.
Its boolean-mode toggle replaces that area with a native value toggle.
Its inset-mode toggle provides four native fields in two rows for padding and border thickness.
Stack padding stays outside inset mode because it uses a uniform structural setter.
The controller retains the boolean draft from native change events because the managed checked property is write-only.
Mode changes do not commit source. Argument and source changes clear each structured mode.
`FocusValue` selects the active field rather than the hidden raw editor.
`DesignerWorkspace.RevertPropertyDraft` checks the current source snapshot and busy state before discarding a property draft.
`DesignerInspector.RevertDraft` restores the authored value through `ShowArgument`, then restores the previous structured mode and editor focus.
It retains the current property and filters, and never calls a source-edit proposal.
`CanRevertPropertyDraft` also gates the command-palette entry, which checks availability again after dismissal.
`Designer.TextModeTests/Program.RevertDraft.cs` covers invalid drafts, every editor mode, unset values, expressions, current revisions, and source undo in both styles.
The application selection smoke checks command dispatch, disabled expression actions, and unchanged native preview dimensions.
`Designer.TextModeTests/Program.Insets.cs` covers inset drafts, native geometry, no-ops, errors, reset, stale source, and undo in both styles.
The application selection smoke checks property focus and the actual label height after a padding edit and source undo.
`Designer.TextModeTests/Program.Dimensions.cs` covers drafts, validation, mode changes, native source undo, resets, and stale-source rejection.
The application selection smoke checks actual preview bounds after size edits and undo.
`Designer.TextModeTests/Program.Booleans.cs` covers boolean drafts, no-ops, focus, undo/redo, reset, and stale-source rejection in both visual styles.
The selection smoke checks the actual native enabled state after a boolean edit and its undo.
`DesignerWorkspace.ApplyProperty` rejects no-op values before it starts a source transaction.
`Designer.TextModeTests` covers the complete native inspector, source callbacks, undo, raw and verbatim spelling, draft conversion, and size-limit errors.
The September 17, 2026 ARM64 Release run passed 26 text-mode and reset assertions, 16 workspace assertions, and 19 grouping assertions.
`DesignerWorkspace.ResetProperty` permits named literal resets through `VisualDocument.RemoveArgument`.
Positional operands and expression-backed arguments remain protected in the inspector.
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

### Pointer inspection evidence

The Stage1 inspection tranche uses the embedded core, source metadata, and owned-dialog baseline.
`src\content_inspection.hpp` validates weak registered targets and retained ancestry.
`src\application_content_inspection.inc` owns native hit resolution, gesture gating, and bounded deferred delivery.
`tests\content_inspection_window_tests.cpp` covers real HWND routes, retained gaps and clips, editor state, refusal paths, and 100 replacements.
The native inspection, ContentHost, core, and dialog-window CTest targets passed on ARM64 Release.

The integrated managed preview suite passed 1,779 assertions.
It covers target validation, 100 registered replacements, native hit identities, versioned pointer events, supersession, and scope retirement.
It also checks collectible assembly retirement while value snapshots remain retained.
Compiler and source suites passed 89 and 780 assertions.
The designer smoke, managed dialog suite, and documentation checks passed.
There is no highlight implementation in this tranche.

### Non-occluding outline evidence

On September 17, 2026, the ARM64 native outline fixture passed for Classic, WinUI, and clipped layouts.
The fixture checks actual outline and clear pixels without changing production input or HWND regions.
It covers EDIT and RichEdit occlusion refusals, exact region equality, native hit targets, styles, bounds, and editor state.
Each visual style completes 100 highlight, clear, and replacement cycles with exact HWND, USER, and GDI baselines.
The fixture also covers original-perimeter clipping, scrolling, resizing, target recreation, and refusal-state retirement.
Geometry checks cover 96, 144, and 192 DPI without fabricated clipping edges.

An initial physical-hit check failed because an unrelated foreground window covered the test editor.
The fixture now selects an unobstructed monitor and identifies editors by exact fixture text.
The strict physical EDIT-hit assertion remains unchanged.
Other fixture corrections keep capture checks paint-only and initialize the capture apartment for the fixture lifetime.
No production highlight change was necessary during this investigation.

The integrated managed preview suite passed 2,091 assertions.
Its checks include 100 registered outline cycles, native-editor refusal, source-version guards, bounded supersession cleanup, and assembly retirement.
The compiler and source suites passed 89 and 780 assertions.
Documentation checks also passed.

Active-highlight physical DPI transitions, real file-dialog and tooltip/popup interaction, and external UIA clients remain outside this evidence.
Modal checks use owner disable. UIA checks use the native action endpoint.
The test procedure is in [CONTRIBUTING](../../CONTRIBUTING.md#non-occluding-selection-outlines).

The shell connects `DesignerWorkspace.SelectionChanged` to a coalesced `Window.Post` outline request.
This delivery occurs outside native input callbacks and reads the current source, version, and selection.
The shell also requests an outline after successful preview replacement.
Separate outline feedback preserves compiler diagnostics and pointer-mode feedback.

The final parent run on September 17, 2026, rebuilt the ARM64 Release runtime and passed the native outline fixture in 36.06 seconds.
The same run passed 2,091 preview assertions, 15 application selection assertions, 10 layout assertions, 16 builder assertions, and 32 file assertions.
The original application smoke also passed its preview failure and recovery cases.
These results do not resolve the documented programmatic owner-close failure during a native file chooser.

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

### Declarative value controls

The parser accepts `RangeInput` and `Progress` as native leaves.
The emitter applies their optional `NumericRange` during construction, before reactive property refresh.
The range has no reactive binding and its authored expression contributes to the structural reload signature.
`currentValue` avoids the existing positional `value` key.
`progressState` selects the native Progress state.
RangeInput uses the existing `OnChange(Action<double>)` adapter for committed changes.
The shared parser supplies source spans, and the existing style catalog supplies control and part schemas.

`GeneratorTests/Program.ValueControls.cs` covers initialization order, default preservation, reactive values, handler registration, diagnostics, styles, and shape changes.
`Designer.SourceTests/Program.ValueControls.cs` covers source edits and native-control hierarchy metadata.
The existing diagnostic and element-mapping fixtures also include these controls.
`ValueControls.Tests/Values.xui` is the actual generated native fixture.
Its executable checks native errors and callbacks, then runs a bounded native window.
The [language contract](../specs/xui-language.md#range-input-and-progress) defines the limits.
The [contributor guide](../../CONTRIBUTING.md#xui-designer) contains test commands.
On September 17, 2026, this tranche passed 41,861 generator assertions, 2,949 source assertions, and 78 designer compiler assertions.
The generated native fixture passed 52 assertions with this worktree's ARM64 Release DLL.
That DLL came from a local `xui` target build, not another session's output.
The native fixture covers a mounted window and callbacks, not screenshot-based appearance checks.

### Native chooser owner-close investigation

The ordinary ARM64 Release file-workflow smoke passed 32 assertions on 2026-09-17.
It covered actual native chooser results, Unicode paths, cancellation, file shortcuts, dirty-document approval, failed destination reads, and source undo and redo.
Native chooser errors and stale save results preserved the current document.
The original application smoke also passed.
The focused suites passed 16 builder assertions, 11 discard assertions, nine layout assertions, 49 document assertions, and 17 recovery assertions.

On 2026-09-17, the complete designer exceeded a 30-second cancellation deadline after programmatic owner closure during a native file chooser.
The native `Close` request reached the dialog, and its COM `Close` returned success.
The modal `Show` call did not return before the diagnostic sent its fallback Cancel command.
The fallback ends the failed fixture and does not establish successful programmatic cancellation.

The same failure occurred with a direct native API call, retired preview content, and a label-only root before `Run`.
Minimal native, C ABI, and managed fixtures passed separately, including a managed WinExe fixture.
These results do not identify the cause of the full-process difference.
No native cancellation correction accompanies the file-workflow integration.
The strict `--file-close-smoke` diagnostic remains available.
A UI-thread stack before the fallback is the next distinct evidence step.

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

## Microsoft Edit integration

`integrations/edit-lsh/xui.lsh` supplies the standalone LSH definition for Microsoft Edit.
Its helpers use the `xui_` prefix because LSH functions share one global namespace.
The shared token lexer handles C# expressions, strings, comments, and nested delimiters.
The outer lexer handles XUI declarations and control names.
Loops that call the token lexer use `continue` to bypass LSH's automatic character skipping.
The optimizer cannot see regexes inside helper calls.

`integrations/edit-lsh/tests/highlighting.rs` compiles the grammar with the upstream LSH compiler and checks runtime highlight spans.
It also compiles the grammar beside all upstream definitions to detect symbol conflicts.
The Cargo manifest and lockfile pin the upstream compiler and runtime.
The [contributor procedure](../../CONTRIBUTING.md#microsoft-edit-lsh-grammar) contains test and integration commands.
The [public guide](../specs/xui-language.md#microsoft-edit-syntax-support) describes coverage and limits.

### Native LSH integration

`src/syntax_highlighting.cpp` loads the `Lsh 0.3.0` native API beside the containing XUI module.
It compiles one shared engine from trusted embedded definitions.
The engine handles complete CR-normalized documents and converts UTF-8 ranges to UTF-16.
`integrations/lsh/lsh.cmake` selects the package architecture and embeds the grammar sources.
`bindings/dotnet/Xui/SyntaxHighlighting.cs` exposes the additive C ABI without a second managed LSH dependency.
The gallery, Designer, and FileExplorer enable the same native implementation.

`integrations/edit-lsh/upstream` contains C, C++, C#, and Rust definitions from Microsoft Edit commit `826b4c097b6f14ba0a846dc56f2f0223a3aaf73a`.
Their [MIT license](../../integrations/edit-lsh/upstream/LICENSE) is retained.
The Rust character regex uses a negated ASCII class because the packaged compiler rejects hexadecimal regex escapes.
The XUI grammar uses the same compatible byte-class approach.
Multiline loops consume input explicitly to avoid the packaged engine's automatic-advance behavior after resumption.
Token branches emit colors before continuing to avoid a self-jump assertion in newer LSH compiler builds.
Both the package-backed native tests and the standalone Rust tests cover this compatibility boundary.
The packaged regex compiler can select a shorter shared-prefix alternative.
The control rule places `TextInput` before `Text`, and separate rules handle hexadecimal, binary, and decimal literals.
Numeric classes spell out both letter cases instead of relying on case-insensitive character classes.
Interpolation rules emit string segments before consuming an opening brace, then emit that brace as code.
The native palette maps named argument labels to the accent color, distinct from plain identifier values.

`tests/syntax_highlighting_tests.cpp` exercises custom grammars and UTF-16 conversion through the packaged engine.
The document syntax core and native-window tests cover presentation and editing invariants.
Read-only RichEdit peers use `EM_SETCHARFORMAT` because TOM foreground setters reject those peers.
The read-only style remains set throughout formatting.

On 2026-09-17, ARM64 Release checks passed for syntax, native editing, managed editing, Designer, and FileExplorer source previews.
The gallery search and compact WinUI interaction checks also passed.
The packaged engine processed all 28 repository XUI sources and every gallery excerpt.
The full Classic gallery sweep hit focus assertions with LSH enabled and disabled.
Its complete interaction sweep remains unverified.
`bindings/dotnet/Syntax.Tests` covers managed binding calls, language changes, and native undo and redo.
The [contributor procedure](../../CONTRIBUTING.md#lsh-highlighting-in-xui-applications) contains the build and test commands.

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
