# Shared application screenshot gallery

These are actual renders of four distinct applications authored in shared `.xui` and C#.
The greeting smoke is additional and is not counted toward these four.
The screenshots were captured during the September 23, 2026 implementation work, not drawn as mockups.
They are evidence for the experimental framework, not a claim that every release gate in the [completion plan](../specs/multi-platform-framework-plan.md) is finished.

| Application | Shared source and behavior |
| --- | --- |
| Order builder | [OrderBuilder.xui](../../bindings/dotnet/Experimental/SharedDemo/OrderBuilder.xui): customer details, validation, quantity limits, coupon arithmetic, and local review |
| Task board | [TaskBoard.xui](../../bindings/dotnet/Experimental/SharedDemo/TaskBoard.xui): three explicitly fixed priority slots, selection, editing, status, and filters |
| Expense ledger | [ExpenseLedger.xui](../../bindings/dotnet/Experimental/SharedDemo/ExpenseLedger.xui): budget, three category totals, validation, and decimal USD calculations |
| Session planner | [SessionPlanner.xui](../../bindings/dotnet/Experimental/SharedDemo/SessionPlanner.xui): start time, three focus blocks, breaks, next-day arithmetic, and review; no fake running timer |

Each row below uses one UI definition and one C# model across all three backends.
The task board, ledger, and planner show their shared seeded state.
Order shows the initial form on native platforms and a completed local review in the browser.
Data and native appearance differ; no platform-specific replacement UI was authored for the pictures.
The separate dynamic task board and settings showcase are subsequent feature fixtures, not substitutes for this four-application baseline.

## Order builder

| Windows | Android | Web |
| --- | --- | --- |
| ![Order builder on native Windows](images/portable-gallery/windows/order.png) | ![Order builder on native Android](images/portable-gallery/android/order-builder.png) | ![Order builder running local C# in the browser](images/portable-gallery/web/order-web.png) |

## Task board

| Windows | Android | Web |
| --- | --- | --- |
| ![Task board on native Windows](images/portable-gallery/windows/tasks.png) | ![Task board on native Android](images/portable-gallery/android/task-board.png) | ![Task board running local C# in the browser](images/portable-gallery/web/task-board-web.png) |

## Expense ledger

| Windows | Android | Web |
| --- | --- | --- |
| ![Expense ledger on native Windows](images/portable-gallery/windows/expenses.png) | ![Expense ledger on native Android](images/portable-gallery/android/expense-ledger.png) | ![Expense ledger running local C# in the browser](images/portable-gallery/web/expense-ledger-web.png) |

## Session planner

| Windows | Android | Web |
| --- | --- | --- |
| ![Session planner on native Windows](images/portable-gallery/windows/planner.png) | ![Session planner on native Android](images/portable-gallery/android/session-planner.png) | ![Session planner running local C# in the browser](images/portable-gallery/web/session-planner-web.png) |

## Capture conditions and limits

- Windows: actual x64 native XUI controls using the opt-in Windows portable adapter and WinUI visual style. Capture targets the application's client window, not the desktop or another application's pixels.
- Android: actual widgets on the existing API 35 x86_64 emulator. The capture helper verifies the resumed sample Activity, expected seed markers, and unobscured output. It does not wipe device data.
- Web: published local .NET Wasm in Edge 153 with real DOM controls, at a 1100 by 1300 CSS-pixel viewport and device scale 1. Native input and local C# actions establish the captured state.

The images were opened for visual inspection, including captions and native editor bounds.
PNG/source hashes and browser/device metadata accompany the captures where supplied by the capture harness.
They do not establish physical-device IME, TalkBack/Narrator, every browser, large-data performance, or pixel parity.
Build/test procedures belong in [CONTRIBUTING](../../CONTRIBUTING.md); the [Android](android-experiment.md) and [DOM](dom-web.md) notes retain platform-specific evidence.

## Additional native Windows feature captures

These later captures show the same shared application sources through the Windows adapter.
They are not substitutes for the three-platform baseline above or evidence that their new control families have already passed Android/web acceptance.
Each targets only its owned native client surface and was visually inspected after capture.
The [capture manifest](images/portable-gallery/windows/forms-apps-capture-manifest.json) records exact PNG and native-runtime hashes.

| Application | Actual Windows render |
| --- | --- |
| [Forms workbench](../../bindings/dotnet/Experimental/SharedDemo/FormsWorkbench.xui): native purpose hints, multiline notes, read-only state, and an initially empty attachment-owned password | ![Forms workbench on native Windows](images/portable-gallery/windows/forms.png) |
| [Workshop registration](../../bindings/dotnet/Experimental/SharedDemo/WorkshopRegistration.xui): local seat estimate, attendance/consent, and multiline learning goals; name/email examples are placeholders, not completed fields | ![Workshop registration on native Windows](images/portable-gallery/windows/workshop.png) |
| [Editable cart](../../bindings/dotnet/Experimental/SharedDemo/EditableCart.xui): initial catalog page before product-detail navigation, editable lines, or a local quote | ![Editable cart catalog on native Windows](images/portable-gallery/windows/cart.png) |
| [Services workbench](../../bindings/dotnet/Experimental/SharedDemo/PlatformServicesWorkbench.xui): startup capability display before any clipboard, file-picker, or URI action | ![Platform services workbench on native Windows](images/portable-gallery/windows/services.png) |

No credentials, user-file contents, clipboard contents, or external service operations were used to create these images.
They do not establish the planned workspace-scale navigation/tab experience, which has its own implementation and native acceptance work.

## Workspace Studio and Operations

These are actual renders of the shared [workspace](../../bindings/dotnet/Experimental/SharedDemo/WorkspaceStudio.xui) and its [Operations page](../../bindings/dotnet/Experimental/SharedDemo/StudioOperationsDashboard.xui).
They show genuine navigation, retained document tabs/editors, a bounded visible subset of a 10,000-document catalog, scoped colors/typography, and a cancellable local dataset workflow.
The desktop capture waits for native catalog realization; the earlier blank/pending-catalog capture was rejected rather than used as proof.
The [cohort manifest](images/portable-gallery/studio-capture-manifest.json) records exact PNG hashes, native runtime identity, viewport sizes, and published-browser interaction evidence.

### Desktop workspace

| Windows, 1440 by 900 logical units | Published web, 1440 by 1000 CSS pixels |
| --- | --- |
| ![Real Windows workspace with native navigation, tabs, editors and realized catalog rows](images/portable-gallery/windows/studio-desktop.png) | ![Real published C# workspace with retained DOM editors and virtual catalog](images/portable-gallery/web/studio-desktop-web.png) |

### Operations dashboard

The counts come from an explicit local scan of the sample documents, not invented cloud telemetry.
The web run also edited a retained document title; native appearance and current drafts therefore differ.

| Windows desktop | Published web desktop |
| --- | --- |
| ![Native Windows Operations dashboard after scanning 10000 local documents](images/portable-gallery/windows/studio-operations-desktop.png) | ![Published C# Operations dashboard with real local scan results](images/portable-gallery/web/studio-operations-desktop-web.png) |

### Narrow layouts

The same authored grid selects compact panes.
Operations metrics use one full-width column instead of overlapping two-column labels.
Content below the visible viewport remains natively scrollable.

| Windows compact document, 320 by 760 logical units | Windows compact Operations, 320 by 760 logical units | Published web Operations, 320 by 844 CSS pixels |
| --- | --- | --- |
| ![Retained native document editor in compact Windows workspace](images/portable-gallery/windows/studio-compact.png) | ![Compact Windows Operations with readable metric rows](images/portable-gallery/windows/studio-operations-compact.png) | ![Compact published browser Operations with readable metric rows](images/portable-gallery/web/studio-operations-phone-web.png) |

These baseline captures do not establish native packaged-font/image support, physical IME or screen-reader behavior, or a passing final virtualization performance gate.
The separately measured catalog-row content fits at 1x/2x text scale use a shared declared pitch of 128; that app correction did not change the independent M6 benchmark workload.

### Explicit Windows scan-controls drawer

The later Windows host opts into the same shared Operations component's real 180 ms Bottom Reveal.
The drawer is off by default on hosts that have not selected this capability.
These inspected captures show it open and settled after the local scan, not an invented intermediate animation frame.
The independently executed native workflow covers focus/composition veto, close/reopen progress, retained controls, busy-scan cancellation access, and native tab activation while the drawer stays closed.
The multiline document editor and virtual catalog remain outside the restricted Reveal subtree.

| Windows desktop, 1440 by 900 logical units | Windows compact, 320 by 760 logical units |
| --- | --- |
| ![Actual Windows Operations with the opt-in retained scan controls drawer](images/portable-gallery/windows/studio-drawer-desktop.png) | ![Actual compact Windows Operations drawer with readable scrollable results](images/portable-gallery/windows/studio-drawer-compact.png) |

The manifest identifies this later native and shared-source cohort separately from the earlier default-off captures.
These images and Windows interaction results do not establish Android/browser application motion or physical IME/assistive-technology acceptance.

### Combined-checkout Android workspace

The final root Debug APK ran the complete seventeen-check Studio workflow on the API 35 x86_64 emulator.
It exercised genuine native navigation/tabs, retained title edits and selection across backgrounding/rotation, a bounded catalog, native Spinner selection, a completed 10,000-document Operations scan, and native Back.
The initial clipped two-line Analyze caption was fixed by measuring native horizontal-stack children at their allocated widths, not by shortening the caption or forcing a height.

| Portrait editor | Native virtual catalog | Completed local Operations |
| --- | --- | --- |
| ![Root Android Studio with complete native caption and retained document editor](images/portable-gallery/android/studio-phone.png) | ![Root Android Studio showing six realized rows from 10000 documents](images/portable-gallery/android/studio-library.png) | ![Root Android Operations after a completed 10000-document local scan](images/portable-gallery/android/studio-operations.png) |

![Root Android short-landscape editing with retained native title, body, and actual IME overlay](images/portable-gallery/android/studio-landscape-editing.png)

The landscape title and body measured 118 and 381 physical pixels high, respectively; selected-range replacement succeeded without replacing the editors.
The screenshot includes the emulator's floating/stylus IME and is not proof of docked-keyboard resize or physical IME behavior.
The physical window is 2400 by 1080 pixels at density 420; do not equate its whole-window size with the inset host allocation.
An earlier run that injected repeated scroll input while a scan was pending produced an ANR.
Waiting for completion without that input passed, but does not resolve the separate input-pressure/performance risk.
Android Reveal, rendered Image, and production packaged-font assignment remain unimplemented; this is functional Studio acceptance, not completion of the framework plan.
