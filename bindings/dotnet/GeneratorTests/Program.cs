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
        TestSizeAndHelp();
        TestParsing();
        TestUserNames();
        TestDiagnostics();
        TestIncremental();
        TestComposition();
        TestCompositionDiagnostics();
        TestCompositionShape();
        TestStyling();
        TestStylingDiagnostics();
        TestStylingShape();
        Console.WriteLine($"XUI generator assertions: {count} passed.");
    }
    private static string StylingSource()
    {
        using var stream = typeof(Program).Assembly.GetManifestResourceStream("GeneratorTests.Fixtures.Styling.xui")!;
        using var reader = new StreamReader(stream);
        return reader.ReadToEnd();
    }
    private static void TestStyling()
    {
        var (_, compilation) = Generate(new File(@"C:\fixture\Styling.xui", StylingSource()));
        using var pe = new MemoryStream();
        var emitted = compilation.Emit(pe);
        Assert(emitted.Success, string.Join("\n", emitted.Diagnostics));
        pe.Position = 0;
        var context = new AssemblyLoadContext("styling-test", isCollectible: true);
        var type = context.LoadFromStream(pe).GetType("Demo.Styling")!;
        var window = new Xui.Window();
        var instance = Activator.CreateInstance(type, window, true)!;
        var button = (Xui.Button)type.GetProperty("DeleteButton")!.GetValue(instance)!;
        var other = (Xui.Button)type.GetProperty("OtherButton")!.GetValue(instance)!;
        var input = (Xui.TextInput)type.GetProperty("Input")!.GetValue(instance)!;
        var style = button.Style!;
        Assert(ReferenceEquals(style, other.Style), "Buttons share immutable named style definitions.");
        Assert(style.Values.Background == new Xui.ThemeColor(0xB42318, 0x8F1D16), "Theme colors retain both modes.");
        Assert(style.Values.Foreground == new Xui.ThemeColor(0xFFFFFF), "Uniform resources retain their color.");
        Assert(style.Values.BorderBrush == new Xui.ThemeColor(0x68110C, 0xFFA198), "Forward resource aliases resolve.");
        Assert(style.Values.CornerRadius == 0 && style.Values.Padding is null &&
            style.Values.BorderThickness == new Xui.Insets(3, 0, 0, 0), "Sparse style values preserve explicit zero and asymmetric edges.");
        Assert(style.BasedOn!.Values.Padding == new Xui.Insets(8) && style.BasedOn.Values.Background is null,
            "Forward derivation preserves sparse base definitions.");
        Assert(style.Rules.Select(rule => rule.State).SequenceEqual(Enum.GetValues<Xui.ButtonStyleState>()),
            "All five style states preserve declaration order.");
        Assert(style.Rules[2].Values.Background == new Xui.ThemeColor(0xD92D20, 0xB42318) &&
            style.Rules[2].Values.Foreground is null, "State overrides remain sparse and theme-aware.");
        Assert(other.StyleValues.Background == new Xui.ThemeColor(0) &&
            other.StyleValues.Padding == new Xui.Insets(0, 1, 2, 3) && other.StyleValues.Foreground is null,
            "Local properties preserve sparse values.");
        input.Edit("Retained input");
        var refresh = type.GetMethod("__xuiRefresh", BindingFlags.Instance | BindingFlags.NonPublic)!;
        refresh.Invoke(instance, null);
        Assert(ReferenceEquals(button.Style, style) && button.StyleSets == 1,
            "Unchanged refresh does not allocate or assign new named styles.");
        var second = Activator.CreateInstance(type, new Xui.Window(), true)!;
        Assert(ReferenceEquals(((Xui.Button)type.GetProperty("DeleteButton")!.GetValue(second)!).Style, style),
            "Style definitions are shared across component instances, not native control ownership.");
        // Emulate the stale revision that remains after a method-body update.
        type.GetField("__xuiStyleRevision", BindingFlags.Static | BindingFlags.NonPublic)!.SetValue(null, "old revision");
        refresh.Invoke(instance, null);
        Assert(!ReferenceEquals(button.Style, style) && button.StyleSets == 2 &&
            ReferenceEquals(button.Style, other.Style), "Changed definition revisions replace styles on existing controls.");
        Assert(input.Text == "Retained input" && (string)type.GetProperty("Entry")!.GetValue(instance)! == "Retained input" &&
            window.ContentSets == 1, "A style refresh preserves input, component state, and the existing tree.");
        context.Unload();
    }
    private static void TestStylingDiagnostics()
    {
        foreach (string declaration in new[]
        {
            "resources { A: resource(Missing); }",
            "resources { A: resource(B); B: resource(A); }",
            "resources { A: resource(A); }",
            "resources { A: 0; A: 1; }",
            "resources { A: 0; @A: 1; }",
            """resources { A: 0; \u0041: 1; }""",
            "resources { A: 0x1000000; }",
            "resources { A: -1; }",
            "resources { A: \"red\"; }",
            "resources { A: theme(light: 0, dark: 0x1000000); }",
            "resources { A: theme(dark: 0, light: 1); }",
            "resources { A: theme(0, 1); }",
            "resources { A: resource(\"A\"); }",
            "resources { A: resource(ref A); }",
            "style A for Text {}",
            "style A for Button basedOn Missing {}",
            "style A for Button basedOn B {} style B for Button basedOn A {}",
            "style A for Button {} style A for Button {}",
            "style A for Button { opacity: 0; }",
            "style A for Button { cornerRadius: 0; cornerRadius: 1; }",
            "style A for Button { when selected {} }",
            "style A for Button { when hovered { when pressed {} } }",
            "style A for Button { background: resource(Missing); }",
            "style A for Button { background: theme(light: 0, dark: -1); }",
            "style A for Button { cornerRadius: 32769; }",
            "style A for Button { cornerRadius: float.NaN; }",
            "style A for Button { cornerRadius: 1e100; }",
            "style A for Button { padding: (1, 2); }",
            "style A for Button { padding: (left: 1, 2, 3, 4); }",
            "style A for Button { borderThickness: (1, 2, 3, -1); }",
            "style A for Button { when disabled { padding: 32769; } }",
            "state int Radius = 1; style A for Button { cornerRadius: Radius; }",
            "state int A = 1; style A for Button {}"
        })
            Invalid("component Bad { " + declaration + " view { VStack() {} } }");
        foreach (string node in new[]
        {
            "Button(\"X\", style: Missing);",
            "Button(\"X\", style: new global::Xui.ButtonStyle(new()));",
            "Button(\"X\", background: resource(Missing));",
            "Button(\"X\", padding: -1);",
            "Text(\"X\", style: Missing);",
            "Toggle(\"X\", background: 0);"
        })
            Invalid("component Bad { view { VStack() { " + node + " } } }");
        string Resources(int n) => "resources { " + string.Join(" ", Enumerable.Range(0, n)
            .Select(i => $"R{i}: " + (i + 1 < n ? $"resource(R{i + 1})" : "0") + ";")) + " }";
        string Styles(int n) => string.Join(" ", Enumerable.Range(0, n)
            .Select(i => $"style S{i} for Button" + (i + 1 < n ? $" basedOn S{i + 1}" : "") + " {}"));
        string Rules(int n) => "style Rules for Button { " + string.Concat(Enumerable.Repeat("when hovered { padding: 0; } ", n)) + " }";
        foreach (string limit in new[] { Resources(257), Styles(17), Rules(257) })
            Invalid("component Bad { " + limit + " view { VStack() {} } }");
        var (_, boundary) = Generate(new File(@"C:\fixture\Limits.xui",
            "component Limits { " + Resources(256) + Styles(16) + Rules(256) +
            " view { VStack() { Button(\"X\", style: S0, padding: 32768, cornerRadius: 0.5, foreground: 0xFFFFFF); } } }"));
        Assert(!boundary.GetDiagnostics().Any(d => d.Severity is DiagnosticSeverity.Error or DiagnosticSeverity.Warning),
            "Maximum resources, inheritance, rules, colors, and dimensions compile without warnings.");
        var (_, escaped) = Generate(new File(@"C:\fixture\EscapedStyles.xui", """
            component EscapedStyles {
                resources { \u0041: 0; @default: resource(A); }
                style \u0042 for Button { background: resource(@default); }
                view { VStack() { Button("Escaped", style: B); } }
            }
            """));
        Assert(!escaped.GetDiagnostics().Any(d => d.Severity is DiagnosticSeverity.Error or DiagnosticSeverity.Warning),
            "Escaped identifiers use canonical resource and style names.");
        var mapped = Invalid("component Bad {\n resources {\n Broken: resource(Missing);\n }\n view { VStack() {} }\n}");
        Assert(mapped.Location.GetLineSpan().StartLinePosition.Line == 2, "Resource errors map to the declaration value.");
        Invalid("component Bad { resources { A: 0;");
        Invalid("component Bad { style A for Button { when hovered { background:");
    }
    private static void TestStylingShape()
    {
        string Generated(string source)
        {
            var (driver, _) = Generate(new File(@"C:\fixture\Styling.xui", source));
            return driver.GetRunResult().Results.Single().GeneratedSources.Single(s => s.HintName.StartsWith("Demo.Styling")).SourceText.ToString();
        }
        string Shape(string generated) => generated.Split('\n').Single(line => line.StartsWith("private string __xuiShape()"));
        string Revision(string generated) => generated.Split('\n').Single(line => line.StartsWith("const string __xuiRevision"));
        string original = StylingSource();
        string generated = Generated(original);
        foreach (string edit in new[]
        {
            original.Replace("0xB42318", "0xF04438"),
            original.Replace("when hovered { background: resource(DangerHover); }", "when pressed { foreground: 0; }"),
            original.Replace("basedOn BaseButton", ""),
            original.Replace("style: DangerButton", "style: BaseButton"),
            original.Replace("background: 0,", "background: 1,"),
            original.Replace("DangerEdge: resource(Edge)", "DangerEdge: resource(DangerFill)")
        })
            Assert(Shape(Generated(edit)) == Shape(generated), "Style value, state, derivation, reference, and local edits refresh in place.");
        Assert(Revision(Generated(original.Replace("0xB42318", "0xF04438"))) != Revision(generated),
            "Resource edits change the method-body cache revision, not only a static initializer.");
        Assert(Revision(Generated(original.Replace("basedOn BaseButton", ""))) != Revision(generated),
            "Derivation edits invalidate the shared definitions.");
        Assert(Shape(Generated(original.Replace("BaseButton", "Foundation"))) != Shape(generated),
            "Style declaration identity changes require replacement.");
        Assert(Shape(Generated(original.Replace("OnDangerFill", "OnDanger"))) != Shape(generated),
            "Resource declaration identity changes require replacement.");
        Assert(Shape(Generated(original.Replace(", background: 0", ""))) != Shape(generated),
            "Local property removal cannot leave an old override on a retained control.");
        Assert(Shape(Generated(original.Replace(", style: DangerButton", ""))) != Shape(generated),
            "Style binding removal requires replacement.");
    }
    private static string CompositionSource()
    {
        using var stream = typeof(Program).Assembly.GetManifestResourceStream("GeneratorTests.Fixtures.Composition.xui")!;
        using var reader = new StreamReader(stream);
        return reader.ReadToEnd();
    }
    private static void TestComposition()
    {
        var (_, compilation) = Generate(
            new File(@"C:\fixture\Composition.xui", CompositionSource()),
            new File(@"C:\fixture\Reusable.xui", """
                component Reusable { param string Title; view { Grid(Title, ref: Layout) { Text(Title); } } }
                """),
            new File(@"C:\fixture\Mount.xui", """
                component Mount { param global::Xui.Element Body; view { VStack() { Content(Body, flex: 1); } } }
                """),
            new File(@"C:\fixture\Duplicate.xui", """
                component Duplicate { param global::Xui.Element Body; view { VStack() { Content(Body); Content(Body); } } }
                """));
        using var pe = new MemoryStream();
        var emitted = compilation.Emit(pe);
        Assert(emitted.Success, string.Join("\n", emitted.Diagnostics));
        pe.Position = 0;
        var context = new AssemblyLoadContext("composition-test", isCollectible: true);
        var assembly = context.LoadFromStream(pe);
        var type = assembly.GetType("Demo.Composition")!;
        var parameters = type.GetConstructors().Single().GetParameters();
        Assert(parameters.Select(p => p.Name).SequenceEqual(["window", "Body", "Title", "Column", "attach"]),
            "Constructor parameters preserve declaration order and append attach.");
        Assert(parameters[^1].HasDefaultValue && Equals(parameters[^1].DefaultValue, true), "Attach defaults to true.");
        Assert(type.GetProperty("Body")!.SetMethod is null && type.GetProperty("Title")!.SetMethod is null,
            "Parameters are immutable properties.");
        var window = new Xui.Window();
        var body = window.Label("External content");
        var component = Activator.CreateInstance(type, window, body, "Explorer", 0, false)!;
        T Ref<T>(string name) => (T)type.GetProperty(name)!.GetValue(component)!;
        var root = Ref<Xui.Stack>("Root");
        var grid = Ref<Xui.Grid>("Layout");
        var details = Ref<Xui.DataGrid>("Details");
        Assert(ReferenceEquals(root, Ref<Xui.Stack>("Panel")) && window.ContentSets == 0, "Unmounted roots and typed references preserve identity.");
        Assert(root.Children[0] == (grid, 1f) && root.Preferred == (640, 480), "Stack flex and preferred size use native APIs.");
        Assert(grid.TrackSets == 1 && grid.Columns[0].Value == 120 && grid.Children[2].ColumnSpan == 2,
            "Tracks are installed before Grid children and spans reach Add.");
        Assert(ReferenceEquals(Ref<Xui.Element>("Embedded"), body) &&
            ReferenceEquals(Ref<Xui.ScrollView>("Scroller").Children.Single(), body), "Content reuses an existing element without a factory.");
        Assert(Ref<Xui.SplitView>("Panes").Children.Count == 2 && Ref<Xui.Popup>("Flyout").Children.Single() is Xui.Stack,
            "Children-taking factories build complete nested compositions.");
        Assert(Ref<Xui.Button>("RefreshButton").Icon == Xui.ButtonIcon.Refresh &&
            Ref<Xui.Popup>("Flyout").Placement == Xui.PopupPlacement.Right && Ref<Xui.Popup>("Flyout").WindowBackground,
            "Button and Popup options use native setters.");
        var input = Ref<Xui.TextInput>("Search");
        Assert(!input.CaptionVisible && input.Placeholder == "Explorer" && !Ref<Xui.NavigationView>("Navigation").HeaderVisible,
            "Input caption, placeholder, and navigation header options.");
        Assert(Ref<Xui.NavigationView>("Navigation").Search.AutomationId == "layout.search" &&
            Ref<Xui.NavigationView>("Navigation").Search.HelpText == "Explorer", "Navigation search options target the native child input.");
        input.Edit("user search");
        Ref<Xui.Toggle>("Filter").Invoke(true);
        details.Columns[0] = new("Name", 999);
        type.GetMethod("__xuiRefresh", BindingFlags.Instance | BindingFlags.NonPublic)!.Invoke(component, null);
        Assert(input.Text == "user search" && Ref<Xui.Toggle>("Filter").Checked, "Unauthored input state is not reset by refresh.");
        Assert(details.ColumnSets == 1 && details.Columns[0].Width == 999 && grid.TrackSets == 1,
            "Equal array expressions preserve user column widths and do not reset tracks.");
        type.GetProperty("Width")!.SetValue(component, 180f);
        Assert(grid.TrackSets == 2 && grid.Columns[0].Value == 180 && details.ColumnSets == 2 && details.Columns[0].Width == 180,
            "State dependencies update tracks and columns.");
        type.GetProperty("Identifier")!.SetValue(component, "renamed");
        Assert(details.AutomationId == "renamed" && grid.TrackSets == 2, "State-driven IDs update without rebuilding the layout.");
        Assert(Ref<Xui.NavigationView>("Navigation").Search.AutomationId == "renamed.search", "Navigation search options track state dependencies.");
        type.GetProperty("Shown")!.SetValue(component, false);
        Assert(!details.IsVisible && !details.Enabled && !Ref<Xui.SplitView>("Panes").SecondVisible, "Common and pane visibility bind to state.");
        window.SetContent(root);
        Assert(window.ContentSets == 1, "Caller can attach an unmounted Stack once.");

        var reusable = assembly.GetType("Reusable")!;
        var otherWindow = new Xui.Window();
        bool Reject(Type target, params object[] args)
        {
            try { Activator.CreateInstance(target, args); return false; }
            catch (TargetInvocationException error) { return error.InnerException is ArgumentException; }
        }
        Assert(Reject(reusable, otherWindow, "Default root", true) && otherWindow.Elements.Count == 0,
            "Non-Stack attach rejects clearly before allocating controls.");
        var child = Activator.CreateInstance(reusable, otherWindow, "Reusable", false)!;
        var childRoot = (Xui.Grid)reusable.GetProperty("Root")!.GetValue(child)!;
        Assert(childRoot.Rows.Single() == new Xui.GridTrack(Xui.TrackSizing.Star, 1) &&
            childRoot.Columns.Single() == new Xui.GridTrack(Xui.TrackSizing.Star, 1), "Omitted Grid tracks default to one star track.");
        var mount = assembly.GetType("Mount")!;
        Activator.CreateInstance(mount, otherWindow, childRoot, true);
        Assert(otherWindow.ContentSets == 1 && otherWindow.Content!.Children.Single().Child == childRoot,
            "A reusable non-Stack component embeds through Content.");
        Assert(Reject(mount, new Xui.Window(), childRoot, false), "Content placement preserves native cross-window ownership checks.");
        var duplicateWindow = new Xui.Window();
        Assert(Reject(assembly.GetType("Duplicate")!, duplicateWindow, duplicateWindow.Label("Only once"), false),
            "A content element cannot occupy two parent slots.");
        context.Unload();
    }
    private static void TestCompositionDiagnostics()
    {
        foreach (string source in new[]
        {
            "param int Value = 1; view { VStack() {} }",
            "param int A; param int A; view { VStack() {} }",
            "param int A; state int A = 0; view { VStack() {} }",
            "param int Root; view { VStack() {} }",
            "param int window; view { VStack() {} }",
            "param int @attach; view { VStack() {} }",
            "param int __xuiInput; view { VStack() {} }",
            "param int A; view { VStack(ref: A) {} }",
            "view { VStack(ref: Same) { Text(\"X\", ref: Same); } }",
            "view { VStack(ref: Root) {} }",
            "view { VStack(ref: Bad) {} }",
            "view { VStack(ref: Go) {} } code csharp { void Go() {} }",
            "view { VStack(ref: __xuiRef) {} }",
            "view { VStack(ref: \"NotAnIdentifier\") {} }",
            "view { VStack() { Text(\"X\", row: 1); } }",
            "view { Grid(\"Grid\", flex: 1) {} }",
            "view { Grid(\"Grid\") { Text(\"X\", flex: 1); } }",
            "view { Grid(\"Grid\") { Text(\"X\", row: -1); } }",
            "view { Grid(\"Grid\") { Text(\"X\", rowSpan: 0); } }",
            "state int Row = 0; view { Grid(\"Grid\") { Text(\"X\", row: Row); } }",
            "state float Flex = 1; view { VStack() { Text(\"X\", flex: Flex); } }",
            "state global::Xui.Element Body = null!; view { VStack() { Content(Body); } }",
            "state string Name = \"Grid\"; view { Grid(Name) {} }",
            "view { Grid(\"Grid\", id: \"unsupported\") {} }",
            "view { ScrollView(\"X\") {} }",
            "view { Popup(\"X\") { Text(\"1\"); Text(\"2\"); } }",
            "view { SplitView(\"X\") { Text(\"1\"); } }",
            "view { Grid() {} }",
            "view { Grid(value: \"Grid\") {} }",
            "view { Content(); }",
            "view { VStack(id: \"unsupported\") {} }",
            "view { VStack() { Content(null!, visible: true); } }"
        })
            Invalid("component Bad { " + source + " }");
        var (_, badTypes) = Generate(new File(@"C:\fixture\Types.xui", """
            component Types {
                param int Immutable;
                view { Grid("Grid") { Text("X", row: 1.5); } }
                code csharp { void Change() => Immutable = 1; }
            }
            """));
        Assert(badTypes.GetDiagnostics().Any(d => d.Id == "CS0266"), "Grid placement requires integers, not floating-point coercion.");
        Assert(badTypes.GetDiagnostics().Any(d => d.Id == "CS0200"), "Code methods cannot assign constructor parameters.");
    }
    private static void TestCompositionShape()
    {
        string Shape(string source)
        {
            var (driver, _) = Generate(new File(@"C:\fixture\Composition.xui", source));
            return driver.GetRunResult().Results.Single().GeneratedSources.Single(s => s.HintName.StartsWith("Demo.Composition"))
                .SourceText.ToString().Split('\n').Single(line => line.StartsWith("private string __xuiShape()"));
        }
        string source = CompositionSource();
        string original = Shape(source);
        Assert(Shape(source.Replace("ref: Details", "ref: Table")) != original, "Ref changes require replacement.");
        Assert(Shape(source.Replace("Identifier + \".search\"", "Identifier + \".navigation\"")) != original,
            "Navigation search identity edits require replacement.");
        Assert(Shape(source.Replace("param int Column;", "param long Column;")) != original, "Parameter types belong to shape.");
        Assert(Shape(source.Replace("row: 0, column: Column", "row: 1, column: Column")) != original, "Grid placement belongs to shape.");
        Assert(Shape(source.Replace("flex: 1", "flex: 2")) != original, "Stack flex belongs to shape.");
        Assert(Shape(source.Replace("Content(Body,", "Content((Body),")) != original, "Content identity expressions belong to shape.");
        Assert(Shape(source.Replace("headerVisible: false", "headerVisible: true")) == original, "Authored property values refresh in place.");
        Assert(Shape(source.Replace(", headerVisible: false", "")) != original, "Optional binding removal replaces controls.");
        Assert(Shape(source.Replace("Fixed, Width", "Fixed, Width + 10")) == original, "Track expressions refresh without topology replacement.");
    }
    private static void TestSizeAndHelp()
    {
        const string source = """
            component Sized {
              state float Side = 36;
              view { VStack() { Button("Cell", size: (Side, 36), help: $"Width: {Side}", id: "cell"); } }
            }
            """;
        var (_, compilation) = Generate(new File(@"C:\fixture\Sized.xui", source));
        using var pe = new MemoryStream();
        var result = compilation.Emit(pe);
        Assert(result.Success, string.Join("\n", result.Diagnostics));
        pe.Position = 0;
        var context = new AssemblyLoadContext("size-test", isCollectible: true);
        var type = context.LoadFromStream(pe).GetType("Sized")!;
        var window = new Xui.Window();
        var component = Activator.CreateInstance(type, window, true)!;
        var cell = window.Elements.OfType<Xui.Button>().Single();
        Assert(cell.Size == (36, 36) && cell.HelpText == "Width: 36", "Tuple size and accessible help use typed native setters.");
        type.GetProperty("Side")!.SetValue(component, 48f);
        Assert(cell.Size == (48, 36) && cell.HelpText == "Width: 48", "Size and help depend on explicit state.");
        type.GetProperty("Side")!.SetValue(component, 48f);
        Assert(cell.SizeSets == 2, "Unchanged size does not call the setter.");
        context.Unload();
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
        var counter = Activator.CreateInstance(type, window, true)!;
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
        var options = CSharpParseOptions.Default.WithPreprocessorSymbols("XUI_HOT_RELOAD");
        var input = Empty().AddSyntaxTrees(CSharpSyntaxTree.ParseText("""
            namespace Xui.Development {
                public static class ReloadHost {
                    public static void Register(Xui.Window window, System.Func<bool> refresh) {}
                    public static void UpdateApplication(System.Type[]? types) {}
                }
            }
            """, options));
        GeneratorDriver driver = CSharpGeneratorDriver.Create([new XuiGenerator().AsSourceGenerator()],
            [new File(@"C:\fixture\LegalComponent.xui", """component XuiGeneratedMetadataHandler { view { VStack() { Text("Legal name"); } } }""")],
            parseOptions: options);
        driver.RunGeneratorsAndUpdateCompilation(input, out var debugCompilation, out var generatorDiagnostics);
        Assert(!generatorDiagnostics.Any(d => d.Severity == DiagnosticSeverity.Error) &&
            !debugCompilation.GetDiagnostics().Any(d => d.Severity is DiagnosticSeverity.Warning or DiagnosticSeverity.Error),
            "Debug infrastructure names must use only the reserved __xui prefix");
    }
    private static void TestDiagnostics()
    {
        var unsupported = Invalid("component Bad {\n view {\n VStack(speling: 8) {}\n }\n}");
        Assert(unsupported.Location.GetLineSpan().StartLinePosition.Line == 2, "Unknown property maps to actual line");
        Invalid("component Bad { view { VStack() { Unknown(\"x\"); } } }");
        Invalid("component Bad { view { VStack() { Text(\"x\", click: Go); } } }");
        Invalid("component Bad { view { VStack() { Text(\"x\", name: \"conflict\"); } } }");
        Invalid("component Bad { view { VStack() { Text(\"x\", id: ); } } }");
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
        var sized = new File(first.Path, Counter.Replace("id: \"increment\"", "id: \"increment\", size: (36, 36), help: \"Help\""));
        var sizedDriver = driver.ReplaceAdditionalText(changed, sized).RunGenerators(Empty());
        Assert(Shape(sizedDriver) != originalShape, "Optional binding addition/removal recreates controls instead of retaining stale properties");
        var resized = new File(first.Path, sized.GetText().ToString().Replace("(36, 36)", "(48, 48)").Replace("\"Help\"", "\"New help\""));
        Assert(Shape(sizedDriver.ReplaceAdditionalText(sized, resized).RunGenerators(Empty())) == Shape(sizedDriver),
            "Size and help value edits preserve topology");
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
