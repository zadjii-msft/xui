using Xui.Designer;
using Xui.Generator;

internal static partial class Program
{
    private static void TestBooleanCodec()
    {
        foreach (var (expression, value) in new[] { ("true", true), ("false", false), (" \ttrue\r\n", true), ("\n false \t", false) })
        {
            Assert(DesignerLiteralCodec.TryDecodeBoolean(expression, out bool decoded, out var error) && decoded == value && error is null,
                "Boolean literals decode without evaluating source.");
            Assert(DesignerLiteralCodec.EncodeBoolean(expression, value) == expression, "No-op booleans preserve exact source.");
            Assert(DesignerLiteralCodec.EncodeBoolean(expression, !value) == expression.Replace(value ? "true" : "false", value ? "false" : "true"),
                "Changed booleans preserve surrounding whitespace.");
        }
        foreach (string? expression in new[]
        {
            null, "", "True", "False", "1", "0", "\"true\"", "null", "default", "(true)", "!false", "true || false",
            "Flag", "true; Run()", "/* keep */ true", "false /* keep */", "true // keep", "true\0", "true\uD800",
            "#if DEBUG\ntrue\n#endif", new string(' ', XuiSourceParser.MaximumSourceLength + 1)
        })
        {
            Assert(!DesignerLiteralCodec.TryDecodeBoolean(expression, out bool value, out var error) && !value && !string.IsNullOrWhiteSpace(error),
                "Invalid boolean input returns an explicit error.");
            Reject(expression!);
        }
        string maximum = new string(' ', XuiSourceParser.MaximumSourceLength - 4) + "true";
        Assert(DesignerLiteralCodec.EncodeBoolean(maximum, true) == maximum, "A maximum-length no-op boolean remains unchanged.");
        Reject(maximum);
        foreach (var (control, property) in new[] { ("Button", "enabled"), ("Button", "visible"), ("Toggle", "checked") })
        {
            var document = Parse($"component Flags {{ view {{ {control}(\"Keep\", {property}: true, help: \"Keep help\"); }} }}");
            string encoded = DesignerLiteralCodec.EncodeBoolean("true", false);
            string result = Apply(document, document.SetArgument(document.Revision, document.Root!.Id, property, encoded));
            Assert(result.Contains($"{property}: false, help: \"Keep help\"", StringComparison.Ordinal),
                "Boolean edits compile and preserve unrelated arguments.");
        }

        void Reject(string expression)
        {
            try
            {
                DesignerLiteralCodec.EncodeBoolean(expression, false);
                Assert(false, "An invalid or oversized boolean cannot encode successfully.");
            }
            catch (ArgumentException error)
            {
                Assert(error.ParamName == "originalExpression" && !string.IsNullOrWhiteSpace(error.Message),
                    "Boolean encoding errors identify the rejected source expression.");
            }
        }
    }
}
