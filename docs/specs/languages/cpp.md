# C++20 applications

C++ applications create native XUI objects directly.
They do not call through the C ABI or load `xui.dll`.
The `xui_windows` target supplies the Windows host.

This path exposes the broadest API.
It includes worker helpers and provider interfaces that the language bindings do not expose.
The public headers are in [`include\xui`](../../../include/xui).

## A minimal application

This complete `main.cpp` creates a native text field and an Apply button.
The label changes after the user edits or submits text.
The explicit automation IDs remain stable as the visible text changes.

```cpp
#include "xui\application.hpp"
#include <windows.h>
#include <exception>
#include <memory>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    try {
        xui::Window window({L"My XUI application", {480, 280}});
        auto root = std::make_shared<xui::Stack>(xui::Axis::vertical);
        root->set_padding({20, 20, 20, 20});
        root->set_spacing(12);

        auto status = std::make_shared<xui::Label>(L"Ready");
        auto input = std::make_shared<xui::TextInput>(L"Workspace name");
        auto apply = std::make_shared<xui::Button>(L"Apply");
        status->set_automation_id(L"status");
        input->set_automation_id(L"workspace");
        apply->set_automation_id(L"apply");

        input->on_change([status](const std::wstring& text) {
            status->set_text(L"Editing: " + text);
        });
        apply->on_click([status, input] {
            status->set_text(L"Applied: " + input->text());
        });
        input->on_submit([status, &input] {
            status->set_text(L"Submitted: " + input->text());
        });

        root->add(status);
        root->add(input);
        root->add(apply);
        window.set_content(root);
        const int result = xui::Application::run(window);
        if (result != 0) {
            MessageBoxW(nullptr, window.error().c_str(), L"XUI error", MB_OK);
        }
        return result;
    } catch (const std::exception& error) {
        MessageBoxA(nullptr, error.what(), "XUI construction error", MB_OK);
        return 1;
    }
}
```

The submit handler borrows the local `input` variable.
That variable remains alive throughout the blocking `Application::run` call.
A strong capture of `input` inside its own callback creates an ownership cycle.

## Project integration

The application requires C++20, the Windows backend, and a native-control manifest.
The manifest enables common-controls v6 and per-monitor DPI behavior.
[`demo\xui.rc`](../../../demo/xui.rc) supplies the repository manifest.

The [C++ integration procedure](../../../CONTRIBUTING.md#use-xui-in-a-c-application) contains the CMake configuration.
The [native build procedure](../../../CONTRIBUTING.md#build-the-native-code) describes toolchains and architecture selection.

## Controls, events, and layout

| Task | Native API |
| --- | --- |
| Create a control | `std::make_shared<xui::Button>(L"Apply")` |
| Handle activation | `Button::on_click` |
| Handle committed text | `TextInput::on_change` |
| Handle Enter in a field | `TextInput::on_submit` |
| Handle a checkbox | `Toggle::on_change`, with a `bool` value |
| Set a preferred size | `Element::set_preferred_size` |
| Reserve fixed dimensions | `Element::set_fixed_size` |
| Share remaining Stack space | `Stack::add(child, 1)` |
| Request native focus | `Window::focus` |
| Request closure | `Window::close` |

`Stack` arranges children on its selected axis.
Nested stacks form rows and columns.
`Grid`, `Wrap`, and `AdaptiveLayout` supply additional layouts through [`adaptive_layout.hpp`](../../../include/xui/adaptive_layout.hpp).
`ScrollView` retains its content but does not virtualize arbitrary child controls.
`ItemsView`, `DataGrid`, and `TreeView` supply virtual collection behavior.

Native setters do not generally simulate user activation.
Collection selection and view callbacks have their own notification rules.
The [application contract](../application.md#ownership-and-property-updates) describes these differences.

## Styles

`WindowOptions::visual_style` selects Classic or the optional WinUI-style appearance.
Shared `ControlStyle` definitions and per-part local values provide application-authored presentation.
Styles preserve native input, ownership, and accessibility.

The [C++ style definitions](../control-styling.md#c-definitions) contain complete declaration patterns.
The [Toggle example](../control-styling.md#toggle-pilot) shows named parts and state rules.
The [inventory](../control-styling-inventory.md) identifies each target's limits.
Native editor interiors, browser content, and Shell menus retain their documented platform boundaries.

## Ownership, threads, and errors

The window retains its root through `std::shared_ptr`.
Each element has one layout parent.
Standalone native controls can outlive the window after host teardown.
This differs from the window-scoped handles in the C ABI.

All control access and callbacks use the creating UI thread.
One window runs on that thread at a time.
Each window runs once.
The caller must not initialize COM as MTA.

`Window::post` accepts work from a worker while the window remains alive.
Closure rejects later delivery and discards queued work.
An application must keep the window alive until its workers stop posting.
`ViewTask` and `SampleTask` provide bounded work and cancellation.
Their loaders must not capture controls or the window.

Property validation can throw before the run starts.
Startup errors and callback exceptions produce a nonzero run result.
`Window::error()` supplies the run diagnostic.
Callbacks request closure rather than destroy their active window.

## Deployment and next steps

A static C++ application needs its executable and Windows system components.
Optional WebView2 adds its separate runtime requirement.
The [deployment reference](../../../CONTRIBUTING.md#nativeaot-and-deployment) compares all language paths.

- [Tutorials](../tutorials/README.md)
- [Control catalog](../controls/README.md)
- [Application lifecycle and accessibility](../application.md)
- [Collections and asynchronous data](../collections.md)
- [Language comparison](README.md)
