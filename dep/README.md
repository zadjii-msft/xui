# Repository packages

This directory is the checked-in feed for manually supplied NuGet packages.
The root `NuGet.Config` adds this feed without replacing the machine's other feeds.
Build output and extracted packages belong under `build`, not here.
Build and update procedures are in [CONTRIBUTING](../CONTRIBUTING.md#repository-local-packages).

## Package inventory

`Lsh.0.3.0.nupkg` supplies the native syntax engine for Windows x64 and ARM64.
The repository maintainer supplied this package.
Its metadata identifies source commit `ad43d44e3f7eb7274c164c9e6fad4826498d443a` and authors `lsh-lib contributors`.
The archive contains the MIT license at `licenses/LSH-LICENSE.txt`.
XUI copies that license with the native runtime.

SHA-256:

```text
B5ADC9786292FBBA9B0F72F4FE1C80839B39C51D6B8CF27081A75FE0BA11135E
```
