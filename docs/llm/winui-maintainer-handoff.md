# WinUI-style experiment: maintainer handoff

Status date: September 15, 2026.

This handoff records an earlier development checkout.
Its branch, uncommitted-state warning, and local results are historical, not the state of the current checkout.
Use [CONTRIBUTING](../../CONTRIBUTING.md) for current build commands.

## Current state

The optional WinUI style covers the custom control catalog and has several reference-based fidelity passes.
It is more than a palette change, but it is not a pixel-exact or behavior-complete WinUI implementation.
The user wants controls that pass visually as WinUI, without losing XUI's lightweight rendering or native editing.
The latest pass adds Segoe Fluent Icons and corrects button, checkbox, radio, navigation, and breadcrumb presentation.

The [design plan](../specs/winui-design-plan.md) contains the original proposal, source references, and detailed implementation notes.
Its implementation sequence includes both completed work and future work.
This handoff separates those states and identifies the next practical tasks.

### Repository state at handoff

| Item | State |
| --- | --- |
| Branch | `zadjii-msft-winui-redesign` |
| Starting HEAD | `2b3d53a` (`merge all the branches`) |
| Persistence | The implementation and this handoff are worktree changes. No experiment commits exist yet. |
| Active build directory | `build\winui-fidelity`, ARM64 Release |
| Public API | C++ only. The C ABI and binding defaults remain unchanged. |
| Default appearance | Classic |

CAUTION: Preserve the uncommitted experiment files before a checkout reset, worktree deletion, or handoff to another checkout.
The branch name alone does not contain this implementation.

## Completed work

### Style architecture and gallery

`WindowOptions::visual_style` and `Window::set_visual_style` select Classic or WinUI independently of `ThemeMode`.
The backend propagates style before measurement.
Controls retain their identities, values, and native editors during a live style change.
Style changes can alter geometry without replacing the shared render target.

The compact `xui_winui_gallery` contains Overview, Collection, and Controls pages.
Controls contains the **Control specimens** page, including a command-icon row.
The ordinary gallery accepts `--winui-catalog` for the complete catalog.
Both galleries support live style and theme changes.

### Measurement, typography, and native fields

| Area | Implemented behavior |
| --- | --- |
| Size ownership | Constructor defaults can change with style. Explicit preferred and fixed sizes remain application constraints. |
| Buttons | Natural text measurement plus 24 horizontal and 13 vertical DIPs of chrome, normally 32 DIPs high. |
| Fields | Single-line height defaults to 32 DIPs. Headers use a 20-DIP line box and an eight-DIP gap. |
| Labels | WinUI removes the Classic minimum line-height floor. Subtitle and body-strong roles provide 20-DIP and 14-DIP semibold text. |
| Wrapped labels | DirectWrite measurement uses the available width and optional line cap. Layouts remain cached. |
| Font policy | Supported Latin UI locales use installed Segoe UI Variable with automatic optical sizing. Native EDIT uses Segoe UI Variable Text. |
| Font fallback | Classic, non-Latin locales, and unsupported variable-font configurations retain Segoe UI. Authored document fonts remain unchanged. |
| Native field fill | EDIT and Direct2D use the same alpha-composited background over the actual parent surface. |
| Field borders | Cached elevation and focused-bottom gradients replace flat approximate outlines. |
| Clear action | Focused, nonempty plain fields have an undoable clear action, no extra Tab stop, and no focus transfer. |
| IME | The clear action stays hidden during composition. Native input retains its composition, selection, and undo behavior. |

The backend creates a clear-button peer only when an eligible field first needs it.
It retains that peer across style changes and releases it with the field.
This is an intentional peer addition, not a render target per control.

### Compound controls and interaction

| Control | Implemented behavior |
| --- | --- |
| NumericInput | Shared outer field, inline Up/Down buttons, and optional hidden spin placement. First focus selects the value. Later clicks place the caret. |
| ComboBox | A 38-DIP arrow region, shared editable frame, bounded popup, and selected-row alignment for noneditable popups. |
| RadioGroup | Natural 32-DIP row pitch. WinUI selection commits on matching release, not press. |
| Choice-list mode | Measured row bodies, explicit list/item margins, clipped selection reveal, and matching pointer/UIA bounds. |
| Expander | Content-sized body, minimum 48-DIP header, joined frame, and header-only disclosure interaction. |
| ContentDialog | Content-sized centered layout, bounded dimensions, two-line title cap, scrollable body, and fixed action footer. |
| ScrollView | Overlay scrollbar and passthrough modes support dialog presentation without changing the Classic layout. |

There is no `ChoiceList` class.
Choice-list mode is `RadioGroup(name, true)`.
XUI keeps inline numeric spin buttons as its default.
`NumberSpinPlacement::hidden` supplies the WinUI-default variant.

### Icons and control states

WinUI icons use the installed **Segoe Fluent Icons** font.
`src\symbols.hpp` maps semantic symbols to explicit glyph codepoints.
The renderer resolves glyph indices and metrics once per font face.
Paint uses glyph runs without per-paint text layouts, icon bitmaps, or per-control render targets.

The symbol cache survives style changes and render-target loss.
Full renderer release frees the cache.
Grayscale antialiasing prevents colored icon fringes without changing ordinary text rendering.
Classic retains its vectors and existing caption font.

Coverage includes commands, navigation, captions, breadcrumbs, search, disclosure arrows, field actions, menu checks, checkbox marks, grid indicators, and status symbols.
Generic file placeholders also use the symbol path.
Shell icons, thumbnails, and authored content retain their original appearance.
Radio dots remain ellipses, as in the reference template.

If Segoe Fluent Icons is absent, the renderer uses Segoe MDL2 Assets and reports the fallback through `OutputDebugStringW`.
Missing symbol fonts or required glyphs produce an explicit error.
There is no per-glyph fallback and no font redistribution.

| Area | Latest correction |
| --- | --- |
| Standard buttons | Translucent fills, three-DIP elevation gradients, and separate pressed/disabled borders. |
| Accent buttons | Separate fill opacity and foreground states. Dark primary accent text is black, not dark blue. |
| Subtle buttons | Transparent idle state and translucent interaction states, rather than opaque standard-button fills. |
| Checkboxes | Distinct interaction brushes, weaker unchecked pressed outline, and documented font marks with state-specific placement. |
| Radios | Correct selected fill states and 12/14/10-DIP normal/hover/pressed dots. Disabled dots measure 14 DIPs. |
| Menu checks | Standalone glyphs instead of filled checkbox squares. |
| Navigation | A 14-DIP semibold pane title and an unframed toggle button. |
| Breadcrumbs | Separate label and separator glyph runs, overflow ellipsis, and subtle buttons. Existing overflow geometry remains. |
| Tabs | Body-sized labels and Fluent close symbols. |

High contrast uses system colors instead of decorative light/dark tokens.
Pressed accent text deliberately uses the reference's translucent secondary foreground.
Tests assert that token separately from normal/hover contrast requirements.

### Broad coverage is not full fidelity

Sliders, progress indicators, grids, collections, scrollbars, split views, charts, tooltips, and status surfaces have WinUI-style paint coverage.
Native document and password fields have themed frames.
Image, vector, map, and runtime-host frames preserve authored content.
These surfaces still need control-by-control comparison before any full-fidelity claim.

## Source map

All paths in this table are relative to the repository root.

| Files | Responsibility |
| --- | --- |
| `include\xui\theme.hpp` | Style metrics, state tokens, ARGB brushes, and geometry helpers. |
| `src\drawing.hpp`, `src\drawing.cpp` | DirectWrite fonts, glyph runs, cached gradients, shared control paint, and target/resource lifecycle. |
| `src\symbols.hpp` | Internal semantic glyph registry and ButtonIcon mapping. |
| `src\application.cpp` | Peer collection, style propagation, native field composition, per-control paint, focus, input, and popup integration. |
| `include\xui\core.hpp`, `src\core.cpp` | Explicit size ownership and layout infrastructure. |
| `include\xui\controls.hpp`, `src\controls.cpp` | Presentation context, text roles, wrapping, Tab-stop policy, and ScrollView modes. |
| `include\xui\foundation.hpp`, `src\foundation.cpp` | NumericInput, ComboBox, RadioGroup, and Expander geometry and interaction. |
| `include\xui\documents.hpp`, `src\documents.cpp` | Retained dialog layout and action/footer composition. |
| `include\xui\native_edit.hpp`, `src\native_edit.cpp` | Native font and inset changes without replacing the editor HWND. |
| `src\control_accessibility.*`, `src\workspace_accessibility.cpp` | Choice-item bounds and UIA hit testing. |
| `include\xui\navigation.hpp`, `src\navigation*.cpp` | Pane typography, retained navigation, and breadcrumb appearance. |
| `src\list_peer.cpp` | File-list style integration and generic file placeholders. |
| `demo\winui_gallery.hpp`, `demo\gallery.cpp` | Experiment, specimens, and complete-catalog integration. |
| `CMakeLists.txt` | Gallery targets and targeted regression registration. |

## Remaining work, in recommended order

### 1. Finish static control fidelity

The next pass needs a discrepancy list with reference captures, not another global radius or palette adjustment.
Tabs, breadcrumbs, menus, navigation, collections, grids, sliders, scrollbars, and progress indicators remain useful comparison targets.
This list identifies audit targets, not established defects in every control.

1. Compare one control family against the pinned WinUI template and the live Gallery.
2. Record normal, hover, pressed, disabled, selected, keyboard-focus, and open-popup differences.
3. Correct measurement, paint, pointer bounds, and UIA bounds together.
4. Add a specimen and a regression assertion for each intentional behavior change.
5. Repeat at 96, 144, and 192 DPI in light, dark, and high contrast.

Known unresolved details:

| Item | Evidence and next investigation |
| --- | --- |
| ComboBox row height | The Gallery exposed 33-DIP bodies and 37-DIP pitch. XUI measures 31/35 with a 19-DIP text line. |
| ComboBox explanation | The pinned presenter has 12 vertical DIPs of padding, no item border, no fixed minimum, and inherited 14-DIP text. Font/layout investigation remains. |
| Checkbox motion | Static font marks replace the animated accept visual. Animation timing and animated paths remain absent. |
| Radio dot outline | Overlapping template states affect the dot stroke. Effective state precedence remains unresolved. |
| NumericInput semantics | Compact spin popup, expression parsing, and full WinUI validation/NaN semantics remain absent. |
| Breadcrumb layout | The current equal-share/overflow layout is an XUI adaptation, not a completed BreadcrumbBar reproduction. |
| Text shaping | Native GDI editing and DirectWrite can still differ in shaping, baseline, and optical appearance. |

Do not invent ComboBox borders or fixed row heights to conceal the measured discrepancy.
Do not turn `Toggle` into a ToggleSwitch through paint changes.
`Toggle` is a checkbox with checkbox semantics.
A switch needs its own model, interaction, geometry, and UIA patterns.

### 2. Complete system and accessibility coverage

Automatic system accent selection is absent.
System theme, transparency, animation preferences, text scale, and explicit application overrides need a documented precedence policy.
Larger text, long translations, bidirectional content, mixed-monitor DPI, Narrator, and keyboard navigation need broader acceptance coverage.

The current icon fallback is explicit but strict.
A supported missing-glyph fallback policy needs a separate design and coverage on systems without Segoe Fluent Icons.
Existing native fields must retain IME, selection, undo, focus, and accessible names.

### 3. Measure performance before accepting the skin

Resource and frame tests pass, but no matched performance benchmark establishes equivalence to Classic.
The design plan proposes five-percent limits for latency and warm private commit.
Those limits remain acceptance targets, not measured results.

1. Use matched Classic and WinUI workloads with identical architecture, DPI, client size, data, and visible content.
2. Measure startup, first paint, median/p95 input-and-paint latency, idle activity, and warm private commit.
3. Repeat resize cycles, popup cycles, and theme/style changes.
4. Record native peers, render targets, text layouts, glyph resources, and native bitmap retention.
5. Account explicitly for the lazy clear-button peer.

Existing starting points are `tests\measure-window-memory.ps1`, `tests\measure-resize-memory.ps1`, and `xui_window_memory_probe`.
The [memory report](../specs/windows-gui-memory.md) describes driver-sensitive allocation behavior.
Working-set trimming is not a substitute for matched measurements.

### 4. Add motion and materials as separate experiments

Hover transitions, checkbox transitions, popup motion, and animated indeterminate progress remain absent.
Indeterminate progress currently uses a static segment.
A future scheduler needs bounded activity and an idle stop condition, not a permanent paint loop.

Mica and acrylic remain absent.
The opaque HWND target and child fills can conceal a DWM backdrop.
A successful backdrop API call does not establish visible material rendering.
Root-drawn popups also cannot acquire acrylic merely through a window attribute.

A material experiment needs composition, focus, occlusion, device-loss, resize, and power-cost evidence.
High contrast, transparency-off, unsupported platforms, and remote sessions need explicit solid fallbacks.
There is no approved renderer replacement in this work.

### 5. Productize only after the presentation contract stabilizes

The subsequent explorer migration adds window style selection to `xui_layout.h` and the C# `Window` binding.
The managed explorer uses `.xui` layouts and selects WinUI.
The Rust wrapper does not expose style selection.
Task Manager and other samples still need their own composition review and acceptance runs.
The experiment does not make WinUI the default for existing applications.

Native date/time controls, suggestion lists, editor scrollbars, disabled RichEdit backgrounds, and third-party Shell menus retain platform-owned visuals.
Root-drawn popups remain constrained to the root viewport.
These are documented boundaries, not completed WinUI equivalents.

## Build and regression procedure

The last development environment used Visual Studio 2022 Preview, MSVC 19.44, and Windows SDK 10.0.26100.0.
The machine ran ARM64 Windows build 28637.
The commands use CMake and CTest from a Visual Studio developer PowerShell.

1. Close the gallery executable from the build directory before relinking it.
2. Configure the desktop-test build:

```powershell
cmake -S . -B build\winui-fidelity -G "Visual Studio 17 2022" -A ARM64 -DBUILD_TESTING=ON -DXUI_DESKTOP_TESTS=ON -DXUI_ENABLE_WEBVIEW2=OFF
```

For x64, use `-A x64` and a separate build directory.

3. Build the experiment and the focused regression targets:

```powershell
cmake --build build\winui-fidelity --config Release --parallel 4 --target xui_winui_gallery xui_gallery xui_gallery_smoke xui_window_tests xui_winui_style_tests xui_winui_form_layout_tests xui_winui_presentation_window_tests xui_flicker_tests xui_navigation_tests xui_navigation_view_tests xui_control_tests xui_core_tests
```

4. Run desktop suites serially on an interactive Windows desktop:

```powershell
ctest --test-dir build\winui-fidelity -C Release -R "^(xui_core_tests|xui_control_tests|xui_navigation_tests|xui_navigation_view_tests|xui_winui_style_tests|xui_winui_form_layout_tests|xui_winui_presentation_window_tests|xui_winui_lifecycle_tests|xui_winui_catalog_smoke|xui_winui_flicker_tests)$" --output-on-failure -j 1
```

5. Run the experiment or the complete catalog:

```powershell
.\build\winui-fidelity\Release\xui_winui_gallery.exe
.\build\winui-fidelity\Release\xui_gallery.exe --winui-catalog
```

The executables also accept `--light`, `--high-contrast`, and `--system-titlebar`.
Controls > Control specimens is the starting point for size, icon, field, and disabled-state comparison.

### Test ownership and last results

| Test/source | Coverage |
| --- | --- |
| `tests\winui_style_tests.cpp` | State tokens, alpha precedence, contrast policy, typography roles, measurement, and style roundtrips. |
| `tests\winui_form_layout_tests.cpp` | Form geometry, compound reservations, choice rows, radio sizing, and content-sized expanders. |
| `tests\window_tests.cpp` via `--style-lifecycle` | Native identity, clear/undo/IME, focus, selection, field brush reuse, glyph availability, and resource lifecycle. |
| `tests\winui_presentation_window_tests.cpp` | Native presentation at 96/144/192 DPI, dialogs, overflow, and diagnostic context. |
| `tests\flicker_tests.cpp` via `--winui` | Presented text/icon retention, target-loss behavior, and absence of colored glyph fringes. |
| `tests\gallery_smoke.cpp` via `--winui-catalog` | Complete-catalog style switching, state retention, and foreground-sensitive focus assertions. |
| `tests\navigation_tests.cpp`, `tests\navigation_view_tests.cpp` | Existing navigation and breadcrumb model behavior. |
| `tests\documents_tests.cpp`, `tests\documents_window_tests.cpp` | Dialog model, native document state, style changes, and occlusion. |

All ten suites in the command passed after the icon, state, navigation, and breadcrumb changes.
That run took 149.31 seconds.
After the final grayscale change, lifecycle, flicker, and catalog suites passed again in 135.65 seconds.
Earlier fidelity work also passed foundation, document-model, and WinUI native-document suites.
Those earlier results are not a fresh full-suite run of the final revision.

The complete `xui_window_tests` suite has a previously isolated retained-page buffer-growth failure on this machine.
Its assertion expects a taller field to enlarge a native buffer, but native EDIT height follows font height.
The targeted `xui_winui_lifecycle_tests` passes independently.
The experiment does not weaken that assertion.

Desktop focus interference can cause unrelated failures.
The native presentation fixture records phase, DPI, theme, style, and selection endpoints.
It preserves the original exception instead of replacing it with a popup-dismissal callback failure.
Foreground-dependent catalog assertions remain intact.

## Reference baseline and maintenance rules

The source baseline is `microsoft/microsoft-ui-xaml`, release `winui3/release/2.4.0`.
The pinned commit is `e8442d07ae57d2d3e653e616831f504937881bd3`.
The installed comparison application was WinUI 3 Gallery 2.9.3.0.
That Gallery targets Windows App SDK 2.0.1, so source and installed-application versions are not identical.

The design plan links the pinned Button, TextBox, NumberBox, ComboBox, ContentDialog, RadioButton, Expander, and font-resolution sources.
Additional latest-pass resources under `src/controls/dev` include:

- `CommonStyles/Common_themeresources_any.xaml`
- `CommonStyles/CheckBox_themeresources.xaml`
- `CommonStyles/MenuFlyout_themeresources.xaml`
- `NavigationView/NavigationView.xaml`
- `NavigationView/NavigationView_themeresources.xaml`
- `Breadcrumb/BreadcrumbBar.xaml`
- `Breadcrumb/BreadcrumbBar_themeresources.xaml`

The [Segoe Fluent Icons registry](https://learn.microsoft.com/en-us/windows/apps/design/iconography/segoe-fluent-icons-font) supplies glyph names and codepoints.
The implementation uses independently written C++ measurement and drawing code, not a XAML runtime.

**Maintenance rules**

- Preserve state across style changes, not identical rectangles.
- Keep explicit application sizes authoritative.
- Keep Classic defaults and high-contrast overrides separate.
- Keep native editing native.
- Keep virtual collections virtual.
- Resolve alpha against the actual parent surface.
- Keep paint, pointer geometry, and UIA bounds consistent.
- Add no per-row timer or render target.
- Report unsupported resources explicitly instead of drawing missing-character boxes.
- Record unresolved differences rather than inventing template facts.
- Update this handoff after each accepted fidelity pass.
