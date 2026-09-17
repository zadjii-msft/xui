using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.CSharp.Syntax;
using Xui.Designer;

internal static partial class Program
{
    private static void TestPaletteExpansion()
    {
        var original = new[]
        {
            ControlTemplate.Text, ControlTemplate.Button, ControlTemplate.Toggle, ControlTemplate.TextInput,
            ControlTemplate.VStack, ControlTemplate.HStack, ControlTemplate.Grid, ControlTemplate.ScrollView,
            ControlTemplate.SplitView
        };
        for (int i = 0; i < original.Length; i++)
            Assert((int)original[i] == i, "Existing palette numeric values remain stable.");
        Assert((int)ControlTemplate.DataGrid == 9 && (int)ControlTemplate.NavigationView == 10,
            "New supported controls append after the existing palette.");
        foreach (string source in new[]
        {
            """component Palette { view { VStack() { } } }""",
            """component Palette { view { HStack() { } } }""",
            """
                component Palette { view {
                    Grid("grid", columns: [
                        new(global::Xui.TrackSizing.Star, 1),
                        new(global::Xui.TrackSizing.Star, 1)
                    ]) { }
                } }
                """
        })
        {
            foreach (string newline in new[] { "\r", "\r\n", "\n" })
            {
                foreach (var template in new[] { ControlTemplate.DataGrid, ControlTemplate.NavigationView })
                {
                    var doc = Parse(source.ReplaceLineEndings(newline));
                    bool grid = doc.Root!.Kind == "Grid";
                    var result = doc.InsertControl(doc.Revision, 0, 0, template, grid ? new(0, 0) : null);
                    var first = Parse(Apply(doc, result));
                    var node = first.Root!.Children.Single();
                    Assert(node.Kind == template.ToString() && node.Children.Count == 0,
                        "Palette template uses a real supported leaf constructor.");
                    Assert(node.Arguments[0].IsPositional && node.Arguments[0].Value.StartsWith('"'),
                        "Each added control has its required standalone accessible name.");
                    Assert(node.Arguments.All(a => a.Name is not ("ref" or "id" or "searchId" or "style" or "click" or "change" or "submit")),
                        "Placeholders require no external identities, style resources, or handlers.");
                    Assert(node.Arguments.Single(a => a.Name == "preferredSize").Value ==
                        (template == ControlTemplate.DataGrid ? "(360, 200)" : "(240, 240)"),
                        "Standalone placeholders have useful bounded preferred sizes.");
                    if (template == ControlTemplate.DataGrid)
                    {
                        var columns = SyntaxFactory.ParseExpression(node.Arguments.Single(a => a.Name == "columns").Value)
                            as ArrayCreationExpressionSyntax;
                        Assert(columns?.Initializer?.Expressions.Count == 2, "Data grid supplies two static columns, not a provider expression.");
                        var titles = columns!.Initializer!.Expressions.Cast<ImplicitObjectCreationExpressionSyntax>()
                            .Select(c => ((LiteralExpressionSyntax)c.ArgumentList.Arguments[0].Expression).Token.ValueText);
                        Assert(titles.SequenceEqual(["Name", "Value"]), "Data grid has meaningful authored column headers.");
                    }
                    else
                        Assert(node.Arguments.Single(a => a.Name == "headerVisible").Value == "true",
                            "Navigation placeholder keeps its built-in header visible.");
                    var secondResult = first.InsertControl(first.Revision, 0, 1, template, grid ? new(0, 1) : null);
                    var second = Parse(Apply(first, secondResult));
                    Assert(second.Root!.Children.Count == 2, "Repeated insertion compiles without duplicate identities.");
                    Assert(second.Root.Children[0].Arguments.All(a => a.Name is not ("ref" or "id" or "searchId")) &&
                        second.Root.Children[1].Arguments.All(a => a.Name is not ("ref" or "id" or "searchId")),
                        "Repeated templates introduce no identity collisions.");
                    Refused(first.WrapNode(first.Revision, node.Id, template), "VStack");
                }
            }
        }
    }
}
