# 7. Reload and diagnose

Use the [final sample entry point](sample/Program.cs) for development reload.
It selects `ReloadHost` only when the imported targets define `XUI_HOT_RELOAD`.
Release builds use an ordinary disposable Window and `Run`.

The [watch procedure](../../../CONTRIBUTING.md#gitbook-tutorial-sample) starts the development loop.
Keep build commands in that guide rather than a second local script.

## Try one edit at a time

Start with a task name entered and Complete selected.
Change the heading's font size or an existing color value.
An accepted in-place edit should preserve the field, toggle, state, and native control identities.

Change an existing handler body.
Later events should use the new body without accumulating another subscription.
Changing an initializer is different: it changes the structural signature.

Add a new node or reorder a style declaration.
The host should replace the window or the watcher should restart the process.
Transient task state then resets.
This is expected behavior, not failed persistence: the tutorial has no persistence layer.

## Know which edits preserve state

| Edit | Expected handling |
| --- | --- |
| Existing text or supported property expression | In-place update when the runtime accepts the edit |
| Existing style value, resource value, or part rule value | In-place style refresh |
| Existing handler body | Later events use the updated method |
| Control types, parents, parameters, or `Content` identity | Structural replacement |
| State schema or initializer | Structural replacement |
| Added or removed event, style binding, or local property binding | Structural replacement |
| Style or resource names and declaration order | Structural replacement |

The .NET runtime can require a process restart for additional unsupported edits.
Do not promise that every property edit will preserve state in every runtime situation.
See the full [reload contract](../xui-language.md#understand-reload-behavior).

## Diagnose failures at the right layer

| Symptom | Check |
| --- | --- |
| `XUI001` during compilation | Read the `.xui` location; check syntax, style target, part, property type, and value bounds |
| C# method or type error | Check generated callback signatures, namespace imports, and the real binding API |
| Native DLL load failure | Match the process architecture and DLL; use the configured output directory or DLL search path |
| Missing native export | Rebuild the DLL from the same source revision as the bindings |
| Wrong-thread exception | Move the UI call to the creating thread through an explicit delivery mechanism |
| Owner or parent error | Use an unattached element from the same Window |
| Callback failure | Preserve the reported exception or status; fix the failing callback rather than ignoring it |
| Style appears unchanged | Check local overrides, active theme, high contrast, and whether that part supports the property |

An invalid style must not replace the previous valid style.
A recoverable property error can leave the old window usable.
Some generation failures require a restart, so do not assume state always survives an invalid edit.

Do not delete generated files to make a valid source edit take effect.
Do not suppress all exceptions around `Run`.
See [binding errors](../bindings.md) for native status and wrapper behavior.

Continue with [delivery and accessibility](08-delivery.md).
