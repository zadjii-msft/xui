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
    private void Expect(string value)
    {
        if (!Is(value)) throw new ParseError($"Expected '{value}'.", Offset);
        Take();
    }
    private string Identifier()
    {
        if (!Peek().IsKind(SyntaxKind.IdentifierToken)) throw new ParseError("Expected an identifier.", Offset);
        var name = Take().Text;
        if (name.TrimStart('@').StartsWith("__xui", StringComparison.Ordinal))
            throw new ParseError("Names starting with '__xui' are reserved.", position);
        return name;
    }
    private void Check(SyntaxNode node, int start)
    {
        var error = node.GetDiagnostics().FirstOrDefault(d => d.Severity == DiagnosticSeverity.Error);
        if (error is not null) Errors.Add(new ParseError(error.GetMessage(), start + error.Location.SourceSpan.Start));
    }
    internal Component Parse()
    {
        var ns = "";
        if (Is("namespace"))
        {
            Take();
            ns = Identifier();
            while (Is(".")) { Take(); ns += "." + Identifier(); }
            Expect(";");
        }
        Expect("component");
        var start = Offset;
        var name = Identifier();
        Expect("{");
        var states = new List<State>();
        var parameters = new List<Parameter>();
        var resources = new List<ColorResource>();
        var styles = new List<StyleDefinition>();
        Node? root = null;
        Expression code = new("", 0);
        bool hasCode = false;
        while (!Is("}"))
        {
            if (Is("resources"))
            {
                Take(); Expect("{");
                while (!Is("}"))
                {
                    int resourceStart = Offset;
                    string resourceName = Identifier();
                    Expect(":");
                    resources.Add(new(resourceName, ReadStyleExpression(), resourceStart));
                    Expect(";");
                    if (resources.Count > 256) throw new ParseError("A component supports at most 256 color resources.", resourceStart);
                }
                Expect("}");
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
                    throw new ParseError("Expected 'param Type Name;'.", Offset);
                Check(field, fieldStart);
                if (field.Modifiers.Count != 0 || field.AttributeLists.Count != 0 ||
                    field.Declaration.Variables.Count != 1 || field.Declaration.Variables[0].Initializer is not null)
                    throw new ParseError("A parameter requires one unmodified field without an initializer.", Offset);
                parameters.Add(new(field.Declaration.Type.ToString(), field.Declaration.Variables[0].Identifier.Text, fieldStart));
                position += field.FullSpan.Length;
            }
            else if (Is("state"))
            {
                Take();
                int fieldStart = position;
                var member = SyntaxFactory.ParseMemberDeclaration(text, position, consumeFullText: false);
                if (member is not FieldDeclarationSyntax field)
                    throw new ParseError("Expected 'state Type Name = initializer;'.", Offset);
                Check(field, fieldStart);
                if (field.Modifiers.Count != 0 || field.AttributeLists.Count != 0 ||
                    field.Declaration.Variables.Count != 1 || field.Declaration.Variables[0].Initializer is null)
                    throw new ParseError("State requires one unmodified field with an initializer.", Offset);
                var variable = field.Declaration.Variables[0];
                var stateName = variable.Identifier.Text;
                if (stateName.TrimStart('@').StartsWith("__xui", StringComparison.Ordinal) ||
                    states.Any(s => s.Name.TrimStart('@') == variable.Identifier.ValueText))
                    throw new ParseError("State names must be unique and cannot start with '__xui'.", Offset);
                var value = variable.Initializer!.Value;
                states.Add(new(field.Declaration.Type.ToString(), stateName,
                    new(value.ToString(), fieldStart + value.SpanStart), fieldStart));
                position += field.FullSpan.Length;
            }
            else if (Is("view"))
            {
                if (root is not null) throw new ParseError("Only one view is supported.", Offset);
                Take(); Expect("{");
                root = ParseNode();
                Expect("}");
            }
            else if (Is("code"))
            {
                if (hasCode) throw new ParseError("Only one code block is supported.", Offset);
                Take(); Expect("csharp");
                int blockStart = position;
                var parsed = SyntaxFactory.ParseStatement(text, position, consumeFullText: false);
                if (parsed is not BlockSyntax block) throw new ParseError("Expected a C# block.", Offset);
                // Parse method declarations as class members, not as local functions.
                int contentStart = blockStart + block.OpenBraceToken.Span.End;
                int contentEnd = blockStart + block.CloseBraceToken.SpanStart;
                if (block.CloseBraceToken.IsMissing) throw new ParseError("Unclosed C# block.", Offset);
                code = new(text[contentStart..contentEnd], contentStart);
                var wrapper = SyntaxFactory.ParseCompilationUnit("class C {" + code.Text + "}");
                var cls = (ClassDeclarationSyntax)wrapper.Members[0];
                var error = wrapper.GetDiagnostics().FirstOrDefault(d => d.Severity == DiagnosticSeverity.Error);
                if (error is not null)
                    Errors.Add(new ParseError(error.GetMessage(), contentStart + Math.Max(0, error.Location.SourceSpan.Start - 9)));
                if (cls.Members.Any(m => m is not MethodDeclarationSyntax))
                    throw new ParseError("code csharp supports method declarations only; declare persistent fields with state.", contentStart);
                hasCode = true;
                position += block.FullSpan.Length;
            }
            else throw new ParseError("Expected resources, style, param, state, view, code csharp, or '}'.", Offset);
        }
        Expect("}");
        if (!Peek().IsKind(SyntaxKind.EndOfFileToken))
            throw new ParseError("Only one component is supported per .xui file.", Offset);
        return new(ns, name, start, states, parameters, root ?? throw new ParseError("A component requires a view.", start), code, resources, styles);
    }
    private Node ParseNode(int depth = 0)
    {
        cancellation.ThrowIfCancellationRequested();
        if (depth >= maximumDepth)
            throw new ParseError($"Visual tooling supports at most {maximumDepth} nested nodes.", Offset);
        int start = Offset;
        string kind = Identifier();
        string[] allowed = kind switch
        {
            "VStack" or "HStack" => ["spacing", "padding"],
            "Text" => ["value"],
            "Button" => ["value", "click", "icon", "style", "background", "foreground", "borderBrush", "cornerRadius", "borderThickness", "padding"],
            "Toggle" => ["value", "checked", "change", "style", "background", "foreground", "borderBrush", "cornerRadius", "borderThickness", "padding"],
            "TextInput" => ["value", "name", "text", "change", "submit", "captionVisible", "placeholder"],
            "Grid" => ["value", "rows", "columns"],
            "DataGrid" => ["value", "columns"],
            "RangeInput" => ["value", "range", "currentValue", "orientation", "reversed", "change"],
            "Progress" => ["value", "range", "currentValue", "progressState"],
            "NavigationView" => ["value", "headerVisible", "searchId", "searchHelp", "duration"],
            "ItemsView" or "ScrollView" => ["value"],
            "Reveal" => ["value", "open", "duration", "layout", "direction"],
            "Popup" => ["value", "placement", "windowBackground"],
            "SplitView" => ["value", "secondVisible", "duration"],
            "Content" => ["value"],
            _ => throw new ParseError($"Unsupported control '{kind}'.", start)
        };
        string styleTarget = StyleCompiler.TargetName(kind);
        if (kind == "Content")
            allowed = [.. allowed, "style", .. StyleCompiler.Properties, .. StyleCompiler.ExtendedProperties];
        else if (kind == "Button" || StyleCatalog.TargetExists(styleTarget))
            allowed = [.. allowed, "style", .. StyleCompiler.AllowedProperties(styleTarget, "root")];
        bool stack = kind is "VStack" or "HStack";
        bool container = stack || kind is "Grid" or "ScrollView" or "Popup" or "SplitView" or "Reveal";
        allowed = [.. allowed, "size", "preferredSize", "ref", "row", "column", "rowSpan", "columnSpan", "flex"];
        if (!stack && kind is not ("Content" or "Grid")) allowed = [.. allowed, "id", "enabled", "visible", "help"];
        var arguments = new Dictionary<string, Expression>(StringComparer.Ordinal);
        var authoredArguments = new List<XuiSourceArgument>();
        Expect("(");
        int argumentsStart = lastTokenEnd;
        bool trailingComma = false;
        while (!Is(")"))
        {
            int argStart = Offset;
            string key;
            var token = Peek();
            int afterToken = position + token.FullSpan.Length;
            if (SyntaxFactory.ParseToken(text, afterToken).Text == ":")
            {
                key = Take().Text;
                Expect(":");
                if (key == "value")
                    Errors.Add(new ParseError("The first argument must be positional, not 'value:'.", argStart));
            }
            else
            {
                if (stack || arguments.Count != 0) throw new ParseError("Only the first control argument may be positional.", argStart);
                key = "value";
            }
            bool positional = key == "value";
            if (!allowed.Contains(key)) Errors.Add(new ParseError($"Unsupported property '{key}' on {kind}.", argStart));
            if (arguments.ContainsKey(key)) throw new ParseError($"Duplicate property '{key}'.", argStart);
            int expressionStart = position;
            var expression = SyntaxFactory.ParseExpression(text, position, consumeFullText: false);
            Check(expression, expressionStart);
            if (expression.IsMissing) throw new ParseError("Expected a C# expression.", Offset);
            if (key is "click" or "change" or "submit" or "ref" && expression is not IdentifierNameSyntax)
                throw new ParseError("Event handlers and references must be identifiers.", expressionStart);
            arguments.Add(key, new(expression.ToString(), expressionStart + expression.SpanStart));
            var valueRange = new SourceRange(expressionStart + expression.SpanStart, expression.Span.Length);
            authoredArguments.Add(new(key, positional, new(argStart, valueRange.End - argStart),
                valueRange, text.Substring(valueRange.Start, valueRange.Length), XuiSourceParser.Classify(expression)));
            position += expression.FullSpan.Length;
            trailingComma = false;
            if (!Is(",")) break;
            Take();
            trailingComma = true;
        }
        int argumentsEnd = Offset;
        Expect(")");
        if (!stack && !arguments.ContainsKey("value"))
            throw new ParseError($"{kind} requires its {(kind == "Content" ? "Element" : "string")} argument.", start);
        var children = new List<Node>();
        SourceRange? bodySpan = null;
        if (container)
        {
            Expect("{");
            int bodyStart = lastTokenEnd;
            while (!Is("}")) children.Add(ParseNode(depth + 1));
            bodySpan = new(bodyStart, Offset - bodyStart);
            Expect("}");
            int required = kind is "ScrollView" or "Popup" or "Reveal" ? 1 : kind == "SplitView" ? 2 : -1;
            if (required >= 0 && children.Count != required)
                throw new ParseError($"{kind} requires exactly {required} content children.", start);
        }
        else
        {
            Expect(";");
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
