# Experimental Android backend

The Android adapter runs generated C# locally through .NET for Android.
It uses native Android widgets, not a WebView.
The [portable contract](experimental-portable-xui.md) defines the authored subset.
This backend does not port the Windows C++ framework.

The application targets `net10.0-android` and requires Android 8.0, API 26, or later.
The adapter namespace is `Xui.Experimental.Android`.
The [Android demo](../../bindings/dotnet/Experimental/AndroidDemo/AndroidDemo.csproj) links the exact shared `Greeting.xui` file.
Its Activity contains startup and lifetime code, not a second copy of the UI.
The Windows generator profile remains unchanged.

## Application integration

The application imports `Xui.Portable.targets` and references `Xui.Android`.
All host construction, element access, and native operations require the Android UI thread.

```csharp
var surface = new Android.Widget.FrameLayout(activity);
activity.SetContentView(surface);
var dispatcher = new Xui.Experimental.Android.AndroidDispatcher();
var host = new Xui.Experimental.Portable.Host(dispatcher);
var component = new PortableDemo.Greeting(host);
host.Attach(new Xui.Experimental.Android.AndroidBackend(surface, dispatcher));
```

The surface must be empty before attachment.
The caller owns the surface and the Activity.
The host owns its backend and peers after attachment starts.
`AndroidBackend.Dispose` only unmounts the root.
The host then disposes each peer in reverse creation order.
The adapter removes native listeners before it releases widgets.

`AndroidDispatcher` posts to the process main looper.
A rejected post throws.
The dispatcher does not cancel accepted callbacks when an Activity stops.
`Host.DispatchAsync` reports a disposed host through its returned task.
Applications must observe that task.

Native callback failures go to Android logcat under `Xui.Android`, then propagate.
The adapter does not replace an authored failure with success.
Platform update failures follow the shared host detachment contract.

## Native mapping

Stacks use a `LinearLayout` subclass with portable measurement and arrangement.
Labels use `TextView`.
Buttons use `Button`.
Inputs use one retained `EditText` and a `TextView` caption inside a `LinearLayout`.
Vertical scrolling uses `ScrollView`.
Each element also owns a measurement wrapper that applies its size constraints.

The backend implements every declared portable property.
It converts logical lengths from dp to physical pixels with the display density.
Positive half pixels round away from zero.
Dimensions saturate at Android's 24-bit measured-dimension limit.
Native fonts, button minimums, chrome, and font scaling remain platform-specific.

Stack spacing applies only between visible children.
Non-flex children receive their desired size first, bounded by the remaining allocation.
Flex weights divide the remaining bounded main axis.
Flex children use their desired size when that axis is unbounded.
The cross axis stretches children unless a fixed size limits them.
Padding, preferred size, and fixed size include the element's complete outer box.

The scroll child receives a bounded width and an unbounded height.
The adapter does not force that child to the viewport height.
Disabled scroll containers also reject touch, wheel, and keyboard input.
Invisible elements use Android `Gone` and consume no layout space.
Disabled ancestors disable their native descendants.

## Text and accessibility

Unrelated updates do not assign `EditText.Text`.
They retain the input identity, selection, focus, and composition spans.
A programmatic text replacement suppresses native change callbacks and clamps the previous selection to the new text.
That replacement can end the current composition because it changes the edited value.
The adapter does not synthesize delayed text events.

Native text changes call the authored change handler.
The single-line editor uses `ImeAction.Done` for submission.
Hardware Enter submits once, on key-up.
Buttons and labels use their native text as the accessible name.

The visible input caption uses Android `LabelFor`.
A hidden caption supplies the input name through `ContentDescription`.
The placeholder remains the native editor hint.
Help uses Android's native tooltip, which Android also exposes through accessibility node information.
TalkBack behavior still requires device acceptance checks.

`AutomationId` uses the native control's string `Tag`, not an Android resource ID.
`AndroidBackend.FindViews(id)` returns all matches within that backend.
Duplicate authored IDs remain distinct widgets.
Android generates a separate view ID for each input's label association.

## Activity lifetime

The demo attaches in `OnStart` and detaches in `OnStop`.
The host retains the model between those calls.
The demo disposes the host in `OnDestroy`.
Old widgets and callbacks cannot reach a later attachment.

Rotation creates a new Activity and host.
The demo saves `Count`, `Entry`, `Message`, focus, and selection in the instance-state bundle.
It restores authored state before attachment.
It does not retain Activity objects across rotation.
Composition sessions, keyboard visibility, and scroll position do not survive recreation.
Applications with additional state need their own state restoration.

## Acceptance status

The reference-only check compiles the production sources against Microsoft's real Android reference assemblies.
The arithmetic tests run without Android.
Neither check produces an APK or establishes native execution.
The native test application requires an Android device or emulator.
Build commands and the device smoke procedure are in [CONTRIBUTING](../../CONTRIBUTING.md#experimental-android-backend).

The [maintainer evidence](../llm/android-experiment.md) records the available checks and remaining platform blockers.
