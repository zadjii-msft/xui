# Frame and menu validation history

This archive preserves earlier implementation reports and measurements from the former root README.
Results, limitations, tool paths, and artifact paths describe those runs, not the current checkout.
Local `build` artifacts are not part of the repository and can be absent.
Use [CONTRIBUTING](../../CONTRIBUTING.md) for current build instructions.

### Menu checks and captures

#### Shared rounded frame

`src/context_menu.cpp` owns the frame for all `Control::on_context_menu` popups, including the C# FileExplorer menus.
The backend disables the native theme so that Windows does not paint a conflicting frame.
The earlier replacement used `FrameRect`, which left the outer frame square despite rounded selection highlights.

The replacement uses an eight-DIP rounded window region and a matching one-pixel `FrameRgn` border.
High contrast retains a rectangular region.
`WM_WINDOWPOSCHANGED` updates the region after native menu placement and size changes.
The size cache prevents recursion through `SetWindowRgn`.
Windows owns the clipping region. The active menu owns a separate border region and releases it after dismissal.

The border can extend into the client area at higher DPI.
Item drawing includes the border in the native item DC, including buffered drawing.
`WM_PAINT` restores the complete border after native row painting.
`WM_PRINT` intersects its destination clip with the same outline and restores the caller's drawing context.
The backend still uses `HMENU`, the native popup, and the native menu loop.
It does not add a layered window, a Direct2D target, or an idle timer.

`tests/menu_tests.cpp` checks all four cutouts, DPI-scaled geometry, and the complete painted border.
Its capture checks retain an external backdrop outside the region, including after first-row and last-row selection.
The existing fixture also covers menu input, accessibility, cancellation, resource cleanup, and native EDIT selection.
The `--geometry` mode opens menus without activation and uses no keyboard or mouse injection.
It covers all three palettes, injected DPI scales, resize, repaint, and repeated resource cleanup.
It checks the real window region and `WM_PRINT` pixels, not compositor antialiasing or shadow parity.

September 18, 2026 evidence uses the ARM64 Release build in `build\menu-corners`, with Windows SDK 10.0.26100.0.
The final build of `xui`, `xui_menu_tests`, and `xui_shell_menu_tests` succeeded.
The `xui_menu_geometry_tests`, `xui_shell_menu_tests`, and `xui_shell_menu_latency_tests` checks passed.
The geometry check includes fifty open/close cycles without retained GDI or USER resources.
Its foreground-window assertion also passed.

The full foreground fixture did not complete on the final implementation.
Its input guard reported an Edge process as the foreground owner, rather than the fixture process.
The guard remains unchanged. These results do not establish complete foreground keyboard acceptance.
Experimental compositor capture attempts also failed to provide a complete result, including process termination and an EDIT-selection failure.
The committed geometry check does not use those attempts as visual evidence.

#### Earlier square-frame baseline

The ARM64 menu test covers three palettes and 96, 144, and 192 DPI.
Pixel assertions read the visible popup border, margin, and selected row.
The test also covers UIA names, focus, disabled invocation, checked state, native EDIT focus, IME messages, and pending suggestion cancellation.
Lifecycle cases cover callback exceptions, public-window deletion, host closure, and a reentrant menu command.
Fifty open/close cycles retained no extra GDI or USER objects. Closed menus produced zero idle paints and zero Direct2D targets.
Work-area checks covered all four monitors on the test desktop.
The DPI tests inject drawing scales. They do not change monitor configuration.
The high-contrast test uses current system colors. Physical high-contrast and screen-reader sessions remain manual checks.

The final ARM64 build passed all 25 CTest tests, including 1,014 menu assertions and 347 C ABI assertions.
The explorer smoke passed 1,930 assertions. The Task Manager desktop smoke passed 328 assertions.
All five binding clients passed both regular and callback-failure runs.
Those clients use copies under `build\menus\bindings`, with the new DLL. Older application deployments remain unchanged.
The first sampled themed popup appeared after 16.5 ms.
Across 50 warm cycles, sampled visibility averaged 23.3 ms, with a 35.2 ms 95th percentile.
The samples use a 10 ms observation interval and include Windows menu work.
`build\menus\menu-timing.json`, `ctest-final.log`, and the client logs contain the measurements and results.

Run these commands from the repository root:

```powershell
$cmake = 'C:\Program Files\Microsoft Visual Studio\2022\Preview\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ctest = Join-Path (Split-Path $cmake) 'ctest.exe'
& $cmake -S . -B build\menus -G 'Visual Studio 17 2022' -A ARM64 -DXUI_DESKTOP_TESTS=ON
& $cmake --build build\menus --config Release --parallel 4
& $ctest --test-dir build\menus -C Release --output-on-failure
& build\menus\Release\xui_menu_tests.exe build\menus\menu-captures
```

The menu test writes owned-window BMP captures to `build\menus\menu-captures`.
Files include `before-native.bmp`, `dark-96.bmp`, `light-144.bmp`, and `contrast-192.bmp`.
The explorer and Task Manager smoke tests write menu captures beside their build's `Release` directory, under `captures`.
Current application captures are `build\menus\captures\explorer-menu.bmp` and `build\menus\captures\task-manager-menu.bmp`.
Before-change application captures are under `build\menus\before\captures`. Those runs use copies of the previous `build\columns` executables.

## Complete text frames

The previous host presented its cleared Direct2D target before it repainted nested native fields and captions.
Transparent viewport HWNDs did not protect these descendants through `WS_CLIPCHILDREN`.
A frame-boundary capture reproduced missing native text in all 72 original frames. Custom labels and buttons remained visible.

The host now includes native pixels before `EndDraw`.
Each visible native region has a reusable bitmap, not a render target.
One GDI scratch buffer serves these regions. Each frame refreshes the pixels from the real native control.
Ancestor viewports limit the copied regions. Hidden regions release their bitmaps.
There is no window-sized CPU copy, full-target readback, additional presentation, or repaint timer.
Native EDIT still owns text, selection, caret, undo, TSF, and accessibility.

Unchanged caption text and placeholder properties no longer cause native repaint requests.
An empty EDIT paints its background and placeholder through one small buffered blit.
The regression captures the desktop immediately after presentation, before any subsequent child paint.
It observed zero missing glyph regions across 1,323 frames with status changes, hover changes, and asynchronous thumbnail arrivals.
Coverage includes three themes, injected 96/144/192 DPI, scroll clipping, split visibility, native text changes, target loss, caret pixels, and repeated closure.
Only the regression uses `DwmFlush`. Physical mixed-monitor transitions and interactive IME candidates still need manual coverage.

Three browser runs used the same initial size and measurement procedure:

| Measurement | Original | Complete frames |
| --- | ---: | ---: |
| Warm private commit | 29.61 MiB | 30.29 MiB |
| Warm private working set | 15.36 MiB | 16.00 MiB |
| Warm total working set | 45.87 MiB | 46.62 MiB |
| Input-and-paint latency | 16.74 ms | 17.83 ms |
| Startup to first paint | 196.47 ms | 189.17 ms |
| Demo executable | 637,952 bytes | 645,632 bytes |

Both versions produced zero additional idle paints and retained one root target.
The known Qualcomm allocation increase near 865×780 client pixels remains. This change does not correct that driver behavior.
A full-target GDI-compatible prototype removed the gap but increased the larger-window paint cost.
The final implementation uses small native bitmaps instead.
All 23 native tests and five binding clients passed, including their callback-failure cases.
Logs, frame samples, and measurements are in `build\flicker`. The updated demo is `build\flicker\Release\xui_demo.exe`.

## Command and navigation delivery

The command/navigation tests cover callback exceptions, host closure, public-window deletion, stale queries, and separate pin identity.
Nine native cases cover dark, light, and high contrast at injected 96, 144, and 192 DPI.
Each case checks stable USER resources across 24 popup cycles and one root render target.
Direct native print tests verify the popup region mask for EDIT and STATIC controls.
Full-window `PrintWindow` fixture images can still show underlying native child pixels.
They are not proof of live compositor occlusion. The gallery has separate popup captures.
The document batch resolves this check with an independent owned-window compositor capture.
This batch does not claim a committed-memory reduction.

The September 12 command/navigation matrix passed 12 of 13 tests.
One foundation target-recreation assertion failed. The subsequent six-test recheck passed, including foundation, collections, navigation, gallery, and explorer tests.
The assertion was not weakened.
See `build\controls\navigation-final-targeted.log`, `navigation-final-recheck.log`, and `navigation-delivery.json` for the earlier results.
The document delivery record contains the later live-frame evidence.

## Document composition evidence

Native document pixels join the existing root frame before `EndDraw`.
The compositor test uses a uniquely colored underlying EDIT and STATIC, then opens a covering popup.
Four immediate root presentations contain no underlying ink. The native field still edits correctly after dismissal.
When foreground activation is blocked, the test uses Windows Graphics Capture with the owned HWND, not a desktop capture.
The corresponding full-window `PrintWindow` image still shows underlying pixels. It is a capture artifact, not the live compositor result.
The evidence is in `build\controls\documents-captures` and `build\controls\documents-delivery.json`.
Tests also cover native UIA, theme/DPI cases, target loss, callback closure, and repeated resources.
Physical monitor changes, interactive IME candidates, and screen-reader speech remain manual checks.
This batch adds no language bindings and makes no process-memory reduction claim.
