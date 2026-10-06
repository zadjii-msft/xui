using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.Text;
using Xui.Generator;

internal static partial class Program
{
    private sealed record DiagnosticCase(string Source, string[] Details, bool Shared = true);

    private static void TestAuthorDiagnostics()
    {
        const string marker = "/*error*/";
        foreach (var test in new DiagnosticCase[]
        {
            new("/*error*/", ["'component'", "end of file", "Declare one component"]),
            new("namespace /*error*/; component Bad {}", ["namespace name", "found ';'", "C# identifier"]),
            new("namespace Demo./*error*/; component Bad {}", ["namespace segment", "found ';'"]),
            new("namespace Demo /*error*/component Bad {}", ["';'", "namespace declaration", "Add ';'"]),
            new("component /*error*/{", ["component name", "found '{'"]),
            new("component /*error*/@__xuiBad {}", ["@__xuiBad", "reserved prefix", "Rename"]),
            new("component Bad /*error*/view {}", ["'{'", "component 'Bad'", "found 'view'"]),
            new("component Bad { /*error*/Text(\"x\"); }", ["inside component 'Bad'", "controls inside view"]),
            new("component Bad { view { VStack() {} } /*error*/", ["Unclosed component 'Bad'", "Add '}'"]),
            new("component /*error*/Bad {}", ["has no view", "Add 'view"]),
            new("component Bad { view /*error*/VStack() {} }", ["'{'", "after 'view'"]),
            new("component Bad { view { /*error*/} }", ["view block is empty", "Add one root"]),
            new("component Bad { view { /*error*/; } }", ["root control name", "found ';'"]),
            new("component Bad { view { Text(\"a\"); /*error*/Text(\"b\"); } }", ["already has a root", "Wrap sibling controls"]),
            new("component Bad { view { VStack() {} } /*error*/view {} }", ["already has a view", "Merge"]),
            new("component Bad { view { Text(\"x\"); } } /*error*/component Next {}", ["after component 'Bad'", "own .xui file"]),
            new("component Bad { view { VStack() {} } }/*error*/;", ["Unexpected ';'", "component 'Bad'", "Remove"]),
            new("component Bad { view { VStack() {} }/*error*/; }", ["Unexpected ';'", "view block", "Remove"]),
            new("component Bad { view { VStack() { HStack() {}/*error*/; Text(\"x\"); } } }",
                ["Unexpected ';'", "child block of 'HStack'", "Remove the semicolon", "'};'"]),
            new("component Bad { view { VStack() { /*error*/Unknown(\"x\"); } } }", ["'Unknown'", "not supported in .xui", "supported control"]),
            new("component Bad { view { VStack/*error*/{ } } }", ["'('", "control name 'VStack'", "VStack(...)"]),
            new("component Bad { view { VStack() /*error*/; } }", ["'{'", "child block of VStack", "container"]),
            new("component Bad { view { Text(\"x\") /*error*/{ } } }", ["';'", "Text", "cannot contain child controls"]),
            new("component Bad { view { Text(\"x\") /*error*/} }", ["';'", "Text", "Finish this leaf control"]),
            new("component Bad { view { VStack() { /*error*/", ["Unclosed", "child block of 'VStack'", "Add '}'"]),
            new("component Bad { view { Text(/*error*/", ["Unclosed argument list of Text", "Add ')'"]),
            new("component Bad { view { VStack(/*error*/8) {} } }", ["VStack", "does not accept positional", "spacing: 8"]),
            new("component Bad { view { Text(\"x\", /*error*/8); } }", ["positional argument in Text", "name: value"]),
            new("component Bad { view { Text(/*error*/value: \"x\"); } }", ["first argument of Text", "Remove 'value:'"]),
            new("component Bad { view { Text(\"x\", /*error*/unknown: 1); } }", ["Unknown argument 'unknown' on Text", "Remove or replace", "'enabled'"]),
            new("component Bad { view { Text(\"x\", /*error*/name: \"y\"); } }", ["Text", "accessible name", "remove 'name:'"]),
            new("component Bad { view { Text(\"x\", id: \"a\", /*error*/id: \"b\"); } }", ["Duplicate argument 'id' on Text", "remove"]),
            new("component Bad { view { Text(\"x\", id: /*error*/); } }", ["Missing value", "'id'", "Text", "found ')'", "Supply a C# expression"]),
            new("component Bad { view { Text(\"x\" /*error*/id: \"y\"); } }", ["','", "Text arguments", "found 'id'", "Separate arguments"]),
            new("component Bad { view { Button(\"x\", click: /*error*/Run()); } }", ["'click'", "Button", "Run()", "without parentheses"]),
            new("component Bad { view { Text(\"x\", ref: /*error*/\"Label\"); } }", ["'ref'", "Text", "ref: ControlName"]),
            new("component Bad { view { /*error*/Button(); } }", ["Button", "first positional string", "Button(\"Name\", ...)"]),
            new("component Bad { view { /*error*/Content(); } }", ["Content", "positional Element", "Content(element)"]),
            new("component Bad { view { /*error*/ScrollView(\"x\") {} } }", ["ScrollView", "exactly 1", "found 0", "Add the missing"]),
            new("component Bad { view { /*error*/SplitView(\"x\") { Text(\"a\"); } } }", ["SplitView", "exactly 2", "found 1"]),
            new("component Bad { view { /*error*/Reveal(\"x\") { Text(\"a\"); Text(\"b\"); } } }", ["Reveal", "found 2", "Wrap sibling controls"]),
            new("component Bad { state /*error*/int Count; view { Text(\"x\"); } }", ["State", "initializer", "state int Count = 0"]),
            new("component Bad { state int Count = 0; state int /*error*/Count = 1; view { Text(\"x\"); } }", ["Duplicate state name 'Count'", "different name"]),
            new("component Bad { state int /*error*/@__xuiCount = 0; view { Text(\"x\"); } }", ["State name '@__xuiCount'", "reserved prefix", "Rename"]),
            new("component Bad { param /*error*/string Title = \"x\"; view { Text(\"x\"); } }", ["parameter", "no initializer", "constructor"]),
            new("component Bad { view { Text(\"x\"); } code /*error*/rust {} }", ["'csharp'", "found 'rust'", "component methods"]),
            new("component Bad { view { Text(\"x\"); } code csharp /*error*/void Run() {} }", ["'{'", "code csharp block", "method"]),
            new("component Bad { view { Text(\"x\"); } code csharp { void Run() {} /*error*/", ["Unclosed code csharp", "Add the missing '}'"]),
            new("component Bad { view { Text(\"x\"); } code csharp { void Run() {} } /*error*/code csharp {} }", ["already has a code csharp block", "Move these methods"]),
            new("component Bad { view { Text(\"x\"); } code csharp { /*error*/int Count = 0; } }", ["method declarations only", "Move persistent fields", "state Type"]),
            new("component Bad { resources /*error*/Accent: 0; }", ["'{'", "after 'resources'"]),
            new("component Bad { resources { Accent/*error*/; } }", ["':'", "resource name 'Accent'"]),
            new("component Bad { resources { Accent: /*error*/; } }", ["Missing value", "color resource 'Accent'", "before ';'"]),
            new("component Bad { resources { Accent: 0 /*error*/} }", ["';'", "value of resource 'Accent'"]),
            new("component Bad { resources { Accent: 0; /*error*/", ["Unclosed", "resources block", "Add '}'"]),
            new("component Bad { resources {}/*error*/; view { Text(\"x\"); } }", ["Unexpected ';'", "resources block", "Remove"]),
            new("component Bad { style Accent /*error*/Button {} }", ["'for'", "style name 'Accent'", "style Name for Control"]),
            new("component Bad { style Accent for /*error*/Tooltip {} view { Text(\"x\"); } }", ["Tooltip", "Window API", "from C#"]),
            new("component Bad { style Accent for /*error*/Unknown {} view { Text(\"x\"); } }", ["Unsupported control target 'Unknown'", "style 'Accent'", "supported style target"]),
            new("component Bad { style Accent for Button { /*error*/opacity: 1; } view { Text(\"x\"); } }", ["'opacity'", "Button part 'root'", "Remove or replace", "'background'"]),
            new("component Bad { style Accent for Button { background/*error*/; } }", ["':'", "'background'", "Button part 'root'"]),
            new("component Bad { style Accent for Button { background: /*error*/; } }", ["Missing value", "'background'", "Button part 'root'"]),
            new("component Bad { style Accent for Button { background: 0; /*error*/background: 1; } view { Text(\"x\"); } }", ["Duplicate style property", "'background'", "Keep one value"]),
            new("component Bad { style Accent for Button { /*error*/part root {} } view { Text(\"x\"); } }", ["part 'root'", "directly inside the style"]),
            new("component Bad { style Accent for Text { part label { /*error*/part caption {} } } view { Text(\"x\"); } }", ["cannot be nested", "part 'label'", "Move it directly"]),
            new("component Bad { style Accent for Text { part label {} /*error*/part label {} } view { Text(\"x\"); } }", ["Duplicate part 'label'", "style 'Accent'", "Merge"]),
            new("component Bad { style Accent for Button { when /*error*/selected {} } view { Text(\"x\"); } }", ["Unsupported state 'selected'", "Button part 'root'", "'hovered'"]),
            new("component Bad { style Accent for Button { when /*error*/", ["Unsupported state end of file", "style 'Accent'", "Choose one of"]),
            new("component Bad { style Accent for Text { when disabled {} when /*error*/disabled {} } view { Text(\"x\"); } }", ["Duplicate state 'disabled'", "part 'root'", "Merge"]),
            new("component Bad { style Accent for Button { /*error*/", ["Unclosed style 'Accent'", "Button part 'root'", "Add '}'"]),
            new("component Bad { style Accent for Button { when hovered { /*error*/", ["Unclosed", "'hovered' state rule", "Add '}'"]),
            new("component Bad { style Accent for Button {}/*error*/; view { Text(\"x\"); } }", ["Unexpected ';'", "style 'Accent'", "Remove"]),
            new("component Bad { resources { Accent: /*error*/0x1000000; } view { Text(\"x\"); } }", ["Invalid color", "0x1000000", "color resource 'Accent'", "0xFFFFFF"], false),
            new("component Bad { resources { Accent: /*error*/resource(\"Accent\"); } view { Text(\"x\"); } }", ["Invalid resource reference", "unquoted resource name"], false),
            new("component Bad { resources { Accent: /*error*/theme(dark: 0, light: 1); } view { Text(\"x\"); } }", ["Invalid theme color", "light first", "dark second"], false),
            new("component Bad { resources { Accent: theme(light: 0, dark: /*error*/0x1000000); } view { Text(\"x\"); } }", ["Invalid color", "dark value", "resource 'Accent'", "0xFFFFFF"], false),
            new("component Bad { resources { Accent: /*error*/resource(Missing); } view { Text(\"x\"); } }", ["resource 'Missing' was not found", "resources block", "correct"], false),
            new("component Bad { resources { Accent: /*error*/resource(Accent); } view { Text(\"x\"); } }", ["cycle at 'Accent'", "Replace an alias", "break the cycle"], false),
            new("component Bad { resources { Accent: 0; /*error*/Accent: 1; } view { Text(\"x\"); } }", ["Duplicate color resource 'Accent'", "different name"], false),
            new("component Bad { style Accent for Button {} style /*error*/Accent for Button {} view { Text(\"x\"); } }", ["Duplicate style 'Accent'", "different name"], false),
            new("component Bad { style /*error*/Accent for Button basedOn Missing {} view { Text(\"x\"); } }", ["Base style 'Missing'", "'Accent'", "correct the basedOn name"], false),
            new("component Bad { style /*error*/Accent for Button basedOn Accent {} view { Text(\"x\"); } }", ["cycle at 'Accent'", "basedOn", "inherits from itself"], false),
            new("component Bad { style TextStyle for Text {} style /*error*/Accent for Button basedOn TextStyle {} view { Text(\"x\"); } }", ["'Accent' targets Button", "'TextStyle' targets Label", "both must match"], false),
            new("component Bad { style Accent for Button { padding: (1, 2, 3, /*error*/-1); } view { Text(\"x\"); } }", ["Invalid dimension '-1'", "bottom inset", "'padding'", "0 and 32768"], false),
            new("component Bad { style Accent for Button { padding: /*error*/(1, 2); } view { Text(\"x\"); } }", ["Invalid insets", "'padding'", "(left, top, right, bottom)", "(4, 8, 4, 8)"], false),
            new("component Bad { style Accent for Button { fontStyle: /*error*/bold; } view { Text(\"x\"); } }", ["Unsupported value 'bold'", "'fontStyle'", "'normal'", "'italic'"], false),
            new("component Bad { style Accent for Text { wrapping: /*error*/\"true\"; } view { Text(\"x\"); } }", ["'wrapping'", "boolean literal true or false"], false),
            new("component Bad { style Accent for Button { fontWeight: /*error*/1000; } view { Text(\"x\"); } }", ["'fontWeight'", "1 through 999"], false),
            new("component Bad { style Accent for Text { maximumLines: /*error*/1.5; } view { Text(\"x\"); } }", ["'maximumLines'", "integer literal", "0 and 32768"], false),
            new("component Bad { style Accent for Button { fontSize: /*error*/0; } view { Text(\"x\"); } }", ["'fontSize'", "greater than zero", "positive numeric literal"], false),
            new("component Bad { style Accent for TextInput { part text { fontSize: /*error*/513; } } view { Text(\"x\"); } }", ["'fontSize'", "TextInput part 'text'", "part's limit of 512", "smaller positive"], false),
            new("component Bad { style Accent for TextInput { part text { fontStyle: /*error*/oblique; } } view { Text(\"x\"); } }", ["Font style 'oblique'", "TextInput part 'text'", "'normal'", "'italic'"], false),
            new("component Bad { style Accent for Button { verticalAlignment: /*error*/stretch; } view { Text(\"x\"); } }", ["Alignment 'stretch'", "Button part 'root'", "'start'", "'center'", "'end'"], false),
            new("component Bad { style Accent for TextInput { part text { fontFamily: /*error*/\"abcdefghijklmnopqrstuvwxyz123456\"; } } view { Text(\"x\"); } }", ["'fontFamily'", "32 UTF-16 code units", "at most 31", "shorter"], false),
            new("component Bad { style Accent for Button { fontFamily: /*error*/\"\"; } view { Text(\"x\"); } }", ["'fontFamily'", "nonempty string literal", "Segoe UI"], false),
            new("""component Bad { style Accent for Button { fontFamily: /*error*/"\uD800"; } view { Text("x"); } }""", ["'fontFamily'", "unpaired Unicode surrogate", "Replace", "Segoe UI"], false),
            new("component Bad { style Accent for DataGrid { rowHeight: /*error*/0; } view { Text(\"x\"); } }", ["'rowHeight'", "DataGrid part 'root'", "greater than zero", "positive numeric literal"], false),
            new("component Bad { style /*error*/Accent for Button { fontWeight: 400; when hovered {} when hovered {} } view { Text(\"x\"); } }", ["Duplicate state 'hovered'", "style 'Accent'", "merge the repeated"], false),
            new("component Bad { view { Button(\"x\", style: /*error*/Missing); } }", ["Style 'Missing' was not found for Button", "Declare", "correct the style name"], false),
            new("component Bad { view { Button(\"x\", style: /*error*/\"Accent\"); } }", ["Invalid style reference", "'style: StyleName'", "not a string"], false),
            new("component Bad { style Accent for Text {} view { Button(\"x\", style: /*error*/Accent); } }", ["'Accent' targets Label, not Button", "targeting Button"], false),
            new("component Bad { view { Content(null, style: /*error*/Missing); } }", ["Content", "declared style name", "style: StyleName"], false),
            new("component Bad { view { Content(null, background: /*error*/0); } }", ["Content argument 'background'", "embedded control's target", "style: StyleName"], false),
            new("component Bad { view { VStack() { Text(\"x\", row: /*error*/0); } } }", ["'row' on Text", "direct Grid parent", "inside VStack", "remove 'row'"], false),
            new("component Bad { view { Text(\"x\", flex: /*error*/1); } }", ["'flex' on Text", "VStack or HStack parent", "view root"], false),
            new("component Bad { view { Grid(\"x\") { Text(\"x\", rowSpan: /*error*/0); } } }", ["'rowSpan'", "integer of at least 1"], false),
            new("component Bad { state int Row = 0; view { Grid(\"x\") { Text(\"x\", row: /*error*/Row); } } }", ["'row' on Text", "cannot depend on state", "constructor parameter"], false),
            new("component Bad { state string Name = \"x\"; view { Grid(/*error*/Name) {} } }", ["Grid's first positional", "cannot depend on state", "constructor parameter"], false),
            new("component Bad { state int Count = 0; view { Text(/*error*/$\"{Count++}\"); } }", ["View expression", "not side-effect-free", "code csharp methods"], false),
            new("component Bad { state int Count = 0; view { Text(/*error*/Display()); } code csharp { string Display() => Count.ToString(); } }", ["method 'Display'", "reactive dependencies", "bind to state directly"], false),
            new("component Bad { state global::Xui.NumericRange Range = new(0, 100, 1, 10); view { RangeInput(\"x\", range: /*error*/Range); } }", ["'range' on RangeInput", "construction", "'currentValue'"], false),
            new("component Bad { view { InfoBadge(\"x\", count: 1, icon: /*error*/global::Xui.ButtonIcon.Refresh); } }", ["InfoBadge", "'count' and 'icon'", "remove the other"], false),
            new("component Bad { param /*error*/string window; view { Text(\"x\"); } }", ["Parameter name 'window'", "generated constructor", "Rename"], false),
            new("component Bad { state /*error*/int Root = 0; view { Text(\"x\"); } }", ["State name 'Root'", "generated Root property", "Rename"], false),
            new("component Bad { state string Entry = \"x\"; view { Text(\"x\", ref: /*error*/Entry); } }", ["Text reference name 'Entry'", "conflicts with State", "Rename"], false),
            new("component Bad { state int Count = 0; view { Text(\"x\"); } code csharp { void /*error*/Count() {} } }", ["Method name 'Count'", "conflicts with State", "Rename"], false)
        })
        {
            foreach (string newline in new[] { "\n", "\r\n", "\r" })
            {
                string marked = test.Source.ReplaceLineEndings(newline);
                int offset = marked.IndexOf(marker, StringComparison.Ordinal);
                Assert(offset >= 0, "Each diagnostic case marks its exact error location.");
                string source = marked.Remove(offset, marker.Length);
                var diagnostic = Invalid(source);
                string message = diagnostic.GetMessage();
                Assert(test.Details.All(detail => message.Contains(detail, StringComparison.Ordinal)),
                    "Expected actionable diagnostic details " + string.Join(", ", test.Details) + ": " + message);
                Assert(diagnostic.Location.SourceSpan.Start == offset,
                    $"Expected error at offset {offset}, got {diagnostic.Location.SourceSpan.Start}: {message}");
                var location = SourceText.From(source).Lines.GetLinePosition(offset);
                Assert(diagnostic.Location.GetLineSpan().StartLinePosition == location,
                    "Diagnostics preserve the exact authored line and UTF-16 column.");
                Assert(!message.Contains("Expected an identifier.", StringComparison.Ordinal),
                    "Diagnostics must not fall back to the context-free identifier error.");
                if (test.Shared)
                {
                    var parsed = XuiSourceParser.Parse(source);
                    Assert(!parsed.Success && parsed.Root is null, "Invalid syntax never publishes a partial visual tree.");
                    var error = parsed.Diagnostics.First(d => d.Message == message);
                    Assert(error.Span.Start == offset, "Visual tooling and generation share the message and source location.");
                }
            }
        }

        const string original = """
            component Counter {
                view {
                    VStack() {
                        HStack() {
                            TextInput("foo");
                        }/*error*/;
                        Text("Count");
                    }
                }
            }
            """;
        foreach (string newline in new[] { "\n", "\r\n", "\r" })
        {
            string marked = original.ReplaceLineEndings(newline);
            int offset = marked.IndexOf(marker, StringComparison.Ordinal);
            string source = marked.Remove(offset, marker.Length);
            var diagnostic = Invalid(source);
            Assert(diagnostic.GetMessage() == "Unexpected ';' after the child block of 'HStack'. Remove the semicolon: blocks end with '}', not '};'.",
                "The original container-semicolon error identifies exactly what to remove.");
            Assert(diagnostic.Location.SourceSpan.Start == offset && diagnostic.Location.GetLineSpan().StartLinePosition.Line == 5,
                "The diagnostic points to the semicolon outside the HStack, not to either brace or its children.");
            Assert(XuiSourceParser.Parse(source.Remove(offset, 1)).Success, "Removing the identified semicolon repairs the syntax.");
            var (_, fixedCompilation) = Generate(new File(@"C:\fixture\FixedCounter.xui", source.Remove(offset, 1)));
            Assert(!fixedCompilation.GetDiagnostics().Any(d => d.Severity == DiagnosticSeverity.Error),
                "The proposed semicolon correction compiles.");
        }

        var (_, valid) = Generate(new File(@"C:\fixture\Corrected.xui", """
            component Corrected {
                state int Count = 0;
                resources { Accent: theme(light: 0xB91C1C, dark: 0x991B1B); }
                style AccentButton for Button {
                    background: resource(Accent);
                    padding: (4, 8, 4, 8);
                    fontWeight: 400;
                    when hovered { foreground: 0xFFFFFF; }
                }
                view {
                    VStack(spacing: 8) {
                        Button("Run", style: AccentButton, click: Run, ref: RunButton);
                        Text($"{Count}");
                    }
                }
                code csharp { void Run() => Count++; }
            }
            """));
        Assert(!valid.GetDiagnostics().Any(d => d.Severity == DiagnosticSeverity.Error),
            "Suggested declaration, resource, style, handler, and state patterns remain valid.");

        GeneratorDriver unreadable = CSharpGeneratorDriver.Create([new XuiGenerator().AsSourceGenerator()], [new UnreadableFile()]);
        unreadable.RunGeneratorsAndUpdateCompilation(Empty(), out var unreadableCompilation, out var unreadableDiagnostics);
        string unreadableMessage = unreadableDiagnostics.Single(d => d.Id == "XUI001").GetMessage();
        Assert(unreadableMessage.Contains("Unreadable.xui", StringComparison.Ordinal) &&
            unreadableMessage.Contains("file exists, is readable", StringComparison.Ordinal),
            "Unreadable-file diagnostics identify the input and the action to take.");
        Assert(unreadableCompilation.GetDiagnostics().Single(d => d.Id == "CS1029").GetMessage().Contains(unreadableMessage, StringComparison.Ordinal),
            "The blocking build error retains the actionable unreadable-file message.");

        var tooLong = XuiSourceParser.Parse(new string(' ', XuiSourceParser.MaximumSourceLength + 1));
        Assert(tooLong.Diagnostics.Single().Message.Contains("65537", StringComparison.Ordinal) &&
            tooLong.Diagnostics.Single().Message.Contains("65536", StringComparison.Ordinal) &&
            tooLong.Diagnostics.Single().Message.Contains("Shorten", StringComparison.Ordinal),
            "Visual source-limit diagnostics give the actual size, limit, and correction.");

        var resourceLimit = Invalid("component Bad { resources { " +
            string.Join(" ", Enumerable.Range(0, 257).Select(i => $"R{i}: 0;")) + " } view { VStack() {} } }");
        Assert(resourceLimit.GetMessage().Contains("Component 'Bad' has more than 256 color resources", StringComparison.Ordinal) &&
            resourceLimit.GetMessage().Contains("Remove or combine", StringComparison.Ordinal), "Resource-limit errors identify the owner, bound, and correction.");
        var ruleLimit = Invalid("component Bad { style Accent for Button { " +
            string.Concat(Enumerable.Repeat("when hovered {} ", 257)) + " } view { VStack() {} } }");
        Assert(ruleLimit.GetMessage().Contains("Style 'Accent' has more than 256 state rules", StringComparison.Ordinal) &&
            ruleLimit.GetMessage().Contains("Remove or combine", StringComparison.Ordinal), "Rule-limit errors identify the style, bound, and correction.");
        var inheritanceLimit = Invalid("component Bad { " + string.Join(" ", Enumerable.Range(0, 17)
            .Select(i => $"style S{i} for Text" + (i < 16 ? $" basedOn S{i + 1}" : "") + " {}")) + " view { VStack() {} } }");
        Assert(inheritanceLimit.GetMessage().Contains("16 layers", StringComparison.Ordinal) &&
            inheritanceLimit.GetMessage().Contains("Shorten", StringComparison.Ordinal) &&
            !inheritanceLimit.GetMessage().Contains("Button", StringComparison.Ordinal), "Inheritance limits report the actual style chain, not an unrelated Button target.");
        var nestingLimit = XuiSourceParser.Parse("component Deep { view { " +
            string.Concat(Enumerable.Repeat("VStack() { ", XuiSourceParser.MaximumDepth + 1)) +
            string.Concat(Enumerable.Repeat("} ", XuiSourceParser.MaximumDepth + 1)) + " } }");
        Assert(nestingLimit.Diagnostics.Single().Message.Contains("128 nested controls", StringComparison.Ordinal) &&
            nestingLimit.Diagnostics.Single().Message.Contains("Flatten", StringComparison.Ordinal), "Nesting errors explain the tooling limit and how to reduce depth.");

        const string multilineValue = "component Bad { style Accent for Button { fontStyle: @\"line\nvalue\"; } view { Text(\"x\"); } }";
        string quotedMessage = Invalid(multilineValue).GetMessage();
        Assert(quotedMessage.Contains("\\n", StringComparison.Ordinal) && !quotedMessage.Contains('\n'),
            "Quoted source excerpts escape newlines so generated blocking directives stay on one line.");
        foreach (string separator in new[] { "\u0085", "\u2028", "\u2029" })
        {
            string source = multilineValue.Replace("\n", separator, StringComparison.Ordinal);
            Assert(!Invalid(source).GetMessage().Contains(separator, StringComparison.Ordinal),
                "Quoted excerpts escape Unicode line separators instead of splitting diagnostics.");
        }
        string longValue = "@\"" + new string('a', 77) + "\U0001F600tail\"";
        string longMessage = Invalid("component Bad { style Accent for Button { fontStyle: " + longValue +
            "; } view { Text(\"x\"); } }").GetMessage();
        Assert(longMessage.Contains("...", StringComparison.Ordinal) &&
            new System.Text.UTF8Encoding(false, true).GetByteCount(longMessage) > 0,
            "Bounded excerpts do not split Unicode surrogate pairs at the truncation boundary.");
    }
}
