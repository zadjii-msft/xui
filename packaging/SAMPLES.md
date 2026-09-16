# XUI samples

This archive contains x64 and ARM64 applications.
Each architecture has separate `native`, `dotnet`, and `rust` directories.
Each application runs from its extracted directory.
No XUI entry in `PATH` is necessary.

Use `win-x64` on x64 Windows. Use `win-arm64` on ARM64 Windows.
Extract the complete archive before you run an executable.
Keep each application beside its supplied DLLs and managed files.

The native directory contains the explorer, two galleries, thumbnails, Task Manager, and three binding samples.
Each .NET sample has its own directory: `Sample`, `DeclarativeSample`, `FileExplorer`, and `Minesweeper`.
The Rust directory contains `xui-sample.exe`.

The .NET applications include their runtime. They do not require a separate .NET installation.
Native and Rust applications use the static C runtime.
Windows system components remain prerequisites.
Optional WebView2 content is disabled in these builds.

Each architecture includes `manifest.json` with the version and SHA-256 hashes of its files.
The release also includes `SHA256SUMS.txt` for the downloadable assets.
