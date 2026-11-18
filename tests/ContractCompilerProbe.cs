using System.Diagnostics;
using System.Text.Json;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.Text;
using Xui.Generator;

const string source = """
component Probe {
    state string Entry = "";
    state bool Checked = false;
    state double Value = 0;
    view { VStack(spacing: 8, padding: 12) {
        Text("Metadata", id: "title");
        TextInput("Entry", text: Entry, change: Edit, placeholder: "Type");
        ToggleSwitch("Toggle", checked: Checked, change: Toggle);
        RangeInput("Value", currentValue: Value, range: new global::Xui.NumericRange(0, 100), change: Change);
        ProgressRing("Progress", currentValue: Value);
        Grid("Grid", rows: [], columns: []) { Button("OK", click: Click); }
        SplitView("Split", secondVisible: Checked) { Text("First"); Text("Second"); }
        Popup("Popup") { Text("Popup text"); }
        AdaptiveLayout("Adaptive", navigationOpen: Checked) { Text("Navigation"); Text("Body"); }
        Reveal("Reveal", open: Checked) { Text("Revealed"); }
    } }
    code csharp {
        void Edit(string value) => Entry = value;
        void Toggle(bool value) => Checked = value;
        void Change(double value) => Value = value;
        void Click() { }
    }
}
""";
var file = new Input(source);
var compilation = CSharpCompilation.Create("Probe");
long Parse()
{
    var result = XuiSourceParser.Parse(source);
    if (!result.Success) throw new InvalidOperationException(string.Join("\n", result.Diagnostics));
    return result.Root!.Children.Count;
}
long Generate()
{
    GeneratorDriver driver = CSharpGeneratorDriver.Create([new XuiGenerator().AsSourceGenerator()], [file]);
    var result = driver.RunGenerators(compilation).GetRunResult();
    if (result.Diagnostics.Any(d => d.Severity == DiagnosticSeverity.Error))
        throw new InvalidOperationException(string.Join("\n", result.Diagnostics));
    return result.Results.Single().GeneratedSources.Length;
}
Measure("markup-parse", 4096, Parse);
Measure("markup-generate", 512, Generate);

static void Measure(string name, int count, Func<long> action)
{
    for (int i = 0; i < 32; ++i) action();
    GC.Collect(); GC.WaitForPendingFinalizers(); GC.Collect();
    using var process = Process.GetCurrentProcess();
    var cpu = process.TotalProcessorTime;
    long allocated = GC.GetAllocatedBytesForCurrentThread();
    var watch = Stopwatch.StartNew();
    long checksum = 0;
    for (int i = 0; i < count; ++i) checksum += action();
    watch.Stop();
    allocated = GC.GetAllocatedBytesForCurrentThread() - allocated;
    Console.WriteLine(JsonSerializer.Serialize(new
    {
        phase = name, iterations = count, wall_ms = watch.Elapsed.TotalMilliseconds,
        cpu_ms = (process.TotalProcessorTime - cpu).TotalMilliseconds, allocated_bytes = allocated, checksum
    }));
}

sealed class Input(string source) : AdditionalText
{
    private readonly SourceText text = SourceText.From(source);
    public override string Path => @"C:\fixture\ContractProbe.xui";
    public override SourceText GetText(CancellationToken cancellationToken = default) => text;
}
