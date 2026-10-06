namespace Xui.Generator;

internal static class DiagnosticText
{
    internal static string Quote(string text)
    {
        const int maximumLength = 80;
        int length = Math.Min(text.Length, maximumLength);
        if (length < text.Length && char.IsHighSurrogate(text[length - 1]) && char.IsLowSurrogate(text[length]))
            length--;
        string value = text[..length] + (length < text.Length ? "..." : "");
        return "'" + value.Replace("\\", "\\\\", StringComparison.Ordinal)
            .Replace("'", "\\'", StringComparison.Ordinal)
            .Replace("\r", "\\r", StringComparison.Ordinal)
            .Replace("\n", "\\n", StringComparison.Ordinal)
            .Replace("\t", "\\t", StringComparison.Ordinal)
            .Replace("\u0085", "\\u0085", StringComparison.Ordinal)
            .Replace("\u2028", "\\u2028", StringComparison.Ordinal)
            .Replace("\u2029", "\\u2029", StringComparison.Ordinal)
            .Replace("\0", "\\0", StringComparison.Ordinal) + "'";
    }

    internal static string Choices(IEnumerable<string> values)
    {
        var choices = values.Distinct(StringComparer.Ordinal).Select(Quote).ToArray();
        return choices.Length == 0 ? "No values are supported here." : "Choose one of: " + string.Join(", ", choices) + ".";
    }
}
