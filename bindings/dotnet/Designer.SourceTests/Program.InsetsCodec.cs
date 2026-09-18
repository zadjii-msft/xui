using System.Globalization;
using Xui.Designer;
using Xui.Generator;

internal static partial class Program
{
    private static void TestInsetsCodec()
    {
        foreach (string expression in new[] { "8", " \t0x10\r\n", "1.5m", "32768", "0", "(1, 2, 3, 4)", " ( 0x10,\r\n2f, 3d,\t4UL ) " })
        {
            Assert(DesignerLiteralCodec.TryDecodeInsets(expression, out var left, out var top, out var right, out var bottom, out var error) &&
                error is null, "Scalar and four-sided insets decode without evaluating code.");
            Assert(DesignerLiteralCodec.EncodeInsets(expression, left, top, right, bottom) == expression,
                "Unchanged inset fields preserve exact source spelling.");
            Assert(DesignerLiteralCodec.EncodeInsets(expression, " " + left, top + " ", right, bottom) == expression,
                "Field-edge whitespace does not change source.");
        }
        Assert(DesignerLiteralCodec.EncodeInsets(" \t8\r\n", "8", "4f", "8", "2") == " \t(8, 4f, 8, 2)\r\n",
            "A scalar expands to a tuple only when edge spellings differ, preserving outer whitespace.");
        Assert(DesignerLiteralCodec.EncodeInsets(" 8 ", "4f", "4f", "4f", "4f") == " 4f ",
            "Equal edge spellings retain uniform scalar syntax.");
        Assert(DesignerLiteralCodec.EncodeInsets(" ( 0x10,\r\n2f, 3d,\t4UL ) ", "0x10", "20", "3d", "40") ==
            " ( 0x10,\r\n20, 3d,\t40 ) ", "Changed tuple edges preserve separators and unchanged edge spelling.");
        Assert(DesignerLiteralCodec.EncodeInsets("(1, 2, 3, 4)", "8", "8", "8", "8") == "(8, 8, 8, 8)",
            "An authored tuple stays a tuple after a uniform edit.");
        foreach (string? expression in new[] {
            null, "", "(8)", "(1,2)", "(1,2,3)", "(1,2,3,4,5)", "(left: 1, top: 2, right: 3, bottom: 4)",
            "Width", "(1,Width,3,4)", "new Insets(8)", "8 /*keep*/", "(1, /*keep*/ 2, 3, 4)",
            "+1", "-0", "-1", "32769", "double.NaN", "1e999", "1e300", "true", "\"8\"", "8; Run()", "8\0",
            new string(' ', XuiSourceParser.MaximumSourceLength + 1)
        })
        {
            Assert(!DesignerLiteralCodec.TryDecodeInsets(expression, out var left, out var top, out var right, out var bottom, out var error) &&
                left == "" && top == "" && right == "" && bottom == "" && !string.IsNullOrWhiteSpace(error),
                "Unsupported insets return an explicit error without partial fields.");
            Reject(expression!, ["1", "2", "3", "4"], "originalExpression");
        }
        foreach (string? value in new[] { null, "", "32769", "-1", "+1", "8 /*keep*/", "8,5", "Width", "1+2", "NaN", "(8)", "8; Run()" })
        {
            string[] names = ["left", "top", "right", "bottom"];
            for (int i = 0; i < names.Length; i++)
            {
                string[] fields = ["1", "2", "3", "4"];
                fields[i] = value!;
                Reject("8", fields, names[i]);
            }
        }
        string maximum = new string(' ', XuiSourceParser.MaximumSourceLength - 1) + "8";
        Assert(DesignerLiteralCodec.EncodeInsets(maximum, "8", "8", "8", "8") == maximum, "No-op insets preserve the source length limit.");
        Reject(maximum, ["8", "4", "8", "4"], "left");
        foreach (string culture in new[] { "en-US", "fr-FR", "tr-TR", "ar-SA" })
        {
            var previous = CultureInfo.CurrentCulture;
            try
            {
                CultureInfo.CurrentCulture = CultureInfo.GetCultureInfo(culture);
                Assert(DesignerLiteralCodec.EncodeInsets("8", "1.5f", "2.5m", "3", "4") == "(1.5f, 2.5m, 3, 4)",
                    "Insets use C# numeric syntax, not the current culture.");
            }
            finally { CultureInfo.CurrentCulture = previous; }
        }
        foreach (string kind in new[] { "Button", "Text", "Toggle", "TextInput" })
        foreach (string argument in new[] { "padding", "borderThickness" })
        {
            var document = Parse($"component Edges {{ view {{ {kind}(\"Keep\", {argument}: 8, help: \"Keep help\"); }} }}");
            string encoded = DesignerLiteralCodec.EncodeInsets("8", "1.5f", "2.5m", "3", "32768");
            string changed = Apply(document, document.SetArgument(document.Revision, 0, argument, encoded));
            Assert(changed.Contains($"{argument}: (1.5f, 2.5m, 3, 32768)", StringComparison.Ordinal) &&
                changed.Contains("help: \"Keep help\"", StringComparison.Ordinal), "Inset edits pass the real style compiler and preserve unrelated arguments.");
        }

        void Reject(string original, string[] fields, string parameter)
        {
            try
            {
                DesignerLiteralCodec.EncodeInsets(original, fields[0], fields[1], fields[2], fields[3]);
                Assert(false, "Invalid insets must not encode.");
            }
            catch (ArgumentException error)
            {
                Assert(error.ParamName == parameter && !string.IsNullOrWhiteSpace(error.Message), "Inset errors identify the rejected field.");
            }
        }
    }
}
