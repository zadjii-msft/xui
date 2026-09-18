# WinUI-style appearance

The WinUI style is optional. Classic remains the default.
See [CONTRIBUTING](../../CONTRIBUTING.md#gallery) for the gallery commands.

## WinUI-style gallery experiment

`xui_winui_gallery` is an opt-in, solid-surface experiment.
It uses the existing Win32, Direct2D, and native text-input backend.
The gallery has live Classic/WinUI and theme controls for comparison.
Standard, accent, and subtle buttons retain the same interaction behavior.
The skin also covers radio groups, choice lists, combo boxes, numeric inputs, expanders, sliders, and progress indicators.
`ToggleSwitch` adds a switch presentation without changing the existing Toggle checkbox.
Ordinary WinUI switches use neutral off-state strokes, accent on-state fills, and translucent disabled brushes.
The default thumb is 12 DIPs across, grows to 14 DIPs on hover, and stretches to 17 by 14 DIPs during press.
Disabled switches retain the idle thumb size. Authored track sizes scale the thumb proportionally.
Natural-width WinUI switches use a twelve-DIP label gap, no implicit trailing padding, and a forty-DIP minimum height.
Explicit padding and sizing retain their authored dimensions.
Explicit indicator and mark styles remain application overrides.
`ToggleButton` selects Button toggle behavior by default.
Default checked WinUI buttons retain the accent-disabled fill and foreground when disabled, without an elevation border.
Disabled states ignore hover and press but preserve the checked appearance.
`ProgressRing` shares the Progress model and defaults to indeterminate state.
Grids, collections, scrollbars, split views, charts, status messages, dialogs, and tooltips use the same style.
Native documents and password inputs have themed frames.
Image, vector, map, and runtime-host frames retain their authored content colors.
Existing applications still use the classic appearance by default.

The complete catalog has a live `Style: WinUI` / `Style: Classic` button.
The style switch preserves the current page, control values, and native text.
The compact experiment remains the default for `xui_winui_gallery`.
Its **Control specimens** page shows default-size controls without gallery-specific height overrides.
This page includes enabled and disabled fields, buttons, choices, an expander, and a content dialog.
Its **Keyboard focus shapes** row includes square, rounded, and pill buttons.
WinUI button focus outlines follow the resolved button corner radius, including local values, state rules, and inherited part styles.
Ordinary button outlines extend three DIPs outside the control, within ancestor clips.
They use a two-DIP outer stroke and a one-DIP inner stroke with the WinUI focus brushes.
High contrast uses system colors and the default corner radius.
HyperlinkButton uses the same external focus treatment, including authored corner radii.
Checkbox-style Toggle and CheckBox extend focus seven DIPs horizontally and three DIPs vertically, within ancestor clips.
Their focus corners use the resolved root radius and the adjacent focus margins.
ToggleSwitch uses those margins around its track and label area, rather than the complete stretched row.
Its default focus target has a five-DIP vertical inset within the natural forty-DIP control.
Authored label alignment preserves the available content extent on the corresponding axis.
Without indicator or mark overrides, WinUI CheckBox and checkbox-style Toggle share the same state brushes and font marks.
Root-only corner styles preserve those default indicator visuals.
Natural-width WinUI checkboxes reserve their indicator prefix without subtracting extra trailing padding from the label.
Explicit root padding remains an application override.
RadioGroup applies the same external margin around its selected radio row, not the complete group.
The outline follows the resolved item radius, including focused-state rules.
Focus-only radio styles preserve the default unfocused appearance.
WinUI links use plain accent text and subtle hover and pressed fills. Classic links retain their underline.
Caption buttons and native text clear actions retain their separate focus treatments.
In light and dark themes, SelectorBar uses two-DIP external focus margins around the selected item.
Its two-tone strokes follow the resolved item radius and remain subject to ancestor clips.
The selected item has a centered sixteen-DIP marker, clamped to the available width.
The default marker thickness is three DIPs. Authored marker size still controls its thickness.
The root and item backgrounds are transparent, including selected and hovered items.
Authored root and item backgrounds still apply.
Classic and high-contrast SelectorBar outlines retain their existing treatment.
MenuBar headings reserve four-DIP item margins so their external Button focus outline fits inside the bar.
The default forty-DIP bar contains thirty-two-DIP heading faces and eight-DIP gaps between adjacent faces.
Authored root padding remains outside these margins. Classic menu layout remains unchanged.
In light and dark themes, Expander hover and press fills apply to the disclosure button instead of the complete header.
Both default and styled WinUI Expanders use a down chevron when collapsed and an up chevron when expanded.
Expanded headers retain only their upper rounded corners and connect to a body with lower rounded corners.
Header-only and text-only styles preserve the default body fill and border.
Explicit header and content styles retain their own colors, border widths, and outer corner radii.
Expander focus uses the same external three-DIP margin and two-tone strokes as Button focus.
It follows the resolved header radius and retains square lower corners when expanded.
Ancestor viewport clips still bound the outline.
High-contrast Expander focus uses the default radius.
Ordinary Expander headers and bodies use distinct translucent card fills over the parent surface.
In light and dark themes, closed noneditable ComboBox focus uses a separate highlight around the field, not its header.
The highlight extends four DIPs outside the field and uses a two-DIP outer border with a seven-DIP radius.
Its radius follows the native template resource, independently of the authored field radius.
A three-by-sixteen-DIP accent marker appears inside the left edge.
The filled highlight paints beneath the field. The marker paints above it.
Ancestor clips, popup layers, and adaptive overlays still bound this treatment.
Editable ComboBox, Classic, and high-contrast focus retain their separate treatments.
An open or disabled ComboBox does not receive this closed-field highlight.
RangeInput keyboard focus surrounds the content area, with seven-DIP horizontal margins and no vertical outset.
Its native focus target has a four-DIP radius before the focus-margin adjustment, independently of the track and thumb radii.
Root padding and borders inset that target. Ancestor clips still bound the outline.
WinUI range focus is hidden without keyboard modality and when disabled. Classic focus remains unchanged.
This treatment covers ordinary keyboard focus, not WinUI gamepad focus engagement.
In light and dark themes, RangeInput uses the native strong track and solid outer-thumb brushes.
Its value segment and inner thumb share the native accent-state opacity.
Root-only styles preserve the default thumb appearance. Explicit thumb styles retain their authored surface behavior.

WinUI RangeInput places its track and thumb in a leading cross-axis slot instead of centering them across excess width or height.
The default slot is 32 DIPs: a four-DIP track with fourteen DIPs on each side.
Authored track thickness and thumb size can enlarge that slot. Narrower content centers the track and thumb within the available cross-axis extent.
Root insets apply before this placement. Classic layout remains unchanged.

The WinUI track spans the complete content length, independently of thumb travel.
The default thumb uses an eighteen-DIP layout box with a two-DIP painted outset, for a twenty-two-DIP outer surface.
The filled segment ends at the layout box. The thumb paints over that junction.
Ancestor viewports still clip the thumb outset.
An authored thumb size sets the exact surface and layout size without the additional outset.
Painting and pointer mapping use the same travel geometry, with inset clamping for short controls.

Its **Toggles and progress** card includes a ToggleSwitch, ToggleButton, ProgressRing, and Progress bar.
Switch specimens include a rounded focus target and both disabled states.
The **Indeterminate task** switch selects indeterminate state or a determinate value of 60%.
The specimens also include CheckBox, HyperlinkButton, SelectorBar, InfoBadge, and MenuBar examples.
The hyperlink and menu commands have sample callbacks, without browser, file, or clipboard effects.

The WinUI presentation now changes measurement and part layout, not only paint.
Style changes preserve control identity and values, but can change their bounds.
Explicit preferred and fixed sizes remain application constraints.
WinUI buttons and single-line fields default to 32 DIPs.
Default button content uses `11,5,11,6` padding plus a one-DIP border.
Styled and unstyled measurement reserve the same space. Natural-width hyperlinks do not lose text space to a duplicate border allowance.
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
Authored text, document, password, numeric, and editable combo fields use their surface radius for the existing two-tone focus outline.
Square corners remain square, and oversized radii use the same clamp as the field surface.
This applies to light and dark themes. Unstyled accent underlines, Classic, and high contrast remain unchanged.

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
In light and dark themes, NavigationView rows use two-tone keyboard focus around the full row bounds.
The outline extends two DIPs from the inset face and follows its resolved corner radius.
Classic and high-contrast navigation outlines retain their existing treatment.
Catalog-wide paint coverage does not establish pixel or behavior parity with WinUI.
It does not implement Mica, acrylic, general control transitions, automatic system accent selection, or WinUI API compatibility.
Indeterminate bars and rings use a window-owned animation timer while attached, visible, and effectively enabled.
Hidden or minimized windows stop the timer.
The system client-area animation preference suppresses motion.
Unknown states and capacity meters remain static.
The [progress contract](foundation-controls.md#progress-presentations-and-animation) defines lifecycle limits.
Date/time controls, native suggestion lists, native editor scrollbars, disabled RichEdit backgrounds, and third-party Shell menus retain platform-owned visuals.
The C ABI exposes window style selection through `xui_window_visual_style_set` and `xui_window_visual_style_get` in `xui_layout.h`.
The .NET binding accepts `visualStyle: VisualStyle.WinUI` in the `Window` constructor.
`Window.SetVisualStyle` changes the style, and `Window.Style` returns the selected style.
These additions preserve the C ABI layouts and the classic default.
The Rust wrapper does not expose style selection.
The [design plan](winui-design-plan.md) describes the remaining stages and measurement requirements.
