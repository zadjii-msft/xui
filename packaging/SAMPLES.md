# XUI samples

This archive contains applications for one architecture, identified by `win-x64` or `win-arm64` in its filename.
The archive has separate `native`, `dotnet`, and `rust` directories.
Each application runs from its extracted directory.
No XUI entry in `PATH` is necessary.

XUI uses the MIT license included in `LICENSE`.
Third-party runtime components retain their own license terms.
Each .NET sample includes `DOTNET-LICENSE.txt` and `THIRD-PARTY-NOTICES.TXT` for its compiled runtime.

Download the `win-x64.zip` asset for x64 Windows or the `win-arm64.zip` asset for ARM64 Windows.
Extract the complete archive before you run an executable.
Keep each application beside its supplied DLLs.

The native directory contains the explorer, two galleries, thumbnails, Task Manager, and three binding samples.
Each .NET sample has its own directory: `Sample`, `DeclarativeSample`, `FileExplorer`, and `Minesweeper`.
The Rust directory contains `xui-sample.exe`.

The .NET applications use NativeAOT and include only their required runtime code.
They do not require a separate .NET installation.
Native and Rust applications use the static C runtime.
Windows system components remain prerequisites.
Optional WebView2 content is disabled in these builds.

The archive includes `manifest.json` with the version, architecture, and SHA-256 hashes of its application files.
The release also includes `SHA256SUMS.txt` for the downloadable assets.
