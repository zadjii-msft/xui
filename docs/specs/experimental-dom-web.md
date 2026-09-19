# Experimental DOM web backend

The DOM backend runs the [portable XUI subset](experimental-portable-xui.md) locally through .NET WebAssembly.
It does not require a server event connection or `xui.dll`.
The browser owns fonts, native controls, text editing, and CSS layout.
The experiment does not promise pixel identity with Windows or Android.

The [WebDemo project](../../bindings/dotnet/Experimental/WebDemo/WebDemo.csproj) links the exact `SharedDemo/Greeting.xui` file.
The generator compiles its layout, state, bindings, and handlers into C#.
Blazor WebAssembly supplies startup and JavaScript interop, not a second Razor implementation of the UI.
The sample has no canvas, JavaScript implementation of authored methods, or copied HTML control tree.
Build and browser-test commands are in [CONTRIBUTING](../../CONTRIBUTING.md#experimental-dom-web).

## Host integration

The adapter library is [Xui.Web](../../bindings/dotnet/Experimental/Xui.Web/Xui.Web.csproj).
The application supplies an empty mount element, a separate error element, and a bounded mount size.
It also loads `_content/Xui.Web/xui-dom.css`.
Both elements must have unique HTML IDs.

```csharp
using var module = await js.InvokeAsync<IJSInProcessObjectReference>(
    "import", "./_content/Xui.Web/xui-dom.js");
void Report(Exception error) =>
    module.InvokeVoid("reportError", "errors", error.ToString());
using var host = new Host(new BrowserDispatcher(Report));
var demo = new PortableDemo.Greeting(host);
host.Attach(new DomBackend(module, "app", "errors"));
```

The host owns the backend after attachment starts.
The application owns the imported module.
The module must remain available until the host and its peers finish disposal.
The sample also uses `BrowserLifetime` for terminal `pagehide` cleanup.

`BrowserDispatcher` records the managed UI thread and its synchronization context.
Without a synchronization context, it uses the single-threaded WebAssembly task queue.
This experiment does not support multithreaded WebAssembly.
`Host.DispatchAsync` reports rejected dispatch, wrong-thread delivery, callback errors, and disposal through its task.
Raw dispatcher callbacks report errors through the required error delegate.

## DOM and accessibility

`VStack` and `HStack` create flex containers.
`Text` creates a text container, and `Button` creates a native `button`.
`TextInput` creates one wrapper with an associated `label` and a native single-line `input`.
`ScrollView` creates a named region with vertical overflow.
The children retain their authored order.

Control names supply accessible names.
Input captions use an explicit HTML label association.
A hidden caption does not remove the input name.
Help text supplies `title` and `aria-description`.
Buttons retain browser-native Enter and Space activation.
Input Enter submits, except during IME composition.

Authored IDs appear as `data-xui-id` on control wrappers.
The adapter assigns separate HTML IDs with a unique surface prefix.
Duplicate authored IDs remain separate elements.
An automation query must scope `data-xui-id` to its host.

Disabled controls use `aria-disabled` and `inert`.
Buttons and inputs also use their native `disabled` property.
A disabled ancestor prevents descendant events.
Invisible controls use `hidden`, occupy no space, and accept no events.
Text uses `textContent` or `value`, never authored HTML.

## Browser layout

Lengths use CSS pixels.
Stacks use CSS `gap` and uniform, border-box padding.
Hidden children do not create gaps.
Without explicit sizes, browser control chrome and content determine the desired size.

`preferredSize` supplies CSS width and height.
`size` takes precedence and constrains both dimensions within the parent.
Unconstrained cross-axis dimensions stretch.
Minimum sizes are zero, and maximum sizes prevent a child box from exceeding its parent allocation.
Label content clips if the allocated height cannot contain the text.

Non-flex children use an automatic basis.
Positive flex weights divide the remaining main-axis space.
Their percentage basis resolves to content size in an unbounded main axis.
Children can shrink when the parent cannot supply their desired space.
This uses browser flexbox rounding and native measurement, not the Windows layout engine.

A scroll region bounds its width and exposes vertical overflow.
Its content has no parent-height cap.
It does not provide horizontal scrolling.
The sample mount fills the viewport.

## Events and lifetime

Each attachment creates new DOM peers from retained state.
Property updates change the existing nodes.
Label and button text updates arrive as `ElementProperty.Name`.
Input text updates arrive as `ElementProperty.Text`.

A surface queues asynchronous .NET callbacks in browser event order.
All authored handlers run as compiled C# in the browser.
Native input changes do not write the same value back into the input.
Unrelated state changes preserve the input, focus, selection, and composition.
Programmatic edits stay silent and invalidate queued text events from an earlier text revision.

The adapter checks serialized state, property values, ownership, event types, and text revisions.
Interop or authored callback errors appear in the error element and the browser console.
After an error report, the event queue can process later events.
The browser tests reject unexpected console errors, page errors, and rejected callback promises.

Detach first invalidates the event sinks and unmounts the DOM tree.
The host then disposes peers in reverse creation order.
Each peer removes its listeners and releases its .NET callback reference.
The backend releases its JavaScript surface reference after the final peer.
Backend disposal does not dispose peers itself.
Old nodes and queued events stay inactive after a later attachment.

The sample disposes its host, test bridge, and imported module on a terminal `pagehide`.
If `pagehide.persisted` is true, the browser retains the live application in its back-forward cache.
A cache restore therefore retains the same application and input state.
The browser releases the entire page realm if it discards a cached page without another lifecycle notification.

## Limits

This backend supports only the declared portable subset.
It has no dynamic child replacement, hot reload, router, server rendering, or general Windows control parity.
The sample has no persistence across a full page reload.
It requires a browser with WebAssembly, flexbox, JavaScript modules, and `inert`.
Automated composition events do not establish compatibility with every operating-system IME or assistive technology.

The optional `?test` bridge exists only in Debug builds.
It exposes test commands for retained state, dispatch, attachment, and intentional error cases.
Release builds do not contain its C# implementation.
The static JavaScript bridge alone cannot access an application or execute those commands.
