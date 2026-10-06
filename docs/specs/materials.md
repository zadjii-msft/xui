# Mica and acrylic materials

Build and test commands are in [CONTRIBUTING](../../CONTRIBUTING.md).
See the [reference index](README.md) for related APIs.
Examples in this reference use C++ unless stated otherwise.

## Scope

XUI supports two kinds of material:

- A **window backdrop** asks Windows 11 to draw Mica, Mica Alt, or Desktop Acrylic behind the window.
- An **acrylic surface** is a `Stack` that blurs and tints the window pixels painted behind it.

Both are opt-in. Existing windows and stacks keep their solid appearance.
Neither material adds a timer or a continuous render loop.
High contrast always takes precedence over both materials.

## Window backdrops

```cpp
#include "xui/application.hpp"

xui::WindowOptions options{L"Notes", {720, 480}};
options.visual_style = xui::VisualStyle::winui;
options.backdrop = xui::WindowBackdrop::mica;
xui::Window window(options);
// Later, on the UI thread:
window.set_backdrop(xui::WindowBackdrop::acrylic);
const bool presented = window.backdrop_active();
```

| Value | Windows material | Intended use |
| --- | --- | --- |
| `WindowBackdrop::none` | None | The default solid theme background |
| `WindowBackdrop::mica` | `DWMSBT_MAINWINDOW` | Long-lived application windows |
| `WindowBackdrop::mica_alt` | `DWMSBT_TABBEDWINDOW` | Windows with tabbed title bars |
| `WindowBackdrop::acrylic` | `DWMSBT_TRANSIENTWINDOW` | Short-lived or secondary windows |

`WindowOptions::backdrop` selects the initial request.
`Window::set_backdrop` changes the request before Show or while the window is open.
It requires the window's UI thread and rejects a closed window.
`Window::backdrop` returns the request.
`Window::backdrop_active` returns `true` only while Windows presents the material.

The material appears where XUI does not paint.
That includes the window background, the system or custom title bar, and the gaps between controls.
Resting caption buttons, unstyled collection backgrounds such as the navigation pane, and unselected title-bar tab rails also show the material.
Surfaces, fields, data grids, and other filled controls remain opaque.
Place a text-heavy page on a `Stack` with `set_surface(true)` to keep its contrast independent of the wallpaper.
The control gallery uses this layout: the catalog and margins show the material, and each page is a surface.

While a backdrop is active, XUI:

- extends the DWM frame across the client area and clears unpainted pixels to transparent;
- leaves the caption color to Windows so that the material also covers the system title bar;
- draws text with grayscale antialiasing, because ClearType requires opaque destination pixels;
- repaints native EDIT, RichEdit, and date/time controls through an opaque buffer.

GDI writes zero alpha, so native editors would otherwise blend with the material.
The opaque repaint applies after each paint request and after input that can draw outside a paint request.
Native text input, selection, IME composition, the caret, and accessibility are unchanged.

### Fallback

XUI paints the solid theme background and reports `backdrop_active() == false` when:

- Windows is older than Windows 11 build 22621;
- **Transparency effects** is off in Windows settings;
- the window uses high contrast; or
- DWM rejects the material.

XUI reevaluates these conditions after theme, system setting, and DWM composition changes.
Rejected materials write a diagnostic to the debugger.
Other DWM errors use the window's error path.
Windows can also show a solid material, for example in battery saver or for inactive windows.
`backdrop_active()` reports the XUI request, not the pixels that DWM chooses to compose.

### Restrictions

A backdrop cannot be combined with `WindowOptions::transparent`.
The layered transparent window uses a separate software surface; see [borrowed HWND and transparent windows](application.md#borrowed-hwnd-and-transparent-windows).
Combining them throws `std::invalid_argument`.
Mica is the desktop wallpaper, not a blur of other windows.
Desktop Acrylic blurs the content behind the window.

## Acrylic surfaces

```cpp
auto flyout = std::make_shared<xui::Stack>(xui::Axis::vertical);
flyout->set_material(xui::SurfaceMaterial::acrylic);
flyout->set_padding({16, 12, 16, 12});
flyout->add(std::make_shared<xui::Label>(L"Recent files"));
```

`Stack::set_material(SurfaceMaterial::acrylic)` replaces the solid surface fill.
An acrylic stack always reports `surface() == true`.
`SurfaceMaterial::solid` restores the previous `set_surface` choice.

Each paint copies the pixels already drawn beneath the stack.
XUI then applies a Gaussian blur, a luminosity layer that keeps the hue behind the surface, a tint, and low-opacity noise.
The values follow the WinUI `AcrylicInAppFillColorDefault` resource:

| Theme | Tint | Tint opacity | Luminosity opacity | Fallback |
| --- | --- | --- | --- | --- |
| Dark | `#2C2C2C` | 0.15 | 0.96 | `#2C2C2C` |
| Light | `#FCFCFC` | 0 | 0.85 | `#F9F9F9` |

The surface uses the WinUI or Classic surface radius and border.
A root style can override the radius, border, and background.
An authored background covers the material.

Only pixels painted earlier in the same window can show through.
Use `Grid` cells or earlier siblings to place content behind an acrylic stack.
Over a window backdrop, the material tints the transparent pixels and the desktop material shows through.
Other application windows and pixels painted after the stack do not affect it.

Disabled transparency effects and high contrast paint the solid fallback color.
So does a graphics target that cannot supply Direct2D effects.
The blur runs only during a paint. Large acrylic areas increase the cost of every repaint of the window.

## Bindings

| Language | Window backdrop | Acrylic surface |
| --- | --- | --- |
| C++ | `WindowOptions::backdrop`, `Window::set_backdrop`, `backdrop_active` | `Stack::set_material` |
| C ABI | `xui_window_backdrop_set`, `xui_window_backdrop_get` | `xui_stack_material` |
| C# | `Window.SetBackdrop`, `Backdrop`, `BackdropActive` | `Stack.Material` |
| Rust | Not available | Not available |
| `.xui` | Not available | Not available |

The C functions are in `xui_layout.h`.
`xui_window_backdrop_set` accepts `XUI_BACKDROP_NONE`, `XUI_BACKDROP_MICA`, `XUI_BACKDROP_MICA_ALT`, and `XUI_BACKDROP_ACRYLIC`.
It returns `XUI_INVALID_ARGUMENT` for another value or a transparent window.
`xui_window_backdrop_get` writes the request and the active state; either output can be null.
`xui_stack_material` accepts `XUI_MATERIAL_SOLID` and `XUI_MATERIAL_ACRYLIC`.

```csharp
window.SetBackdrop(WindowBackdrop.Mica);
var flyout = window.Stack().Material(SurfaceMaterial.Acrylic);
flyout.Add(window.Label("Acrylic surface"));
bool presented = window.BackdropActive;
```

## Samples and tests

The control gallery's **Mica and acrylic** page switches the window backdrop and toggles an acrylic surface.
Start the gallery with `--backdrop mica`, `--backdrop mica-alt`, or `--backdrop acrylic` to select an initial material.
`xui_backdrop_window_tests` checks the acrylic pixels, fallback, clipping, DWM attributes, and opaque native pixels.
The C ABI and C# window tests cover argument validation, threading, and runtime changes.
`xui_backdrop_window_tests` needs an unlocked interactive desktop for its window captures.
