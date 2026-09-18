# C ABI applications

The C ABI exposes native XUI through opaque handles and fixed-width records.
[`xui.h`](../../../include/xui/xui.h) is a C-compatible header, not a C++ wrapper.
It includes the feature declarations from [`xui_features.h`](../../../include/xui/xui_features.h).
Additional layout and text declarations live in `xui_layout.h` and `xui_text.h`.

All exports use `__cdecl`.
The DLL owns the renderer and native controls.
The C application owns its buffers and callback contexts.

## A complete C application

This `main.c` contains only C source.
The Apply button updates the label through a status-returning callback.
The console receives diagnostics without a second UI dependency.

```c
#include "xui\xui.h"
#include <stdio.h>
#include <string.h>

struct app {
    xui_handle window;
    xui_handle label;
};

static xui_string text(const char* value) {
    xui_string result = {0};
    result.data = value;
    result.length = (uint32_t)strlen(value);
    return result;
}

static void report(xui_status status) {
    char message[1024];
    uint32_t count = 0;
    xui_status original = XUI_OK;
    xui_status copied = xui_error_copy(message, sizeof(message),
                                      &count, &original);
    if (copied == XUI_OK) {
        fprintf(stderr, "XUI %d: %.*s\n", (int)status,
                (int)count, message);
    } else {
        fprintf(stderr, "XUI error %d\n", (int)status);
    }
}

static xui_status XUI_CALL apply(void* context, const xui_event* event) {
    struct app* app = (struct app*)context;
    xui_property update = {0};
    if (event->kind != XUI_CLICK) return XUI_OK;
    update.size = sizeof(update);
    update.property = XUI_TEXT;
    update.target = app->label;
    update.text = text("Applied");
    return xui_update(app->window, &update, 1);
}

#define TRY(call) do { status = (call); \
    if (status != XUI_OK) goto cleanup; } while (0)

int main(void) {
    struct app app = {0};
    xui_handle root = 0, button = 0;
    xui_window_options options = {0};
    xui_property layout[2] = {0};
    xui_status status = XUI_OK;

    if (xui_abi_version() != XUI_ABI_VERSION) {
        fputs("XUI ABI version mismatch\n", stderr);
        return 1;
    }
    options.size = sizeof(options);
    options.version = XUI_ABI_VERSION;
    options.title = text("C application");
    options.width = 480;
    options.height = 240;
    TRY(xui_window_create(&options, &app.window));
    TRY(xui_stack_create(app.window, 1, &root));
    TRY(xui_create(app.window, XUI_LABEL, text("Ready"), 0, &app.label));
    TRY(xui_create(app.window, XUI_BUTTON, text("Apply"), 0, &button));

    layout[0].size = sizeof(layout[0]);
    layout[0].property = XUI_PADDING;
    layout[0].target = root;
    layout[0].a = layout[0].b = layout[0].c = layout[0].d = 20;
    layout[1].size = sizeof(layout[1]);
    layout[1].property = XUI_SPACING;
    layout[1].target = root;
    layout[1].a = 12;
    TRY(xui_update(app.window, layout, 2));
    TRY(xui_stack_add(root, app.label, 0));
    TRY(xui_stack_add(root, button, 0));
    TRY(xui_subscribe(button, apply, &app));
    TRY(xui_window_content(app.window, root));
    status = xui_window_run(app.window);

cleanup:
    if (status != XUI_OK) report(status);
    if (app.window != 0) {
        if (status == XUI_CALLBACK_FAILED) {
            xui_status callback_status = XUI_OK;
            if (xui_window_callback_error(app.window, &callback_status) == XUI_OK)
                fprintf(stderr, "Callback status: %d\n", (int)callback_status);
        }
        xui_status destroyed = xui_window_destroy(app.window);
        if (destroyed != XUI_OK) {
            report(destroyed);
            if (status == XUI_OK) status = destroyed;
        }
    }
    return status == XUI_OK ? 0 : 1;
}
```

`app` remains alive until the window run and destruction finish.
The string helper serves the short literals in this example.
General application buffers still require valid UTF-8 and bounded lengths.

## Project integration

The application includes headers from `include` and links the import library `xui.lib`.
It loads the matching `xui.dll` at runtime.
It also needs the common-controls v6 and per-monitor-DPI manifest.
The native header check in [`tests\abi_c_test.c`](../../../tests/abi_c_test.c) uses an actual C compiler.
The sample in `bindings\native\sample.cpp` uses the C ABI but remains C++ source.

The [C integration procedure](../../../CONTRIBUTING.md#c-abi-application-setup) supplies a complete CMake example.
The [native build procedure](../../../CONTRIBUTING.md#build-the-native-code) supplies the DLL and import library.
The [deployment procedure](../../../CONTRIBUTING.md#nativeaot-and-deployment) describes runtime files.
The backend requires a 64-bit process.
The application, import library, and DLL must use the same architecture.

## Controls, layout, and events

| Task | C API |
| --- | --- |
| Create a baseline control | `xui_create` with an `XUI_*` kind |
| Create a Stack | `xui_stack_create`, with axis `0` for horizontal or `1` for vertical |
| Add a Stack child | `xui_stack_add`, with positive flex for remaining space |
| Apply ordinary properties | `xui_update` |
| Create a feature control | `xui_feature_create` with `xui_feature_options` |
| Configure feature values | `xui_feature_set` and the feature-specific functions |
| Handle events | `xui_subscribe` |
| Read text | `xui_text_copy` |
| Request closure | `xui_window_close` |

One subscription exists per handle.
A null callback revokes the subscription synchronously.
The temporary event contains a kind, source handle, and value.
Text input events use `XUI_CHANGE` and `XUI_SUBMIT`.
The application reads committed text through `xui_text_copy`.

Feature kinds `XUI_TOGGLE_SWITCH`, `XUI_TOGGLE_BUTTON`, and `XUI_PROGRESS_RING` create the dedicated toggle and ring presentations.
ToggleSwitch uses `XUI_F_CHECKED`, while ToggleButton uses `XUI_F_BUTTON_CHECKED`.
Both setters are silent. Accepted actions report `XUI_CHANGE`.
ProgressRing shares the Progress range, value, and state properties and defaults to indeterminate state.
The [binding contract](../bindings.md#toggle-and-progress-presentations) defines the fields, events, and lifecycle rules.

Additional feature kinds are `XUI_CHECK_BOX`, `XUI_HYPERLINK_BUTTON`, `XUI_SELECTOR_BAR`, `XUI_INFO_BADGE`, and `XUI_MENU_BAR`.
CheckBox change events carry a CheckState value rather than a boolean.
SelectorBar uses choice snapshots and selection events.
MenuBar uses command snapshots with submenu roots.
The [additional control contract](../bindings.md#checkbox-links-selectors-badges-and-menu-bars) lists the properties and wrapper equivalents.

`xui_update` accepts at most 4,096 records.
It checks the complete batch before ordinary mutation.
Allocation or platform errors during application can leave earlier properties applied.
The [batch contract](../bindings.md#batched-updates) defines that distinction.

## Strings and errors

Structures require their exact `size`, supported `version` where present, and zero reserved fields.
UTF-8 strings have explicit byte lengths.
Input strings and copied item arrays remain caller-owned.
The native engine copies them during the call.

Text copy functions report byte counts without a NUL terminator.
A null buffer and zero capacity request the required length.
An undersized buffer returns `XUI_BUFFER_TOO_SMALL` without a partial copy.

Successful exports normally clear the thread-local diagnostic.
The example reports an error before cleanup calls can replace that diagnostic.
`xui_error_copy` does not change the stored error.
Its return status describes the copy, not the original operation.

Callbacks return `XUI_OK` or an application failure status.
They must not unwind through the ABI with exceptions, panics, or `longjmp`.
A callback failure requests closure and produces `XUI_CALLBACK_FAILED` from the enclosing operation.
`xui_window_callback_error` returns the original callback status.

## Lifetime and thread rules

The window owns all control handles, including detached controls.
There is no independent control-release function.
Window destruction revokes every child handle and subscription.
Styles, sources, and request tokens have their own explicit release or cancellation functions.

Construction finishes before `xui_window_run`.
All object operations and callbacks require the creating UI thread.
The caller must not initialize COM as MTA.
The C ABI does not supply an automatic worker-to-UI dispatcher.

`xui_window_close` requests closure but does not destroy handles.
Destruction during a run or callback returns `XUI_BUSY`.
Callback contexts must remain alive until revocation or window destruction.
The [ownership contract](../bindings.md#ownership-strings-and-errors) describes reentrancy and stale handles.

## Styles and next steps

`xui_control_style_create` creates a window-scoped immutable definition.
`xui_control_set_style` attaches that definition to a compatible element.
Applied controls retain the definition after `xui_control_style_release`.
Schemas and limits are available through `xui_control_style_get_schema` and `xui_control_style_get_limits`.
The [generic style contract](../bindings.md#generic-control-styles) describes records, state bits, local values, and errors.

- [Tutorials](../tutorials/README.md)
- [Control catalog](../controls/README.md)
- [ABI structure and version contract](../bindings.md#abi-contract)
- [Binding coverage and gaps](../bindings.md#advanced-api-gaps)
- [Language comparison](README.md)
