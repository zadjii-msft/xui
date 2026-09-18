using Xui.Designer;
using Xui.Generator;

internal static partial class Program
{
    private static void TestRgbColorCodec()
    {
        foreach (var (expression, expected) in new (string, uint)[]
        {
            ("0", 0), ("16777215", 0xFFFFFF), ("0x123abcUL", 0x123ABC), (" 0b0001_0010\r\n", 18),
            ("\t00010u ", 10), ("0Xff_00_aa", 0xFF00AA)
        })
        {
            Assert(DesignerLiteralCodec.TryDecodeRgbColor(expression, out uint value, out var error) &&
                value == expected && error is null, "RGB literals decode without executing C#.");
            Assert(DesignerLiteralCodec.EncodeRgbColor(expression, value) == expression, "An unchanged color preserves exact spelling and whitespace.");
        }
        Assert(DesignerLiteralCodec.EncodeRgbColor(" \t18UL\r\n", 0x12ABCD) == " \t0x12ABCD\r\n",
            "Changed colors use six uppercase hexadecimal digits and preserve surrounding whitespace.");
        foreach (string? invalid in new[] { null, "", "0x1000000", "4294967295u", "-1", "-0", "+1", "1.0", "1f",
            "1m", "'A'", "true", "\"red\"", "(1)", "1 + 2", "theme(light: 1, dark: 2)", "resource(Accent)",
            "1 /*keep*/", "1; Run()", "1\0", new string(' ', XuiSourceParser.MaximumSourceLength + 1) })
        {
            Assert(!DesignerLiteralCodec.TryDecodeRgbColor(invalid, out uint value, out var error) &&
                value == 0 && !string.IsNullOrEmpty(error), "Unsupported color syntax has an explicit error and no partial color.");
            Reject(invalid!, 0, "originalExpression");
        }
        Reject("0", 0x1000000, "value");
        string maximum = new string(' ', XuiSourceParser.MaximumSourceLength - 1) + "0";
        Assert(DesignerLiteralCodec.EncodeRgbColor(maximum, 0) == maximum, "Maximum-length no-ops remain exact.");
        Reject(maximum, 1, "value");
        foreach (string kind in new[] { "Button", "Text", "Toggle", "TextInput" })
        foreach (string argument in new[] { "foreground", "background", "borderBrush" })
        {
            var document = Parse($"component Colors {{ view {{ {kind}(\"Keep\", {argument}: 0x112233); }} }}");
            string changed = Apply(document, document.SetArgument(document.Revision, 0, argument,
                DesignerLiteralCodec.EncodeRgbColor("0x112233", 0xABCDEF)));
            Assert(changed.Contains($"{argument}: 0xABCDEF", StringComparison.Ordinal),
                "Every supported color property compiles through the real visual source pipeline.");
        }

        void Reject(string original, uint value, string parameter)
        {
            try { DesignerLiteralCodec.EncodeRgbColor(original, value); Assert(false, "Invalid color encoding must fail."); }
            catch (ArgumentException error)
            { Assert(error.ParamName == parameter && !string.IsNullOrEmpty(error.Message), "Color encoding errors identify the rejected input."); }
        }
    }
}
