using System.Reflection;
using System.Runtime.Loader;
using Microsoft.CodeAnalysis;

internal static partial class Program
{
    private static void TestReveal()
    {
        const string source = """
            component Reveals {
                state bool Expanded = true;
                state uint Motion = 180;
                state global::Xui.RevealLayout Mode = global::Xui.RevealLayout.Expand;
                state global::Xui.RevealDirection Edge = global::Xui.RevealDirection.Top;
                view { VStack() {
                    Reveal("Find", open: Expanded, direction: Edge, layout: Mode, duration: Motion, ref: Host) {
                        HStack(ref: Body) { TextInput("Find", ref: Input); }
                    }
                    Reveal("Default", ref: Default) { Text("Body"); }
                } }
            }
            """;
        var (_, compilation) = Generate(new File(@"C:\fixture\Reveals.xui", source));
        using var bytes = new MemoryStream();
        var emitted = compilation.Emit(bytes);
        Assert(emitted.Success, string.Join("\n", emitted.Diagnostics));
        bytes.Position = 0;
        var context = new AssemblyLoadContext("reveals", isCollectible: true);
        var type = context.LoadFromStream(bytes).GetType("Reveals")!;
        var instance = Activator.CreateInstance(type, new Xui.Window(), true)!;
        T Ref<T>(string name) => (T)type.GetProperty(name)!.GetValue(instance)!;
        var host = Ref<Xui.Reveal>("Host");
        var body = Ref<Xui.Stack>("Body");
        var input = Ref<Xui.TextInput>("Input");
        Assert(host.Name == "Find" && host.Open && host.Duration == 180 && host.DurationAtOpen == 180,
            "Reveal initializes duration before open and preserves its accessible name.");
        Assert(host.LayoutAtOpen == Xui.RevealLayout.Expand && host.DirectionAtOpen == Xui.RevealDirection.Top,
            "Reveal initializes layout and direction before open regardless of authored argument order.");
        Assert(ReferenceEquals(host.Children.Single(), body) && ReferenceEquals(body.Children.Single().Child, input),
            "Reveal retains exactly one child without replacing its input.");
        var defaults = Ref<Xui.Reveal>("Default");
        Assert(!defaults.Open && defaults.Duration == 0 && defaults.OpenSets == 0 && defaults.DurationSets == 0,
            "Omitted properties preserve native Reveal defaults.");
        Assert(defaults.Layout == Xui.RevealLayout.Fixed && defaults.Direction == Xui.RevealDirection.Bottom &&
            defaults.LayoutSets == 0 && defaults.DirectionSets == 0, "Omitted enum properties preserve fixed/bottom defaults.");
        input.Text = "native editor text";
        type.GetProperty("Expanded")!.SetValue(instance, false);
        type.GetProperty("Motion")!.SetValue(instance, 240u);
        type.GetProperty("Mode")!.SetValue(instance, Xui.RevealLayout.Fixed);
        type.GetProperty("Edge")!.SetValue(instance, Xui.RevealDirection.Right);
        type.GetProperty("Expanded")!.SetValue(instance, true);
        Assert(host.Open && host.Duration == 240 && input.Text == "native editor text" && ReferenceEquals(input, Ref<Xui.TextInput>("Input")),
            "Reactive reversal preserves the native input instance and its text.");
        type.GetMethod("__xuiRefresh", BindingFlags.Instance | BindingFlags.NonPublic)!.Invoke(instance, null);
        Assert(host.OpenSets == 3 && host.DurationSets == 2, "Refresh does not restart unchanged Reveal motion.");
        Assert(host.LayoutAtOpen == Xui.RevealLayout.Fixed && host.DirectionAtOpen == Xui.RevealDirection.Right &&
            host.LayoutSets == 2 && host.DirectionSets == 2, "Enum bindings react without redundant refresh setters.");
        try
        {
            type.GetProperty("Motion")!.SetValue(instance, 10001u);
            throw new Exception("Expected duration rejection.");
        }
        catch (TargetInvocationException error) when (error.InnerException is ArgumentOutOfRangeException) { count++; }
        foreach (var (property, value) in new (string, object)[] { ("Mode", (Xui.RevealLayout)2), ("Edge", (Xui.RevealDirection)4) })
        {
            try
            {
                type.GetProperty(property)!.SetValue(instance, value);
                throw new Exception("Expected enum rejection.");
            }
            catch (TargetInvocationException error) when (error.InnerException is ArgumentOutOfRangeException) { count++; }
        }
        context.Unload();

        foreach (string invalid in new[]
        {
            """component Bad { view { Reveal("x") {} } }""",
            """component Bad { view { Reveal("x") { Text("a"); Text("b"); } } }""",
            """component Bad { view { Reveal("x", progress: 0.5) { Text("a"); } } }""",
            """component Bad { view { Reveal("x", animating: true) { Text("a"); } } }"""
        }) Invalid(invalid);
        foreach (string invalid in new[]
        {
            """component Bad { view { Reveal(42) { Text("a"); } } }""",
            """component Bad { view { Reveal("x", open: 1) { Text("a"); } } }""",
            """component Bad { view { Reveal("x", duration: -1) { Text("a"); } } }""",
            """component Bad { view { Reveal("x", duration: 1.5) { Text("a"); } } }""",
            """component Bad { view { Reveal("x", duration: "180") { Text("a"); } } }""",
            """component Bad { view { Reveal("x", layout: true) { Text("a"); } } }""",
            """component Bad { view { Reveal("x", direction: "bottom") { Text("a"); } } }""",
            """component Bad { view { Reveal("x", layout: global::Xui.RevealDirection.Bottom) { Text("a"); } } }""",
            """component Bad { view { Reveal("x", direction: global::Xui.RevealLayout.Expand) { Text("a"); } } }"""
        })
        {
            var (_, bad) = Generate(new File(@"C:\fixture\BadReveal.xui", invalid));
            Assert(bad.GetDiagnostics().Any(d => d.Severity == DiagnosticSeverity.Error), "Reveal rejects incorrect property types.");
        }
    }
}
