# Swap chain panel

`SwapChainPanel` embeds an application-owned DirectX surface in XUI layout.
It uses DirectComposition, not a bitmap copy or a child application window.
The Windows-only C++ API is in `include\xui\swap_chain_panel.hpp`.
The C ABI is in `include\xui\xui_swap_chain.h`.
C# and declarative `.xui` expose the control. Rust has no typed wrapper.
Build and sample commands are in [CONTRIBUTING](../../CONTRIBUTING.md#swap-chain-sample).

## Attach a renderer

The panel accepts two kinds of content:

- `set_swap_chain(IDXGISwapChain*)` accepts a swap chain from `IDXGIFactory2::CreateSwapChainForComposition`.
- `set_swap_chain_handle(HANDLE)` accepts a surface handle from `DCompositionCreateSurfaceHandle`.

The pointer path requires `DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL`, `DXGI_SCALING_STRETCH`, and no multisampling.
It retains a COM reference to the swap chain.
The handle path imports the composition surface through `IDCompositionDesktopDevice::CreateSurfaceFromHandle`.
The caller retains handle ownership and can close the handle after the call returns.
A shared texture handle, HWND, or handle value from another process is not a composition surface handle.
Cross-process producers must explicitly duplicate the surface handle into the application process.

Either setter replaces the current content, regardless of its type.
Null detaches the current content and releases the panel's graphics resources.
The renderer retains ownership of its device, back buffers, rendering thread, and presentation.
XUI never calls `ResizeBuffers`, `Present`, or a swap-chain matrix transform.
It displays producer pixels at their native size and clips them to the panel and ancestor viewports.

This fragment uses an application-owned composition swap chain:

```cpp
#include "xui/application.hpp"
#include "xui/swap_chain_panel.hpp"

void add_renderer(xui::Stack& root, IDXGISwapChain* swap_chain)
{
    auto panel = std::make_shared<xui::SwapChainPanel>(L"Terminal display");
    panel->set_preferred_size({800, 480});
    panel->set_swap_chain(swap_chain);
    root.add(panel, 1);
}
```

Attachment operations and callbacks require the creating UI thread.
An `Application` instance initializes that thread for COM.
Before `Application::run(Window&)` starts, callers that create graphics resources must initialize COM themselves.
Content can attach before the native peer exists.
An unattached panel retains its content until explicit detachment or destruction.
The complete [sample](../../demo/swap_chain.cpp) creates its renderer in the first visible metrics callback.
It displays a rainbow triangle that rotates around its Y axis, with perspective, interpolated vertex colors, and a Pause button.
Its frame scheduler permits at most one pending UI callback and stops while the panel is hidden.

## Size and visibility

C# creates a panel through `Window.SwapChainPanel(name)`.
`SetSwapChain(nint)` accepts the COM pointer. `SetSurfaceHandle(nint)` accepts the composition surface handle.
`Metrics` returns physical dimensions, rasterization scale, and visibility.
`MetricsChanged` uses the existing window-owned, content-scoped callback lifetime and error contract.
`NativeWindow` returns the borrowed child HWND.
`VisiblePixelBounds` returns the visible rectangle in panel-local physical pixels.

The declarative leaf `SwapChainPanel("Terminal", ref: Display, flex: 1)` supports common control and layout arguments.
The C# controller uses the generated `Display` property for graphics operations and event subscriptions.
Markup does not store native pointers or handles.

The C ABI creates `XUI_SWAP_CHAIN_PANEL` through `xui_create`.
`xui_subscribe` reports changed metrics with `XUI_VIEW`.
The callback reads `xui_swap_chain_get_metrics`, with its structure size initialized.
It can also read `xui_swap_chain_get_visible_pixel_bounds`, with its separate structure size initialized.
This additive query does not change the layout of `xui_swap_chain_metrics`.
Native errors return an explicit status and `xui_error_copy` message.
Managed exceptions follow the existing callback-error path. They do not cross the unmanaged boundary.

`metrics()` returns the current `SwapChainPanelMetrics`.
`pixel_width` and `pixel_height` describe the full native client area, not its visible scroll intersection.
`rasterization_scale` equals DPI divided by 96.
`visible` reports whether the panel can display content.
The metrics do not indicate whether a swap chain is attached or whether a frame was presented.

`visible_pixel_bounds()` returns the same effective rectangle that clips the composition visual.
Its origin is the panel client area's top-left corner, not the screen or retained root.
Its coordinates preserve fractional physical pixels and stay inside the full physical client area.
Hidden, suspended, closed, unattached, or fully clipped hosts return an empty rectangle.
Like the existing metrics, this rectangle does not indicate whether producer content is attached.
To obtain screen coordinates, add the native client's physical screen origin without another DPI multiplication.

`on_metrics_changed` receives size, DPI, visibility, and visible-rectangle changes after native layout.
The first callback supplies the initial native metrics.
Clip-only changes also notify C# `MetricsChanged` and C ABI `XUI_VIEW` subscribers.
Callbacks do not repeat when both the metrics and visible rectangle stay unchanged.
A callback registered after attachment receives the next change, not an immediate replay.
The caller can read `metrics()` for the current value.

The renderer must use the pixel dimensions for its buffers.
It must not multiply those dimensions by `rasterization_scale` again.
A terminal adapter can use the scale separately for font metrics.
Zero-sized or invisible panels require no frame.
The renderer must release its back-buffer views before `ResizeBuffers` and handle its own device-loss errors.

Callbacks can request renderer work through an application-owned queue.
Worker threads must use `Window::post` for subsequent panel changes.
XUI does not stop or join renderer threads.
The application must cancel renderer work when its owner closes.

Hidden pages, minimized windows, invisible controls, and fully scrolled-out panels stop displaying their surface.
XUI retains their content and restores it when they become visible.
An adaptive overlay also hides the surface.
The metrics callback reports these visibility changes so the producer can pause rendering.

## Native composition boundary

The panel is a rectangular native surface.
It supports scrolling and normal layout, but not retained transforms, rounded masks, or retained controls over its pixels.
The panel has no XUI control-style schema.
The renderer supplies its own background, colors, and high-contrast presentation.

Retained popups cannot open while a swap-chain surface is active.
Tooltips remain hidden while a surface is active.
After the application detaches the surface, retained popups can open.
An attachment during an open popup does not display until that popup closes.
Native text inputs beside the panel keep their existing native editing behavior.

`WM_PRINT`, root bitmap capture, and Designer pointer inspection do not include the composition content.
A real compositor capture is necessary to inspect its pixels.

## Lifetime and errors

`native_window()` returns the borrowed input-peer HWND.
It returns null before native attachment and after owner teardown.
The application must not destroy, reparent, or retain this HWND after teardown.
This handle does not transfer XUI window ownership to the renderer.

Window closure and content replacement release attached panel content before XUI shuts down COM.
A retained panel then reports no content, a null HWND, and empty metrics.
Teardown does not call the metrics callback.
`Window::on_closed` supplies the owner notification.
Non-null attachment to a closed host throws until a new native host attaches that control.

Invalid inputs throw `std::invalid_argument` or a native error.
DirectComposition and DXGI failures throw `std::system_error` with the HRESULT value.
An exception in a metrics callback follows the existing window callback-error contract and closes the window.
XUI does not hide attachment failures or substitute a screenshot.
After producer device loss, null detachment releases the old composition device before a new attachment.

## Windows Terminal integration

Windows Terminal's Atlas renderer creates a composition surface handle.
`TermControl::_AttachDxgiSwapChainToXaml` passes that handle to `ISwapChainPanelNative2::SetSwapChainHandle`.
XUI's handle setter supplies the corresponding graphics boundary without XAML.
The upstream implementations are [AtlasEngine.r.cpp](https://github.com/microsoft/terminal/blob/9694946ae22420a30691b7610c31f5723d9a5ff8/src/renderer/atlas/AtlasEngine.r.cpp)
and [TermControl.cpp](https://github.com/microsoft/terminal/blob/9694946ae22420a30691b7610c31f5723d9a5ff8/src/cascadia/TerminalControl/TermControl.cpp).

This control does not embed Windows Terminal by itself.
A terminal adapter still needs renderer ownership, terminal sessions, keyboard and pointer input, IME/TSF, clipboard behavior, and terminal text accessibility.
The panel exposes a named UIA group, not a text provider.
It is not a tab stop by default.
The application can enable its tab stop, but that change does not implement terminal input or accessibility.

`set_native_input(true)` enables the tab stop and passes Tab and Page keys to an application-owned HWND input adapter.
C# uses `SetNativeInput(true)`. The C ABI uses `xui_swap_chain_native_input(panel, 1)`.
Window shortcuts and modal handling still run first.
The default is false. Disabling native input also disables the tab stop.
The application owns its HWND subclass and must remove it before the session or native peer closes.
This opt-in does not add IME, clipboard, pointer selection, or terminal text accessibility.
