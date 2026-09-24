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
        foreach (string node in new[] { "ToggleSwitch(\"No\")", "ProgressRing(\"No\")", "SwapChainPanel(\"No\")" })
        {
            string syntax = node.EndsWith('}') ? node : node + ";";
            Invalid($"component Invalid {{ view {{ VStack() {{ {syntax} }} }} }}", "Portable profile does not support");
        }
        foreach (string property in new[] { "icon: default", "background: default", "padding: 8", "style: Missing" })
            Invalid($"component Invalid {{ view {{ VStack() {{ Button(\"No\", {property}); }} }} }}", "Portable profile does not support");
        Invalid("component Invalid { view { Text(\"No\"); } }", "requires a VStack or HStack root");
        Invalid("component Invalid { state float Weight = 1; view { VStack() { Button(\"No\", flex: Weight); } } }", "cannot depend on state");
        Invalid("component Invalid { view { VStack() { ScrollView(\"S\") { Text(\"No\", flex: 1); } } } }", "requires a Stack parent");
        Invalid("component Invalid { view { VStack() { Button(\"No\", ref: Root); } } }", "reserved or duplicated");
        Invalid("component Invalid { state int Lifetime = 0; view { VStack() {} } }", "reserved or duplicated");
        Invalid("component Invalid { resources { Accent: 0x0078d4; } view { VStack() {} } }", "does not support resources");
        Invalid("component Invalid { style Accent for Button {} view { VStack() {} } }", "does not support styles");
        Invalid("component Invalid { state int Count = 0; view { VStack() { Text((Count++).ToString()); } } }", "side-effect-free");
        Valid("component Reuse { param global::Xui.Experimental.Portable.Element Body; view { VStack() { Content(Body); } } }");
        Valid("component Keyed { state global::Xui.Experimental.Portable.KeyedItem[] Items = []; view { VStack() { KeyedVStack(Items, spacing: 8, padding: 2, ref: Rows, flex: 1); } } }");
        Valid("component Keyed { param global::Xui.Experimental.Portable.KeyedItem[] Items; view { HStack() { KeyedHStack(Items); } } }");
        Invalid("component Invalid { state global::Xui.Experimental.Portable.Element Body = null!; view { VStack() { Content(Body); } } }", "cannot depend on state");
        Invalid("component Invalid { view { VStack() { KeyedVStack([]); } } }", "must directly name");
        Invalid("component Invalid { state global::Xui.Experimental.Portable.KeyedItem[] Items = []; view { VStack() { KeyedVStack(Items); KeyedHStack(Items); } } }", "only one container");
        var legacyKeyed = Run("component Invalid { view { VStack() { KeyedVStack([]); } } }", "Windows");
        Assert(legacyKeyed.Diagnostics.Any(d => d.Id == "XUI001" && d.GetMessage().Contains("requires the Portable profile")), "Legacy Windows rejects portable keyed composition.");
        Valid("""
            component Values {
                state bool Active = true;
                state global::Xui.Experimental.Portable.CheckState Choice = global::Xui.Experimental.Portable.CheckState.Indeterminate;
                state double Done = 12.5;
                view {
                    VStack() {
                        Toggle("Binary", checked: Active, change: Change, ref: Binary);
                        CheckBox("Mixed", checkState: Choice, threeState: true, change: Check, ref: Mixed);
                        Progress("Work", range: new global::Xui.Experimental.Portable.NumericRange(-10, 50),
                            currentValue: Done, progressState: global::Xui.Experimental.Portable.ProgressState.Determinate, ref: Work);
                    }
                }
                code csharp {
                    void Change(bool value) => Active = value;
                    void Check(global::Xui.Experimental.Portable.CheckState value) => Choice = value;
                }
            }
            """);
        Invalid("component Invalid { view { VStack() { Progress(\"No\", orientation: default); } } }", "does not support");
        Invalid("component Invalid { state double Max = 100; view { VStack() { Progress(\"No\", range: new global::Xui.Experimental.Portable.NumericRange(0, Max)); } } }", "cannot depend on component state");
        var invalidMode = Run("component Invalid { view { VStack() { Progress(\"No\", progressState: global::Xui.Experimental.Portable.ProgressState.Paused); } } }");
        Assert(invalidMode.Compilation.GetDiagnostics().Any(d => d.Severity == DiagnosticSeverity.Error &&
            d.GetMessage().Contains("Paused") && d.Location.GetMappedLineSpan().Path == path), "Unsupported portable progress modes produce source-mapped C# diagnostics.");
        Valid("""
            component Axes {
                state global::Xui.Experimental.Portable.AxisConstraints? Width = 280;
                state global::Xui.Experimental.Portable.AxisConstraints? Height = global::Xui.Experimental.Portable.AxisConstraints.Auto;
                view {
                    VStack(width: new global::Xui.Experimental.Portable.AxisConstraints(Minimum: 10, Maximum: 500)) {
                        TextInput("Both", size: (180, 90), width: Width, height: Height);
                        TextInput("Width only", width: Width);
                        TextInput("Height only", height: Height);
                    }
                }
            }
            """);
        Valid("component ContentAxes { param global::Xui.Experimental.Portable.Element Body; view { VStack() { Content(Body, width: 240, height: global::Xui.Experimental.Portable.AxisConstraints.Auto); } } }");
        var legacyAxes = Run("component Axes { view { VStack() { TextInput(\"No\", width: 280); } } }", "Windows");
        Assert(legacyAxes.Diagnostics.Any(d => d.Id == "XUI001" && d.GetMessage().Contains("Per-axis constraints require the Portable profile")), "Legacy profile axis arguments reject explicitly, not with missing native types.");
        Valid("""
            component GridPage {
                state global::Xui.Experimental.Portable.GridTrack[] Rows = [new(global::Xui.Experimental.Portable.TrackSizing.Automatic, 1)];
                view {
                    VStack() {
                        Grid("Diagnostic layout name", rows: Rows,
                            columns: [new(global::Xui.Experimental.Portable.TrackSizing.Fixed, 20), new(global::Xui.Experimental.Portable.TrackSizing.Star, 1)],
                            ref: Layout, flex: 1) {
                            Text("Spanning", row: 0, column: 0, columnSpan: 2);
                        }
                    }
                }
            }
            """);
        Invalid("component Bad { view { VStack() { Text(\"No\", row: 0); } } }", "requires a Grid parent");
        Invalid("component Bad { state int Row = 0; view { VStack() { Grid(\"G\") { Text(\"No\", row: Row); } } } }", "cannot depend on state");
        Invalid("component Bad { view { VStack() { Grid(\"G\") { Text(\"No\", rowSpan: 0); } } } }", "must be positive");
        Invalid("component Bad { view { VStack() { Grid(\"G\", padding: 4) {} } } }", "does not support");
        Invalid("component Bad { view { Grid(\"G\") {} } }", "requires a VStack or HStack root");
        Valid("""
            component Presentation {
                state float Size = 18;
                state uint Weight = 700;
                view { VStack() {
                    Text("Title", textRole: global::Xui.Experimental.Portable.TextRole.Title);
                    TextInput("Editor", fontSize: Size, fontWeight: Weight);
                    Button("Action", fontWeight: 700);
                } }
            }
            """);
        Invalid("component Bad { view { VStack() { Progress(\"No\", fontSize: 18); } } }", "does not support");
        Invalid("component Bad { view { VStack() { Text(\"No\", fontFamily: \"Custom\"); } } }", "does not support");
        Valid("""
            component Forms {
                state string Email = "";
                state string Number = "";
                state string Notes = "";
                view { VStack() {
                    TextInput("Email", text: Email, purpose: global::Xui.Experimental.Portable.InputPurpose.Email);
                    TextInput("Number", text: Number, purpose: global::Xui.Experimental.Portable.InputPurpose.Number);
                    MultilineText("Notes", text: Notes, change: Edit, readOnly: false, maximumLength: 64);
                    PasswordInput("Secret", change: Changed, maximumLength: 32);
                } }
                code csharp {
                    void Edit(string value) => Notes = value;
                    void Changed() { }
                }
            }
            """);
        Invalid("component Bad { state global::Xui.Experimental.Portable.InputPurpose Purpose = default; view { VStack() { TextInput(\"No\", purpose: Purpose); } } }", "constructor-time");
        Invalid("component Bad { state int Limit = 32; view { VStack() { MultilineText(\"No\", maximumLength: Limit); } } }", "constructor-time");
        Invalid("component Bad { view { VStack() { PasswordInput(\"No\", text: \"secret\"); } } }", "does not support");
        Invalid("component Bad { view { VStack() { MultilineText(\"No\", submit: Submit); } } code csharp { void Submit() {} } }", "does not support");
        var legacyForms = Run("component Bad { view { VStack() { MultilineText(\"No\"); } } }", "Windows");
        Assert(legacyForms.Diagnostics.Any(d => d.Id == "XUI001" && d.GetMessage().Contains("require the Portable profile")), "New editor authoring cannot silently change the default Windows profile.");
        Valid("""
            component ChoiceRange {
                state (global::Xui.Experimental.Portable.Choice[] Items, ulong? Selected) Options =
                    ([new(1, "One"), new(9007199254740993UL, "Large ID")], 1UL);
                state double Amount = 2.25;
                view {
                    VStack() {
                        SingleChoice("Priority", items: Options.Items, selected: Options.Selected, change: Choose);
                        RangeInput("Adjustment", range: new global::Xui.Experimental.Portable.NumericRange(0, 10, 0.5, 2),
                            currentValue: Amount, change: Commit, preview: Preview, cancel: Cancel);
                    }
                }
                code csharp {
                    void Choose(ulong value) => Options = (Options.Items, value);
                    void Commit(double value) => Amount = value;
                    void Preview(double value) { }
                    void Cancel(double value) { }
                }
            }
            """);
        Invalid("component Bad { view { VStack() { SingleChoice(\"No\", selected: 1UL); } } }", "requires an items snapshot");
        Invalid("component Bad { view { VStack() { RangeInput(\"No\", orientation: global::Xui.Experimental.Portable.Axis.Vertical); } } }", "does not support");
        Invalid("component Bad { view { VStack() { RangeInput(\"No\", reversed: true); } } }", "does not support");
        Invalid("component Bad { view { VStack() { SingleChoice(\"No\", fontSize: 18); } } }", "does not support");
        var legacyChoice = Run("component Bad { view { VStack() { SingleChoice(\"No\"); } } }", "Windows");
        Assert(legacyChoice.Diagnostics.Any(d => d.Id == "XUI001" && d.GetMessage().Contains("require the Portable profile")), "SingleChoice remains explicitly portable-only.");
        var legacyRangePreview = Run("component Bad { view { VStack() { RangeInput(\"No\", preview: OnPreview); } } code csharp { void OnPreview(double value) {} } }", "Windows");
        Assert(legacyRangePreview.Diagnostics.Any(d => d.Id == "XUI001" && d.GetMessage().Contains("require the Portable profile")), "Existing Windows range generation does not silently acquire new preview/cancel syntax.");
        Valid("""
            component Workspace {
                state (global::Xui.Experimental.Portable.PageItem[] Items, ulong? Selected) PagesState = ([], null);
                state bool PaneVisible = true;
                view {
                    VStack() {
                        NavigationView("Workspace", pages: Pages, change: Selected, activate: Activated, expanded: true);
                        TabStrip("Documents", pages: Pages, change: Selected, activate: Activated, close: Close, closable: true);
                        PageView("Retained documents", pages: PagesState.Items, selected: PagesState.Selected,
                            visible: PaneVisible, ref: Pages, flex: 1);
                    }
                }
                code csharp {
                    void Selected(ulong id) => PagesState = (PagesState.Items, id);
                    void Activated(ulong id) { }
                    void Close(ulong id) { }
                }
            }
            """);
        Invalid("component Bad { view { VStack() { TabStrip(\"No\"); } } }", "requires its pages argument");
        Invalid("component Bad { view { VStack() { NavigationView(\"No\", pages: Missing); } } }", "link a PageView ref");
        Invalid("component Bad { view { VStack() { PageView(\"No\", ref: Pages); } } }", "requires its pages argument");
        Invalid("component Bad { view { VStack() { PageView(\"No\", pages: [], enabled: false); } } }", "does not support");
        Invalid("component Bad { state global::Xui.Experimental.Portable.PageItem[] Items = []; state ulong? Selected = null; view { VStack() { PageView(\"No\", pages: Items, selected: Selected); } } }", "one shared state snapshot");
        Invalid("component Bad { state (global::Xui.Experimental.Portable.PageItem[] Items, ulong? Selected) Snapshot = ([], null); view { VStack() { PageView(\"No\", pages: Snapshot.Items, selected: Snapshot.Selected, visible: Snapshot.Selected != null); } } }", "visibility requires a state independent");
        Invalid("component Bad { state (global::Xui.Experimental.Portable.PageItem[] Items, ulong? Selected) Snapshot = ([], null); view { VStack() { PageView(\"First\", pages: Snapshot.Items, selected: Snapshot.Selected); PageView(\"Second\", pages: Snapshot.Items, selected: Snapshot.Selected); } } }", "only one keyed or page container");
        var legacyPages = Run("component Bad { view { VStack() { PageView(\"No\", pages: []); } } }", "Windows");
        Assert(legacyPages.Diagnostics.Any(d => d.Id == "XUI001" && d.GetMessage().Contains("Linked page navigation requires")), "Existing Windows default generation never substitutes its non-retaining legacy PageView.");
        Valid("""
            component TextLayout {
                state global::Xui.Experimental.Portable.LabelTextLayout? Layout = global::Xui.Experimental.Portable.LabelTextLayout.Wrap(2);
                state string Caption = "Line one\nLine two\nLine three";
                view { VStack() {
                    Text(Caption, textLayout: Layout, ref: Paragraph);
                    Text("Headline", textLayout: global::Xui.Experimental.Portable.LabelTextLayout.SingleLine(global::Xui.Experimental.Portable.TextOverflow.CharacterEllipsis));
                } }
            }
            """);
        Invalid("component Bad { view { VStack() { Button(\"No\", textLayout: null); } } }", "does not support");
        Invalid("component Bad { view { VStack() { TextInput(\"No\", textLayout: null); } } }", "does not support");
        var legacyTextLayout = Run("component Bad { view { VStack() { Text(\"No\", textLayout: null); } } }", "Windows");
        Assert(legacyTextLayout.Diagnostics.Any(d => d.Id == "XUI001" && d.GetMessage().Contains("Explicit label text layout requires")), "Legacy text generation does not silently acquire portable wrapping semantics.");
        Valid("""
            component PackagedImage {
                state global::Xui.Experimental.Portable.PackagedImageSource? Source = null;
                state global::Xui.Experimental.Portable.ImageDecodeOptions Options = new(64, 64);
                view { VStack() {
                    Image("Accessible description", source: Source, decodeOptions: Options, stateChanged: Changed,
                        ref: Preview, preferredSize: (192, 144));
                } }
                code csharp {
                    void Changed(global::Xui.Experimental.Portable.ImageLoadState value) { }
                }
            }
            """);
        Invalid("component Bad { view { VStack() { Image(\"No\", fit: 0); } } }", "does not support");
        Invalid("component Bad { view { VStack() { Image(\"No\", fontSize: 18); } } }", "does not support");
        var legacyImage = Run("component Bad { view { VStack() { Image(\"No\"); } } }", "Windows");
        Assert(legacyImage.Diagnostics.Any(d => d.Id == "XUI001" && d.GetMessage().Contains("Packaged Image declarations require")), "Existing Windows path-image behavior is not relabeled as portable packaged decoding.");
        Valid("""
            component RevealPanel {
                state (bool Open, global::Xui.Experimental.Portable.RevealMotion Motion) Panel =
                    (true, new(200, global::Xui.Experimental.Portable.RevealDirection.Right));
                view { VStack() {
                    Text(Panel.Open ? "Open" : "Closed");
                    Reveal("Retained details", open: Panel.Open, motion: Panel.Motion, ref: Details) {
                        TextInput("Notes");
                    }
                } }
            }
            """);
        Valid("""
            component IndependentReveal {
                state bool Open = true;
                state global::Xui.Experimental.Portable.RevealMotion Motion = new();
                view { VStack() {
                    Reveal("One transactional pair", open: Open, motion: Motion) { Text("Content"); }
                } }
            }
            """);
        foreach (string property in new[] { "visible: false", "enabled: true", "id: \"no\"", "duration: 100", "layout: default", "direction: default" })
            Invalid($"component Bad {{ view {{ VStack() {{ Reveal(\"No\", {property}) {{ Text(\"Content\"); }} }} }} }}", "does not support");
        Invalid("component Bad { state bool Open = false; view { VStack() { Reveal(\"First\", open: Open) { Text(\"A\"); } Reveal(\"Second\", open: Open) { Text(\"B\"); } } } }", "independent of other native preflight transactions");
        var legacyMotion = Run("component Bad { view { VStack() { Reveal(\"No\", motion: null) { Text(\"Content\"); } } } }", "Windows");
        Assert(legacyMotion.Diagnostics.Any(d => d.Id == "XUI001" && d.GetMessage().Contains("motion record requires the Portable profile")), "Default Windows Reveal keeps its existing motion arguments and runtime.");
        foreach (string property in new[] { "size: (120, 40)", "preferredSize: (120, 40)", "width: 120", "height: global::Xui.Experimental.Portable.AxisConstraints.Auto" })
            Invalid($"component Bad {{ view {{ VStack() {{ Reveal(\"No\", {property}) {{ Text(\"Content\"); }} }} }} }}", "does not support outer sizing");
        foreach (string flex in new[] { "1", "0 + 0", "\"zero\"", "false" })
            Invalid($"component Bad {{ view {{ VStack() {{ Reveal(\"No\", flex: {flex}) {{ Text(\"Content\"); }} }} }} }}", "only literal zero flex");
        Valid("component ChildSize { view { VStack() { Reveal(\"Sized child\", flex: 0.0f) { VStack(width: 240) { Text(\"Content\"); } } } } }");
        Valid("component GridReveal { view { VStack() { Grid(\"Layout\") { Reveal(\"Cell\", row: 0, column: 0) { Text(\"Content\"); } } } } }");
        Invalid("component Bad { state string Caption = \"Details\"; view { VStack() { Reveal(Caption) { Text(\"Content\"); } } } }", "constructor input is fixed");
        Valid("component GroupName { param string Caption; view { VStack() { Reveal(Caption) { Text(\"Content\"); } } } }");
    }
}
