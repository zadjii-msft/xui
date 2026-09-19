using System.Collections.Immutable;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.Diagnostics;
using Xui.Generator;

internal static partial class Program
{
    private sealed class ProfileOptions(string profile) : AnalyzerConfigOptionsProvider
    {
        private sealed class Options(string profile) : AnalyzerConfigOptions
        {
            public override bool TryGetValue(string key, out string value)
            {
                value = profile;
                return key == "build_property.XuiGeneratorProfile";
            }
        }
        public override AnalyzerConfigOptions GlobalOptions { get; } = new Options(profile);
        public override AnalyzerConfigOptions GetOptions(SyntaxTree tree) => GlobalOptions;
        public override AnalyzerConfigOptions GetOptions(AdditionalText textFile) => GlobalOptions;
    }

    private static void TestPortableProfile()
    {
        const string path = @"C:\fixture\Portable.xui";
        var references = References.Append(MetadataReference.CreateFromFile(typeof(Xui.Experimental.Portable.Host).Assembly.Location));
        var empty = CSharpCompilation.Create("PortableGenerated", references: references,
            options: new CSharpCompilationOptions(OutputKind.DynamicallyLinkedLibrary, nullableContextOptions: NullableContextOptions.Enable));
        (GeneratorDriver Driver, Compilation Compilation, ImmutableArray<Diagnostic> Diagnostics) Run(string text, string profile = "Portable")
        {
            GeneratorDriver driver = CSharpGeneratorDriver.Create([new XuiGenerator().AsSourceGenerator()],
                [new File(path, text)], optionsProvider: new ProfileOptions(profile));
            driver = driver.RunGeneratorsAndUpdateCompilation(empty, out var compilation, out var diagnostics);
            return (driver, compilation, diagnostics);
        }
        void Valid(string text)
        {
            var (_, compilation, diagnostics) = Run(text);
            Assert(!diagnostics.Any(d => d.Severity == DiagnosticSeverity.Error), string.Join("\n", diagnostics));
            Assert(!compilation.GetDiagnostics().Any(d => d.Severity == DiagnosticSeverity.Error),
                string.Join("\n", compilation.GetDiagnostics()));
        }
        void Invalid(string text, string message)
        {
            var (_, _, diagnostics) = Run(text);
            var error = diagnostics.FirstOrDefault(d => d.Id == "XUI001" && d.GetMessage().Contains(message, StringComparison.Ordinal));
            Assert(error is not null, $"Missing portable diagnostic '{message}': {string.Join("\n", diagnostics)}");
            Assert(error!.Location.GetLineSpan().Path == path && error.Location.SourceSpan.Length == 1,
                "Portable diagnostics retain source locations.");
            Assert(error.Location.SourceSpan.Start < text.Length, "Portable diagnostic is within authored source.");
        }
        const string supported = """
            namespace PortableChecks;
            component All {
                param string Initial;
                state string Entry = "";
                state bool Enabled = true;
                state float Gap = 4;
                view {
                    VStack(spacing: Gap, padding: 8, ref: Layout, size: (400, 500)) {
                        ScrollView("Content", id: "scroll", help: "Content", enabled: Enabled, visible: Enabled, flex: 1) {
                            HStack(spacing: 2, padding: 3, preferredSize: (300, 80)) {
                                Text(Initial, id: "label", enabled: Enabled, visible: Enabled, help: "A label", flex: 1);
                                TextInput("Caption", name: "Accessible name", text: Entry, change: Edit, submit: Submit,
                                    placeholder: "Hint", captionVisible: false, ref: Input, preferredSize: (120, 40));
                                Button("Go", click: Submit, id: "button", ref: ActionButton, size: (60, 40));
                            }
                        }
                    }
                }
                code csharp {
                    void Edit(string text) => Entry = text;
                    void Submit() => Entry = "global::Xui.Window stays authored C#";
                }
            }
            """;
        Valid(supported);
        var generated = Run(supported).Compilation.SyntaxTrees.Last().ToString();
        Assert(generated.Contains("\"global::Xui.Window stays authored C#\"", StringComparison.Ordinal),
            "Portable emission must not rewrite authored C#.");
        Assert(generated.Contains("global::Xui.Experimental.Portable.Host", StringComparison.Ordinal),
            "Portable constructor uses the independent host.");
        using var resource = typeof(Program).Assembly.GetManifestResourceStream("GeneratorTests.SharedGreeting.xui")!;
        using var reader = new StreamReader(resource);
        string demo = reader.ReadToEnd();
        Valid(demo);
        var (_, windows, windowsDiagnostics) = Run(demo, "Windows");
        Assert(!windowsDiagnostics.Any(d => d.Severity == DiagnosticSeverity.Error), string.Join("\n", windowsDiagnostics));
        Assert(!windows.GetDiagnostics().Any(d => d.Severity == DiagnosticSeverity.Error), string.Join("\n", windows.GetDiagnostics()));
        var unknown = Run(demo, "Portabel");
        Assert(unknown.Diagnostics.Any(d => d.Id == "XUI001" && d.GetMessage().Contains("Unknown XuiGeneratorProfile")), "Reject profile typos.");
        var portableDriver = Run(demo).Driver;
        portableDriver = portableDriver.WithUpdatedAnalyzerConfigOptions(new ProfileOptions("Windows"));
        portableDriver.RunGeneratorsAndUpdateCompilation(empty, out var switched, out var switchedDiagnostics);
        Assert(!switchedDiagnostics.Any(d => d.Severity == DiagnosticSeverity.Error), "Incremental profile switch.");
        Assert(switched.SyntaxTrees.Any(t => t.ToString().Contains("global::Xui.Window window")), "Profile is an incremental input.");
        foreach (string node in new[] { "Toggle(\"No\")", "Grid(\"No\") {}", "Content(null!)", "SwapChainPanel(\"No\")" })
        {
            string syntax = node.EndsWith('}') ? node : node + ";";
            Invalid($"component Invalid {{ view {{ VStack() {{ {syntax} }} }} }}", "Portable profile does not support");
        }
        foreach (string property in new[] { "icon: default", "background: default", "padding: 8", "row: 0", "style: Missing" })
            Invalid($"component Invalid {{ view {{ VStack() {{ Button(\"No\", {property}); }} }} }}", "Portable profile does not support");
        Invalid("component Invalid { view { Text(\"No\"); } }", "requires a VStack or HStack root");
        Invalid("component Invalid { state float Weight = 1; view { VStack() { Button(\"No\", flex: Weight); } } }", "cannot depend on state");
        Invalid("component Invalid { view { VStack() { ScrollView(\"S\") { Text(\"No\", flex: 1); } } } }", "requires a Stack parent");
        Invalid("component Invalid { view { VStack() { Button(\"No\", ref: Root); } } }", "reserved or duplicated");
        Invalid("component Invalid { resources { Accent: 0x0078d4; } view { VStack() {} } }", "does not support resources");
        Invalid("component Invalid { style Accent for Button {} view { VStack() {} } }", "does not support styles");
        Invalid("component Invalid { state int Count = 0; view { VStack() { Text((Count++).ToString()); } } }", "side-effect-free");
    }
}
