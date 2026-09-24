# XUI experimental portable SDK

This preview compiles shared `.xui` and C# applications for native Windows,
native Android, and the browser. Application behavior remains local C#; the web
adapter uses real DOM controls and .NET WebAssembly.

These packages are experimental. They are not a declaration that the full
multi-platform release plan or physical input/accessibility matrix is complete.
Keep all `Xui.Experimental.*` packages on the same version.

## Package family

- `Xui.Experimental.Compiler`: source generator and build assets; build-time only.
- `Xui.Experimental.Portable`: managed retained elements, ownership, and application helpers.
- `Xui.Experimental.Windows`: native Windows adapter and the explicitly packaged native architecture.
- `Xui.Experimental.Android`: native Android widget adapter.
- `Xui.Experimental.Web`: local Wasm DOM adapter and static web assets.
- `Xui.Experimental.Templates`: the `xui-portable` project template.

The existing Windows `Xui` package and `dotnet new xui` template remain separate.
The experimental Windows package reports an error if it lacks the requested RID;
it never substitutes an x64 DLL for ARM64.

## Create an application

Configure the local or approved package source containing the complete preview
family, install the matching template package, and generate `xui-portable`.
The template supplies a shared UI/model library, tests, and thin platform hosts.
It does not copy framework runtime source or depend on an XUI checkout.

Use .NET 10 and the platform prerequisites documented by XUI. Android requires
its workload, SDK, and JDK. Windows requires the matching native runtime.
Browser publication requires a static host with the correct Wasm MIME type.
The web adapter's assets are under `_content/Xui.Web/`.

Compiler and development/test bridges are not part of Release application output.
Packages do not install workloads, accept SDK licenses, publish applications, or
configure production signing.

See the [XUI repository](https://github.com/zadjii-msft/xui) for the public portable,
Android, and DOM contracts, contributor procedures, and the current completion
plan. Unsupported features must remain explicit rather than silently falling
back to a different UI.
