# WinUI-style experiment: maintainer handoff

Status date: September 15, 2026.

This handoff records an earlier development checkout.
Its branch, uncommitted-state warning, and local results are historical, not the state of the current checkout.
Use [CONTRIBUTING](../../CONTRIBUTING.md) for current build commands.

## September 17 overnight parity pass: button focus corners

Starting revision: `1d6cda4`.
Build directory: `build\winui-fidelity`, ARM64 Release, Visual Studio 18 Insiders, Windows SDK 10.0.26100.0.
The active session has recurring passes through September 18 at 04:00 UTC-05:00.
That schedule is not evidence of completed comparisons.

`Drawing::styled_button` previously passed the default four-DIP radius to `Drawing::focus_ring` for every WinUI button.
Authored square and pill faces therefore received unrelated focus corners.
The focus radius now uses the resolved root style and the same half-size clamp as the face.
This includes local Button values, focused state rules, and inherited part defaults.
Classic focus geometry remains unchanged.
The initial pass retained inset high-contrast focus. The external-focus pass described next supersedes that result.

The compact gallery now includes square, rounded, and pill specimens.
The new `xui_button_focus_pixels` check failed against the original renderer and passed after the correction.
It compares actual Direct2D pixels at 96, 144, and 192 DPI in light, dark, and high contrast.
It also covers unchanged defaults, Classic, state rules, inherited defaults, and radii larger than the control.
The gallery and test executable compiled without new warnings after the correction.
The focus, WinUI style, switch/ring, and additional-control pixel checks all passed.

Live inspection opened both this checkout's gallery and the installed WinUI 3 Gallery.
The installed gallery's home page reported Windows App SDK 2.5, unlike the older baseline recorded later in this document.
The post-click XUI screenshot showed the new specimens in dark mode.
Several screenshot requests returned an image-limit notice instead of usable images.
This record does not treat those requests as native visual evidence.
A later post-click native screenshot showed the dark Button page and the focus outline around its Source button.
That outline extends outside the button, unlike XUI's inset outline at the start of this pass.
Background keyboard actions did not reliably focus the requested specimens.
Later Tab actions returned `requires_escalation` because foreground actions were not allowed.
This pass does not claim a live, focused, before/after comparison of the repaired buttons.

Reference evidence:

- The pinned `src/controls/dev/CommonStyles/Button_themeresources.xaml` sets `FocusVisualMargin` to `-3`.
- `CFocusRectManager::GetFocusOptionsForElement` derives the focus radius from the target and adjusts it for the margin.
- `DependencyProperty.cpp` defines a two-DIP primary border and a one-DIP secondary border.
- The latter two files came from `microsoft/microsoft-ui-xaml` commit `4eabc71e72bbf11039604cd37f475ace0ff4fc02`.
  They are not present at the same paths in the older pinned commit.

### External focus and the window adapter

The second pass found a separate legacy Button paint path in `Window::Impl::paint_control`.
The initial `Drawing::styled_button` correction did not reach that path.
The gallery's authored specimens use those legacy values.

`Drawing::winui_focus_ring` applies the three-DIP external margin and separate WinUI focus brushes.
The outer stroke is two DIPs. The inner stroke is one DIP.
Square corners remain square. Rounded corners use the resolved face radius plus the stroke-center offset.
High contrast uses the default radius and system colors.
`Window::Impl::paint` paints ordinary button focus after sibling bodies, without the button's own clip.
Ancestor viewport clips and rounded host clips still apply.
Root content, popup content, and adaptive overlays use this pass.
Native text pixels still compose after custom control pixels.

On September 17, the ARM64 Release gallery and both relevant test targets compiled successfully.
The five targeted suites passed: `xui_button_focus_pixels`, `xui_button_focus_window_tests`,
`xui_winui_style_tests`, `xui_switch_ring_pixels`, and `xui_next_controls_pixels`.
Software checks cover brush alpha, both outer DIPs, the inner DIP, the external extent, and unchanged interiors.
The nonactivating window fixture covers legacy, named, and unstyled buttons in light, dark, and high contrast.
It checks scroll clipping, outline removal, and unchanged native editor peers, text, and selection.
This fixture sets model focus after a queued keyboard message. It does not establish live keyboard navigation.
Disabling the external window pass made this fixture fail at the first outside-border assertion.
Restoring the pass restored the expected result.

The rebuilt gallery opened successfully. A fresh screenshot showed the dark Controls page and all three authored shapes.
The specimen buttons did not have keyboard focus in that screenshot.
The parallel native screenshot request returned the image-limit notice, not comparison evidence.

Remaining work:

- Live external-focus comparisons still need permitted keyboard delivery and usable images.
- Captions and native text clear actions retain separate focus treatments.
- Other authored controls still need a control-specific focus-target and corner review.
- A successful UIA invocation or hidden-target pixel check does not establish visible keyboard focus.

### HyperlinkButton text, surfaces, and focus

The installed gallery's dark HyperlinkButton page showed plain accent text without an underline.
The pinned `HyperlinkButton_themeresources.xaml` confirms a ContentPresenter without text decoration, subtle interaction fills, and `FocusVisualMargin="-3"`.
XUI previously drew an underline in both styles and omitted the WinUI hover fill.
It also painted hyperlink focus inside the control, independently of the new button pass.

WinUI hyperlinks now omit the underline and reuse the subtle Button fill resources.
Disabled ink retains the WinUI resource alpha over the actual surface.
Pressed text remains accent-colored instead of becoming neutral gray.
The current fixed accent palette still differs from native system accent shade resources.
Classic retains its underline and existing paint behavior.
Hyperlinks now join the external Button focus pass, including authored rounded corners and ancestor clipping.
The compact gallery includes natural-width, rounded, and disabled hyperlink specimens.
The callbacks still report local status without browser navigation.

The new software state comparison failed before the correction.
After the correction, `xui_next_controls_pixels` and `xui_button_focus_window_tests` passed on ARM64 Release.
The state comparison covers three DPI values, light, dark, high contrast, and enabled, disabled, hover, and pressed paint.
The window fixture also covers a rounded hyperlink.
These checks do not establish live focused-state parity.

### Natural-width text clipping

The new natural-width specimens exposed truncated hyperlink labels in the actual gallery.
The default measurement already reserved 24 horizontal DIPs and 13 vertical DIPs for button content chrome.
The styled content path then used those totals as padding and added another one-DIP border.
That duplicate allowance reduced both content dimensions by two DIPs.

The pinned Button template defines `ButtonPadding` as `11,5,11,6` and `ButtonBorderThemeThickness` as `1`.
`default_button_padding` now supplies those padding values to Button content layout and both styled measurement paths.
Default outer sizes remain unchanged.
Explicit padding and border values still replace their corresponding defaults. Classic retains its original padding.
The new `xui_winui_style_tests` assertions failed before the correction and passed afterward.
They cover exact content bounds, natural-width hyperlinks, rounded named styles, and explicit border removal.
The style, basic-style, hyperlink-pixel, focus-pixel, and focus-window checks also passed.
The rebuilt gallery's light-theme screenshot showed all three hyperlink labels without ellipses.

### Checkbox focus margins and corners

The pinned CheckBox and ToggleSwitch templates use horizontal focus margins of `-7` and vertical margins of `-3`.
Native `AdjustCornerRadius` averages the two adjacent margin values for each corner.
Checkbox-style Toggle and CheckBox now use these margins and the resolved root radius.
The window renderer paints their focus after sibling bodies, within ancestor clips.
Classic checkbox focus remains unchanged.

The new software regression failed against the previous inset rendering.
It now passes at 96, 144, and 192 DPI in light, dark, and high contrast.
The window fixture includes binary Toggle, CheckBox, and a rounded CheckBox.
It checks the wider margin, its outer limit, and outline removal.
The expanded fixture initially exceeded its unchanged 60-second timeout.
Each capture creates a new graphics capture session and device.
The fixture now reuses one unfocused baseline per theme instead of recapturing that unchanged state before every control.
All assertions remain intact.
Two subsequent adapter runs passed in 4.63 and 5.20 seconds.
The button, additional-control, and switch pixel suites also passed twice.
These results establish renderer coverage, not live keyboard navigation or complete native parity.

### Inner focus targets still need comparison

ToggleSwitch targets `SwitchAreaGrid`, not the complete root. That grid has a five-DIP vertical margin.
The current XUI switch model has a different content layout, so a Button-style outline is not a safe direct substitution.
The checkbox correction does not change switch focus.

### CheckBox defaults and the binary Toggle renderer

Source inspection found another split paint path.
Unstyled binary Toggle used `Drawing::check_indicator`, but CheckBox always used the generic styled indicator and vector marks.
A root-only corner style also selected that generic path.
Those paths differed in disabled alpha, interaction brushes, glyphs, and horizontal indicator placement.

WinUI checkbox-style controls now reuse `check_indicator` when the application does not supply indicator or mark styles.
Root-only radius styles retain the same default indicator.
Explicit indicator and mark styles, Classic, and ToggleSwitch retain their previous paint paths.
The compact gallery includes unchecked, checked, and rounded-root CheckBox specimens beside the mixed specimen.
The new software comparison failed before the correction and passed afterward.
It covers three states, enabled and disabled controls, normal/hover/pressed interaction, three DPI values, and all three themes.
It compares the default and rounded-root paths against the existing WinUI indicator renderer.
This is a consistency regression, not an independent proof of complete native fidelity.

A live dark-gallery image showed the mixed CheckBox and hyperlink specimens before this correction.
The new checkbox-focus binary was present, but the specimen did not have keyboard focus.
Other screenshot requests returned image-limit notices.

### Natural-width checkbox labels

The rebuilt light gallery showed truncated labels in all three new checkbox specimens.
`Control::measure` reserved the correct 28-DIP WinUI prefix.
`Toggle::layout_metrics` still subtracted 12 DIPs of Classic trailing padding from the label.
The pinned CheckBox template uses `CheckBoxPadding="8,5,0,0"` on its content presenter, with no trailing padding.

WinUI checkbox-style controls now omit that default trailing padding.
Default outer sizes remain unchanged.
Explicit padding, Classic padding, and ToggleSwitch padding retain their previous behavior.
The model regression failed before the correction and passed afterward.
It covers Toggle and CheckBox with default, rounded-root, Classic, and explicit-padding layouts.
The eight targeted model, style, switch, checkbox, and focus suites passed after the correction.
The rebuilt light-gallery screenshot showed complete Unchecked, Checked, and Rounded labels without ellipses.
Checked and rounded-root specimens also showed the same font checkmark.
The authored Toggle software and native adapter checks passed again.
This does not change XUI's natural-width policy to the native template's 120-DIP minimum width.

### Radio row focus and focus-only styles

The installed Gallery's RadioButton page showed separate radio rows in vertical and horizontal groups.
The pinned RadioButton template defines `FocusVisualMargin="-7,-3,-7,-3"` and uses the control corner radius.
XUI previously painted an inset outline and ignored the resolved item radius.

RadioGroup now uses the external focus pass around the selected row.
The outline uses the item's resolved corner radius, including focused-state rules.
Selection movement and blur remove the previous outline.
Choice-list and SelectorBar focus remain unchanged.

The new window regression failed against the inset renderer and passed after the correction.
A second regression exposed another discrepancy: a focus-only item style changed the unfocused group fill, indicator, and text placement.
Styled WinUI radio groups now retain transparent default roots and reuse the native-style radio indicator when indicator and mark overrides are absent.
Their default item geometry matches the unstyled WinUI path.
Explicit indicator and mark overrides retain the custom paint path. Classic retains its previous defaults.
The window fixture compares every unfocused pixel between default and focus-only-style groups in light, dark, and high contrast.
The compact gallery includes adjacent default and rounded-focus radio groups.
Live focused-state comparison still needs reliable keyboard delivery. The nonactivating fixture sets model focus.
The foundation, WinUI form-layout, choice-style model, choice-style window, and external-focus window suites passed after the changes.
The rebuilt gallery exposed both radio groups with matching row bounds and selected values through accessibility.
An anchored Tab action did not change its reported focus from the title-bar tabs.

### Expander disclosure interaction

The installed Gallery's Expander page showed separate collapsed and expanded specimens with down and up chevrons.
The pinned `Expander_themeresources.xaml` keeps the header background constant across normal, hover, and pressed states.
Only its 32-DIP disclosure button receives subtle hover and pressed fills.
Its disclosure border uses the four-DIP control radius, not a circular button.

XUI previously changed the complete header fill on hover and used the selection fill on press.
Light and dark themes now keep the base header fill and apply the subtle fill to the disclosure bounds.
Explicit header fill overrides remain authoritative. High-contrast interaction fills remain unchanged.
The styled path also used right/down disclosure glyphs instead of down/up.
Both WinUI paths now share the same disclosure painter and direction.
Classic glyph behavior remains unchanged.

The header-fill regression failed against the old renderer and passed after the correction.
The nonactivating window fixture covers default and rounded headers, interaction cleanup, and equal glyph pixels in collapsed and expanded states.
The compact gallery now includes a collapsed rounded-header specimen.
Expander focus margins and asymmetric expanded-header corners remain separate investigation targets.

The gallery, layout model, layout window, and focused renderer checks passed after the disclosure correction.
Accessibility inspection confirmed that the new rounded specimen expanded and exposed its body text.
The initial screenshot requests returned image-limit notices, not new comparison images.

### Continuous rounded surface borders

`Drawing::styled_surface` painted every border through four narrow edge clips.
Equal-width borders therefore lost the curved segments between adjacent edge strips.
Source inspection of the rounded Expander paint path identified this defect.
Styled Buttons already used a continuous inset stroke for equal-width borders.

Both paths now share that stroke helper.
The renderer preserves the documented edge-region behavior for unequal borders.
The new software regression failed before the correction.
It compares full Direct2D output against an independent inset stroke at three DPI values.
Cases include square, rounded, pill, and oversized radii, zero and thick borders, transparent surfaces, and all three themes.
After the correction, all seven targeted software and desktop suites passed.
These include the complete styling fixture, layout and choice fixtures, and external-focus fixture.
The rebuilt gallery's light-theme image showed a continuous rounded-header border and up chevrons on both expanded specimens.
That image also showed a separate styled Expander defect: its expanded header retained bottom corners and its body lacked the default frame.
The default Expander beside it retained the connected header and body frame.
The next pass corrected that state-dependent surface mismatch, as described next.

### Connected Expander surfaces

The styled Expander branch omitted the default content surface and rounded all four header corners even when expanded.
WinUI header painting now uses one path for styled and default controls.
Expanded headers keep their top corners. Content surfaces keep their bottom corners.
Collapsed headers keep all four corners.
Header-only and text-only styles no longer remove the default body frame.
Authored header and content colors, border widths, and outer radii remain authoritative outside high contrast.
Classic retains its existing paths.

`Drawing::styled_surface` accepts an internal corner selection for these connected surfaces.
The renderer paints disjoint curved and square halves through clips.
This keeps the existing allocation-free surface path.
The new nonactivating `xui_expander_surface_window_tests` failed against the previous renderer.
After the correction, it passed with the layout, focus, and control-pixel fixtures.
It compares default and text-only surfaces, expanded corners, body defaults, explicit body styling, and retained peers across three themes.
The separate fixture avoids more captures in the existing external-focus fixture.

The native Gallery's light sample also showed a lighter header than body.
Pinned Expander resources map these fills to the default and secondary card brushes.
XUI now uses their alpha values over the actual parent surface instead of one opaque `palette.surface` fill.
The pinned card stroke also replaces the generic border color in ordinary themes.
The new owned-window comparison failed before this brush correction and passed afterward.
It uses an authored blue parent to expose incorrect opaque fills.
Its blended-color samples allow one channel value of hardware raster rounding. Authored opaque colors still require exact matches.

A translucent software check exposed double blending at the first square-join implementation.
Disjoint half clips corrected that defect and preserved fractional-DPI coverage.
The software comparison now covers translucent borders, transparent fills, both join directions, and three DPI values.
High contrast retains system brushes rather than the new ARGB resources.

The external-focus fixture later exceeded its 60-second limit during its third theme.
It now reuses one graphics device for the fixture, but each capture still creates and closes a fresh capture session.
The owned-window guard, frame wait bound, assertions, and CTest timeout remain unchanged.
The focus fixture then passed three consecutive runs in 23.82, 54.17, and 29.65 seconds.
The surface and additional-control pixel fixtures also passed three consecutive runs.
Capture timing remains variable. The 54.17-second result leaves limited timeout headroom.

### ToggleSwitch state brushes

The native dark ToggleSwitch page showed a neutral off-state outline and a black on-state thumb.
XUI used an accent outline for the off state and opaque generic fills and disabled ink.
Ordinary WinUI switches now use the template's off fills, neutral strokes, accent state fills, and alpha-correct thumb and text brushes.
The on state has no default track stroke.
Explicit indicator and mark styles still replace their corresponding values.
Classic and high contrast retain their previous brush paths.

The new software regression failed before the correction and passed afterward.
It compares all pixels for on/off, enabled/disabled, and idle/hover/pressed combinations at three DPI values.
The switch, additional-control, and focus pixel suites passed.
Authored Toggle software and native checks also passed.
The compact gallery includes off, disabled-off, and disabled-on specimens.
The rebuilt dark-gallery image showed a neutral off outline, black on thumbs, and distinct disabled-on and disabled-off states.
The new specimens exposed their expected enabled and checked values through accessibility.
At that point, switch focus geometry, native thumb dimensions, and the separate header/content model remained unresolved.

### ToggleSwitch thumb geometry

The September 17 pass at 20:27 UTC-05:00 examined the native thumb dimensions.
The pinned `CommonStyles/ToggleSwitch_themeresources.xaml` specifies 12 by 12 DIPs for idle and disabled thumbs.
Hover uses 14 by 14 DIPs. Press uses 17 by 14 DIPs.
Its 20-DIP knob host and asymmetric margins place the default centers half a DIP left of each track-end center.
The later 21:10 source review also found pressed-state alignment setters outside the size animations.
These setters anchor the stretched thumb three DIPs from the outer track edge rather than at its idle center.
The model and independent pixel reference now include those pressed-state offsets.
XUI previously used circular thumbs of 14, 15, and 16 DIPs for idle, hover, and press.

`Toggle::mark_bounds` now matches these WinUI dimensions and offsets.
Authored track sizes scale this geometry. Authored borders and constrained layout still bound the available thumb area.
An enabled-aware overload preserves the original overload and lets the renderer suppress growth under disabled ancestors.
Classic retains its previous thumb geometry. Color overrides and high-contrast brush selection remain unchanged.
The foundation regression failed against the old idle size and passed after the correction.
The independent software reference now uses explicit template dimensions instead of calling `mark_bounds`.
It covers on/off, enabled/disabled, all three interaction states, both ordinary themes, and three DPI values.
The foundation model, foundation window, switch pixel, focus pixel, and authored Toggle checks passed.
The rebuilt gallery also opened and exposed the expected specimen states.

A returned dark XUI screenshot showed the smaller idle thumbs and the off, disabled-off, and disabled-on specimens.
Native screenshot requests during this pass returned image-limit notices, including requests after theme and switch changes.
Those notices do not establish a fresh native visual comparison.
The native template and earlier returned native images support this correction, but live hover and pressed-state comparison remains incomplete.
An anchored native Tab action reported success without target-focus evidence.
A subsequent Tab action returned `requires_escalation`. No foreground workaround followed.
Switch focus geometry, the separate header/content model, and precise native raster rounding remain unresolved.

### Expander focus corner shape

The next September 17 pass started at 20:44 UTC-05:00.
`Window::Impl::paint_control` still passed a fixed four-DIP radius to Expander focus.
Rounded headers therefore received focus pixels outside their curved faces.
Expanded headers also received rounded lower focus corners at their square body joins.
The pinned `Expander.xaml` binds the header radius to the control and filters it to the top corners in the expanded-down state.

Expander focus now uses the resolved header radius with the same half-size clamp as the header.
Expanded focus keeps only its upper rounded corners.
High contrast ignores authored radii. Classic retains its previous focus path.
The existing focus margin, brushes, and stroke widths remain unchanged.
This correction does not establish exact native Expander focus placement.
The interaction between explicit header styles and native default focus margins still needs direct evidence.

The nonactivating surface fixture now enters through a queued keyboard message and uses a fixture-scoped capture device.
Its first attempt drained that message through a manual pump and did not establish keyboard modality.
The corrected fixture uses the application's message path and requires visible focus before accepting the corner result.
Against the old renderer, its corner assertion read `0x858d97` where the blue backdrop was `0x315579`.
After the correction, the fixture passed three consecutive runs in 7.96, 7.19, and 6.67 seconds.
It checks collapsed corners, expanded lower joins, focus visibility, surface styles, and retained peers.
Antialiased join samples allow one channel value of hardware rounding, matching the existing composited-surface tolerance.
An exact software comparison covers full, upper-only, and lower-only focus corners at three DPI values and three themes.
The switch, additional-control, and button-focus pixel suites also passed.
The final gallery build succeeded. The external-focus window fixture passed in 31.75 seconds.

The rebuilt XUI gallery showed both expanded specimens with connected header and body surfaces.
A fresh native Gallery image showed its expanded dark Expander with the same connected arrangement.
Neither specimen had keyboard focus in these images.
Focus-corner evidence therefore comes from the native template and regression captures, not a live focused native comparison.

The switch template review also confirmed a 12-DIP content gap and ten-DIP rows before and after the 20-DIP track.
Its inner focus grid has a five-DIP vertical inset, spans the track and content columns, and aligns left within the control.
At that point, XUI used a nine-DIP gap, twelve-DIP trailing padding, and a 32-DIP natural minimum height.
The next pass corrected switch measurement and its stretched-row focus target together.

### ToggleSwitch layout and external focus

The September 17 pass started at 21:17 UTC-05:00 and continued through 21:54.
Default WinUI switches now use a 12-DIP content gap, no implicit root padding, and a 40-DIP natural minimum height.
Explicit application sizes and padding remain authoritative. Classic and CheckBox layout remain unchanged.
The rebuilt XUI "Off" specimen and the native Gallery's simple switch both exposed 72-by-40 bounds.
Returned images showed the XUI state specimens and the native simple switch.
The gallery also includes a rounded switch specimen.

`Drawing::winui_toggle_focus` shares the focus target between direct drawing and the window's external-focus pass.
The target follows the track and label instead of unused space in a stretched row.
It uses retained text metrics, authored label alignment, and the resolved root radius.
The window paints focus outside the switch's peer clip but retains ancestor viewport clips.
The native template's five-DIP vertical margin also applies when larger text determines the content height.
A 24-DIP font case covers that calculation.

The new model and software regressions failed before the correction.
The independent software reference covers three themes, three DPI values, square and rounded corners, padding, alignment, and larger text.
The separate nonactivating desktop fixture checks external strokes, the content-sized right edge, ancestor clipping, and complete baseline restoration.
It also checks retained peers and zero additional text layouts.
The fixture passed repeated runs in 4.80, 4.79, and 5.69 seconds.
The final relink passed the fixture in 5.50 seconds, together with the foundation model and switch pixel suite.
The native foundation and authored Toggle suites also passed during this pass.
The final gallery and desktop fixture include the larger-text refinement.

Sequential anchored Tab actions reached the native Gallery's simple switch without foreground escalation.
A returned dark native image showed its rounded focus outline around both the track and "Off" label.
The outline did not span the sample row.
This is direct native visual evidence, not an inference from a successful key action.
The same XUI attempts moved focus among the toolbar buttons, not the switch.
An accessibility click changed a switch value without establishing keyboard focus on that switch.
No live focused XUI/native pair is claimed. XUI focus evidence remains the owned-window captures and software references.

The combined external-focus desktop fixture passed in 56.74 seconds, close to its unchanged 60-second timeout.
Its runtime remains variable despite capture-device reuse.
Separate selectors for its independent control groups remain a possible fixture improvement, not a completed change.
Native Expander focus-margin precedence, exact border compositing, switch animations, and the separate switch header/content model remain unresolved.

### Focused Expander comparison and fixture isolation

The September 17 continuation started at 21:55 UTC-05:00.
Sequential background-safe Tab actions reached XUI's rounded Button and its default Expander header.
Returned images showed the rounded Button outline and the Expander's old inset outline.
The native Gallery then showed keyboard focus on its collapsed and expanded Expander.
Its outline sat outside the header. Expanded focus retained square lower corners.
The pinned default ToggleButton style supplies `FocusVisualMargin="-3"`.
The live native images resolve the earlier uncertainty about an external margin for this Expander template.

WinUI Expander focus now joins the shared external-focus pass.
Its target remains the header, not the body.
It uses the resolved header radius, native two-tone strokes, and a three-DIP external margin.
Expanded focus retains square lower corners. Classic focus remains unchanged.
The pass retains ancestor clips and the existing popup and adaptive-overlay paint order.
The previous inset focus does not paint beneath the external outline.

The new external-stroke assertion failed against the old renderer.
The corrected capture fixture compares default and rounded headers one focused control at a time.
It checks external upper strokes, square expanded joins, ancestor clipping, visible unclipped strokes, and complete frame restoration after blur.
Clipping checks cover light, dark, and high contrast.
The fixture passed three consecutive runs in 12.82, 11.71, and 14.27 seconds.
Independent software references cover full and partial-corner external outlines at 96, 144, and 192 DPI.
The first implementation clipped an antialiased outer pixel at 144 DPI.
One DIP of extra clip allowance preserves that fringe without changing the stroke geometry.
The software suite then passed without relaxing its color tolerance.
Switch coverage also adds a 48-DIP font case, which exceeds the natural track height.

The formerly combined external-focus CTest entry now selects only buttons and hyperlinks.
Separate serial entries cover choices and Expander interactions.
All three keep the original control composition, three themes, native editor retention checks, and 60-second timeout.
No assertion was removed. The original combined executable selector and full styling suite still run every group.
Each split entry passed three consecutive runs before the Expander focus correction.
Those runs took 3.59 to 12.07 seconds each.
The original combined selector also passed.
The split entries passed again after the Expander correction.

A later computer-use interruption reset XUI traversal to its toolbar.
No foreground workaround followed.
After the final rebuild, a fresh traversal ran without concurrent desktop fixtures.
Returned images showed the corrected default expanded Expander and the rounded Expander in collapsed and expanded states.
Their external outlines followed the header corners. Expanded outlines retained square lower corners at the body join.
These XUI images support a direct visual comparison with the native focused Expander images from this pass.
They do not establish pixel equality across the differently sized specimens.

The same traversal reached the XUI "Off" and rounded switch specimens.
Returned images showed compact focus around each track and label, including the rounded specimen's pill-shaped outline.
The "Off" outline matched the compact target arrangement in the earlier native Gallery image.
This supersedes the earlier limitation that XUI switch focus had only fixture evidence.
The final switch capture fixture also passed three runs in 12.92, 9.91, and 17.33 seconds.
Exact native raster rounding, inner-border background compositing, switch animations, and separate switch header/content support remain unresolved.

### Closed ComboBox focus highlight

The September 17 pass around 22:36-23:08 UTC-05:00 compared the complete XUI gallery with the native ComboBox page.
The old XUI Output format field showed an inset outline without an accent marker.
The native Colors field showed an external rounded highlight and a narrow accent marker.
Background Tab established keyboard modality. The XUI navigation action then focused the page's first specimen.
This route avoids the compact gallery's longer tab sequence.

The reference is `src/controls/dev/ComboBox/ComboBox_themeresources.xaml` at `e8442d07ae57d2d3e653e616831f504937881bd3`.
`HighlightBackground` has margin `-4`, border thickness `2`, and the fixed `ComboBoxHiglightBorderCornerRadius` resource value `7`.
The marker measures three by sixteen DIPs, with a 1.5-DIP radius and a one-DIP left margin.
`ComboBoxHelper.cpp` changes editable popup corners, not the closed highlight radius.
This is a template highlight, not Button's two-tone system focus outline.

Ordinary noneditable ComboBox now uses this background and marker.
The window painter draws the filled highlight before the field and the marker after its content.
The same phases apply within popups and adaptive overlays, including overlays inside popups.
The highlight follows the field bounds and excludes an authored header.
Ancestor clips remain active. Open and disabled controls do not receive the highlight.
Editable fields, Classic, and high contrast retain their existing focus paths.

The new desktop assertion failed against the previous renderer:
`Closed WinUI ComboBox focus extends four DIPs outside its field`.
The corrected renderer passed the main and layer fixtures twice.
Main fixture times were 5.67 and 5.61 seconds. Layer fixture times were 2.47 and 8.44 seconds.

The initial combined fixture exceeded its 60-second limit, so the checks now run as two serial entries.
Fixture development also caught invalid style, ownership, and native-callback usage before the successful runs.
The final fixture uses `header_height`, one retained parent per element, and `Window::post` for content replacement.
The software reference passed at 96, 144, and 192 DPI with independent geometry and brush constants.
Existing Button, choice, Expander, and switch checks also passed after the renderer change.

The final rebuild and focused CTest run passed all thirteen selected checks at 23:08 UTC-05:00.
This run included the foundation, choice-style, and WinUI-style model suites.
The final ComboBox fixtures took 13.06 and 5.61 seconds. The whitespace check passed.
The choice-style build emitted existing C4244 warnings from the integer assignment at `tests/style_choices_tests.cpp:58`.
That assignment predates this pass. No full repository suite ran during this pass.

The rebuilt complete gallery showed the external highlight and accent marker in dark and light themes.
Fresh native images showed the corresponding Colors highlight in both themes.
These images establish visible shape and placement agreement, not exact pixel identity.
The specimens have different widths and parent surfaces. Native system accent and XUI's fixed accent also differ.
Exact native raster rounding, border/background compositing, and pressed animations remain outside this result.
The native editable path was not part of this visual correction.
The adapter checks preserve its draft text, editor identity, and native peers, but do not establish IME or caret parity.

### Slider content focus margins

The September 17 pass after 23:09 UTC-05:00 compared XUI RangeInput with the native Slider page.
The old XUI outline stayed inside its control.
After six background Tab actions, the native simple Slider showed a wider rounded outline around the control, not its thumb.

The reference is `src/controls/dev/CommonStyles/Slider_themeresources.xaml` at `e8442d07ae57d2d3e653e616831f504937881bd3`.
The ordinary `FocusBorder` target uses `ControlCornerRadius`, with `FocusVisualMargin="-7,0,-7,0"` on Slider.
The template has separate thumb targets for focus engagement. Those targets do not define ordinary keyboard focus.
The XUI correction covers ordinary keyboard focus, not gamepad focus engagement.

`Drawing::winui_focus_ring` now accepts a separate vertical outset.
Existing callers retain their three-DIP vertical default.
RangeInput uses seven horizontal DIPs and zero vertical DIPs around its content area.
The target excludes root padding and borders through the existing layout helper.
It retains the native four-DIP target radius before the averaged margin adjustment.
Very short targets skip strokes with nonpositive inner dimensions instead of passing inverted rectangles to Direct2D.

The window adapter now paints RangeInput focus through the shared external-focus pass.
This preserves ancestor clips and the existing popup and adaptive-overlay paths.
The two former WinUI inline outlines are removed. Classic paint remains unchanged.
The shared pass also suppresses the outline without keyboard modality and for disabled controls.

The new assertion failed against the previous renderer:
`WinUI range focus extends seven horizontal DIPs beyond its content`.
The final build and focused CTest run passed all fourteen selected checks at 23:23 UTC-05:00.
The RangeInput adapter took 20.82 seconds.
It covers horizontal, reversed vertical, padded, clipped, disabled, and initially non-keyboard-focused specimens.
Both styles and all three themes retain values, drag state, native peers, and the complete frame after blur.
The software reference passed at 96, 144, and 192 DPI, including very short targets.

The rebuilt gallery opened the Range input page after these checks.
Later background key actions did not produce a stable visible focus outline in the returned images.
Those images do not establish live post-fix focus parity.
The correction therefore has native visual evidence, pinned template evidence, a failing pre-fix regression, and passing adapter pixels.
At that point, a fresh focused XUI comparison remained necessary. No foreground workaround was used.
The native vertical specimen, focus engagement, thumb geometry, and exact raster equivalence remain outside this visual result.

The later brush pass obtained focused XUI images after another gallery launch.
Those images showed the external horizontal outline and then the vertical outline after Tab.
A theme change also showed the focused vertical specimen in light mode.
This supersedes the earlier post-fix capture limitation for XUI focus.
The native horizontal comparison establishes visible placement agreement, not exact pixels or native vertical parity.

### Slider thumb and track brushes

The same September 17 session continued through 23:48 UTC-05:00.
Unfocused comparison showed a darker XUI thumb surround and a brighter track than native WinUI.
The native template uses `ControlSolidFillColorDefaultBrush` for the surround and `ControlStrongFillColorDefaultBrush` for the track.
`Common_themeresources_any.xaml`, at the same pinned revision, defines the ordinary solid fill as white in light mode and `#454545` in dark mode.
The strong track resources retain theme-specific alpha, including a separate disabled value.

XUI now uses those brushes instead of the generic surface and secondary-text colors.
The value segment and inner thumb share the existing native accent-state brush helper.
The default thumb renderer is shared between unstyled controls and controls without thumb-part overrides.
Previously, a root-only style replaced the outer circle and inner dot with one solid accent-colored thumb.
Explicit thumb styles retain their authored surface behavior.
Classic and high-contrast colors retain their existing paths.

The first adapter check failed at `(298,34)`: actual `0x005fb8`, expected `0xffffff`.
That pixel belongs to the styled thumb surround, not its accent center.
The corrected adapter checks solid fills, translucent tracks over window and authored backgrounds, disabled colors, and explicit thumb-style restoration.
Token checks cover enabled, disabled, hover, and pressed brush values.
At 23:48 UTC-05:00, the rebuilt targets passed all fourteen selected checks.
The range adapter passed in 15.72 seconds, followed by repeated passes of 10.69 and 17.23 seconds.

The rebuilt gallery showed the brighter dark thumb surround and the reduced track brightness.
The light image retained the white outer thumb and gray track.
These are qualitative visual comparisons. The native and XUI controls have different widths, values, accents, and parent surfaces.
A native range-value action reported unsuccessful verification and left the sample near `1.1645`, rather than the requested value.
No comparison assumes equal values from that action.

Thumb layout size, outer-border geometry, elevation gradients, pressed/disabled dot sizes, and motion remain unresolved.
The brush correction does not change pointer geometry, numeric values, drag commit/cancel behavior, or focus engagement.
The focused CTest run is not a full repository-suite result.

### SplitButton comparison and contract boundary

The 2026-09-17 late-evening comparison opened Action variants in XUI and SplitButton in the native gallery.
The XUI image showed separate primary and dropdown frames, including rounded corners at their join.
The native focused specimen showed one outline around the complete split control.
The returned XUI image did not establish focus on either child during this pass.

The native reference is `src/controls/dev/SplitButton/SplitButton.xaml` at the same pinned revision.
SplitButton has `IsTabStop="True"` and `FocusVisualMargin="-1"`.
Its two internal buttons have `IsTabStop="False"` and raw accessibility visibility.
Separate border grids provide square interior corners and rounded exterior corners.

XUI's public foundation contract explicitly defines two independent Buttons with separate names, callbacks, and keyboard targets.
`SplitButton::SplitButton` retains those two controls, rather than a single composite focus target.
Replacing both child outlines with one shared outline would hide which action receives keyboard activation.
Removing one Tab stop would change the public input and accessibility contract.
This pass therefore made no SplitButton renderer or keyboard change.
Exact native compound focus requires a separate contract decision, not a blanket Button focus-margin change.

### September 18, 00:18 pass: visual tools unavailable

At 00:18 UTC-05:00, this pass read the current worktree changes and the previous comparison evidence.
The session settings reported the built-in `computer-use` server as disabled.
This pass preserved that setting and did not use another input or capture mechanism.
No application windows received input, and no new XUI or native WinUI images were available.

This pass made no renderer changes and adds no visual-parity claim.
The previous SplitButton contract boundary and the remaining Slider geometry questions still apply.
The worktree whitespace check passed. No new build or test run was necessary for this documentation-only entry.
The recurring automation remained active before the 04:00 UTC-05:00 deadline.
Further direct comparisons require the permitted computer-use tools to be available again.

### September 18 continuation: authored checkbox regression

After the continuation request at 00:20 UTC-05:00, this session enabled the existing built-in computer-use server.
The settings action reported Connected, but its application action tools remained unavailable in that turn.
No alternate desktop input mechanism was used. This does not establish a new visual comparison.

The adjacent model checks found a stale assertion in `xui_control_styling_tests`.
It still expected WinUI checkbox-style Toggle to retain twelve DIPs of default trailing padding.
The earlier natural-width label correction removed that padding, based on actual clipped labels and the native template.
The assertion failed with `Classic and WinUI apply different default gap/left padding`.

The updated regression checks exact measured widths for the same authored twenty-DIP indicator and sixty-DIP label.
Classic measures 116 DIPs. WinUI measures 89 DIPs.
Both label bounds retain all sixty DIPs.
An explicit twelve-DIP trailing padding increases WinUI measurement to 101 DIPs without reducing the label.
This continuation changed the regression, not the renderer or the public contract.

The rebuilt ARM64 Release targets passed six selected model suites:
`xui_control_styling_tests`, `xui_control_style_expansion_tests`, `xui_style_fields_tests`,
`xui_winui_style_tests`, `xui_winui_form_layout_tests`, and `xui_foundation_tests`.
The first build emitted C4244 conversion warnings in the expansion and field test targets.
The final selected run completed in 0.74 seconds.
These results establish model regression coverage, not native visual parity or a full repository-suite result.

### September 18, 00:24-00:32: vertical Slider and numeric input

The computer-use tools became available in the next turn.
This pass opened the existing ARM64 Release gallery in the background and selected WinUI style.
No renderer changes occurred between this build and the previous Slider brush comparison.

Background Tab traversal reached the native vertical Slider and scrolled its complete target into view.
The returned native image showed a rounded outline around the full content target, not only the narrow track or thumb.
The XUI Range input image showed its corresponding external outline around the vertical specimen.
Native accessibility reported a 100-by-100 target. XUI reported a 44-by-160 target.
This extends the previous live focus comparison to the vertical orientation.
Different sizes, values, tick marks, parent surfaces, and screenshot scales prevent a pixel-equality claim.

The native template gives the track the full length of its container.
XUI's `slider_visual` retains a twelve-DIP default inset at both track ends.
Its twenty-DIP thumb also differs from the native eighteen-DIP layout box and its external two-DIP border margin.
These geometry questions remain unresolved. This pass did not change pointer mapping or paint geometry from unmatched screenshots.

The same pass opened native NumberBox and XUI Numeric input.
Their default inline specimens showed upward and downward chevrons within the text field.
Tab traversal also exposed XUI's separate spin-button focus outline.
The native `NumberBoxSpinButtonStyle` explicitly sets `IsTabStop="False"`.
XUI retains separate Button children with keyboard targets.
Removing those targets is an input-contract change, not a rounded-focus correction.
This pass preserved them and made no numeric-input change.

Several background scroll actions produced no viewport change.
An unanchored Escape required foreground escalation, which this pass did not use.
The native scrollbar value action failed its postcondition check.
The successful vertical comparison used subsequent Tab navigation, not that scrollbar action.
Native search text replacement did not update the suggestion list, so category navigation opened NumberBox instead.
These delivery limits are not XUI rendering defects.
The recurring automation remained active before the deadline.

### September 18, 00:33-00:49: authored field focus corners

The full gallery now includes a sixteen-DIP TextInput specimen on the Text input page.
Background Tab navigation reached its native editor.
The before-fix image showed a nearly rectangular focus outline across the rounded field corners.
The authored surface used sixteen DIPs, but `paint_edit` called the four-DIP default focus helper.
The rebuilt gallery image showed a rounded outline along the same field silhouette.
These are live XUI observations, not conclusions from the regression suite.

The native NumberBox page provided a separate focused TextBox reference.
Six background Tab actions from its toolbar reached the first expression field.
Its image showed an accent underline, rather than XUI's authored two-tone outline.
The native specimen did not have a matching sixteen-DIP radius.
This correction fixes inconsistent XUI corners. It does not establish native authored-field pixel parity or redesign sparse-style focus treatment.

`Drawing::styled_field_focus` now resolves and clamps the authored radius for WinUI light and dark themes.
TextInput, MultilineText, RichText, PasswordInput, NumericInput, and editable ComboBox share this correction.
Classic and high contrast retain their four-DIP outline.
Unstyled fields, native editor bounds, keyboard targets, selection, undo, and IME paths remain unchanged.
The closed ComboBox highlight still uses its separate native-template path.

The new nonactivating `xui_field_focus_window_tests` failed against the old four-DIP painting.
Its corrected corner probe reported `corners=5, edge=3` for TextInput.
The first probe included five DIPs and intersected the valid sixteen-DIP arc.
The final probe samples only the outer three-by-three corner region, outside that arc.
With the fix, all six field types passed in both ordinary themes.
The fixture also checked blur cleanup and TextInput native identity, text, selection, and undo.
Its first successful run took 23.48 seconds.

The independent pixel suite passed with absent, square, rounded, and oversized radii, short fields, and three DPI values.
It also covered the unchanged Classic and high-contrast paths.
The adjacent model/style suites and both closed ComboBox fixtures passed.
The final selected run passed all seven tests in 35.21 seconds.
The field adapter completed in 11.01 seconds on that run.
These selected results do not represent a full repository-suite run.
The field test target emitted existing C4244 and C4245 conversion warnings in unrelated fixture code.

The background gallery launch reported that Windows refused its attempt to restore the previous foreground process.
This pass used no foreground workaround and did not manipulate an unrelated window.
The recurring automation remained active before the deadline.

### September 18, 00:53-01:11: Slider cross-axis placement

The Range input page now includes a 200-DIP horizontal specimen and a 100-by-100 vertical specimen.
Their sizes match the native gallery declarations, and their initial values match the corresponding native samples.
The existing interactive specimens and their event callbacks remain intact.

Seventeen background Tab actions reached the native vertical Slider and brought its complete target into view.
The native image showed its track near the left side of the 100-DIP target.
The initial XUI image showed its corresponding track at the horizontal center.
The pinned native template uses fourteen-DIP pre-content and post-content columns around the four-DIP track.
The horizontal template uses the same fixed rows above and below its track.
Thus the default cross-axis center is sixteen DIPs from the content origin, not half the available excess space.

`slider_visual` now uses that leading slot for WinUI.
Authored track thickness and thumb size can enlarge the slot.
Narrow content clamps the center to half the available cross-axis extent.
Root padding still applies through `RangeInput::slider_geometry`.
Classic retains its centered placement.
This change does not alter longitudinal thumb travel, pointer fractions, values, or drag cancellation.

The model regression failed before the correction:
`WinUI uses its leading 32-DIP cross-axis slot while Classic stays centered`.
The nonactivating adapter also failed at `(36,124)`, with background `0xf3f3f3` instead of accent `0x005fb8`.
After the correction, all four selected suites passed in 21.34 seconds.
They were `xui_winui_style_tests`, `xui_style_choices_tests`, `xui_foundation_tests`, and `xui_range_focus_window_tests`.
The adapter took 20.18 seconds and retained its focus, clipping, theme, disabled-state, and peer checks.
Additional model checks cover both axes, reversal, narrow content, authored thickness, and authored thumb size.

The rebuilt gallery image showed the vertical track near the leading edge of its matched 100-DIP target.
Its existing 44-DIP vertical specimen and 42-DIP horizontal specimen also use the leading slot.
This establishes qualitative cross-axis placement agreement, not full Slider parity.
XUI still has twelve-DIP track-end insets and a twenty-DIP outer thumb.
The native template separates its full-length track, eighteen-DIP thumb layout, and negative two-DIP outer-border margin.
Endpoint travel, outer-thumb extent, elevation, and motion remain separate unresolved work.
The recurring automation remained active before the deadline.

### September 18, 01:12-01:40: Slider rail and endpoint geometry

Background Home and End reached -50 and 50 in both matched vertical specimens.
The native minimum-state image showed the rail across the full target.
The initial XUI image showed its rail twelve DIPs short of each content edge.
The pinned `Slider_themeresources.xaml` and `Slider_Partial.cpp` separate the full rail from the thumb travel.
These files came from `microsoft/microsoft-ui-xaml` commit `e8442d07ae57d2d3e653e616831f504937881bd3`.
Their eighteen-DIP thumb layout box has an outer surface with a negative two-DIP margin.

`SliderVisual::travel_inset` now separates the rail from the pointer mapping.
WinUI uses a full-length rail, eighteen-DIP default layout box, and twenty-two-DIP painted thumb.
The value segment ends at the thumb layout boundary.
Explicit thumb sizes remain exact, without the default outset.
Classic retains its previous rail and travel.
The default thumb uses a ten-DIP radius and an inside one-DIP border.

The window adapter permits the default thumb to paint two DIPs outside its own longitudinal bounds.
Ancestor viewport clips and rounded host clips still apply.
The change does not expand the control hit target.
The existing value, reversal, preview, commit, and cancellation contracts remain intact.

The model regression failed before the correction:
`WinUI rail spans the content instead of stopping at thumb centers`.
The adapter regression failed at `(198,36)`, with background `0xf3f3f3` instead of rail color `0x868686`.
After the correction, the four selected suites passed repeatedly.
The final run took 34.07 seconds, including 33.53 seconds for `xui_range_focus_window_tests`.
The other suites were `xui_winui_style_tests`, `xui_style_choices_tests`, and `xui_foundation_tests`.

Model checks cover short controls, zero and explicit thumb sizes, reversal, padded pointer mapping, and drag events.
Owned-window checks cover full rail endpoints, horizontal and vertical thumb outsets, ancestor clipping, and removal of old pixels.
The rebuilt gallery images showed the full rails and larger thumbs.
Post-fix keyboard-focus images showed the reference vertical thumb at both endpoints inside the complete focus target.
The minimum and maximum values also appeared in the accessibility tree.

This pass supersedes the earlier twelve-DIP inset and twenty-DIP thumb limitations.
It does not establish pixel equivalence between the galleries.
Native elevation gradients, dot interaction states, animation, per-DPI layout rounding, and gamepad focus remain outside this correction.
Native tick marks, specimen backgrounds, and screenshot scaling also differ.
The recurring automation remained active before the deadline.

### September 18, 01:41-02:03: checked-disabled ToggleButton brushes

Background Tab traversal reached the checked ToggleButton in both galleries.
The images showed external rounded focus around the checked faces.
This pass did not find a new focus-corner defect in that state.

Disabling the native checked ToggleButton retained a distinct gray fill and text-on-accent foreground.
XUI instead used its ordinary unchecked disabled fill, foreground, and outline.
The accessibility trees retained the checked value in both applications.
The pinned `ToggleButton_themeresources.xaml` selects accent-disabled resources for `CheckedDisabled`, including a transparent border.
This reference came from commit `e8442d07ae57d2d3e653e616831f504937881bd3`.

`winui_button_brushes` now preserves the checked brush family when the button is disabled.
Disabled hover and press still have no effect.
The correction reaches default Button painting, legacy Button styles, and named-part styles.
Explicit author colors, Classic painting, system high-contrast colors, focus targets, and toggle events remain unchanged.

The model regression failed before the correction at the checked-disabled resource assertion.
The software regression also failed at `(22,40)`, with `0x586571` instead of `0x0f1d2b` over its sentinel surface.
After the correction, all six selected suites passed in 4.79 seconds.
They were `xui_winui_style_tests`, `xui_foundation_tests`, `xui_next_controls_tests`, `xui_menu_bar_tests`,
`xui_next_controls_pixels`, and `xui_button_focus_pixels`.
The pixel checks independently cover fill alpha, foreground, border removal, and stale hover/press inputs in light and dark themes.

The rebuilt XUI gallery showed the corrected disabled checked faces in dark and light themes.
Its light specimen retained white text on the gray checked-disabled fill, like the observed native specimen.
Different specimen widths and background surfaces prevent a pixel-equivalence claim.
No new gallery specimen was necessary.
The recurring automation remained active before the deadline.

### September 18, 02:04-02:33: NavigationView row focus

Background Tab traversal reached the selected navigation item in both galleries.
Native WinUI showed a strong white outline around the full row.
XUI showed a thinner outline inside its selected face.
The native accessibility bounds changed from a 312-by-36 face to a 320-by-40 focused row.
These bounds support the extent comparison. They are not measurements of individual painted pixels.

Both XUI navigation paint paths used the generic one-DIP focus strokes.
The styled path also ignored the resolved row radius.
Light and dark WinUI navigation now use the shared two-DIP primary stroke and one-DIP secondary stroke.
Two-DIP outsets from the existing inset face place the outline inside the full row bounds.
Styled rows and group headers use their resolved face radius, including square and oversized values.
Classic, high contrast, non-navigation collections, selected markers, and input behavior remain unchanged.

The new software regression failed before the correction.
Its 648 cases cover default and styled rows, groups, selected and unselected items, five radius settings, and clipped viewports.
They cover 96, 144, and 192 DPI in light, dark, and high contrast for both visual styles.
The reference draws independent stroke rectangles and native brush values instead of calling the repaired focus helper.
All six selected suites passed in 7.42 seconds.
They were `xui_style_collections_tests`, `xui_navigation_tests`, `xui_navigation_view_tests`,
`xui_next_controls_pixels`, `xui_button_focus_pixels`, and `xui_style_navigation_render_tests`.

The rebuilt gallery showed the selected Toggle button navigation row with a strong external two-tone outline.
The actual dark screenshot showed the full outline and rounded corners without viewport clipping.
This visual check was separate from the software regression.
Some intervening capture requests returned image-limit notices. Those requests do not supply additional visual evidence.

The native template reference was `NavigationView_themeresources.xaml` at commit `e8442d07ae57d2d3e653e616831f504937881bd3`.
Its presenter uses an inset template focus target.
The installed gallery and pinned template are different versions.
This correction retains XUI's existing five-DIP face radius rather than claiming an exact native corner-radius measurement.
Different row widths, pane padding, and screenshot scales prevent a pixel-equivalence claim.
The recurring automation remained active before the deadline.

### September 18, 02:34-03:06: SelectorBar focus

Live keyboard input reached native Recent and XUI Active items.
Native WinUI showed an external rounded outline. XUI showed a thin outline at the item edge.
The pinned `SelectorBar.xaml` uses the item corner radius and system focus visuals.
`SelectorBar_themeresources.xaml` sets `SelectorBarItemFocusVisualMargin` to `-2`.
Both references came from commit `e8442d07ae57d2d3e653e616831f504937881bd3`.

The existing external-focus pass now includes horizontal SelectorBar items in light and dark themes.
It uses the selected item's resolved radius and two-DIP margins on both axes.
Ancestor clips still apply. Selection, callbacks, peers, and native editor ownership remain unchanged.
Classic, high contrast, and vertical choice-list painting retain their existing treatment.

The new regression failed before the correction with `0x8c8c8c` instead of the expected `0x1b1b1b` primary stroke.
The owned-window fixture covers default, square, and pill corners, two visible items, selection movement, ancestor clips, and focus cleanup.
It also checks unchanged native text, selection, and peer identities across theme changes.
An initial eighty-DIP specimen displayed only one item slot. The fixture now uses 160 DIPs and asserts that both slots exist.
High-contrast checks retain the old extent, including the half-pixel edge of its one-DIP stroke.

SelectorBar checks now have a separate `xui_selector_focus_window_tests` entry to keep each desktop fixture bounded.
All eight selected suites passed in 40.52 seconds after this split.
They were the four focus/interaction adapters, `xui_style_choices_tests`, `xui_foundation_tests`,
`xui_next_controls_tests`, and `xui_next_controls_pixels`.
The rebuilt gallery showed the corrected focused Active item in dark and light themes.
The native light specimen also retained its selected Recent focus outline after the theme change.
Different item widths, root surfaces, and screenshot scales still prevent a pixel-equivalence claim.

During this pass, D: ran out of space and a failed patch truncated `tests/styling_window_tests.cpp`.
Recovery replayed all 48 successful file diffs from the session log, with exact source-context and hunk-length checks.
The recovered file compiled and passed the selected suites.
Only this session's build cache moved to C: under its session files directory.
The original `build\winui-fidelity` path is now a junction to that cache.
No unrelated files were removed. The recurring automation remained active before the deadline.

### September 18, 03:06-03:14: SelectorBar selection marker

The live native Recent and Page1 examples showed short centered selection markers at different item widths.
XUI instead stretched each marker to half the item width.
The pinned SelectorBar resources define a four-DIP rectangle with a horizontal scale of four in the selected states.
The resulting marker is sixteen DIPs wide and three DIPs high.

WinUI light and dark SelectorBar items now use that fixed width, clamped to the available item width.
Authored marker size still controls thickness.
Classic, high contrast, vertical choices, item layout, and selection behavior remain unchanged.
This correction does not change selection fills, animation, or marker corner geometry.

The owned-window regression failed before the correction.
It measures both painted marker edges and their position around the item center.
The four selected suites passed in 9.82 seconds.
They were `xui_style_choices_tests`, `xui_next_controls_tests`, `xui_next_controls_pixels`, and `xui_selector_focus_window_tests`.
The rebuilt gallery showed the shorter marker under focused Active in dark and light themes.
The light screenshot also showed the preserved external focus outline.
Different parent surfaces and item layout remain visible differences from native WinUI.
The recurring automation remained active before the deadline.

### September 18, 03:20: combined focus regression pass

The affected model, rendering, and window-adapter targets rebuilt against the latest sources.
All twenty selected suites passed in 105.42 seconds on ARM64 Release.
Coverage included navigation, Button, checkbox, radio, switch, ComboBox, popup focus layers, Slider, fields, SelectorBar, and Expander.
The run included the new navigation pixel matrix and the separate SelectorBar adapter.
This was a targeted integration run, not the complete repository suite.
Whitespace checks passed. The recovered test file also regained its original CRLF line endings without content changes.

The worktree gallery remains open on the light WinUI Selector bar page, with Active selected and keyboard focus visible.
The native gallery remains on the light SelectorBar specimen with Recent selected.
The current app handles are `xui_gallery` window `24649994` and native WinUI Gallery window `9046066`.
The session retains a generated recovery patch outside the repository.
No commit, push, or pull request occurred.
The deadline had not arrived, so the recurring automation remained active.

### September 18, 03:21-03:31: SelectorBar transparent surfaces

The live native light specimen had transparent item backgrounds, including the selected Recent and Page1 items.
XUI added an opaque root and a selected face fill.
The pinned SelectorBar resources specify transparent root and item brushes for every interaction state.
The light and dark WinUI renderer now uses transparent defaults for these surfaces.
Explicit root and item fills still apply. Classic and high contrast retain their previous defaults.
This correction does not change text-state brushes, disabled marker color, layout, or selection behavior.

The new owned-window regression failed before the correction with `0xebebeb` instead of the parent color `0x363636`.
It now checks transparent roots, selected and unselected items, hover, and distinct authored root and item fills.
All seven selected suites passed in 63.13 seconds.
They were the four focus/interaction adapters, `xui_style_choices_tests`, `xui_foundation_tests`, and `xui_next_controls_pixels`.
The rebuilt gallery showed the transparent SelectorBar surfaces and retained the external focus outline and short marker.
The live native light specimen provided another comparison after the correction.
Different content, widths, padding, and screenshot scales still prevent a pixel-equivalence claim.

The rebuilt worktree gallery uses window `45622056`.
The native gallery still uses window `9046066`.
Both remain on their light SelectorBar examples. The recurring automation remained active before the deadline.

### September 18, 03:32-03:49: MenuBar focus clipping

Keyboard traversal reached the native File heading with a complete rounded outline.
Opening and dismissing XUI's File menu left only the right side of its focus outline visible.
The heading filled the bar's height and touched its left edge. The bar's ancestor clip removed the other three sides.

The pinned `MenuBarItem.xaml` uses `FocusVisualMargin="-3"`.
`MenuBar_themeresources.xaml` supplies four-DIP margins around each item and a forty-DIP bar height.
Both references came from commit `e8442d07ae57d2d3e653e616831f504937881bd3`.
XUI now reserves those item margins during WinUI measurement and arrangement.
The ordinary forty-DIP bar contains thirty-two-DIP heading faces.
Root padding remains separate. Hidden headings reserve no space, and narrow slots retain nonnegative content bounds.
Classic layout, heading identities, command snapshots, and popup ownership remain unchanged.

Both the model-margin regression and the owned-window focus regression failed before the correction.
The window fixture checks all four edges and blur cleanup for default, square, and pill heading corners.
It runs in light, dark, and high-contrast themes.
The six selected suites passed in 31.46 seconds after the correction.
They were `xui_menu_bar_tests`, `xui_menu_bar_window_tests`, and the four focus/interaction adapters.
The keyboard adapter retained F10, arrow navigation, submenu switching, Escape, source replacement, and focus restoration.

The rebuilt gallery showed complete File outlines in dark and light themes.
The native light specimen also retained its complete File outline.
The XUI File face remains four DIPs wider than the installed native example because its existing text padding differs.
This pass does not change heading text padding or the open-menu selected fill.
The UI tool could not send F10 without foreground escalation, so the live XUI route used menu dismissal instead.
One Escape returned a key-up error after its popup window closed. A fresh observation showed successful dismissal and the focused heading.
The worktree gallery now uses window `30022430`. The native gallery still uses window `9046066`.
Both remain on their light MenuBar examples. The recurring automation remained active before the deadline.

### September 18, 03:56: final combined regression pass

All twenty-two selected suites passed in 96.14 seconds on ARM64 Release.
Their executables rebuilt against the latest sources before the run.
This matrix extends the 03:20 integration run with the MenuBar model and native-window suites.
It includes the new SelectorBar background checks and MenuBar focus checks.
This was not the complete repository suite.
The build log is `build\winui-fidelity\final-focus-build.log`.

The final live comparison moved focus from File to Edit with the Right arrow in both galleries.
Both light specimens showed complete outlines around Edit and no old outline around File.
The worktree gallery remains open at window `30022430`. The native gallery remains open at window `9046066`.
No unrelated window received UI input.

Remaining limits include native animation and elevation, per-DPI layout rounding, and gamepad focus.
SplitButton retains its documented separate keyboard targets rather than native compound focus.
SelectorBar text-state brushes, disabled marker color, and marker animation still differ from the pinned template.
MenuBar text padding and the open-menu selected fill remain unchanged.
The installed WinUI Gallery and pinned source differ in version.
The observations do not establish pixel equality across controls, DPI settings, or operating-system configurations.

All source changes remain uncommitted. No push or pull request occurred.
The recovery patch remains outside the repository, together with the relocated build cache.
The deadline had not arrived at this checkpoint.

At 03:58, the combined `--button-focus-window-only` fixture also passed with the new MenuBar specimens.
Its three themes retained the native editor peers, text, and selection.
The obsolete recovery script and older recovered source snapshot were removed.
The current recovery patch, active build cache, and worktree junction remain.
The recurring automation changed to a single 04:00 closeout so it can stop at the requested cutoff.
No fix remains in progress.

### September 18, 04:00: overnight cutoff

The final clock check returned `2026-09-18T04:00:37-05:00`, after the requested deadline.
The session automation was removed. No further visual pass or production change started.
The final evidence remains the twenty-two-suite run and the combined focus fixture described above.
The last live comparison covered keyboard focus on the light MenuBar Edit heading in both galleries.

The obsolete recovery script and recovered source snapshot are gone.
The active build cache and its worktree junction remain intact.
The final closeout includes a whitespace check and an updated session-owned `overnight-worktree.patch`.
All repository changes remain uncommitted. No push or pull request occurred.
The documented visual limitations remain open rather than implying complete native WinUI parity.

## Additional choices, badges, and menu bars

The September 17, 2026 extension adds CheckBox, HyperlinkButton, SelectorBar, InfoBadge, and MenuBar.
The [foundation contract](../specs/foundation-controls.md) and [command contract](../specs/commands-and-navigation.md#menubar) define their public behavior.
Existing Toggle and ToggleSwitch APIs remain binary.
HyperlinkButton supplies a callback action, not automatic URI navigation.
SelectorBar applies items and selected ID as one snapshot.
InfoBadge has no input action.
MenuBar roots use CommandSet submenu groups and window-managed popup ownership.

The [binding contract](../specs/bindings.md#checkbox-links-selectors-badges-and-menu-bars) defines typed wrappers and the five new markup nodes.
The [control catalog](../specs/controls/README.md) links four-language recipes.
The gallery includes `checkbox`, `hyperlink-button`, `selector-bar`, `info-badge`, and `menu-bar` pages.
These additions do not include TeachingTip, Rating, or color, dialog, and calendar upgrades.
This scope note does not extend the runtime evidence for the earlier toggle/progress work.

Source map:

- `include\xui\controls.hpp` and `src\controls.cpp`: CheckBox state, HyperlinkButton, and InfoBadge.
- `include\xui\foundation.hpp` and `src\foundation.cpp`: SelectorBar and the shared choice model.
- `include\xui\menu_bar.hpp` and `src\menu_bar.cpp`: MenuBar headings and command snapshot ownership.
- `bindings\dotnet\Xui\ParityControls.cs`: Typed events, selector snapshots, and menu callbacks.
- `bindings\dotnet\Xui.Generator` and `bindings\dotnet\Designer\VisualDocument.cs`: Markup arguments and palette templates.

## Toggle and progress update

September 17, 2026: the current implementation adds ToggleSwitch, ToggleButton, and ProgressRing.
The earlier handoff sections retain their original scope and evidence.
Their absent-animation statements do not describe this update.

`ToggleSwitch` derives from Toggle and retains its checkbox model and accessibility role.
`ToggleButton` derives from Button and selects toggle behavior by default.
`ProgressRing` derives from Progress and defaults to an indeterminate ring.
These presentations reuse the `toggle`, `button`, and `progress` style targets.
They do not add style catalog targets.

Indeterminate bars and rings use a window-owned timer.
Eligibility requires attachment, effective visibility, effective enabled state, and a visible, nonminimized window.
The system client-area animation preference suppresses motion.
Unknown and capacity displays remain static.
Hide, detach, disable, minimization, and destruction must leave no unnecessary progress timer.

The [foundation contract](../specs/foundation-controls.md) defines current behavior.
The [gallery reference](../specs/gallery.md) lists the dedicated presentation pages.
The [focused test procedure](../../CONTRIBUTING.md#toggles-and-progress) contains the native, ABI, and gallery commands.
Implementation scope alone does not establish runtime, screen-reader, visual-parity, or performance results.
The coordinating session reported these results on September 17, 2026, for the ARM64 Release worktree build:

- Core, catalog tests, and gallery targets compiled.
- `xui_gallery_catalog_tests` passed.
- `build\ARM64\Release\xui_gallery_smoke.exe build\ARM64\Release\xui_gallery.exe --controls-only` passed.

The focused gallery smoke checked actual ToggleSwitch and ToggleButton values and disabled behavior through UIA.
It also checked read-only Progress and ProgressRing values and idle hidden pages.
These results do not establish visual parity, screen-reader speech, full timer lifecycle coverage, or system reduced-animation behavior.

Source map:

- `include\xui\controls.hpp`: ToggleSwitch and ToggleButton declarations and inherited state APIs.
- `include\xui\foundation.hpp` and `src\foundation.cpp`: ProgressRing and the shared progress state and capacity model.
- `src\application.cpp`: `animated_progress`, `sync_progress_animation`, and `stop_progress_animation` own timer eligibility and cleanup.
- `bindings\features.json` and `include\xui\xui_features.h`: Factory kinds and scalar property contracts.
- `bindings\dotnet\Xui\ToggleControls.cs`: Typed toggle events and progress capacity setters.
- `bindings\dotnet\Xui.Generator` and `bindings\dotnet\Designer\VisualDocument.cs`: Native markup nodes and palette templates.

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
