# Vector scenes, maps, and native hosts

Build and test commands are in [CONTRIBUTING](../../CONTRIBUTING.md).
See the [reference index](README.md) for related APIs.
Examples in this reference use C++ unless stated otherwise.
For C# and Rust coverage, use the [binding reference](bindings.md).
See [CONTRIBUTING](../../CONTRIBUTING.md#optional-webview2) for the optional WebView2 build.

## Vector scenes and offline maps

`include\xui\vector_canvas.hpp` defines the public retained scene API.
`VectorScene` owns immutable `VectorShape` records with nonzero, unique `ShapeId` values.
Paths contain straight segments. The rectangle helper creates four vertices, and the ellipse helper creates 64 vertices.
Closed paths use an even-odd fill. Open paths have no fill.
Colors use unpremultiplied sRGB components between zero and one.

Each shape has an affine transform and an optional axis-aligned clip.
The scene applies transforms once, at construction. Clips use canvas coordinates after the transform.
Stroke widths remain in DIPs after the transform. Rounded stroke ends and joins match the hit-test distance calculation.
Scene coordinates must be finite, with an absolute value of at most one billion.
Hit testing examines the topmost interactive shape first and applies the same clip.

```cpp
#include "xui/vector_canvas.hpp"
auto shape = xui::VectorShape::rectangle(10, {20, 20, 120, 80});
shape.fill = {0.1f, 0.4f, 0.9f, 1};
shape.name = L"Blue region";
shape.interactive = true;
shape.transform = {1, 0, 0.2, 1, 10, 0};
shape.clip = xui::Rect{0, 0, 320, 200};
auto canvas = std::make_shared<xui::VectorCanvas>();
canvas->set_scene(std::make_shared<const xui::VectorScene>(
    std::vector<xui::VectorShape>{shape}));
canvas->on_select([](xui::ShapeId id) { /* application selection */ });
```

A scene supports 4,096 shapes, 65,536 total vertices, and 256 interactive entries.
Names have at most 256 UTF-16 units. Strokes have a maximum width of 256 DIPs.
The root renderer caches geometry for at most eight scene snapshots. It creates no additional Direct2D render target.
Snapshot replacement normally causes paint invalidation. The first or last interactive entry also changes the list layout.
Selection retains a stable ID across scene replacement.

The named list below the scene is its semantic alternative.
It exposes virtual UIA SelectionItem children and ordinary keyboard navigation without one HWND per shape.
Its rows represent semantic elements, not the spatial bounds of the drawing.
Selection through the list changes the same scene selection and outline as a pointer hit.
Decorative shapes have no automation actions.

`include\xui\map_view.hpp` defines `MapView`, `GeoPoint`, `WorldPoint`, `MapMarker`, `MapPolyline`, and `MapOverlay`.
The map shows a Mercator graticule and application-authored coordinates.
It has no street basemap, commercial tiles, copied world assets, geocoding, routing, or implicit network access.
It is an offline coordinate map, not a mapping-service replacement.
The renderer uses `VectorCanvas` and the same root target.

Longitude wraps across the dateline. Latitude clamps to approximately ±85.051129 degrees.
Projection uses double-precision normalized world coordinates. Zoom clamps to the range zero through 20.
Drag or arrow keys pan the viewport. Plus and minus zoom, and Home resets the viewport.
`zoom_at` preserves the geographic point under its DIP anchor, except at the latitude clamp.
The viewport displays the nearest wrapped world copy of each marker.

`set_overlay` accepts at most 256 markers, 256 polylines, and 2,048 total polyline points.
Marker IDs occupy the nonzero lower half of the 64-bit ID range.
Selected marker IDs survive pan and zoom. The marker list supplies keyboard and UIA selection.
UIA help exposes the current latitude, longitude, zoom, and provider error.
The application supplies meaningful marker names, including coordinates when necessary.

`on_request` and `request_overlay` define the optional provider boundary.
Each `MapRequest` contains a generation, center, zoom, and owner-specific stop token.
Pan, zoom, source replacement, hiding, and `cancel_request` cancel the old token.
`complete` rejects foreign, obsolete, canceled, or duplicate responses.
The application runs provider work and marshals completion to the UI thread.
XUI does not create a provider worker or implement network tile loading.

## Native media and optional web content

`include\xui\runtime_hosts.hpp` contains platform-independent models. No public method exposes COM interfaces or native renderer objects.
`MediaPlayback` loads only an explicitly supplied absolute local drive path.
It rejects URL, UNC, and alternate-stream syntax. It never selects a file or starts playback automatically.
The Windows backend loads the system `mfplay.dll` only after `load_local`.
MFPlay is a legacy Media Foundation API, not DirectShow or a bundled codec library.

`play`, `pause`, `stop`, `seek`, `set_volume`, `refresh`, and `unload` operate on the real native player.
Volume ranges from zero to one. Seek uses seconds and clamps to the current duration.
The public seek limit is seven days. `refresh` reads position and duration without a periodic UI timer.
Native callbacks update playback state and errors through a bounded 16-event mailbox.
Source replacement revokes the old mailbox before player shutdown.
Windows owns codec selection, audio output, and the native video renderer.

The host renders video in a native child window.
It does not replace video with a thumbnail or capture each frame into the root renderer.
Windows codec allocations and noninterruptible native calls are outside XUI memory and cancellation bounds.
The API does not implement camera capture, DRM, streaming URLs, subtitles, or recording.
Camera access never starts as a side effect.

Separate `Button` and `RangeInput` controls provide accessible Play, Pause, Stop, Seek, and Volume actions.
The gallery demonstrates this composition and a live status element.
The native host itself exposes a named group, not unsupported transport patterns.
The generated WAV and uncompressed AVI fixtures contain only repository-authored samples.
They load and play through the Windows codec stack.

`WebContent` requires the [opt-in build](../../CONTRIBUTING.md#optional-webview2) and an installed Microsoft Edge WebView2 runtime.
The default build has no WebView2 dependency. An enabled build still creates no environment before an explicit load.
XUI never installs the browser runtime or copies a complete browser distribution.
The native browser owns DOM rendering, editing, and browser accessibility.
`focus_content` enters the browser. WebView2 moves focus back to the XUI traversal order at its boundary.

`set_profile_root` requires an explicit absolute local directory.
Each environment uses a unique owned child directory under that root.
`set_html` accepts at most 262,144 UTF-16 units of owned HTML.
XUI adds a restrictive content policy before the HTML. Inline scripts and styles are allowed, but network connections, frames, workers, and form submission are blocked.
`navigate` accepts only `about:blank` or an exact HTTPS origin from `set_allowed_origins`.
The allowlist has at most 16 entries. An origin contains a scheme and authority, without a trailing slash.
Navigation and ordinary resource requests receive the same origin check.
This policy is not a network firewall for arbitrary remote applications or the browser runtime itself.

New windows, downloads, and permission requests are blocked.
Developer tools, browser accelerator keys, and default context menus are disabled.
`evaluate` accepts an explicit application script and returns its JSON result or error.
At most eight scripts can be pending. Script text and results each have a 65,536-unit limit.
At most eight environment/controller creation requests can be pending across source changes.
Unload or source replacement discards pending script callbacks. Obsolete callbacks cannot restore an old controller or document.
`stop`, `reload`, and `unload` operate on the actual browser.

Hiding a runtime host, its page, or its window unloads its native resources.
A fully scrolled-out host also unloads. A later show does not resume the old source.
The application must issue another explicit load.
Native cleanup tracks each owned profile through asynchronous environment and controller creation.
It keeps a process handle for the owned browser until that process exits.
After window teardown, `Application::run` dispatches STA completion messages before COM shutdown.
This cleanup has a 30-second limit. A timeout returns an error instead of reporting successful disposal.
`BrowserProcessExited` reports completion for the browser and its associated runtime processes.
Cleanup removes each retired profile after its creation callbacks and that process collection complete.
At most eight locked retired profiles can accumulate per host before further loads report an error.
Applications can remove their dedicated profile root after all owned runtimes exit.
Tests remove only their own fixture roots.

Native media and web surfaces are not part of the root bitmap composition.
`Window::show_popup` rejects a retained popup while a native runtime is active.
Tooltips are suppressed while a media or web runtime is active. An adaptive retained overlay unloads the runtime.
This explicit boundary prevents false claims about `WM_PRINT` composition or popup occlusion.
Ordinary native parent clipping handles scroll viewports.
The document, EDIT, and RichEdit composition paths remain unchanged.
