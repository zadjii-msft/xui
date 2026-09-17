using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.Text;
using Xui.Designer;
using Xui.Generator;

internal static partial class Program
{
    private static void TestDiagnosticColumns()
    {
        var references = ((string)AppContext.GetData("TRUSTED_PLATFORM_ASSEMBLIES")!).Split(Path.PathSeparator)
            .Append(typeof(Xui.Window).Assembly.Location).Distinct(StringComparer.OrdinalIgnoreCase)
            .Select(path => MetadataReference.CreateFromFile(path)).ToArray();
        foreach (string source in new[]
        {
            "component Broken {\n    view {\n        Text(MissingValue);\n    }\n}",
            """
                component Named {
                    view {
                        /* 😀 prefix */ Text("value", enabled: MissingEnabled, size: (MissingWidth, 20));
                    }
                }
                """,
            """component Data { view { DataGrid("data", columns: MissingColumns); } }""",
            """
                component GridMapping {
                    view {
                        Grid(MissingGridName, rows: MissingRows, columns: MissingTracks) {
                            Text("cell", row: MissingRow, column: MissingColumn, rowSpan: MissingSpan);
                        }
                    }
                }
                """,
            """component FlexMapping { view { VStack() { Text("child", flex: MissingFlex); } } }""",
            """component ContentMapping { view { Content(MissingContent); } }""",
            """component Handler { view { Button("button", click: MissingHandler); } }""",
            """component RangeMapping { view { RangeInput("range", range: MissingRange, currentValue: MissingNumber, orientation: MissingOrientation, reversed: MissingReversed, change: MissingChange); } }""",
            """component ProgressMapping { view { Progress("progress", range: MissingRange, currentValue: MissingNumber, progressState: MissingState); } }""",
            """component Initializer { state int Count = MissingState; view { Text("x"); } }""",
            """component InlineCode { view { Text("x"); } code csharp { void Run() { MissingCode(); } } }""",
            """
                component Multiline {
                    view {
                        Text(
                            MissingFirst
                            + MissingSecond);
                    }
                }
                """,
            """"
                component RawInterpolation {
                    view {
                        Text($"""
                            raw 😀 {MissingRaw}
                            """);
                    }
                }
                """",
            """
                component ArrayInitializer {
                    view {
                        Grid("grid", rows: new global::Xui.GridTrack[] {
                            new(MissingSizing, 1)
                        }, columns: MissingArrayColumns) { }
                    }
                }
                """
        })
        {
            foreach (string newline in new[] { "\n", "\r\n", "\r" })
            {
                string authored = source.ReplaceLineEndings(newline);
                var text = SourceText.From(authored);
                var generated = Generate(text);
                var expected = Microsoft.CodeAnalysis.CSharp.SyntaxFactory.ParseTokens(authored)
                    .Where(t => t.IsKind(SyntaxKind.IdentifierToken) && t.ValueText.StartsWith("Missing", StringComparison.Ordinal))
                    .GroupBy(t => t.ValueText, StringComparer.Ordinal).ToDictionary(g => g.Key, g => g.First().SpanStart, StringComparer.Ordinal);
                // Interpolation identifiers are inside the interpolated string token sequence.
                if (authored.Contains("MissingRaw", StringComparison.Ordinal))
                    expected["MissingRaw"] = authored.IndexOf("MissingRaw", StringComparison.Ordinal);
                var diagnostics = generated.GetDiagnostics().Where(d => d.Severity == DiagnosticSeverity.Error).ToArray();
                Assert(diagnostics.Length == expected.Count, string.Join("\n", diagnostics.Select(d => d.ToString())));
                foreach (var (name, offset) in expected)
                {
                    var diagnostic = diagnostics.Single(d => d.Id == "CS0103" && d.GetMessage().Contains("'" + name + "'", StringComparison.Ordinal));
                    var mapped = diagnostic.Location.GetMappedLineSpan();
                    var span = text.Lines.GetLinePositionSpan(new TextSpan(offset, name.Length));
                    Assert(mapped.Path == DiagnosticSourceFile.SourcePath && mapped.StartLinePosition == span.Start &&
                        mapped.EndLinePosition == span.End,
                        $"Expected exact UTF-16 mapping for {name}: {span}, got {mapped}.");
                }
            }
        }

        foreach (var (source, value) in new[]
        {
            ("""component TypeError { view { Text(42); } }""", "42"),
            ("component TypeError {\n view {\n Text(\"name\", enabled: \"wrong\");\n }\n}", "\"wrong\""),
            ("""component TypeError { state int Count = "wrong"; view { Text("name"); } }""", "\"wrong\"")
        })
        {
            foreach (string newline in new[] { "\n", "\r\n", "\r" })
            {
                string authored = source.ReplaceLineEndings(newline);
                var text = SourceText.From(authored);
                var error = Generate(text).GetDiagnostics().Single(d => d.Id == "CS0029");
                var expected = text.Lines.GetLinePositionSpan(new(authored.IndexOf(value, StringComparison.Ordinal), value.Length));
                var mapped = error.Location.GetMappedLineSpan();
                Assert(mapped.StartLinePosition == expected.Start && mapped.EndLinePosition == expected.End,
                    "Type errors retain the complete exact authored expression range.");
            }
        }

        foreach (string source in new[]
        {
            "component LongLine { view { Text(" + new string(' ', 70000) + "\"okay\"); } }",
            "component LongLiteral { view { Text(\"" + new string('a', 70000) + "\"); } }"
        })
        {
            var errors = Generate(SourceText.From(source)).GetDiagnostics().Where(d => d.Severity == DiagnosticSeverity.Error).ToArray();
            Assert(errors.Length == 0,
                "Ordinary generator source outside the visual tooling limit must not gain directive errors: " + string.Join("\n", errors.Select(d => d.ToString())));
        }

        const string previewSource = "component Broken {\n    view {\n        Text(MissingValue);\n    }\n}";
        foreach (string newline in new[] { "\n", "\r\n", "\r" })
        {
            var preview = PreviewCompiler.Compile(previewSource.ReplaceLineEndings(newline));
            Assert(!preview.Success && preview.Diagnostics.Contains("Preview.xui(3,14): error CS0103", StringComparison.Ordinal),
                "Actual PreviewCompiler output must identify the M in MissingValue: " + preview.Diagnostics);
        }

        const string boundaryPrefix = "component Boundary { view { Text(";
        string boundary = boundaryPrefix + new string(' ', 65535 - boundaryPrefix.Length) + "M); } }";
        var boundaryErrors = Generate(SourceText.From(boundary)).GetDiagnostics().Where(d => d.Severity == DiagnosticSeverity.Error).ToArray();
        Assert(boundaryErrors.Length == 1 && boundaryErrors[0].Id == "CS0103" &&
            boundaryErrors[0].Location.GetMappedLineSpan().StartLinePosition.Character == 65535,
            "The enhanced directive supports its inclusive final column without a spurious directive error.");

        Compilation Generate(SourceText text)
        {
            var input = CSharpCompilation.Create("DiagnosticColumns", references: references,
                options: new CSharpCompilationOptions(OutputKind.DynamicallyLinkedLibrary,
                    nullableContextOptions: NullableContextOptions.Enable));
            GeneratorDriver driver = CSharpGeneratorDriver.Create(
                generators: [new XuiGenerator().AsSourceGenerator()], additionalTexts: [new DiagnosticSourceFile(text)],
                parseOptions: CSharpParseOptions.Default.WithLanguageVersion(LanguageVersion.Latest));
            driver.RunGeneratorsAndUpdateCompilation(input, out var generated, out var generatorDiagnostics);
            Assert(!generatorDiagnostics.Any(d => d.Severity == DiagnosticSeverity.Error), string.Join("\n", generatorDiagnostics));
            return generated;
        }
    }

    private sealed class DiagnosticSourceFile(SourceText text) : AdditionalText
    {
        internal const string SourcePath = @"C:\fixture\DesignerDiagnostics.xui";
        public override string Path => SourcePath;
        public override SourceText GetText(CancellationToken cancellationToken = default) => text;
    }
}
