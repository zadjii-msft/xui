using Xui.Designer;
using Xui.Generator;

internal static partial class Program
{
    private static void TestWrapping()
    {
        const string literalSource = """"
            component Literals {
                view {
                    HStack() {
                        VStack() {
                            Text("""
                                raw 😀
                                  { // text }
                                """, ref: Label);
                            Text(@"verbatim
            keep this indentation");
                        }
                    }
                }
                code csharp { void Read() { Label.Text = """unchanged { }"""; } }
            }
            """";
        foreach (string newline in new[] { "\r", "\r\n", "\n" })
        {
            var doc = Parse(literalSource.ReplaceLineEndings(newline));
            var selected = doc.Root!.Children[0];
            string subtree = doc.Source.Substring(selected.Span.Start, selected.Span.Length);
            foreach (var wrapper in new[] { ControlTemplate.VStack, ControlTemplate.HStack, ControlTemplate.ScrollView })
            {
                var result = doc.WrapNode(doc.Revision, selected.Id, wrapper);
                string source = Apply(doc, result);
                var wrapped = Parse(source);
                var wrappedNode = wrapped.FindNode(result.Edit!.Selection.Start)!;
                var child = wrappedNode.Children.Single();
                Assert(wrappedNode.Kind == wrapper.ToString(), "Wrapping selects the new wrapper.");
                Assert(source.Substring(child.Span.Start, child.Span.Length) == subtree,
                    "Wrapping retains the exact subtree, including raw and verbatim string indentation.");
                Assert(result.Edit.Replacement.Replace(newline, "", StringComparison.Ordinal).IndexOfAny(['\r', '\n']) == -1,
                    "Generated separators retain the native document newline shape.");
                var undoWrapper = wrapped.UnwrapNode(wrapped.Revision, wrappedNode.Id);
                string unwrapped = Apply(wrapped, undoWrapper);
                Assert(unwrapped.Substring(undoWrapper.Edit!.Selection.Start, undoWrapper.Edit.Selection.Length) == subtree,
                    "Unwrapping selects and preserves the original subtree.");
                Assert(unwrapped[unwrapped.IndexOf("code csharp", StringComparison.Ordinal)..] ==
                    doc.Source[doc.Source.IndexOf("code csharp", StringComparison.Ordinal)..], "C# code remains exact.");
            }
        }

        foreach (var wrapper in new[] { ControlTemplate.VStack, ControlTemplate.HStack, ControlTemplate.ScrollView })
        {
            var root = Parse("""component RootSample { view { Text("root"); } }""");
            var result = root.WrapNode(root.Revision, 0, wrapper);
            Assert(!result.Edit!.Replacement.Contains('\n'), "A single-line native document gains CR separators, not CRLF.");
            var wrapped = Parse(Apply(root, result));
            Assert(wrapped.Root!.Kind == wrapper.ToString(), "Any root can be wrapped.");
            var unwrapped = Parse(Apply(wrapped, wrapped.UnwrapNode(wrapped.Revision, 0)));
            Assert(unwrapped.Root!.Kind == "Text", "A one-child root can be unwrapped.");
        }

        var owned = Parse("""component Owned { param global::Xui.Element Child; view { Content(Child, ref: Element); } }""");
        var ownedWrapped = Parse(Apply(owned, owned.WrapNode(owned.Revision, 0, ControlTemplate.ScrollView)));
        Apply(ownedWrapped, ownedWrapped.UnwrapNode(ownedWrapped.Revision, 0));
        Assert(ownedWrapped.Root!.Children.Single().Arguments.Single(a => a.Name == "value").Value == "Child",
            "Wrap retains Content ownership instead of duplicating the external element.");

        foreach (string kind in new[] { "ScrollView", "Popup", "SplitView" })
        {
            var doc = Parse($"component Arity {{ view {{ {kind}(\"container\") {{ Text(\"first\"); " +
                (kind == "SplitView" ? "Text(\"second\");" : "") + " } } }");
            var wrapped = Parse(Apply(doc, doc.WrapNode(doc.Revision, 1, ControlTemplate.HStack)));
            Assert(wrapped.Root!.Children.Count == doc.Root!.Children.Count, "Wrapping keeps parent arity.");
            Apply(wrapped, wrapped.UnwrapNode(wrapped.Revision, 1));
            if (kind == "SplitView") Refused(doc.UnwrapNode(doc.Revision, 0), "exactly one");
            else Apply(doc, doc.UnwrapNode(doc.Revision, 0));
        }

        TestPlacementMigration();
        TestUnwrapRefusals();
        var current = Parse(literalSource);
        Refused(current.WrapNode(Guid.NewGuid(), 1, ControlTemplate.VStack), "stale");
        Refused(current.UnwrapNode(Guid.NewGuid(), 1), "stale");
        Refused(current.WrapNode(current.Revision, int.MaxValue, ControlTemplate.VStack), "does not exist");
        Refused(current.UnwrapNode(current.Revision, int.MaxValue), "does not exist");
        Refused(current.WrapNode(current.Revision, 1, ControlTemplate.Grid), "VStack");
        Refused(current.WrapNode(current.Revision, 1, (ControlTemplate)999), "VStack");
        var malformed = VisualDocument.Parse("component Broken {");
        Refused(malformed.WrapNode(malformed.Revision, 0, ControlTemplate.VStack), "Repair");
        Refused(malformed.UnwrapNode(malformed.Revision, 0), "Repair");
        var compilerError = Parse("""component Broken { state int Bad = "bad"; view { VStack() { Text("x"); } } }""");
        Refused(compilerError.WrapNode(compilerError.Revision, 0, ControlTemplate.VStack), "compilation");
        Refused(compilerError.UnwrapNode(compilerError.Revision, 0), "compilation");
        var rootReference = Parse("""component References { view { Text("x"); } code csharp { void Change() { Root.Text = "x"; } } }""");
        Refused(rootReference.WrapNode(rootReference.Revision, 0, ControlTemplate.VStack), "compile");
        using var cancellation = new CancellationTokenSource();
        cancellation.Cancel();
        Throws<OperationCanceledException>(() => current.WrapNode(current.Revision, 1, ControlTemplate.VStack, cancellation.Token),
            "Wrapping respects cancellation.");
        Throws<OperationCanceledException>(() => current.UnwrapNode(current.Revision, 0, cancellation.Token),
            "Unwrapping respects cancellation.");
    }

    private static void TestPlacementMigration()
    {
        foreach (string arguments in new[]
        {
            "flex: Weight", "flex: Weight,", "flex: Weight, padding: 2", "padding: 2, flex: Weight",
            "padding: 2, flex: /* exact expression */ Weight, spacing: 1",
            "padding: 2, flex: Weight /* keep at child */, spacing: 1,",
            "flex: Weight, /* preserved separator */ padding: 2,"
        })
        {
            var doc = Parse("component Placement { param float Weight; view { VStack() { VStack(" + arguments +
                ") { Text(\"child\"); } } } }");
            var original = doc.Root!.Children[0];
            string authored = original.Arguments.Single(a => a.Name == "flex").Value;
            foreach (var kind in new[] { ControlTemplate.HStack, ControlTemplate.ScrollView })
            {
                var result = doc.WrapNode(doc.Revision, original.Id, kind);
                var wrapped = Parse(Apply(doc, result));
                var wrapper = wrapped.FindNode(result.Edit!.Selection.Start)!;
                Assert(wrapper.Arguments.Single(a => a.Name == "flex").Value == authored, "Parent flex expression migrates without replacement.");
                Assert(wrapper.Children[0].Arguments.All(a => a.Name != "flex"), "Child loses only its transferred placement.");
                foreach (var arg in original.Arguments.Where(a => a.Name != "flex"))
                    Assert(wrapper.Children[0].Arguments.Single(a => a.Name == arg.Name).Value == arg.Value, "Other values remain exact.");
                if (arguments.Contains("/*", StringComparison.Ordinal))
                    Assert(result.Edit.Replacement.Contains(arguments.Contains("exact expression", StringComparison.Ordinal)
                        ? "/* exact expression */" : arguments.Contains("keep at child", StringComparison.Ordinal)
                            ? "/* keep at child */" : "/* preserved separator */", StringComparison.Ordinal), "Placement comments survive.");
                var unwrapped = Parse(Apply(wrapped, wrapped.UnwrapNode(wrapped.Revision, wrapper.Id)));
                Assert(unwrapped.Root!.Children[0].Arguments.Single(a => a.Name == "flex").Value == authored, "Unwrap restores parent flex.");
            }
        }

        const string grid = """
            component GridPlacement {
                param int Row;
                param int Column;
                param global::Xui.GridTrack[] Tracks;
                view {
                    Grid("grid", rows: Tracks, columns: Tracks) {
                        Text("child", row: /* keep expression */ Row, column: Column, rowSpan: 1, columnSpan: 1,);
                    }
                }
            }
            """;
        var gridDoc = Parse(grid);
        foreach (var kind in new[] { ControlTemplate.VStack, ControlTemplate.HStack, ControlTemplate.ScrollView })
        {
            var edit = gridDoc.WrapNode(gridDoc.Revision, 1, kind);
            var wrapped = Parse(Apply(gridDoc, edit));
            var wrapper = wrapped.Root!.Children[0];
            foreach (var name in new[] { "row", "column", "rowSpan", "columnSpan" })
            {
                var old = gridDoc.Root!.Children[0].Arguments.Single(a => a.Name == name);
                var current = wrapper.Arguments.Single(a => a.Name == name);
                Assert(current.Value == old.Value && wrapped.Source.Substring(current.Span.Start, current.Span.Length) ==
                    grid.Substring(old.Span.Start, old.Span.Length), "Grid placement retains its exact authored expression and internal trivia.");
            }
            Assert(wrapper.Children[0].Arguments.Count == 1, "Moved placement does not remain on the child.");
            var unwrapped = Parse(Apply(wrapped, wrapped.UnwrapNode(wrapped.Revision, wrapper.Id)));
            Assert(unwrapped.Root!.Children[0].Arguments.Single(a => a.Name == "row").Value == "Row", "Grid unwrap retains expression.");
        }

        var noPlacement = Parse("""component Defaults { view { Grid("grid") { VStack() { Text("child"); } } } }""");
        Apply(noPlacement, noPlacement.WrapNode(noPlacement.Revision, 1, ControlTemplate.ScrollView));
        Apply(noPlacement, noPlacement.UnwrapNode(noPlacement.Revision, 1));
        var stackChild = Parse("""component StackChild { view { Grid("grid") { VStack(row: 0, column: 0) { VStack(/* keep */) { } } } } }""");
        var unwrappedStack = Parse(Apply(stackChild, stackChild.UnwrapNode(stackChild.Revision, 1)));
        Assert(unwrappedStack.Root!.Children[0].Arguments.Count == 2, "Placement inserts into an empty child argument list.");
        var simpleGrid = Parse("""component SimpleGrid { view { Grid("grid") { Text("cell"); } } }""");
        Assert(Parse(Apply(simpleGrid, simpleGrid.UnwrapNode(simpleGrid.Revision, 0))).Root!.Kind == "Text", "Unwrap a default Grid root.");
    }

    private static void TestUnwrapRefusals()
    {
        foreach (var (source, reason) in new[]
        {
            ("""component Empty { view { VStack() { } } }""", "exactly one"),
            ("""component Leaf { view { Text("leaf"); } }""", "exactly one"),
            ("""component Multiple { view { VStack() { Text("a"); Text("b"); } } }""", "exactly one"),
            ("""component Identity { view { VStack(ref: Container) { Text("child"); } } }""", "ref"),
            ("""component Identity { view { ScrollView("scroll", id: "scroll") { Text("child"); } } }""", "id"),
            ("""component Property { view { VStack(padding: 8) { Text("child"); } } }""", "padding"),
            ("""component Property { state float Size = 8; view { VStack(spacing: Size) { Text("child"); } } }""", "spacing"),
            ("""component NamedWrapper { param string Name; view { ScrollView(Name) { Text("child"); } } }""", "value"),
            ("""component Comment { view { VStack(/* preserve */) { Text("child"); } } }""", "comments"),
            ("""component Flex { view { VStack() { Text("child", flex: 1); } } }""", "own placement"),
            ("""component Grid { view { Grid("grid") { Text("child", row: 0); } } }""", "own placement")
        })
        {
            var doc = Parse(source);
            Refused(doc.UnwrapNode(doc.Revision, 0), reason);
        }
        var comments = Parse("""
            component Comments { view { VStack() {
                // before child 😀
                Text("child");
                /* after child */
            } } }
            """);
        string body = comments.Source.Substring(comments.Root!.BodySpan!.Value.Start, comments.Root.BodySpan.Value.Length);
        var result = comments.UnwrapNode(comments.Revision, 0);
        Apply(comments, result);
        Assert(result.Edit!.Replacement == body, "Unwrap preserves the exact body, including comments before and after the child.");
    }
}
