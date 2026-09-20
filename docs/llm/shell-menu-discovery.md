# Asynchronous Shell menu discovery

These notes describe the gallery and C# explorer Shell-menu integration.
Public menu contracts are in [the menu reference](../specs/menus-and-input.md).

The gallery menu shows application commands and a loading message before Shell discovery finishes.
After the first menu paint, a dedicated STA thread creates the Shell handlers and their native window.
The UI thread receives command metadata, not COM objects.
When discovery finishes, the menu replaces the loading message with the discovered commands.
The replacement retains the gallery appearance and recomputes the native menu dimensions.
Discovery errors appear in the menu, with the Windows menu fallback still available.

Application commands and the Windows fallback keep their positions when Shell commands appear.
The menu postpones replacement while a command is highlighted or a mouse button is pressed.

Each opening uses its exact selection and fresh Shell handlers.
The worker does not reuse commands from a previous file, folder, or opening.
It retains at most one active request and one pending request, then exits after ten idle seconds.
Closing the menu cancels its request without waiting for a Shell extension.
A blocked extension can delay later Shell results, but application commands and cancellation remain available.
The worker releases its handlers on their STA after the extension returns, and retains the module until that cleanup finishes.

## Shell command icons

`NativeShellProvider::read` copies ordinary `MIIM_BITMAP` images into immutable `MenuIcon` pixels on the Shell STA.
Each bitmap is at most 64 by 64 pixels. The menu has a 4 MiB pixel budget.
Legacy bitmaps without alpha become opaque. Alpha bitmaps retain premultiplied BGRA pixels.
Callback and system pseudo-handles never reach GDI bitmap APIs.
Owner-drawn entries retain their native fallback and do not request extension drawing.
Unreadable or oversized bitmaps leave the icon space empty and emit a debugger diagnostic.

`CustomShellMenu` retains copied icons by command ID. Shell entries without copied bitmap data have no icon.
`Menu::draw` composites pixels against the current row color with GDI.
For supplied icons, disabled rows and high-contrast menus use the theme-colored generic glyph.
A separate column preserves checkmarks. Separators and discovery notices have no icon.
The Windows-menu fallback has no icon before or after discovery.
Icon discovery does not request canonical verb strings or traverse dynamic submenus.

`tests/shell_menu_tests.cpp` covers copied alpha pixels, bitmap ownership, pseudo-handles, size limits, and model propagation.
Its drawing checks inspect bitmap pixels, glyph pixels, and empty icon spaces at multiple DPI values in dark, light, and high-contrast palettes.
The existing latency fixture retains its first-paint, cancellation, and original-handler checks.

## Prefetch experiment (September 19, 2026)

**Result:** a completed folder prefetch reduced immediate discovery times in this sample, but did not make all later menus fast.
By default, the explorer still starts discovery on menu opening. Navigation does not prefetch without an explicit flag.
The initial experiment added the opt-in probe. The subsequent test build also exposes the [navigation experiment](#opt-in-navigation-test-build).

### Method

The build used source revision `84b17c80` plus the probe changes, Windows build 28638, ARM64 Release, and MSVC 19.44.
The probe is `xui_shell_menu_tests TARGET --prefetch-probe WARMUP_OR_DASH DELAY_MS`.
The [contributor procedure](../../CONTRIBUTING.md#shell-menu-prefetch-experiment) contains build and run commands.

`tests/shell_menu_tests.cpp::prefetch_probe` calls `AsyncShellMenu::start`, the same discovery path that serves the C# explorer.
It waits for the hidden request, copies its metadata, cancels it, and waits for handler release.
It then starts fresh requests for the target and a repeated target.
No menu or verb is necessary. No command metadata or handler crosses between requests.
This models a completed prefetch before the first menu request, not the full C# navigation or painting path.

Each case used a separate process.
The immediate matrix had five runs per target and warmup combination.
Warmup order alternated between runs. Target order remained folder, text, then image.
The idle matrix had three runs per combination, with the same delay for the control and prefetch cases.
Processes shared Windows caches and external Shell services. These are process-cold results, not machine-cold results.

| Role | Path relative to the checkout |
|---|---|
| Folder target | `bindings\dotnet\FileExplorer\Models` |
| Text target | `build\ARM64\CMakeCache.txt` |
| Image target | `assets\branding\generated\zoey-32.png` |
| Folder prefetch | `bindings\dotnet\FileExplorer` |
| Text prefetch | `CMakeLists.txt` |

### Observations

The following values are median target-ready times in milliseconds.
Parentheses contain the minimum and maximum. Each immediate cell contains five observations.
Prefetch time is separate from target-ready time.

| Target | No prefetch | Folder prefetch | Text prefetch |
|---|---:|---:|---:|
| Folder | 599 (508-1548) | 234 (218-333) | 800 (219-915) |
| Text | 827 (656-1642) | 531 (350-645) | 313 (217-598) |
| Image | 803 (640-1395) | 566 (451-619) | 718 (334-2029) |

The folder prefetch itself cost a median 606 ms, with a 449-944 ms range across 15 observations.
The text prefetch cost a median 837 ms, with a 670-1706 ms range.
This moves work before a click. It does not remove the work.
Even immediate repeated targets had outliers, including an image request at 3402 ms.

After an 11-second delay, the folder-prefetch advantage disappeared in this sample.
Each cell in this comparison contains three observations.

| Target | No prefetch, 11-second delay | Folder prefetch, 11-second delay |
|---|---:|---:|
| Folder | 779 (763-1067) | 1713 (1468-1967) |
| Text | 1429 (1094-1668) | 1880 (1073-2243) |
| Image | 1493 (998-1502) | 1767 (1632-1798) |

The probe also recorded different entry counts across fresh discoveries: 25-33 for folders, 22-28 for text, and 26-35 for images.
Thus, these timings do not compare identical command sets.
The experiment did not identify which extensions caused those differences or the latency outliers.
All foreground samples in the completed matrices were unchanged.
A pilot stopped on a foreground change. The final probe reports that sample without attributing the change to discovery.
Sampled foreground state does not prove the absence of transient activation.

### Interpretation and limits

`NativeShellProvider` parses paths, obtains an `IContextMenu`, and calls `QueryContextMenu` for every opening.
Prefetch can initialize shared Shell code, but selection-specific handlers still run.
Different extensions can participate for folders, file types, and selection counts.
The probe covered single-item selections only. It did not cover network locations, cloud placeholders, or multi-selection.

`ShellWorker::work` exits after ten idle seconds.
The delay comparison is consistent with loss of worker-local initialization, but does not isolate that cause from external services or command-set changes.
The existing regression fixture separately observes worker exit.
There is no evidence here for a universal improvement after arbitrary navigation-to-click delays.

The shared worker also serializes requests.
Cancellation does not interrupt an extension inside COM.
An unfinished speculative request can therefore delay a real menu request.
The probe deliberately waits for prefetch completion, so it does not measure that contention penalty.

**Initial decision:** retain the opt-in probe and leave default navigation unchanged.
The measurements support a possible short-lived cold-start benefit, not an unconditional prefetch on each directory visit.
A future navigation experiment needs bounded speculative work, explicit diagnostics, and real-request priority.
Cached menu commands or reused handlers are not substitutes: they can contain stale state or the wrong Shell identities.

The ARM64 Release build and `ctest -R "^xui_shell_menu_tests$"` passed after the probe addition.
The fixture covers existing menu behavior, cancellation, original-handler ownership, and idle cleanup.
Invalid delays and absent target or warmup paths returned errors.

## Opt-in navigation test build

The hands-on test build adds `--prefetch-shell-menus` to FileExplorer.
Normal launches retain the previous behavior.
The [contributor procedure](../../CONTRIBUTING.md#try-prefetch-inside-fileexplorer) contains the build and comparison commands.
The [public contract](../specs/menus-and-input.md#experimental-shell-warmup) describes the additive C++, C, and C# APIs.

`FilePaneView.Navigate` requests warmup after a successful snapshot commit and render.
`ExplorerApplication` tracks the requesting pane and cancels obsolete work during navigation, tab changes, and pane closure.
The option propagates through `ExplorerWindows` to new windows.
The caption includes `[menu prefetch]` so the comparison mode remains visible.

`Window::Impl` retains one cancellable request and releases it through `close_posts` on all closure paths.
`AsyncShellMenu::prefetch` uses the existing STA and discards metadata before readiness.
It releases handlers automatically instead of waiting for a menu action.
`ShellWorker::submit` skips speculative work if an interactive request is active or pending.
Interactive requests cancel active speculative work and replace pending speculative work.
The queue remains bounded. Cancellation does not interrupt COM.
Discovery failures and skipped warmups emit debugger diagnostics.

The native fixture covers automatic release, no invocation, error retention, interactive-request priority, UI-thread checks, explicit cancellation, and window closure.
The managed smoke checks that initial navigation requests warmup only when the flag is present.
The address smoke exercises subsequent navigation and pane isolation with that flag.
The ARM64 Release test build uses the matching local `xui.dll`.
Its first managed build encountered an unavailable NuGet audit endpoint (`NU1900`).
The local retry used `-p:NuGetAudit=false`, without a repository configuration change.
