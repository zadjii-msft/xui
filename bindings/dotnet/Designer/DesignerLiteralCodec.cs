using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.CSharp.Syntax;
using Xui.Generator;

namespace Xui.Designer;

public static class DesignerLiteralCodec
{
    public static bool TryDecodeText(string? expression, out string nativeText, out string? error)
    {
        nativeText = "";
        error = ValidateText(expression);
        if (error is not null) return false;
        var syntax = SyntaxFactory.ParseExpression(expression!);
        if (syntax.ContainsDiagnostics || syntax is not LiteralExpressionSyntax literal ||
            !literal.IsKind(SyntaxKind.StringLiteralExpression))
        {
            error = "Text mode requires one ordinary, verbatim, or raw string literal. Edit expressions, interpolation, and UTF-8 literals in source mode.";
            return false;
        }
        if (syntax.GetLeadingTrivia().Concat(syntax.GetTrailingTrivia())
            .Any(trivia => !trivia.IsKind(SyntaxKind.WhitespaceTrivia) && !trivia.IsKind(SyntaxKind.EndOfLineTrivia)))
        {
            error = "The literal has surrounding comments or directives. Preserve them in source mode before using text mode.";
            return false;
        }
        string value = literal.Token.ValueText;
        error = ValidateText(value);
        if (error is not null) return false;
        nativeText = NativeNewlines(value);
        return true;
    }

    public static string EncodeText(string originalExpression, string currentNativeText)
    {
        if (!TryDecodeText(originalExpression, out string originalText, out string? error))
            throw new ArgumentException(error, nameof(originalExpression));
        if (ValidateText(currentNativeText) is { } currentError)
            throw new ArgumentException(currentError, nameof(currentNativeText));
        if (string.Equals(originalText, NativeNewlines(currentNativeText), StringComparison.Ordinal))
            return originalExpression;
        string expression = SymbolDisplay.FormatLiteral(ValueNewlines(currentNativeText), quote: true);
        if (expression.Length > XuiSourceParser.MaximumSourceLength)
            throw new ArgumentException(
                $"The encoded literal exceeds {XuiSourceParser.MaximumSourceLength} UTF-16 code units. Shorten the text before applying it.",
                nameof(currentNativeText));
        return expression;
    }

    private static string ValueNewlines(string text) =>
        text.Replace("\r\n", "\n", StringComparison.Ordinal).Replace('\r', '\n');

    private static string NativeNewlines(string text) => ValueNewlines(text).Replace('\n', '\r');

    private static string? ValidateText(string? text)
    {
        if (text is null) return "Supply text before using text mode.";
        if (text.Length > XuiSourceParser.MaximumSourceLength)
            return $"Text must not exceed {XuiSourceParser.MaximumSourceLength} UTF-16 code units. Shorten it before using text mode.";
        for (int i = 0; i < text.Length; i++)
        {
            if (text[i] == '\0') return "Native text mode cannot represent NUL. Remove it or edit the literal in source mode.";
            if (char.IsHighSurrogate(text[i]))
            {
                if (i + 1 == text.Length || !char.IsLowSurrogate(text[i + 1]))
                    return "Replace the unpaired Unicode surrogate with valid Unicode text before using text mode.";
                i++;
            }
            else if (char.IsLowSurrogate(text[i]))
                return "Replace the unpaired Unicode surrogate with valid Unicode text before using text mode.";
        }
        return null;
    }
}
