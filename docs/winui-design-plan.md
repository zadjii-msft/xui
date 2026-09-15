# A Windows 11 appearance for XUI

The [maintainer handoff](winui-maintainer-handoff.md) separates completed work from remaining tasks and includes build commands and the latest regression results.

## Recommendation and scope

**A convincing WinUI-style XUI is feasible without replacing Win32, Direct2D, or DirectWrite.**
The first step is a visual style layer, not a port to WinUI.
The main work is consistent control states, typography, layout, and system integration.
Rounded rectangles and neutral colors are the easy part.

This plan targets the Windows 11 Fluent appearance of WinUI 3.
It does not target XAML compatibility, the complete WinUI API, or every behavior of Windows File Explorer.
File Explorer is an application reference, not a specification for all WinUI controls.
The existing XUI interaction model, native text input, virtualization, and UI Automation remain in place.

Status: the opt-in solid-surface style now covers the custom control catalog, not only the compact experiment.
The [README](../README.md#winui-style-gallery-experiment) contains build instructions and the current scope.
The style includes state tokens, button variants, fields, selection marks, sliders, grids, status surfaces, and popup geometry.
It retains the renderer and native text input. Classic retains its vector icons.
The fidelity pass also gives core controls distinct WinUI measurement and part layout.
Runtime style changes preserve model state, not identical rectangles.
The full-catalog section records the implemented scope.
Materials, bindings, and matched performance measurements remain separate stages.
The default appearance remains unchanged.
No matched latency or memory benchmark establishes the performance gates in this plan yet.

The experiment has targeted token, contrast, interaction, lifecycle, and frame-presentation coverage.
The lifecycle check preserves native text, selection, focus, control identity, and the shared target during style changes.
The existing full `xui_window_tests` suite separately fails its retained-page buffer-growth assertion on this machine.
That fixture expects a taller input to enlarge the native buffer, but native EDIT height follows font height.
The isolated fixture reproduces the failure without the new style lifecycle, and cleanup leaves zero live targets.
The experiment does not change that existing assertion or native input geometry.
`xui_winui_lifecycle_tests` runs the relevant lifecycle independently.

The full-catalog sweep checks runtime style changes on every page.
Its foreground-dependent focus assertions remain intact.
Native document checks cover themed frames, open-dialog style changes, and native-window occlusion.
The fidelity pass also uses the installed WinUI 3 Gallery as a live visual reference.

For one engineer familiar with XUI and Windows graphics, the initial planning estimates are:

- **2-4 engineering days:** an opt-in gallery experiment with solid surfaces and representative control states.
- **4-8 engineering weeks total:** a coherent skin across existing control families, samples, accessibility states, and bindings.
- **2-4 additional weeks of contingency:** real materials and animation, if the renderer needs composition changes.

These are estimates, not measured delivery rates.
They assume the current controls retain their behavior and the work does not add new file-management features.
A pixel-exact replacement for every WinUI control is a multi-month compatibility project with continuing maintenance.
The materials experiment must precede any firm estimate for that part.

## Starting point

Before this experiment, `include\xui\theme.hpp` supplied `ThemeColors`, `theme_colors`, and `VisualMetrics`.
The original palette lacked separate control fills, strokes, and text colors for each state.
Its single default radius did not describe every Windows control and surface.
The experiment adds typed state colors and separate control and surface metrics.

`src\application.cpp` contains most control painting in `Window::Impl::paint_control`, plus `paint_edit` and popup composition.
It already reads hover, press, focus, checked, and enabled state.
However, many branches contain literal radii, insets, and colors selected from the same small palette.
Labels and controls also repaint their rectangular parent background.
That behavior matters for layered surfaces and translucent backgrounds.

`src\drawing.cpp` supplies Direct2D primitives, DirectWrite formats, cached resources, and `Palette::system`.
`Drawing::begin` creates an HWND render target and clears the client area with an opaque background.
`Palette::system` already respects Windows high contrast.
The application also responds to theme, system-color, settings, and DPI messages.
Those messages do not by themselves provide automatic light/dark selection or an OS accent policy.

`src\controls.cpp` and `include\xui\controls.hpp` own measurement and input behavior.
For example, `Control::measure` contains fixed button heights and text insets.
`include\xui\file_list.hpp` fixes the file-row height at 32 DIPs.
A density change must update measurement, painting, hit testing, scrolling, and accessibility bounds together.

`src\window_host.cpp` already applies dark title-bar mode, caption color, and caption text color through DWM.
`include\xui\titlebar.hpp` supplies a custom `TitleBar` with caption buttons and hit testing.
The gallery already uses the custom title bar.
The explorer currently uses a system title bar and its own pane composition in `demo\browser.cpp`.

`src\list_peer.cpp` and `Drawing::collection_row` contain additional row painting.
Native EDIT, RichEdit, suggestions, and native menus have separate font and color paths.
A change limited to `theme.hpp` cannot produce a consistent result across these surfaces.

## Proposed appearance

The visual target has neutral light and dark backgrounds, distinct content layers, restrained borders, and accent color for meaningful states.
Toolbar buttons have quiet surfaces.
Primary actions use accent fills.
Selection and keyboard focus remain visibly different.
The explorer retains its compact layout and split-pane workflow.
A Windows appearance does not require a large navigation rail or less useful file space.

Windows guidance distinguishes small control corners from larger container corners.
The initial style uses 4-DIP control corners and 8-DIP flyout/dialog corners, subject to comparison with the chosen WinUI reference.
Shared edges remain square.
DWM controls the outer window corners, including maximized and snapped states.
Existing pill-shaped controls retain their appropriate geometry.

Typography uses Segoe UI Variable where the installed font and text path support it.
Segoe UI remains the explicit fallback.
Segoe Fluent Icons supplies suitable command symbols through the installed system font.
If Segoe Fluent Icons is absent, the WinUI style uses Segoe MDL2 Assets and reports the fallback in the debugger.
Missing symbol fonts or required glyphs produce an explicit error.
File thumbnails and Shell icons remain file content, not generic command symbols.
No font redistribution is necessary for this approach.

## Full-catalog implementation plan

The first catalog milestone extended the solid-surface style across XUI-owned control chrome.
It keeps the runtime Classic/WinUI switch, current density, interaction geometry, and native editing.
The compact experiment remains available.
The complete gallery has its own style switch and a `--winui-catalog` entry point.

The implementation order is:

1. Add shared painting helpers for field frames, selection marks, chevrons, sliders, focus, and surface frames.
2. Apply those helpers to radio groups, choice lists, combo boxes, numeric inputs, expanders, ranges, and progress.
3. Extend grids, tree/list collections, scrollbars, split views, and chart frames.
4. Complete dialog, tooltip, inline-status, color-picker, document, image, and hosted-content surrounds.
5. Exercise the full gallery with runtime style changes and representative interactions.

Each control retains an explicit Classic path.
High contrast uses system colors, including selected and disabled states.
Style changes do not replace controls, lose selection, reset native input, or create extra render targets.
The style does not recolor authored images, vector shapes, map data, video, or web content.

Native date/time controls, their calendar popups, native suggestion lists, and third-party Shell menus retain their platform appearance.
Native EDIT and RichEdit retain their editing engines inside XUI-styled surrounds.
Document and password frames reserve a small inset around the native child. Their outer control bounds do not change.
Style changes arrange existing native document children without replacing their HWNDs.
Native editor scrollbars remain platform-owned.
Disabled RichEdit backgrounds also retain the Windows system color, which can appear light beneath a dark modal dimming layer.
The style does not replace disabled controls with interactive read-only controls to avoid that native limitation.
Runtime hosts retain their own content rendering.
These boundaries are deliberate, not claims of complete visual control over those surfaces.

The initial appearance tests cover light, dark, and high contrast.
They also cover disabled, focused, selected, expanded, checked, and indeterminate states where each control supports them.
The full-gallery sweep compares Classic and WinUI on every page without changing the page model.
Focused regressions cover popup dismissal, grid selection, sliders, native text, and shared resource ownership.
Authored content colors and existing Classic behavior remain unchanged.

The first paint milestone excluded materials, animation, new controls, density changes, font replacement, and binding API expansion.
The subsequent fidelity pass changes typography and control geometry.
Materials, animation, and binding API expansion remain separate work.

### Implemented coverage

- **Shared paint:** field frames, surface frames, check marks, mixed-state marks, radio indicators, chevrons, focus rings, and scrollbar thumbs.
- **Forms:** radio groups, choice lists, editable and noneditable combo boxes, numeric-input buttons, expanders, and native document/password frames.
- **Ranges:** filled slider tracks, ringed thumbs, and distinct drag/hover states. Reversed and vertical sliders retain their value positions.
- **Progress:** thin tracks, accent completion, warning-colored paused state, error state, and a static indeterminate segment.
- **Data:** rounded grid selections, header check states, collection actions, tree disclosure marks, file-list scrollbars, chart frames, and split handles.
- **Surfaces:** semantic status backgrounds, subtle dismiss actions, accent dialog defaults, modal dimming, popup shadows, tooltip frames, and content-host borders.

The modal dimming layer also covers native owner windows.
Native document tests switch styles with a dialog open and retain its content, focus, and root render target.
The full-gallery test switches styles on each materialized page and checks accessible state, native text, peer counts, and target ownership.
Color tests check message contrast on all status surfaces.
Slider tests cover minimum, intermediate, and maximum values in both orientations and both directions.

The painter adds no animation timer or render target.
This is a structural property, not a measured latency or memory comparison.
Matched performance measurements and a pixel-by-pixel WinUI reference comparison remain open work.

## Control presentation fidelity

Paint coverage did not make the original experiment a faithful WinUI implementation.
Shared Classic dimensions, composite frames, and dialog layout remained visible.
The presentation layer separates model state from presentation without a second UI engine.

`Control::set_visual_style` supplies the backend presentation context before measurement.
`Element::set_default_size` distinguishes constructor defaults from application-authored preferred and fixed sizes.
The active style can change default dimensions without overwriting those application constraints.
Style changes invalidate layout and retain controls, native editor HWNDs, values, and selections.

The core WinUI presentation has 32-DIP single-line fields and normally 32-DIP buttons.
Button height grows with measured text and the template's asymmetric vertical padding.
Input headers use 14-DIP text, a 20-DIP line box, and an eight-DIP gap.
The native EDIT receives enough interior height for its full text line.
NumericInput and editable ComboBox share one field frame across their retained parts.
Numeric fields select the entire value on first focus.
Later pointer clicks in the focused field place the caret normally.
Style changes do not replace an existing numeric selection.
Field fills use the pinned normal, hover, focused, and disabled alpha colors.
The renderer composites these colors over the actual parent surface.
Native EDIT receives the same opaque color as its surrounding Direct2D field.
Field borders use a bottom-oriented gradient with the reference stroke alpha, not secondary text colors.
Disabled fields have no strong bottom stroke.
Normal and focused states each reuse a cached gradient brush.
High contrast retains system colors and uses a two-DIP field outline.
Buttons and labels no longer cover WinUI parent surfaces with opaque rectangular backgrounds.
Keyboard navigation shows focus rings separately from pointer focus.
WinUI radio and choice-list pointer selection commits on release, not on press.
Dragging outside the pressed item cancels selection.
Classic retains its original pointer behavior.
An expanded Expander has one outer frame and a header separator.
Its default size follows its header and content, with a 48-DIP minimum header and 16-DIP content padding.
Explicit outer sizes remain application constraints.
Only its header accepts hover and disclosure gestures.
Releasing a header press over the body cancels disclosure.

Noneditable ComboBox popups align the selected row over the closed field.
Viewport limits can move the popup away from this alignment.
The popup keeps its position during keyboard preview, without committing the preview.
Editable popups retain below-or-above placement.
The WinUI ComboBox arrow column occupies 38 DIPs.
Choice painting, pointer hit testing, and UI Automation bounds use the same inset item body.

Numeric inputs retain inline spin buttons as the XUI default.
`set_spin_placement(NumberSpinPlacement::hidden)` selects the WinUI-default hidden variant.
Inline buttons retain their separate outer allocation and inner editor reservation.
Compact spin popups and complete WinUI numeric-validation semantics are not implemented.

Plain TextInput fields show a clear action when focused, nonempty, and wide enough.
Search fields, shortcut-hint fields, composite editors, and active IME composition do not show this action.
The action uses native EDIT replacement, so native undo restores the text.
It keeps input focus and adds no Tab stop.
The backend creates one clear-button peer when an eligible field first needs it.
The backend retains this peer across style changes and releases it with the field.

Latin UI locales use the installed Segoe UI Variable typographic family through DirectWrite.
Text formats enable automatic optical sizing.
Native EDIT uses the corresponding Segoe UI Variable Text face.
Classic, non-Latin UI locales, and systems without the variable font retain Segoe UI.
Authored document typography remains unchanged.
Text layouts remain cached, including width-aware wrapped labels.

WinUI symbols now use the installed Segoe Fluent Icons font.
The renderer resolves glyph indices and design metrics once per font face.
Paint calls use DirectWrite glyph runs without text layouts, icon bitmaps, or per-control render targets.
Symbols use grayscale antialiasing to prevent colored fringes. This does not change the antialiasing mode for ordinary text.
The symbol cache survives theme changes, style changes, and render-target loss.
Full renderer release frees the cache.

The shared symbol path covers command buttons, navigation items, window captions, disclosure arrows, search, and native field actions.
It also covers grid sort and filter indicators, tab close buttons, status symbols, and generic file placeholders.
Command icons use 16- or 20-DIP sizes. Template-specific chevrons and close affordances use 12 DIPs.
Caption glyphs retain their 10-DIP size. Captions use ChromeClose rather than the larger command Cancel glyph.
Shell icons, thumbnails, and authored text do not use this substitution.
The specimen page includes a command-icon row for live style comparison.
Native coverage checks installed-font selection, required glyph availability, cache retention, and release.

The next state pass replaces opaque button approximations with the pinned translucent template brushes.
Standard and accent buttons use cached three-DIP elevation gradients, rather than a uniform outline and a straight bottom line.
Standard gradients emphasize the top edge in dark mode and the bottom edge in light mode.
Accent gradients emphasize the bottom edge in both modes.
Standard fills stop inside the border. Accent fills extend under the border.
Pressed and disabled buttons use their separate flat-border states.
Subtle buttons use translucent fills and matching borders, not opaque standard-button backgrounds.

Checkboxes and radio buttons now distinguish normal, hover, pressed, and disabled indicator brushes.
An unchecked press weakens the outline. Selected indicators use accent opacity for hover and press.
Dark accent foregrounds use black, not dark blue.
The disabled radio dot retains the primary accent foreground and measures 14 DIPs.
The disabled checkbox mark uses its separate disabled foreground.
Checkbox marks use the documented font fallback: E73E for checked and E9AE for indeterminate.
Checked marks have a one-DIP downward offset. Indeterminate marks remain centered.
Menu checks use a standalone glyph, not a filled checkbox square.
Breadcrumbs use separate label and symbol runs, including the E974 separator and the overflow ellipsis.
Their buttons use the subtle appearance. Their existing overflow layout remains unchanged.
Radio dots remain ellipses. Animated mark transitions and overlapping radio-dot stroke states remain outside this pass.

Navigation pane titles use the template's 14-DIP semibold style through `TextStyle::body_strong`.
The pane toggle uses an unframed button. Classic retains its original title and button rendering.
Tab labels use body text rather than caption text.
The native template deliberately dims pressed accent text.
Tests assert that foreground token separately from the normal and hover contrast requirements.

ContentDialog has a content-sized WinUI layout instead of the fixed Classic rectangle.
The dialog keeps the same content, validation label, primary action, and cancel action.
Its title uses 20-DIP semibold text.
The renderer centers the dialog and gives its action footer a separate surface.
Viewport constraints and explicit application sizes still apply.
The dialog centers within the host client area, not the monitor.
Its title wraps to at most two lines.
The retained body scrolls when the title, content, and validation exceed the available height.
The action footer stays outside this scrolling region.
The body wrapper adds no Tab stop and uses an overlay scrollbar.
Classic uses passthrough layout, without a new gutter or scroll offset.
The WinUI scroll offset returns after a Classic style roundtrip.
The default accent primary action corresponds to WinUI's `DefaultButton=Primary` variant.
Applications can override that appearance.

The compact gallery includes a **Control specimens** page for direct visual comparison.
These specimens use default control heights and bounded field widths.
The local visual reference is the installed WinUI 3 Gallery, version 2.9.3.0.
This reference already exposed a mismatch between nominal item height and actual ComboBox popup row spacing.
The pinned item template adds 12 vertical DIPs to its measured presenter, plus four DIPs between rows.
Its default item border is zero.
The live Gallery's observed row size differs from XUI's current DirectWrite result.
This discrepancy remains open rather than becoming an invented border or fixed row height.
Template values and rendered geometry both matter.

Headless coverage checks default dimensions, explicit size ownership, composite geometry, and state retention.
Native coverage checks actual editor identities, selections, focus, dialog placement, and resource ownership across styles and DPI changes.
These checks do not establish pixel parity, material fidelity, animation fidelity, or equal performance.
The native platform boundaries in the first milestone still apply.

### Reference baseline

The template reference is WinUI 3 release `winui3/release/2.4.0`, commit `e8442d07ae57d2d3e653e616831f504937881bd3`.
This pin keeps source facts separate from older documentation images and the installed Gallery version.
XUI uses independent C++ measurement and drawing code, not copied XAML templates.

- [Button defaults](https://github.com/microsoft/microsoft-ui-xaml/blob/e8442d07ae57d2d3e653e616831f504937881bd3/src/controls/dev/CommonStyles/Button_themeresources.xaml) define padding and borders rather than a fixed button height.
- [TextBox defaults](https://github.com/microsoft/microsoft-ui-xaml/blob/e8442d07ae57d2d3e653e616831f504937881bd3/src/controls/dev/CommonStyles/TextBox_themeresources.xaml) distinguish the header, editable region, clear action, and focus border.
- [NumberBox parts](https://github.com/microsoft/microsoft-ui-xaml/blob/e8442d07ae57d2d3e653e616831f504937881bd3/src/controls/dev/NumberBox/NumberBox.xaml) distinguish Hidden, Inline, and Compact spin placement.
- [ComboBox resources](https://github.com/microsoft/microsoft-ui-xaml/blob/e8442d07ae57d2d3e653e616831f504937881bd3/src/controls/dev/ComboBox/ComboBox_themeresources.xaml) define the arrow region, item spacing, and popup surface.
- [Dialog resources](https://github.com/microsoft/microsoft-ui-xaml/blob/e8442d07ae57d2d3e653e616831f504937881bd3/src/controls/dev/CommonStyles/ContentDialog_themeresources.xaml) define bounded size, a two-line title, and measured action columns.
- [Radio resources](https://github.com/microsoft/microsoft-ui-xaml/blob/e8442d07ae57d2d3e653e616831f504937881bd3/src/controls/dev/CommonStyles/RadioButton_themeresources.xaml) define the checked indicator's normal, hover, and pressed sizes.
- [Expander resources](https://github.com/microsoft/microsoft-ui-xaml/blob/e8442d07ae57d2d3e653e616831f504937881bd3/src/controls/dev/Expander/Expander_themeresources.xaml) define the header, disclosure region, and content padding.
- [Font resolution](https://github.com/microsoft/microsoft-ui-xaml/blob/e8442d07ae57d2d3e653e616831f504937881bd3/src/dxaml/xcp/core/text/RichTextServices/xcp/PALFontAndScriptServices.cpp) distinguishes Latin variable-font defaults from language-specific fallback.

## Implementation sequence

### 1. Establish a reference and a reversible experiment

Select a specific WinUI 3 Gallery version and Windows build as the reference.
Record their light, dark, focused, disabled, selected, pressed, and open-popup appearances.
Separate reference behavior from XUI-specific choices, especially compact file rows.

Add an opt-in visual style to the C++ window API.
Keep visual style independent of `ThemeMode`: dark, light, and high contrast are not separate design systems.
The current style remains the default during the experiment.
A runtime selector in the gallery permits comparison without a rebuild.
The experiment implements `WindowOptions::visual_style`, `Window::set_visual_style`, and the gallery selector.

Build one comparison page with buttons, text input, a checkbox, tabs, a list, a navigation item, and a flyout.
Include disabled and keyboard-focused examples, plus a scrollable collection.
Use real XUI controls rather than a static illustration.
Use solid surfaces initially, with no blur and no animation.

Exit condition: the page looks recognizably Windows 11 in both themes, with working input and unchanged resource ownership.
This experiment determines whether the direction is worthwhile before a broad conversion.

### 2. Introduce resolved style tokens

Extend the theme layer with semantic tokens for surfaces, text, strokes, selection, focus, and control states.
Add typography roles, control metrics, corner radii, and density metrics.
Resolve the tokens once per relevant settings change, not through Windows queries in every paint.
High contrast overrides decorative tokens with system colors.

Use small typed structures and direct helper calls.
A stylesheet language, general selector engine, or XAML-like template system is unnecessary for this experiment.
Extract shared control-paint helpers from `application.cpp` as each control changes.
Avoid a separate conditional implementation in every drawing branch.

Represent opacity where a surface requires it.
For native text backgrounds, resolve an opaque color that matches the actual parent surface.
Do not apply one preblended color to different parent surfaces.
Replace unconditional child background fills where they erase the intended content layer.

Color changes invalidate paint.
Font and density changes invalidate measurement, cached text layouts, arrangement, and accessible geometry.
Preserve explicit application sizes rather than silently replacing them with theme defaults.

Primary files: `include\xui\theme.hpp`, `include\xui\application.hpp`, `src\drawing.hpp`, `src\drawing.cpp`, and `src\application.cpp`.

### 3. Restyle primitives and native text

Add separate visual variants for standard, accent, and subtle buttons.
Keep appearance separate from `ButtonBehavior`, which already controls momentary, toggle, repeat, and dropdown behavior.
Define normal, hover, pressed, disabled, checked, and focused combinations explicitly.
Use a distinct keyboard-focus treatment rather than the selection color alone.

Restyle text fields with the appropriate border, focused underline, padding, placeholder, and error treatment.
Keep native EDIT and RichEdit for caret behavior, selection, undo, Unicode, and IME.
Do not replace them with custom text entry merely to obtain a closer border.

The current `Toggle` is a checkbox.
Do not turn it into a ToggleSwitch through paint alone.
A separate switch requires appropriate geometry, interaction, and UIA semantics.
It is an additional control, not a prerequisite for the initial skin.

Apply typography consistently to DirectWrite text, native inputs, document peers, suggestions, and menus.
Font-family substitution alone does not establish matching optical size or baseline alignment.
Check DirectWrite and GDI output together at each supported DPI.
Keep numeric and monospace document roles intact.

Primary files: `src\controls.cpp`, `src\drawing.cpp`, `src\application.cpp`, `src\native_edit.cpp`, `src\native_document.cpp`, and `src\suggestion_peer.cpp`.

### 4. Restyle collections, navigation, and application composition

Apply shared state tokens to file rows, general collections, grids, tabs, navigation items, and scrollbars.
Keep virtual rows virtual.
A rounded selection background needs no additional HWND or retained control per row.
Preserve existing selection models and keyboard actions.

Retain compact density for data-heavy views.
Treat a standard-density mode as a separate, coordinated geometry change.
Do not alter scroll extent through paint-only row-height changes.

Use neutral window chrome behind distinct content surfaces.
Restyle the gallery first, then the explorer, Task Manager, and thumbnail sample.
Keep explorer pane tabs independent and preserve the existing narrow-window collapse behavior.
Changes to navigation layout or pane semantics are outside this visual migration.

Primary files: `src\list_peer.cpp`, `src\drawing.cpp`, `src\application.cpp`, `src\controls.cpp`, `src\data_grid.cpp`, and the sample compositions under `demo`.

### 5. Finish dialogs, menus, and window integration

Give dialogs, tooltips, command surfaces, and suggestion dropdowns consistent corners, spacing, borders, and elevation.
Start with solid flyouts and bounded shadow drawing.
The current command popup already draws a multi-pass shadow.
Do not add an unbounded shadow cache or a render target per popup.

Retain native Shell menus for third-party owner-drawn extensions.
Those surfaces can remain system-styled rather than falsely claiming exact custom-theme coverage.
Restyle XUI-owned commands independently of the Shell fallback.

Extend the existing window host for supported corner and backdrop preferences.
Check caption hit testing, Snap Layouts, system menus, resize borders, inactive captions, and maximize/restore transitions.
Keep documented unsupported-platform fallback behavior visible through diagnostics.
Unexpected graphics errors still use the existing error path.

Add a deliberate system-appearance policy for light/dark mode, accent, transparency, text scale, and animation preferences.
Explicit application choices and system overrides need documented precedence.
High contrast always takes precedence over decorative effects.
Include larger text, long translations, and bidirectional text in the layout review.

### 6. Investigate real materials separately

Mica is not live blur of windows behind the application.
It incorporates the desktop wallpaper and is intended for long-lived application backgrounds.
Acrylic is the more expensive translucent material for transient or overlapping surfaces.
Neither effect belongs on every file row or control.

The documented `DWM_SYSTEMBACKDROP_TYPE` API supports Windows 11 build 22621 and later.
`DWMSBT_MAINWINDOW` selects the long-lived backdrop.
`DWMSBT_TABBEDWINDOW` selects the tabbed-window backdrop.
`DWMSBT_TRANSIENTWINDOW` selects the transient-window backdrop.
These correspond to Mica, Mica Alt, and Desktop Acrylic on Windows 11.
Their documented minimum differs from the minimum for Windows App SDK material controllers.

The current opaque HWND target can cover the system backdrop.
Its child-background fills can also hide layered surfaces.
Therefore, a successful `DwmSetWindowAttribute` call is not proof of visible Mica.

Build a bounded experiment with a backdrop-visible title/navigation region and an opaque content region.
Check native text composition, device loss, inactive appearance, resize, and maximize/restore before wider use.
If the existing presentation path cannot expose the backdrop correctly, evaluate composition-compatible presentation in that experiment only.
Do not replace the renderer globally before the comparison establishes correctness and cost.

Most XUI popups currently draw inside the root client area.
A transient-window backdrop does not automatically blur those client-drawn rectangles.
Real acrylic can require a separate popup window or an in-app composition effect, with additional focus, clipping, and resource work.
Keep solid flyouts until that work has a demonstrated benefit.

Unsupported systems, disabled transparency, high contrast, and power constraints require an explicit solid fallback.
Check remote sessions and reduced-capability graphics paths.
A fallback is a supported appearance, but it must not be labeled as working Mica or acrylic.

### 7. Add bounded motion and complete API coverage

After static rendering passes, add short transitions for hover, press, selection indicators, and popup appearance.
Use one window-level scheduler with work only while a transition is active.
Stop transitions for hidden or destroyed controls and settle immediately when animations are disabled.
There is no permanent animation loop for an idle gallery or explorer.
Avoid per-row timers, layout animation across large collections, and continuous blur recomputation.

Expose accepted style settings through the versioned C ABI and the C# and Rust wrappers.
Do not renumber `ThemeMode`, repurpose reserved ABI fields, or enlarge existing ABI structures without version negotiation.
Keep the C++ experiment isolated until its API is stable.
Existing binding clients must retain the current default and continue to work without source changes.

## Performance and acceptance gates

The cost of geometry and solid-color changes is expected to be modest.
That expectation is not a benchmark.
Text formats, composition surfaces, blur, invalidation scope, and graphics-driver allocations can dominate the result.
The existing [memory report](windows-gui-memory.md) documents resize retention and driver-sensitive allocation thresholds.
A small-window screenshot cannot establish performance equivalence.

Use matched baseline and candidate builds at the same source revision except for the skin.
Keep architecture, client pixel size, DPI, data, visible content, and sample behavior equal.
Measure the explorer, gallery, and Task Manager separately.
Include fixed-size runs and repeated grow/shrink cycles, followed by idle observation.
Do not use working-set trimming.

Proposed initial gates for the solid-surface skin, subject to baseline noise:

- No additional steady-state render targets or native peers for equivalent visible controls.
- No recurring paint or timer activity caused by the skin after a static window settles.
- No unbounded growth in text layouts, native bitmaps, or style resources across theme switches and popup cycles.
- No more than 5% regression in median or p95 input-and-paint latency across repeated matched runs.
- No more than 5% growth in warm private commit at each matched fixed size.
- No additional retained-growth trend after repeated resize cycles.

Investigate failed gates before acceptance rather than changing the workload to obtain a better result.
Record private commit, private working set, total working set, CPU time, and resource counters separately.
Extend the measurement output for p95 if the current scripts do not report it.
Record GPU/compositor cost separately for materials, because application memory counters do not include all system costs.
Report startup and first-paint cost as well as steady state.

Use `tests\measure-window-memory.ps1`, `tests\measure-resize-memory.ps1`, and `xui_window_memory_probe` as the initial measurement tools.
The memory report contains the existing reproduction procedure.
The proposed thresholds are acceptance targets, not claims that the candidate already meets them.

## Regression coverage

Add unit tests for token resolution, state precedence, theme fallback, and style-driven measurement.
Cover combinations such as selected plus hovered, pressed plus focused, and disabled plus checked.
Keep existing control behavior tests unchanged unless the requirement deliberately changes.

Use the existing control, foundation, collections, navigation, grid, explorer, and ABI tests.
Run `xui_palette_smoke`, `xui_gallery_smoke`, `xui_explorer_smoke`, and `xui_native_integration_tests` after host integration.
Use `xui_flicker_tests` to detect missing native text during presentation.
Use `xui_split_window_tests` to retain resource-ownership coverage.
Desktop tests require `XUI_DESKTOP_TESTS=ON` and an appropriate interactive test environment.

Capture the actual application at 96, 144, and 192 DPI in light, dark, and high contrast.
Include disabled controls, keyboard focus, open menus, native text selection, and narrow windows.
Compare these captures with the pinned WinUI reference and retain intentional differences.
Use human visual review for typography and material appearance rather than unstable full-screen pixel equality.

Check Narrator, UIA names and patterns, focus return, tab order, IME composition, and mixed-monitor transitions.
Material acceptance additionally requires transparency-off, inactive, snapped, maximized, and unsupported-platform cases.
No visual improvement justifies weaker editing or accessibility.

## First implementation milestone

The recommended first change is the opt-in, solid-surface gallery comparison from step 1.
It includes resolved tokens, button variants, a text-field treatment, and consistent row/focus states.
It leaves Mica, acrylic, animation, ABI expansion, and explorer restructuring out of the first milestone.
This is a useful visual experiment without an early dependency on the highest-risk graphics work.

## Design references

Microsoft documentation consulted on September 14, 2026:

- [Windows geometry](https://learn.microsoft.com/en-us/windows/apps/design/signature-experiences/geometry): corner hierarchy and shared edges.
- [Windows typography](https://learn.microsoft.com/en-us/windows/apps/design/signature-experiences/typography): Segoe UI Variable and optical sizing.
- [Segoe Fluent Icons](https://learn.microsoft.com/en-us/windows/apps/design/iconography/segoe-fluent-icons-font): system glyphs and missing-font considerations.
- [Mica](https://learn.microsoft.com/en-us/windows/apps/design/style/mica): background layering, intended use, and fallback conditions.
- [Acrylic](https://learn.microsoft.com/en-us/windows/apps/design/style/acrylic): transient surfaces and in-app versus background acrylic.
- [DWM backdrop types](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/ne-dwmapi-dwm_systembackdrop_type): native backdrop choices and minimum Windows build.
