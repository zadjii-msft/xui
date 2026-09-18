using System.Runtime.Loader;

internal static partial class Program
{
    private static void TestSplitAnimation()
    {
        const string source = """
            component AnimatedPanes {
                state bool Open = false;
                state uint Motion = 180;
                view { VStack() {
                    SplitView("Panes", secondVisible: Open, duration: Motion, ref: Panes) {
                        TextInput("First");
                        TextInput("Second");
                    }
                } }
            }
            """;
        var (_, compilation) = Generate(new File(@"C:\fixture\AnimatedPanes.xui", source));
        using var bytes = new MemoryStream();
        var emitted = compilation.Emit(bytes);
        Assert(emitted.Success, string.Join("\n", emitted.Diagnostics));
        bytes.Position = 0;
        var context = new AssemblyLoadContext("animated-panes", isCollectible: true);
        var type = context.LoadFromStream(bytes).GetType("AnimatedPanes")!;
        var instance = Activator.CreateInstance(type, new Xui.Window(), true)!;
        var panes = (Xui.SplitView)type.GetProperty("Panes")!.GetValue(instance)!;
        Assert(panes.TransitionDuration == 180 && panes.DurationAtVisibilityChange == 180 && !panes.SecondVisible,
            "Split duration initializes before visibility regardless of argument order.");
        type.GetProperty("Motion")!.SetValue(instance, 240u);
        type.GetProperty("Open")!.SetValue(instance, true);
        Assert(panes.SecondVisible && panes.DurationAtVisibilityChange == 240 && panes.Children.Count == 2,
            "Split visibility and duration react without replacing pane children.");
        context.Unload();
    }
}
