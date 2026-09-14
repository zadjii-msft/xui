# XUI Syntax for VS Code

This extension adds syntax highlighting and snippets for `.xui` files.
Its extension ID is `zadjii-msft.xui`. Its language ID is `xui`.

The extension supports the proposed XUI syntax. It does not supply a compiler,
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

The namespace is optional. A file can contain multiple components.
The reserved keywords are `namespace`, `component`, `state`, `view`, `code`, and `csharp`.
Built-in node names are `VStack`, `HStack`, `Text`, `Button`, `Toggle`, and `TextInput`.
Other node names receive a generic node scope. This highlighting does not imply compiler support for custom components.

The grammar includes C# expressions in control arguments and state initializers.
It includes C# members in `code csharp` blocks.
Nested braces, strings, interpolation, and comments retain their C# scopes.

VS Code supplies the built-in `source.cs` grammar.
Keep the built-in **C# Language Basics** extension enabled for embedded highlighting.
The grammar uses its `type` and `expression` rules for state types and expressions.
An incompatible or disabled C# grammar can affect embedded highlighting.
No C# language server is necessary.

Raw strings receive the highlighting available in the C# grammar.
Highlighting does not guarantee that the XUI compiler accepts a C# construct.
The compiler defines the supported expressions, state types, nodes, properties, and handlers.

## Editor support

The extension supplies comment commands, bracket matching, automatic closing pairs,
indentation rules, and indentation-based folding.
Folding markers support `// region` and `// endregion`, plus C# `#region` and `#endregion`.
The indentation rules use line patterns, not a parser. Braces inside multiline strings can affect indentation.
Incomplete strings or blocks can affect highlighting until their closing delimiter appears.

Snippet prefixes are `component`, `namespace`, `state`, `view`, `code`, `vstack`,
`hstack`, `text`, `button`, `toggle`, and `textinput`.
The component snippet supplies a counter with a named method handler.
Control snippets use placeholders for state and named method handlers.
They do not declare that state or those methods.

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
The VSIX contains only the manifest, grammar, language configuration, snippets, README, license, and VSIX metadata.

To check an existing package, run:

```powershell
npm run check:package
```
