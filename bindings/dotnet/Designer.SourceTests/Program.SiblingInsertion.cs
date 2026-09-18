using Xui.Designer;

internal static partial class Program
{
    private static void TestSiblingInsertion()
    {
        foreach (string newline in new[] { "\r", "\n", "\r\n" })
        foreach (string kind in new[] { "VStack", "HStack" })
        foreach (int anchor in new[] { 0, 1 })
        foreach (bool after in new[] { false, true })
        {
            var doc = Parse($"component Relative {{ view {{ {kind}() {{{newline}" +
                $"    Text(\"First\", id: \"first\");{newline}    /* keep */ Text(\"Second\", ref: Second);{newline}}} }} }}");
            var node = doc.Root!.Children[anchor];
            string source = Apply(doc, doc.InsertSibling(doc.Revision, node.Id, after, ControlTemplate.Button));
            var changed = Parse(source);
            Assert(changed.Root!.Children.Count == 3 && changed.Root.Children[anchor + (after ? 1 : 0)].Kind == "Button",
                "Before and after insert at the exact sibling index in both stack orientations.");
            Assert(source.Contains("id: \"first\"", StringComparison.Ordinal) && source.Contains("ref: Second", StringComparison.Ordinal) &&
                source.Split("/* keep */", StringSplitOptions.None).Length == 2,
                "Sibling insertion preserves existing identities and comments without duplicating them.");
        }

        var stack = Parse("""component Relative { view { VStack() { Text("Keep"); } } }""");
        Refused(stack.InsertSibling(Guid.Empty, 1, false, ControlTemplate.Text), "stale");
        Refused(stack.InsertSibling(stack.Revision, 999, false, ControlTemplate.Text), "node");
        Refused(stack.InsertSibling(stack.Revision, 0, false, ControlTemplate.Text), "root");
        Refused(stack.InsertSibling(stack.Revision, 1, true, (ControlTemplate)999), "supported");
        Refused(stack.InsertSibling(stack.Revision, 1, false, ControlTemplate.Text, new(0, 0)), "Grid parent");
        foreach (string container in new[]
        {
            """ScrollView("Fixed") { Text("Only"); }""",
            """Popup("Fixed") { Text("Only"); }""",
            """SplitView("Fixed") { Text("First"); Text("Second"); }"""
        })
        {
            var fixedParent = Parse("component Fixed { view { " + container + " } }");
            foreach (bool after in new[] { false, true })
                Refused(fixedParent.InsertSibling(fixedParent.Revision, fixedParent.Root!.Children[0].Id, after, ControlTemplate.Text), "requires exactly");
        }
        var grid = Parse("""
            component RelativeGrid { view {
                Grid("Cells", columns: [new(global::Xui.TrackSizing.Star, 1), new(global::Xui.TrackSizing.Star, 1)]) {
                    Text("Keep", column: 0);
                }
            } }
            """);
        Refused(grid.InsertSibling(grid.Revision, 1, false, ControlTemplate.Text), "GridPlacement");
        Refused(grid.InsertSibling(grid.Revision, 1, true, ControlTemplate.Text, new(0, 0)), "overlaps");
        Refused(grid.InsertSibling(grid.Revision, 1, false, ControlTemplate.Text, new(0, 2)), "outside");
        foreach (bool after in new[] { false, true })
        {
            var changed = Parse(Apply(grid, grid.InsertSibling(grid.Revision, 1, after, ControlTemplate.Button, new(0, 1))));
            var inserted = changed.Root!.Children[after ? 1 : 0];
            Assert(inserted.Kind == "Button" && inserted.Arguments.Single(argument => argument.Name == "column").Value == "1",
                "Grid sibling order and explicit visual cell placement stay independent.");
        }
        using var cancelled = new CancellationTokenSource();
        cancelled.Cancel();
        Throws<OperationCanceledException>(() => stack.InsertSibling(stack.Revision, 1, false, ControlTemplate.Text,
            cancellation: cancelled.Token), "Sibling insertion honors cancellation before compilation.");
        var invalid = Parse("""component Broken { view { VStack() { Text(MissingName); } } }""");
        Refused(invalid.InsertSibling(invalid.Revision, 1, true, ControlTemplate.Text), "compilation");
    }
}
