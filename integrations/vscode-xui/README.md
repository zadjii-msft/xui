# XUI for VS Code

IntelliSense, navigation, syntax highlighting, and snippets for `.xui` files.
Extension ID: `zadjii-msft.xui`. Language ID: `xui`.

## Use IntelliSense

Open an `.xui` file and type, or press **Ctrl+Space**:

- In a `view` or container body, choose a control to insert its constructor and child block.
- Inside a control's arguments, choose a property supported by that control.
  Already-authored properties are omitted. Grid placement and Stack `flex` follow the parent.
- After `:`, choose boolean or enum values, component state, parameters, or named handlers.
- In styles, choose targets, parts, states, properties, and bounded enum literals.
  Style references match the control target; color references use the component's resources.

Hover over controls and properties for documentation. **Ctrl+Shift+Space** shows a
control's signature. **F12** navigates local XUI references to their declarations.
The Outline lists the component, state, parameters, styles, resources, references, and methods.

```xui
component Counter {
  state int Count = 0;
  view {
    VStack(spacing: 8, padding: 16) {
      Text($"Count: {Count}");
      Button("Increment", click: Increment);
    }
  }
  code csharp {
    void Increment() => Count++;
  }
}
```

Suggestions work on unsaved files and incomplete declarations without running your
application, invoking .NET, or starting a language server.
XUI suggestions do not appear inside comments, strings, or `code csharp`.
Local member suggestions are lexical, not C# type checking.
This extension does not supply C# member completion, cross-file navigation,
diagnostics, rename, formatting, a compiler, or a visual designer.

## Highlighting and snippets

The extension includes embedded C# highlighting, comment commands, bracket matching,
closing pairs, indentation rules, and folding.
Keep VS Code's built-in **C# Language Basics** extension enabled for highlighting.
No C# language server is required.

Snippet prefixes include `component`, `namespace`, `state`, `view`, `code`,
`vstack`, `hstack`, `button`, `textinput`, `resources`, `style`, `part`, and `when`.
Control and style snippets can reference state, handlers, or resources that you
must declare. The compiler remains authoritative for supported expressions,
types, ownership, style bounds, and invalid declarations.

See the [XUI language guide](https://github.com/zadjii-msft/xui/blob/main/docs/specs/xui-language.md)
for syntax, all supported controls, style examples, and editor limits.

## Install

Choose **Extensions: Install from VSIX...** in VS Code and select `xui-0.3.0.vsix`,
or run:

```powershell
code --install-extension .\integrations\vscode-xui\dist\xui-0.3.0.vsix
```

Build the local VSIX using the repository's
[contributor instructions](https://github.com/zadjii-msft/xui/blob/main/CONTRIBUTING.md#vs-code-extension).
Packaging does not install or publish the extension.

After upgrading from the syntax-only extension, run **Developer: Reload Window**
in VS Code. Snippets and highlighting can appear before the new IntelliSense runtime
has loaded. If only snippets appear, confirm XUI is enabled in the current profile
and the document's language mode is **XUI**, then reload the window.
