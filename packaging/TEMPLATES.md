# XUI project templates

Create Windows desktop applications with C# and declarative `.xui` controls.
The template requires Windows x64 or ARM64 and the .NET 10 SDK.

Download `Xui.Templates.<version>.nupkg` and `Xui.<version>.nupkg` from the same [XUI release](https://github.com/zadjii-msft/xui/releases).
Place both files in a local package directory.
Replace `1.2.3` with the downloaded version:

```powershell
dotnet nuget add source D:\packages\xui --name XuiLocal
dotnet new install D:\packages\xui\Xui.Templates.1.2.3.nupkg
dotnet new xui -n MyApp
cd MyApp
dotnet run
```

The generated project references the matching `Xui` package version.
It selects the host Windows architecture by default.
`dotnet run -r win-x64` or `dotnet run -r win-arm64` selects another architecture.
The template does not restore packages until the first build or run.

`Counter.xui` defines the layout and counter behavior.
`Program.cs` creates the window and selects the Debug hot-reload host.
Use `dotnet watch` for development.
Release builds exclude the hot-reload host.

The [package guide](https://github.com/zadjii-msft/xui/blob/main/docs/specs/packages.md) describes deployment and package sources.
The [language guide](https://github.com/zadjii-msft/xui/blob/main/docs/specs/xui-language.md) describes `.xui` syntax.

To remove the template:

```powershell
dotnet new uninstall Xui.Templates
```
