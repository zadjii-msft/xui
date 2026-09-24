# Opt-in animations

## Scope

The transitions in this guide require application opt-in.
Their durations default to zero.
Automatic indeterminate Progress and ProgressRing animation is separate and does not require an explicit duration.
It applies to both Classic and WinUI styles. Unknown progress remains static.
The [progress contract](foundation-controls.md#progress-presentations-and-animation) defines that animation and its separate native timer.
`Reveal` supports four entry edges and optional layout expansion.
The FileExplorer Find bar uses bottom entry with layout expansion.
Unchecked items in the roadmap are not supported APIs.

A reveal retains one child and a native clipping window.
Its content stays at full size while its position changes.
It does not replace native text input with a bitmap snapshot.
Closed reveals defer native child-window creation until their content opens.
Retained controls keep their values, ownership, and typography before native creation.
Once created, native editors retain their identity while an attached reveal closes and opens.
Popup dismissal and content retirement still release their native windows.

## Reveal contract

`xui::Reveal` belongs to `xui/reveal.hpp`.
Its constructor accepts one content element and an optional accessible name.
Its duration defaults to zero, so motion requires an explicit duration.
Durations range from zero through 10,000 milliseconds.
Invalid durations produce an error without changing the previous value.

```cpp
#include <xui/reveal.hpp>

auto input = std::make_shared<xui::TextInput>(L"Find");
input->set_caption_visible(false);
input->set_preferred_size({320, 44});
auto bar = std::make_shared<xui::Stack>(xui::Axis::horizontal);
bar->set_padding({6, 6, 6, 6});
bar->add(input, 1);
auto find = std::make_shared<xui::Reveal>(bar, L"Find bar");
find->set_duration(180);
find->set_layout(xui::RevealLayout::expand);
root->add(find);

find->set_open(true);
window.focus(*input);
```

The snippet uses an existing root stack and window.
The application calls the final two methods when Find opens.

### Bindings and declarative authoring

C# creates the host with `Window.Reveal(content, name)`.
`Reveal.Open`, `Reveal.Duration`, `Reveal.Layout`, and `Reveal.Direction` are writable properties.
`Reveal.Progress` and `Reveal.Animating` are read-only properties.
The content must belong to the same window and have no existing parent.
Content scopes use the existing retirement and stale-handle rules.

Rust creates the same retained host with `Window::reveal(content, name)`.
The C ABI declares `xui_reveal_create` in `xui.h`.
It also supplies `xui_reveal_set_open`, `xui_reveal_get_open`, `xui_reveal_set_duration`, and `xui_reveal_get_duration`.
`xui_reveal_get_progress` and `xui_reveal_get_animating` report the current presentation.
Layout and direction use `xui_reveal_set_layout`, `xui_reveal_get_layout`, `xui_reveal_set_direction`, and `xui_reveal_get_direction`.
The feature uses explicit functions rather than a new public `XuiKind` value.

The `.xui` node uses `Reveal("Find", open: FindOpen, duration: 180, layout: global::Xui.RevealLayout.Expand) { ... }`.
It accepts exactly one child.
The compiler configures the native host and binds the state.
It does not drive frame updates in C#.
The [language guide](xui-language.md#opt-in-bottom-reveal) contains a complete component.

### Bounded portable Windows opt-in

`xui_reveal_portable.h` adds a separately versioned opt-in on an existing Reveal handle.
It does not change legacy Reveal defaults or setters on legacy instances. Portable instances
use one atomic Open/Motion state with duration 0 through 400 milliseconds and Bottom or Right
expansion. Initial attachment is settled at the requested target. An unchanged state does not
restart motion; reversal with unchanged motion starts from the current presentation. Changing
duration or direction settles the old logical target first, then applies the new configuration
and any paired target change. Motion-only changes therefore settle rather than restart.

The actual native clip is the smaller of the parent allocation and the full natural child
extent multiplied by progress. The child retains its full finite extent on the animation axis
and receives the actual cross-axis allocation. A fixed or star parent slot can retain unused
space; this is not a promise that all neighboring layouts animate. Zero allocation always
clips to zero. A settled closed element measures zero but remains a Stack layout participant,
so authored spacing remains. Size the child, not the Reveal: outer sizes, axis overrides, and
nonzero flex are outside this bounded contract.

Closing preflight returns false only for focused or composing descendants. Move focus
explicitly and finish composition before closing; the backend does not cancel native input
or steal focus. Closed content becomes input-inert and leaves accessibility Control/Content
views immediately, even while real exit pixels remain. Native editor HWNDs, selection, undo,
and delegated native value behavior are retained. High contrast, reduced motion, owner
invisibility, and retirement settle/stop the owned motion; a settled Reveal has no idle clock.
Peer disposal cancels its native motion before unmount, without an authored Cancel event.

The first Windows slice explicitly rejects RichEdit-backed MultilineText/RichText descendants
to preserve their native Text/Text2 providers. EDIT-backed PasswordInput is not excluded by
that restriction. FileList, native runtime/plugin hosts, and leased virtual viewports also
require separate accessibility qualification and are not supported inside this opt-in.
Portable adapters must preflight complete trees and prospective inserted subtrees before
their model commit; native construction/insertion repeats these checks. Unsupported content
is an explicit error, never a false focus/composition veto or a bitmap replacement.
Detect all six portable Reveal exports and their version before advertising the capability.

### State and lifetime

The default layout is `RevealLayout::fixed`, with `RevealDirection::bottom`.
In fixed layout, `set_open(true)` reserves the full measured height immediately.
The content moves upward from below the host clip.
`set_open(false)` stops interaction immediately, but retains the reserved height during the exit.
The content moves downward.
The host releases the reserved height when the exit finishes.
An automatic grid row can therefore collapse without a separate application timer.

Fixed layout does not animate neighboring layout.
It preserves the original, cheaper placement-only behavior.

`RevealLayout::expand` changes the reserved extent on every frame.
The reserved extent equals the natural content extent multiplied by `progress()`, within the available space.
An automatic grid track or a non-flex stack child uses that extent.
Neighboring flex or star content uses the remaining space in the same layout pass.
The Find bar and the file view therefore share one moving edge.
Neither entry nor exit adds a second translation to that edge.

Expanding content keeps its full natural extent along the animation axis, even at progress zero.
The native clip grows or shrinks, but the native editor does not shrink.
The content must have a bounded natural extent along that axis.
A flex stack or star grid that fills unlimited space needs an explicit preferred size.
Unbounded content produces an error rather than an oversized native window.
Constraints on the parent can clip content before the complete natural extent is visible.
Fixed tracks, nonzero track minima, flex allocation on the reveal, and explicit spacing can reserve space independently of progress.

`RevealDirection::bottom` and `top` animate height.
`RevealDirection::left` and `right` animate width.
In expanding layout, bottom and right entry anchor the child at the clip origin.
Top and left entry align the trailing child edge with the trailing clip edge.
In fixed layout, the selected edge determines the translation inside the full slot.
Layout and direction changes settle active motion before applying the new geometry.
Invalid enum values produce an error without changing state.
Content and viewport changes use the current progress with the new natural size.
They do not start a separate resize transition.
Nested reveals retain independent targets and share the window clock.

`open()` reports the logical state.
`progress()` reports the current presentation, from zero through one.
`animating()` reports an unfinished transition.
A new state reverses motion from the current presentation.
The easing curve is cubic ease-out.
Each new transition uses the configured duration.
Repeated assignments of the same state do not restart motion.
A duration change settles an active transition to its logical state.

The application chooses focus.
The FileExplorer controller focuses Find immediately on entry and restores file-view focus immediately on exit.
The input keeps its native identity, text, selection, and undo history.
Native input bounds move with the presentation.
The exit does not delay filter cancellation or other application commands.

The Windows animation preference controls motion.
Disabled client-area animation settles transitions immediately.
Hidden or minimized windows settle active transitions.
Hidden ancestors, disabled ancestors, content retirement, and window closure also stop active motion.
The scheduler retains no worker thread or per-control timer.
One shared transition timer exists only while a transition is active.
The message loop services due animation timers and their frame paints during sustained posted-message traffic.
This prevents a busy update queue from indefinitely delaying motion.
It does not interrupt a blocking application callback or guarantee a frame rate.
Timer and system-preference errors use the existing window error path.

## Performance boundary

Fixed-layout intermediate frames arrange the reveal subtree and update native peer positions.
They do not measure and arrange the complete root tree.
Expanding-layout frames use ordinary root measure and arrange.
All active transitions receive the same clock sample before that layout pass.
The cost depends on the complete window tree, not only the revealed child.
Entry, exit completion, and other layout changes also use ordinary root layout.
The child controls and their native windows remain retained.

The current renderer still redraws the complete visible frame.
It also captures visible native editor pixels for that frame.
Changing native clip sizes can resize native composition buffers.
This feature does not promise compositor-only motion, allocation-free frames, or a fixed CPU cost.
Large documents, multiple simultaneous reveals, and large windows need separate measurements.

The shared transition timer stops after the final transition.
These transitions then cause no periodic repaint.
Eligible indeterminate progress controls can still request periodic paint through their separate timer.
No animation surface is necessary for this implementation.
Unrelated control updates retain their existing behavior.

## Split-pane transitions

`SplitView` has an opt-in transition duration, with zero as the default.
C++ uses `set_transition_duration(milliseconds)`.
C# uses `TransitionDuration` or `SetTransitionDuration(milliseconds)`.
Rust uses `set_transition_duration(milliseconds)`.
The C ABI uses `xui_split_view_set_transition_duration` and its matching getter.
The range is zero through 10,000 milliseconds.
Invalid values preserve the previous duration and presentation.

The `.xui` `SplitView` node accepts `duration: 180`.
The compiler initializes duration before `secondVisible`.
The duration can depend on component state.
`Progress` and `Animating` report presentation in C#.
C++ and Rust expose the corresponding `progress()` and `animating()` methods.
The C ABI exposes `xui_split_view_get_progress` and `xui_split_view_get_animating`.

The secondary pane enters from the right.
The primary pane and divider follow the same progress value.
The secondary content retains its full target width inside the split viewport.
This policy prevents repeated text and toolbar reflow inside the incoming pane.
The native editor can receive focus before the pane reaches its final position.
Closing disables secondary input immediately while exit pixels remain.
The application remains responsible for focus return and title-tab interaction.
Explorer keeps title-tab geometry aligned with the pane and disables outgoing title tabs.

Logical visibility and `expanded()` do not wait for animation completion.
View events report logical visibility and available layout, not completion.
Visibility reversal starts at the current presentation.
During entry or exit, ratio changes settle visibility motion and apply immediately.
During entry or exit, resizing a sufficiently wide window uses the same progress with new target widths.
Incompatible narrow widths settle motion.
The shared window scheduler applies reduced motion, hiding, retirement, and shutdown to reveals, split panes, and tabs.

### Ratio presets

The same duration enables programmatic ratio transitions when both panes are available.
The logical ratio changes immediately. Both pane widths and the divider move between minimum-width-constrained endpoints.
Repeated or equivalent targets do not start extra motion.
Retargeting starts from the displayed geometry and uses the full configured duration.
Visibility changes settle the ratio target before entry or exit.
Resize and metric changes settle ratio motion. Translation without resize preserves it.

Pointer capture takes control at the displayed divider position.
Subsequent pointer dragging remains direct.
`progress()` continues to report visibility progress, which stays at one during a ratio transition.
`animating()` reports either visibility or ratio motion.
The gallery presets use the existing ratio API. No new binding surface is necessary.

## Tab insertion, removal, reorder, and overflow

`TabStrip` has an opt-in duration, with zero as the default.
C++ uses `set_duration(milliseconds)`. C# uses `Duration` or `SetDuration(milliseconds)`.
Rust uses `set_duration(milliseconds)`.
The C ABI uses `xui_tab_set_duration` and `xui_tab_get_duration`.
The range is zero through 10,000 milliseconds.

New tabs grow from their left edge while existing stable IDs move to their new positions.
Logical selection changes immediately.
Drawing, pointer targets, close targets, accessible bounds, and the retained New tab button use the same presented rectangles.
Intermediate frames use placement invalidation rather than root layout.
Initial population remains immediate.

Removal deletes the logical tab, its pointer targets, and its accessible item immediately.
Surviving tabs retain their presented positions, then move to close the gap.
The retained New tab button also moves when the last tab is removed.
Insertion during removal starts from the current surviving rectangles without snapping away the remaining gap.
The removed tab has no exit pixels or delayed close command.
Clearing all tabs remains immediate.

Same-ID reorder changes logical order immediately and moves each tab from its current presented rectangle.
Reordering does not synthesize selection callbacks.
Crossing tabs narrow around their moving centers to keep their rectangles disjoint.
They can briefly reach zero width, then regain width after crossing.
Pointer, context, close, and accessible bounds use those same rectangles.
This avoids competing input targets at an overlap, but it is not a full-width overlapping-tab effect.

Reorder and reversal can interrupt active motion.
Supported order-preserving insertion and removal can also start from the current presented rectangles.
Each retarget uses the configured duration.
Mixed replacement/reorder batches, overflow topology changes, resize, metric changes, and New tab button visibility changes settle incompatible motion.
Overflow selection moves the viewport from its current presented offset.
Logical selection changes immediately, including selection through a same-ID `set_tabs` refresh.
The New tab button stays stationary and reachable.
Overflow reserves its button slot at the viewport edge, including immediate mode and endpoints with unused tab space.
Tab rectangles clip at both viewport edges. Pointer, close, context, and accessible bounds follow those clipped rectangles.
Reversal preserves the current offset. Metadata refresh and window translation preserve active motion.
Overflow insertion, removal, and reorder still settle immediately.
Explorer opts into 180 ms insertion, removal, reorder, and overflow selection.
Its existing Shift left and Shift right tab-menu commands expose reorder.
The gallery `tabs` page provides normal, slow, and immediate duration choices.
Add document, Close selected document, and Reverse document order expose the supported transitions.
Fill overflow adds documents without changing selection. First document and Last document move between the viewport endpoints.
Applications can pass an existing `TabStrip` through `.xui` `Content`.

## Navigation group transitions

`NavigationView` has an opt-in group duration from zero through 10,000 milliseconds.
The default is zero. C++ and Rust use `set_duration`.
C# uses `Duration` or `SetDuration`, with read-only `Animating`.
The C ABI provides `xui_navigation_view_set_duration`, `xui_navigation_view_get_duration`, and `xui_navigation_view_get_animating`.

Main-section disclosure changes logical rows, expansion state, and selection immediately.
Collapse repairs descendant focus to the surviving ancestor.
Outgoing rows supply draw-only content. They have no pointer targets or accessible membership.
Their text and images come from the retained snapshot, not indexes in the current source.
Frames move surviving rows and clip descendants without replacing the logical source or measuring the root.
The shared window clock runs only while motion remains active.

Same-branch reversal starts at the last presented geometry.
A different branch settles the previous transition before its own eligible transition starts.
Equivalent targets and durations preserve motion.
Initial population, header/footer disclosure, offscreen branches, incompatible geometry, and excessive exit budgets remain immediate.

A surviving visible key anchors scrolling, subject to extent clamping.
Focus repair that needs scrolling settles motion before it reveals the ancestor.
Source, filter, selection, pane geometry, and duration changes settle motion.
Direct scrolling, reveal, resize, cancellation, hidden or disabled ownership, reduced motion, and retirement also settle motion.
Native search retains its identity, text, selection, and undo ownership.

Explorer opts into 180 ms for sidebar group arrows.
The gallery `navigation-view` page provides normal, slow, and immediate durations.
The pane toggle remains distinct from group disclosure. Compact-pane motion is still a separate roadmap item.
Applications can configure the constructed control or use a reactive `.xui` `duration` argument.
Omission preserves the native zero-duration default. Unchanged bindings do not repeat the setter.
Explorer declares its opt-in in `SidebarLayout.xui`.

```xui
component AnimatedNavigation {
    state uint Motion = 180;
    view {
        NavigationView("Workspace", duration: Motion, ref: Navigation);
    }
}
```

## Expander transitions

`Expander` has an opt-in duration from zero through 10,000 milliseconds.
The default is zero. C++ and Rust use `set_duration`.
C# uses `Duration` or `SetDuration`.
The C ABI exposes `xui_expander_set_duration`, `xui_expander_get_duration`, `xui_expander_get_progress`, and `xui_expander_get_animating`.

Logical expansion changes immediately.
The body clip and neighboring layout follow cubic ease-out progress.
The original child retains its full size and ownership during motion.
Closing disables child input immediately and restores focus to the header through the existing focus-repair path.
Opening supports native focus before the body clip grows.
Reversal starts from the current progress.
Duration changes settle the current target. Invalid values leave state unchanged.

Resize and content measurement apply the current progress without restarting the transition.
Explicit host sizing can reserve space while the body clip changes.
The zero-duration path preserves the existing Classic, WinUI, and authored-style geometry.
The gallery `disclosure` page provides duration choices and Reverse expansion.
Applications can pass the configured expander through `.xui` `Content`.

## Determinate progress transitions

`Progress` interpolates determinate values when its duration is nonzero.
The default duration is zero. The valid range is zero through 10,000 milliseconds.
Logical values and UIA RangeValue change immediately.
Only the displayed fill and generated percentage caption follow presentation.
Frames request painting, not root layout.

C++ uses `set_duration`, `duration`, `presented_value`, `presented_fraction`, and `animating`.
C# uses `Duration`, `SetDuration`, `PresentedValue`, and `Animating`.
Rust uses `set_duration`, `duration`, `presented_value`, and `animating`.
The C ABI provides `xui_progress_set_duration`, `xui_progress_get_duration`, `xui_progress_get_presented_value`, and `xui_progress_get_animating`.

Retargeting starts from the current displayed value.
Changed range, state, or duration settles motion.
Equivalent assignments preserve active motion.
Paused and error values update immediately, with their existing colors and caption suffixes.
Unknown states remain static. Capacity updates remain immediate.
Indeterminate motion uses the separate native progress timer, not this duration or the shared transition timer.

An incomplete logical value cannot produce a complete fraction or a generated `100%` caption.
This guard also applies immediately after a downward retarget from maximum.
It is a numeric guarantee, not a guarantee of a visible one-pixel gap.
Reduced motion and lifecycle settlement use the shared window policy.
The gallery `progress` page exposes normal, slow, and immediate modes with reset, retarget, and completion actions.
Applications can pass the configured progress control through `.xui` `Content`.

## Replayable examples

The gallery **Layout > Motion** page (`animations`) exposes all four reveal directions, layout modes, concurrent motion, and a nested pane editor.
Its controls support replay, reversal, immediate mode, and slow diagnostic motion.
Duration and layout changes settle current targets before applying the new configuration.
The [gallery guide](gallery.md) describes the controls.

The `feedback-motion` page composes `InlineStatus` with expanding reveals.
Show notice and Hide notice change the reveal target without changing built-in status dismissal.
The validation reveal follows native field edits and the explicit Validate name action.
Message state changes once per action or validation change, not once per animation frame.
The field remains a native editor with its existing selection and undo history.

The `content-motion` page composes fixed reveals in one grid cell.
Show loading, Show empty, and Show results simulate state changes without a background request.
The fixed slot prevents neighboring content from moving.
The query editor stays outside that slot.
Enter in the query shows results without moving focus.
Enter in the result note shows loading and returns focus to the query.
The state buttons retain normal button-focus behavior.

Outgoing result content stops accepting input immediately, while its exit remains visible.
If the outgoing result editor owns focus, the demo returns focus to the query.
New result content does not take focus automatically.
The result editor retains its native identity, text, and undo history across state changes.
This example does not add transitions to `PageView` or change asynchronous request cancellation.

The `pages` example wraps a retained `PageView` in one fixed Reveal.
The application selects the new logical page immediately, then restarts entry from the selected direction.
The second page enters from the right. The first page enters from the left.
Only the selected page accepts input. Inactive editors retain native identity, text, selection, and undo.
The fixed slot prevents adjacent layout changes.
Rapid switching retires the previous entry and starts the latest page from its entry edge.
This is incoming entry, not simultaneous page presentation or an outgoing crossfade.
`PageView` has no duration property. The application explicitly configures the surrounding Reveal.

Explorer view changes do not animate.
The selected file view appears in its final position without a Reveal wrapper or an entry timer.
The previous view loses input through the normal native update.
The toolbar, footer, content allocation, selection, and saved scroll offset retain their existing ownership.
Rapid mode changes cancel obsolete filter work and display only the final mode.
This policy does not change the separate Find, navigation-pane, split-pane, or preview transitions.

The `document-motion` page moves retained `MultilineText` and `RichText` controls through an expanding top reveal.
Native RichEdit keeps text, selection, caret, and undo ownership.
The controls remain full size behind the clip.
The rich document starts read-only and has a separate editing option.
Motion does not change that policy.
This example does not establish media, WebView, opacity, snapshot, or IME support.

Detached Explorer previews also opt into client-content entry.
`PreviewLayout.xui` wraps the retained body in a fixed Reveal with an explicit 180 ms duration.
The first loaded text, image placeholder, or metadata starts entry.
The status row, titlebar, and operating-system window remain stationary.
Asynchronous image decoding remains independent of entry.
The demo does not crossfade decoded images or animate native window creation.
Escape and Close use the existing queued window closure without waiting for entry.

## Delivery roadmap

The first two phases cover Find and layout expansion.
Later phases add visible application demos, not default control animations.

### Collection presentation foundation

Navigation-group motion requires a shared collection contract before a duration API.
The default `VirtualCollection` path derives row bounds from consecutive `ItemsSource::row_start()` offsets.
An artificial exit gap in that logical source therefore extends a surviving row's pointer and accessible bounds.
Keeping outgoing rows in the logical source also keeps their accessible identities alive.
Replacing the source on each frame is not an acceptable substitute.

The collection phase separates logical membership from presentation:

- An immutable presentation snapshot supplies explicit row bounds, clips, and noninteractive gaps.
- Drawing, hit testing, accessible bounds, scrolling, and visible-row enumeration consume the same snapshot.
- Logical expansion and selection keep their existing immediate semantics.
- Outgoing rows use a separate draw-only cache without pointer targets, focus, or accessible membership.
- Cached outgoing content owns or safely retains its text and images. It never reuses indices against a replacement source.
- Per-frame work stays bounded by visible content and active transitions, without per-row native controls or source replacement.
- Scroll anchors use stable identities and offsets. Existing focus repair still reveals the surviving ancestor after collapse.
- Source changes, retirement, user scrolling, and reduced motion have explicit interruption rules.

The internal foundation and its NavigationView consumer passed model and native acceptance.
Immutable bands describe single-column logical ranges without allocating geometry for every item.
Frames support explicit rectangles, clips, extent, and monotonically increasing versions.
They reject overlapping paint regions, stale versions, incompatible dimensions, and tile presentation.
Frame updates change layout events without publishing new logical sources.
Outgoing rows retain frozen text, hierarchy, selection appearance, and visual metadata outside logical membership.

Frame updates preserve the numeric scroll offset, constrained by the current extent.
The transition producer must supply any stable-key anchor adjustment.
Explicit scroll or reveal, resize, metric changes, cancellation, and retirement clear the presentation.
The protected retirement callback lets the producer stop its clock.
Presentation consumers enumerate `visible_content()` rather than treating the `visible_items()` envelope as a row list.
The foundation limits each frame to 4,096 bands and 512 outgoing rows.

Navigation groups are the first application consumer.
Tree, virtual-list, and grid transitions follow only after that shared contract passes native acceptance.
The NavigationView producer and thin duration bindings now use this foundation.

### Proposed overlay motion rules

Popup content entry is available through an application-owned Reveal.
Hover-card and popup exit motion remain separate roadmap items.
The existing popup service owns modality, native clipping, dismissal, focus return, and generation checks.
An animation must not delay command cancellation, dialog results, or generation revocation.

The popup gallery animates client content on entry only.
The popup frame uses its final, edge-aware placement from the start.
Its content remains full size inside that clip.
Native typing works before the first animation frame.
Dismissal remains immediate and releases closed native peers through the existing popup lifecycle.
Reopening preserves the retained text model, not the dismissed native HWND or its undo history.
The animation retains native identity while that popup generation remains open.

Explorer command and location palettes appear immediately, without result-area motion.
The query editor, results, and final popup frame stay stationary.
Cold queries remove old logical rows immediately. Canceled results cannot replace a newer generation.
Native typing, selection, undo, and command execution remain available immediately.
Escape and execution dismiss immediately, with normal deferred native cleanup.
The result slide was removed after visual review.
A whole-palette pop-in is a future effect, not another result slide.
Scale-and-fade presentation needs a separate rendering contract that preserves native text input and accessible geometry.
There is no animated entry or exit in the current Explorer palettes.

The dialog gallery provides optional entry for its form content.
The dialog title, frame, validation area, and action buttons stay fixed.
Modal exclusion and native focus apply immediately.
Validation and results use the existing dialog callbacks.
Dismissal does not wait for entry motion to finish.

Exit motion needs a separate, noninteractive presentation record after logical closure.
That record must not retain input capture, focus, modal ownership, or live command callbacks.
Reopening the same popup must revoke the old presentation before the new generation becomes interactive.
Result callbacks can change retained content, so an exit cannot assume that the old content remains unchanged.
The first implementation will not substitute screenshots for unsupported native surfaces.

Nested entries must retain the current popup-stack order and nesting limit.
Anchor retirement, owner hiding, focus loss, and unavailable placement must settle or cancel presentation.
Reduced motion must preserve the same callback order and logical results.
Native media and web restrictions remain unchanged.
Tooltips must retain their existing dwell time and must not become focus targets.

### Demo catalog

This catalog records candidates, not a promise to animate every state change.
The first priority is the Explorer workspace: Find, tabs, navigation, and panes.
An animation gallery must expose replay, reversal, immediate mode, and a slow diagnostic mode.
The normal demo uses short durations. Diagnostic durations are not production defaults.

| ID | Demo surface | Motion to evaluate | State |
| --- | --- | --- | --- |
| A01 | Explorer Find | Bottom entry and exit with coordinated file-view height | Delivered |
| A02 | Explorer sidebar | Left entry and exit with coordinated file-view and title-tab positions | Delivered |
| A03 | Explorer tabs | New tab entry from the left and neighboring-tab movement | Delivered; pane-aligned host correction accepted |
| A04 | Explorer secondary pane | Right entry and exit with coordinated primary pane, divider, and title tabs | Delivered |
| A05 | Tab removal | Neighboring tabs close the gap without delaying logical removal | Delivered |
| A06 | Tab reorder | Stable-ID positions move to the new order without stale pointer targets | Delivered with disjoint crossing rectangles |
| A07 | Tab overflow | Selected-tab reveal and overflow scrolling without hiding the new-tab button | Delivered |
| A08 | Split ratio changes | Programmatic preset changes move both panes together. Pointer dragging remains direct | Delivered |
| A09 | Navigation groups | Expand and collapse child rows while retaining selection and scroll position | Delivered |
| A10 | Compact navigation | Expanded pane to compact rail, including labels and search focus | Planned |
| A11 | Adaptive navigation | Overlay entry, dismissal, and breakpoint settlement | Planned |
| A12 | Reveal playground | Four directions, fixed and expanding layouts, nested clips, and native text input | Delivered |
| A13 | Page and view switching | Directional transitions between retained pages without simultaneous input owners | Delivered: gallery PageView incoming entry with retained native editors; outgoing presentation remains separate |
| A14 | Explorer file views | Immediate view changes with retained selection and viewport | Entry animation removed after user feedback; Miller-column topology remains separate |
| A15 | Miller columns | Column insertion and removal with stable sibling selection | Planned |
| A16 | Expanders | Content expansion with neighboring form layout and native field height | Delivered |
| A17 | Tree nodes | Branch expansion and collapse with bounded visible-row work | Planned |
| A18 | Virtual collections | Stable-ID insertion, removal, and reorder without per-row controls | Planned |
| A19 | Data grid updates | Row insertion/removal and column rearrangement without changing text-editing semantics | Planned |
| A20 | Empty and loading states | Transition between states without flashing stale data or shifting focus | Delivered as manual retained-state demo |
| A21 | Inline status and notifications | Entry and dismissal that keep live announcements tied to logical state | Delivered |
| A22 | Validation messages | Field-adjacent expansion without moving the caret or scrolling unexpectedly | Delivered |
| A23 | Command and location palettes | Whole-palette pop-in without scrolling results, with immediate native typing and Escape handling | Result slide removed after review; pop-in planned; current entry and dismissal immediate |
| A24 | Popups and custom flyouts | Edge-aware motion inside the owner clip with consistent pointer dismissal | Entry delivered; exit planned |
| A25 | Client modal dialogs | Content entry with modal focus and background interaction blocked immediately | Form entry delivered; whole-surface motion planned |
| A26 | Hover cards and tooltips | Small positional transitions without extending dwell or exposing stale content | Planned |
| A27 | Progress displays | Optional interpolation of determinate values without falsifying task completion | Delivered |
| A28 | Toggle, selection, and focus indicators | Small state transitions with immediate logical and accessible values | Planned |
| A29 | Image replacement | Optional crossfade after decode, with explicit allocation and failure behavior | Deferred rendering work |
| A30 | Vector and chart samples | Data-point or scene interpolation on request, without an idle render loop | Planned experiment |
| A31 | Theme and color transitions | Optional color interpolation with high-contrast settlement | Deferred rendering work |
| A32 | Native document, media, and web boundaries | Demonstrate supported host motion and explicit unsupported effects | Native documents and clipping delivered; other hosts pending |
| A33 | Window creation and detached previews | Evaluate client-content entry. Do not animate OS chrome or window lifetime | Detached preview content entry delivered |
| A34 | Stress and interruption lab | Concurrent motion, rapid reversal, resize, DPI, hidden windows, and retirement | In progress |

Every delivered row needs a discoverable action in Explorer or the gallery.
A native API without a reachable demo does not complete a row.
The acceptance pass checks intermediate pixels, geometry, focus, native identity, cancellation, reduced motion, and idle work.
Pointer targets and accessible bounds must use the same presentation as drawing.
Opacity, arbitrary transforms, and snapshot effects require separate rendering work.
System menus, native file dialogs, and OS caption behavior remain under Windows control.

### Implementation and refinement order

- [x] Deliver A02, A03, and A04 in the normal Explorer workflow.
- [x] Add A12 as a replayable animation gallery, including duration and immediate-mode controls.
- [ ] Complete the remaining A34 interruption and platform checks.
- [x] Add A05 through A08 with explicit interruption rules.
- [x] Add A09 and A16 without per-item timers or native-window churn.
- [ ] Define compact and adaptive navigation motion for A10 and A11.
- [x] Define entry-only popup and modal contracts for A24 and A25.
- [x] Remove A23 result sliding after visual review and retain immediate native typing and dismissal.
- [ ] Define a whole-palette pop-in for A23 without moving results independently or replacing native input.
- [ ] Add A26 and evaluate independent overlay exit presentation.
- [x] Add retained entry and state examples for A13 and A20 through A22.
- [x] Keep A14 Explorer view changes immediate, with retained selection and viewport.
- [ ] Evaluate A15 and A17 through A19 with stable identity and virtualized data.
- [x] Keep A27 tied to truthful logical values and system accessibility preferences.
- [ ] Define A28 indicator transitions without delaying logical state.
- [x] Add native document motion for A32 and detached preview client entry for A33.
- [ ] Measure A29 through A31 and the remaining A32 host boundaries before adopting new effects.
- [x] Repeat the acceptance pass on the implemented demos and record unresolved visual or performance issues.

### 1. Native reveal and FileExplorer Find

- [x] Add a retained bottom reveal with explicit duration and open state.
- [x] Add one active-only scheduler per window and placement-only invalidation.
- [x] Preserve native input, focus, clipping, accessible bounds, and child ownership.
- [x] Support reversal, reduced motion, hidden windows, retirement, and closure.
- [x] Expose the native feature through the C ABI, C#, and Rust.
- [x] Add a thin `.xui` node with one child and reactive open state.
- [x] Replace the Find row visibility switch with the reveal host.
- [x] Cover intermediate positions, idle work, first-character input, and Explorer regressions.

### 2. Coordinated layout and reveal directions

This phase first removes the Find resize discontinuity.
Overlay composition has a separate follow-up because it requires z-order and input contracts beyond layout expansion.

- [x] Define `fixed` and `expand` policies without changing existing defaults.
- [x] Add bottom, top, left, and right entry directions.
- [x] Measure the animated extent without applying progress twice during parent layout.
- [x] Arrange full-size native content inside the changing clip.
- [x] Request root layout only for expanding frames and retain placement-only fixed frames.
- [x] Permit immediate native focus when an opening clip still has zero extent.
- [x] Expose layout and direction through C, C#, Rust, and reactive `.xui` arguments.
- [x] Switch FileExplorer Find to expansion and cover the moving shared edge.
- [x] Cover all directions, exact endpoints, reversal, constraints, and viewport resize.
- [x] Cover native input, timer shutdown, retirement, concurrent and nested transitions.
- [x] Record fixed versus expanding root-layout counts and bounded-frame timing.
- [x] Cover DPI messages and scroll containers during active expansion.

The DPI fixture injects messages into its own window.
Physical monitor changes, IME candidate windows, and representative application benchmarks remain acceptance work in phase 6.

#### Placement follow-up

- [ ] Define an overlay owner that does not reserve space in the parent layout.
- [ ] Specify z-order, occlusion, pointer clipping, focus return, and accessible offscreen state.
- [ ] Cover nested overlays and native surfaces that cannot share the clipping path.
- [ ] Add pane and navigation examples after the overlay contract is complete.

### 3. Tab insertion

- [x] Track tab presentation by stable tab ID inside `TabStrip`.
- [x] Add an explicit insertion-transition setting without per-tab HWNDs or timers.
- [x] Keep drawing, pointer targets, keyboard selection, and UIA bounds consistent.
- [x] Define interruption for rapid insertion, removal, reordering, and scrolling.
- [x] Add FileExplorer tab examples and bounded-work regressions.

### 4. Panes and navigation

- [x] Add opt-in pane entry through the shared animation scheduler.
- [ ] Define transitions for `SplitView`, `AdaptiveLayout`, and navigation expansion.
- [ ] Separate overlay navigation from navigation that resizes the content area.
- [ ] Preserve focus return, selected IDs, scroll positions, and native editor identity.
- [ ] Settle transitions on incompatible breakpoint or topology changes.

### 5. Authoring and lifecycle

- [ ] Add reusable transition descriptions after the first primitives stabilize.
- [ ] Keep interpolation in native code, not reactive C# updates for every frame.
- [ ] Define precedence between authored values, animation values, and control styles.
- [ ] Define cancellation and completion events only where applications require them.
- [ ] Cover hot reload, content replacement, stale completions, and detached controls.
- [ ] Keep unsupported native surfaces explicit rather than substituting screenshots.

### 6. Performance and broader presentation

- [ ] Record frame-time distributions, UI-thread work, native placements, and root layouts for representative applications.
- [ ] Record transient buffers, graphics allocations, and repeated-cycle resource retention.
- [ ] Cover high DPI, large windows, simultaneous transitions, native documents, and slow frame delivery.
- [ ] Require no animation-driven wakeups or repaints after completion.
- [ ] Consider damage tracking or cached surfaces only after measurements identify the dominant cost.
- [ ] Evaluate opacity and arbitrary transforms separately, including media and web-host limits.
- [ ] Consider compositor integration only if the existing renderer cannot meet the measured requirements.

Build procedures belong in [CONTRIBUTING](../../CONTRIBUTING.md).
Implementation details and dated evidence belong in the [maintainer notes](../llm/README.md).
