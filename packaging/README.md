# XUI

XUI is a Windows UI framework for C++, C#, and Rust.
This package contains the native SDK, the C ABI runtime, .NET bindings, and the `.xui` compiler.
It supports Windows x64 and ARM64.
XUI uses the MIT license included in `LICENSE`.

For .NET applications, use `net10.0` and set `RuntimeIdentifier` to `win-x64` or `win-arm64`.
The package copies the matching `xui.dll` into build and publish output.
The compiler processes `.xui` files automatically.
Debug builds include the development host. Release builds exclude it.

For C++ applications, the native targets use Release static libraries and the static MSVC runtime.
They do not copy managed assemblies or `xui.dll` into the application.
The NuGet download contains both architectures and both APIs, but native deployment contains only the linked application.
`XuiNativeLinkage=CAbi` selects the import library and copies `xui.dll` instead.

The package includes `build\native\XuiConfig.cmake` for CMake consumers.
The [package guide](https://github.com/zadjii-msft/xui/blob/main/docs/specs/packages.md) contains integration examples and supported configurations.
