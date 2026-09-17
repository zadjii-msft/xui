# Installed Windows previews

`ShellPreview` opens an installed Explorer preview handler in a separate, broker-owned window.
It does not embed a provider window inside XUI.
The retained control reports progress and errors.
The application keeps its basic preview available.

An explicit user action starts a preview.
FileExplorer uses **Open Windows preview** in its basic preview window.
Space opens only the basic preview.
The action discloses the separate window and third-party content risks.

## Host boundary

Foreign child windows can block their parent during focus, resize, or destruction.
An out-of-process COM call does not remove this Windows input-queue dependency.
The broker therefore creates an ownerless top-level window.
Neither its parent nor its owner is an XUI window.
The provider receives that broker window through `IPreviewHandler::SetWindow`.

The XUI UI thread performs no Shell lookup, COM activation, or provider call.
It exchanges bounded requests with a supervisor.
The supervisor owns a matching-architecture `xui_preview_host.exe` process.
The helper uses a pumping STA for initialization, display, resize, focus, and cleanup.
The existing thumbnail worker does not participate.

`Image.ShellSource` remains a thumbnail/icon API.
It does not supply `IPreviewHandler` content or prove preview-handler availability.
The helper window is independent of retained popups and does not add native content above an XUI popup.

## Eligibility and selection

The helper accepts absolute local drive paths on fixed volumes with named streams.
It rejects remote paths, device paths, alternate streams, reparse points, offline files, and recall-on-access files.
It also rejects shortcuts, internet-location files, Shell container files, executable files, and files larger than 64 MiB.
The helper requires the local-machine URL zone.
An existing `Zone.Identifier` must contain one unambiguous `ZoneId=0`.
An absent zone stream is acceptable on an eligible local volume.

The helper retains the checked file handle, ancestor directory handles, and an existing zone stream.
The file denies write and delete sharing.
The provider receives a read-only `IStream` over that checked file handle.
The helper never unblocks a file.
An ambiguous origin produces `Restricted` before Shell association lookup or provider activation.

The helper selects the association for the actual `IShellItem` through `BHID_AssociationArray`.
It reads `ASSOCSTR_SHELLEXTENSION` for the preview-handler association.
It does not select a provider from the global preview-handler inventory.

The first version accepts existing `prevhost.exe` surrogate registrations.
It rejects explicit low-integrity opt-out, blocked handlers, disabled Explorer previews, local-server overrides, and remote-server overrides.
It requires a compatible local provider DLL.
Native ARM64 accepts ARM64 or ARM64X DLLs. Native x64 accepts x64 DLLs.
It does not search another registry architecture or install another helper architecture.

Activation uses `CLSCTX_LOCAL_SERVER`, never `CLSCTX_INPROC_SERVER`.
The helper requires `IPreviewHandler`, `IObjectWithSite`, and `IInitializeWithStream`.
File-only and item-only initializers are unsupported.
The helper changes no registry entry, association, or provider installation.

## API

C++ exposes `ShellPreview` in `xui/shell_preview.hpp`.
C exposes `xui_shell_preview_*` through `xui/xui.h`.
C# exposes `Window.ShellPreview`, `ShellPreview.Changed`, and typed status records.
Rust exposes `Window::shell_preview`, `ShellPreview::on_changed`, and the same typed states.
These APIs expose no HWND or COM pointer.
Declarative applications can pass the control through an `Element` parameter.

```csharp
var preview = window.ShellPreview("Windows preview");
var open = window.Button("Open Windows preview");
open.Click += () => preview.LoadLocal(path);
preview.Changed += status =>
{
    if (status.State == PreviewState.Failed)
        ShowPreviewError(status.Reason, status.HResult);
};
```

Each load returns a new generation.
`Cancel(generation)` affects only the current generation.
`Unload()` immediately revokes delivery and starts bounded helper retirement.
An obsolete completion cannot change the current status.
Callbacks run on the owning UI thread and retain the normal binding error contract.

`Status` contains the generation, state, reason, phase, HRESULT, and cleanup outcome.
The C record requires its exact size, version 1, and zero reserved fields.
`XUI_CHANGE` carries the generation. A callback can read the typed status immediately.
`Accepted` means that `DoPreview` succeeded, not that all content finished rendering.

`NoHandler` is an ordinary, quiet result.
`Restricted`, `UnsupportedProvider`, and `UnsupportedArchitecture` explain policy or compatibility limits.
Missing providers, initialization errors, display errors, timeouts, and helper failures retain explicit diagnostics.
`ActivationFailed` distinguishes COM activation errors, such as access denial, from a missing class registration.
The basic preview remains available after every unsupported or failed request.
Closing the helper produces `Cancelled` and an unload outcome without closing the basic preview.

## Bounds and cleanup

The supervisor allows four active helpers.
Each helper job permits one process and 256 MiB of committed process memory.
Startup permits five seconds. Subsequent operations permit one second.
The supervisor sends a heartbeat every half-second during idle display.
The retirement deadline is one second.
These limits apply to the owned helper, not a shared provider process.

Owner closure immediately revokes callbacks.
The supervisor then attempts `Unload`, clears the site, and releases COM references on the helper STA.
An unresponsive helper loses its owned job.
An uncertain cleanup opens a process-wide circuit breaker for subsequent installed previews.
The final application drain waits for owned supervisor retirement.
The native module remains resident after first use, so detached supervisors cannot return into an unloaded DLL.

`Unloaded` records successful provider cleanup calls.
`ProviderUnknown` means that the framework cannot establish the provider's final state.
Neither outcome promises that shared Prevhost exited or released every file.
The framework never terminates a process by name or changes a third-party surrogate registration.
Closing the application releases its job handles and retires its own helpers.

## Security and accessibility limits

This mode is not a content, macro, credential, or network sandbox.
Read-only streams prevent writes through that stream. They do not restrict a provider's other access.
An installed provider can use its own processes, network connections, and file handles.
The existing surrogate policy is not proof of provider trust.

Provider controls expose their native accessibility tree in the separate window.
The host supplies an `IPreviewHandlerFrame` for Tab traversal and Escape.
Escape closes the installed preview. Tab can return to the basic preview's focus sequence.
The host delegates content focus and resize to the provider.
Third-party keyboard behavior and accessibility quality remain provider-dependent.

FileExplorer restricts unknown-origin content to generic metadata before text or WIC decoding.
Its preview metadata does not call `ShellSource`, because that pathname API cannot retain the checked file identity.
The large generic icon and file details remain available.
Eligible local text and image files retain their basic previews.

## Platform references

- [Preview handlers](https://learn.microsoft.com/en-us/windows/win32/shell/preview-handlers)
- [Building preview handlers](https://learn.microsoft.com/en-us/windows/win32/shell/building-preview-handlers)
- [Registering a preview handler](https://learn.microsoft.com/en-us/windows/win32/shell/how-to-register-a-preview-handler)
- [IPreviewHandler](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nn-shobjidl_core-ipreviewhandler)
- [IPreviewHandlerFrame](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nn-shobjidl_core-ipreviewhandlerframe)
- [Surrogate activation](https://learn.microsoft.com/en-us/windows/win32/com/registering-the-dll-server-for-surrogate-activation)
- [Explorer restrictions for downloaded files](https://support.microsoft.com/en-us/servicing/os/windows/docs/2025/10/file-explorer-automatically-disables-the-preview-feature-for-files-downloaded-from-the-internet)

The framework applies its own origin gate. Direct COM activation does not inherit Explorer's complete preview policy.
