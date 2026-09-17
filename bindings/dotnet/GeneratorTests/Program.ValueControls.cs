using System.Reflection;
using System.Runtime.Loader;
using Microsoft.CodeAnalysis;

internal static partial class Program
{
    private static void TestValueControls()
    {
        const string source = """
            component Values {
                param global::Xui.NumericRange Bounds;
                state double Current = 150;
                state bool Reverse = false;
                state global::Xui.Axis Direction = global::Xui.Axis.Horizontal;
                state global::Xui.ProgressState Status = global::Xui.ProgressState.Determinate;
                state int Calls = 0;
                style TrackStyle for RangeInput { part track { background: 0x112233; } }
                style MeterStyle for Progress { part fill { background: 0x223344; } }
                view { VStack() {
                    RangeInput("Input", ref: Input, range: Bounds, currentValue: Current,
                        reversed: Reverse, orientation: Direction, change: Changed, style: TrackStyle);
                    Progress("Meter", ref: Meter, range: Bounds, currentValue: Current, progressState: Status, style: MeterStyle);
                    RangeInput("Default", ref: Default);
                    Progress("Clamped", ref: Clamped, range: Bounds);
                } }
                code csharp { void Changed(double value) { Calls++; Current = value; } }
            }
            """;
        var (driver, compilation) = Generate(new File(@"C:\fixture\Values.xui", source));
        using var bytes = new MemoryStream();
        var emitted = compilation.Emit(bytes);
        Assert(emitted.Success, string.Join("\n", emitted.Diagnostics));
        bytes.Position = 0;
        var context = new AssemblyLoadContext("value-controls", isCollectible: true);
        var type = context.LoadFromStream(bytes).GetType("Values")!;
        var instance = Activator.CreateInstance(type, new Xui.Window(), new Xui.NumericRange(100, 200), true)!;
        var input = (Xui.RangeInput)type.GetProperty("Input")!.GetValue(instance)!;
        var meter = (Xui.Progress)type.GetProperty("Meter")!.GetValue(instance)!;
        var defaults = (Xui.RangeInput)type.GetProperty("Default")!.GetValue(instance)!;
        var clamped = (Xui.Progress)type.GetProperty("Clamped")!.GetValue(instance)!;
        Assert(input.Range.Minimum == 100 && input.Value == 150 && meter.Value == 150, "Range initializes before authored value.");
        Assert(input.Name == "Input" && meter.Name == "Meter", "Positional strings set native accessible names.");
        Assert(defaults.Range == new Xui.NumericRange(0, 100) && defaults.Value == 0 && defaults.RangeSets == 0 && defaults.ValueSets == 0,
            "Omitted arguments preserve native defaults without synthetic setters.");
        Assert(clamped.Value == 100 && clamped.ValueSets == 0, "Omitted value preserves the range setter's native clamp.");
        Assert(input.ControlStyle?.Target == Xui.StyleTarget.RangeInput && meter.ControlStyle?.Target == Xui.StyleTarget.Progress,
            "Value controls reuse catalog style targets.");
        input.ChangeValue(175);
        Assert((int)type.GetProperty("Calls")!.GetValue(instance)! == 1 && meter.Value == 175, "Committed double event updates state and progress.");
        type.GetProperty("Current")!.SetValue(instance, 180d);
        type.GetProperty("Reverse")!.SetValue(instance, true);
        type.GetProperty("Direction")!.SetValue(instance, Xui.Axis.Vertical);
        type.GetProperty("Status")!.SetValue(instance, Xui.ProgressState.Paused);
        Assert(input.Value == 180 && meter.Value == 180 && input.Reversed && input.Orientation == Xui.Axis.Vertical &&
            meter.State == Xui.ProgressState.Paused, "Typed reactive bindings update native properties.");
        Assert((int)type.GetProperty("Calls")!.GetValue(instance)! == 1, "Programmatic updates do not call change handlers.");
        var refresh = type.GetMethod("__xuiRefresh", BindingFlags.Instance | BindingFlags.NonPublic)!;
        refresh.Invoke(instance, null);
        refresh.Invoke(instance, null);
        Assert(input.RangeSets == 1 && meter.RangeSets == 1 && input.Subscriptions == 1, "Refresh neither rebinds range nor duplicates subscriptions.");
        input.ChangeValue(185);
        Assert((int)type.GetProperty("Calls")!.GetValue(instance)! == 2, "One callback remains after refresh.");
        context.Unload();

        string Shape(string text)
        {
            var (generated, _) = Generate(new File(@"C:\fixture\Values.xui", text));
            return generated.GetRunResult().Results.Single().GeneratedSources.Single(s => s.HintName.StartsWith(".Values"))
                .SourceText.ToString().Split('\n').Single(line => line.StartsWith("private string __xuiShape()"));
        }
        string shape = Shape(source);
        Assert(Shape(source.Replace("range: Bounds", "range: new global::Xui.NumericRange(100, 300)")) != shape,
            "Range source changes require structural reload.");
        Assert(Shape(source.Replace("currentValue: Current", "currentValue: Current + 1")) == shape, "Numeric value edits remain reactive.");
        Assert(Shape(source.Replace("progressState: Status", "progressState: global::Xui.ProgressState.Error")) == shape,
            "Progress state expression edits preserve shape.");

        foreach (string invalid in new[]
        {
            """component Bad { state double Bound = 10; view { RangeInput("x", range: new global::Xui.NumericRange(0, Bound)); } }""",
            """component Bad { view { Progress("x", range: Bounds()); } code csharp { global::Xui.NumericRange Bounds() => new(0, 10); } }""",
            """component Bad { view { RangeInput("x", range: new global::Xui.NumericRange(0, Count++)); } state int Count = 10; }""",
            """component Bad { view { Slider("x"); } }""",
            """component Bad { view { Progress("x", change: Changed); } code csharp { void Changed(double value) { } } }""",
            """component Bad { view { RangeInput("x", value: 1); } }""",
            """component Bad { view { RangeInput("x") { Text("child"); } } }"""
        }) Invalid(invalid);
        foreach (string invalid in new[]
        {
            """component Bad { view { RangeInput("x", currentValue: "bad"); } }""",
            """component Bad { view { Progress("x", range: (0, 100)); } }""",
            """component Bad { view { Progress("x", progressState: true); } }""",
            """component Bad { view { RangeInput("x", change: Changed); } code csharp { void Changed(string value) { } } }"""
        })
        {
            var (_, bad) = Generate(new File(@"C:\fixture\BadValue.xui", invalid));
            Assert(bad.GetDiagnostics().Any(d => d.Severity == DiagnosticSeverity.Error), "Incorrect property and handler types reject.");
        }
    }
}
