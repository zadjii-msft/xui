# XUI Syntax for VS Code

This extension adds syntax highlighting and snippets for `.xui` files.
Its extension ID is `zadjii-msft.xui`. Its language ID is `xui`.

The extension supports the XUI syntax. It does not supply a compiler,
a language server, diagnostics, IntelliSense, formatting, or semantic binding checks.

## Install from source

Use Node.js 22 or later and npm. The commands also work with Node.js 25.

From the repository root, run:

```powershell
cd integrations\vscode-xui
npm ci
npm run package
```

The package command runs tokenizer tests and checks the VSIX contents.
It creates `dist\xui-0.1.0.vsix`. It does not publish or install the extension.

To install the local package, run:

```powershell
code --install-extension .\dist\xui-0.1.0.vsix
```

Alternatively, run **Extensions: Install from VSIX...** in the VS Code Command Palette.
Then select the VSIX file.

## Syntax

```xui
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

The namespace is optional. The compiler accepts one component per file.
The grammar can highlight multiple components, but this does not imply compiler support.
The grammar recognizes `namespace`, `component`, `param`, `state`, `view`, `code`, `csharp`, `resources`, `style`, `basedOn`, and `when`.
It recognizes the native node names in the [language guide](https://github.com/zadjii-msft/xui/blob/main/docs/specs/xui-language.md).
Other node names receive a generic node scope. This highlighting does not imply compiler support for custom components.

The grammar includes C# expressions in control arguments and state initializers.
It includes C# members in `code csharp` blocks.
Nested braces, strings, interpolation, and comments retain their C# scopes.
Ordinary C# calls named `theme` or `resource` retain their C# scopes outside style values and color arguments.

VS Code supplies the built-in `source.cs` grammar.
Keep the built-in **C# Language Basics** extension enabled for embedded highlighting.
The grammar uses its `type` and `expression` rules for state types and expressions.
An incompatible or disabled C# grammar can affect embedded highlighting.
No C# language server is necessary.

Raw strings receive the highlighting available in the C# grammar.
Highlighting does not guarantee that the XUI compiler accepts a C# construct.
The compiler defines the supported expressions, state types, nodes, properties, and handlers.
The compiler rejects `name:` on `Text`, `Button`, and `Toggle`.
`TextInput` supports `name:`.

Named Button styles and color resources have XUI scopes:

```xui
resources {
  DangerFill: theme(light: 0xB42318, dark: 0x8F1D16);
}
style DangerButton for Button {
  background: resource(DangerFill);
  borderThickness: (3, 0, 0, 0);
  when hovered { cornerRadius: 0; }
}
style CompactDanger for Button basedOn DangerButton {
  padding: (8, 2, 8, 2);
}
```

These declarations belong inside a component, beside its `view` block.
`Button("Delete", style: DangerButton);` applies the named style.
The grammar recognizes `focused`, `checked`, `hovered`, `pressed`, and `disabled` state blocks.
It also recognizes `basedOn` derivation, sparse properties, resource aliases, and the `theme` color function.
Style declarations and style references receive separate scopes.
Comments and line breaks can separate style headers, state names, and color function arguments.
Color literals retain their C# numeric scopes, including hexadecimal, binary, decimal, and integer suffixes.
The [style grammar](https://github.com/zadjii-msft/xui/blob/main/docs/specs/xui-language.md#declare-button-styles-and-resources) defines the compiler limits.
Highlighting does not check resource names, cycles, property types, or numeric bounds.

## Editor support

The extension supplies comment commands, bracket matching, automatic closing pairs,
indentation rules, and indentation-based folding.
Folding markers support `// region` and `// endregion`, plus C# `#region` and `#endregion`.
The indentation rules use line patterns, not a parser. Braces inside multiline strings can affect indentation.
Incomplete strings or blocks can affect highlighting until their closing delimiter appears.
Style values recover at a closing brace or a new property line after a missing semicolon.
This recovery aids editing. It does not make incomplete syntax valid.

Snippet prefixes are `component`, `namespace`, `state`, `view`, `code`, `vstack`,
`hstack`, `text`, `button`, `toggle`, `textinput`, `resources`, `style`, `when`, `stylebasedon`, and `styledbutton`.
The component snippet supplies a counter with a named method handler.
Control snippets use placeholders for state and named method handlers.
They do not declare that state or those methods.
The style snippets refer to resources or base styles that the component must declare.

## Development

Run the tokenizer tests:

```powershell
npm test
```

The tests use `vscode-textmate`, `vscode-oniguruma`, and a pinned upstream C# grammar.
The first run downloads the C# grammar and its MIT license into `test\cache`.
Later runs use the cache after a SHA-256 check. The first run requires network access to GitHub.
The upstream revision and attribution are in `test\fixtures\NOTICE.md`.

The tests cover embedded scopes, nested delimiters, interpolation, comments, snippets,
and recovery into XUI after C# blocks.
Style tests cover references, theme colors, numeric literals, incomplete declarations, and recovery after missing delimiters.
Some tokenizer fixtures exceed the compiler subset to exercise lexical recovery.
The VSIX contains only the manifest, grammar, language configuration, snippets, README, license, and VSIX metadata.

To check an existing package, run:

```powershell
npm run check:package
```
