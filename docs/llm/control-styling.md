# Control styling implementation and evidence

Latest integration evidence: 2026-09-16.
The current delivery is the control-family expansion after Astra completed the Sonnet-5 pilot takeover.
Earlier foundation evidence dates from 2026-09-15 and uses base commit `70a009f4cac55bc74250ae7500a9abbf0b20a1f9`.
This note separates current integration results, accepted performance costs, and historical investigations.

The earlier foundation investigation measured an additional styled raster cost.
Its USER-object failure also reproduced against pristine native libraries.
The managed Shell-menu failure did not reproduce in six bounded runs.
The delivery decision below records the later acceptance of the measured Button cost.

The [public contract](../specs/control-styling.md) and [inventory](../specs/control-styling-inventory.md) describe implemented coverage and explicit gaps.
Control templates and item templates remain future work.

## Delivery decision

On 2026-09-15, the application author accepted the measured additional CPU cost of approximately 0.26 ms per frame.
This decision permits broader control styling without another investigation of that foundation cost.
The earlier blocked dispositions in this report describe the investigation before this decision.
Brush changes remain a possible explanation, not an established cause.
Default-path performance, bounded storage, native input, and accessibility remain requirements for subsequent changes.

## Consumer integration

### Minesweeper alignment

The legacy Button path in `src\application.cpp` uses a DirectWrite layout with centered paragraph alignment.
Its content rectangle must not add another vertical centering offset.
The generic Button path already centers text through its format.
Minesweeper uses uniform covered-cell borders and a shared coordinate style for centered row and column labels.
The native alignment regression compares actual glyph positions in 36-DIP cells against a centered Label.
It covers covered and cleared cells, enabled and disabled states, both themes, and both visual styles.

### Explorer header icons

`bindings\dotnet\FileExplorer\ExplorerStyles.cs` shares one Button style across the title-bar navigation button, both New tab buttons, and address-toolbar icons.
The idle fill matches the WinUI window background because style colors remain opaque RGB24.
The address button keeps its standard face.
`xui_feature_child` exposes the retained TabStrip New tab Button at index 0.
The C++ accessor and both bindings use that same control without replacement.
The ABI regression covers handle identity, separate pane buttons, invalid indices, and Button styling.
The explorer smoke covers the shared style and existing New tab activation.

### Compact explorer navigation

`ExplorerStyles.NavigationItems` supplies the demo's row, text, indentation, padding, and icon metrics.
`NavigationSidebar` applies that shared style to the main, header, and footer lists.
The NavigationList schema accepts `icon.size`.
`Drawing::styled_collection_row` uses it for image and vector slots, text placement, and compact centering.
Unset sizes retain their previous dimensions and placement.
The generated native catalog supplies the same property mask to C#, Rust, and the declarative compiler.
The collection style regression covers row geometry, inherited text size, style removal, and rasterized icon sizes.

### Final VSIX catalog integration

The extension update uses the verified compiler and native catalog from commit `9bc9c4f90034b9acef48bf53e7e0907b6a672b3c`.
It covers 45 Element-applicable style targets and 221 part schemas.
The native catalog also contains Tooltip and its two Window-only parts.
The extension retains the existing application constructors and adds typography, part, and existing-content snippets.
Its vocabulary is lexical, not a replacement for compiler checks of target, part, state, property, or value combinations.

The parent integrated the extension-only increment from `60219a6b9760516aea6d234ca931798b1dcd5a83`.
All 37 tokenizer and configuration tests passed.
The rebuilt VSIX passed the strict eight-file content check.
The package excludes tests, scripts, cached grammars, and dependencies.
Catalog regeneration from the verified source commit produced no source changes.
The package is `integrations\vscode-xui\dist\xui-0.1.0.vsix`.
Its SHA-256 is `F1656AF3E7C7A4FD3809E3B3C3B33D40B1071446D0F8BA17BE8178250F7ED35E`.
The package was not installed or published.

After the native expansion commit, the parent also reran the existing performance-invariant suite and all 11 real MSBuild assertions.
Both passed without benchmark mode or a new frame-time claim.
The earlier Minesweeper variability and manual-input coverage limits below remain explicit.

### Retained facade binding follow-up

`xui_feature_child` now exposes the retained dialog, command surface, location picker, view picker, and navigation pane children.
Existing child indices and Popup-backed facade root handles remain unchanged.
CommandSurface menu uses a retained-only Element kind instead of an incompatible ItemsView kind.
C# exposes `RetainedElement` for that menu, and Rust returns `Element`.
The shared ABI keeps native Button actions separate from foreign activation notifications.
TextInput wrappers capture original change and submit callbacks once.
This preserves composition behavior across repeated subscriptions.
NumericInput, RadioGroup, RangeInput, and Popup use the same callback-preservation contract.
Their native getters return the original callback fields without additional storage.
The retained-choice accessors cover ComboBox overlays, NumericInput editors/buttons, InlineStatus buttons, and ColorPicker channels/swatches.
Only the noneditable ComboBox editor uses a successful zero handle.
The managed and Rust optional helpers preserve that absence without creating an invalid Element wrapper.

New ABI regression source covers child target assignment, handle identity, local clearing, facade target rejection, and lifetime/thread guards.
Managed and Rust regression sources cover retained accessors and local-value preservation.
The later coordinator results appear in the retained-facade and final-navigation follow-ups below.

### All-control native expansion

Uniform-metric policy uses the fifth `StylePartSchema` field, `state_allowed`.
Native creation checks this mask before publication, and the ABI exports its intersection with `allowed`.
`bindings\generate_control_styles.py` carries the mask into the managed, Rust, and compiler catalogs.
Collection tile-width regressions cover forbidden state rules, base/local column geometry, and local clearing.
Grid and choices regressions distinguish owner-wide layout metrics from per-item content geometry.
Complete generated authoring coverage requires a catalog from the matching native schema.

Numeric and editable ComboBox geometry uses additive root, field, and retained-editor insets.
In `src\foundation.cpp`, `field_bounds()` applies root insets, and the editor/action geometry applies field insets through `choice_style_content`.
The native `TextInput` adapter in `src\application.cpp` separately applies the child text-content insets.
Parent field colors supply read-only defaults, not replacement child geometry or attachments.
The [public contract](../specs/control-styling.md#numeric-and-editable-combobox-insets) describes the application-author boundary.

Stack composition defaults use `set_default_padding` and `set_default_spacing` in `src\core.cpp`.
These methods leave structural-value flags unset and cannot overwrite explicit structural values.
`tests\style_layouts_tests.cpp` covers style overrides, explicit zero, later default calls, clearing, and allocation-free ordinary setters.

This section records the ARM64 Release expansion after the committed Toggle pilot, `0735a5047018d44fc220f7c6e6936687fc73cada`.
The native implementation covers basic controls, native fields, choices, layouts, collections, grids, navigation, and native hosts.
Portable catalog, compiler, binding, and extension integration remain separate acceptance steps.
Native coverage alone does not establish complete authoring support.
The expansion does not implement replaceable control templates or item templates.

All seventeen new native model, raster, and window suites passed in the stable-source combined batch.
An earlier broader run passed 42 of 43 suites against unchanged native source, headers, and tests.
The only failure was an existing suggestion-popup fixture.
That fixture compared the popup against the smaller, vertically centered native EDIT rectangle instead of the full model field.
The corrected fixture uses the full field bottom in screen coordinates and preserves the existing inset requirements.
Its subsequent run passed without a production geometry change.
The parent reports that all 44 native regression targets are now resolved.

The broader run also covered core controls, legacy styles, the C ABI, native input, collections, navigation, host ownership, and existing desktop regressions.
The existing performance-invariant suite passed without benchmark mode.
It covers million-row semantics, allocation-free interactions, cancellation, and off-thread retirement.
These results are invariant checks, not new frame-time measurements.
The accepted Button timing evidence remains separate.

Final focused runs also passed navigation model, navigation raster, and native-field window suites.
They include paragraph alignment limits, retained command callbacks, and native clear-glyph coverage.
The clear-glyph fixture accepts authored antialias blends but rejects background and unrelated colors.
It requires more than three full-pixel-equivalent units of foreground coverage.
The suggestion fixture and antialias fixture changes preserve their behavioral requirements.

The shared allocation report records an unstyled Element baseline of 96 bytes and one allocation.
An unstyled Control adds no style allocation beyond that baseline.
`sizeof(Element)` is 88 bytes, and `sizeof(Control)` remains 416 bytes, equal to the committed Toggle pilot.
`sizeof(PartStyleValues)` is 240 bytes.
Warmed inherited-text state loops allocate no memory.
These figures describe this ARM64 Release build, not other architectures.

The parent rebuilt Minesweeper and copied the exact current native DLL into its ARM64 output.
Matching file hashes established the native library used by the managed run.
All 133,822 model and native-style assertions passed.
A complete desktop smoke run passed 510 assertions, including gameplay, unchanged borderless-cell geometry, hot reload, and NativeAOT.
The successful local artifact directory is `build\minesweeper-check\0066dd3a823a4367afee99eab76493b2`.

Two earlier smoke runs failed during initial covered-cell checks at different late cells.
One returned `"1"` instead of `"?"`.
The fixture now reports the actual name, summary, and help text on failure.
No gameplay expectation or production behavior changed to obtain the passing run.
Both earlier changed-cell failures remain unclassified.
The later successful run does not establish their cause or prove that an intermittent failure is fixed.

No physical keyboard, screen-reader speech, or operating-system high-contrast transition forms part of this expansion evidence.
Dedicated native-region checks across DPI changes and high-contrast clearing are not separately established.
Existing host checks cover asymmetric geometry, nonzero origins, rounded pixels, and clip-cache reuse.
Navigation model and raster suites do not establish interactive ContentDialog validation or focus behavior.
Custom Shell HMENU presentation, automatic Breadcrumb-current selectors, palette-status selectors, and CommandBar section headings remain unsupported.
Tab icon, add, and drag parts are not advertised without corresponding models.
Composite facades retain actual Popup roots and expose their retained children.
They do not supply synthetic root states.
The parent ran native builds and desktop suites sequentially.
The broad integration log remains a session artifact named `all-control-native-integration.log`.

### Tooltip binding integration follow-up

The subsequent Tooltip ABI bridge uses real Window nodes and the existing generic style cache and property records.
C# and Rust expose definition application, local replacement and clearing, and effective-value reads.
The `.xui` compiler rejects Tooltip declarations with a Window API diagnostic.
It does not accept component syntax that cannot apply the style.

New ABI regressions cover hidden-state preservation, local precedence, weak identities, thread and window guards, borrowed font text, and output-buffer bounds.
Managed and Rust regression sources cover application, clearing, target rejection, and cross-window definition reuse.
The parent rebuilt the native bridge and ran the ABI features suite successfully.
The later managed and Rust runs cover these native-backed additions.
The earlier native acceptance results alone do not establish those portable additions.
The parent later exported the full native schema snapshot.

A later shared edit changed inherited root rules to use item state instead of the actual owner state.
The parent reproduced failures in three previously passing collection, choice, and grid suites.
The parent restored the approved owner-root behavior and the matching common assertions.
Non-root rules still use item state plus owner disabled context.
The subsequent native rebuild and eight affected suites passed.
Those suites cover common resolution, the ABI, and collection, choice, and grid models and windows.
The choice suite also covers per-channel invalid labels, valid neighbors, recovery, and allocation-free repeated resolution.
The picker root retains aggregate invalid state.

The schema pipeline now includes the native `xui_style_catalog` exporter and a read-only parity mode.
An exporter process avoids Python/DLL architecture mismatches.
Generated assertions cover every C/C++ target, part, property, and state identifier.
The portable generator suite also loads the real managed library under a native-load guard.
It compares catalog masks and limits, exercises definition validation, and does not load the native DLL.
The parent corrected the generated check header to include the public umbrella header, `xui/xui.h`.
The native exporter then built successfully.
The parent refreshed the catalog with that exporter and passed its read-only parity mode.
The snapshot contains 46 supported targets and 223 part schemas.
The first GeneratorTests run with this snapshot failed in `TestStylingDiagnostics`, which expected `XUI001` at `Program.cs:354`.
After the compiler-fixture update, the parent reran GeneratorTests successfully with 41,607 assertions.
The real MSBuild suite also passed its 11 assertions.
Earlier generator results describe the smaller catalog.

The later generator corrections passed 41,669 assertions and the 11 MSBuild assertions in independent parent runs.
A narrow review found both original defects resolved: mixed Button inheritance and omitted Stack setters.
The parent also added `bindings\dotnet\Tests\ExpandedStylingFixture.xui` with real native controls.
It covers legacy and generic Button ancestors, shared promoted definitions, native Label and Stack values, and disabled-state precedence.
Unchanged refreshes create no native handles.
A forced revision mismatch replaces definitions while preserving native control identity and input text.
This fixture increased the passing managed styling count to 10,488.

The generated Stack checks establish emitted setters and native style attachment, not measured native geometry.
Native layout suites separately cover explicit-zero precedence.
The revision fixture does not simulate a file edit through `dotnet watch`.

### Retained facade binding follow-up

The parent rebuilt the native DLL and C ABI fixtures after the facade accessor integration.
The C fixture passed.
The new feature fixture incorrectly expected a retained child lookup to return `XUI_CLOSED` immediately after `xui_window_close`.
The public lifecycle contract keeps controls valid until `xui_window_destroy`.
The parent corrected the fixture to require unchanged child identity after the close request and an invalid handle after destruction.
The feature suite then passed without a production lifecycle change.

The parent later rebuilt the choice-accessor additions and passed the C and feature ABI suites.
The managed tests used a hash-checked copy of the current native DLL.
The full managed run passed 48 wrapper, 220 feature, 10,465 styling, and 22 text-selection assertions.
That run includes the current retained-facade cases and existing callback and Shell-menu regressions.

The Rust run initially failed two new fixtures that expected child getters to fail after `drop(window)`.
Rust child wrappers share the window owner and keep its arena alive.
The parent corrected those fixtures to require valid child values while these owners remain.
All 23 Rust unit tests and two documentation tests then passed against the current native DLL.
The run includes retained choice callbacks, facade accessors, and existing last-owner destruction checks.
Cargo reported nonfatal incremental-cache access-denied notes, but the test binaries compiled and ran.

These results do not establish the remaining keyed-button, titlebar, or generated all-control reload integration.

### Final navigation binding integration

The final bridge adds keyed Breadcrumb and CommandBar Buttons, overflow Buttons, and the retained Window titlebar and NavigationView children.
It preserves the existing facade child indices and uses the actual native child targets.
NavigationPane also exposes its retained group Expander and progress indicator.
Button notifications remain separate from native command and caption actions.
Command snapshot changes do not overwrite foreign subscriptions.

The parent rebuilt the ARM64 Release DLL, C fixture, ABI features, and navigation model suite.
All three native suites passed.
The parent then copied the exact DLL into the managed test output and compared its hash.
The full managed run passed 48 wrapper, 287 feature, 10,488 styling, and 22 text-selection assertions.
The Rust run passed 24 unit tests and two documentation tests against the same native build.
Cargo reported a nonfatal incremental-cache access-denied note.
The final catalog parity check, 41,669 generator assertions, and 11 MSBuild assertions also passed.
Seven further native suites passed after the callback integration.
They cover existing controls, legacy styles, generic styles, expansion invariants, foundation controls, navigation raster output, and the styling window.

These runs include keyed identity, missing-key errors, native actions, subscription changes, and command snapshot transitions.
They also cover the generated mixed-style revision fixture described above.
They do not add physical keyboard, screen-reader, or frame-time evidence.

### Callback review corrections

A narrow review found that exposed, detached keyed Buttons could still invoke actions from obsolete command or breadcrumb snapshots.
The navigation adapter now disables retired Buttons, clears their callbacks, and guards copied callbacks and notifications.
Native, ABI, managed, and Rust regressions cover removal, disabled-key reintroduction, and breadcrumb version replacement.

The review also found an unconditional observer capture that exceeded MSVC's inline callback capacity.
Unexposed CommandBars now retain the smaller callback.
Observer callbacks are installed only when requested.
The navigation model suite checks allocations during actual unexposed CommandBar refresh and activation.

Callback preservation initially added six inline function objects to every ABI Node, beyond two preexisting function objects.
The parent replaced all eight with typed optional storage in `src\abi_callbacks.hpp`.
The storage occupies 16 bytes in this ARM64 build and allocates nothing for empty native callbacks.
Nonempty originals allocate only their callback category, not a record containing every possible callback.
The common expansion suite checks empty captures, exact allocation sizes, callback execution, and owner release.

The parent rebuilt the DLL and passed the common expansion, navigation model, and ABI features suites after these corrections.
The full managed run used a hash-checked DLL and passed 48 wrapper, 302 feature, 10,488 styling, and 22 text-selection assertions.
The Rust run passed all 24 unit tests and two documentation tests.
The final compiler handoff adds catalog-driven initialization and reload checks for all 45 Element-applicable targets.
The parent independently passed the resulting 41,813 generator assertions and portable catalog parity.
Those catalog-wide checks use the managed binding harness, not native layout measurement for every target.

The retirement correction initially suppressed Click notifications when an action disabled its still-current Button.
The parent reproduced that defect with the new native fixture against the previous DLL.
Notification delivery now checks current child identity rather than post-action enabled state.
Observer ownership becomes invalid before composition destruction.
The corrected DLL passed the same fixture, common expansion checks, and ABI features.
The cases include momentary and toggle actions, same-key disabling, removal with reintroduction, and destruction during dispatch.
The managed and Rust navigation bridge cases also passed against a hash-checked copy of the corrected DLL.
Native catalog parity remained unchanged.

### Retained-choice portable checks

The shared owner added picker-channel and swatch regressions after the native facade run.
The fixtures cover nested NumericInput step actions, original color-change callbacks, repeated subscriptions, handle identity, optional editors, and index rejection.
At that checkpoint, the new native ABI fixture and managed/Rust runtime tests remained unrun under the native-build hold.
The subsequent parent results appear in the retained facade follow-up.

The subsequent portable GeneratorTests run passed 41,622 assertions, including its native-load guard.
BuildTests passed 11 assertions.
Managed Tests compilation reported zero warnings and errors.
Rust workspace/test compilation passed, with nonfatal access-denied notes for its incremental cache.
The generated-catalog check and whitespace check passed.
These checks do not establish native runtime behavior for the new accessors.

### Generator review corrections

The binding review identified an invalid cast in mixed legacy/generic Button inheritance.
The compiler now constructs shared generic copies of required legacy ancestors while retaining the original `ButtonStyle` definitions and references.
Promotion merges repeated legacy state blocks property by property, without adding inheritance layers.
It does not require a new public binding API or native schema change.

The review also identified unconditional Stack padding and spacing setters for omitted `.xui` arguments.
The compiler now emits these setters only for authored arguments, including explicit zero.
Generated initialization and reload regressions cover both stack axes.
Mixed-inheritance regressions execute generated components and real managed definition initialization under the native-load guard.
The updated GeneratorTests run passed 41,669 assertions.
The native header, schema, and engine freeze remains unchanged.

### Native family freeze

The parent reports that all 17 new model, raster, and window suites passed in the stable-source combined batch.
The results cover full text inheritance, native font and alignment limits, default allocations, pixels, and UIA.
The parent corrected the final CommandBar callback fault and the native clear antialias fixture.
The shared owner preserved these corrections and froze native headers, schemas, the resolver, and binding catalogs.
Only concrete new failures permit further native changes during final integration.

After the generator review corrections, the final shared portable run passed 41,669 generator assertions and 11 MSBuild assertions.
Managed compilation reported zero warnings and errors, and Rust workspace/test compilation passed.
Current-DLL catalog parity and the generated-file check passed for the snapshot with 46 targets and 223 parts.
The parent ran native and desktop checks sequentially and reports all 44 native regression targets resolved.
The exact-DLL Minesweeper model/style run passed 133,822 assertions, and the latest desktop/hot-reload/NativeAOT smoke passed 510.
The two earlier unclassified changed-cell failures remain part of the evidence, not erased by the later pass.

### Toggle VSIX increment

The parent integrated extension-only commit `55de578` from `828f95f18af93def490167d22a9b1bcff507b474`.
The parent reported 30 passing tokenizer and configuration tests.
The extension owner reported a passing strict VSIX/source check across eight files.
The package was not installed.
These are parent and child reports, not new checks by the shared integration owner.
The extension session remains interactive until the complete implemented control catalog is ready.
The documentation session still waits for the entire all-control expansion and its integration commit.

### Toggle pilot integration status

Evidence date: 2026-09-15.
This section records the integrated Toggle pilot before the control-family expansion.
The native write and build holds ended before these native checks.
The delivery library is `build\styling-arm64\Release\xui.dll`.
This takeover verified ARM64 Release, not the earlier Debug or x64 reports.

These checks passed against the final implementation:

- Native build targets: `xui`, `xui_core_tests`, `xui_control_tests`, `xui_styling_tests`, `xui_control_styling_tests`, `xui_abi_c_test`, `xui_abi_features_tests`, and `xui_styling_window_tests`.
- Focused CTest run: all seven test targets passed, including the full Button and Toggle window regression.
- `dotnet run --project bindings\dotnet\GeneratorTests --configuration Release`: 480 assertions.
- `bindings\dotnet\GeneratorTests\BuildTests.ps1`: 11 real MSBuild assertions.
- `dotnet build bindings\dotnet\DeclarativeSample --configuration Release --no-restore`: real Toggle part syntax, without warnings or errors.
- `dotnet run --project bindings\dotnet\Tests --configuration Release --no-build -- --styling`: 10,454 assertions against the delivery DLL.
- `dotnet run --project bindings\dotnet\Minesweeper.Tests --configuration Release --no-build -- --styles`: 133,822 assertions against the delivery DLL.
- `cargo test --manifest-path bindings\rust\Cargo.toml -p xui --target aarch64-pc-windows-msvc styling::tests -- --test-threads=1`: 11 native Button and Toggle tests.
- Repeated `python bindings\generate_features.py` calls produced identical FFI declarations.
- `git diff --check`: no whitespace errors.

The managed runs used explicit delivery-DLL copies in their output directories.
The Rust run used the delivery directory for `XUI_LIB_DIR` and `PATH`.
The contributor guide contains the build and test procedures.

The native window suite covers authored light/dark pixels, indicator metrics, keyboard messages, real UIA activation, ancestor/modal disablement, and native editor retention.
It also covers stable native peer counts across style removal and replacement.
Software Direct2D checks cover all named parts, foreground inheritance, checked marks, Classic/WinUI palettes, and simulated high contrast.
The UIA check finds the semantic checkbox beneath its HWND host.
The host itself is a pane without a Toggle pattern.
No production accessibility provider changed.

The real generated `ToggleStylingFixture.xui` executes part styles through the native ABI.
An unchanged refresh creates no new native handles.
A forced style-revision mismatch rebuilds shared definitions without replacing controls or changing native input text.
This revision-cache check is not a new `dotnet watch` file-edit run.
No physical keyboard, screen-reader speech, native IME composition, or operating-system high-contrast transition forms part of this evidence.

The final Release allocation report is:

```text
Unstyled construction: baseline=96 bytes/1 allocations, Toggle=96 bytes/1 allocations
First set_style activation: 456 bytes across 5 allocations
Warmed loop (1000 iterations): 0 bytes, 0 allocations
Allocation failure sweep scenario=0 points=6 passed
Allocation failure sweep scenario=1 points=4 passed
Allocation failure sweep scenario=2 points=5 passed
Allocation failure sweep scenario=3 points=3 passed
sizeof_control=416 sizeof_toggle=488 sizeof_button=576
```

Unstyled Toggle construction adds no style allocation beyond the common Control/Label baseline.
The failure sweeps cover every allocation in fresh style attachment, fresh local attachment, style replacement, and local-part growth.
Each failure preserves the prior style, locals, effective values, and invalidation behavior.
MSVC iterator-debug builds exclude failure injection because container-proxy allocations can occur in `noexcept` paths.
Only the Release injection results above form part of this takeover evidence.
The pilot cannot reach the defensive eight-part and 256-compiled-bucket limits with its four-part, five-state schema.

The pilot is ready for parent review and control-family expansion.
The next families must reuse the shared attachment, sparse engine, ABI records, wrappers, and surface helper.
No family work, VSIX edits, commits, or template changes occurred in this takeover.

### Earlier Button consumer evidence

Commit `f77795d` integrates the first VSIX grammar update.
The parent reran all 24 tokenizer and configuration tests successfully.
The child also rebuilt the VSIX and checked its eight packaged source files.
The package was not installed.
Expanded target and part syntax still depends on the native and compiler pilot.

Commit `5909694` integrates the Minesweeper palette and presentation changes.
Cleared cells have no border, and numbered cells use distinct light and dark colors.
The palette also distinguishes covered, flagged, exploded, incorrect-flag, and completed-game cells.
The parent reran 133,822 model and native style assertions against its native library.
Unchanged refreshes create no new native style handles.

The Minesweeper child reported 510 desktop smoke assertions, including UIA bounds, gameplay, hot reload, and NativeAOT.
Its smoke-owned processes are closed.
No new screenshots, physical keyboard checks, or screen-reader checks form part of that evidence.
Static C# palette edits require a restart rather than named-style hot reload.

## Source map

The control-family expansion adds these shared paths:

- `src\style_*_schema.cpp`: supported properties, states, inheritance, and limits for each family.
- `bindings\native\style_catalog.cpp`, `bindings\generate_control_styles.py`, `bindings\control_style_catalog.json`: native export and reproducible portable catalogs.
- `src\abi_callbacks.hpp`, `src\c_api.cpp`, `src\c_api_features.inc`: optional callback preservation, retained child handles, and style application.
- `tests\control_style_expansion_tests.cpp`, `tests\style_*_tests.cpp`: shared-engine, family, geometry, and allocation regressions.

The Toggle pilot adds these shared-engine paths:

- `include\xui\control_styling.hpp`, `src\control_styling.cpp`: target schemas, sparse state buckets, attachment cache, and local inheritance.
- `include\xui\controls.hpp`, `src\controls.cpp`: transactional Control attachment, private backend context, and shared Toggle geometry.
- `src\application.cpp`, `src\drawing.cpp`, `src\drawing.hpp`: effective disabled context, shared surfaces, and actual Toggle painting.
- `include\xui\xui_features.h`, `src\c_api.cpp`: generic typed-property ABI and weak native identities.
- `bindings\dotnet\Xui\ControlStyling.cs`, `bindings\rust\xui\src\control_styling.rs`: generic definitions and common attachment APIs.
- `bindings\dotnet\Xui\Controls.cs`, `bindings\rust\xui\src\lib.rs`: wrapper integration.
- `bindings\dotnet\Xui\Native.Features.g.cs`, `bindings\rust\xui-sys\src\features.rs`: generated FFI declarations.
- `bindings\dotnet\Xui.Generator\Styling.cs`, `Parser.cs`, `XuiGenerator.cs`: target-aware grammar, diagnostics, generation, and revision cache.
- `bindings\dotnet\DeclarativeSample\Counter.xui`: application-author example.
- `bindings\dotnet\GeneratorTests\Fakes.cs`, `Program.cs`: compiler and refresh regressions.
- `bindings\dotnet\Tests\StylingTests.cs`, `ToggleStylingFixture.xui`, `ExpandedStylingFixture.xui`, `Tests.csproj`: real managed/native definitions, lifecycle, and generated refresh.
- `tests\control_styling_tests.cpp`: native model and allocation checks.
- `tests\abi_c_test.c`, `tests\abi_features_tests.cpp`: record layout, explicit errors, release, and weak identities.
- `tests\styling_window_tests.cpp`: Toggle pixel, metric, keyboard, UIA, disabled-context, and native-editor checks.
- `CMakeLists.txt`: generic engine and model-test registration.

The existing source map describes the compatible Button foundation:

- `include\xui\styling.hpp`, `src\styling.cpp`: immutable colors, resource scopes, sparse values, derivation, and state tables.
- `include\xui\controls.hpp`, `src\controls.cpp`: optional Button storage, local values, measurement, and state invalidation.
- `src\application.cpp`, `src\drawing.cpp`, `src\drawing.hpp`: actual authored faces, content bounds, disabled context, and high-contrast policy.
- `include\xui\xui_features.h`, `src\c_api.cpp`: additive descriptors, style handles, weak shared-definition cache, and explicit release.
- `bindings\dotnet\Xui\Styling.cs`, `bindings\rust\xui\src\styling.rs`: typed definitions, scope errors, sharing, and lifetime guards.
- `bindings\dotnet\Xui.Generator\Styling.cs`: declaration parser, reference checks, code generation, and reload cache.
- `integrations\vscode-xui`: TextMate grammar, snippets, and syntax tests.
- `tests\styling_tests.cpp`: native resource, precedence, validation, layout, and retention checks.
- `tests\styling_window_tests.cpp`: Direct2D pixel capture, native editor identity, resource checks, and benchmark mode.
- `tests\button_style_probe.cpp`: baseline-compatible default-path probe.

The FFI declarations come from `bindings\generate_features.py`.
No generated declaration changes depend on an absent generator-source change.
The unchanged generator reads the additive feature-header declarations.

## Baseline protocol

The native source remained unchanged through the initial build and baseline runs.
Only the parent-owned design document and index differed from the base commit.
The build used ARM64, Visual Studio 2022 Preview, MSVC 19.44.35228, Windows SDK 10.0.26100, and Release.
The output directory was `build\styling-arm64`.
The configuration enabled `BUILD_TESTING` and `XUI_DESKTOP_TESTS`.

The initial complete native build succeeded.
The baseline probe source initially lived under the build directory.
`tests\button_style_probe.cpp` preserves that source for reproduction.
Both probe executables used `/std:c++20 /O2 /EHsc /MT /LTCG`.
The unchanged executable remains at `build\styling-arm64\button-baseline.exe`.

Initial raw samples are under `build\styling-arm64\baseline`.
Later alternating comparisons are under `build\styling-arm64\comparison`.
The collection baseline executable was preserved before its rebuild.
The initial gallery JSON records its executable hash.
These local artifacts are build outputs, not repository files.

The Miller-columns and Shell-latency sessions received requests for a quiet measurement window.
No acknowledgement established exclusive desktop access.
Process checks delayed measurements while other compiler or linker processes were active.
The paired measurements found no active compiler, linker, or XUI test process at their boundaries.
These checks do not exclude unrelated user activity or short-lived processes between samples.
Large timing variation remains visible in the raw results.

A later final-build comparison attempt encountered another checkout's navigation-window tests.
The measurement guard stopped before it completed a new sample batch.
A bounded wait also lost its quiet window before the next sample.
The early numerical batches precede the final disabled-ancestor context integration.
The coordinated comparisons later in this note replace that incomplete final-build evidence.

The original foreground-sensitive gallery performance run failed because the owned window lost foreground.
That guard remains unchanged.
The gallery traversal mode also rejected the fixture because its hard-coded page count did not match.
The retained gallery run uses fixed-page memory mode without either traversal or a foreground latency guarantee.

## Default-path samples

The following paired samples alternate the unchanged and changed probe executables.
Each construction sample creates 100,000 Buttons.
Each state sample performs 1,000,000 state-and-measurement iterations.
Values are milliseconds, in run order.

```text
construction baseline: 17.9575 16.9993 16.6131 18.0176 19.0901 20.5960 22.2859
construction changed:  17.0693 53.3055 15.4440 16.3808 18.8496 19.9113 19.3930
state baseline:        72.9326 63.3351 62.7616 63.6765 67.8258 65.2248 69.6069
state changed:         62.2955 124.488 91.3846 63.3001 68.7038 59.4650 61.0638
```

Construction medians were 18.0176 and 18.8496 ms.
The absolute increase was 0.8320 ms per 100,000 constructions, or 4.62%.
State medians were 65.2248 and 63.3001 ms, a decrease of 1.9247 ms.
Both private-memory medians were 753,664 bytes.
Both executables reported 100,001 construction allocations and one state-loop allocation.
The stream-formatting allocation is included in both counters.
There was no additional allocation on the changed unstyled path.

The ARM64 object sizes were:

```text
type        baseline bytes  changed bytes  absolute change
Control     408             408            0
Button      560             568            8
ItemsView   800             800            0
```

The eight-byte Button increase is the optional sidecar pointer.
Unstyled Buttons do not allocate that sidecar.
There is no new per-row visual object in virtualized collections.

The next samples measure 20,000 virtual-list interactions.
Values are milliseconds.

```text
60 rows baseline:     4.5090 2.6272 3.6103 4.6161 2.6922
60 rows changed:      3.6457 3.2809 4.7897 2.4928 2.5814
100k rows baseline:   33.9397 31.0663 24.8665 19.1043 23.2470
100k rows changed:    32.1058 24.9868 29.7729 25.3297 18.4923
1m rows baseline:     56.8946 40.0259 44.8964 41.4088 49.1927
1m rows changed:      69.2295 42.4862 40.3827 50.0703 44.1799
```

The 100k-row medians were 24.8665 and 25.3297 ms, an increase of 0.4632 ms (1.86%).
The 60-row and million-row medians decreased by 0.3294 and 0.7165 ms.
All runs passed the existing zero-allocation interaction and bounded-index checks.
These invariants supplement the numerical comparison rather than replace it.

## Default gallery resources

The command used `measure-window-memory.ps1` with `--page buttons`, three runs, `800x780`, and `-ResourceMetrics`.
The fixed-page runs reported these private-byte samples:

```text
baseline: 102961152 103014400 102940672
changed:  103010304 103301120 103075840
```

The medians were 102,961,152 and 103,075,840 bytes.
The absolute increase was 114,688 bytes, or approximately 0.11%.
Every run retained one target, 38 peers, 274 process handles, and zero idle paints.
Native staging-buffer bytes stayed at 188,864.
Native bitmap bytes stayed at 198,744.
Baseline USER/GDI counts varied between 68/37 and 69/39.
Changed runs reported 69/39.

Uncontrolled paint-latency medians were 13.8862 and 14.5650 ms, an increase of 0.6788 ms (4.89%).
Individual samples varied from 7.7319 through 27.2304 ms.
These values do not establish a latency guarantee because foreground ownership was not stable.

## Styled workload and lifetime

Three separate runs measured 200 synchronous state-and-paint iterations per variant.
Both variants use the changed build.
They are not pristine-versus-changed samples.

```text
unstyled CPU microseconds/iteration: 2578.12 1640.62 2187.50
styled CPU microseconds/iteration:   2343.75 1953.12 1796.88
unstyled p50 latency microseconds:  16803.9 16735.9 16826.1
styled p50 latency microseconds:    17105.9 16421.1 16418.2
```

The CPU medians were 2187.50 and 1953.12 microseconds.
The latency medians were 16803.9 and 16421.1 microseconds.
The variation prevents a claim that styles improve performance.
Both variants retained one target, 303 handles, 23 GDI objects, and 21 USER objects in these runs.
Each run completed 432 style/theme cycles with three stable child HWNDs and zero idle paints.
Per-process private-byte samples remain in the raw benchmark logs.

Native model tests also apply and clear a shared definition 10,000 times.
The definition returns to one owning reference after the loop.
Binding tests cover 65,537 transient cache cycles while another definition remains live.
The weak cache removes expired entries and releases its storage when empty.

An early wrapper implementation leaked 1,024 native handles after 512 managed apply/clear cycles and garbage collection.
The final implementation retains zero handles after that workload.
It also shares one immutable native definition among 256 Buttons through 32 repeated updates.
Those updates allocate zero new handles.
Same-style assignments preserve native state caches without invalidation.

## Coordinated final comparisons

The parent later confirmed that Miller-columns and Shell-menu work had finished.
Both workstreams had no remaining builds, desktop tests, or measurements.
No other session required a hold.
The comparison batches began only after this worktree's builds and tests finished.
Each completed batch explicitly released its measurement interval.

The original gallery was rebuilt from a `git archive` of the base commit inside this worktree.
It used the same ARM64 Release compiler, SDK, and configuration as the candidate.
No original or candidate source changed during a measurement batch.
The measurement script checks for competing builds and tests before and after each phase.
The gallery measurements require foreground ownership throughout the measured paints and idle interval.

The first coordinated batch completed at 20:00 UTC.
Its raw results are under `build\styling-arm64\reserved-comparison`.
It detected no interference, but several medians exceeded the working 5% guardrail.
Construction increased 5.26%, state/measurement increased 6.65%, and the 100k-row interaction increased 7.33%.
Gallery median latency increased approximately 5.84%, while its CPU time decreased.
This batch was not accepted as a performance pass.

Inspection found avoidable calls into the styled path for unstyled Buttons.
The final refinement restores the small inline default measurement path.
State invalidation also bypasses styled resolution when the optional sidecar is absent.
The refinement changes no properties, state precedence, or ABI layouts.
Four focused native suites passed after its rebuild.

The second coordinated batch completed at 20:08 UTC.
Its raw results are under `build\styling-arm64\counterbalanced-comparison`.
It alternated which executable ran first in each pair.
It also increased the gallery sample count to six pairs, with 200 measured paints per process.
The batch reported `completed: true` and an empty interference list.

Final Button samples are milliseconds:

```text
construction baseline: 12.9747 12.4338 11.6029 11.6843 11.6161 12.1042 15.0455
construction final:    12.1734 11.5859 11.6797 18.7417 11.7655 11.6320 12.9197
state baseline:        42.9450 40.3947 42.2067 41.5721 41.4189 40.0003 43.6363
state final:           41.3745 40.6057 40.3660 42.7383 42.0142 39.9039 41.1259
```

Construction medians were 12.1042 and 11.7655 ms, a decrease of 0.3387 ms (2.80%).
State medians were 41.5721 and 41.1259 ms, a decrease of 0.4462 ms (1.07%).
Private-memory medians were 749,568 and 753,664 bytes, an increase of 4,096 bytes (0.55%).
Allocation counts and object sizes remained unchanged from the earlier comparison.

Final collection samples are milliseconds:

```text
60 rows baseline:   2.2516 3.2619 2.8431 3.3556 3.2396
60 rows final:      2.4926 3.7144 2.8262 2.9326 5.3231
100k rows baseline: 22.0838 14.9110 19.5433 19.9940 19.7913
100k rows final:    22.3936 19.8213 18.3544 20.7254 19.4432
1m rows baseline:   35.9291 30.5644 31.1713 41.7057 33.3923
1m rows final:      86.4742 33.7885 33.7336 35.8230 28.8094
```

The 60-row median decreased by 0.3070 ms.
The 100k-row median increased by 0.0300 ms, from 19.7913 to 19.8213 ms (0.15%).
The million-row median increased by 0.3962 ms, from 33.3923 to 33.7885 ms (1.19%).
The 86.4742 ms outlier remains in the raw data.
No outlier was removed to obtain these medians.

Final gallery samples use one median latency and one mean CPU time per process.
Values are milliseconds per paint:

```text
latency baseline: 14.06540 14.91780 14.94320 15.52995 15.57355 15.63605
latency final:    14.76070 14.68140 14.44940 14.84435 15.64435 15.54785
CPU baseline:      4.84375  3.515625 2.65625  3.515625 3.203125 3.515625
CPU final:         3.984375 3.75000  2.734375 3.28125  3.59375  3.59375
private baseline: 101830656 101597184 102662144 102301696 102027264 101330944
private final:    102301696 101863424 101318656 101777408 102666240 101355520
```

The latency medians were 15.236575 and 14.802525 ms, a decrease of 0.434050 ms (2.85%).
The CPU medians were 3.515625 and 3.593750 ms, an increase of 0.078125 ms (2.22%).
Private-byte medians were 101,928,960 and 101,820,416, a decrease of 108,544 bytes (0.11%).
Every gallery retained 298 process handles, one target, 38 peers, and zero idle paints.
Native buffer and bitmap sizes remained 188,864 and 198,744 bytes.
USER/GDI counts varied between 70/37 and 71/39 in both versions.

The separate final styled workload produced these samples in microseconds:

```text
unstyled CPU:     1953.12 3750.00 1718.75
styled CPU:       2578.12 4062.50 2812.50
unstyled latency: 16548.4 16584.3 16753.1
styled latency:   16616.0 16725.7 16695.0
```

The styled CPU median increased by 859.38 microseconds, from 1953.12 to 2812.50 (44.0%).
The styled latency median increased by 110.7 microseconds (0.67%).
These are authored-versus-default workload costs, not a regression in the default path.
Earlier batches showed the opposite CPU direction.
The acceptance follow-up replaces that weak estimate with counterbalanced samples and separate raster measurements.
All three final styled runs passed 432 lifecycle cycles with stable HWNDs, one target, and zero idle paints.

## Verification and limitations

The focused native model and ABI tests passed.
The final native batch passed nine tests after the disabled-context integration rebuild.
That batch included collections, collections-window behavior, C compatibility, and native input integration.
The presentation fixture passed actual Direct2D color, square-corner, left-border, theme, state, and high-contrast pixel checks.
It also passed editor identity, text, selection, target teardown, and idle-paint checks.
The existing WinUI presentation regression passed.

Managed styling tests passed 10,424 assertions.
Rust passed 17 tests and two compile-fail documentation tests.
Rust formatting and Clippy passed.
Generator tests passed 419 assertions, with 11 project-build assertions.
The real declarative integration script passed 56 assertions.
The VS Code package passed all 19 tests after restoration of its missing tokenizer dependencies.

One presentation run failed when USER objects increased from 21 to 24.
A separate idle probe observed the same increase and a later return to 21.
Traces found resource activity in `MSCTF.dll` and `UxTheme.dll`, but did not identify the exact three objects.
Five subsequent targeted CTest runs, a traced run, and three comparison benchmark runs passed.
The test retains its original resource limits and now supports `--trace-resources`.
The cause of these USER objects remains unresolved.
The follow-up later reproduced the same failure against pristine native libraries.

The full managed suite failed in the separate Shell-menu accessibility driver with `DISP_E_MEMBERNOTFOUND`.
The failure was outside the styling-specific tests.
No baseline reproduction establishes that this failure is pre-existing.
The task did not change that menu code or weaken its tests.
Physical IME and screen-reader speech were not manually exercised.

The final counterbalanced default-path medians remain within the working 5% guardrail.
The earlier candidate did not meet that numerical check and was refined before the final comparison.
This is evidence for the sampled workloads, not a universal performance guarantee.
The follow-up records the remaining styled CPU cost and baseline attribution.
Performance acceptance for styled presentation remains open.
The next stage can use the foundation APIs without treating styles as cost-free.

## Acceptance follow-up

### Measurement contract

This follow-up used the same ARM64 Release configuration.
All its artifacts are under `build\styling-arm64\acceptance-followup`.
Builds and desktop checks finished before each measurement batch.
No external reservation or process termination occurred.
The guards found no compiler, linker, or other XUI test process at the completed batch boundaries.
Those guards do not establish exclusive CPU or desktop access.

The full-window fixture now alternates default-first and styled-first runs.
Each variant receives 60 warm-up frames and 600 measured frames.
Each variant uses the same label, 420-by-56-DIP Button, parent layout, native editor, and native peers.
Assertions require exactly 600 root presentations and 1,800 native paint messages.
The authored colors, square corners, three-DIP left border, and hover changes remain present.
Foreground ownership is mandatory throughout timing.

The isolated fixture performs six forward/reverse sweeps across default, equivalent-style, and authored-style variants.
Each state sample performs one million hover transitions, effective-value reads, and checksum updates.
Each raster sample performs 4,000 draws after 100 warm-up draws.
The variants use the same 160-by-96 software target, 120-by-56 face, cached label, and text rectangle.
Direct2D flushes every 32 draws and at the sample end.
The timed raster region excludes `EndDraw` and HWND presentation.

The equivalent style uses default palette colors and a six-DIP radius.
The authored style uses the required square face and left-only border.
These samples distinguish style resolution from the cost of different visual geometry.
They do not substitute for the full-window comparison.

The fixture reports wall time, `GetThreadTimes`, and `QueryThreadCycleTime`.
The thread-time counter has coarse increments, especially in the short isolated loops.
Cycle counts are a separate work estimate, not portable elapsed time.
The runs use neither CPU affinity nor a fixed processor frequency.
All samples and outliers remain in the results.

The measurement-only command is:

```powershell
.\tests\measure-button-styling.ps1 -StyledOnly -StyledRuns 6 `
    -OutputDirectory build\styling-arm64\acceptance-followup\new-comparison
```

`-StyledOnly` requires only the candidate presentation executable, not the pristine probes.
The script has an eight-minute batch limit.
It preserves standard error, exit codes, executable hashes, arguments, timestamps, and failure reasons.
The earlier complete styled batches preceded the executable-hash records.
Their exact executable bytes were not archived before subsequent rebuilds.

### Reproduced cost and retained fix

The first strengthened batch completed at 20:29 UTC.
Its directory is `styled-cost`.
Four runs alternate which variant runs first.
Each value is per measured frame:

```text
UI CPU, us:
default 3463.54 2864.58 3255.21 3072.92
styled  2864.58 3619.79 3880.21 3619.79

UI cycles:
default 9894820 9187490 9763870 9423320
styled  9394460 11282900 11032600 10169100

latency p50, us:
default 16668.5 16579.2 16636.4 16586.2
styled  16605.9 16644.3 16631.1 16582.3

latency p95, us:
default 19772.2 19455.2 19181.0 22777.3
styled  19431.1 19074.2 18931.3 19929.1
```

The CPU medians were 3,164.065 and 3,619.790 us, an increase of 455.725 us (14.40%).
The cycle medians increased by 1,007,255 cycles (10.50%).
The p50 latency medians increased by 7.2 us (0.043%).
This result did not support performance acceptance.

Before the fix, the isolated samples were:

```text
state wall, ns:
default    43.4655 40.5421 44.3444 33.0473 38.8970 37.5868
equivalent 51.9835 52.9430 56.0617 49.4396 48.9707 47.2145
authored   50.1422 50.9485 49.0382 49.5364 47.9500 48.0796

state cycles:
default    102.107 107.501 106.770 91.8957 105.359 102.631
equivalent 141.235 136.069 144.151 138.828 132.120 127.722
authored   135.717 137.125 136.485 137.120 138.310 126.606

raster wall, us:
default    40.2885 40.0531 41.9449 39.5316 42.5680 39.7269
equivalent 38.6781 36.5354 42.4284 38.0192 39.1367 40.1648
authored   47.1135 45.5561 89.3214 44.6092 47.6043 44.6443

raster CPU, us:
default    31.2500 31.2500 27.3438 27.3438 39.0625 27.3438
equivalent 35.1562 35.1562 35.1562 39.0625 35.1562 39.0625
authored   46.8750 46.8750 50.7812 42.9688 39.0625 27.3438

raster cycles:
default    107008 108745 105868 98299.5 105331 104870
equivalent 105139 102541 106109 100136 105921 102596
authored   126873 126102 132903 116496 125925 120005
```

The authored raster median exceeded the default by 6.164 us (15.34%) and 20,414 cycles (19.33%).
The equivalent style did not show that raster increase.
The state wall medians differed by 9.568 ns.
This evidence identified the asymmetric face renderer as a useful optimization target.

`Drawing::styled_button_face` now uses a rectangle fill for a zero-radius background.
Pixel-aligned square border edges also use rectangle fills instead of clips around complete rounded silhouettes.
Fractional device-pixel boundaries and rounded corners retain the original clipped path.
The fast path adds no geometry object, style allocation, native peer, or render target.
Nine whole-image comparisons match the previous algorithm at 96, 120, and 144 DPI.
Those comparisons include integer borders, fractional borders, and four unequal edges.

The complete post-fix batch ended at 20:41 UTC.
Its directory is `square-edge-cost`.
It contains six full-window pairs, with three pairs in each order:

```text
UI CPU, us:
default 3567.71 2838.54 3307.29 2942.71 3333.33 3281.25
styled  3697.92 3281.25 3333.33 3203.12 3723.96 3541.67
paired delta
         130.21 442.71   26.04  260.41  390.63  260.42

UI cycles:
default 9542870 8257700 8878610 8667090 9580080 9388050
styled  10390000 9954070 9775150 9435050 10772400 9565570

latency p50, us:
default 16593.8 16585.5 16647.7 16615.9 16696.2 16652.3
styled  16603.3 16617.2 16676.2 16557.8 16615.9 16612.8

latency p95, us:
default 19261.8 19331.2 19665.2 19747.5 19551.8 19807.3
styled  19180.5 19152.0 19693.9 21247.0 19128.6 19454.7

private bytes after each variant:
default 28594176 28483584 28278784 28315648 28364800 28598272
styled  28565504 28454912 28262400 28299264 28332032 28532736
```

The CPU medians were 3,294.270 and 3,437.500 us, an increase of 143.230 us (4.35%).
However, all six paired CPU differences were positive.
Their median was 260.415 us, and the median paired percentage increase was 8.39%.
The cycle medians increased from 9,133,330 to 9,864,610 (8.01%).
The p50 latency medians decreased by 17.45 us (0.10%).
The p95 latency medians decreased by 290.9 us (1.48%).

The aggregate CPU percentage is not a performance acceptance result.
The default-path guardrail does not approve a meaningful authored-style cost.
The consistent paired CPU differences remain an acceptance concern.
The current evidence does not attribute all that cost to a specific native operation.

Both variants retained one target, 23 USER objects, and 23 GDI objects during timing.
Handle counts ranged from 311 to 313, with a median of 312 for both variants.
Private-byte medians were 28,424,192 and 28,393,472, a decrease of 30,720 bytes.
The exact paint-count, native-peer, idle-paint, and lifetime assertions passed in every complete pair.

The corresponding isolated post-fix samples were:

```text
state wall, ns:
default    36.7674 34.2975 37.5053 35.8009 35.8117 38.6280
equivalent 44.3171 60.5777 55.2012 47.0670 47.0480 44.2898
authored   44.4378 46.2821 57.1744 49.9425 58.2251 49.4397

state cycles:
default    100.453 100.414 98.4286 104.828 103.462 98.4804
equivalent 125.696 132.182 143.467 134.216 135.974 127.773
authored   127.113 129.591 140.359 138.202 144.126 132.356

raster wall, us:
default    37.1071 41.5830 54.6892 40.8637 37.1255 39.2245
equivalent 39.4628 36.3290 45.3059 36.9313 39.3195 35.6927
authored   21.2225 27.1988 22.7360 26.3815 23.7272 22.7583

raster CPU, us:
default    35.1562 31.2500 27.3438 31.2500 39.0625 39.0625
equivalent 35.1562 35.1562 42.9688 23.4375 39.0625 31.2500
authored   23.4375 19.5312 23.4375 23.4375 19.5312 19.5312

raster cycles:
default    104031 105910 100946 108049 103029 106160
equivalent 109298 99226.9 110698 101711 106891 94537.1
authored   59141.7 59687.5 61090.3 65209.7 62680.4 62144.7
```

The authored raster median decreased from the earlier 46.3348 us to 23.24275 us.
Within the post-fix batch, it was 16.80135 us faster than the default.
Its cycle median was 61,617.5, compared with 104,970.5 for the default.
This result supports the square-edge fix without claiming that styles have no cost.

### Additional profiling and blocked presentation repeats

A second trial omitted redundant text clips only when glyph bounds fit inside the content area.
Whole-image checks passed for fractional positions, clipped text, Arabic text, ClearType, and three DPI scales.
The isolated cycle median did not improve: 17,526.2 reference cycles versus 17,765.8 trial cycles.
Three full-window attempts stopped at foreground guards before a complete batch.
The trial was removed because its benefit was not established.
Production text clipping remains unchanged.

The incomplete directories are `contained-text-cost`, `contained-text-repeat`, and `final-contained-text-cost`.
Their partial window samples do not contribute to accepted comparisons.
The last attempt lost foreground at frame zero to process 13836, outside the fixture process 61336.
The guard remained active, and no further full-window retry occurred.
No claim of an interference-free final presentation repeat follows from these aborted runs.

The final retained implementation also passed a hidden, isolated run at 21:02 UTC.
Its files are `final-isolated.stdout.txt`, `final-isolated.stderr.txt`, and `final-isolated.json`.
Its executable SHA-256 is `7D417B3E875CE94FDE5F8F99924C142705DB0BBC9A5AA8E430A6AF30B838B346`.
This executable contains later profiling and failure diagnostics, but the same retained square-edge renderer.

Final isolated samples were:

```text
state wall, ns:
default    19.9597 18.7878 19.2595 20.9276 19.1920 19.0218
equivalent 28.3638 25.7867 25.1557 26.8506 27.1480 25.1618
authored   27.3038 28.2680 26.6701 25.8786 29.0085 31.8828

state cycles:
default    56.7330 55.8155 55.9206 56.7729 56.4860 55.8792
equivalent 75.5476 74.4932 73.9266 74.5456 78.2236 74.3986
authored   74.0037 75.9690 74.9289 74.4688 75.9524 76.8681

raster wall, us:
default    25.2291 24.1204 22.4378 24.1915 24.4263 22.2850
equivalent 25.0312 23.4605 23.8556 23.4492 25.3898 23.1037
authored   14.4588 14.2186 14.4199 14.3862 14.0105 18.4626

raster CPU, us:
default    15.6250 19.5312 23.4375 23.4375 27.3438 23.4375
equivalent 27.3438 23.4375 27.3438 19.5312 19.5312 23.4375
authored   11.7188 11.7188 7.8125 11.7188 11.7188 15.6250

raster cycles:
default    66400.6 64598.2 61113.3 64649.5 62453.6 64619.4
equivalent 65357.0 61872.8 63774.2 64882.9 64116.9 62526.9
authored   38518.0 39442.2 37605.0 41229.3 37016.6 42056.3

fitting-text diagnostic wall, us:
reference 5.46148 5.13992 5.23250 5.99180 5.37518 5.42948
unclipped 4.77882 4.98985 4.98487 4.92002 6.25910 5.43760

fitting-text diagnostic CPU, us:
reference 7.81250 3.90625 7.81250 3.90625 3.90625 3.90625
unclipped 3.90625 3.90625 3.90625 3.90625 0.00000 3.90625

fitting-text diagnostic cycles:
reference 16000.5 14945.1 15037.8 15383.6 15057.1 15314.7
unclipped 14150.4 14497.8 14307.9 14327.2 14824.9 14751.4
```

The final state wall medians were 19.22575 ns default and 27.78590 ns authored, an increase of 8.56015 ns.
The final raster medians were 24.15595 us default and 14.40305 us authored.
Large differences between batches show why cross-batch absolute timings are not controlled comparisons.

The fitting-text diagnostic compares an explicit clip with no outer clip around identical, fully contained glyphs.
Whole-image equality is mandatory before timing.
It uses six alternating pairs, 4,000 draws per sample, and no production clipping change.
Its median wall difference was 0.41497 us, and its median cycle difference was 773.4 cycles.
This software-target diagnostic does not explain the remaining full-window CPU difference.

### Baseline failure attribution

The original managed test project and native DLL came from the pristine source archive.
The follow-up first ran each managed combination twice:

```text
original managed suite + original native DLL: 2 passed
original managed suite + candidate native DLL: 2 passed
current managed suite  + candidate native DLL: 2 passed
```

Every run reached all seven Shell-menu modes and the final text-selection assertions.
Both current-suite runs also passed 10,424 styling assertions.
All six standard-error logs were empty.
The logs use the prefixes `managed-pristine`, `pristine-managed-final-native`, and `current-managed-final-native`.
The managed `DISP_E_MEMBERNOTFOUND` failure did not reproduce.
Its baseline attribution remains unresolved, and no menu change or exception suppression followed.

The native comparison uses `XUI_STYLING_BASELINE` to compile this fixture against original headers and static libraries.
The copied fixture source resides under `baseline-source\tests`, so its relative drawing-header include resolves to the original header.
The compile configuration includes `/O2 /MT /LTCG /DNOMINMAX /DWIN32_LEAN_AND_MEAN`.
The baseline executable is `build\styling-arm64\baseline-lifetime.exe`.
Its SHA-256 is `CD6C554D4B13DE5B7D82CECE31BBE0028F534D72A97B71EBC982305E14FCA3F6`.

The baseline mode omits unavailable style calls and authored pixel checks.
It retains the same real editor, native captures, theme changes, target-loss exercise, teardown, and resource limits.
Each successful run performs 144 cycles across each of three window lifetimes.
This compares original Window/editor/theme lifetime, not nonexistent baseline styling behavior.

The first three alternating baseline/candidate pairs all passed.
Those logs use the prefixes `lifetime-baseline` and `lifetime-candidate`.
Each resource comparison retained USER 21 while alive and USER 5 after teardown.
GDI counts remained 23 while alive and 16 after teardown.

The final native CTest batch later reproduced USER 21-to-24 growth in the candidate.
Its other eight suites passed.
Two immediate baseline/candidate pairs then produced:

```text
recurrence-baseline-1:  failed, USER 21 -> 24, GDI 23 -> 23, handles 315 -> 315
recurrence-candidate-1: passed all three window lifetimes
recurrence-baseline-2:  failed, USER 21 -> 24, GDI 23 -> 23, handles 315 -> 315
recurrence-candidate-2: passed all three window lifetimes
```

The recurrence logs and `recurrence-results.json` preserve the exact failures, exit codes, and executable hashes.
This establishes that the same USER assertion can fail against pristine native libraries under this harness.
It does not identify the three objects or establish that every future USER increase is harmless.
The strict USER equality requirement remains unchanged.
No unrelated framework or operating-system change followed.

### Follow-up disposition

The retained native change is the pixel-equivalent square-edge optimization in `src\drawing.cpp`.
The other follow-up changes are in `tests\styling_window_tests.cpp`, `tests\measure-button-styling.ps1`, and this evidence note.
The temporary text-renderer changes were removed from `src\application.cpp` and `src\drawing.hpp`.
No binding, compiler, template, parent-owned specification, or public index changed during this follow-up.

The retained renderer passed the original color, geometry, state, theme, high-contrast, and native editor checks.
The latest CTest batch passed eight suites and encountered the attributed intermittent USER failure.
Two subsequent complete candidate lifetime runs passed without relaxed limits.
The final hidden profiling run passed its actual-pixel comparisons.

Styled presentation performance remains unaccepted because the complete paired runs retain a positive CPU cost.
The interrupted repeats provide no basis for a stronger claim.
No hold, build, test, or measurement process remains active from this follow-up.
No commit, push, or pull request occurred.

## Scoped phase attribution

This final investigation used separate diagnostic source and build trees.
No production source, build configuration, library, or executable changed.
The artifacts reside under `build\styling-arm64\phase-attribution`.
The investigation made one presentation attempt and one bounded, non-presentation host-update attempt.
It did not identify a production fault that justified another runtime change.

### Probe boundaries and direct reference

The diagnostic records nested QPC and thread-cycle scopes.
The scopes cover the frame, state invalidation, host update, layout, paint, face, content, native composition, and `EndDraw`.
Both inclusive and exclusive counters are available.
The face and content scopes measure CPU command submission, not deferred GPU execution.
The `EndDraw` scope includes presentation waits and any deferred drawing work.
The outer frame residual also includes child-window message handling and `RedrawWindow` overhead.

The fixture compares three modes:

```text
0: unstyled Button
1: Button with its production immutable style and cached effective values
2: unstyled Button with a diagnostic raw-values override at the paint boundary
```

Mode 2 bypasses Button style storage, resolution, and style-specific state invalidation.
It retains the production authored-face and content renderer.
It is not an independent Direct2D implementation or an available application API.
Its rectangle, clipping, text, foreground, background, and border match mode 1.
Whole-Button pixel comparisons passed for both normal and hovered states.
The comparison preserved the same label, bounds, native editor, and peers.

The instrumented build used the same ARM64 Release compiler and SDK configuration.
An empty diagnostic scope cost approximately 1,048 cycles in its calibration sample.
The diagnostic timings include that overhead.
The scopes exist only in the archived diagnostic sources and binaries.

### Presentation attempt

The presentation attempt started at 21:24:21 UTC and stopped at 21:24:30 UTC.
The unchanged foreground requirement failed at frame 56 of the first unstyled block.
No styled or direct-reference timing block completed.
No repeated presentation attempt followed.

The partial trace contains 56 default frames.
In 38 frames, the starting and ending processor numbers differed.
The default phase medians were:

```text
exclusive phase       cycles       wall us
outer frame residual  2055077.5     774.75
state                   30057.0      10.25
host update            156240.0      53.15
layout                      0.0       0.00
paint residual         176923.0      76.05
face                    30951.0      10.45
content                 60859.5      20.60
native composition     885806.0     299.95
EndDraw               2447613.5   15340.85
```

These partial default samples cannot establish a styled regression or improvement.
The QPC frequency was 10,000,000 ticks per second.
The source also included a flush-before-presentation diagnostic, but the interrupted run never reached it.
No conclusion separates GPU submission from presentation through that unexecuted probe.

### Non-presentation host-update attempt

The second attempt completed successfully at 21:30:48 UTC.
It used the same owned fixture with `XUI_PHASE_SYNC_ONLY=1`.
It measured state changes and synchronous host-update messages without `RedrawWindow`.
Assertions required zero paint messages and zero presentations throughout each sample.
This separate diagnostic does not remove the foreground guard from presentation measurements.

Three cyclic mode orders each contained 500 measured updates per mode.
Every sample recorded exactly 500 host updates and zero layout passes.
The original resource, pixel, native editor, selection, target-loss, and three-window lifetime checks also passed.
Raw mean values per update were:

```text
mode block   frame cycles  state cycles  update cycles  frame us  update us  CPU us
0    0       116569.0       9783.34       94523.2         81.1392   67.2922   62.50
0    1        64088.8       5680.59       50875.4         31.1586   21.7714   31.25
0    2       129614.0      11068.30      105014.0        112.3140   91.2608   62.50
1    0        64562.0       5646.66       51650.9         30.6194   24.8800   62.50
1    1       126620.0      11317.10      101899.0        106.8800   85.8634   31.25
1    2        82466.8       6748.98       66623.2         53.4416   44.8062    0.00
2    0       107732.0       9157.77       87243.9         75.5220   61.1352   62.50
2    1       110399.0       9406.54       89966.3         82.1826   69.1908    0.00
2    2       128484.0      10752.30      104404.0        101.1030   81.6758    0.00
```

This diagnostic found no extra layout pass or host-update count in the styled mode.
It also found no consistently higher styled host-update cost.
The coarse CPU counter produced zero-time samples despite positive cycle counts.
That counter behavior and the processor migrations limit attribution.
They do not prove that the earlier whole-window CPU difference was a counter artifact.

The presentation comparison remains incomplete, so actual additional presentation work and scheduling effects remain unresolved.
No runtime optimization followed this investigation.
No new performance acceptance claim follows from these diagnostic timings.

### Preserved identities and cleanup

`production-before.json` contains 173 native, test, resource, and build-input file hashes.
The snapshot identifier hashes the ordered manifest entries as UTF-8 path, NUL, file SHA-256, and newline.
The public documentation files are outside that native snapshot.
The checks in `production-after.json` show that every recorded production source and binary still matches.

```text
final production source snapshot
5fd2a8afa54147bab64f05ccf692fca339fdb3edfffe70204e020af70473e73b

production xui.dll
807a664518db08b15f49d7ce7e461b720a05dcdb29193c8b998dee1c2265f85e

production xui_styling_window_tests.exe
7d417b3e875ce94fde5f8f99924c142705db0bbc9a5aa8e430a6af30b838b346

presentation diagnostic source snapshot
3ff28f08206db75a97c76ff27f05282ccaba00fe89d60591d424e818a456c8fa
presentation-diagnostic.exe
63b5040f999b4b28cc10b0640f3330b5f5d735cf8daf360bead8bda58451d4cd

host-update diagnostic source snapshot
570ba4e8b3668bf7894aa6a6775af563b5809dbf444e5051cbfaa3df5c86e4bf
sync-diagnostic.exe
3b5d4fd4b56398b341cf14db87d9ff21264aa1b78442dcc97b5cdc4617a2cdc5
```

`production-binaries` preserves the unchanged production DLL and fixture executable.
The production fixture hash matches the preceding successful candidate lifetime runs and `final-isolated.json`.
The diagnostic executables have distinct names and hashes.
Their run manifests contain the matching executable hashes, exit codes, and timestamps.

`archives.json` records hashes for three complete source archives.
Archive checks matched every file to its source manifest.
The cleanup removed the temporary diagnostic source and build trees.
Only the named diagnostic binaries, verified archives, build configurations, manifests, and raw logs remain as evidence.
No production probe, active process, hold, commit, or template change remains from this investigation.

## Changed-file inventory

The parent owns `docs\specs\README.md` and `docs\specs\styling-and-templates-design.md`.
This implementation did not edit those two files.
The stage owns the following files:

```text
CMakeLists.txt
CONTRIBUTING.md
include\xui\controls.hpp
include\xui\styling.hpp
include\xui\xui_features.h
src\application.cpp
src\c_api.cpp
src\controls.cpp
src\drawing.cpp
src\drawing.hpp
src\styling.cpp
tests\abi_features_tests.cpp
tests\button_style_probe.cpp
tests\measure-button-styling.ps1
tests\styling_tests.cpp
tests\styling_window_tests.cpp
bindings\dotnet\DeclarativeSample\Counter.xui
bindings\dotnet\GeneratorTests\BuildTests.ps1
bindings\dotnet\GeneratorTests\Fakes.cs
bindings\dotnet\GeneratorTests\Fixtures\Styling.xui
bindings\dotnet\GeneratorTests\GeneratorTests.csproj
bindings\dotnet\GeneratorTests\Program.cs
bindings\dotnet\Tests\Program.cs
bindings\dotnet\Tests\StylingTests.cs
bindings\dotnet\Xui.Generator\Parser.cs
bindings\dotnet\Xui.Generator\Styling.cs
bindings\dotnet\Xui.Generator\XuiGenerator.cs
bindings\dotnet\Xui\Controls.cs
bindings\dotnet\Xui\Native.Features.g.cs
bindings\dotnet\Xui\Styling.cs
bindings\rust\xui-sys\src\features.rs
bindings\rust\xui\src\features_generated.rs
bindings\rust\xui\src\lib.rs
bindings\rust\xui\src\styling.rs
integrations\vscode-xui\README.md
integrations\vscode-xui\snippets\xui.json
integrations\vscode-xui\syntaxes\xui.tmLanguage.json
integrations\vscode-xui\test\configuration.test.mjs
integrations\vscode-xui\test\tokenizer.test.mjs
docs\specs\control-styling.md
docs\specs\bindings.md
docs\specs\xui-language.md
docs\llm\README.md
docs\llm\control-styling.md
```

`features_generated.rs` can appear in status after regeneration without a textual diff because of newline conversion.
No template implementation, branch operation, commit, push, or pull request belongs to this stage.
