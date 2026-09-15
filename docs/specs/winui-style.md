# WinUI-style appearance

The WinUI style is optional. Classic remains the default.
See [CONTRIBUTING](../../CONTRIBUTING.md#gallery) for the gallery commands.

## WinUI-style gallery experiment

`xui_winui_gallery` is an opt-in, solid-surface experiment.
It uses the existing Win32, Direct2D, and native text-input backend.
The gallery has live Classic/WinUI and theme controls for comparison.
Standard, accent, and subtle buttons retain the same interaction behavior.
The skin also covers radio groups, choice lists, combo boxes, numeric inputs, expanders, sliders, and progress indicators.
Grids, collections, scrollbars, split views, charts, status messages, dialogs, and tooltips use the same style.
Native documents and password inputs have themed frames.
Image, vector, map, and runtime-host frames retain their authored content colors.
Existing applications still use the classic appearance by default.

The complete catalog has a live `Style: WinUI` / `Style: Classic` button.
The style switch preserves the current page, control values, and native text.
The compact experiment remains the default for `xui_winui_gallery`.
Its **Control specimens** page shows default-size controls without gallery-specific height overrides.
This page includes enabled and disabled fields, buttons, choices, an expander, and a content dialog.

The WinUI presentation now changes measurement and part layout, not only paint.
Style changes preserve control identity and values, but can change their bounds.
Explicit preferred and fixed sizes remain application constraints.
WinUI buttons and single-line fields default to 32 DIPs.
Input headers use 14-DIP text, a 20-DIP line box, and an eight-DIP gap.
Numeric and editable combo inputs share one field frame instead of separate nested frames.
Numeric fields select the value on first focus, but later clicks place the caret normally.
Numeric inputs retain inline spin buttons by default.
`set_spin_placement(NumberSpinPlacement::hidden)` selects the WinUI-default hidden variant.
Expanders measure their content instead of retaining an empty Classic-sized body.
WinUI content dialogs use centered, content-sized layouts with separate action footers.
Long dialog bodies scroll without moving the footer.
Titles wrap to at most two lines.
Noneditable combo popups align the selected row over the closed field, within viewport limits.
Plain text fields show a clear action when focused and nonempty.
This action preserves native undo and adds no Tab stop.
Field fills use the reference state's alpha color over the actual parent surface.
Native EDIT and Direct2D use the same resolved fill.

The C++ API separates the visual style from the color theme:

```cpp
xui::WindowOptions options;
options.visual_style = xui::VisualStyle::winui;
xui::Window window(options);
auto save = std::make_shared<xui::Button>(L"Save");
save->set_appearance(xui::ButtonAppearance::accent);
// A live comparison preserves the controls, native inputs, and render target.
window.set_visual_style(xui::VisualStyle::classic);
```

Button appearances apply in the WinUI style.
High contrast uses system colors instead of the experimental RGB tokens.
Latin UI locales use Segoe UI Variable with automatic optical sizing when the installed DirectWrite fonts support it.
Native EDIT uses the corresponding Segoe UI Variable Text face.
Classic and unsupported font configurations retain Segoe UI.
WinUI command icons use the installed Segoe Fluent Icons font, including navigation, breadcrumbs, captions, disclosure arrows, search, and field actions.
Checkbox marks, menu checks, grid indicators, tab close buttons, status symbols, and generic file placeholders use the same font.
Classic retains its vector icons and existing caption font. Shell icons and thumbnails retain their original content.
If Segoe Fluent Icons is absent, WinUI uses Segoe MDL2 Assets and reports this fallback in the debugger.
Missing symbol fonts or required glyphs produce an explicit error, not missing-character boxes. XUI does not redistribute fonts.
Buttons and selection indicators use translucent template brushes, including separate disabled and pressed states.
Dark accent text and selected marks use black. Radio dots remain geometry, not font characters.
Navigation uses a 14-DIP semibold pane title and an unframed menu button.
Catalog-wide paint coverage does not establish pixel or behavior parity with WinUI.
It does not implement Mica, acrylic, animation, automatic system accent selection, or WinUI API compatibility.
Indeterminate progress uses a static segment, not an animation timer.
Date/time controls, native suggestion lists, native editor scrollbars, disabled RichEdit backgrounds, and third-party Shell menus retain platform-owned visuals.
The C ABI exposes window style selection through `xui_window_visual_style_set` and `xui_window_visual_style_get` in `xui_layout.h`.
The .NET binding accepts `visualStyle: VisualStyle.WinUI` in the `Window` constructor.
`Window.SetVisualStyle` changes the style, and `Window.Style` returns the selected style.
These additions preserve the C ABI layouts and the classic default.
The Rust wrapper does not expose style selection.
The [design plan](winui-design-plan.md) describes the remaining stages and measurement requirements.
