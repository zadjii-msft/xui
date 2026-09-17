using System.Runtime.InteropServices;
using Xui.Designer;
using Xui.Generator;

internal static class Program
{
    private static int assertions;
    private const string Source = """"
        namespace Example;
        component Sample {
            state int Count = 4;
            state bool Active = true;
            resources { Accent: 0x112233; }
            style AccentText for Text { foreground: resource(Accent); }
            view {
                VStack(/* stack comment */ spacing: 8) {
                    // before first
                    Text("😀", /* argument comment */ size: (10, 20), style: AccentText);
                    HStack() {
                        Button("""braces { } and // text""", click: Run);
                        Toggle("Enabled", checked: Active);
                    }
                    Text($"Count: {Count}");
                }
            }
            code csharp {
                void Run() {
                    string value = """view { Text("not a node"); }""";
                    // a brace }
                    if (value.Length > 0) { Count++; }
                }
            }
        }
        """";

    private static int Main()
    {
        try
        {
            NativeLibrary.SetDllImportResolver(typeof(Xui.Window).Assembly, (_, _, _) =>
                throw new InvalidOperationException("Source tests must never load native XUI."));
            TestSpans();
            TestEmbeddedCSharp();
            TestInvalidSource();
            TestArguments();
            TestStructure();
            TestGrid();
            TestRevisions();
            TestCancellation();
            Console.WriteLine($"Designer source assertions: {assertions} passed.");
            return 0;
        }
        catch (Exception error)
        {
            Console.Error.WriteLine(error);
            return 1;
        }
    }

    private static void TestSpans()
    {
        foreach (string newline in new[] { "\n", "\r", "\r\n" })
        {
            string source = Source.ReplaceLineEndings(newline);
            var doc = Parse(source);
            Assert(doc.Source == source, "Source must not be normalized.");
            var root = doc.Root!;
            Assert(root.Kind == "VStack" && root.Children.Count == 3, "Ordered root children.");
            Assert(root.Children[1].Children.Count == 2, "Nested hierarchy.");
            Assert(root.Id == 0 && root.Children[2].Id == 5, "Preorder IDs.");
            Check(root);
            var text = root.Children[0];
            int emoji = source.IndexOf("😀", StringComparison.Ordinal);
            Assert(doc.FindNode(emoji)?.Id == text.Id && doc.FindNode(emoji + 1)?.Id == text.Id,
                "Both UTF-16 halves of an authored surrogate pair belong to the same node.");
            Assert(doc.FindNode(source.IndexOf("stack comment", StringComparison.Ordinal))?.Id == root.Id,
                "Container trivia selects the containing node.");
            Assert(doc.FindNode(source.IndexOf("not a node", StringComparison.Ordinal)) is null, "C# is not UI.");
            Assert(doc.FindNode(-1) is null && doc.FindNode(source.Length) is null, "Outside source returns no selection.");
            Assert(text.Arguments[0].IsPositional && text.Arguments[0].ValueKind == XuiValueKind.String, "String literal.");
            Assert(text.Arguments.Single(a => a.Name == "size").ValueKind == XuiValueKind.Tuple, "Literal tuple.");
            Assert(root.Arguments[0].ValueKind == XuiValueKind.Number, "Number literal.");
            Assert(root.Children[2].Arguments[0].ValueKind == XuiValueKind.Expression, "Interpolation is an expression.");
            Assert(root.Children[1].Children[0].Arguments[0].ValueKind == XuiValueKind.String, "Raw string literal.");
            var result = doc.SetArgument(doc.Revision, root.Id, "spacing", "12");
            var edited = Apply(doc, result);
            Assert(edited == source.Remove(root.Arguments[0].ValueSpan.Start, 1).Insert(root.Arguments[0].ValueSpan.Start, "12"),
                "Property edit preserves every other authored code unit.");
            Assert(edited.Contains(newline + "    code csharp", StringComparison.Ordinal), "C# line endings preserved.");

            void Check(XuiSourceNode node)
            {
                string authored = source.Substring(node.Span.Start, node.Span.Length);
                Assert(authored.StartsWith(node.Kind + "(", StringComparison.Ordinal), "Node begins at identifier.");
                Assert(authored.EndsWith(node.Children.Count > 0 || node.BodySpan is not null ? "}" : ";",
                    StringComparison.Ordinal), "Node ends at its terminator, not trailing trivia.");
                Assert(source[node.ArgumentsSpan.Start - 1] == '(' && source[node.ArgumentsSpan.End] == ')', "Argument boundaries.");
                if (node.BodySpan is { } body)
                    Assert(source[body.Start - 1] == '{' && source[body.End] == '}', "Body boundaries.");
                foreach (var arg in node.Arguments)
                {
                    Assert(source.Substring(arg.ValueSpan.Start, arg.ValueSpan.Length) == arg.Value, "Exact value range.");
                    Assert(arg.Span.Start <= arg.ValueSpan.Start && arg.Span.End == arg.ValueSpan.End, "Argument encloses value.");
                }
                foreach (var child in node.Children)
                {
                    Assert(node.Span.Contains(child.Span.Start) && child.Span.End < node.Span.End, "Nested source spans.");
                    Check(child);
                }
            }
        }
    }

    private static void TestInvalidSource()
    {
        foreach (string source in new[]
        {
            "", " ", "component Missing {}", """component Bad { view { Text("x") } }""",
            """component Bad { view { VStack() { Text("x"); } }""",
            """component Bad { view { Text("x", size: ()); } }""",
            """component Bad { view { Text("x", value: "y"); } }""",
            """component Bad { view { Text("x", unknown: 1); } }""",
            """component Bad { view { Text("x"); } } /*""",
            """component Bad { view { Text("x"); } code csharp { void Run() { """,
            """component Bad { view { ScrollView("x") { } } }""",
            """component Bad { view { SplitView("x") { Text("one"); } } }""",
            Source + "\0", Source + "\uD800", Source + "\uDC00",
            new string(' ', XuiSourceParser.MaximumSourceLength + 1),
            "component Deep { view { " + string.Concat(Enumerable.Repeat("VStack() { ", 129)) +
                string.Concat(Enumerable.Repeat("} ", 129)) + "} }"
        })
        {
            var doc = VisualDocument.Parse(source);
            Assert(!doc.Success && doc.Root is null && doc.Diagnostics.Count > 0,
                "Malformed source must expose errors and no editable tree: " + source[..Math.Min(source.Length, 100)]);
            Refused(doc.DeleteNode(doc.Revision, 0), "Repair");
        }

        var semantic = Parse("""component Broken { state int Count = "bad"; view { VStack() { Text("x"); } } }""");
        Refused(semantic.SetArgument(semantic.Revision, 0, "spacing", "1"), "compilation");
        Refused(Parse(Source).SetArgument(Guid.NewGuid(), 0, "spacing", "1"), "stale");
    }

    private static void TestEmbeddedCSharp()
    {
        const string source = """"
            // Unicode before the root: 😀
            component Embedded {
                state string Value = $$"""literal { braces } {{1 + 2}} """;
                view {
                    VStack() {
                        Text($$"""value: {{Value}} { }""", help: @"quoted ""text""");
                        Button("Go", click: Run);
                    }
                }
                code csharp {
                    void Run() {
                        var values = new[] { "}", "{" };
                        string s = $$"""{{values[0]}} // /* { Button("no"); }""";
                        /* close } brace */
                        if (s.Length > 0) { Value = s; }
                    }
                }
            }
            """";
        foreach (string newline in new[] { "\n", "\r\n", "\r" })
        {
            var doc = Parse(source.ReplaceLineEndings(newline));
            Assert(doc.Root!.Children.Count == 2, "Raw interpolated C# does not introduce nodes.");
            Assert(doc.Root.Children[0].Arguments[0].ValueKind == XuiValueKind.Expression, "Raw interpolation is an expression.");
            Assert(doc.Root.Children[0].Arguments[1].ValueKind == XuiValueKind.String, "Verbatim string is a literal.");
            var edit = doc.SetArgument(doc.Revision, 1, "help", "\"Changed\"");
            string changed = Apply(doc, edit);
            int code = doc.Source.IndexOf("code csharp", StringComparison.Ordinal);
            Assert(changed[changed.IndexOf("code csharp", StringComparison.Ordinal)..] == doc.Source[code..], "C# bytes survive.");
        }
    }

    private static void TestArguments()
    {
        var doc = Parse(Source);
        int text = doc.Root!.Children[0].Id;
        Apply(doc, doc.SetArgument(doc.Revision, text, "value", "\"A\\\"B\""));
        Apply(doc, doc.SetArgument(doc.Revision, text, "size", "(30.5f, 40)"));
        Apply(doc, doc.SetArgument(doc.Revision, text, "enabled", "false"));
        Refused(doc.SetArgument(doc.Revision, text, "spacing", "4"), "supported");
        Refused(doc.SetArgument(doc.Revision, text, "enabled", "\"not bool\""), "compile");
        Refused(doc.SetArgument(doc.Revision, text, "enabled", "true, help: \"injected\""), "one complete");
        Refused(doc.SetArgument(doc.Revision, text, "enabled", "true // hide the rest"), "comments");
        Refused(doc.SetArgument(doc.Revision, text, "enabled", ""), "expression");
        int toggle = doc.Root.Children[1].Children[1].Id;
        Refused(doc.SetArgument(doc.Revision, toggle, "checked", "false"), "Explicitly");
        string toggled = Apply(doc, doc.SetArgument(doc.Revision, toggle, "checked", "false", replaceExpression: true));
        Assert(Parse(toggled).Root!.Children[1].Children[1].Arguments[1].ValueKind == XuiValueKind.Boolean, "Explicit expression replacement.");
        int dynamicText = doc.Root.Children[2].Id;
        Refused(doc.SetArgument(doc.Revision, dynamicText, "value", "\"overwrite\""), "Explicitly");
        Apply(doc, doc.SetArgument(doc.Revision, text, "value", "Count.ToString()"));
        Apply(doc, doc.SetArgument(doc.Revision, text, "size", "(Count, 20)"));
        Refused(Parse(Apply(doc, doc.SetArgument(doc.Revision, text, "size", "(Count, 20)")))
            .SetArgument(Guid.Empty, text, "size", "(1, 2)"), "stale");
        var tuple = Parse("""component Tuple { state int Count = 1; view { Text("x", size: (Count, 20)); } }""");
        Refused(tuple.SetArgument(tuple.Revision, 0, "size", "(1, 2)"), "Explicitly");
        foreach (string arguments in new[] { "", " /* empty */ ", "spacing: 1", "spacing: 1,", "spacing: 1 /* tail */, // keep\n" })
        {
            var stack = Parse("component Insert { view { VStack(" + arguments + ") { } } }");
            string changed = Apply(stack, stack.SetArgument(stack.Revision, 0, "padding", "2"));
            Assert(changed.Contains("padding: 2", StringComparison.Ordinal), "Insert named argument.");
            if (arguments.Contains("/*", StringComparison.Ordinal))
                Assert(changed.Contains(arguments.Contains("empty", StringComparison.Ordinal) ? "/* empty */" : "/* tail */",
                    StringComparison.Ordinal), "Preserve argument comments.");
        }
        var refDoc = Parse("""component Ref { view { Text("x"); } }""");
        Apply(refDoc, refDoc.SetArgument(refDoc.Revision, 0, "ref", "Label"));
    }

    private static void TestStructure()
    {
        var doc = Parse(Source);
        string deleted = Apply(doc, doc.DeleteNode(doc.Revision, doc.Root!.Children[1].Id));
        Assert(deleted.Contains("// before first", StringComparison.Ordinal) && deleted.Contains("void Run()", StringComparison.Ordinal),
            "Deletion preserves unrelated comments and C#.");
        string duplicated = Apply(doc, doc.DuplicateNode(doc.Revision, doc.Root.Children[0].Id));
        Assert(Parse(duplicated).Root!.Children.Count == 4, "Duplicate subtree.");
        var nested = Parse(Apply(doc, doc.DuplicateNode(doc.Revision, doc.Root.Children[1].Id)));
        Assert(nested.Root!.Children[2].Children.Count == 2, "Duplicate ordered nested subtree.");
        string moved = Apply(doc, doc.MoveNode(doc.Revision, doc.Root.Children[1].Id, -1));
        var movedDoc = Parse(moved);
        Assert(movedDoc.Root!.Children[0].Kind == "HStack", "Move earlier.");
        string restored = Apply(movedDoc, movedDoc.MoveNode(movedDoc.Revision, movedDoc.Root.Children[0].Id, 1));
        Assert(restored == Source, "Reversing reorder restores all trivia exactly.");
        Refused(doc.MoveNode(doc.Revision, doc.Root.Children[0].Id, -1), "no sibling");
        Refused(doc.MoveNode(doc.Revision, doc.Root.Children[0].Id, 2), "adjacent");
        Refused(doc.DeleteNode(doc.Revision, 0), "root");
        Refused(doc.DuplicateNode(doc.Revision, 0), "root");
        Refused(doc.MoveNode(doc.Revision, 0, 1), "root");
        foreach (var template in Enum.GetValues<ControlTemplate>())
        {
            var inserted = Parse(Apply(doc, doc.InsertControl(doc.Revision, 0, 1, template)));
            Assert(inserted.Root!.Children.Count == 4, "Every palette template compiles.");
            Apply(doc, doc.InsertControl(doc.Revision, 0, 3, template));
        }
        var empty = Parse("""component Empty { view { VStack(/*comment*/) { /*body*/ } } }""");
        Assert(Apply(empty, empty.InsertControl(empty.Revision, 0, 0, ControlTemplate.Button)).Contains("/*body*/",
            StringComparison.Ordinal), "Insertion keeps body trivia.");
        Refused(doc.InsertControl(doc.Revision, 0, 4, ControlTemplate.Text), "index");
        Refused(doc.InsertControl(doc.Revision, 0, 0, (ControlTemplate)999), "supported");
        Refused(doc.InsertControl(doc.Revision, 0, 0, ControlTemplate.Text, new(0, 0)), "Grid parent");
        foreach (string wrapper in new[] { "ScrollView", "Popup", "SplitView" })
        {
            var fixedDoc = Parse($"component Fixed {{ view {{ {wrapper}(\"wrapper\") {{ Text(\"one\"); " +
                (wrapper == "SplitView" ? "Text(\"two\");" : "") + " } } }");
            Refused(fixedDoc.DeleteNode(fixedDoc.Revision, 1), "exactly");
            Refused(fixedDoc.DuplicateNode(fixedDoc.Revision, 1), "exactly");
            Refused(fixedDoc.InsertControl(fixedDoc.Revision, 0, 0, ControlTemplate.Text), "Stack or Grid");
            if (wrapper == "SplitView") Apply(fixedDoc, fixedDoc.MoveNode(fixedDoc.Revision, 1, 1));
        }
        foreach (string identity in new[] { "ref: Label", "id: \"label\"" })
        {
            var identityDoc = Parse($"component Identity {{ view {{ VStack() {{ Text(\"x\", {identity}); }} }} }}");
            Refused(identityDoc.DuplicateNode(identityDoc.Revision, 1), "identities");
        }
        var referenced = Parse("""component Referenced { view { VStack() { Text("x", ref: Label); } } code csharp { void Read() { Label.Text = "x"; } } }""");
        Refused(referenced.DeleteNode(referenced.Revision, 1), "compile");
        var content = Parse("""component Owned { param global::Xui.Element Child; view { VStack() { Content(Child); } } }""");
        Refused(content.DuplicateNode(content.Revision, 1), "owns");
    }

    private static void TestGrid()
    {
        var doc = Parse("""
            component Cells { view {
                Grid("grid", columns: new global::Xui.GridTrack[] {
                    new(global::Xui.TrackSizing.Star, 1), new(global::Xui.TrackSizing.Star, 1)
                }) { Text("first", row: 0, column: 0); }
            } }
            """);
        Refused(doc.InsertControl(doc.Revision, 0, 1, ControlTemplate.Text), "GridPlacement");
        Refused(doc.InsertControl(doc.Revision, 0, 1, ControlTemplate.Text, new(0, 0)), "overlaps");
        Refused(doc.InsertControl(doc.Revision, 0, 1, ControlTemplate.Text, new(1, 0)), "outside");
        Refused(doc.InsertControl(doc.Revision, 0, 1, ControlTemplate.Text, new(-1, 0)), "nonnegative");
        Refused(doc.InsertControl(doc.Revision, 0, 1, ControlTemplate.Text, new(0, 0, int.MaxValue)), "outside");
        foreach (var template in Enum.GetValues<ControlTemplate>())
            Apply(doc, doc.InsertControl(doc.Revision, 0, 1, template, new(0, 1)));
        Refused(doc.SetArgument(doc.Revision, 1, "column", "2"), "outside");
        Apply(doc, doc.SetArgument(doc.Revision, 1, "column", "1"));
        Apply(doc, doc.DeleteNode(doc.Revision, 1));
        Refused(doc.DuplicateNode(doc.Revision, 1), "distinct placement");
        Apply(doc, doc.DuplicateNode(doc.Revision, 1, new(0, 1)));
        Refused(doc.DuplicateNode(doc.Revision, 1, new(0, 0)), "overlaps");
        Refused(doc.DuplicateNode(doc.Revision, 1, new(0, 1, 1, int.MaxValue)), "outside");
        var trailing = Parse("""
            component Trailing { view {
                Grid("grid", columns: [new(global::Xui.TrackSizing.Star, 1), new(global::Xui.TrackSizing.Star, 1)]) {
                    Text("first", column: 0 /* keep */,);
                }
            } }
            """);
        string duplicate = Apply(trailing, trailing.DuplicateNode(trailing.Revision, 1, new(0, 1)));
        Assert(duplicate.Split("/* keep */", StringSplitOptions.None).Length == 3, "Grid duplication preserves the copied comments.");
        var stackGrid = Parse("""
            component StackGrid { view {
                Grid("grid", columns: [new(global::Xui.TrackSizing.Star, 1), new(global::Xui.TrackSizing.Star, 1)]) {
                    VStack() { Text("child"); }
                }
            } }
            """);
        Apply(stackGrid, stackGrid.DuplicateNode(stackGrid.Revision, 1, new(0, 1)));
        var unknown = Parse("""component Unknown { param global::Xui.GridTrack[] Tracks; view { Grid("grid", rows: Tracks) { } } }""");
        Refused(unknown.InsertControl(unknown.Revision, 0, 0, ControlTemplate.Text, new(0, 0)), "unknown lengths");
        var expression = Parse("""component Expression { param int Row; view { Grid("grid") { Text("x", row: Row); } } }""");
        Refused(expression.InsertControl(expression.Revision, 0, 1, ControlTemplate.Text, new(0, 0)), "C# expressions");
        var stack = Parse("""component Placement { view { VStack() { Text("x"); } } }""");
        Refused(stack.SetArgument(stack.Revision, 1, "row", "0"), "Grid");
        Apply(stack, stack.SetArgument(stack.Revision, 1, "flex", "1"));
    }

    private static void TestRevisions()
    {
        var doc = Parse(Source);
        var result = doc.SetArgument(doc.Revision, 0, "spacing", "1");
        Assert(result.Success, result.Error ?? "Proposal failed.");
        var edit = result.Edit!;
        Assert(edit.ExpectedSource == Source && edit.Revision == doc.Revision, "Full source and revision precondition.");
        Throws<InvalidOperationException>(() => edit.Apply(Guid.NewGuid(), Source), "Wrong revision refused.");
        Throws<InvalidOperationException>(() => edit.Apply(doc.Revision, Source + " "), "Wrong source refused.");
        var current = Parse(Apply(doc, result));
        Refused(current.DeleteNode(doc.Revision, 1), "stale");
        Refused(current.DeleteNode(current.Revision, int.MaxValue), "does not exist");
        Assert(current.Revision != Parse(current.Source).Revision, "Identical text in a new snapshot still has a distinct revision.");
    }

    private static void TestCancellation()
    {
        using var source = new CancellationTokenSource();
        source.Cancel();
        Throws<OperationCanceledException>(() => VisualDocument.Parse(Source, source.Token), "Parse cancellation.");
        var doc = Parse(Source);
        Throws<OperationCanceledException>(() => doc.SetArgument(doc.Revision, 0, "spacing", "1",
            cancellation: source.Token), "Edit compilation cancellation.");
    }

    private static VisualDocument Parse(string source)
    {
        var doc = VisualDocument.Parse(source);
        Assert(doc.Success, string.Join("; ", doc.Diagnostics.Select(d => d.Message)));
        return doc;
    }
    private static string Apply(VisualDocument doc, VisualEditResult result)
    {
        Assert(result.Success, result.Error ?? "Missing edit.");
        var edit = result.Edit!;
        string changed = edit.Apply(doc.Revision, doc.Source);
        Assert(changed[..edit.Range.Start] == doc.Source[..edit.Range.Start] &&
            changed[(edit.Range.Start + edit.Replacement.Length)..] == doc.Source[edit.Range.End..],
            "Every code unit outside the edit is preserved.");
        var parsed = Parse(changed);
        var selected = parsed.FindNode(edit.Selection.Start);
        Assert(selected is not null && selected.Span == edit.Selection, "Selection addresses the resulting node.");
        Assert(VisualSourceCompilation.Validate(changed, default) is null, "Result emits with the actual generator and bindings.");
        return changed;
    }
    private static void Refused(VisualEditResult result, string message)
    {
        Assert(!result.Success && result.Edit is null && result.Error?.Contains(message, StringComparison.OrdinalIgnoreCase) == true,
            $"Expected refusal containing '{message}', got '{result.Error}'.");
    }
    private static void Throws<T>(Action action, string message) where T : Exception
    {
        try { action(); }
        catch (T) { assertions++; return; }
        throw new InvalidOperationException(message);
    }
    private static void Assert(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
        assertions++;
    }
}
