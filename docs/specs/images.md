# Images and thumbnails

Build and test commands are in [CONTRIBUTING](../../CONTRIBUTING.md).
See the [reference index](README.md) for related APIs.
Examples in this reference use C++ unless stated otherwise.
For C# and Rust coverage, use the [binding reference](bindings.md).

## Images and resource limits

`include\xui\image.hpp` provides the Windows image API. Link the application to `xui_windows`.
The gallery contains a native path field, Load image and Unload image buttons, and a reusable image preview.
The thumbnail sample uses the same `Image` control. It has no application drawing calls or private backend includes.

```cpp
#include "xui\image.hpp"

auto image = std::make_shared<xui::Image>(L"Workspace photograph");
image->set_preferred_size({320, 180}); // Layout size in DIPs.
image->set_source(L"C:\\Pictures\\workspace.jpg", {640, 360}); // Decode box in physical pixels.
content->add(image);

// Later, on the UI thread:
image->reload(); // Read the file version again.
image->unload(); // Cancel pending work and clear the source.
auto usage = xui::ImageResources::statistics();
xui::ImageResources::clear_unused();
```

`ImageSize` contains unsigned 32-bit `width` and `height` fields.
The decoder fits the first frame inside this box and preserves its aspect ratio.
The decoder does not enlarge small source images. The renderer centers and scales the result inside the arranged control.
The default decode box is 192 by 144 pixels. The decode box is independent of the layout size and monitor DPI.
For a different physical resolution, call `set_source` with a different box.

`source`, `source_kind`, `display_pixels`, `revision`, `status`, and `error` expose the current request state.
Resource statistics are thread-safe. Image control properties still belong to the UI thread.
`ImageStatus` has `empty`, `loading`, `ready`, and `error` values.
`ready` means that decoded pixels are available. The next frame uploads those pixels.
An upload error changes the state to `error`. The error text also appears in the accessible image name.
The control exposes UIA Image semantics and ancestor `ScrollItemPattern`. It does not advertise a keyboard action.

The backend starts a request only for a visible, attached image.
A fully clipped image releases its request, pixels, and bitmap on the next host update.
Its source remains available for a later reveal. Its state becomes `empty` until that reveal.
`unload` also clears the source. Property changes take effect through the normal coalesced host update.
A source revision and a cancelled mailbox prevent an obsolete completion from replacing a recycled tile.
Only the UI thread changes control state or uses Direct2D.

One process-wide WIC worker initializes COM as MTA and owns its WIC factory.
The worker starts with the first WIC image request. Text-only browser folders instead use the separate Shell worker.
File access, path resolution, metadata queries, and WIC operations stay on this worker.
The worker checks cancellation before decoding, after metadata, before allocation, before pixel conversion, after pixel transfer, and before delivery.
WIC codecs can still decode the full source internally. A codec call or filesystem driver can ignore cancellation until that call returns.

The shared service retains at most two active jobs and 64 queued jobs.
Only one WIC job and one Shell job can run at a time.
New requests remove cancelled queued jobs before the queue-limit check.
A full queue returns an error. It does not start another worker or silently use a different image.
Window closure cancels delivery and clears its image references without a worker join.
The workers and their shared service object last until process exit. Windows reclaims them without a C++ shutdown join.
Repeated windows do not create retired decoder threads or an unbounded shutdown queue.

| Limit | Policy |
| --- | --- |
| Owned decoded pixels | 8 MiB across the process, including in-flight reservations |
| Controlled bitmap estimate | 8 MiB across the process, including upload reservations |
| Decoded cache | At most 128 entries, least-recently-used eviction of unpinned entries |
| Bitmap cache | At most 128 entries per target, subject to the shared byte limit |
| Requests | One WIC worker and one Shell worker, two active jobs, and at most 64 queued jobs |
| Output box | Each dimension must be between 1 and 1,024 pixels |
| Source dimensions | At most 16,384 per axis and 16,777,216 pixels in total |
| Encoded file | Nonempty, at most 32 MiB |
| Request path | At most 32,767 UTF-16 units, with no embedded NUL |

Invalid output dimensions and invalid path strings throw `std::invalid_argument` from `set_source` and `set_shell_source`.
File, codec, queue, pixel-budget, and bitmap errors produce an error state.
The backend checks dimensions with 64-bit arithmetic before multiplication or allocation.
If pinned resources fill a budget, unload other images.
Then call `reload` on the image that failed.
The backend never substitutes a success result at a lower requested resolution.

The cache key includes the normalized final path, volume and file identity, last-write timestamp, file length, source kind, and requested decode box.
Every new request opens the file and checks that key on the worker.
The open handle denies concurrent writes during metadata access and decoding.
Different sizes have separate cache entries. Identical keys share immutable premultiplied BGRA pixels and one bitmap per target.
`reload` checks the key again. It is not a file watcher or a content-hash check.
Changes that preserve file identity, length, and timestamp can reuse cached pixels.

Pixel reservations cover the output allocation before the worker allocates it.
The pixel counter includes decoded cache entries, pending results, and live controls without double counting shared resources.
Bitmap reservations cover `CreateBitmap` before the upload completes.
The bitmap counter uses width times height times four bytes. It is a controlled estimate, not measured GPU residency.
The counters exclude allocator overhead, file paths, WIC buffers, encoded data, Direct2D internal storage, render targets, and driver allocations.
These limits are not a process-memory ceiling.

Visible controls pin their decoded resources. Cache eviction cannot release those resources.
`clear_unused` removes unpinned decoded entries. The renderer removes bitmaps that no visible peer retains.
Window closure clears the decoded cache and releases its target-owned bitmaps.
Device loss releases all bitmaps for that target. The next frame recreates them from retained pixels without another file decode.
Settled images have no timer, animation loop, or periodic repaint.

The current image path does not apply EXIF orientation, ICC color management, or animation.
The sample enumerates PNG, JPEG, BMP, GIF, and TIFF extensions.
Installed WIC codecs determine actual format support. Automated fixtures cover PNG and malformed BMP data.
Real codec cancellation, physical GPU loss, mixed-monitor image quality, and screen-reader speech still need manual coverage.

## Standalone Shell images

`Image::set_shell_source(path, display_pixels)` requests a Shell thumbnail for a file or folder.
If the Shell has no thumbnail, the same request uses its icon.
The existing `set_source` method still uses WIC, without a Shell fallback.

```cpp
auto preview = std::make_shared<xui::Image>(L"Selected file");
preview->set_preferred_size({160, 160}); // Layout size in DIPs.
preview->set_shell_source(path, {160, 160}); // Physical pixels at 96 DPI.
content->add(preview);
```

The output box uses physical pixels, not DIPs.
A 160-DIP preview at 192 DPI needs a 320-by-320-pixel request.
The renderer preserves aspect ratio inside the control.
The default box and validation limits match `set_source`.

`source()` retains the supplied path for both methods.
`source_kind()` returns `ImageKind::wic` or `ImageKind::shell`.
A kind change advances the revision even with the same path and dimensions.
The next host update cancels the old request and releases its pixels.
An obsolete completion cannot replace the new source.
`reload` retains the kind, and `unload` clears the source.

Standalone Shell images use the same lazy STA worker, cache, cancellation checks, and resource budgets as row thumbnails.
The UI thread does not query file metadata or call Shell image interfaces.
Clipping, visibility, closure, and error states follow the existing `Image` contract.
A missing thumbnail is not an error when the Shell supplies an icon.
Missing files and failed Shell extraction produce an error state.
The Shell handler limits described in the next section also apply.

## Explorer thumbnail icons

### Native window icons

`Window::set_icon_source(path)` uses the shared Shell thumbnail and icon service.
`Window::on_icon_error(callback)` reports asynchronous errors on the UI thread.
The C ABI exposes `xui_window_set_icon_source` and `xui_window_on_icon_error`.
C# exposes `Window.SetIconSource(path)` and `Window.IconErrorHandler`.

The source can identify a folder or a file. An empty path clears the native window icon.
XUI requests a Shell thumbnail first and a Shell icon second.
The Shell worker performs file access and decoding, with the same cancellation, queue, cache, and pixel limits as other Shell images.

The setter accepts calls before `Run` and during `Run`, on the creating UI thread.
Before `Run`, it retains the path without decoding.
A new source cancels the previous request and clears both native icon slots.
Cancelled requests cannot deliver stale pixels or errors. A DPI change requests a new physical icon size.
The setter rejects embedded NUL characters and paths longer than 32,767 UTF-16 units.

XUI creates and owns the HICON that the small and large HWND icon slots use.
Replacement and closure clear both slots before XUI destroys its handle.
XUI does not destroy handles that another component supplies.
Closure cancels pending delivery without a worker join.
Asynchronous errors leave the native icon clear and call the error handler once.
The normal callback error contract applies if the handler throws.

The C# FileExplorer requests the committed directory of the active pane and tab.
Its notification area reports icon errors. Pending navigation does not change the icon source.

### File list icons

`FileList` provides optional thumbnail icons through the C++ API.
The explorer enables this option in both panes. Other `FileList` clients retain vector icons by default.
The C ABI and the C# and Rust wrappers remain unchanged.

```cpp
auto files = std::make_shared<xui::FileList>();
files->set_thumbnails(true);
files->on_thumbnail_error([](xui::ItemId id, const std::wstring& error) {
    // Report a failed visible request without a modal dialog.
});
files->reload_thumbnails();
files->set_thumbnails(false);
```

The backend uses each `FileItem::path` as the visual source.
The direct WIC path supports `.png`, `.jpg`, `.jpeg`, `.bmp`, `.gif`, `.tif`, `.tiff`, and `.webp`.
Extension matching ignores case. Paths retain their Unicode characters.
WebP requires an installed WIC codec. GIF and TIFF use the first frame only.
The existing WIC image limits, codec restrictions, and orientation limitations also apply here.

All other extensions and folders use `IShellItemImageFactory::GetImage`.
XUI first requests `SIIGBF_THUMBNAILONLY`. If no thumbnail is available, XUI requests `SIIGBF_ICONONLY`.
EXE, DLL, shortcut, document, PDF, and unknown extensions no longer have an extension filter.
Available thumbnail handlers and file associations determine their visuals.
A document preview shows file content. An application or file-type icon identifies the application or file type, not file content.
XUI does not run the selected executable, load it as application code, or change file associations to obtain its icon.

Each icon fits inside a 24-by-24-DIP box in the existing 32-DIP row.
The decode box follows the physical DPI size. The renderer preserves aspect ratio and blends alpha over the row background.
Names, selection, focus, scrolling, and UIA item identities retain their existing behavior.
Pending requests and failed requests retain the original vector icons.
The error callback receives the stable item ID and the error text on the UI thread.
The explorer status area counts failed visual requests.
A missing thumbnail is not an error if the Shell returns an icon.
If both Shell requests fail, the callback includes an HRESULT. Missing files also produce an explicit error.
The list does not retry a failed slot every frame.

The native list adapter retains only visible rows and the existing viewport buffer.
Limits are 24 thumbnail slots per list and 48 per window. Additional rows retain vector icons.
There is no thumbnail control, HWND, render target, or worker for each file.
WIC and Shell requests use separate, lazy, process-lifetime workers.
The Shell worker initializes a COM STA and pumps messages between requests and while idle.
Shell interfaces stay on that worker. They never cross apartments.
Both workers share one 64-request queue, a 128-entry cache, and the existing 8-MiB CPU and 8-MiB GPU pixel budgets.
At most two requests are active. A blocked Shell handler does not block the WIC worker.
The host retains bitmap IDs from both lists and `Image` controls in one shared target.
Equal files, source kinds, and physical decode sizes share cached pixels and bitmaps across panes.

Shell output converts to premultiplied BGRA for the existing renderer.
Shell HBITMAPs use straight alpha. WIC applies premultiplication once before Direct2D blends the pixels over the row background.
The backend rejects bitmaps larger than the requested dimensions before conversion.
Legacy icons without alpha use their HICON transparency mask through WIC.
Temporary HBITMAP and HICON handles have scoped ownership. XUI does not cache these handles.
The CPU ledger includes retained output pixels and pending output reservations.
Shell allocations, temporary WIC conversion storage, handler allocations, and driver allocations are outside these pixel budgets.
These budgets are not hard limits on total process memory.

Filtering and scrolling match slots by stable item ID and path, not row position.
A new source snapshot, `reload_thumbnails`, or a DPI change cancels the old requests.
The worker checks the canonical path, file identity, modification time, size, source kind, and physical output size before cache reuse.
Shell keys also retain the absolute item path, because links can have different visuals from their targets.
File metadata access and Shell extraction stay off the UI thread.
Directory refresh provides a new source snapshot. There is no periodic file watcher.
Hidden panes, offscreen rows, disabled thumbnails, and window closure release their requests and pixel references.
The bounded cache can retain unused pixels until eviction or `ImageResources::clear_unused`.
Window closure does not wait for a codec or Shell handler. Completed visuals do not request idle frames.

Cancellation revokes delivery and releases completed pixels. It cannot interrupt a Shell call that is already active.
XUI does not terminate blocked threads or create replacement workers.
A blocked Shell handler can therefore delay subsequent Shell icons until that call returns.
Network paths and third-party handlers can block or allocate memory outside XUI's budgets.
The Shell controls provider activation and its own thumbnail cache. XUI does not guarantee isolation for every third-party handler.
XUI does not directly instantiate thumbnail providers or disable Shell-managed provider isolation.
Windows can retain an old per-path icon after a file update, even when XUI detects the new file version.
File-association changes do not invalidate XUI's cache automatically.
Third-party handlers, network stalls, and physical monitor transitions still require manual coverage.

`xui_thumbnail_tests` covers the public control and the actual explorer composition.
Original WIC fixtures include PNG, JPEG, alpha, corrupt data, Unicode paths, folders, and ordinary text files.
Pixel assertions cover aspect ratio, selection, theme changes, synthetic DPI changes, and target recreation.
Other assertions cover shared ownership, filtering, scrolling, tabs, pane visibility, navigation, file changes, deletion, and blocked-decoder closure.
A 20,006-row source and a synthetic tall viewport cover slot limits without thousands of decodes.
The existing image pipeline tests retain their budget, cache, reservation, and cancellation checks.
Physical monitor transitions, physical GPU loss, and optional third-party WIC codecs still require manual coverage.
