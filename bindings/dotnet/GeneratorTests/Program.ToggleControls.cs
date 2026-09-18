using System.Reflection;
using System.Runtime.Loader;
using Microsoft.CodeAnalysis;

internal static partial class Program
{
    private static void TestToggleControls()
    {
        const string source = """
            component NewControls {
                param global::Xui.NumericRange Bounds;
                state bool Active = false;
                state double Current = 150;
                state global::Xui.ProgressState Status = global::Xui.ProgressState.Paused;
                state int Changes = 0;
                style SwitchStyle for ToggleSwitch { part indicator { when checked { background: 0x123456; } } }
                style ButtonStyle for ToggleButton { background: 0x234567; }
                style RingStyle for ProgressRing { part fill { background: 0x345678; } }
                style GenericButton for ToggleButton { part label { foreground: 0x456789; } }
                view { VStack() {
                    ToggleSwitch("Enabled", checked: Active, change: Changed, ref: Switch, style: SwitchStyle);
                    ToggleButton("Bold", checked: Active, change: Changed, ref: Button, style: ButtonStyle);
                    ToggleButton("Generic", style: GenericButton, ref: Generic);
                    ProgressRing("Progress", range: Bounds, currentValue: Current, progressState: Status, ref: Ring, style: RingStyle);
                    ProgressRing("Loading", ref: DefaultRing);
                } }
                code csharp {
                    void Changed(bool value) { Active = value; Changes++; }
                }
            }
            """;
        var (_, compilation) = Generate(new File(@"C:\fixture\NewControls.xui", source));
        using var bytes = new MemoryStream();
        var emitted = compilation.Emit(bytes);
        Assert(emitted.Success, string.Join("\n", emitted.Diagnostics));
        bytes.Position = 0;
        var context = new AssemblyLoadContext("new-controls", isCollectible: true);
        var type = context.LoadFromStream(bytes).GetType("NewControls")!;
        var instance = Activator.CreateInstance(type, new Xui.Window(), new Xui.NumericRange(100, 200), true)!;
        T Control<T>(string name) => (T)type.GetProperty(name)!.GetValue(instance)!;
        var toggle = Control<Xui.ToggleSwitch>("Switch");
        var button = Control<Xui.ToggleButton>("Button");
        var ring = Control<Xui.ProgressRing>("Ring");
        var defaults = Control<Xui.ProgressRing>("DefaultRing");
        Assert(toggle.Text == "Enabled" && button.Text == "Bold" && ring.Name == "Progress", "New control names bind to native text.");
        Assert(ring.Range.Minimum == 100 && ring.Value == 150 && ring.State == Xui.ProgressState.Paused, "Ring range precedes its reactive value and state.");
        Assert(defaults.State == Xui.ProgressState.Indeterminate && defaults.RangeSets == 0 && defaults.ValueSets == 0,
            "Omitted arguments preserve the ring's native indeterminate default.");
        Assert(toggle.ControlStyle?.Target == Xui.StyleTarget.Toggle && ring.ControlStyle?.Target == Xui.StyleTarget.Progress &&
            Control<Xui.ToggleButton>("Generic").ControlStyle?.Target == Xui.StyleTarget.Button && button.Style is not null,
            "Alias targets support generic styles and legacy Button styles without new style IDs.");
        toggle.Invoke(true);
        Assert(button.Checked && (int)type.GetProperty("Changes")!.GetValue(instance)! == 1, "Switch changes propagate checked state.");
        button.Invoke(false);
        Assert(!toggle.Checked && (int)type.GetProperty("Changes")!.GetValue(instance)! == 2, "ToggleButton emits a checked change.");
        type.GetProperty("Current")!.SetValue(instance, 175d);
        type.GetProperty("Status")!.SetValue(instance, Xui.ProgressState.Error);
        type.GetProperty("Active")!.SetValue(instance, true);
        Assert(ring.Value == 175 && ring.State == Xui.ProgressState.Error && toggle.Checked && button.Checked, "Reactive values update typed controls.");
        var refresh = type.GetMethod("__xuiRefresh", BindingFlags.Instance | BindingFlags.NonPublic)!;
        refresh.Invoke(instance, null); refresh.Invoke(instance, null);
        button.Invoke(false);
        Assert((int)type.GetProperty("Changes")!.GetValue(instance)! == 3 && ring.RangeSets == 1,
            "Refresh does not duplicate event subscriptions or range initialization.");
        context.Unload();

        foreach (string invalid in new[] {
            """component Bad { view { ProgressRing("x", change: Changed); } code csharp { void Changed(bool value) { } } }""",
            """component Bad { state double Max = 100; view { ProgressRing("x", range: new global::Xui.NumericRange(0, Max)); } }""",
            """component Bad { view { ToggleSwitch("x") { Text("child"); } } }""",
            """component Bad { view { ToggleButton("x", click: Clicked); } code csharp { void Clicked() { } } }""",
            """component Bad { style Wrong for ProgressRing { part indicator { size: 20; } } view { ProgressRing("x", style: Wrong); } }"""
        }) Invalid(invalid);
        foreach (string invalid in new[] {
            """component Bad { view { ToggleButton("x", checked: 1); } }""",
            """component Bad { view { ToggleSwitch("x", change: Changed); } code csharp { void Changed(string value) { } } }""",
            """component Bad { view { ProgressRing("x", progressState: true); } }"""
        }) {
            var (_, bad) = Generate(new File(@"C:\fixture\BadNewControl.xui", invalid));
            Assert(bad.GetDiagnostics().Any(d => d.Severity == DiagnosticSeverity.Error), "New control bindings enforce property and callback types.");
        }
    }
}
