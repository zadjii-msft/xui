# Images and native hosts

[Control catalog](README.md) · [Image contract](../images.md) · [Scene and host contract](../scenes-and-hosts.md)

Examples use the [shared fragment context](README.md#use-the-examples).
For C++, add the header named in each section at file scope.
Example file paths are application inputs, not files that XUI creates.

The `.xui` compiler has no image, scene, map, or runtime-host node names.
Each `.xui` example requires C# to create and configure the existing element before `Content` embeds it.
Each component attaches its own Stack root to the window.
Do not add its mounted elements to another parent.
Use each example independently on the window's UI thread.
The C# fragments require `using System;` and `using Xui;` at file scope.
The Rust fragments require `use xui::*;` and the fallible function context from the shared setup.
Rust callbacks retain weak handles and upgrade them only for the callback.
The binding tabs retain the same native runtime, ownership, and cancellation requirements as C++.

## Image

Use `Image` for asynchronous file decoding with a bounded display size.
Use Button icons for standard vector actions.
Use collection visual metadata for virtual row images.

Add `xui\image.hpp`.

{% tabs %}
{% tab title=".xui" %}

```xui
namespace ControlExamples;
component WorkspacePreview {
    param global::Xui.Element Image;
    param global::Xui.Element Unload;
    view {
        VStack() {
            Content(Image);
            Content(Unload);
        }
    }
}
```

Create and configure the image in C# before composition:

```csharp
var image = window.Image("Workspace preview")
    .Source(@"C:\Data\Preview.png", 320, 180).PreferredSize(320, 180);
var unload = window.Button("Unload image");
unload.Click += () => image.Unload();
var view = new ControlExamples.WorkspacePreview(window, image, unload);
```

{% endtab %}
{% tab title="C#" %}

```csharp
var image = window.Image("Workspace preview")
    .Source(@"C:\Data\Preview.png", 320, 180).PreferredSize(320, 180);
anchor.Click += () => image.Unload();
root.Add(image);
```

{% endtab %}
{% tab title="Rust" %}

```rust
let image = window.image("Workspace preview")?;
image.source(r"C:\Data\Preview.png", 320, 180)?;
image.preferred_size(320., 180.)?;
let unload_image = image.downgrade();
anchor.on_event(move |event| {
    if event.kind == 1 {
        let Some(unload_image) = unload_image.upgrade() else { return Ok(()); };
        unload_image.image_source("", 192, 144).map_err(|error| {
            eprintln!("Image unload failed: {error}");
            error
        })?;
    }
    Ok(())
})?;
root.add(&image, 0.)?;
```

Event kind `1` is a click.
An empty source unloads the image through the upgraded element handle.

{% endtab %}
{% tab title="C++" %}

```cpp
auto image = std::make_shared<xui::Image>(L"Workspace preview");
image->set_source(L"C:\\Data\\Preview.png", {320, 180});
image->set_preferred_size({320, 180});
anchor->on_click([image] { image->unload(); });
root->add(image);
```

{% endtab %}
{% endtabs %}

The C# and Rust image bindings expose source, unload, and status APIs.
They do not expose image error text, reload, or `ImageResources` counters and cache operations.

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

{% tabs %}
{% tab title=".xui" %}

```xui
namespace ControlExamples;
component RegionDiagram {
    param global::Xui.Element Canvas;
    param global::Xui.Element Selected;
    view {
        VStack() {
            Content(Canvas, flex: 1);
            Content(Selected);
        }
    }
}
```

The C# prerequisite supplies the scene points and selection callback:

```csharp
var canvas = window.VectorCanvas("Region diagram");
canvas.SetScene([
    new VectorShape(1, [new(20, 20), new(180, 20), new(180, 100), new(20, 100)],
        Name: "Main region", Closed: true, Interactive: true,
        Fill: new SceneColor(0.1f, 0.4f, 0.9f, 1))
]);
var selected = window.Label("No region selected");
canvas.Event += e =>
{
    if (e.Kind == EventKind.Selection) selected.Text = $"Selected region {e.Value}";
};
var view = new ControlExamples.RegionDiagram(window, canvas, selected);
```

{% endtab %}
{% tab title="C#" %}

```csharp
var canvas = window.VectorCanvas("Region diagram");
canvas.SetScene([
    new VectorShape(1, [new(20, 20), new(180, 20), new(180, 100), new(20, 100)],
        Name: "Main region", Closed: true, Interactive: true,
        Fill: new SceneColor(0.1f, 0.4f, 0.9f, 1))
]);
var selected = window.Label("No region selected");
canvas.Event += e =>
{
    if (e.Kind == EventKind.Selection) selected.Text = $"Selected region {e.Value}";
};
root.Add(canvas, 1);
root.Add(selected);
```

{% endtab %}
{% tab title="Rust" %}

```rust
let canvas = window.vector_canvas("Region diagram")?;
canvas.set_scene(&[VectorShape {
    id: 1,
    points: vec![
        ScenePoint { x: 20., y: 20. }, ScenePoint { x: 180., y: 20. },
        ScenePoint { x: 180., y: 100. }, ScenePoint { x: 20., y: 100. },
    ],
    name: "Main region".into(),
    closed: true,
    interactive: true,
    fill: SceneColor { red: 0.1, green: 0.4, blue: 0.9, alpha: 1. },
    ..Default::default()
}])?;
let selected = window.label("No region selected")?;
let selected_label = selected.downgrade();
canvas.on_event(move |event| {
    if event.kind == 5 {
        let Some(selected_label) = selected_label.upgrade() else { return Ok(()); };
        selected_label.set_text(&format!("Selected region {}", event.value))
            .map_err(|error| {
                eprintln!("Region selection failed: {error}");
                error
            })?;
    }
    Ok(())
})?;
root.add(&canvas, 1.)?;
root.add(&selected, 0.)?;
```

Event kind `5` is a selection.

{% endtab %}
{% tab title="C++" %}

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

{% endtab %}
{% endtabs %}

The C# and Rust bindings accept point-based shapes with transforms and clips.
They do not expose the native rectangle and ellipse helpers or the `VectorScene` model.
Native scene construction still supplies the immutable scene and accessibility behavior.

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

{% tabs %}
{% tab title=".xui" %}

```xui
namespace ControlExamples;
component OfficeMap {
    param global::Xui.Element Map;
    view {
        VStack() {
            Content(Map, flex: 1);
        }
    }
}
```

Create the map and its markers in C# before composition:

```csharp
var map = window.MapView("Office locations").SetView(new(47.6, -122.3), 4);
map.SetMarkers([new MapMarker(1, new(47.6, -122.3), "Seattle office")]);
var view = new ControlExamples.OfficeMap(window, map);
```

{% endtab %}
{% tab title="C#" %}

```csharp
var map = window.MapView("Office locations").SetView(new(47.6, -122.3), 4);
map.SetMarkers([new MapMarker(1, new(47.6, -122.3), "Seattle office")]);
root.Add(map, 1);
```

{% endtab %}
{% tab title="Rust" %}

```rust
let map = window.map_view("Office locations")?;
let center = GeoPoint { latitude: 47.6, longitude: -122.3 };
map.set_view(center, 4.)?;
map.set_markers(&[MapMarker {
    id: 1, location: center, name: "Seattle office".into(),
}])?;
root.add(&map, 1.)?;
```

{% endtab %}
{% tab title="C++" %}

```cpp
auto map = std::make_shared<xui::MapView>(L"Office locations");
map->set_view({47.6, -122.3}, 4);
xui::MapOverlay overlay;
overlay.markers.push_back({1, {47.6, -122.3}, L"Seattle office"});
map->set_overlay(std::move(overlay));
root->add(map, 1);
```

{% endtab %}
{% endtabs %}

The C# and Rust bindings expose markers, not `MapOverlay` polylines.
Their request APIs return owned tokens instead of the native provider callback.
C# disposes an unfinished `MapRequest`. Rust cancels an unfinished request when its token drops.
Completion remains a UI-thread operation. No binding starts provider work or network access.

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

{% tabs %}
{% tab title=".xui" %}

```xui
namespace ControlExamples;
component TrainingPlayback {
    param global::Xui.Element Media;
    param global::Xui.Element Load;
    param global::Xui.Element Play;
    param global::Xui.Element Pause;
    param global::Xui.Element Stop;
    view {
        VStack() {
            Content(Media, flex: 1);
            Content(Load);
            Content(Play);
            Content(Pause);
            Content(Stop);
        }
    }
}
```

The C# prerequisite supplies the native host and explicit transport callbacks:

```csharp
var media = window.MediaPlayback("Training clip").SetVolume(0.5);
var load = window.Button("Load training clip");
var play = window.Button("Play");
var pause = window.Button("Pause");
var stop = window.Button("Stop");
load.Click += () => media.LoadLocal(@"C:\Media\Training.mp4");
play.Click += () => media.Play();
pause.Click += () => media.Pause();
stop.Click += () => media.Stop();
var view = new ControlExamples.TrainingPlayback(window, media, load, play, pause, stop);
```

{% endtab %}
{% tab title="C#" %}

```csharp
var media = window.MediaPlayback("Training clip").SetVolume(0.5);
var play = window.Button("Play");
var pause = window.Button("Pause");
var stop = window.Button("Stop");
anchor.Click += () => media.LoadLocal(@"C:\Media\Training.mp4");
play.Click += () => media.Play();
pause.Click += () => media.Pause();
stop.Click += () => media.Stop();
root.Add(media, 1);
root.Add(play);
root.Add(pause);
root.Add(stop);
```

{% endtab %}
{% tab title="Rust" %}

```rust
let media = window.media_playback("Training clip")?;
media.set_volume(0.5)?;
let play = window.button("Play")?;
let pause = window.button("Pause")?;
let stop = window.button("Stop")?;
let load_media = media.weak();
anchor.on_event(move |event| {
    if event.kind == 1 {
        let Some(load_media) = load_media.upgrade() else { return Ok(()); };
        load_media.load_local(r"C:\Media\Training.mp4").map_err(|error| {
            eprintln!("Media load failed: {error}");
            error
        })?;
    }
    Ok(())
})?;
let play_media = media.weak();
play.on_event(move |event| {
    if event.kind == 1 {
        let Some(play_media) = play_media.upgrade() else { return Ok(()); };
        play_media.play().map_err(|error| {
            eprintln!("Media play failed: {error}");
            error
        })?;
    }
    Ok(())
})?;
let pause_media = media.weak();
pause.on_event(move |event| {
    if event.kind == 1 {
        let Some(pause_media) = pause_media.upgrade() else { return Ok(()); };
        pause_media.pause().map_err(|error| {
            eprintln!("Media pause failed: {error}");
            error
        })?;
    }
    Ok(())
})?;
let stop_media = media.weak();
stop.on_event(move |event| {
    if event.kind == 1 {
        let Some(stop_media) = stop_media.upgrade() else { return Ok(()); };
        stop_media.stop().map_err(|error| {
            eprintln!("Media stop failed: {error}");
            error
        })?;
    }
    Ok(())
})?;
root.add(&media, 1.)?;
root.add(&play, 0.)?;
root.add(&pause, 0.)?;
root.add(&stop, 0.)?;
```

Event kind `1` is a click.

{% endtab %}
{% tab title="C++" %}

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

{% endtab %}
{% endtabs %}

The C# and Rust bindings expose host state and transport commands, including seek.
They do not expose refresh, position, duration, the retained status child, or the native host error getter.
Operation errors remain explicit exceptions or `Result` values.

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

{% tabs %}
{% tab title=".xui" %}

```xui
namespace ControlExamples;
component OwnedWebPreview {
    param global::Xui.Element Web;
    param global::Xui.Element Load;
    view {
        VStack() {
            Content(Web, flex: 1);
            Content(Load);
        }
    }
}
```

The C# prerequisite requires the optional WebView2 build and an installed runtime.
The application owns the profile directory.

```csharp
var web = window.WebContent("Document preview").SetProfileRoot(@"C:\XuiProfiles");
var load = window.Button("Load preview");
load.Click += () =>
    web.SetHtml("<h1>Document preview</h1><p>Owned application content.</p>");
var view = new ControlExamples.OwnedWebPreview(window, web, load);
```

{% endtab %}
{% tab title="C#" %}

```csharp
var web = window.WebContent("Document preview").SetProfileRoot(@"C:\XuiProfiles");
anchor.Click += () =>
    web.SetHtml("<h1>Document preview</h1><p>Owned application content.</p>");
root.Add(web, 1);
```

{% endtab %}
{% tab title="Rust" %}

```rust
let web = window.web_content("Document preview")?;
web.set_profile_root(r"C:\XuiProfiles")?;
let load_web = web.weak();
anchor.on_event(move |event| {
    if event.kind == 1 {
        let Some(load_web) = load_web.upgrade() else { return Ok(()); };
        load_web.set_html("<h1>Document preview</h1><p>Owned application content.</p>")
            .map_err(|error| {
                eprintln!("Web content load failed: {error}");
                error
            })?;
    }
    Ok(())
})?;
root.add(&web, 1.)?;
```

Event kind `1` is a click.

{% endtab %}
{% tab title="C++" %}

```cpp
auto web = std::make_shared<xui::WebContent>(L"Document preview");
web->set_profile_root(L"C:\\XuiProfiles");
anchor->on_click([web] {
    web->set_html(L"<h1>Document preview</h1><p>Owned application content.</p>");
});
root->add(web, 1);
```

{% endtab %}
{% endtabs %}

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
