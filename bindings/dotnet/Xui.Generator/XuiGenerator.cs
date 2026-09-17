using System.Security.Cryptography;
using System.Text;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.CSharp.Syntax;
using Microsoft.CodeAnalysis.Text;

namespace Xui.Generator;

[Generator]
public sealed class XuiGenerator : IIncrementalGenerator
{
    private static readonly DiagnosticDescriptor Invalid = new("XUI001", "Invalid XUI source", "{0}",
        "Xui", DiagnosticSeverity.Error, true);

    public void Initialize(IncrementalGeneratorInitializationContext context)
    {
        context.RegisterPostInitializationOutput(output => output.AddSource("Xui.HotReload.g.cs", """
            #nullable enable
            #if XUI_HOT_RELOAD
            [assembly: System.Reflection.Metadata.MetadataUpdateHandler(typeof(__xuiMetadataUpdateHandler))]
            internal static class __xuiMetadataUpdateHandler
            {
                public static void UpdateApplication(System.Type[]? types) => Xui.Development.ReloadHost.UpdateApplication(types);
            }
            #endif
            """));
        var files = context.AdditionalTextsProvider
            .Where(file => file.Path.EndsWith(".xui", StringComparison.OrdinalIgnoreCase))
            .Select((file, token) => (file.Path, Text: file.GetText(token)));
        context.RegisterSourceOutput(files, (output, file) =>
        {
            string suffix = Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(file.Path)))[..12];
            if (file.Text is null)
            {
                output.ReportDiagnostic(Diagnostic.Create(Invalid, Location.None, $"Cannot read '{file.Path}'."));
                output.AddSource("Xui.Error." + suffix + ".g.cs", "#error XUI001: Cannot read XUI source.\n");
                return;
            }
            try
            {
                var parser = new Parser(file.Text.ToString());
                var component = parser.Parse();
                var emitter = new Emitter(component, file.Path, file.Text);
                var generated = emitter.Emit();
                // Keep declarations to avoid rude deletions. The watch delta pipeline can
                // ignore generator diagnostics, so C# #error must also block invalid updates.
                foreach (var error in parser.Errors.Concat(emitter.Errors))
                {
                    Report(error);
                    generated += ErrorDirective(error);
                }
                string identity = component.Namespace + "." + component.Name;
                output.AddSource(identity.Replace("@", "") + "." + suffix + ".g.cs", generated);
            }
            catch (ParseError error)
            {
                Report(error);
                output.AddSource("Xui.Error." + suffix + ".g.cs", ErrorDirective(error));
            }
            string ErrorDirective(ParseError error)
            {
                int offset = Math.Clamp(error.Offset, 0, file.Text.Length);
                int line = file.Text.Lines.GetLineFromPosition(offset).LineNumber + 1;
                string message = string.Join(" ", error.Message.Split(['\r', '\n', '\u0085', '\u2028', '\u2029']));
                return $"\n#line {line} \"{file.Path}\"\n#error XUI001: {message}\n#line default\n";
            }
            void Report(ParseError error)
            {
                int offset = Math.Clamp(error.Offset, 0, file.Text.Length);
                var span = new TextSpan(offset, offset < file.Text.Length ? 1 : 0);
                output.ReportDiagnostic(Diagnostic.Create(Invalid,
                    Location.Create(file.Path, span, file.Text.Lines.GetLinePositionSpan(span)), error.Message));
            }
        });
    }
}

internal sealed class Emitter(Component component, string path, SourceText source)
{
    internal List<ParseError> Errors { get; } = [];
    private readonly StringBuilder output = new();
    private readonly List<Node> nodes = [];
    private readonly List<Binding> bindings = [];
    private readonly StyleCompiler styling = new(component);
    private sealed record Binding(string Name, int Node, string Type, string Setter, Expression Value, string[] Dependencies);
    private void Line(string value = "") => output.AppendLine(value);
    // #line filenames do not interpret backslash escapes like C# string expressions.
    private void Map(int offset) => Line($"#line {source.Lines.GetLineFromPosition(Math.Clamp(offset, 0, source.Length)).LineNumber + 1} \"{path}\"");
    private void Unmap() => Line("#line default");
    private static string Literal(string value) => SymbolDisplay.FormatLiteral(value, true);
    private static string Type(Node node) => node.Kind switch
    {
        "VStack" or "HStack" => "Stack",
        "Text" => "Label",
        "Content" => "Element",
        _ => node.Kind
    };
    private string[] Dependencies(Expression value)
    {
        var expression = SyntaxFactory.ParseExpression(value.Text);
        var identifiers = expression.DescendantNodesAndSelf().OfType<SimpleNameSyntax>().Select(n => n.Identifier.ValueText).ToHashSet();
        var methods = SyntaxFactory.ParseCompilationUnit("class C {" + component.Code.Text + "}")
            .DescendantNodes().OfType<MethodDeclarationSyntax>().Select(m => m.Identifier.ValueText);
        if (methods.Any(identifiers.Contains))
            Errors.Add(new ParseError("View expressions cannot call component methods. Use state directly; computed dependencies must be explicit.", value.Offset));
        if (expression.DescendantNodesAndSelf().Any(n => n is AssignmentExpressionSyntax or AnonymousFunctionExpressionSyntax or AwaitExpressionSyntax ||
            n.IsKind(SyntaxKind.PreIncrementExpression) || n.IsKind(SyntaxKind.PreDecrementExpression) ||
            n.IsKind(SyntaxKind.PostIncrementExpression) || n.IsKind(SyntaxKind.PostDecrementExpression)))
            Errors.Add(new ParseError("View expressions must be side-effect-free; assignments, increment, lambdas, and await are unsupported.", value.Offset));
        return component.States.Where(s => identifiers.Contains(s.Name.TrimStart('@'))).Select(s => s.Name).ToArray();
    }
    private void Collect(Node node, Node? parent = null)
    {
        int index = nodes.Count;
        nodes.Add(node);
        void Bind(string name, string type, string setter, string fallback, bool staticCall = false)
        {
            var value = node.Arguments.GetValueOrDefault(name) ?? new Expression(fallback, node.Offset);
            bindings.Add(new($"__xuiB{index}_{name}", index, type, staticCall ? setter : $"__xuiN{index}." + setter, value,
                Dependencies(value)));
        }
        foreach (string key in new[] { "row", "column", "rowSpan", "columnSpan", "flex" })
        {
            if (!node.Arguments.TryGetValue(key, out var placement)) continue;
            if (key == "flex" ? parent?.Kind is not ("VStack" or "HStack") : parent?.Kind != "Grid")
                Errors.Add(new ParseError($"'{key}' requires a {(key == "flex" ? "Stack" : "Grid")} parent.", placement.Offset));
            if (Dependencies(placement).Length != 0)
                Errors.Add(new ParseError($"'{key}' cannot depend on state. Placement is fixed for the component lifetime.", placement.Offset));
            if (key != "flex" && int.TryParse(placement.Text, out int literal) && literal < (key.EndsWith("Span") ? 1 : 0))
                Errors.Add(new ParseError($"'{key}' must be {(key.EndsWith("Span") ? "positive" : "nonnegative")}.", placement.Offset));
        }
        if (node.Kind is "VStack" or "HStack")
        {
            if (node.Arguments.ContainsKey("spacing")) Bind("spacing", "float", "Spacing({0})", "0");
            if (node.Arguments.ContainsKey("padding")) Bind("padding", "float", "Padding({0})", "0");
        }
        else if (node.Kind is not ("Content" or "Grid"))
        {
            if (node.Kind != "TextInput")
                Bind("value", "string", node.Kind is "Text" or "Button" or "Toggle" ? "Text = {0}" : "Name = {0}", "\"\"");
            else
            {
                // Other controls alias Name and Text; only TextInput has a separate name.
                if (!node.Arguments.ContainsKey("name")) node.Arguments["name"] = node.Arguments["value"];
                Bind("name", "string", "Name = {0}", "\"\"");
            }
            if (node.Arguments.ContainsKey("id")) Bind("id", "string", "AutomationId = {0}", "\"\"");
            if (node.Arguments.ContainsKey("enabled")) Bind("enabled", "bool", "Enabled = {0}", "true");
            if (node.Kind == "Toggle" && node.Arguments.ContainsKey("checked")) Bind("checked", "bool", "Checked = {0}", "false");
            if (node.Kind == "TextInput" && node.Arguments.ContainsKey("text")) Bind("text", "string", "Text = {0}", "\"\"");
            if (node.Arguments.ContainsKey("visible"))
                Bind("visible", "bool", $"global::Xui.ControlFeatures.Visible(__xuiN{index}, {{0}})", "true", staticCall: true);
            if (node.Arguments.ContainsKey("help"))
                Bind("help", "string", $"global::Xui.ControlFeatures.Help(__xuiN{index}, {{0}})", "\"\"", staticCall: true);
        }
        else if (Dependencies(node.Arguments["value"]).Length != 0)
            Errors.Add(new ParseError($"{node.Kind} positional input cannot depend on state. This constructor input is fixed for the component lifetime.", node.Arguments["value"].Offset));
        foreach (var option in new[]
        {
            ("icon", "global::Xui.ButtonIcon", "SetIcon({0})"),
            ("captionVisible", "bool", "SetCaptionVisible({0})"),
            ("placeholder", "string", "SetPlaceholder({0})"),
            ("headerVisible", "bool", "SetHeaderVisible({0})"),
            ("secondVisible", "bool", "SetSecondVisible({0})"),
            ("placement", "global::Xui.PopupPlacement", "SetPlacement({0})"),
            ("windowBackground", "bool", "SetWindowBackground({0})")
        })
            if (node.Arguments.ContainsKey(option.Item1)) Bind(option.Item1, option.Item2, option.Item3, "default");
        var styleValue = node.Arguments.GetValueOrDefault("style");
        string styleTarget = node.Kind == "Content" && styleValue is not null ? styling.ReferenceTarget(styleValue) : StyleCompiler.TargetName(node.Kind);
        bool genericStyle = styleValue is not null && styling.GenericReference(styleValue);
        if (node.Kind == "Content" && styleValue is null) {
            var untypedLocal = node.Arguments.FirstOrDefault(pair => StyleCompiler.Properties.Contains(pair.Key) || StyleCompiler.ExtendedProperties.Contains(pair.Key));
            if (untypedLocal.Value is not null)
                throw new ParseError("Content style properties require a named style to identify the target schema.", untypedLocal.Value.Offset);
        }
        if (styleValue is not null) {
            string reference = styling.Reference(styleValue, styleTarget);
            if (!genericStyle && node.Kind != "Button")
                throw new ParseError("A legacy Button style requires a Button node.", styleValue.Offset);
            bindings.Add(new($"__xuiB{index}_style", index, genericStyle ? "global::Xui.ControlStyle" : "global::Xui.ButtonStyle",
                genericStyle ? $"__xuiN{index}.SetControlStyle({{0}})" : $"__xuiN{index}.Style = {{0}}",
                new(reference, styleValue.Offset), []));
        }
        if (styleTarget == "Button" || StyleCatalog.TargetExists(styleTarget)) {
            var local = node.Arguments.Where(pair =>
                (StyleCompiler.Properties.Contains(pair.Key) || StyleCompiler.ExtendedProperties.Contains(pair.Key)) &&
                !((node.Kind is "VStack" or "HStack") && (pair.Key is "spacing" or "padding"))).ToDictionary();
            foreach (var pair in local)
                if (!StyleCompiler.AllowedProperties(styleTarget, "root").Contains(pair.Key))
                    throw new ParseError($"Unsupported {styleTarget} root style property '{pair.Key}'.", pair.Value.Offset);
            bool genericLocal = styleTarget != "Button" || genericStyle || local.Keys.Any(key => !StyleCompiler.Properties.Contains(key));
            if (local.Count != 0)
                bindings.Add(new($"__xuiB{index}_styleValues", index, genericLocal ? "global::Xui.PartStyleValues" : "global::Xui.ButtonStyleValues",
                    genericLocal ? $"__xuiN{index}.SetControlStyleValues(global::Xui.StylePart.Root, {{0}})" : $"__xuiN{index}.StyleValues = {{0}}",
                    new(styling.Values(local, genericLocal ? "generic" : "Button", styleTarget), local.First().Value.Offset), []));
        }
        if (node.Kind == "Grid")
        {
            const string tracks = "new global::Xui.GridTrack[] { new(global::Xui.TrackSizing.Star, 1) }";
            var rows = node.Arguments.GetValueOrDefault("rows") ?? new Expression(tracks, node.Offset);
            var columns = node.Arguments.GetValueOrDefault("columns") ?? new Expression(tracks, node.Offset);
            bindings.Add(new($"__xuiB{index}_tracks", index,
                "(global::Xui.GridTrack[] Rows, global::Xui.GridTrack[] Columns)",
                $"__xuiN{index}.SetTracks({{0}}.Rows, {{0}}.Columns)",
                new($"({rows.Text}, {columns.Text})", rows.Offset),
                Dependencies(rows).Concat(Dependencies(columns)).Distinct().ToArray()));
        }
        if (node.Kind == "DataGrid" && node.Arguments.ContainsKey("columns"))
            Bind("columns", "global::Xui.GridColumn[]", "SetColumns({0})", "[]");
        if (node.Kind == "NavigationView")
        {
            if (node.Arguments.ContainsKey("searchId"))
                Bind("searchId", "string", "Search.AutomationId = {0}", "\"\"");
            if (node.Arguments.ContainsKey("searchHelp"))
                Bind("searchHelp", "string", $"global::Xui.ControlFeatures.Help(__xuiN{index}.Search, {{0}})", "\"\"", staticCall: true);
        }
        if (node.Arguments.ContainsKey("size"))
            Bind("size", "(float Width, float Height)",
                $"global::Xui.ElementExtensions.FixedSize(__xuiN{index}, {{0}}.Width, {{0}}.Height)", "(0, 0)", staticCall: true);
        if (node.Arguments.ContainsKey("preferredSize"))
            Bind("preferredSize", "(float Width, float Height)",
                $"global::Xui.ElementExtensions.PreferredSize(__xuiN{index}, {{0}}.Width, {{0}}.Height)", "(0, 0)", staticCall: true);
        foreach (var child in node.Children) Collect(child, node);
    }
    private static string Part(string value) => value.Length.ToString(System.Globalization.CultureInfo.InvariantCulture) + ":" + value;
    private string Shape(Node node) => Part(node.Kind) + Part(node.Arguments.GetValueOrDefault("id")?.Text ?? "") +
        Part(node.Arguments.GetValueOrDefault("searchId")?.Text ?? "") +
        Part(string.Join(",", node.Arguments.Keys.Order())) +
        string.Concat(new[] { "ref", "row", "column", "rowSpan", "columnSpan", "flex" }
            .Select(key => Part(node.Arguments.GetValueOrDefault(key)?.Text ?? ""))) +
        Part(node.Kind is "Content" or "Grid" ? node.Arguments["value"].Text : "") +
        Part(string.Concat(node.Children.Select(child => Part(Shape(child)))));

    internal string Emit()
    {
        styling.Validate();
        Collect(component.Root);
        var names = new HashSet<string>(StringComparer.Ordinal) { "Root", component.Name.TrimStart('@') };
        void Reserve(string name, int offset)
        {
            name = name.TrimStart('@');
            if (name.StartsWith("__xui", StringComparison.Ordinal) || !names.Add(name))
                Errors.Add(new ParseError($"Member name '{name}' is reserved or duplicated.", offset));
        }
        foreach (var state in component.States) Reserve(state.Name, state.Offset);
        foreach (var style in component.Styles) Reserve(SyntaxFactory.ParseToken(style.Name).ValueText, style.Offset);
        foreach (var parameter in component.Parameters)
        {
            Reserve(parameter.Name, parameter.Offset);
            if (parameter.Name.TrimStart('@') is "window" or "attach")
                Errors.Add(new ParseError("Parameter names 'window' and 'attach' are reserved.", parameter.Offset));
        }
        foreach (var method in SyntaxFactory.ParseCompilationUnit("class C {" + component.Code.Text + "}")
            .DescendantNodes().OfType<ClassDeclarationSyntax>().First().Members.OfType<MethodDeclarationSyntax>()
            .GroupBy(m => m.Identifier.ValueText))
            Reserve(method.Key, component.Code.Offset);
        foreach (var node in nodes)
            if (node.Arguments.TryGetValue("ref", out var reference)) Reserve(reference.Text, reference.Offset);
        Line("// <auto-generated/>");
        Line("#nullable enable");
        if (component.Namespace.Length != 0) Line($"namespace {component.Namespace};");
        Map(component.Offset);
        Line($"public sealed partial class {component.Name}");
        Unmap();
        Line("{");
        if (component.Styles.Count != 0) EmitStyles();
        Line("private readonly global::Xui.Window __xuiWindow;");
        Line($"public global::Xui.{Type(component.Root)} Root => __xuiN0;");
        Line("#if XUI_DESIGNER");
        Line($"internal int __xuiDesignerNodeCount => {nodes.Count};");
        Line("internal global::Xui.Element __xuiDesignerElement(int nodeId)");
        Line("{");
        Line("__xuiWindow.VerifyAccess();");
        Line("return nodeId switch");
        Line("{");
        for (int i = 0; i < nodes.Count; i++) Line($"{i} => __xuiN{i},");
        Line("_ => throw new global::System.ArgumentOutOfRangeException(nameof(nodeId))");
        Line("};");
        Line("}");
        Line("#endif");
        foreach (var parameter in component.Parameters)
        {
            Map(parameter.Offset);
            Line($"public {parameter.Type} {parameter.Name} {{ get; }}");
            Unmap();
        }
        for (int i = 0; i < nodes.Count; i++)
            if (nodes[i].Arguments.TryGetValue("ref", out var reference))
                Line($"public global::Xui.{Type(nodes[i])} {reference.Text} => __xuiN{i};");
        if (component.States.Count != 0) Line("private bool __xuiReady;");
        foreach (var state in component.States)
        {
            Map(state.Offset);
            Line($"private {state.Type} __xuiState_{state.Name.TrimStart('@')} =");
            Map(state.Initializer.Offset);
            Line(state.Initializer.Text + ";");
            Unmap();
            Map(state.Offset);
            Line($"public {state.Type} {state.Name}");
            Unmap();
            Line("{");
            Line($"get {{ __xuiWindow.VerifyAccess(); return __xuiState_{state.Name.TrimStart('@')}; }}");
            Line("set");
            Line("{");
            Line("__xuiWindow.VerifyAccess();");
            Line($"if (global::System.Collections.Generic.EqualityComparer<{state.Type}>.Default.Equals(__xuiState_{state.Name.TrimStart('@')}, value)) return;");
            Line($"__xuiState_{state.Name.TrimStart('@')} = value;");
            Line("if (__xuiReady) {");
            foreach (var binding in bindings.Where(b => b.Dependencies.Contains(state.Name))) Line(binding.Name + "();");
            Line("}");
            Line("}");
            Line("}");
        }
        for (int i = 0; i < nodes.Count; i++) Line($"private readonly global::Xui.{Type(nodes[i])} __xuiN{i};");
        foreach (var binding in bindings)
        {
            Line($"private {binding.Type} {binding.Name}_last = default!;");
            Line($"private bool {binding.Name}_set;");
        }
        Line("#if XUI_HOT_RELOAD");
        Line("private readonly string __xuiOriginalShape;");
        Line("private string __xuiShape() => " + Literal(Part(Shape(component.Root)) +
            Part(string.Concat(component.Resources.Select(r => Part(r.Name)))) +
            Part(string.Concat(component.Styles.Select(s => Part(s.Name)))) +
            string.Concat(component.Parameters.Select(p => Part(p.Type) + Part(p.Name))) +
            string.Concat(component.States.Select(s => Part(s.Type) + Part(s.Name) + Part(s.Initializer.Text)))) + ";");
        Line("private bool __xuiReload()");
        Line("{");
        Line("__xuiWindow.VerifyAccess();");
        Line("if (__xuiOriginalShape != __xuiShape()) return false;");
        Line("__xuiRefresh();");
        Line("return true;");
        Line("}");
        Line("#endif");
        Line($"public {component.Name}(global::Xui.Window window{string.Concat(component.Parameters.Select(p => $", {p.Type} {p.Name}"))}, bool attach = true)");
        Line("{");
        Line("global::System.ArgumentNullException.ThrowIfNull(window);");
        Line("window.VerifyAccess();");
        if (component.Root.Kind is not ("VStack" or "HStack"))
            Line("if (attach) throw new global::System.ArgumentException(\"Only a Stack root can attach to a window. Use attach: false for this component.\", nameof(attach));");
        Line("__xuiWindow = window;");
        foreach (var parameter in component.Parameters) Line($"this.{parameter.Name} = {parameter.Name};");
        for (int i = nodes.Count - 1; i >= 0; i--)
        {
            var node = nodes[i];
            string Child(int child) => $"__xuiN{nodes.IndexOf(node.Children[child])}";
            var create = node.Kind switch
            {
                "VStack" => "Stack(global::Xui.Axis.Vertical)",
                "HStack" => "Stack(global::Xui.Axis.Horizontal)",
                "Text" => "Label(\"\")",
                "Grid" => $"Grid({node.Arguments["value"].Text})",
                "ScrollView" => $"ScrollView({Child(0)}, \"\")",
                "Popup" => $"Popup(\"\", {Child(0)})",
                "SplitView" => $"SplitView(\"\", {Child(0)}, {Child(1)})",
                _ => node.Kind + "(\"\")"
            };
            if (node.Kind == "Content")
            {
                Line($"__xuiN{i} =");
                Map(node.Arguments["value"].Offset);
                Line(node.Arguments["value"].Text + ";");
                Unmap();
                Line($"global::System.ArgumentNullException.ThrowIfNull(__xuiN{i});");
            }
            else
            {
                Map(node.Arguments.GetValueOrDefault("value")?.Offset ?? node.Offset);
                Line($"__xuiN{i} = window.{create};");
                Unmap();
            }
        }
        Line("__xuiRefresh();");
        for (int i = 0; i < nodes.Count; i++)
        {
            var node = nodes[i];
            if (node.Kind is not ("VStack" or "HStack" or "Grid")) continue;
            foreach (var child in node.Children)
            {
                int childIndex = nodes.IndexOf(child);
                if (node.Kind == "Grid")
                {
                    foreach (string key in new[] { "row", "column", "rowSpan", "columnSpan" })
                    {
                        var value = child.Arguments.GetValueOrDefault(key) ?? new Expression(key.EndsWith("Span") ? "1" : "0", child.Offset);
                        Line($"int __xuiP{childIndex}_{key} =");
                        Map(value.Offset);
                        Line(value.Text + ";");
                        Unmap();
                        Line($"if (__xuiP{childIndex}_{key} < {(key.EndsWith("Span") ? 1 : 0)}) throw new global::System.ArgumentOutOfRangeException(\"{key}\");");
                    }
                    Line($"__xuiN{i}.Add(__xuiN{childIndex}, (uint)__xuiP{childIndex}_row, (uint)__xuiP{childIndex}_column, (uint)__xuiP{childIndex}_rowSpan, (uint)__xuiP{childIndex}_columnSpan);");
                }
                else
                {
                    Map(child.Arguments.GetValueOrDefault("flex")?.Offset ?? child.Offset);
                    Line($"__xuiN{i}.Add(__xuiN{childIndex}, {child.Arguments.GetValueOrDefault("flex")?.Text ?? "0"});");
                    Unmap();
                }
            }
        }
        for (int i = 0; i < nodes.Count; i++)
        {
            var node = nodes[i];
            foreach (string key in new[] { "click", "change", "submit" })
            {
                if (!node.Arguments.ContainsKey(key)) continue;
                var eventName = key switch { "click" => "Click", "change" => "Changed", _ => "Submitted" };
                Line($"__xuiN{i}.{eventName} += __xuiEvent{i}_{key};");
            }
        }
        if (component.Root.Kind is "VStack" or "HStack") Line("if (attach) window.SetContent(__xuiN0);");
        if (component.States.Count != 0) Line("__xuiReady = true;");
        Line("#if XUI_HOT_RELOAD");
        Line("__xuiOriginalShape = __xuiShape();");
        Line("global::Xui.Development.ReloadHost.Register(window, __xuiReload);");
        Line("#endif");
        Line("}");
        Line("private void __xuiRefresh()");
        Line("{");
        foreach (var binding in bindings) Line(binding.Name + "();");
        Line("}");
        foreach (var binding in bindings)
        {
            Line($"private void {binding.Name}()");
            Line("{");
            Line($"{binding.Type} __xuiValue =");
            Map(binding.Value.Offset);
            Line(binding.Value.Text + ";");
            Unmap();
            string equal = binding.Type.EndsWith("[]")
                ? $"global::System.Linq.Enumerable.SequenceEqual({binding.Name}_last, __xuiValue)"
                : binding.Name.EndsWith("_tracks")
                    ? $"(global::System.Linq.Enumerable.SequenceEqual({binding.Name}_last.Rows, __xuiValue.Rows) && global::System.Linq.Enumerable.SequenceEqual({binding.Name}_last.Columns, __xuiValue.Columns))"
                    : $"global::System.Collections.Generic.EqualityComparer<{binding.Type}>.Default.Equals({binding.Name}_last, __xuiValue)";
            Line($"if (!{binding.Name}_set || !{equal})");
            Line("{");
            Line(string.Format(System.Globalization.CultureInfo.InvariantCulture, binding.Setter, "__xuiValue") + ";");
            Line($"{binding.Name}_last = __xuiValue;");
            Line($"{binding.Name}_set = true;");
            Line("}");
            Line("}");
        }
        for (int i = 0; i < nodes.Count; i++)
        {
            foreach (string key in new[] { "click", "change", "submit" })
            {
                if (!nodes[i].Arguments.TryGetValue(key, out var handler)) continue;
                string arg = key == "change" ? (nodes[i].Kind == "Toggle" ? "bool __xuiValue" : "string __xuiValue") : "";
                Line($"private void __xuiEvent{i}_{key}({arg})");
                Line("{");
                Map(handler.Offset);
                Line(handler.Text + "(" + (arg.Length == 0 ? "" : "__xuiValue") + ");");
                Unmap();
                Line("}");
            }
        }
        Map(component.Code.Offset);
        Line(component.Code.Text);
        Unmap();
        Line("}");
        return output.ToString();
    }

    private void EmitStyles()
    {
        // A method-body revision changes under hot reload. Static field initializers do not rerun.
        Line("private static readonly object __xuiStyleLock = new();");
        Line("private static string? __xuiStyleRevision;");
        Line("private static global::System.Collections.Generic.Dictionary<string, object>? __xuiStyleCache;");
        Line("private static global::System.Collections.Generic.Dictionary<string, object> __xuiGetStyles()");
        Line("{");
        string definitions = styling.Definitions();
        string revision = Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(definitions)));
        Line("const string __xuiRevision = " + Literal(revision) + ";");
        Line("lock (__xuiStyleLock)");
        Line("{");
        Line("if (__xuiStyleCache is not null && __xuiStyleRevision == __xuiRevision) return __xuiStyleCache;");
        Line("var __xuiStyles = new global::System.Collections.Generic.Dictionary<string, object>(global::System.StringComparer.Ordinal);");
        Line(definitions);
        Line("__xuiStyleCache = __xuiStyles;");
        Line("__xuiStyleRevision = __xuiRevision;");
        Line("return __xuiStyles;");
        Line("}");
        Line("}");
    }
}
