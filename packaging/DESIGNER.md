# XUI Designer

This archive contains XUI Designer for the Windows architecture in its filename: `win-x64` or `win-arm64`.
It includes the .NET runtime, the XUI native runtime, and the compiler for live previews.
No separate .NET runtime or SDK installation is necessary.

Extract the complete archive before you run `Designer.exe`.
Keep the executable beside all supplied files and directories.
To open a trusted component from the command line, run:

```powershell
.\Designer.exe "C:\Projects\Demo\Counter.xui"
```

The Designer compiles and runs authored C# in its own process.
It is not a sandbox.
Open only trusted `.xui` files.

The Designer uses managed, untrimmed, multi-file output because its preview requires runtime compilation.
The separate sample archives retain their smaller NativeAOT applications.

XUI uses the MIT license in `LICENSE`.
The .NET license and third-party notices are in `DOTNET-LICENSE.txt` and `THIRD-PARTY-NOTICES.TXT`.
Third-party components retain their own license terms.
Windows system components remain prerequisites.

The archive includes `manifest.json` with the version, architecture, and SHA-256 hashes of its application files.
The release also includes `SHA256SUMS.txt` for the downloadable assets.
The [Designer guide](https://github.com/zadjii-msft/xui/blob/main/docs/specs/designer.md) describes editing and preview behavior.
