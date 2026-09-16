# Images and native hosts

[Control catalog](README.md) · [Image contract](../images.md) · [Scene and host contract](../scenes-and-hosts.md)

Examples use the [C++ fragment context](README.md#use-the-examples).
Add the header named in each section at file scope.
Example file paths are application inputs, not files that XUI creates.

## Image

Use `Image` for asynchronous file decoding with a bounded display size.
Use Button icons for standard vector actions.
Use collection visual metadata for virtual row images.

Add `xui\image.hpp`.

```cpp
auto image = std::make_shared<xui::Image>(L"Workspace preview");
image->set_source(L"C:\\Data\\Preview.png", {320, 180});
image->set_preferred_size({320, 180});
anchor->on_click([image] { image->unload(); });
root->add(image);
```

`display_pixels` bounds decoded output pixels.
The element size defines layout in DIPs.
`status()` reports empty, loading, ready, or error.
`error()` provides failure text.
`reload` requests a new decode. `unload` releases the current source.
There is no public Image completion callback in this API.

The backend uses shared WIC workers, pixels, and bitmap caches.
Source changes and unload revoke obsolete delivery.
`ImageResources::statistics` exposes resource counters.
`ImageResources::clear_unused` removes unpinned decoded entries without file I/O or a decoder join.
The [image contract](../images.md) defines exact limits and cancellation guarantees.

The name describes the image for accessibility.
The style target is `image`.
Frame, placeholder, and error text support styles.
Decoded content colors and decode behavior remain image-owned.

## VectorCanvas

Use `VectorCanvas` for application-authored paths and shapes.
It is not an image, HTML, or SVG importer.

Add `xui\vector_canvas.hpp`.

```cpp
auto region = xui::VectorShape::rectangle(1, {20, 20, 160, 80});
region.fill = {0.1f, 0.4f, 0.9f, 1};
region.name = L"Main region";
region.interactive = true;
auto scene = std::make_shared<const xui::VectorScene>(
    std::vector<xui::VectorShape>{region});
auto canvas = std::make_shared<xui::VectorCanvas>(L"Region diagram");
canvas->set_scene(scene);
auto selected = std::make_shared<xui::Label>(L"No region selected");
canvas->on_select([selected](xui::ShapeId id) {
    selected->set_text(L"Selected region " + std::to_wstring(id));
});
root->add(canvas, 1);
root->add(selected);
```

Scenes own immutable shapes with unique, nonzero IDs.
Shapes support transforms and clips.
`VectorShape::ellipse` supplies the ellipse helper.
Scene construction applies transforms once.
`set_selected` sets the property. `select` performs a semantic selection.

Interactive shapes require meaningful names.
`accessible_items()` exposes a retained virtual collection as the keyboard and UIA alternative.
Decorative shapes have no automation actions.
The [scene contract](../scenes-and-hosts.md#vector-scenes-and-offline-maps) defines shape, point, coordinate, and interactive-entry limits.

The style target is `vector_canvas`.
Frame, selection highlight, and empty text support styles.
Shape fills and strokes remain scene data.
The target has no error part or error state.

## MapView

Use `MapView` for offline geographic coordinates and application-authored overlays.
It has a Mercator graticule, not a street basemap or mapping service.

Add `xui\map_view.hpp`.

```cpp
auto map = std::make_shared<xui::MapView>(L"Office locations");
map->set_view({47.6, -122.3}, 4);
xui::MapOverlay overlay;
overlay.markers.push_back({1, {47.6, -122.3}, L"Seattle office"});
map->set_overlay(std::move(overlay));
root->add(map, 1);
```

`pan`, `zoom_at`, `screen_point`, and `location_at` use the current view.
Longitude wraps. Latitude clamps to the Mercator limit.
Zoom stays between zero and 20.
The marker list supplies keyboard and UIA selection.
The application supplies meaningful marker names.

`on_request` and `request_overlay` define an optional provider boundary.
The application runs provider work and delivers `complete` on the UI thread.
View changes, source replacement, hide, and `cancel_request` cancel old tokens.
Completion rejects foreign, obsolete, canceled, or duplicate responses.
The control performs no implicit network access.

The style target is `map_view`.
Frame, coordinate text, error text, and selection highlight support styles.
Marker and polyline colors remain authored overlay data.

## MediaPlayback

Use `MediaPlayback` for an explicit absolute local audio or video file.
Construction does not load a runtime or start playback.

Add `xui\runtime_hosts.hpp`.

```cpp
auto media = std::make_shared<xui::MediaPlayback>(L"Training clip");
auto play = std::make_shared<xui::Button>(L"Play");
auto pause = std::make_shared<xui::Button>(L"Pause");
auto stop = std::make_shared<xui::Button>(L"Stop");
anchor->on_click([media] { media->load_local(L"C:\\Media\\Training.mp4"); });
play->on_click([media] { media->play(); });
pause->on_click([media] { media->pause(); });
stop->on_click([media] { media->stop(); });
media->set_volume(0.5);
root->add(media, 1);
root->add(play);
root->add(pause);
root->add(stop);
```

Only explicit local drive paths are accepted.
URLs, UNC paths, and alternate streams reject.
`seek` uses seconds. `set_volume` accepts zero through one.
`refresh` reads position and duration without a periodic UI timer.
`on_state` reports host state changes.
`error()` and the retained `status()` expose failures.

Separate controls supply accessible transport commands.
The host exposes a named group, not unsupported transport patterns.
Windows owns codecs, audio output, and native video pixels.
Native codec allocations and blocking calls are outside XUI cancellation bounds.

The style target is `media_playback`.
Frame, caption, placeholder, status, and error text support styles.
Styles do not recolor video frames.

## WebContent

Use `WebContent` for owned HTML or explicitly allowed HTTPS origins.
It requires the optional WebView2 build and an installed runtime.
XUI does not install that runtime.

Add `xui\runtime_hosts.hpp`.

```cpp
auto web = std::make_shared<xui::WebContent>(L"Document preview");
web->set_profile_root(L"C:\\XuiProfiles");
anchor->on_click([web] {
    web->set_html(L"<h1>Document preview</h1><p>Owned application content.</p>");
});
root->add(web, 1);
```

The profile root must be an explicit absolute local directory.
Use an application-owned location suitable for browser profile data.
Construction alone does not create a WebView2 environment.

For remote navigation, call `set_allowed_origins` before `navigate`.
Entries are exact HTTPS origins without a trailing slash.
The control rejects unlisted origins.
Owned HTML receives a restrictive content policy.
The policy is not a network firewall for the runtime or arbitrary remote applications.

`focus_content` enters native browser focus.
`evaluate` returns explicit application script results or an error.
C# and Rust use disposable native-owned result tokens instead of retained completion delegates.
`stop`, `reload`, and `unload` operate on the real browser.
Obsolete results cannot restore an old document.

The browser owns DOM rendering, editing, and accessibility.
The style target is `web_content`.
XUI frame and status styles do not change page CSS.
Playing and paused style states reject.
The [web contract](../scenes-and-hosts.md#native-media-and-optional-web-content) defines content limits, allowlists, script limits, and cleanup.

## Native-host boundaries

Media and web content use native child surfaces, not a bitmap in the shared XUI frame.
An active runtime makes `Window::show_popup` reject retained popups.
Tooltips are suppressed during active media or web runtimes.
Adaptive retained overlays unload the runtime.

Hide, page changes, window hiding, and full scroll-out unload native resources.
A later show does not resume the old source.
Issue another explicit load to resume content.

`RuntimeHost` supplies common state, error, status, and event APIs.
Its constructor is protected.
The media and web constructors are the application entry points.
Their retained InlineStatus remains a real child with its own target.

Window teardown tracks asynchronous browser cleanup.
The cleanup deadline is 30 seconds.
A timeout returns an error instead of a successful-disposal claim.
The [host contract](../scenes-and-hosts.md) defines native resource and profile ownership.

## Related contracts

- [Images, thumbnails, and Shell icons](../images.md)
- [Scenes, offline maps, and optional hosts](../scenes-and-hosts.md)
- [Exact host and scene style parts](../control-styling-inventory.md#commands-windows-and-hosted-content)
- [C# and Rust source, token, and disposal APIs](../bindings.md)
