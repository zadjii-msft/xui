using System.Reflection;
using System.Runtime.Loader;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.Text;
using Xui.Generator;

internal static class Program
{
    private static int count;
    private sealed class File(string path, string text) : AdditionalText
    {
        public override string Path => path;
        public override SourceText GetText(CancellationToken cancellationToken = default) => SourceText.From(text);
    }
    private sealed class UnreadableFile : AdditionalText
    {
        public override string Path => @"C:\fixture\Unreadable.xui";
        public override SourceText? GetText(CancellationToken cancellationToken = default) => null;
    }
    private static readonly MetadataReference[] References =
        ((string)AppContext.GetData("TRUSTED_PLATFORM_ASSEMBLIES")!).Split(System.IO.Path.PathSeparator)
            .Append(typeof(Xui.Window).Assembly.Location).Distinct().Select(p => MetadataReference.CreateFromFile(p)).ToArray();
    private static void Assert(bool condition, string message)
    {
        if (!condition) throw new Exception(message);
        count++;
    }
    private static CSharpCompilation Empty() => CSharpCompilation.Create("Generated" + Guid.NewGuid().ToString("N"),
        references: References, options: new CSharpCompilationOptions(OutputKind.DynamicallyLinkedLibrary, nullableContextOptions: NullableContextOptions.Enable));
    private static (GeneratorDriver Driver, Compilation Compilation) Generate(params AdditionalText[] files)
    {
        GeneratorDriver driver = CSharpGeneratorDriver.Create([new XuiGenerator().AsSourceGenerator()], files);
        driver = driver.RunGeneratorsAndUpdateCompilation(Empty(), out var compilation, out var diagnostics);
        Assert(!diagnostics.Any(d => d.Severity == DiagnosticSeverity.Error), string.Join("\n", diagnostics));
        return (driver, compilation);
    }
    private const string Counter = """
        namespace Demo;
        component Counter {
          state int Count = 0;
          state bool Active = false;
          state string Entry = "";
          view {
            VStack(spacing: 8, padding: 16) {
              Text($"Count: {Count}", id: "count");
              Button("Increment", click: Increment, id: "increment");
              Toggle("Active", checked: Active, change: SetActive, id: "active");
              TextInput("Entry", text: Entry, change: SetEntry, id: "entry");
              Text(global::ExpressionProbe.Constant(), id: "constant");
            }
          }
          code csharp {
            public void Increment() => Count++;
            void SetActive(bool value) => Active = value;
            void SetEntry(string value) => Entry = value;
          }
        }
        """;
    private static void Main()
    {
        TestExecution();
        TestParsing();
        TestUserNames();
        TestDiagnostics();
        TestIncremental();
        Console.WriteLine($"XUI generator assertions: {count} passed.");
    }
    private static void TestExecution()
    {
        var (_, compilation) = Generate(new File(@"C:\fixture\Counter.xui", Counter));
        var errors = compilation.GetDiagnostics().Where(d => d.Severity == DiagnosticSeverity.Error).ToArray();
        Assert(errors.Length == 0, string.Join("\n", errors.Select(e => e.ToString())));
        using var pe = new MemoryStream();
        var emitted = compilation.Emit(pe);
        Assert(emitted.Success, string.Join("\n", emitted.Diagnostics));
        pe.Position = 0;
        var context = new AssemblyLoadContext("generated-test", isCollectible: true);
        var assembly = context.LoadFromStream(pe);
        var type = assembly.GetType("Demo.Counter")!;
        var window = new Xui.Window();
        var counter = Activator.CreateInstance(type, window)!;
        var label = window.Elements.OfType<Xui.Label>().Single(c => c.AutomationId == "count");
        var constant = window.Elements.OfType<Xui.Label>().Single(c => c.AutomationId == "constant");
        var button = window.Elements.OfType<Xui.Button>().Single();
        var input = window.Elements.OfType<Xui.TextInput>().Single();
        var toggle = window.Elements.OfType<Xui.Toggle>().Single();
        var root = window.Elements.OfType<Xui.Stack>().Single();
        Assert(label.Text == "Count: 0" && root.CurrentSpacing == 8 && root.CurrentPadding == 16, "Initial direct setters");
        Assert(label.Name == "Count: 0" && button.Name == "Increment" && toggle.Name == "Active", "Initial native text/name aliases are not cleared");
        Assert(input.Name == "Entry" && input.Text == "", "TextInput name remains separate from text");
        Assert(ExpressionProbe.ConstantReads == 1, "Independent expression evaluated during construction");
        button.Invoke();
        Assert(label.Text == "Count: 1", "Named handler updates state and dependent label");
        Assert(constant.TextSets == 1 && root.SpacingSets == 1, "No unrelated native property updates");
        Assert(ExpressionProbe.ConstantReads == 1, "State mutation does not evaluate unrelated expressions");
        var state = type.GetProperty("Count")!;
        state.SetValue(counter, 1);
        Assert(label.TextSets == 2, "Equal state value causes no setter");
        toggle.Invoke(true);
        Assert((bool)type.GetProperty("Active")!.GetValue(counter)!, "Toggle change handler");
        input.Edit("user text");
        Assert((string)type.GetProperty("Entry")!.GetValue(counter)! == "user text", "Input two-way explicit handler");
        int before = input.TextSets;
        type.GetMethod("__xuiRefresh", BindingFlags.NonPublic | BindingFlags.Instance)!.Invoke(counter, null);
        Assert(input.TextSets == before, "Reload leaves unchanged authored value/user edit alone");
        var threadError = Task.Run(() =>
        {
            try { state.SetValue(counter, 9); return false; }
            catch (TargetInvocationException error) { return error.InnerException is InvalidOperationException; }
        }).GetAwaiter().GetResult();
        Assert(threadError, "State enforces UI thread");
        Assert(assembly.GetReferencedAssemblies().All(a => a.Name != "Xui.Development"), "Release omits development assembly");
        Assert(type.GetMethod("__xuiReload", BindingFlags.Instance | BindingFlags.NonPublic) is null, "Release omits reload method");
        Assert(type.GetField("__xuiOriginalShape", BindingFlags.Instance | BindingFlags.NonPublic) is null, "Release omits reload tracking");
        context.Unload();
    }
    private static void TestParsing()
    {
        string source = """""
            // Leading comment with a brace }
            component Strings {
              state string Value = "A";
              view {
                VStack(spacing: (1 + 2), padding: 8) {
                  Text($"{Value}: {string.Join(",", new[] { "}", "{" })}", id: "interpolated");
                  Text(@"verbatim ""quoted"" { } //", id: "verbatim");
                  Text("""raw " { } // text""", id: "raw");
                  Text($$"""raw {{Value}} and { braces }""", id: "raw-interpolated");
                }
              }
              code csharp {
                public string Format() {
                  /* } comment { */
                  string literal = "}";
                  return $"{literal} {Value}";
                }
              }
            }
            """"";
        var (_, compilation) = Generate(new File(@"C:\fixture\Strings.xui", source));
        Assert(!compilation.GetDiagnostics().Any(d => d.Severity == DiagnosticSeverity.Error),
            string.Join("\n", compilation.GetDiagnostics()));
    }
    private static Diagnostic Invalid(string source)
    {
        GeneratorDriver driver = CSharpGeneratorDriver.Create([new XuiGenerator().AsSourceGenerator()],
            [new File(@"C:\fixture\Bad.xui", source)]);
        driver.RunGeneratorsAndUpdateCompilation(Empty(), out var compilation, out var diagnostics);
        var diagnostic = diagnostics.FirstOrDefault(d => d.Id == "XUI001");
        Assert(diagnostic is not null, "Expected XUI001: " + string.Join("\n", diagnostics));
        Assert(diagnostic!.Location.GetLineSpan().Path.EndsWith("Bad.xui"), "Diagnostic maps to authoring path");
        Assert(compilation.GetDiagnostics().Any(d => d.Id == "CS1029"), "Every invalid source blocks the C# compilation pipeline");
        return diagnostic;
    }
    private static void TestUserNames()
    {
        var (_, compilation) = Generate(
            new File(@"C:\fixture\LowerState.xui",
                """component LowerState { state string value = "Hello"; view { VStack() { Text(value); } } }"""),
            new File(@"C:\fixture\LowerHandler.xui",
                """component LowerHandler { state string Entry = ""; view { VStack() { TextInput("Entry", text: Entry, change: value); } } code csharp { void value(string incoming) => Entry = incoming; } }"""),
            new File(@"C:\fixture\EscapedState.xui",
                """component EscapedState { state int @value = 0; view { VStack() { Text($"{value}"); } } }"""));
        var diagnostics = compilation.GetDiagnostics();
        Assert(!diagnostics.Any(d => d.Severity is DiagnosticSeverity.Warning or DiagnosticSeverity.Error),
            "Generated locals cannot shadow user state/handler names: " + string.Join("\n", diagnostics));
    }
    private static void TestDiagnostics()
    {
        var unsupported = Invalid("component Bad {\n view {\n VStack(speling: 8) {}\n }\n}");
        Assert(unsupported.Location.GetLineSpan().StartLinePosition.Line == 2, "Unknown property maps to actual line");
        Invalid("component Bad { view { VStack() { Unknown(\"x\"); } } }");
        Invalid("component Bad { view { VStack() { Text(\"x\", click: Go); } } }");
        Invalid("component Bad { view { VStack() { Text(\"x\", name: \"conflict\"); } } }");
        Invalid("component Bad { view { VStack() { Text(\"x\", id: ); } } }");
        Invalid("component Bad { view { Text(\"root\"); } }");
        Invalid("component Bad { state int X; view { VStack() {} } }");
        Invalid("component Bad { state int X=0; state int X=1; view { VStack() {} } }");
        Invalid("component Bad { view { VStack() { Text(Get()); } } code csharp { string Get() => \"x\"; } }");
        Invalid("component Bad { state int Count=0; view { VStack() { Text(Display<int>()); } } code csharp { string Display<T>() => Count.ToString(); } }");
        Invalid("component Bad { state int X=0; view { VStack() { Text($\"{X++}\"); } } }");
        Invalid("component Bad { view { VStack() {} } code csharp { int field=0; } }");
        Invalid("component Bad { view { VStack() {} } } component Second {}");
        Invalid("component @__xuiBad { view { VStack() {} } }");
        Invalid("component Bad { state int @Count=0; state int Count=1; view { VStack() {} } }");
        var (_, compilation) = Generate(new File(@"C:\fixture\TypeError.xui",
            "component Bad {\n view {\n VStack() {\n Text(42);\n }\n }\n}"));
        var typeError = compilation.GetDiagnostics().First(d => d.Id == "CS0029");
        var mapped = typeError.Location.GetMappedLineSpan();
        Assert(mapped.Path == @"C:\fixture\TypeError.xui" && mapped.StartLinePosition.Line == 3,
            "C# type errors map to actual XUI expression line: " + mapped);
        var (_, badCode) = Generate(new File(@"C:\fixture\CodeError.xui",
            "component Bad {\n view { VStack() {} }\n code csharp {\n void Go() => Missing();\n }\n}"));
        var methodError = badCode.GetDiagnostics().First(d => d.Id == "CS0103");
        Assert(methodError.Location.GetMappedLineSpan().StartLinePosition.Line == 3, "Embedded C# maps to actual XUI line");
        GeneratorDriver unreadable = CSharpGeneratorDriver.Create([new XuiGenerator().AsSourceGenerator()], [new UnreadableFile()]);
        unreadable.RunGeneratorsAndUpdateCompilation(Empty(), out var missingCompilation, out var missingDiagnostics);
        Assert(missingDiagnostics.Any(d => d.Id == "XUI001") && missingCompilation.GetDiagnostics().Any(d => d.Id == "CS1029"),
            "Unreadable input blocks compilation as well as reporting a generator error");
    }
    private static void TestIncremental()
    {
        var first = new File(@"C:\fixture\Counter.xui", Counter);
        var second = new File(@"C:\fixture\Second.xui", "component Second { view { VStack() { Text(\"Second\"); } } }");
        var (driver, _) = Generate(first, second);
        var initial = driver.GetRunResult().Results.Single().GeneratedSources;
        driver.AddAdditionalTexts([new File(@"C:\fixture\NewBroken.xui", "component NewBroken { view {")])
            .RunGeneratorsAndUpdateCompilation(Empty(), out var brokenAdditionalCompilation, out _);
        Assert(brokenAdditionalCompilation.GetDiagnostics().Any(d => d.Id == "CS1029"),
            "Malformed newly added unused component blocks otherwise-valid hot deltas");
        var (_, stateless) = Generate(second);
        Assert(!stateless.GetDiagnostics().Any(d => d.Severity is DiagnosticSeverity.Warning or DiagnosticSeverity.Error),
            "State-free component compiles without unused-field warnings");
        var badProperty = new File(first.Path, Counter.Replace("spacing: 8", "spacing: 8, unknownArgument: 1"));
        var invalidDriver = driver.ReplaceAdditionalText(first, badProperty).RunGenerators(Empty());
        Assert(invalidDriver.GetRunResult().Diagnostics.Any(d => d.Id == "XUI001"), "Recoverable invalid property blocks compilation");
        Assert(invalidDriver.GetRunResult().Results.Single().GeneratedSources.Length == initial.Length,
            "Recoverable invalid property does not delete generated component during watch");
        invalidDriver.RunGeneratorsAndUpdateCompilation(Empty(), out var invalidCompilation, out _);
        var blocking = invalidCompilation.GetDiagnostics().Single(d => d.Id == "CS1029");
        Assert(blocking.Location.GetMappedLineSpan().Path == first.Path, "Invalid property emits mapped C# error to block hot deltas");
        using (var stream = new MemoryStream())
            Assert(!invalidCompilation.Emit(stream).Success, "Invalid property cannot produce a successful assembly");
        var invalidBody = new File(first.Path, Counter.Replace("Count++;", "Count += ;"));
        var invalidBodyDriver = driver.ReplaceAdditionalText(first, invalidBody).RunGenerators(Empty());
        Assert(invalidBodyDriver.GetRunResult().Diagnostics.Any(d => d.Id == "XUI001"), "Recoverable C# syntax error diagnosed");
        Assert(invalidBodyDriver.GetRunResult().Results.Single().GeneratedSources.Length == initial.Length,
            "Recoverable C# error retains component declarations");
        var changed = new File(first.Path, Counter.Replace("spacing: 8", "spacing: 12"));
        driver = driver.ReplaceAdditionalText(first, changed).RunGenerators(Empty());
        var edited = driver.GetRunResult().Results.Single().GeneratedSources;
        Assert(initial.Select(s => s.HintName).SequenceEqual(edited.Select(s => s.HintName)), "Stable generated names");
        Assert(initial.Single(s => s.HintName.StartsWith(".Second")).SourceText.ToString() ==
            edited.Single(s => s.HintName.StartsWith(".Second")).SourceText.ToString(), "Unrelated file output unchanged");
        string Shape(GeneratorDriver value) => value.GetRunResult().Results.Single().GeneratedSources
            .Single(s => s.HintName.StartsWith("Demo.Counter")).SourceText.ToString().Split('\n')
            .Single(line => line.StartsWith("private string __xuiShape()"));
        string originalShape = Shape(driver);
        var idEdit = new File(first.Path, Counter.Replace("id: \"count\"", "id: \"count-renamed\""));
        var identityDriver = driver.ReplaceAdditionalText(changed, idEdit).RunGenerators(Empty());
        Assert(Shape(identityDriver) != originalShape, "Explicit id edit requires recreation");
        var labelEdit = new File(first.Path, Counter.Replace("Count: {Count}", "Value: {Count}"));
        var labelDriver = driver.ReplaceAdditionalText(changed, labelEdit).RunGenerators(Empty());
        Assert(Shape(labelDriver) == originalShape, "Label and spacing edits preserve topology signature");
        driver = driver.RemoveAdditionalTexts([second]).RunGenerators(Empty());
        Assert(driver.GetRunResult().Results.Single().GeneratedSources.Length == 2, "File deletion removes generated component");
        driver = driver.AddAdditionalTexts([second]).RunGenerators(Empty());
        Assert(driver.GetRunResult().Results.Single().GeneratedSources.Length == 3, "File addition generates component");
        var broken = new File(first.Path, "component Counter { view {");
        driver = driver.ReplaceAdditionalText(changed, broken).RunGenerators(Empty());
        Assert(driver.GetRunResult().Diagnostics.Any(d => d.Id == "XUI001"), "Malformed incremental edit diagnosed");
        driver = driver.ReplaceAdditionalText(broken, first).RunGenerators(Empty());
        Assert(driver.GetRunResult().Diagnostics.Length == 0 && driver.GetRunResult().Results.Single().GeneratedSources.Length == 3,
            "Malformed edit recovery restores component");
    }
}

public static class ExpressionProbe
{
    public static int ConstantReads;
    public static string Constant() { ConstantReads++; return "Constant"; }
}
