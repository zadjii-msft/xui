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
        _ => node.Kind
    };
    private void Collect(Node node)
    {
        int index = nodes.Count;
        nodes.Add(node);
        void Bind(string name, string type, string setter, string fallback, bool staticCall = false)
        {
            var value = node.Arguments.GetValueOrDefault(name) ?? new Expression(fallback, node.Offset);
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
            bindings.Add(new($"__xuiB{index}_{name}", index, type, staticCall ? setter : $"__xuiN{index}." + setter, value,
                component.States.Where(s => identifiers.Contains(s.Name.TrimStart('@'))).Select(s => s.Name).ToArray()));
        }
        if (node.Kind is "VStack" or "HStack")
        {
            Bind("spacing", "float", "Spacing({0})", "0");
            Bind("padding", "float", "Padding({0})", "0");
        }
        else
        {
            if (node.Kind != "TextInput") Bind("value", "string", "Text = {0}", "\"\"");
            else
            {
                // Other controls alias Name and Text; only TextInput has a separate name.
                if (!node.Arguments.ContainsKey("name")) node.Arguments["name"] = node.Arguments["value"];
                Bind("name", "string", "Name = {0}", "\"\"");
            }
            Bind("id", "string", "AutomationId = {0}", "\"\"");
            Bind("enabled", "bool", "Enabled = {0}", "true");
            if (node.Kind == "Toggle") Bind("checked", "bool", "Checked = {0}", "false");
            if (node.Kind == "TextInput") Bind("text", "string", "Text = {0}", "\"\"");
            if (node.Arguments.ContainsKey("help"))
                Bind("help", "string", $"global::Xui.ControlFeatures.Help(__xuiN{index}, {{0}})", "\"\"", staticCall: true);
        }
        if (node.Arguments.ContainsKey("size"))
            Bind("size", "(float Width, float Height)",
                $"global::Xui.ElementExtensions.FixedSize(__xuiN{index}, {{0}}.Width, {{0}}.Height)", "(0, 0)", staticCall: true);
        foreach (var child in node.Children) Collect(child);
    }
    private static string Part(string value) => value.Length.ToString(System.Globalization.CultureInfo.InvariantCulture) + ":" + value;
    private string Shape(Node node) => Part(node.Kind) + Part(node.Arguments.GetValueOrDefault("id")?.Text ?? "") +
        Part(string.Join(",", node.Arguments.Keys.Where(k => k is "click" or "change" or "submit" or "size" or "help").Order())) +
        Part(string.Concat(node.Children.Select(child => Part(Shape(child)))));

    internal string Emit()
    {
        Collect(component.Root);
        Line("// <auto-generated/>");
        Line("#nullable enable");
        if (component.Namespace.Length != 0) Line($"namespace {component.Namespace};");
        Map(component.Offset);
        Line($"public sealed partial class {component.Name}");
        Unmap();
        Line("{");
        Line("private readonly global::Xui.Window __xuiWindow;");
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
            string.Concat(component.States.Select(s => Part(s.Type) + Part(s.Name) + Part(s.Initializer.Text)))) + ";");
        Line("private bool __xuiReload()");
        Line("{");
        Line("__xuiWindow.VerifyAccess();");
        Line("if (__xuiOriginalShape != __xuiShape()) return false;");
        Line("__xuiRefresh();");
        Line("return true;");
        Line("}");
        Line("#endif");
        Line($"public {component.Name}(global::Xui.Window window)");
        Line("{");
        Line("global::System.ArgumentNullException.ThrowIfNull(window);");
        Line("window.VerifyAccess();");
        Line("__xuiWindow = window;");
        for (int i = 0; i < nodes.Count; i++)
        {
            var node = nodes[i];
            var create = node.Kind switch
            {
                "VStack" => "Stack(global::Xui.Axis.Vertical)",
                "HStack" => "Stack(global::Xui.Axis.Horizontal)",
                "Text" => "Label(\"\")",
                _ => node.Kind + "(\"\")"
            };
            Line($"__xuiN{i} = window.{create};");
        }
        for (int i = 0; i < nodes.Count; i++)
            foreach (var child in nodes[i].Children) Line($"__xuiN{i}.Add(__xuiN{nodes.IndexOf(child)});");
        Line("__xuiRefresh();");
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
        Line("window.SetContent(__xuiN0);");
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
            Line($"if (!{binding.Name}_set || !global::System.Collections.Generic.EqualityComparer<{binding.Type}>.Default.Equals({binding.Name}_last, __xuiValue))");
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
}
