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
        var profiles = context.AnalyzerConfigOptionsProvider.Select((options, _) =>
            options.GlobalOptions.TryGetValue("build_property.XuiGeneratorProfile", out var profile) ? profile : "");
        context.RegisterSourceOutput(files.Combine(profiles), (output, input) =>
        {
            var (file, profile) = input;
            string suffix = Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(file.Path)))[..12];
            if (file.Text is null)
            {
                output.ReportDiagnostic(Diagnostic.Create(Invalid, Location.None, $"Cannot read '{file.Path}'."));
                output.AddSource("Xui.Error." + suffix + ".g.cs", "#error XUI001: Cannot read XUI source.\n");
                return;
            }
            try
            {
                if (profile is not ("" or "Windows" or "Portable"))
                    throw new ParseError($"Unknown XuiGeneratorProfile '{profile}'. Use Windows or Portable.", 0);
                var parser = new Parser(file.Text.ToString());
                var component = parser.Parse();
                var emitter = new Emitter(component, file.Path, file.Text, profile == "Portable");
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

internal sealed class Emitter(Component component, string path, SourceText source, bool portable = false)
{
    private string Runtime => portable ? "global::Xui.Experimental.Portable" : "global::Xui";
    private const int MaximumLineDirectiveColumn = 65536;
    internal List<ParseError> Errors { get; } = [];
    private readonly StringBuilder output = new();
    private readonly List<Node> nodes = [];
    private readonly List<Binding> bindings = [];
    private readonly StyleCompiler styling = new(component);
    private sealed record Binding(string Name, int Node, string Type, string Setter, Expression Value, string[] Dependencies,
        IReadOnlyList<Expression>? TupleItems = null);
    private void Line(string value = "") => output.AppendLine(value);
    // #line filenames do not interpret backslash escapes like C# string expressions.
    private void Map(int offset) => Line($"#line {source.Lines.GetLineFromPosition(Math.Clamp(offset, 0, source.Length)).LineNumber + 1} \"{path}\"");
    private void Map(Expression value, int generatedOffset = 0)
    {
        if (value.Text.Length == 0 || value.Offset < 0 || value.Offset > source.Length - value.Text.Length ||
            source.ToString(new TextSpan(value.Offset, value.Text.Length)) != value.Text)
        {
            Map(value.Offset);
            return;
        }
        var start = source.Lines.GetLinePosition(value.Offset);
        var end = source.Lines.GetLinePosition(value.Offset + value.Text.Length - 1);
        if (start.Character >= MaximumLineDirectiveColumn || end.Character >= MaximumLineDirectiveColumn)
        {
            Map(value.Offset);
            return;
        }
        // The generated offset is zero-based, but the directive represents zero by omission.
        string offset = generatedOffset == 0 ? "" : generatedOffset + " ";
        Line($"#line ({start.Line + 1},{start.Character + 1})-({end.Line + 1},{end.Character + 1}) {offset}\"{path}\"");
    }
    private void Unmap() => Line("#line default");
    private static string Literal(string value) => SymbolDisplay.FormatLiteral(value, true);
    private static string Type(Node node) => node.Kind switch
    {
        "VStack" or "HStack" => "Stack",
        "KeyedVStack" or "KeyedHStack" => "KeyedStack",
        "Text" => "Label",
        "Content" => "Element",
        _ => node.Kind
    };
    private static bool IsKeyed(Node node) => node.Kind is "KeyedVStack" or "KeyedHStack";
    private string[] Dependencies(Expression value)
    {
        var expression = SyntaxFactory.ParseExpression(value.Text);
        var identifiers = expression.DescendantNodesAndSelf().OfType<SimpleNameSyntax>()
            .Where(n => n.Parent is not (QualifiedNameSyntax or AliasQualifiedNameSyntax))
            .Where(n => n.Parent is not MemberAccessExpressionSyntax member || member.Name != n || member.Expression is ThisExpressionSyntax)
            .Select(n => n.Identifier.ValueText).ToHashSet();
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
            var value = node.Arguments.GetValueOrDefault(name == "items" && IsKeyed(node) ? "value" : name) ?? new Expression(fallback, node.Offset);
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
        if (node.Kind is "VStack" or "HStack" || IsKeyed(node))
        {
            if (node.Arguments.ContainsKey("spacing")) Bind("spacing", "float", "Spacing({0})", "0");
            if (node.Arguments.ContainsKey("padding")) Bind("padding", "float", "Padding({0})", "0");
            if (IsKeyed(node)) Bind("items", $"{Runtime}.KeyedItem[]", "Reconcile({0})", "[]");
        }
        else if (node.Kind is not ("Content" or "Grid" or "PageView") && !(portable && node.Kind == "Reveal"))
        {
            if (node.Kind != "TextInput")
                Bind("value", "string", node.Kind is "Text" or "Button" or "Toggle" or "ToggleSwitch" or "ToggleButton" or "CheckBox" or "HyperlinkButton" ? "Text = {0}" : "Name = {0}", "\"\"");
            else
            {
                // Other controls alias Name and Text; only TextInput has a separate name.
                if (!node.Arguments.ContainsKey("name")) node.Arguments["name"] = node.Arguments["value"];
                Bind("name", "string", "Name = {0}", "\"\"");
            }
            if (node.Arguments.ContainsKey("id")) Bind("id", "string", "AutomationId = {0}", "\"\"");
            if (node.Arguments.ContainsKey("enabled")) Bind("enabled", "bool", "Enabled = {0}", "true");
            if (node.Kind is "Toggle" or "ToggleSwitch" or "ToggleButton" && node.Arguments.ContainsKey("checked")) Bind("checked", "bool", "Checked = {0}", "false");
            if (node.Kind is "TextInput" or "MultilineText" && node.Arguments.ContainsKey("text")) Bind("text", "string", "Text = {0}", "\"\"");
            if (node.Kind == "MultilineText" && node.Arguments.ContainsKey("readOnly")) Bind("readOnly", "bool", "SetReadOnly({0})", "false");
            if (node.Kind == "NavigationView" && node.Arguments.ContainsKey("expanded")) Bind("expanded", "bool", "Expanded = {0}", "true");
            if (node.Kind == "TabStrip" && node.Arguments.ContainsKey("closable")) Bind("closable", "bool", "Closable = {0}", "false");
            if (node.Arguments.ContainsKey("visible"))
                Bind("visible", "bool", $"{Runtime}.ControlFeatures.Visible(__xuiN{index}, {{0}})", "true", staticCall: true);
            if (node.Arguments.ContainsKey("help"))
                Bind("help", "string", $"{Runtime}.ControlFeatures.Help(__xuiN{index}, {{0}})", "\"\"", staticCall: true);
        }
        else if (Dependencies(node.Arguments["value"]).Length != 0)
            Errors.Add(new ParseError($"{node.Kind} positional input cannot depend on state. This constructor input is fixed for the component lifetime.", node.Arguments["value"].Offset));
        if (node.Kind == "PageView")
        {
            var pages = node.Arguments["pages"];
            var selected = node.Arguments.GetValueOrDefault("selected") ?? new Expression("null", node.Offset);
            bindings.Add(new($"__xuiB{index}_pages", index, $"({Runtime}.PageItem[] Items, ulong? Selected)",
                $"__xuiN{index}.SetPages({{0}}.Items, {{0}}.Selected)", new($"({pages.Text}, {selected.Text})", pages.Offset),
                Dependencies(pages).Concat(Dependencies(selected)).Distinct().ToArray(), [pages, selected]));
            if (node.Arguments.TryGetValue("visible", out var visible))
                bindings.Add(new($"__xuiB{index}_pageVisibility", index, "bool", $"__xuiN{index}.Visible = {{0}}", visible, Dependencies(visible)));
        }
        if (node.Kind is "RangeInput" or "Progress" or "ProgressRing")
        {
            if (node.Arguments.TryGetValue("range", out var range) && Dependencies(range).Length != 0)
                Errors.Add(new ParseError("'range' is evaluated only during construction and cannot depend on component state.", range.Offset));
            if (node.Arguments.ContainsKey("currentValue")) Bind("currentValue", "double", "SetValue({0})", "0");
            if (node.Arguments.ContainsKey("orientation")) Bind("orientation", "global::Xui.Axis", "SetOrientation({0})", "default");
            if (node.Arguments.ContainsKey("reversed")) Bind("reversed", "bool", "SetReversed({0})", "false");
            if (node.Arguments.ContainsKey("progressState")) Bind("progressState", $"{Runtime}.ProgressState", "SetState({0})", "default");
        }
        if (node.Kind == "Image")
        {
            var imageSource = node.Arguments.GetValueOrDefault("source") ?? new Expression("null", node.Offset);
            var options = node.Arguments.GetValueOrDefault("decodeOptions") ?? new Expression($"new {Runtime}.ImageDecodeOptions()", node.Offset);
            bindings.Add(new($"__xuiB{index}_image", index,
                $"({Runtime}.PackagedImageSource? Source, {Runtime}.ImageDecodeOptions Options)",
                $"__xuiN{index}.SetImage({{0}}.Source, {{0}}.Options)",
                new($"({imageSource.Text}, {options.Text})", imageSource.Offset),
                Dependencies(imageSource).Concat(Dependencies(options)).Distinct().ToArray(), [imageSource, options]));
        }
        if (portable && node.Kind is "TextInput" or "MultilineText" or "PasswordInput")
            foreach (string key in new[] { "purpose", "maximumLength" })
                if (node.Arguments.TryGetValue(key, out var initial) && Dependencies(initial).Length != 0)
                    Errors.Add(new ParseError($"'{key}' is constructor-time and cannot depend on component state.", initial.Offset));
        if (node.Kind == "Reveal")
        {
            if (portable)
            {
                var open = node.Arguments.GetValueOrDefault("open");
                var motion = node.Arguments.GetValueOrDefault("motion");
                if (open is not null && motion is not null)
                    bindings.Add(new($"__xuiB{index}_reveal", index, $"(bool Open, {Runtime}.RevealMotion Motion)",
                        $"__xuiN{index}.SetState({{0}}.Open, {{0}}.Motion)", new($"({open.Text}, {motion.Text})", open.Offset),
                        Dependencies(open).Concat(Dependencies(motion)).Distinct().ToArray(), [open, motion]));
                else if (open is not null)
                    bindings.Add(new($"__xuiB{index}_reveal", index, "bool", $"__xuiN{index}.SetOpen({{0}})", open, Dependencies(open)));
                else if (motion is not null)
                    bindings.Add(new($"__xuiB{index}_reveal", index, $"{Runtime}.RevealMotion", $"__xuiN{index}.SetMotion({{0}})", motion, Dependencies(motion)));
            }
            else
            {
                if (node.Arguments.ContainsKey("duration")) Bind("duration", "uint", "SetDuration({0})", "0");
                if (node.Arguments.ContainsKey("layout")) Bind("layout", "global::Xui.RevealLayout", "SetLayout({0})", "default");
                if (node.Arguments.ContainsKey("direction")) Bind("direction", "global::Xui.RevealDirection", "SetDirection({0})", "default");
                if (node.Arguments.ContainsKey("open")) Bind("open", "bool", "SetOpen({0})", "false");
            }
        }
        if (node.Kind == "SplitView" && node.Arguments.ContainsKey("duration"))
            Bind("duration", "uint", "SetTransitionDuration({0})", "0");
        if (node.Kind == "CheckBox")
        {
            if (node.Arguments.ContainsKey("threeState")) Bind("threeState", "bool", "SetThreeState({0})", "false");
            if (node.Arguments.ContainsKey("checkState")) Bind("checkState", $"{Runtime}.CheckState", "SetState({0})", "default");
        }
        if (node.Kind == "InfoBadge")
        {
            if (node.Arguments.ContainsKey("count") && node.Arguments.ContainsKey("icon"))
                throw new ParseError("InfoBadge accepts either count or icon, not both.", node.Offset);
            if (node.Arguments.ContainsKey("count")) Bind("count", "uint", "SetCount({0})", "0");
        }
        if (node.Kind is "SelectorBar" or "SingleChoice")
        {
            if (node.Arguments.TryGetValue("items", out var items))
            {
                var selected = node.Arguments.GetValueOrDefault("selected") ?? new Expression("null", node.Offset);
                bindings.Add(new($"__xuiB{index}_choices", index,
                    $"({(node.Kind == "SingleChoice" ? Runtime : "global::Xui")}.Choice[] Items, ulong? Selected)", $"__xuiN{index}.SetItems({{0}}.Items, {{0}}.Selected)",
                    new($"({items.Text}, {selected.Text})", items.Offset),
                    Dependencies(items).Concat(Dependencies(selected)).Distinct().ToArray(), [items, selected]));
            }
            else if (node.Arguments.ContainsKey("selected"))
            {
                if (node.Kind == "SingleChoice") throw new ParseError("SingleChoice selected requires an items snapshot.", node.Arguments["selected"].Offset);
                Bind("selected", "ulong", "SetSelected({0})", "0");
            }
        }
        if (node.Kind == "MenuBar" && node.Arguments.ContainsKey("commands"))
            Bind("commands", "global::Xui.Command[]", "SetCommands({0})", "[]");
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
            var untypedLocal = node.Arguments.FirstOrDefault(pair => !(portable && pair.Key is "width" or "height" or "fontSize" or "fontWeight") &&
                (StyleCompiler.Properties.Contains(pair.Key) || StyleCompiler.ExtendedProperties.Contains(pair.Key)));
            if (untypedLocal.Value is not null)
                throw new ParseError("Content style properties require a named style to identify the target schema.", untypedLocal.Value.Offset);
        }
        if (styleValue is not null) {
            string reference = styling.Reference(styleValue, styleTarget);
            if (!genericStyle && node.Kind is not ("Button" or "ToggleButton" or "HyperlinkButton"))
                throw new ParseError("A legacy Button style requires a Button node.", styleValue.Offset);
            bindings.Add(new($"__xuiB{index}_style", index, genericStyle ? "global::Xui.ControlStyle" : "global::Xui.ButtonStyle",
                genericStyle ? $"__xuiN{index}.SetControlStyle({{0}})" : $"__xuiN{index}.Style = {{0}}",
                new(reference, styleValue.Offset), []));
        }
        if (styleTarget == "Button" || StyleCatalog.TargetExists(styleTarget)) {
            var local = node.Arguments.Where(pair =>
                (StyleCompiler.Properties.Contains(pair.Key) || StyleCompiler.ExtendedProperties.Contains(pair.Key)) &&
                !(portable && pair.Key is "width" or "height" or "fontSize" or "fontWeight") &&
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
            string tracks = $"new {Runtime}.GridTrack[] {{ new({Runtime}.TrackSizing.Star, 1) }}";
            var rows = node.Arguments.GetValueOrDefault("rows") ?? new Expression(tracks, node.Offset);
            var columns = node.Arguments.GetValueOrDefault("columns") ?? new Expression(tracks, node.Offset);
            bindings.Add(new($"__xuiB{index}_tracks", index,
                $"({Runtime}.GridTrack[] Rows, {Runtime}.GridTrack[] Columns)",
                $"__xuiN{index}.SetTracks({{0}}.Rows, {{0}}.Columns)",
                new($"({rows.Text}, {columns.Text})", rows.Offset),
                Dependencies(rows).Concat(Dependencies(columns)).Distinct().ToArray(), [rows, columns]));
        }
        if (node.Kind == "DataGrid" && node.Arguments.ContainsKey("columns"))
            Bind("columns", "global::Xui.GridColumn[]", "SetColumns({0})", "[]");
        if (node.Kind == "NavigationView")
        {
            if (node.Arguments.ContainsKey("duration"))
                Bind("duration", "uint", "SetDuration({0})", "0");
            if (node.Arguments.ContainsKey("searchId"))
                Bind("searchId", "string", "Search.AutomationId = {0}", "\"\"");
            if (node.Arguments.ContainsKey("searchHelp"))
                Bind("searchHelp", "string", $"global::Xui.ControlFeatures.Help(__xuiN{index}.Search, {{0}})", "\"\"", staticCall: true);
        }
        if (node.Arguments.ContainsKey("size"))
            Bind("size", "(float Width, float Height)",
                $"{Runtime}.ElementExtensions.FixedSize(__xuiN{index}, {{0}}.Width, {{0}}.Height)", "(0, 0)", staticCall: true);
        if (node.Arguments.ContainsKey("preferredSize"))
            Bind("preferredSize", "(float Width, float Height)",
                $"{Runtime}.ElementExtensions.PreferredSize(__xuiN{index}, {{0}}.Width, {{0}}.Height)", "(0, 0)", staticCall: true);
        if (portable)
        {
            if (node.Kind == "Text" && node.Arguments.ContainsKey("textLayout"))
                Bind("textLayout", $"{Runtime}.LabelTextLayout?", "SetTextLayout({0})", "null");
            var typography = new[] { "textRole", "fontSize", "fontWeight" }
                .Where(node.Arguments.ContainsKey).Select(key => node.Arguments[key]).ToArray();
            if (typography.Length != 0)
            {
                var role = node.Arguments.GetValueOrDefault("textRole") ?? new Expression($"{Runtime}.TextRole.Body", node.Offset);
                var fontSize = node.Arguments.GetValueOrDefault("fontSize") ?? new Expression("null", node.Offset);
                var fontWeight = node.Arguments.GetValueOrDefault("fontWeight") ?? new Expression("null", node.Offset);
                bindings.Add(new($"__xuiB{index}_typography", index, $"{Runtime}.Typography?",
                    $"__xuiN{index}.SetTypography({{0}})",
                    new($"new {Runtime}.Typography({role.Text}, {fontSize.Text}, {fontWeight.Text})", typography[0].Offset),
                    typography.SelectMany(Dependencies).Distinct().ToArray()));
            }
            var width = node.Arguments.GetValueOrDefault("width");
            var height = node.Arguments.GetValueOrDefault("height");
            if (width is not null && height is not null)
                bindings.Add(new($"__xuiB{index}_constraints", index,
                    $"({Runtime}.AxisConstraints? Width, {Runtime}.AxisConstraints? Height)",
                    $"__xuiN{index}.SetConstraints({{0}}.Width, {{0}}.Height)",
                    new($"({width.Text}, {height.Text})", width.Offset),
                    Dependencies(width).Concat(Dependencies(height)).Distinct().ToArray(), [width, height]));
            else if (width is not null) Bind("width", $"{Runtime}.AxisConstraints?", "SetWidth({0})", "null");
            else if (height is not null) Bind("height", $"{Runtime}.AxisConstraints?", "SetHeight({0})", "null");
        }
        foreach (var child in node.Children) Collect(child, node);
    }
    private static string Part(string value) => value.Length.ToString(System.Globalization.CultureInfo.InvariantCulture) + ":" + value;
    private string Shape(Node node) => Part(node.Kind) + Part(node.Arguments.GetValueOrDefault("id")?.Text ?? "") +
        Part(node.Arguments.GetValueOrDefault("searchId")?.Text ?? "") +
        Part(string.Join(",", node.Arguments.Keys.Order())) +
        string.Concat(new[] { "ref", "row", "column", "rowSpan", "columnSpan", "flex" }
            .Select(key => Part(node.Arguments.GetValueOrDefault(key)?.Text ?? ""))) +
        Part(node.Kind is "Content" or "Grid" ? node.Arguments["value"].Text : "") +
        (node.Kind is "RangeInput" or "Progress" or "ProgressRing" ? Part(node.Arguments.GetValueOrDefault("range")?.Text ?? "") : "") +
        Part(string.Concat(node.Children.Select(child => Part(Shape(child)))));

    internal string Emit()
    {
        if (portable) ValidatePortable();
        else
        {
            void RejectKeyed(Node node)
            {
                if (IsKeyed(node)) throw new ParseError("Keyed composition requires the Portable profile and a mutable backend.", node.Offset);
                if (node.Kind == "Image") throw new ParseError("Packaged Image declarations require the Portable profile and an image backend.", node.Offset);
                if (node.Kind == "Reveal" && node.Arguments.ContainsKey("motion"))
                    throw new ParseError("The motion record requires the Portable profile; Windows Reveal retains its existing duration/direction/layout arguments.", node.Arguments["motion"].Offset);
                if (node.Kind is "PageView" or "TabStrip" || node.Kind == "NavigationView" && node.Arguments.ContainsKey("pages"))
                    throw new ParseError("Linked page navigation requires the Portable profile and retained page support.", node.Offset);
                if (node.Kind == "SingleChoice" || (node.Kind == "RangeInput" && (node.Arguments.ContainsKey("preview") || node.Arguments.ContainsKey("cancel"))))
                    throw new ParseError("SingleChoice and authored range preview/cancel require the Portable profile.", node.Offset);
                if (node.Kind is "MultilineText" or "PasswordInput" || node.Arguments.ContainsKey("purpose"))
                    throw new ParseError("These native editor declarations require the Portable profile and a forms backend.", node.Offset);
                foreach (string axis in new[] { "width", "height" })
                    if (node.Arguments.TryGetValue(axis, out var constraint))
                        throw new ParseError("Per-axis constraints require the Portable profile and a constrained backend.", constraint.Offset);
                if (node.Arguments.TryGetValue("textRole", out var role))
                    throw new ParseError("Semantic text roles require the Portable profile and a presentation backend.", role.Offset);
                if (node.Arguments.TryGetValue("textLayout", out var textLayout))
                    throw new ParseError("Explicit label text layout requires the Portable profile and a text-layout backend.", textLayout.Offset);
                foreach (var child in node.Children) RejectKeyed(child);
            }
            RejectKeyed(component.Root);
        }
        styling.Validate();
        Collect(component.Root);
        var names = new HashSet<string>(StringComparer.Ordinal) { "Root", component.Name.TrimStart('@') };
        void Reserve(string name, int offset)
        {
            name = name.TrimStart('@');
            if (name.StartsWith("__xui", StringComparison.Ordinal) || !names.Add(name))
                Errors.Add(new ParseError($"Member name '{name}' is reserved or duplicated.", offset));
        }
        if (portable) Reserve("Lifetime", component.Offset);
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
        Line($"public sealed partial class {component.Name}{(portable ? $" : {Runtime}.IPortableComponent" : "")}");
        Unmap();
        Line("{");
        if (component.Styles.Count != 0) EmitStyles();
        Line($"private readonly {Runtime}.{(portable ? "Host" : "Window")} __xuiWindow;");
        Line($"public {Runtime}.{Type(component.Root)} Root => __xuiN0;");
        if (portable) Line($"{Runtime}.Element {Runtime}.IPortableComponent.Root => Root;");
        if (portable) Line($"public {Runtime}.ComponentLifetime Lifetime => __xuiWindow.GetComponentLifetime(__xuiN0);");
        if (!portable)
        {
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
        }
        foreach (var parameter in component.Parameters)
        {
            Map(parameter.Offset);
            Line($"public {parameter.Type} {parameter.Name} {{ get; }}");
            Unmap();
        }
        for (int i = 0; i < nodes.Count; i++)
            if (nodes[i].Arguments.TryGetValue("ref", out var reference))
                Line($"public {Runtime}.{Type(nodes[i])} {reference.Text} => __xuiN{i};");
        if (component.States.Count != 0) Line("private bool __xuiReady;");
        foreach (var state in component.States)
        {
            Map(state.Offset);
            Line($"private {state.Type} __xuiState_{state.Name.TrimStart('@')} =");
            Map(state.Initializer);
            Line(state.Initializer.Text + ";");
            Unmap();
            Map(state.Offset);
            Line($"public {state.Type} {state.Name}");
            Unmap();
            Line("{");
            Line($"get {{ __xuiWindow.{(portable ? "VerifyComponent(__xuiN0)" : "VerifyAccess()")}; return __xuiState_{state.Name.TrimStart('@')}; }}");
            Line("set");
            Line("{");
            Line(portable ? "__xuiWindow.VerifyComponentMutation(__xuiN0);" : "__xuiWindow.VerifyAccess();");
            Line($"if (global::System.Collections.Generic.EqualityComparer<{state.Type}>.Default.Equals(__xuiState_{state.Name.TrimStart('@')}, value)) return;");
            var keyedBinding = portable ? bindings.SingleOrDefault(b => (b.Name.EndsWith("_items", StringComparison.Ordinal) ||
                b.Name.EndsWith("_pages", StringComparison.Ordinal) || b.Name.EndsWith("_pageVisibility", StringComparison.Ordinal) ||
                b.Name.EndsWith("_reveal", StringComparison.Ordinal)) && b.Dependencies.Contains(state.Name)) : null;
            if (keyedBinding is not null) Line($"var __xuiPrevious = __xuiState_{state.Name.TrimStart('@')};");
            Line($"__xuiState_{state.Name.TrimStart('@')} = value;");
            Line("if (__xuiReady) {");
            if (keyedBinding is not null)
            {
                Line("try { " + keyedBinding.Name + "(); }");
                Line($"catch ({Runtime}.KeyedUpdateException __xuiError) when (!__xuiError.ModelCommitted)");
                Line("{");
                Line($"__xuiState_{state.Name.TrimStart('@')} = __xuiPrevious;");
                Line("throw;");
                Line("}");
            }
            foreach (var binding in bindings.Where(b => b != keyedBinding && b.Dependencies.Contains(state.Name))) Line(binding.Name + "();");
            Line("}");
            Line("}");
            Line("}");
        }
        for (int i = 0; i < nodes.Count; i++) Line($"private readonly {Runtime}.{Type(nodes[i])} __xuiN{i};");
        foreach (var binding in bindings)
        {
            Line($"private {binding.Type} {binding.Name}_last = default!;");
            Line($"private bool {binding.Name}_set;");
        }
        if (!portable)
        {
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
        }
        Line($"public {component.Name}({Runtime}.{(portable ? "Host" : "Window")} window{string.Concat(component.Parameters.Select(p => $", {p.Type} {p.Name}"))}{(portable ? "" : ", bool attach = true")})");
        Line("{");
        Line("global::System.ArgumentNullException.ThrowIfNull(window);");
        Line("window.VerifyAccess();");
        if (portable)
        {
            Line("var __xuiBuild = window.BeginBuild();");
            Line("try {");
        }
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
                "VStack" => $"Stack({Runtime}.Axis.Vertical)",
                "HStack" => $"Stack({Runtime}.Axis.Horizontal)",
                "KeyedVStack" => $"KeyedStack({Runtime}.Axis.Vertical)",
                "KeyedHStack" => $"KeyedStack({Runtime}.Axis.Horizontal)",
                "Text" => "Label(\"\")",
                "TextInput" when portable && node.Arguments.TryGetValue("purpose", out var purpose) => $"TextInput(\"\", {purpose.Text})",
                "MultilineText" => $"MultilineText(\"\", {node.Arguments.GetValueOrDefault("maximumLength")?.Text ?? "65536"})",
                "PasswordInput" => $"PasswordInput(\"\", {node.Arguments.GetValueOrDefault("maximumLength")?.Text ?? "256"})",
                "Grid" => $"Grid({node.Arguments["value"].Text})",
                "PageView" => $"PageView({node.Arguments["value"].Text})",
                "ScrollView" => $"ScrollView({Child(0)}, \"\")",
                "Reveal" => portable ? $"Reveal({Child(0)}, {node.Arguments["value"].Text})" : $"Reveal({Child(0)}, \"\")",
                "Popup" => $"Popup(\"\", {Child(0)})",
                "SplitView" => $"SplitView(\"\", {Child(0)}, {Child(1)})",
                _ => node.Kind + "(\"\")"
            };
            if (node.Kind == "Content")
            {
                Line($"__xuiN{i} =");
                Map(node.Arguments["value"]);
                Line(node.Arguments["value"].Text + ";");
                Unmap();
                Line($"global::System.ArgumentNullException.ThrowIfNull(__xuiN{i});");
            }
            else
            {
                if (node.Kind == "Grid") Map(node.Arguments["value"], $"__xuiN{i} = window.Grid(".Length);
                else Map(node.Arguments.GetValueOrDefault("value")?.Offset ?? node.Offset);
                Line($"__xuiN{i} = window.{create};");
                Unmap();
            }
            if (node.Kind is "RangeInput" or "Progress" or "ProgressRing" && node.Arguments.TryGetValue("range", out var range))
            {
                Line($"{Runtime}.NumericRange __xuiRange{i} =");
                Map(range);
                Line(range.Text + ";");
                Unmap();
                Line($"__xuiN{i}.SetRange(__xuiRange{i});");
            }
        }
        Line("__xuiRefresh();");
        if (portable)
            for (int i = 0; i < nodes.Count; i++)
                if (nodes[i].Kind is "TabStrip" or "NavigationView")
                {
                    var target = nodes[i].Arguments["pages"];
                    Map(target);
                    Line($"__xuiN{i}.BindPages({target.Text});");
                    Unmap();
                }
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
                        Map(value);
                        Line(value.Text + ";");
                        Unmap();
                        Line($"if (__xuiP{childIndex}_{key} < {(key.EndsWith("Span") ? 1 : 0)}) throw new global::System.ArgumentOutOfRangeException(\"{key}\");");
                    }
                    Line($"__xuiN{i}.Add(__xuiN{childIndex}, (uint)__xuiP{childIndex}_row, (uint)__xuiP{childIndex}_column, (uint)__xuiP{childIndex}_rowSpan, (uint)__xuiP{childIndex}_columnSpan);");
                }
                else
                {
                    var flex = child.Arguments.GetValueOrDefault("flex") ?? new Expression("0", child.Offset);
                    string prefix = $"__xuiN{i}.Add(__xuiN{childIndex}, ";
                    Map(flex, prefix.Length);
                    Line(prefix + flex.Text + ");");
                    Unmap();
                }
            }
        }
        for (int i = 0; i < nodes.Count; i++)
        {
            var node = nodes[i];
            foreach (string key in new[] { "click", "change", "submit", "invoke", "pin", "preview", "cancel", "activate", "close", "stateChanged" })
            {
                if (!node.Arguments.ContainsKey(key)) continue;
                var eventName = key switch { "click" => "Click", "change" => "Changed", "invoke" => "Invoked", "pin" => "Pinned", "preview" => "Previewed", "cancel" => "Canceled", "activate" => "Activated", "close" => "CloseRequested", "stateChanged" => "StateChanged", _ => "Submitted" };
                if (!portable && node.Kind == "RangeInput" && key == "change")
                    Line($"__xuiN{i}.OnChange(__xuiEvent{i}_{key});");
                else Line($"__xuiN{i}.{eventName} += __xuiEvent{i}_{key};");
            }
        }
        if (component.Root.Kind is "VStack" or "HStack") Line(portable ? "window.SetContent(__xuiN0);" : "if (attach) window.SetContent(__xuiN0);");
        if (component.States.Count != 0) Line("__xuiReady = true;");
        if (portable)
        {
            Line("__xuiBuild.Complete();");
            Line("} catch (global::System.Exception __xuiConstructionError) {");
            Line("try { __xuiBuild.Dispose(); }");
            Line("catch (global::System.Exception __xuiCleanupError) { throw new global::System.AggregateException(__xuiConstructionError, __xuiCleanupError); }");
            Line("throw;");
            Line("}");
        }
        else
        {
        Line("#if XUI_HOT_RELOAD");
        Line("__xuiOriginalShape = __xuiShape();");
        Line("global::Xui.Development.ReloadHost.Register(window, __xuiReload);");
        Line("#endif");
        }
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
            if (binding.TupleItems is { } items)
            {
                Line("(");
                for (int i = 0; i < items.Count; i++)
                {
                    Map(items[i]);
                    Line(items[i].Text + (i + 1 < items.Count ? "," : ""));
                    Unmap();
                }
                Line(");");
            }
            else
            {
                Map(binding.Value);
                Line(binding.Value.Text + ";");
                Unmap();
            }
            string equal = binding.Name.EndsWith("_pages", StringComparison.Ordinal)
                ? $"(global::System.Object.ReferenceEquals({binding.Name}_last.Items, __xuiValue.Items) && {binding.Name}_last.Selected == __xuiValue.Selected)"
                : binding.Name.EndsWith("_items", StringComparison.Ordinal)
                ? $"global::System.Object.ReferenceEquals({binding.Name}_last, __xuiValue)"
                : binding.Type.EndsWith("[]")
                ? $"global::System.Linq.Enumerable.SequenceEqual({binding.Name}_last, __xuiValue)"
                : binding.Name.EndsWith("_choices")
                    ? $"({(portable ? "__xuiValue.Items is not null && " : "")}{binding.Name}_last.Selected == __xuiValue.Selected && global::System.Linq.Enumerable.SequenceEqual({binding.Name}_last.Items, __xuiValue.Items))"
                : binding.Name.EndsWith("_tracks")
                    ? $"({(portable ? "__xuiValue.Rows is not null && __xuiValue.Columns is not null && " : "")}global::System.Linq.Enumerable.SequenceEqual({binding.Name}_last.Rows, __xuiValue.Rows) && global::System.Linq.Enumerable.SequenceEqual({binding.Name}_last.Columns, __xuiValue.Columns))"
                    : $"global::System.Collections.Generic.EqualityComparer<{binding.Type}>.Default.Equals({binding.Name}_last, __xuiValue)";
            if (portable && nodes[binding.Node].Kind == "SingleChoice" && binding.Name.EndsWith("_choices", StringComparison.Ordinal))
                Line("global::System.ArgumentNullException.ThrowIfNull(__xuiValue.Items);");
            Line($"if (!{binding.Name}_set || !{equal})");
            Line("{");
            bool keyed = portable && (binding.Name.EndsWith("_items", StringComparison.Ordinal) || binding.Name.EndsWith("_pages", StringComparison.Ordinal) ||
                binding.Name.EndsWith("_pageVisibility", StringComparison.Ordinal) || binding.Name.EndsWith("_reveal", StringComparison.Ordinal));
            string? committedModel = portable ? binding.Name.Split('_').Last() switch
            {
                "width" => $"__xuiN{binding.Node}.WidthConstraints == __xuiValue",
                "height" => $"__xuiN{binding.Node}.HeightConstraints == __xuiValue",
                "constraints" => $"(__xuiN{binding.Node}.WidthConstraints, __xuiN{binding.Node}.HeightConstraints) == __xuiValue",
                "tracks" => $"__xuiValue.Rows is not null && __xuiValue.Columns is not null && global::System.Linq.Enumerable.SequenceEqual(__xuiN{binding.Node}.Rows, __xuiValue.Rows) && global::System.Linq.Enumerable.SequenceEqual(__xuiN{binding.Node}.Columns, __xuiValue.Columns)",
                "typography" => $"__xuiN{binding.Node}.Typography == __xuiValue",
                "textLayout" => $"__xuiN{binding.Node}.TextLayout == __xuiValue",
                "image" => $"global::System.Object.ReferenceEquals(__xuiN{binding.Node}.Source, __xuiValue.Source) && global::System.Object.ReferenceEquals(__xuiN{binding.Node}.DecodeOptions, __xuiValue.Options)",
                "choices" when nodes[binding.Node].Kind == "SingleChoice" =>
                    $"__xuiValue.Items is not null && global::System.Linq.Enumerable.SequenceEqual(__xuiN{binding.Node}.Items, __xuiValue.Items) && (!__xuiValue.Selected.HasValue || __xuiN{binding.Node}.Selected == __xuiValue.Selected)",
                "currentValue" when nodes[binding.Node].Kind == "RangeInput" => $"__xuiN{binding.Node}.Value == __xuiValue",
                _ => null
            } : null;
            if (keyed || committedModel is not null) Line("try {");
            Line(string.Format(System.Globalization.CultureInfo.InvariantCulture, binding.Setter, "__xuiValue") + ";");
            if (keyed)
            {
                Line($"}} catch ({Runtime}.KeyedUpdateException __xuiError) when (__xuiError.ModelCommitted) {{");
                Line($"{binding.Name}_last = __xuiValue;");
                Line($"{binding.Name}_set = true;");
                Line("throw;");
                Line("}");
            }
            else if (committedModel is not null)
            {
                Line("} catch {");
                Line($"if ({committedModel}) {{");
                Line($"{binding.Name}_last = __xuiValue;");
                Line($"{binding.Name}_set = true;");
                Line("}");
                Line("throw;");
                Line("}");
            }
            Line($"{binding.Name}_last = __xuiValue;");
            Line($"{binding.Name}_set = true;");
            Line("}");
            Line("}");
        }
        for (int i = 0; i < nodes.Count; i++)
        {
            foreach (string key in new[] { "click", "change", "submit", "invoke", "pin", "preview", "cancel", "activate", "close", "stateChanged" })
            {
                if (!nodes[i].Arguments.TryGetValue(key, out var handler)) continue;
                string arg = key == "change" ? nodes[i].Kind switch
                {
                    "PasswordInput" => "",
                    "Toggle" or "ToggleSwitch" or "ToggleButton" => "bool __xuiValue",
                    "CheckBox" => $"{Runtime}.CheckState __xuiValue",
                    "SelectorBar" or "SingleChoice" or "TabStrip" or "NavigationView" => "ulong __xuiValue",
                    "RangeInput" => "double __xuiValue",
                    _ => "string __xuiValue"
                } : key is "invoke" or "pin" or "activate" or "close" ? "ulong __xuiValue" : key is "preview" or "cancel" ? "double __xuiValue" :
                    key == "stateChanged" ? $"{Runtime}.ImageLoadState __xuiValue" : "";
                Line($"private void __xuiEvent{i}_{key}({arg})");
                Line("{");
                Map(handler);
                Line(handler.Text + "(" + (arg.Length == 0 ? "" : "__xuiValue") + ");");
                Unmap();
                Line("}");
            }
        }
        Map(component.Code);
        Line(component.Code.Text);
        Unmap();
        Line("}");
        return output.ToString();
    }

    private void ValidatePortable()
    {
        if (component.Root.Kind is not ("VStack" or "HStack"))
            throw new ParseError("The Portable profile requires a VStack or HStack root.", component.Root.Offset);
        if (component.Styles.Count != 0)
            throw new ParseError("The Portable profile does not support styles.", component.Styles[0].Offset);
        if (component.Resources.Count != 0)
            throw new ParseError("The Portable profile does not support resources.", component.Resources[0].Offset);
        var keyedStates = new HashSet<string>(StringComparer.Ordinal);
        var pageReferences = new HashSet<string>(StringComparer.Ordinal);
        void FindPageReferences(Node node)
        {
            if (node.Kind == "PageView" && node.Arguments.TryGetValue("ref", out var reference)) pageReferences.Add(reference.Text);
            foreach (var child in node.Children) FindPageReferences(child);
        }
        FindPageReferences(component.Root);
        void Visit(Node node)
        {
            string[] specific = node.Kind switch
            {
                "VStack" or "HStack" => ["spacing", "padding"],
                "KeyedVStack" or "KeyedHStack" => ["value", "spacing", "padding"],
                "Content" => ["value"],
                "Grid" => ["value", "rows", "columns"],
                "Text" => ["value", "textLayout"],
                "Button" => ["value", "click"],
                "Toggle" => ["value", "checked", "change"],
                "CheckBox" => ["value", "checkState", "threeState", "change"],
                "Progress" => ["value", "range", "currentValue", "progressState"],
                "SingleChoice" => ["value", "items", "selected", "change"],
                "RangeInput" => ["value", "range", "currentValue", "change", "preview", "cancel"],
                "PageView" => ["value", "pages", "selected", "visible"],
                "TabStrip" => ["value", "pages", "change", "activate", "close", "closable"],
                "NavigationView" => ["value", "pages", "change", "activate", "expanded"],
                "TextInput" => ["value", "name", "text", "change", "submit", "captionVisible", "placeholder"],
                "MultilineText" => ["value", "text", "change", "readOnly", "maximumLength"],
                "PasswordInput" => ["value", "change", "maximumLength"],
                "Image" => ["value", "source", "decodeOptions", "stateChanged"],
                "ScrollView" => ["value"],
                "Reveal" => ["value", "open", "motion"],
                _ => throw new ParseError($"The Portable profile does not support '{node.Kind}'.", node.Offset)
            };
            var allowed = new HashSet<string>(specific, StringComparer.Ordinal) { "size", "preferredSize", "width", "height", "ref", "flex", "row", "column", "rowSpan", "columnSpan" };
            if (node.Kind == "TextInput") allowed.Add("purpose");
            if (node.Kind is "Text" or "Button" or "TextInput" or "Toggle" or "CheckBox")
                allowed.UnionWith(["textRole", "fontSize", "fontWeight"]);
            if (node.Kind is not ("VStack" or "HStack" or "KeyedVStack" or "KeyedHStack" or "Content" or "Grid" or "PageView" or "Reveal"))
                allowed.UnionWith(["id", "enabled", "visible", "help"]);
            foreach (var argument in node.Arguments)
                if (!allowed.Contains(argument.Key))
                    throw new ParseError($"The Portable profile does not support '{argument.Key}' on {node.Kind}.", argument.Value.Offset);
            if (IsKeyed(node))
            {
                var value = node.Arguments["value"];
                if (SyntaxFactory.ParseExpression(value.Text) is not IdentifierNameSyntax identifier ||
                    !component.States.Select(s => s.Name.TrimStart('@')).Concat(component.Parameters.Select(p => p.Name.TrimStart('@'))).Contains(identifier.Identifier.ValueText))
                    throw new ParseError("Keyed items must directly name a KeyedItem[] state or constructor parameter.", value.Offset);
                string name = identifier.Identifier.ValueText;
                if (component.States.Any(state => state.Name.TrimStart('@') == name) && !keyedStates.Add(name))
                    throw new ParseError("A keyed descriptor state may drive only one container.", value.Offset);
            }
            if (node.Kind is "PageView" or "TabStrip" or "NavigationView" && !node.Arguments.ContainsKey("pages"))
                throw new ParseError($"{node.Kind} requires its pages argument.", node.Offset);
            if (node.Kind is "TabStrip" or "NavigationView")
            {
                var value = node.Arguments["pages"];
                if (!pageReferences.Contains(value.Text))
                    throw new ParseError("A page selector must link a PageView ref in this component.", value.Offset);
            }
            if (node.Kind == "PageView")
            {
                var value = node.Arguments["pages"];
                var selected = node.Arguments.GetValueOrDefault("selected") ?? new Expression("null", node.Offset);
                var dependencies = Dependencies(value).Concat(Dependencies(selected)).Distinct().ToArray();
                if (dependencies.Length > 1)
                    throw new ParseError("Page items and selected ID must use one shared state snapshot.", value.Offset);
                if (dependencies.Length == 1 && !keyedStates.Add(dependencies[0].TrimStart('@')))
                    throw new ParseError("A structural state snapshot may drive only one keyed or page container.", value.Offset);
                if (node.Arguments.TryGetValue("visible", out var visibility))
                {
                    var visibilityStates = Dependencies(visibility);
                    if (visibilityStates.Any(state => !keyedStates.Add(state.TrimStart('@'))))
                        throw new ParseError("Page visibility requires a state independent of other page or keyed transactions.", visibility.Offset);
                }
            }
            if (node.Kind == "Reveal")
            {
                foreach (string size in new[] { "size", "preferredSize", "width", "height" })
                    if (node.Arguments.TryGetValue(size, out var unsupported))
                        throw new ParseError("Portable Reveal does not support outer sizing; size its retained child instead.", unsupported.Offset);
                if (node.Arguments.TryGetValue("flex", out var flex))
                {
                    var value = SyntaxFactory.ParseExpression(flex.Text);
                    if (value is not LiteralExpressionSyntax literal || !literal.IsKind(SyntaxKind.NumericLiteralExpression) ||
                        literal.Token.Value is not IConvertible number ||
                        number.ToDouble(System.Globalization.CultureInfo.InvariantCulture) != 0)
                        throw new ParseError("Portable Reveal supports only literal zero flex; size its retained child instead.", flex.Offset);
                }
                var dependencies = new[] { "open", "motion" }.Where(node.Arguments.ContainsKey)
                    .SelectMany(key => Dependencies(node.Arguments[key])).Distinct();
                if (dependencies.Any(state => !keyedStates.Add(state.TrimStart('@'))))
                    throw new ParseError("Reveal state must be independent of other native preflight transactions.", node.Offset);
            }
            foreach (var child in node.Children) Visit(child);
        }
        Visit(component.Root);
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
