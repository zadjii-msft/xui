using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.CSharp.Syntax;
using Xui.Generator;

namespace Xui.Designer;

public static class DesignerLiteralCodec
{
    public static bool TryDecodeBoolean(string? expression, out bool value, out string? error)
    {
        value = false;
        error = "Boolean mode requires a true or false literal without comments or directives.";
        if (expression is null || expression.Length > XuiSourceParser.MaximumSourceLength) return false;
        var syntax = SyntaxFactory.ParseExpression(expression);
        if (syntax.ContainsDiagnostics || !HasOnlyWhitespaceTrivia(syntax) ||
            (!syntax.IsKind(SyntaxKind.TrueLiteralExpression) && !syntax.IsKind(SyntaxKind.FalseLiteralExpression))) return false;
        value = syntax.IsKind(SyntaxKind.TrueLiteralExpression);
        error = null;
        return true;
    }

    public static string EncodeBoolean(string originalExpression, bool value)
    {
        if (!TryDecodeBoolean(originalExpression, out bool originalValue, out string? error))
            throw new ArgumentException(error, nameof(originalExpression));
        if (value == originalValue) return originalExpression;
        var syntax = SyntaxFactory.ParseExpression(originalExpression);
        string encoded = SyntaxFactory.LiteralExpression(value ? SyntaxKind.TrueLiteralExpression : SyntaxKind.FalseLiteralExpression)
            .WithTriviaFrom(syntax).ToFullString();
        if (encoded.Length > XuiSourceParser.MaximumSourceLength)
            throw new ArgumentException("The encoded boolean exceeds the source length limit.", nameof(originalExpression));
        return encoded;
    }

    public static bool TryDecodeDimensions(string? expression, out string width, out string height, out string? error)
    {
        width = height = "";
        error = "Dimension mode requires an unnamed tuple of two finite, non-negative numeric literals without comments or directives.";
        if (expression is null || expression.Length > XuiSourceParser.MaximumSourceLength) return false;
        var syntax = SyntaxFactory.ParseExpression(expression);
        if (syntax.ContainsDiagnostics || syntax is not TupleExpressionSyntax tuple || tuple.Arguments.Count != 2 ||
            tuple.Arguments.Any(argument => argument.NameColon is not null || !IsDimension(argument.Expression)) ||
            !HasOnlyWhitespaceTrivia(tuple)) return false;
        width = tuple.Arguments[0].Expression.ToString();
        height = tuple.Arguments[1].Expression.ToString();
        error = null;
        return true;
    }

    public static string EncodeDimensions(string originalExpression, string width, string height)
    {
        if (!TryDecodeDimensions(originalExpression, out string originalWidth, out string originalHeight, out string? error))
            throw new ArgumentException(error, nameof(originalExpression));
        var widthSyntax = ParseDimension(width, nameof(width));
        var heightSyntax = ParseDimension(height, nameof(height));
        if (width.Trim() == originalWidth && height.Trim() == originalHeight) return originalExpression;
        var tuple = (TupleExpressionSyntax)SyntaxFactory.ParseExpression(originalExpression);
        var previousWidth = tuple.Arguments[0].Expression;
        tuple = tuple.ReplaceNode(previousWidth, widthSyntax.WithTriviaFrom(previousWidth));
        var previousHeight = tuple.Arguments[1].Expression;
        tuple = tuple.ReplaceNode(previousHeight, heightSyntax.WithTriviaFrom(previousHeight));
        string encoded = tuple.ToFullString();
        if (encoded.Length > XuiSourceParser.MaximumSourceLength)
            throw new ArgumentException("The encoded dimensions exceed the source length limit.", nameof(width));
        return encoded;
    }

    private static ExpressionSyntax ParseDimension(string text, string parameter)
    {
        if (text is not null && text.Length <= XuiSourceParser.MaximumSourceLength)
        {
            var syntax = SyntaxFactory.ParseExpression(text.Trim());
            if (!syntax.ContainsDiagnostics && HasOnlyWhitespaceTrivia(syntax) && IsDimension(syntax)) return syntax;
        }
        throw new ArgumentException("Enter one finite, non-negative numeric literal that fits a single-precision dimension.", parameter);
    }

    private static bool HasOnlyWhitespaceTrivia(SyntaxNode syntax) => syntax.DescendantTrivia(descendIntoTrivia: true)
        .All(trivia => trivia.IsKind(SyntaxKind.WhitespaceTrivia) || trivia.IsKind(SyntaxKind.EndOfLineTrivia));

    private static bool IsDimension(ExpressionSyntax expression)
    {
        bool negative = false;
        if (expression is PrefixUnaryExpressionSyntax unary &&
            (unary.IsKind(SyntaxKind.UnaryMinusExpression) || unary.IsKind(SyntaxKind.UnaryPlusExpression)))
        {
            negative = unary.IsKind(SyntaxKind.UnaryMinusExpression);
            expression = unary.Operand;
        }
        if (expression is not LiteralExpressionSyntax literal || !literal.IsKind(SyntaxKind.NumericLiteralExpression)) return false;
        double? number = literal.Token.Value switch
        {
            int value => value,
            uint value => value,
            long value => value,
            ulong value => value,
            float value => value,
            double value => value,
            decimal value => (double)value,
            _ => null
        };
        if (negative) number = -number;
        return number is { } dimension && double.IsFinite(dimension) && dimension >= 0 && dimension <= float.MaxValue;
    }

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
