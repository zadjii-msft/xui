# XUI

A small Windows UI framework for C++, C#, and Rust.
Build desktop applications from retained controls, native text inputs, and callbacks.
Win32 hosts the windows, Direct2D draws the interface, and DirectWrite draws text.
The core framework needs no bundled browser or third-party runtime.

**Prototype:** APIs are still evolving. Windows ARM64 and x64 are supported.
C# and Rust use a versioned C ABI, with less coverage than the C++ API.

## Describe a UI with C#

Write the layout in a `.xui` file and keep behavior in C#:

```xui
namespace Demo;

component Counter {
    state int Count = 0;

    view {
        VStack(spacing: 8, padding: 16) {
            Text($"Count: {Count}", id: "count");
            Button("Increment", click: Increment, id: "increment");
        }
    }

    code csharp {
        void Increment() => Count++;
    }
}
```

The compiler generates C# that creates native XUI controls.
Each click updates the label without rebuilding the tree.
The development host supports hot reload. Release builds contain no UI parser or reload host.

The [language guide](docs/specs/xui-language.md#configure-a-project) shows the project file and window entry point.
The [counter sample](bindings/dotnet/DeclarativeSample) contains a complete application.
See [CONTRIBUTING](CONTRIBUTING.md#c-and-declarative-samples) to build and run it.

## Or compose controls in C++

```cpp
#include "xui\application.hpp"
#include <windows.h>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    xui::Window window({L"My application", {480, 240}});
    auto content = std::make_shared<xui::Stack>(xui::Axis::vertical);
    content->set_padding({24, 24, 24, 24});
    content->set_spacing(12);
    auto label = std::make_shared<xui::Label>(L"Ready");
    auto button = std::make_shared<xui::Button>(L"Update label");
    button->on_click([&] { label->set_text(L"Updated"); });
    content->add(label);
    content->add(button);
    window.set_content(content);
    return xui::Application::run(window);
}
```

XUI owns layout, drawing, input, and accessibility. Your application supplies the controls and actions.
See [C++ application setup](CONTRIBUTING.md#use-xui-in-a-c-application) for linkage and the Windows manifest.
The [binding reference](docs/specs/bindings.md#rust-ownership-and-use) includes an equivalent Rust example.

## Explore the samples

- **[Control gallery](docs/specs/gallery.md):** Interactive controls with API excerpts and copyable code.
- **[File explorers](docs/specs/file-explorers.md):** C# and C++ applications with tabs, split panes, and folder navigation.
- **[Minesweeper](bindings/dotnet/Minesweeper/README.md):** A playable game with declarative layout and C# state.
- **[Task Manager](docs/specs/task-manager.md):** Live process data, virtual grids, and history charts.
- **[Thumbnails](docs/specs/thumbnail-sample.md):** Asynchronous images with a fixed pool of reusable tiles.

Classic is the default appearance. The optional [WinUI-style skin](docs/specs/winui-style.md) supports light, dark, and high-contrast themes.
It is not WinUI or XAML compatibility.
Shared [control styles](docs/specs/control-styling.md) customize colors, typography, named visual parts, and state rules without replacing control behavior.

## Documentation

[Packages and deployment](docs/specs/packages.md) covers NuGet, Cargo, and runnable release samples.

[XUI handbook](docs/specs/README.md) · [Tutorials](docs/specs/tutorials/README.md) · [All controls](docs/specs/controls/README.md) · [Language guides](docs/specs/languages/README.md)

[Local documentation preview](CONTRIBUTING.md#retype-preview-and-github-pages)

[Build and contribute](CONTRIBUTING.md) · [Maintainer notes](docs/llm/README.md)

## License

XUI is available under the [MIT license](LICENSE).
