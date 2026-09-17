using Xui.Designer;

internal static partial class Program
{
    private static void TestValueControlSource()
    {
        const string source = """
            component ValueSource {
                param global::Xui.NumericRange Bounds;
                state double Current = 25;
                view { Grid("values", columns: [
                    new(global::Xui.TrackSizing.Star, 1), new(global::Xui.TrackSizing.Star, 1)
                ]) {
                    /* 😀 */ RangeInput("Volume", range: Bounds, currentValue: Current, change: Changed);
                    Progress("Completed", column: 1, range: Bounds, currentValue: Current);
                } }
                code csharp { void Changed(double value) { Current = value; } }
            }
            """;
        foreach (string newline in new[] { "\n", "\r", "\r\n" })
        {
            var doc = Parse(source.ReplaceLineEndings(newline));
            foreach (var node in doc.Root!.Children)
            {
                Assert(doc.FindNode(node.Arguments[0].ValueSpan.Start)?.Id == node.Id, "Value controls participate in caret hierarchy.");
                foreach (var arg in node.Arguments)
                    Assert(doc.Source.Substring(arg.ValueSpan.Start, arg.ValueSpan.Length) == arg.Value, "Value-control argument spans remain exact.");
                Apply(doc, doc.SetArgument(doc.Revision, node.Id, "currentValue", "30", replaceExpression: true));
                Apply(doc, doc.SetArgument(doc.Revision, node.Id, "range", "new global::Xui.NumericRange(0, 200)", replaceExpression: true));
                Apply(doc, doc.RemoveArgument(doc.Revision, node.Id, "currentValue"));
                Refused(doc.SetArgument(doc.Revision, node.Id, "range", "new global::Xui.NumericRange(0, Current)",
                    replaceExpression: true), "cannot depend");
                Refused(doc.SetArgument(doc.Revision, node.Id, "currentValue", "\"bad\"", replaceExpression: true), "compile");
            }
            Apply(doc, doc.SetArgument(doc.Revision, 1, "orientation", "global::Xui.Axis.Vertical"));
            Apply(doc, doc.SetArgument(doc.Revision, 1, "reversed", "true"));
            Apply(doc, doc.SetArgument(doc.Revision, 2, "progressState", "global::Xui.ProgressState.Paused"));
            Refused(doc.SetArgument(doc.Revision, 2, "change", "Changed"), "not a supported");
        }
    }
}
