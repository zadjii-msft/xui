# `.xui` language and development reload

This plan records the initial implementation and its acceptance evidence.
The [language guide](../specs/xui-language.md) describes the current syntax and expanded control set.
Use [CONTRIBUTING](../../CONTRIBUTING.md) for build and test commands.

## Designer source map

The [designer guide](../specs/designer.md) describes the standalone development tool.
`bindings/dotnet/Designer/DesignerLayout.xui` defines its shell.
`DesignerApplication.cs` owns native documents, file operations, recovery drafts, and the bounded compiler queue.
`PreviewCompiler.cs` runs `XuiGenerator` and Roslyn, with semantic discovery of the generated component.
It emits a wrapper that attaches any supported root beneath a stack.
It does not execute authored code during compilation.

`PreviewHost.cs` owns one STA thread and sequential preview windows.
It constructs a candidate before closing the previous window.
The host retains the generated component until window disposal, then unloads its collectible assembly context.
Authored tasks or static references can prevent collection.
The host reports construction and callback exceptions to the editor.
This thread boundary does not isolate untrusted code.

`Designer.Tests` covers compilation, diagnostics, cancellation, input limits, and the generated wrapper.
The designer's `--smoke` mode covers the native editor and preview lifecycle.
`xui_abi_features_tests --activation` covers the opt-in no-activation window contract.
The normal window activation default remains unchanged.

## Goal

Developers author a retained XUI application with a small declarative language.
The build compiles `.xui` files into ordinary C#.
The release application uses the existing XUI controls, layout, input, and accessibility.
It has no additional virtual tree, reconciler, expression interpreter, or layout engine.

The development loop detects `.xui` edits.
Supported edits update the current window without a restart.
Unsupported edits require an explicit restart or a legal window replacement.
The application must never report a successful reload while it displays an obsolete tree.

The performance baseline is equivalent handwritten C# that uses the same bindings.
It is not the static C++ application.
NativeAOT publishing remains separate from the managed development loop.

## Initial language

```text
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

The namespace is optional.
Each file declares one component.
The initial controls are `VStack`, `HStack`, `Text`, `Button`, `Toggle`, and `TextInput`.
The compiler resolves their arguments to typed XUI calls.
The `id` argument supplies the automation ID of a control.
Stacks accept `spacing` and `padding`.

State initializers run when the component is created.
State changes update the expressions that depend on that state.
The compiler must define its dependency rules.
It must reject unsupported dependency patterns instead of adding a runtime scan.

The `code csharp` block contains C# members.
The initial implementation accepts methods in that block.
Fields use `state` declarations.
The parser must distinguish code braces from braces in strings, interpolations, and comments.
C# errors must refer to the original `.xui` file and line.
Unknown controls, arguments, and language constructs must produce build errors.

The first version supports a fixed control tree.
It does not promise dynamic child collections, arbitrary row templates, or a complete replacement for XAML.
Additional controls can extend the compiler after the initial contracts pass their acceptance checks.

## Compilation contract

The compiler processes each input independently where possible.
Generated names remain stable across edits to unrelated components.
Generated output has deterministic ordering.
The build does not rewrite unchanged generated files.

The build integration includes `.xui` inputs in the watch list.
It excludes generated outputs from that list.
It handles input creation, deletion, renaming, and syntax errors.
A deleted input must not leave an obsolete compiled component.
An input-list manifest invalidates compilation after input deletion or renaming.
The manifest changes only when the input list changes.
It has a separate path for each target framework and runtime identifier.

The generated construction code calls the current `Window` factories and attaches children once.
Named event handlers attach once.
Generated state setters call only the relevant property-update methods.
The compiler preserves the error and thread contracts of those methods.

Release output excludes development registries, reload metadata, and refresh subscriptions.
The application does not need the parser or compiler at runtime.
The generated code must build with the existing AOT-compatible bindings.

## Reload contract

### Supported in-place edits

The first live-edit targets are authored text, stack spacing, stack padding, and supported control properties.
Supported C# method-body edits affect later calls to those methods.
Changes to supported property expressions use the current component state.

An accepted .NET code update does not automatically refresh native controls.
A development-only update handler requests a generated refresh on the creating UI thread.
The refresh does not rerun state initializers or attach handlers again.
It must not reset user input that the application owns.

Stable generated names help .NET associate old and new methods.
The UI refresh also needs a shape check.
A valid .NET code delta can still describe an invalid live XUI tree.
Explicit ID expressions form part of that shape.
Controls without IDs use positional identity.

### Structural edits

Adding a control, changing its type, and changing parent-child relationships are structural edits.
The initial implementation does not patch those relationships in place.
It reports the required fallback explicitly.

The baseline fallback is a process restart through the development tools.
An alternative is a development host that replaces the entire window.
Window replacement must call `Close`, wait for `Run` to return, and dispose the old window before it creates the replacement.
Neither approach bypasses the native topology guard.

Process restart resets transient state unless the application saves it.
Window replacement can retain an explicit application-state model.
Neither approach automatically retains native focus, selection, scrolling, or resource state.
The user guide must identify the actual behavior.

### Errors and shutdown

Invalid source must produce a diagnostic and no partial update.
A later valid edit must recover without a manual cleanup of generated files.
Refresh errors must reach the existing error channel or a visible development diagnostic.

Recoverable errors retain the generated declarations.
The generator emits both its diagnostic and a mapped C# `#error` directive.
The C# directive blocks hot deltas that the SDK otherwise permits despite generator diagnostics.
This mechanism has no last-good-output cache.

Some errors prevent the generator from emitting the component declarations.
The SDK can request a process restart for those errors.
A corrected file starts the application again, with fresh state.
The user guide distinguishes this behavior from in-place recovery.

Reload requests must not access a closed window.
Window shutdown must revoke development subscriptions and queued work.
The release application must not poll for edits.

## VS Code package

The extension registers the `.xui` file association.
It supplies a TextMate grammar, bracket pairs, comments, indentation rules, and snippets.
The grammar distinguishes the UI language from embedded C#.

Tokenizer checks cover keywords, control names, named arguments, state declarations, and namespace declarations.
They also cover nested C# blocks, interpolated strings, comments with braces, and the transition back to UI syntax.

The extension produces an installable `.vsix`.
The package excludes development dependencies and test fixtures.
Syntax support does not imply a language server, semantic completion, or a designer.
The implementation does not publish the extension or change global VS Code configuration.

## Delivery stages

1. Define the grammar, generated API, update rules, and failure behavior.
2. Implement the parser, code generator, diagnostics, and build integration.
3. Add an application sample and restart-on-save.
4. Implement development-only live updates and explicit structural fallback.
5. Implement and package the VS Code extension.
6. Run the acceptance checks and correct failures.
7. Document the commands, supported edits, and remaining limits.

The compiler and extension can proceed in parallel after the grammar is stable.
Native build preparation can also proceed in parallel.
The end-to-end edit checks require the compiler, reload host, and native library.

## Acceptance checks

### Compiler and build

- A clean build produces an application from `.xui` inputs.
- Multiple inputs produce distinct components without unstable output.
- A no-op build preserves unchanged generated output.
- Adding, deleting, and renaming inputs updates the build correctly.
- Invalid language syntax produces a file-and-line diagnostic.
- Invalid embedded C# produces a diagnostic in the original input.
- Braces inside comments and strings do not end a block.
- Unsupported controls and arguments produce explicit diagnostics.
- State setters update their dependent controls without rebuilding the tree.
- Release output has no development reload machinery.

### Native application and watch loop

- The sample opens a real XUI window.
- A button event changes generated state and the native label.
- A `.xui` text edit updates the existing window and preserves state.
- A spacing edit updates existing layout.
- A supported handler edit changes subsequent event behavior.
- Repeated edits do not multiply event subscriptions.
- A structural edit uses the documented fallback.
- An invalid edit reports an error instead of a partial update.
- A valid edit after an invalid edit recovers.
- Closing the application leaves no orphan watcher or application process.
- The existing managed binding checks still pass.
- The focused native ABI checks still pass after any native bridge change.

### VS Code package

- Tokenizer checks exercise the real TextMate grammar.
- Embedded C# highlighting survives nested blocks and string braces.
- The extension package includes its grammar, snippets, and language configuration.
- The package excludes dependency directories and fixtures.
- The installation command uses the generated package.

### Performance evidence

The initial checks inspect generated calls and release dependencies.
They compare the generated update path with handwritten C#.
They record the complete save-to-visible-change loop where the test environment permits it.

The project must not claim a latency bound or memory improvement without measurements.
A large application still pays its compilation and startup costs after a process restart.
Per-file generation does not eliminate those costs.

## Existing code anchors

- `bindings\dotnet\Xui\Window.cs`: factories, property batches, subscriptions, and UI-thread ownership.
- `bindings\dotnet\Xui\Controls.cs`: typed controls, fluent setters, and events.
- `bindings\dotnet\Xui\Features.cs`: feature controls and immutable collection sources.
- `bindings\features.json`: existing control metadata.
- `bindings\generate_features.py`: existing binding-generation precedent.
- `src\c_api.cpp`: topology checks, handle ownership, and property updates.
- `src\c_api_features.inc`: feature construction and attachment checks.

## Progress

The compiler, development host, VS Code package, and integration checks are complete.
The native ABI and its topology rules remain unchanged.
The [user guide](../specs/xui-language.md) describes the syntax and reload behavior.
[CONTRIBUTING](../../CONTRIBUTING.md) contains the build and watch commands.

Acceptance checks passed on Windows ARM64 with .NET SDK 10.0.401:

- The generator suite passed 92 assertions.
- The real MSBuild suite passed 9 assertions, including last-input deletion and unchanged no-op timestamps.
- The native watch suite passed 56 assertions.
- The VS Code package passed 16 tokenizer and configuration tests.
- The VSIX content check found the expected eight runtime files and no development dependencies.
- The existing C# suites passed 28 wrapper assertions and 79 feature assertions.
- Both focused native ABI suites passed.
- The final NativeAOT sample published and passed native initial-text, button, state-update, and shutdown checks.

The watch suite checked native text, bounds, input values, event behavior, and process lifecycle.
It did not use successful reload messages as a substitute for native UI checks.
It also checked that invalid edits produced no layout changes or extra completion markers.
The suite covered structural replacement, severe-error restart, and both recovery paths.

The generator checks include native text/name aliasing, reserved identifiers, generic hidden dependencies, and state-free components.
Release checks reject references to the development host, compiler, and Roslyn.
The development host uses a Win32 message dispatcher instead of a native ABI extension.

The successful watch artifacts are under `build\xui-language-check\59a4faa0f6a34718b229d2bf0ce9f88e`.
That directory contains the fixture, watcher logs, build log, and edit durations.
The local extension package is `integrations\vscode-xui\dist\xui-0.1.0.vsix`.
The extension is not published or installed globally.

The initial scope remains fixed compositions and the six control names listed in this plan.
Dynamic child collections, arbitrary row templates, a language server, and a visual designer are outside this delivery.
