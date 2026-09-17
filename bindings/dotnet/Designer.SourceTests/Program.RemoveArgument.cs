using Xui.Designer;

internal static partial class Program
{
    private static void TestRemoveArgument()
    {
        foreach (var (arguments, remove, expected) in new[]
        {
            ("spacing: 1", "spacing", ""),
            ("spacing: 1,", "spacing", ""),
            ("spacing: 1, padding: 2", "spacing", " padding: 2"),
            ("padding: 2, spacing: 1", "spacing", "padding: 2 "),
            ("padding: 2, spacing: 1,", "spacing", "padding: 2, "),
            ("padding: 2, spacing: 1, size: (10, 20)", "spacing", "padding: 2,  size: (10, 20)"),
            ("/*before*/ spacing: 1 /*after*/ , /*end*/", "spacing", "/*before*/  /*after*/  /*end*/"),
            ("padding: 2 /*prev*/, /*before*/ spacing: 1 /*after*/", "spacing", "padding: 2 /*prev*/ /*before*/  /*after*/"),
            ("spacing: 1 //after\n, //next\n padding: 2", "spacing", " //after\n //next\n padding: 2"),
            ("padding: 2, //before\n spacing: 1 //after\n", "spacing", "padding: 2 //before\n  //after\n"),
            ("padding: 2,\n spacing:\n 1,\n size: (10, 20)", "spacing", "padding: 2,\n \n size: (10, 20)"),
            ("spacing: Weight, padding: 2", "spacing", " padding: 2"),
            ("spacing: 1, size: (10, 20), padding: 2", "size", "spacing: 1,  padding: 2")
        })
        {
            foreach (string newline in new[] { "\n", "\r\n", "\r" })
            {
                const string prefix = "component Remove { param float Weight; view { VStack(";
                const string suffix = ") { Text(\"😀\"); } } code csharp { void Keep() { } } }";
                var doc = Parse((prefix + arguments + suffix).ReplaceLineEndings(newline));
                var result = doc.RemoveArgument(doc.Revision, 0, remove);
                string changed = Apply(doc, result);
                Assert(changed == (prefix + expected + suffix).ReplaceLineEndings(newline),
                    "Remove only the named argument and one comma; preserve exact trivia, other arguments, and source.");
                Assert(result.Edit!.Selection.Start == doc.Root!.Span.Start, "Removal keeps the edited node selected.");
                var updated = Parse(changed);
                Assert(updated.Root!.Arguments.All(a => a.Name != remove), "Removed argument is absent in resulting hierarchy.");
                Refused(updated.RemoveArgument(updated.Revision, 0, remove), "not an authored");
            }
        }

        foreach (var (source, name) in new[]
        {
            ("""component TextEdit { view { Text("required", id: "identity"); } }""", "id"),
            ("""component RefEdit { view { Text("required", ref: Label); } }""", "ref"),
            ("""component HandlerEdit { view { Button("button", click: Run); } code csharp { void Run() { } } }""", "click"),
            ("""component ExpressionEdit { state bool Enabled = true; view { Text("text", enabled: Enabled); } }""", "enabled"),
            ("""component LiteralComment { view { Text("text", help: "// not a comment /* 😀 */"); } }""", "help"),
            (""""component Raw { view { Text("text", help: """raw // /* { }"""); } }"""", "help"),
            ("""component Verbatim { view { Text("text", help: @"literal // /*"); } }""", "help"),
            ("""component Trailing { view { Text("text", help: "value",); } }""", "help")
        })
        {
            var doc = Parse(source);
            Apply(doc, doc.RemoveArgument(doc.Revision, 0, name));
        }
        foreach (string argument in new[] { "spacing /*key*/: 1", "spacing: /*before*/ 1", "spacing: (1 /*value*/ + 2)",
            "spacing: (1 //value\n + 2)" })
        {
            var doc = Parse("component Comments { view { VStack(" + argument + ") { } } }");
            Refused(doc.RemoveArgument(doc.Revision, 0, "spacing"), "comments");
        }

        var refs = Parse("""component Referenced { view { Text("x", ref: Label); } code csharp { void Read() { Label.Text = "x"; } } }""");
        Refused(refs.RemoveArgument(refs.Revision, 0, "ref"), "compile");
        var requiredStyle = Parse("""
            component Styled {
                param global::Xui.Element Child;
                style LabelStyle for Text { foreground: 0x112233; }
                view { Content(Child, style: LabelStyle, foreground: 0x223344); }
            }
            """);
        Refused(requiredStyle.RemoveArgument(requiredStyle.Revision, 0, "style"), "require");
        Apply(requiredStyle, requiredStyle.RemoveArgument(requiredStyle.Revision, 0, "foreground"));
        var grid = Parse("""
            component GridReset { view {
                Grid("grid", columns: [new(global::Xui.TrackSizing.Star, 1), new(global::Xui.TrackSizing.Star, 1)]) {
                    Text("a");
                    Text("b", column: 1);
                }
            } }
            """);
        Refused(grid.RemoveArgument(grid.Revision, 0, "columns"), "outside");
        Refused(grid.RemoveArgument(grid.Revision, 2, "column"), "overlaps");
        var safeGrid = Parse("""component SafeGrid { view { Grid("grid") { Text("x", row: 0, column: 0); } } }""");
        Apply(safeGrid, safeGrid.RemoveArgument(safeGrid.Revision, 1, "row"));

        var docErrors = Parse("""component Errors { view { Text("x", enabled: true); } }""");
        Refused(docErrors.RemoveArgument(docErrors.Revision, 0, "value"), "positional");
        Refused(docErrors.RemoveArgument(docErrors.Revision, 0, "missing"), "not an authored");
        Refused(docErrors.RemoveArgument(docErrors.Revision, 0, ""), "not an authored");
        Refused(docErrors.RemoveArgument(docErrors.Revision, 0, null!), "not an authored");
        Refused(docErrors.RemoveArgument(docErrors.Revision, int.MaxValue, "enabled"), "does not exist");
        Refused(docErrors.RemoveArgument(Guid.NewGuid(), 0, "enabled"), "stale");
        var malformed = VisualDocument.Parse("component Bad {");
        Refused(malformed.RemoveArgument(malformed.Revision, 0, "enabled"), "Repair");
        var invalidCompilation = Parse("""component Bad { state int Count = "bad"; view { Text("x", enabled: true); } }""");
        Refused(invalidCompilation.RemoveArgument(invalidCompilation.Revision, 0, "enabled"), "compilation");
        var proposed = docErrors.RemoveArgument(docErrors.Revision, 0, "enabled").Edit!;
        Throws<InvalidOperationException>(() => proposed.Apply(docErrors.Revision, docErrors.Source + " "), "Changed source refuses removal.");
        Throws<InvalidOperationException>(() => proposed.Apply(Guid.NewGuid(), docErrors.Source), "Changed revision refuses removal.");
        using var cancellation = new CancellationTokenSource();
        cancellation.Cancel();
        Throws<OperationCanceledException>(() => docErrors.RemoveArgument(docErrors.Revision, 0, "enabled", cancellation.Token),
            "Removal respects cancellation.");
        Throws<OperationCanceledException>(() => malformed.RemoveArgument(Guid.Empty, -1, "", cancellation.Token),
            "Cancellation precedes invalid-input validation.");
    }
}
