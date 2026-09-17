using System.Globalization;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.CSharp.Syntax;
using Xui.Designer;
using Xui.Generator;

internal static partial class Program
{
    private static void TestLiteralCodec()
    {
        foreach (var (expression, native) in new[]
        {
            ("\"\"", ""),
            ("\"quotes: \\\" slash: \\\\ tab: \\t\"", "quotes: \" slash: \\ tab: \t"),
            ("\"first\\r\\nsecond\\nthird\\rfourth\"", "first\rsecond\rthird\rfourth"),
            ("\"\\U0001F600 \\uD83D\\uDE00\"", "😀 😀"),
            ("\"\\u00E9\\u2028\\u0085\"", "é\u2028\u0085"),
            ("@\"quotes \"\" and \\slashes\"", "quotes \" and \\slashes"),
            ("@\"one\r\ntwo\nthree\rfour\"", "one\rtwo\rthree\rfour"),
            ("\"\"\"raw \"quotes\" \\text 😀\"\"\"", "raw \"quotes\" \\text 😀"),
            ("\"\"\"\n    first\n      indented\n    \"\"\"", "first\r  indented"),
            ("\"\"\"\r\n    first\r\n      indented\r\n    \"\"\"", "first\r  indented"),
            (" \t@\"original style\"\r\n", "original style")
        })
        {
            Assert(DesignerLiteralCodec.TryDecodeText(expression, out string text, out string? error), error ?? "Decode failed.");
            Assert(error is null && text == native, "Decode returns exact value with only CR/CRLF/LF normalized for native input.");
            foreach (string newline in new[] { "\r", "\r\n", "\n" })
                Assert(DesignerLiteralCodec.EncodeText(expression, native.Replace("\r", newline, StringComparison.Ordinal)) == expression,
                    "Unchanged native text preserves exact authored literal style, escapes, trivia, and original value line endings.");
            string changed = native + "\rchanged \"\\\t😀";
            string encoded = DesignerLiteralCodec.EncodeText(expression, changed);
            var parsed = SyntaxFactory.ParseExpression(encoded);
            Assert(!parsed.ContainsDiagnostics && parsed is LiteralExpressionSyntax literal &&
                literal.Token.ValueText == changed.Replace('\r', '\n'), "Changed text is a C# literal with exact LF-normalized semantics.");
            Assert(DesignerLiteralCodec.TryDecodeText(encoded, out string roundtrip, out error) && roundtrip == changed,
                "Changed literal returns to the same native text.");
            var doc = Parse("component LiteralEdit { view { Text(" + expression + "); } }");
            var result = doc.SetArgument(doc.Revision, 0, "value", encoded);
            Apply(doc, result);
        }

        foreach (string? expression in new[]
        {
            null, "", "\"unterminated", "\"x\" + \"y\"", "Name", "(\"literal\")", "\"x\"; Text(\"injection\")",
            "$\"literal\"", "$\"{Name}\"", "$\"\"\"{Name}\"\"\"", "\"utf8\"u8", "\"\"\"utf8\"\"\"u8",
            "'x'", "true", "123", "null", "\"x\" /* preserve */", "/* preserve */ \"x\"", "\"x\" // preserve",
            "\"\\0\"", "\"\\u0000\"", "\"\\x0\"", "\"\\uD800\"", "\"\\uDC00\"", "\"\\uD800x\"",
            "\"\0\"", "\"\uD800\"", "\"\uDC00\"", new string(' ', XuiSourceParser.MaximumSourceLength + 1)
        })
        {
            Assert(!DesignerLiteralCodec.TryDecodeText(expression, out string text, out string? error) &&
                text == "" && !string.IsNullOrWhiteSpace(error), "Unsafe literal must fail explicitly without decoded text.");
            EncodingFails(expression!, "replacement", "originalExpression");
        }
        foreach (string? invalid in new[] { null, "\0", "\uD800", "\uDC00", "x\uD800y",
            new string('x', XuiSourceParser.MaximumSourceLength + 1), new string('\t', XuiSourceParser.MaximumSourceLength) })
            EncodingFails("\"old\"", invalid!, "currentNativeText");

        string maximum = "\"" + new string('x', XuiSourceParser.MaximumSourceLength - 2) + "\"";
        Assert(DesignerLiteralCodec.TryDecodeText(maximum, out string maximumText, out _), "A literal at the source limit is accepted.");
        Assert(DesignerLiteralCodec.EncodeText("\"old\"", maximumText) == maximum, "Encoded length can equal the exact source limit.");
        EncodingFails("\"old\"", maximumText + "x", "currentNativeText");
        foreach (var cultureName in new[] { "en-US", "tr-TR", "fr-FR", "ar-SA" })
        {
            var previous = CultureInfo.CurrentCulture;
            try
            {
                CultureInfo.CurrentCulture = CultureInfo.GetCultureInfo(cultureName);
                string encoded = DesignerLiteralCodec.EncodeText("\"old\"", "Iıİi 1.5 😀\r\n\"\\\t");
                Assert(encoded == Microsoft.CodeAnalysis.CSharp.SymbolDisplay.FormatLiteral("Iıİi 1.5 😀\n\"\\\t", true),
                    "Encoding uses culture-independent Roslyn literal semantics.");
            }
            finally
            {
                CultureInfo.CurrentCulture = previous;
            }
        }

        void EncodingFails(string original, string current, string parameter)
        {
            try
            {
                DesignerLiteralCodec.EncodeText(original, current);
                Assert(false, "Invalid text must not encode successfully.");
            }
            catch (ArgumentException error)
            {
                Assert(error.ParamName == parameter && !string.IsNullOrWhiteSpace(error.Message), "Encoding error identifies the offending input.");
            }
        }
    }
}
