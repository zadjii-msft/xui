using System.Collections;
using System.Reflection;
using System.Runtime.Loader;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.Text;
using Xui.Generator;

internal static class DesignerMetadataTests
{
    private const string Fixture = """
        namespace Tooling;
        component Mapping {
            param global::Xui.Element External;
            view {
                VStack(ref: Outer) {
                    Text("text", ref: Label);
                    HStack(ref: Row) {
                        Button("button", ref: Button);
                        Toggle("toggle", ref: Toggle);
                        TextInput("input", ref: Input);
                    }
                    Grid("grid", ref: Grid) { Text("cell", ref: Cell); }
                    ScrollView("scroll", ref: Scroll) { VStack(ref: ScrollBody) { } }
                    Popup("popup", ref: Popup) { Text("popup content", ref: PopupBody); }
                    SplitView("split", ref: Split) {
                        Text("first", ref: First);
                        Text("second", ref: Second);
                    }
                    Content(External, ref: ExternalElement);
                    DataGrid("data", ref: Data);
                    NavigationView("navigation", ref: Navigation);
                    ItemsView("items", ref: Items);
                }
            }
        }
        """;

    internal static void Run(Action<bool, string> assert)
    {
        var references = ((string)AppContext.GetData("TRUSTED_PLATFORM_ASSEMBLIES")!)
            .Split(Path.PathSeparator).Where(path => !string.Equals(path, typeof(Xui.Window).Assembly.Location,
                StringComparison.OrdinalIgnoreCase))
            .Select(path => MetadataReference.CreateFromFile(path)).ToArray();
        string fakeSource = Resource("Fakes.cs");
        string catalog = Resource("ControlStyleCatalog.cs");
        foreach (string source in new[]
        {
            Fixture, Fixture.ReplaceLineEndings("\r"), Fixture.ReplaceLineEndings("\r\n"),
            """namespace Tooling; component Mapping { param global::Xui.Element External; view { Content(External, ref: ExternalElement); } }""",
            """namespace Tooling; component Mapping { param global::Xui.Element External; view { Grid("grid", ref: Grid) { Text("cell", ref: Cell); } } }"""
        })
        {
            var syntax = XuiSourceParser.Parse(source);
            assert(syntax.Success, "Metadata fixture parses with the tooling parser.");
            var nodes = Flatten(syntax.Root!).ToArray();
            using var designerBytes = Compile(source, enabled: true);
            using var ordinaryBytes = Compile(source, enabled: false);
            var context = new AssemblyLoadContext("designer-metadata-" + Guid.NewGuid(), isCollectible: true);
            try
            {
                var assembly = context.LoadFromStream(designerBytes);
                var ordinaryAssembly = context.LoadFromStream(ordinaryBytes);
                var type = assembly.GetType("Tooling.Mapping")!;
                var ordinary = ordinaryAssembly.GetType("Tooling.Mapping")!;
                const BindingFlags all = BindingFlags.Instance | BindingFlags.Static | BindingFlags.Public | BindingFlags.NonPublic;
                assert(!ordinary.GetMembers(all).Any(m => m.Name.Contains("__xuiDesigner", StringComparison.Ordinal)),
                    "Ordinary emitted components contain no designer metadata members.");
                assert(type.GetFields(all).Select(f => f.Name).Order().SequenceEqual(ordinary.GetFields(all).Select(f => f.Name).Order()),
                    "Designer metadata adds no instance or static fields.");

                var window = Activator.CreateInstance(assembly.GetType("Xui.Window")!)!;
                var external = window.GetType().GetMethod("Label")!.Invoke(window, ["external"])!;
                var component = Activator.CreateInstance(type, window, external, false)!;
                var probe = assembly.GetType("Tooling.DesignerProbe")!;
                var lookup = probe.GetMethod("Element")!;
                int count = (int)probe.GetMethod("Count")!.Invoke(null, [component])!;
                assert(count == nodes.Length, "Generated metadata count equals parse-only authored node count.");
                var elements = (ICollection)window.GetType().GetField("Elements")!.GetValue(window)!;
                int elementCount = elements.Count;
                foreach (var node in nodes)
                {
                    var expected = type.GetProperty(node.Arguments.Single(a => a.Name == "ref").Value)!.GetValue(component);
                    var actual = lookup.Invoke(null, [component, node.Id]);
                    assert(ReferenceEquals(actual, expected), $"{node.Kind} node {node.Id} maps to its exact authored ref instance.");
                    if (node.Kind == "Content")
                        assert(ReferenceEquals(actual, external), "Content maps to the existing authored Element.");
                    if (node.Id == 0)
                        assert(ReferenceEquals(actual, type.GetProperty("Root")!.GetValue(component)), "ID zero maps to any root kind.");
                }
                assert(elements.Count == elementCount, "Mapping does not construct native elements or wrappers.");
                foreach (int invalid in new[] { -1, count, int.MaxValue })
                {
                    try
                    {
                        lookup.Invoke(null, [component, invalid]);
                        assert(false, "Invalid designer node ID must throw.");
                    }
                    catch (TargetInvocationException error)
                    {
                        assert(error.InnerException is ArgumentOutOfRangeException { ParamName: "nodeId" },
                            "Invalid ID reports the nodeId argument.");
                    }
                }
                Exception? wrongThread = Task.Run(() =>
                {
                    try { lookup.Invoke(null, [component, 0]); return null; }
                    catch (TargetInvocationException error) { return error.InnerException; }
                }).GetAwaiter().GetResult();
                assert(wrongThread is InvalidOperationException { Message: "UI thread required." },
                    "Designer element access enforces the owning UI thread.");
            }
            finally
            {
                context.Unload();
            }
        }

        MemoryStream Compile(string source, bool enabled)
        {
            string[] symbols = enabled ? ["XUI_DESIGNER"] : [];
            var options = CSharpParseOptions.Default.WithLanguageVersion(LanguageVersion.Latest)
                .WithPreprocessorSymbols(symbols);
            var compilation = CSharpCompilation.Create("DesignerMetadata." + Guid.NewGuid().ToString("N"),
                [
                    CSharpSyntaxTree.ParseText("global using System; global using System.Collections.Generic; global using System.Linq;", options),
                    CSharpSyntaxTree.ParseText(fakeSource, options),
                    CSharpSyntaxTree.ParseText(catalog, options),
                    CSharpSyntaxTree.ParseText("""
                        #if XUI_DESIGNER
                        namespace Tooling;
                        public static class DesignerProbe {
                            public static int Count(Mapping component) => component.__xuiDesignerNodeCount;
                            public static global::Xui.Element Element(Mapping component, int nodeId) => component.__xuiDesignerElement(nodeId);
                        }
                        #endif
                        """, options)
                ], references, new CSharpCompilationOptions(OutputKind.DynamicallyLinkedLibrary,
                    nullableContextOptions: NullableContextOptions.Enable));
            GeneratorDriver driver = CSharpGeneratorDriver.Create(
                generators: [new XuiGenerator().AsSourceGenerator()], additionalTexts: [new SourceFile(source)], parseOptions: options);
            driver.RunGeneratorsAndUpdateCompilation(compilation, out var generated, out var diagnostics);
            assert(!diagnostics.Any(d => d.Severity == DiagnosticSeverity.Error), string.Join("\n", diagnostics));
            var bytes = new MemoryStream();
            var emitted = generated.Emit(bytes);
            assert(emitted.Success, string.Join("\n", emitted.Diagnostics));
            bytes.Position = 0;
            return bytes;
        }
    }

    private static IEnumerable<XuiSourceNode> Flatten(XuiSourceNode node)
    {
        yield return node;
        foreach (var child in node.Children)
            foreach (var descendant in Flatten(child)) yield return descendant;
    }

    private static string Resource(string name)
    {
        using var stream = typeof(DesignerMetadataTests).Assembly.GetManifestResourceStream("Designer.SourceTests." + name)!;
        using var reader = new StreamReader(stream);
        return reader.ReadToEnd();
    }

    private sealed class SourceFile(string source) : AdditionalText
    {
        public override string Path => "DesignerMapping.xui";
        public override SourceText GetText(CancellationToken cancellationToken = default) => SourceText.From(source);
    }
}
