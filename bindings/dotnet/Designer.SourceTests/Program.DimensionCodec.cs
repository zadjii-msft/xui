using System.Globalization;
using Xui.Designer;
using Xui.Generator;

internal static partial class Program
{
    private static void TestDimensionCodec()
    {
        foreach (var (expression, width, height) in new[]
        {
            ("(120, 40)", "120", "40"),
            (" \t( 0x10,\r\n +20 )\r\n", "0x10", "+20"),
            ("(1.25f, 2e2F)", "1.25f", "2e2F"),
            ("(1_000, 0b1010)", "1_000", "0b1010"),
            ("(0, -0.0f)", "0", "-0.0f"),
            ("(3.4028235e38f, 0)", "3.4028235e38f", "0"),
            ("(20UL, 40L)", "20UL", "40L"),
            ("(1.5m, 40d)", "1.5m", "40d")
        })
        {
            Assert(DesignerLiteralCodec.TryDecodeDimensions(expression, out string actualWidth, out string actualHeight, out var error),
                error ?? "Dimensions decode.");
            Assert(actualWidth == width && actualHeight == height, "Decode preserves numeric spelling, signs, suffixes, and separators.");
            Assert(DesignerLiteralCodec.EncodeDimensions(expression, width, height) == expression,
                "Unchanged dimensions preserve the exact original tuple and surrounding whitespace.");
            Assert(DesignerLiteralCodec.EncodeDimensions(expression, " " + width + " ", "\t" + height + "\t") == expression,
                "Field-edge whitespace does not create a source edit.");
        }
        Assert(DesignerLiteralCodec.EncodeDimensions(" \t( 0x10,\r\n +20 )\r\n", "240f", "30.5f") ==
            " \t( 240f,\r\n 30.5f )\r\n", "Changed values preserve tuple separators and all surrounding whitespace.");
        Assert(DesignerLiteralCodec.EncodeDimensions("(0x10, 40)", "0x10", "80") == "(0x10, 80)",
            "Changing one dimension preserves the spelling of the other dimension.");

        foreach (string? expression in new[]
        {
            null, "", "120", "(120)", "(120,)", "(1,2,3)", "new Size(1,2)", "(Width, 20)", "(1 + 2, 20)",
            "(width: 120, height: 40)", "(120, -1)", "(double.NaN, 20)", "(1e300, 20)", "(3.5e38, 20)",
            "(1e999, 20)", "(\"120\", 40)", "(true, false)", "(120, 40);", "(120, 40) // note",
            "/*note*/ (120,40)", "(/*note*/ 120,40)", "(120 /*note*/, 40)", "(120, /*note*/ 40)",
            "(120,40)\0", "(\uD800,40)", new string(' ', XuiSourceParser.MaximumSourceLength + 1)
        })
        {
            Assert(!DesignerLiteralCodec.TryDecodeDimensions(expression, out string width, out string height, out string? error) &&
                width == "" && height == "" && !string.IsNullOrWhiteSpace(error),
                "Unsafe dimensions fail explicitly without partial field values.");
            Reject(expression!, "120", "40", "originalExpression");
        }
        foreach (string? value in new[]
        {
            null, "", "-1", "NaN", "float.PositiveInfinity", "1e300", "3.5e38", "1e999", "Width", "1 + 2",
            "(120)", "true", "120; Delete()", "120 /* note */", "120 // note", "12,5", "\0", "\uD800",
            new string(' ', XuiSourceParser.MaximumSourceLength + 1)
        })
        {
            Reject("(120, 40)", value!, "40", "width");
            Reject("(120, 40)", "120", value!, "height");
        }
        string maximum = new string(' ', XuiSourceParser.MaximumSourceLength - "(1, 2)".Length) + "(1, 2)";
        Assert(DesignerLiteralCodec.EncodeDimensions(maximum, "1", "2") == maximum, "The exact source limit remains valid for a no-op.");
        Reject(maximum, "10", "2", "width");

        foreach (string culture in new[] { "en-US", "fr-FR", "tr-TR", "ar-SA" })
        {
            var previous = CultureInfo.CurrentCulture;
            try
            {
                CultureInfo.CurrentCulture = CultureInfo.GetCultureInfo(culture);
                Assert(DesignerLiteralCodec.EncodeDimensions("(120, 40)", "1.5f", "2.5f") == "(1.5f, 2.5f)",
                    "Dimensions use C# numeric syntax independently of the current culture.");
            }
            finally { CultureInfo.CurrentCulture = previous; }
        }
        foreach (string argument in new[] { "size", "preferredSize" })
        {
            var doc = Parse($"component Dimensions {{ view {{ Button(\"Keep\", {argument}: ( 120,\n40 ), help: \"Keep help\"); }} }}");
            string value = doc.Root!.Arguments.Single(a => a.Name == argument).Value;
            string encoded = DesignerLiteralCodec.EncodeDimensions(value, "240f", "60f");
            string result = Apply(doc, doc.SetArgument(doc.Revision, doc.Root.Id, argument, encoded));
            Assert(result.Contains("( 240f,\n60f )", StringComparison.Ordinal) && result.Contains("help: \"Keep help\"", StringComparison.Ordinal),
                "Dimension edits compile and preserve unrelated arguments and tuple formatting.");
        }

        void Reject(string original, string width, string height, string parameter)
        {
            try
            {
                DesignerLiteralCodec.EncodeDimensions(original, width, height);
                Assert(false, "Invalid dimensions cannot encode successfully.");
            }
            catch (ArgumentException error)
            {
                Assert(error.ParamName == parameter && !string.IsNullOrWhiteSpace(error.Message),
                    "Dimension errors identify the rejected input.");
            }
        }
    }
}
