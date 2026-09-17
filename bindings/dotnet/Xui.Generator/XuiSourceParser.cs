using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.CSharp.Syntax;

namespace Xui.Generator;

public readonly record struct SourceRange(int Start, int Length)
{
    public int End => checked(Start + Length);
    public bool Contains(int offset) => offset >= Start && offset < End;
}

public enum XuiValueKind { String, Number, Boolean, Tuple, Expression }

public sealed record XuiSourceArgument(
    string Name, bool IsPositional, SourceRange Span, SourceRange ValueSpan, string Value, XuiValueKind ValueKind);

public sealed record XuiSourceNode(
    int Id, string Kind, SourceRange Span, SourceRange ArgumentsSpan, SourceRange? BodySpan,
    IReadOnlyList<XuiSourceArgument> Arguments, IReadOnlyList<XuiSourceNode> Children,
    IReadOnlyList<string> SupportedArguments, bool HasTrailingComma);

public sealed record XuiSourceDiagnostic(string Message, SourceRange Span);

public sealed record XuiSourceDocument(
    string Source, XuiSourceNode? Root, IReadOnlyList<XuiSourceDiagnostic> Diagnostics)
{
    public bool Success => Root is not null && Diagnostics.Count == 0;
}

public static class XuiSourceParser
{
    public const int MaximumSourceLength = 65536;
    public const int MaximumDepth = 128;

    public static XuiSourceDocument Parse(string source, CancellationToken cancellation = default)
    {
        ArgumentNullException.ThrowIfNull(source);
        cancellation.ThrowIfCancellationRequested();
        if (source.Length > MaximumSourceLength)
            return Failure($"Source must not exceed {MaximumSourceLength} UTF-16 code units.", 0);
        for (int i = 0; i < source.Length; i++)
        {
            cancellation.ThrowIfCancellationRequested();
            if (source[i] == '\0') return Failure("Remove the embedded NUL character.", i);
            if (char.IsHighSurrogate(source[i]))
            {
                if (i + 1 == source.Length || !char.IsLowSurrogate(source[i + 1]))
                    return Failure("Replace the unpaired Unicode surrogate with valid Unicode text.", i);
                i++;
            }
            else if (char.IsLowSurrogate(source[i]))
                return Failure("Replace the unpaired Unicode surrogate with valid Unicode text.", i);
        }

        var errors = new List<XuiSourceDiagnostic>();
        try
        {
            var parser = new Parser(source, cancellation, MaximumDepth);
            var component = parser.Parse();
            errors.AddRange(parser.Errors.Select(error => Diagnostic(error.Message, error.Offset)));
            // Token diagnostics include unfinished comments in otherwise valid trailing trivia.
            foreach (var token in SyntaxFactory.ParseTokens(source))
            {
                cancellation.ThrowIfCancellationRequested();
                foreach (var error in token.GetDiagnostics().Where(d => d.Severity == DiagnosticSeverity.Error))
                    errors.Add(Diagnostic(error.GetMessage(), token.SpanStart));
                foreach (var trivia in token.LeadingTrivia.Concat(token.TrailingTrivia))
                    foreach (var error in trivia.GetDiagnostics().Where(d => d.Severity == DiagnosticSeverity.Error))
                        errors.Add(Diagnostic(error.GetMessage(), trivia.SpanStart));
            }
            int nextId = 0;
            return new(source, errors.Count == 0 ? Convert(component.Root) : null, errors.AsReadOnly());

            XuiSourceNode Convert(Node node)
            {
                cancellation.ThrowIfCancellationRequested();
                int id = nextId++;
                return new(id, node.Kind, node.Span, node.ArgumentsSpan, node.BodySpan,
                    node.AuthoredArguments, Array.AsReadOnly(node.Children.Select(Convert).ToArray()),
                    node.SupportedArguments, node.HasTrailingComma);
            }
        }
        catch (ParseError error)
        {
            errors.Add(Diagnostic(error.Message, error.Offset));
            return new(source, null, errors.AsReadOnly());
        }

        XuiSourceDiagnostic Diagnostic(string message, int offset)
        {
            offset = Math.Clamp(offset, 0, source.Length);
            return new(message, new(offset, offset < source.Length ? 1 : 0));
        }
        XuiSourceDocument Failure(string message, int offset) =>
            new(source, null, Array.AsReadOnly(new[] { Diagnostic(message, offset) }));
    }

    internal static XuiValueKind Classify(ExpressionSyntax expression) => expression switch
    {
        LiteralExpressionSyntax literal when literal.IsKind(SyntaxKind.StringLiteralExpression) => XuiValueKind.String,
        LiteralExpressionSyntax literal when literal.IsKind(SyntaxKind.NumericLiteralExpression) => XuiValueKind.Number,
        LiteralExpressionSyntax literal when literal.IsKind(SyntaxKind.TrueLiteralExpression) ||
            literal.IsKind(SyntaxKind.FalseLiteralExpression) => XuiValueKind.Boolean,
        PrefixUnaryExpressionSyntax unary when
            (unary.IsKind(SyntaxKind.UnaryMinusExpression) || unary.IsKind(SyntaxKind.UnaryPlusExpression)) &&
            Classify(unary.Operand) == XuiValueKind.Number => XuiValueKind.Number,
        TupleExpressionSyntax tuple when tuple.Arguments.All(a => a.NameColon is null &&
            Classify(a.Expression) != XuiValueKind.Expression) => XuiValueKind.Tuple,
        _ => XuiValueKind.Expression
    };
}
