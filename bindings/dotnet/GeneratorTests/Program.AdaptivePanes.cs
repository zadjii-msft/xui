using System.Reflection;
using System.Runtime.Loader;

internal static partial class Program
{
    private static void TestAdaptivePanes()
    {
        var (_, compilation) = Generate(new File(@"C:\fixture\DockedPanes.xui", """
            component DockedPanes {
                state bool Pinned = true;
                view { VStack() {
                    AdaptiveLayout("Workspace", ref: Dock,
                        presentation: Pinned ? global::Xui.AdaptivePresentation.InlinePane : global::Xui.AdaptivePresentation.Overlay,
                        navigationSide: global::Xui.NavigationSide.Right, navigationExtent: 280, breakpoint: 640,
                        compactNavigation: global::Xui.CompactNavigation.Overlay, navigationOpen: false, dismissOnFocusOutside: true) {
                        TextInput("Properties", ref: Field);
                        TextInput("Source", ref: Source);
                    }
                } }
            }
            """));
        using var bytes = new MemoryStream();
        var emitted = compilation.Emit(bytes);
        Assert(emitted.Success, string.Join("\n", emitted.Diagnostics));
        bytes.Position = 0;
        var context = new AssemblyLoadContext("adaptive-panes", isCollectible: true);
        var type = context.LoadFromStream(bytes).GetType("DockedPanes")!;
        var instance = Activator.CreateInstance(type, new Xui.Window(), true)!;
        var dock = (Xui.AdaptiveLayout)type.GetProperty("Dock")!.GetValue(instance)!;
        var field = (Xui.TextInput)type.GetProperty("Field")!.GetValue(instance)!;
        var source = (Xui.TextInput)type.GetProperty("Source")!.GetValue(instance)!;
        Assert(dock.Presentation == Xui.AdaptivePresentation.InlinePane && dock.NavigationSide == Xui.NavigationSide.Right &&
            dock.NavigationExtent == 280 && dock.Breakpoint == 640 && !dock.NavigationOpen && dock.DismissOnFocusOutside &&
            dock.CompactNavigation == Xui.CompactNavigation.Overlay, "Adaptive markup initializes every structural property.");
        Assert(ReferenceEquals(dock.Children[0].Child, field) && ReferenceEquals(dock.Children[1].Child, source),
            "Adaptive markup passes navigation first and content second.");
        field.Text = "Retained property draft";
        type.GetProperty("Pinned")!.SetValue(instance, false);
        type.GetProperty("Pinned")!.SetValue(instance, false);
        type.GetMethod("__xuiRefresh", BindingFlags.Instance | BindingFlags.NonPublic)!.Invoke(instance, null);
        Assert(dock.Presentation == Xui.AdaptivePresentation.Overlay && dock.PresentationSets == 2 &&
            ReferenceEquals(dock.Children[0].Child, field) && field.Text == "Retained property draft",
            "Reactive pin state updates the retained layout once without replacing its children.");
        context.Unload();
        Invalid("""component Bad { view { AdaptiveLayout("x") { Text("Only one"); } } }""");
        Invalid("""component Bad { view { AdaptiveLayout("x") { Text("a"); Text("b"); Text("c"); } } }""");
        Invalid("""component Bad { view { AdaptiveLayout("x", id: "not-a-control") { Text("a"); Text("b"); } } }""");
        Invalid("""component Bad { state string Name = "x"; view { AdaptiveLayout(Name) { Text("a"); Text("b"); } } }""");
        const string shapeSource = """component Pane { view { AdaptiveLayout("Original", navigationExtent: 220) { Text("a"); Text("b"); } } }""";
        Assert(Shape(shapeSource) != Shape(shapeSource.Replace("Original", "Renamed")),
            "Changing an adaptive constructor name requires replacement.");
        Assert(Shape(shapeSource) == Shape(shapeSource.Replace("220", "284")),
            "Changing adaptive presentation properties preserves the structural signature.");

        static string Shape(string source)
        {
            var (driver, _) = Generate(new File(@"C:\fixture\Pane.xui", source));
            return driver.GetRunResult().Results.Single().GeneratedSources.Single(s => s.HintName.StartsWith(".Pane"))
                .SourceText.ToString().Split('\n').Single(line => line.StartsWith("private string __xuiShape()"));
        }
    }
}
