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
The grammar recognizes `namespace`, `component`, `param`, `state`, `view`, `code`, `csharp`, `resources`, `style`, `basedOn`, `part`, and `when`.
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

### Control styles and resources

Named styles and color resources have XUI scopes:

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
State names include `focused`, `checked`, `hovered`, `pressed`, and `disabled`, plus catalog states such as `selected`, `expanded`, and `readOnly`.
It also recognizes `basedOn` derivation, sparse properties, resource aliases, and the `theme` color function.
Style declarations and style references receive separate scopes.
Comments and line breaks can separate style headers, state names, and color function arguments.
Color literals retain their C# numeric scopes, including hexadecimal, binary, decimal, and integer suffixes.
The [style grammar](https://github.com/zadjii-msft/xui/blob/main/docs/specs/xui-language.md#declare-control-styles-and-resources) defines the compiler limits.
Highlighting does not check resource names, cycles, property types, numeric bounds, or target and part combinations.

Style targets use canonical PascalCase names from the
[generated catalog](https://github.com/zadjii-msft/xui/blob/main/bindings/dotnet/Xui.Generator/StyleCatalog.g.cs).
The grammar covers its 45 Element-applicable targets and 221 part schemas.
Aliases are `Text` for `Label`, and `VStack` or `HStack` for `Stack`.
Parts, properties, and states use lower camel case.
`Tooltip` requires the Window API and is not a `.xui` style target.
`ContentDialog`, `CommandSurface`, `LocationPicker`, and `ViewPicker` are facades, not style targets.
Their actual root target is `Popup`.

The application node set remains `VStack`, `HStack`, `Text`, `Button`, `Toggle`, `TextInput`, `Grid`,
`DataGrid`, `NavigationView`, `ItemsView`, `ScrollView`, `Popup`, `SplitView`, and `Content`.
Other catalog targets do not add constructors.
Use `Content(existingElement, style: NamedStyle);` to style an existing element of the matching target.
Local style properties on `Content` require a named style.
A legacy Button style requires a `Button` node, not `Content`.

Parts belong directly inside a style.
They can contain properties and `when` rules, but not other parts.
State rules cannot contain parts or other state rules.
The root is implicit; `part root` is invalid.
Base styles must target the same control type.
Generic styles reject duplicate parts and duplicate state blocks for one part.
Six-property legacy Button styles retain repeated-state compatibility.

The grammar recognizes the catalog vocabulary, not every valid combination.
The compiler checks each part's properties, states, state-specific properties, and value limits.
For example, base `ItemsView` `tile.width` is valid, but `tile.width` in a state rule is not.
Highlighting alone does not establish that a declaration is valid.

### Toggle styles

A Toggle style can declare named parts:

```xui
style CompactToggle for Toggle {
  foreground: theme(light: 0x202020, dark: 0xEEEEEE);
  part indicator {
    background: theme(light: 0xEEEEEE, dark: 0x202020);
    cornerRadius: 3;
    size: 18;
    when checked { background: 0x2468AD; }
  }
  part mark { foreground: 0xFFFFFF; }
  when disabled { foreground: 0x888888; }
}
```

`Toggle("Active", style: CompactToggle);` applies this style.
Use the following property sets:

| Toggle part | Properties |
|---|---|
| Implicit root | `background`, `foreground`, `borderBrush`, `borderThickness`, `cornerRadius`, `padding`, typography and alignment |
| `label` | `foreground`, typography and alignment |
| `indicator` | `background`, `borderBrush`, `borderThickness`, `cornerRadius`, `size` |
| `mark` | `foreground` |

`size` specifies the indicator's outer square in DIPs.
A label without its own foreground inherits the effective root foreground.
Toggle root and part-local rules accept `focused`, `checked`, `hovered`, `pressed`, and `disabled`.
Button styles also support parts: `label`, `icon`, and `arrow`.
Scalar Button `size` applies to `icon` and `arrow`, not the root.

### Typography and metrics

```xui
style Heading for Label {
  fontFamily: "Segoe UI";
  fontSize: 20;
  fontWeight: 600;
  fontStyle: normal;
  horizontalAlignment: start;
  wrapping: true;
  maximumLines: 2;
}
```

Apply it with `Text("Title", style: Heading);` inside the component's view.
The following value syntax applies only where the catalog permits the property:

| Properties | Values |
|---|---|
| Color properties | RGB24 integer, `resource(Name)`, or `theme(light: RGB24, dark: RGB24)` |
| `padding`, `borderThickness` | One dimension or four dimensions in left, top, right, bottom order |
| `fontFamily` | Nonempty C# string literal with valid Unicode and no NUL; at most 1024 UTF-8 bytes and the part's UTF-16 limit |
| `fontSize` | Positive numeric literal within the part's limit |
| `fontWeight` | Integer from 1 through 999 |
| `fontStyle` | Bare `normal`, `italic`, or `oblique`, subject to the part's limit |
| `horizontalAlignment`, `verticalAlignment` | Bare `start`, `center`, `end`, or `stretch`, subject to the part's limit |
| `wrapping` | `true` or `false` |
| `maximumLines` | Integer from 0 through 32768 |
| `cornerRadius`, `size`, `spacing`, `headerHeight`, `indentation`, `thickness`, `width`, `height`, `rowGap`, `columnGap` | Numeric literal from 0 through 32768 DIPs |
| `rowHeight` | Positive dimension, at most 32768 DIPs |

Native text parts limit fonts to 512 DIPs, 31 UTF-16 family code units, and normal or italic style.
Paragraph parts reject vertical stretch.
The compiler, not the tokenizer, checks these limits.

Node `size: (width, height)` remains structural size, not a scalar style metric.
Stack node `padding` and `spacing` remain structural float overrides.
Padding inside a style still accepts four-edge insets.

## Editor support

The extension supplies comment commands, bracket matching, automatic closing pairs,
indentation rules, and indentation-based folding.
Folding markers support `// region` and `// endregion`, plus C# `#region` and `#endregion`.
The indentation rules use line patterns, not a parser. Braces inside multiline strings can affect indentation.
Incomplete strings or blocks can affect highlighting until their closing delimiter appears.
Style values recover at a closing brace or a new property, part, or state-rule line after a missing semicolon.
This recovery aids editing. It does not make incomplete syntax valid.

Snippet prefixes are `component`, `namespace`, `state`, `view`, `code`, `vstack`,
`hstack`, `text`, `button`, `toggle`, `textinput`, `resources`, `style`, `when`, `stylebasedon`, and `styledbutton`.
Use `togglestyle` for a Toggle style and `styledtoggle` to apply a declared Toggle style.
Additional style prefixes are `controlstyle`, `part`, `typographystyle`, and `styledcontent`.
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
They also cover Toggle parts, part-local states, and recovery after unsupported nesting.
Catalog tests cover every exported Element target, part, property, and state without adding application constructors.
Some tokenizer fixtures exceed the compiler subset to exercise lexical recovery.
The VSIX contains only the manifest, grammar, language configuration, snippets, README, license, and VSIX metadata.

To update the catalog vocabulary from a validated compiler commit, run:

```powershell
node scripts\sync-style-catalog.mjs <validated-git-ref>
npm run package
```

The script reads the committed generated catalog and checks its size against the native catalog.
It updates four grammar rules and the test snapshot, excluding Window-only Tooltip.
The snapshot records its source commit.
Review parser aliases, value syntax, and snippets separately when the compiler contract changes.

To check an existing package, run:

```powershell
npm run check:package
```
