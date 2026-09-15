using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.CSharp.Syntax;
using Microsoft.CodeAnalysis.Text;

namespace Xui.Generator;

internal sealed record Expression(string Text, int Offset);
internal sealed record State(string Type, string Name, Expression Initializer, int Offset);
internal sealed record Node(string Kind, int Offset, Dictionary<string, Expression> Arguments, List<Node> Children);
internal sealed record Component(string Namespace, string Name, int Offset, List<State> States, Node Root, Expression Code);
internal sealed class ParseError(string message, int offset) : Exception(message)
{
    internal int Offset { get; } = offset;
}

internal sealed class Parser(string text)
{
    private int position;
    internal List<ParseError> Errors { get; } = [];
    private SyntaxToken Peek() => SyntaxFactory.ParseToken(text, position);
    private bool Is(string value) => Peek().Text == value;
    private SyntaxToken Take()
    {
        var token = Peek();
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
        Node? root = null;
        Expression code = new("", 0);
        bool hasCode = false;
        while (!Is("}"))
        {
            if (Is("state"))
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
                if (root.Kind is not ("VStack" or "HStack"))
                    throw new ParseError("The view root must be VStack or HStack.", root.Offset);
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
            else throw new ParseError("Expected state, view, code csharp, or '}'.", Offset);
        }
        Expect("}");
        if (!Peek().IsKind(SyntaxKind.EndOfFileToken))
            throw new ParseError("Only one component is supported per .xui file.", Offset);
        return new(ns, name, start, states, root ?? throw new ParseError("A component requires a view.", start), code);
    }
    private Node ParseNode()
    {
        int start = Offset;
        string kind = Identifier();
        string[] allowed = kind switch
        {
            "VStack" or "HStack" => ["spacing", "padding"],
            "Text" => ["value", "id", "enabled"],
            "Button" => ["value", "id", "enabled", "click"],
            "Toggle" => ["value", "id", "enabled", "checked", "change"],
            "TextInput" => ["value", "id", "name", "enabled", "text", "change", "submit"],
            _ => throw new ParseError($"Unsupported control '{kind}'.", start)
        };
        bool stack = kind is "VStack" or "HStack";
        allowed = stack ? [.. allowed, "size"] : [.. allowed, "size", "help"];
        var arguments = new Dictionary<string, Expression>(StringComparer.Ordinal);
        Expect("(");
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
            }
            else
            {
                if (stack || arguments.Count != 0) throw new ParseError("Only the first control argument may be positional.", argStart);
                key = "value";
            }
            if (!allowed.Contains(key)) Errors.Add(new ParseError($"Unsupported property '{key}' on {kind}.", argStart));
            if (arguments.ContainsKey(key)) throw new ParseError($"Duplicate property '{key}'.", argStart);
            int expressionStart = position;
            var expression = SyntaxFactory.ParseExpression(text, position, consumeFullText: false);
            Check(expression, expressionStart);
            if (expression.IsMissing) throw new ParseError("Expected a C# expression.", Offset);
            if (key is "click" or "change" or "submit" && expression is not IdentifierNameSyntax)
                throw new ParseError("Event handlers must be method names.", expressionStart);
            arguments.Add(key, new(expression.ToString(), expressionStart + expression.SpanStart));
            position += expression.FullSpan.Length;
            if (!Is(",")) break;
            Take();
        }
        Expect(")");
        var children = new List<Node>();
        if (stack)
        {
            Expect("{");
            while (!Is("}")) children.Add(ParseNode());
            Expect("}");
        }
        else
        {
            if (!arguments.ContainsKey("value")) throw new ParseError($"{kind} requires its string argument.", start);
            Expect(";");
        }
        return new(kind, start, arguments, children);
    }
}
