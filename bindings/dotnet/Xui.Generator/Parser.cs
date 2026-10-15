using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.CSharp.Syntax;
using Microsoft.CodeAnalysis.Text;

namespace Xui.Generator;

internal sealed record Expression(string Text, int Offset);
internal sealed record State(string Type, string Name, Expression Initializer, int Offset);
internal sealed record Parameter(string Type, string Name, int Offset);
internal sealed record Node(string Kind, int Offset, Dictionary<string, Expression> Arguments, List<Node> Children)
{
    internal SourceRange Span { get; init; }
    internal SourceRange ArgumentsSpan { get; init; }
    internal SourceRange? BodySpan { get; init; }
    internal IReadOnlyList<XuiSourceArgument> AuthoredArguments { get; init; } = [];
    internal IReadOnlyList<string> SupportedArguments { get; init; } = [];
    internal bool HasTrailingComma { get; init; }
}
internal sealed record Component(string Namespace, string Name, int Offset, List<State> States, List<Parameter> Parameters, Node Root, Expression Code,
    List<ColorResource> Resources, List<StyleDefinition> Styles);
internal sealed class ParseError(string message, int offset) : Exception(message)
{
    internal int Offset { get; } = offset;
}

internal sealed partial class Parser(string text, CancellationToken cancellation = default, int maximumDepth = int.MaxValue)
{
    private int position;
    private int lastTokenEnd;
    internal List<ParseError> Errors { get; } = [];
    private SyntaxToken Peek() => SyntaxFactory.ParseToken(text, position);
    private bool Is(string value) => Peek().Text == value;
    private SyntaxToken Take()
    {
        cancellation.ThrowIfCancellationRequested();
        var token = Peek();
        lastTokenEnd = position + token.Span.End;
        position += token.FullSpan.Length;
        return token;
    }
    private int Offset => position + Peek().LeadingTrivia.FullSpan.Length;
    private string Found => Peek().IsKind(SyntaxKind.EndOfFileToken) ? "end of file" : DiagnosticText.Quote(Peek().Text);
    private void Expect(string value, string context, string? correction = null)
    {
        if (!Is(value))
            throw new ParseError($"Expected '{value}' {context}, but found {Found}. " +
                (correction ?? $"Add '{value}' {context}."), Offset);
        Take();
    }
    private void CheckBlockEnd(string context)
    {
        if (Peek().IsKind(SyntaxKind.EndOfFileToken))
            throw new ParseError($"Unclosed {context}. Add '}}' to close this block.", Offset);
    }
    private void RejectBlockSemicolon(string context)
    {
        if (Is(";"))
            throw new ParseError($"Unexpected ';' after {context}. Remove the semicolon: blocks end with '}}', not '}};'.", Offset);
    }
    private void CloseBlock(string context)
    {
        Expect("}", $"to close {context}");
        RejectBlockSemicolon(context);
    }
    private string Identifier(string role)
    {
        if (!Peek().IsKind(SyntaxKind.IdentifierToken))
            throw new ParseError($"Expected {role}, but found {Found}. Use a C# identifier, not punctuation or a quoted string.", Offset);
        int start = Offset;
        var name = Take().Text;
        if (name.TrimStart('@').StartsWith("__xui", StringComparison.Ordinal))
            throw new ParseError($"Name '{name}' starts with the reserved prefix '__xui'. Rename it without that prefix.", start);
        return name;
    }
    private void Check(SyntaxNode node, int start, string context)
    {
        var error = node.GetDiagnostics().FirstOrDefault(d => d.Severity == DiagnosticSeverity.Error);
        if (error is not null)
            Errors.Add(new ParseError($"Invalid C# {context}: {error.GetMessage()}", start + error.Location.SourceSpan.Start));
    }
    internal Component Parse()
    {
        var ns = "";
        if (Is("namespace"))
        {
            Take();
            ns = Identifier("a namespace name after 'namespace'");
            while (Is(".")) { Take(); ns += "." + Identifier("a namespace segment after '.'"); }
            Expect(";", "after the namespace declaration");
        }
        Expect("component", "at the start of the component declaration", "Declare one component with 'component Name { view { ... } }'.");
        var start = Offset;
        var name = Identifier("a component name after 'component'");
        Expect("{", $"to begin component '{name}'");
        var states = new List<State>();
        var parameters = new List<Parameter>();
        var resources = new List<ColorResource>();
        var styles = new List<StyleDefinition>();
        Node? root = null;
        Expression code = new("", 0);
        bool hasCode = false;
        while (!Is("}"))
        {
            CheckBlockEnd($"component '{name}'");
            if (Is("resources"))
            {
                Take(); Expect("{", "after 'resources'");
                while (!Is("}"))
                {
                    CheckBlockEnd($"resources block in component '{name}'");
                    int resourceStart = Offset;
                    string resourceName = Identifier("a color resource name inside 'resources'");
                    Expect(":", $"after resource name '{resourceName}'");
                    resources.Add(new(resourceName, ReadStyleExpression($"color resource '{resourceName}'"), resourceStart));
                    Expect(";", $"after the value of resource '{resourceName}'");
                    if (resources.Count > 256)
                        throw new ParseError($"Component '{name}' has more than 256 color resources. Remove or combine resources to stay within this limit.", resourceStart);
                }
                CloseBlock($"the resources block in component '{name}'");
            }
            else if (Is("style"))
            {
                styles.Add(ParseStyle());
            }
            else if (Is("param"))
            {
                Take();
                int fieldStart = position;
                var member = SyntaxFactory.ParseMemberDeclaration(text, position, consumeFullText: false);
                if (member is not FieldDeclarationSyntax field)
                    throw new ParseError($"Invalid parameter declaration; found {Found}. Declare a parameter as 'param Type Name;', for example 'param string Title;'.", Offset);
                Check(field, fieldStart, "parameter declaration");
                if (field.Modifiers.Count != 0 || field.AttributeLists.Count != 0 ||
                    field.Declaration.Variables.Count != 1 || field.Declaration.Variables[0].Initializer is not null)
                    throw new ParseError("A parameter requires one name, no attributes or modifiers, and no initializer. Use 'param Type Name;' and supply its value through the component constructor.", Offset);
                parameters.Add(new(field.Declaration.Type.ToString(), field.Declaration.Variables[0].Identifier.Text, fieldStart));
                position += field.FullSpan.Length;
            }
            else if (Is("state"))
            {
                Take();
                int fieldStart = position;
                var member = SyntaxFactory.ParseMemberDeclaration(text, position, consumeFullText: false);
                if (member is not FieldDeclarationSyntax field)
                    throw new ParseError($"Invalid state declaration; found {Found}. Declare state as 'state Type Name = initializer;', for example 'state int Count = 0;'.", Offset);
                Check(field, fieldStart, "state declaration");
                if (field.Modifiers.Count != 0 || field.AttributeLists.Count != 0 ||
                    field.Declaration.Variables.Count != 1 || field.Declaration.Variables[0].Initializer is null)
                    throw new ParseError("State requires one name, an initializer, and no attributes or modifiers. Use 'state Type Name = initializer;', for example 'state int Count = 0;'.", Offset);
                var variable = field.Declaration.Variables[0];
                var stateName = variable.Identifier.Text;
                if (stateName.TrimStart('@').StartsWith("__xui", StringComparison.Ordinal))
                    throw new ParseError($"State name '{stateName}' starts with the reserved prefix '__xui'. Rename it without that prefix.", fieldStart + variable.Identifier.SpanStart);
                if (states.Any(s => s.Name.TrimStart('@') == variable.Identifier.ValueText))
                    throw new ParseError($"Duplicate state name '{stateName}'. Remove the repeated declaration or give it a different name.", fieldStart + variable.Identifier.SpanStart);
                var value = variable.Initializer!.Value;
                states.Add(new(field.Declaration.Type.ToString(), stateName,
                    new(value.ToString(), fieldStart + value.SpanStart), fieldStart));
                position += field.FullSpan.Length;
            }
            else if (Is("view"))
            {
                if (root is not null)
                    throw new ParseError($"Component '{name}' already has a view. Merge these controls into its existing view; only one view block is supported.", Offset);
                Take(); Expect("{", "after 'view'");
                if (Is("}"))
                    throw new ParseError("The view block is empty. Add one root control, for example 'VStack() {}'.", Offset);
                root = ParseNode();
                if (Peek().IsKind(SyntaxKind.IdentifierToken))
                    throw new ParseError($"The view already has a root control, '{root.Kind}', but found another control {Found}. Wrap sibling controls in one VStack, HStack, or Grid.", Offset);
                CloseBlock($"the view block in component '{name}'");
            }
            else if (Is("code"))
            {
                if (hasCode)
                    throw new ParseError($"Component '{name}' already has a code csharp block. Move these methods into the existing block.", Offset);
                Take(); Expect("csharp", "after 'code'", "Use 'code csharp { ... }' for component methods.");
                if (!Is("{"))
                    throw new ParseError($"Expected '{{' to begin the code csharp block, but found {Found}. Put component methods inside 'code csharp {{ ... }}'.", Offset);
                int blockStart = position;
                var parsed = SyntaxFactory.ParseStatement(text, position, consumeFullText: false);
                if (parsed is not BlockSyntax block)
                    throw new ParseError($"Invalid code csharp block in component '{name}'. Put method declarations inside 'code csharp {{ ... }}'.", Offset);
                // Parse method declarations as class members, not as local functions.
                int contentStart = blockStart + block.OpenBraceToken.Span.End;
                int contentEnd = blockStart + block.CloseBraceToken.SpanStart;
                if (block.CloseBraceToken.IsMissing)
                    throw new ParseError($"Unclosed code csharp block in component '{name}'. Add the missing '}}' after the methods.", blockStart + block.CloseBraceToken.SpanStart);
                code = new(text[contentStart..contentEnd], contentStart);
                var wrapper = SyntaxFactory.ParseCompilationUnit("class C {" + code.Text + "}");
                var cls = (ClassDeclarationSyntax)wrapper.Members[0];
                var error = wrapper.GetDiagnostics().FirstOrDefault(d => d.Severity == DiagnosticSeverity.Error);
                if (error is not null)
                    Errors.Add(new ParseError($"Invalid C# in the code csharp block: {error.GetMessage()}", contentStart + Math.Max(0, error.Location.SourceSpan.Start - 9)));
                var invalidMember = cls.Members.FirstOrDefault(m => m is not MethodDeclarationSyntax);
                if (invalidMember is not null)
                    throw new ParseError("The code csharp block supports method declarations only. Move persistent fields to 'state Type Name = initializer;' declarations outside this block.",
                        contentStart + Math.Max(0, invalidMember.SpanStart - 9));
                hasCode = true;
                position += block.FullSpan.Length;
                RejectBlockSemicolon("the code csharp block");
            }
            else
                throw new ParseError($"Unexpected {Found} inside component '{name}'. Use resources, style, param, state, view, or code csharp declarations; put controls inside view and close the component with '}}'.", Offset);
        }
        CloseBlock($"component '{name}'");
        if (!Peek().IsKind(SyntaxKind.EndOfFileToken))
            throw new ParseError($"Unexpected {Found} after component '{name}'. A .xui file supports one component; move another component to its own .xui file or remove the trailing text.", Offset);
        return new(ns, name, start, states, parameters,
            root ?? throw new ParseError($"Component '{name}' has no view. Add 'view {{ VStack() {{}} }}' with one root control.", start),
            code, resources, styles);
    }
    private Node ParseNode(int depth = 0, string? parent = null)
    {
        cancellation.ThrowIfCancellationRequested();
        if (depth >= maximumDepth)
            throw new ParseError($"Visual tooling supports at most {maximumDepth} nested controls. Flatten this hierarchy by removing unnecessary container layers.", Offset);
        int start = Offset;
        string kind = Identifier(parent is null ? "a root control name inside 'view'" : $"a child control name inside '{parent}'");
        string[] allowed = kind switch
        {
            "VStack" or "HStack" => ["spacing", "padding"],
            "Text" => ["value"],
            "Button" => ["value", "click", "icon", "verticalText"],
            "Toggle" or "ToggleSwitch" => ["value", "checked", "change"],
            "ToggleButton" => ["value", "checked", "change", "icon"],
            "CheckBox" => ["value", "checkState", "threeState", "change"],
            "HyperlinkButton" => ["value", "click", "icon"],
            "SelectorBar" => ["value", "items", "selected", "change"],
            "InfoBadge" => ["value", "count", "icon"],
            "MenuBar" => ["value", "commands", "invoke", "pin"],
            "TextInput" => ["value", "name", "text", "change", "submit", "captionVisible", "placeholder"],
            "Grid" => ["value", "rows", "columns"],
            "DataGrid" => ["value", "columns"],
            "RangeInput" => ["value", "range", "currentValue", "orientation", "reversed", "change"],
            "Progress" or "ProgressRing" => ["value", "range", "currentValue", "progressState"],
            "NavigationView" => ["value", "headerVisible", "searchId", "searchHelp", "duration"],
            "ItemsView" or "ScrollView" or "SwapChainPanel" => ["value"],
            "Reveal" => ["value", "open", "duration", "layout", "direction"],
            "Popup" => ["value", "placement", "windowBackground"],
            "SplitView" => ["value", "secondVisible", "duration"],
            "AdaptiveLayout" => ["value", "presentation", "navigationSide", "navigationExtent", "navigationOpen", "dismissOnFocusOutside", "breakpoint", "compactNavigation"],
            "Content" => ["value"],
            _ => throw new ParseError($"Control '{kind}' is not supported in .xui. Use a supported control such as Text, Button, VStack, or HStack; see docs/specs/xui-language.md for the complete list.", start)
        };
        string styleTarget = StyleCompiler.TargetName(kind);
        if (kind == "Content")
            allowed = [.. allowed, "style", .. StyleCompiler.LocalProperties];
        else if (StyleCatalog.TargetExists(styleTarget))
            allowed = [.. allowed, "style", .. StyleCompiler.AllowedProperties(styleTarget, "root")];
        bool stack = kind is "VStack" or "HStack";
        bool container = stack || kind is "Grid" or "ScrollView" or "Popup" or "SplitView" or "Reveal" or "AdaptiveLayout";
        allowed = [.. allowed, "size", "preferredSize", "ref", "row", "column", "rowSpan", "columnSpan", "flex"];
        if (!stack && kind is not ("Content" or "Grid" or "AdaptiveLayout")) allowed = [.. allowed, "id", "enabled", "visible", "help"];
        var arguments = new Dictionary<string, Expression>(StringComparer.Ordinal);
        var authoredArguments = new List<XuiSourceArgument>();
        Expect("(", $"after control name '{kind}'", $"Write '{kind}(...)' before its {(container ? "child block" : "terminating ';'")}.");
        int argumentsStart = lastTokenEnd;
        bool trailingComma = false;
        while (!Is(")"))
        {
            if (Peek().IsKind(SyntaxKind.EndOfFileToken))
                throw new ParseError($"Unclosed argument list of {kind}. Add ')' after its arguments, then {(container ? "'{ ... }' for its children" : "';' to finish the control")}.", Offset);
            int argStart = Offset;
            string key;
            var token = Peek();
            int afterToken = position + token.FullSpan.Length;
            if (SyntaxFactory.ParseToken(text, afterToken).Text == ":")
            {
                key = Take().Text;
                Expect(":", $"after argument name '{key}' on {kind}");
                if (key == "value")
                    Errors.Add(new ParseError($"The first argument of {kind} must be positional, not 'value:'. Remove 'value:' and put this value before any named arguments.", argStart));
            }
            else
            {
                if (stack)
                    throw new ParseError($"{kind} does not accept positional arguments. Name each argument, for example 'spacing: 8' or 'padding: 16'.", argStart);
                if (arguments.Count != 0)
                    throw new ParseError($"Unexpected positional argument in {kind}. Only its first argument can be positional; write later arguments as 'name: value'.", argStart);
                key = "value";
            }
            bool positional = key == "value";
            if (!allowed.Contains(key))
                Errors.Add(new ParseError($"Unknown argument '{key}' on {kind}. " +
                    (key == "name" && kind is "Text" or "Button" or "Toggle" or "ToggleSwitch" or "ToggleButton"
                        ? "Its first positional argument supplies both text and the accessible name; remove 'name:'."
                        : "Remove or replace this argument. " + DiagnosticText.Choices(allowed.Where(a => a != "value"))), argStart));
            if (arguments.ContainsKey(key))
                throw new ParseError($"Duplicate argument '{key}' on {kind}. Keep one value for this argument and remove the repeated entry.", argStart);
            int expressionStart = position;
            var expression = SyntaxFactory.ParseExpression(text, position, consumeFullText: false);
            if (expression.IsMissing)
                throw new ParseError($"Missing value for {(positional ? "the first argument" : $"argument '{key}'")} on {kind}; found {Found}. Supply a C# expression before the next ',' or ')'.", Offset);
            Check(expression, expressionStart, $"expression for {(positional ? "the first argument" : $"argument '{key}'")} on {kind}");
            if (key is "click" or "change" or "submit" or "ref" && expression is not IdentifierNameSyntax)
                throw new ParseError($"Argument '{key}' on {kind} requires a single identifier, not {DiagnosticText.Quote(expression.ToString())}. " +
                    (key == "ref" ? "Use 'ref: ControlName' to declare a typed control reference." : $"Use '{key}: HandlerName' without parentheses or a lambda."), expressionStart + expression.SpanStart);
            arguments.Add(key, new(expression.ToString(), expressionStart + expression.SpanStart));
            var valueRange = new SourceRange(expressionStart + expression.SpanStart, expression.Span.Length);
            authoredArguments.Add(new(key, positional, new(argStart, valueRange.End - argStart),
                valueRange, text.Substring(valueRange.Start, valueRange.Length), XuiSourceParser.Classify(expression)));
            position += expression.FullSpan.Length;
            trailingComma = false;
            if (!Is(","))
            {
                if (!Is(")"))
                    throw new ParseError($"Expected ',' between {kind} arguments or ')' to end its argument list, but found {Found}. " +
                        (Peek().IsKind(SyntaxKind.EndOfFileToken) ? "Add ')' to close the argument list." : "Separate arguments with ','; put any terminating ';' after ')'."), Offset);
                break;
            }
            Take();
            trailingComma = true;
        }
        int argumentsEnd = Offset;
        Expect(")", $"to end the argument list of {kind}");
        if (!stack && !arguments.ContainsKey("value"))
            throw new ParseError($"{kind} is missing its first positional {(kind == "Content" ? "Element" : "string")} argument. " +
                (kind == "Content" ? "Use 'Content(element)' with an existing Xui.Element." : $"Use '{kind}(\"Name\", ...)' and put named arguments after the string."), start);
        var children = new List<Node>();
        SourceRange? bodySpan = null;
        if (container)
        {
            Expect("{", $"to begin the child block of {kind}", $"{kind} is a container. Put its children inside '{kind}(...) {{ ... }}', not after a ';'.");
            int bodyStart = lastTokenEnd;
            while (!Is("}"))
            {
                CheckBlockEnd($"the child block of '{kind}'");
                children.Add(ParseNode(depth + 1, kind));
            }
            bodySpan = new(bodyStart, Offset - bodyStart);
            CloseBlock($"the child block of '{kind}'");
            int required = kind is "ScrollView" or "Popup" or "Reveal" ? 1 : kind is "SplitView" or "AdaptiveLayout" ? 2 : -1;
            if (required >= 0 && children.Count != required)
                throw new ParseError($"{kind} requires exactly {required} direct child control{(required == 1 ? "" : "s")}, but found {children.Count}. " +
                    (children.Count > required ? "Wrap sibling controls in a VStack or HStack to group them into the required number of children." : "Add the missing child controls inside its '{ ... }' block."), start);
        }
        else
        {
            Expect(";", $"after {kind}'s closing ')'", Is("{")
                ? $"{kind} cannot contain child controls. End it with ';' and move the children into a VStack, HStack, or Grid."
                : $"Finish this leaf control as '{kind}(...);' before the next control or closing brace.");
        }
        return new(kind, start, arguments, children)
        {
            Span = new(start, lastTokenEnd - start),
            ArgumentsSpan = new(argumentsStart, argumentsEnd - argumentsStart),
            BodySpan = bodySpan,
            AuthoredArguments = authoredArguments.AsReadOnly(),
            SupportedArguments = Array.AsReadOnly(allowed.Distinct(StringComparer.Ordinal).ToArray()),
            HasTrailingComma = trailingComma
        };
    }
}
