using System.Globalization;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.CSharp.Syntax;

namespace Xui.Generator;

internal sealed record ColorResource(string Name, Expression Value, int Offset);
internal sealed record StyleRule(string State, Dictionary<string, Expression> Values, string Part = "root");
internal sealed record StyleDefinition(string Name, string? BasedOn, int Offset,
    Dictionary<string, Expression> Values, List<StyleRule> Rules, string Target,
    Dictionary<string, Dictionary<string, Expression>> Parts);

internal sealed partial class Parser
{
    private Expression ReadStyleExpression()
    {
        int start = position;
        var expression = SyntaxFactory.ParseExpression(text, position, consumeFullText: false);
        Check(expression, start);
        if (expression.IsMissing) throw new ParseError("Expected a style value.", Offset);
        position += expression.FullSpan.Length;
        return new(expression.ToString(), start + expression.SpanStart);
    }

    private void ReadStyleProperty(Dictionary<string, Expression> values, string target, string part, bool stateRule = false)
    {
        int start = Offset;
        string key = Identifier();
        if (!StyleCompiler.AllowedProperties(target, part, stateRule).Contains(key))
            throw new ParseError($"Unsupported {target} style property '{key}' on part '{part}'.", start);
        Expect(":");
        if (!values.TryAdd(key, ReadStyleExpression())) throw new ParseError($"Duplicate style property '{key}'.", start);
        Expect(";");
    }

    private StyleDefinition ParseStyle()
    {
        Take();
        int start = Offset;
        string name = Identifier();
        Expect("for");
        string target = StyleCompiler.TargetName(Identifier());
        if (target == "Tooltip")
            throw new ParseError("Tooltip styles require the Window API. The .xui compiler cannot apply them.", start);
        if (target != "Button" && !StyleCatalog.TargetExists(target)) throw new ParseError($"Unsupported style target '{target}'.", start);
        string? basedOn = null;
        if (Is("basedOn")) { Take(); basedOn = Identifier(); }
        Expect("{");
        var values = new Dictionary<string, Expression>(StringComparer.Ordinal);
        var rules = new List<StyleRule>();
        var parts = new Dictionary<string, Dictionary<string, Expression>>(StringComparer.Ordinal);
        void Body(Dictionary<string, Expression> body, string part, bool root)
        {
            while (!Is("}"))
            {
                if (Is("part"))
                {
                    int partStart = Offset;
                    if (!root) throw new ParseError("Parts are not supported here.", partStart);
                    Take();
                    string name = Identifier();
                    if (name == "root" || StyleCompiler.AllowedProperties(target, name).Length == 0)
                        throw new ParseError($"Unsupported {target} part '{name}'.", partStart);
                    var partValues = new Dictionary<string, Expression>(StringComparer.Ordinal);
                    if (!parts.TryAdd(name, partValues)) throw new ParseError($"Duplicate style part '{name}'.", partStart);
                    Expect("{"); Body(partValues, name, false); Expect("}");
                }
                else if (Is("when"))
                {
                    Take();
                    int stateStart = Offset;
                    string state = Take().Text;
                    if (!StyleCompiler.AllowedStates(target, part).Contains(state))
                        throw new ParseError($"Unsupported {target} style state '{state}'.", stateStart);
                    if (target != "Button" && rules.Any(r => r.Part == part && r.State == state))
                        throw new ParseError($"Duplicate style state '{state}' on part '{part}'.", stateStart);
                    Expect("{");
                    var ruleValues = new Dictionary<string, Expression>(StringComparer.Ordinal);
                    while (!Is("}")) ReadStyleProperty(ruleValues, target, part, true);
                    Expect("}");
                    rules.Add(new(state, ruleValues, part));
                    if (rules.Count > 256) throw new ParseError("A control style supports at most 256 rules.", stateStart);
                }
                else ReadStyleProperty(body, target, part);
            }
        }
        Body(values, "root", true);
        Expect("}");
        return new(name, basedOn, start, values, rules, target, parts);
    }
}

internal sealed class StyleCompiler(Component component)
{
    internal static readonly string[] Properties = ["background", "foreground", "borderBrush", "cornerRadius", "borderThickness", "padding"];
    internal static readonly string[] States = ["focused", "checked", "hovered", "pressed", "disabled"];
    internal static string TargetName(string target) => target switch { "Text" => "Label", "VStack" or "HStack" => "Stack", _ => target };
    internal static string[] AllowedProperties(string target, string part, bool stateRule = false) {
        var properties = stateRule ? StyleCatalog.StateProperties(target, part) : StyleCatalog.Properties(target, part);
        return target == "Button" && part == "root" ? [.. Properties.Union(properties)] : properties;
    }
    internal static string[] AllowedStates(string target, string part) =>
        target == "Button" && part == "root" ? [.. States.Union(StyleCatalog.States(target, part))] : StyleCatalog.States(target, part);
    internal static readonly string[] ExtendedProperties = ["fontFamily", "fontSize", "fontWeight", "fontStyle", "horizontalAlignment",
        "verticalAlignment", "spacing", "rowHeight", "headerHeight", "indentation", "thickness", "width", "height",
        "rowGap", "columnGap", "maximumLines", "wrapping"];
    private readonly Dictionary<string, ColorResource> resources = new(StringComparer.Ordinal);
    private readonly Dictionary<string, StyleDefinition> styles = new(StringComparer.Ordinal);
    private readonly Dictionary<string, string> colors = new(StringComparer.Ordinal);
    private readonly List<StyleDefinition> ordered = [];
    private readonly HashSet<string> genericStyles = new(StringComparer.Ordinal);
    private static string Key(string value) => SyntaxFactory.ParseToken(value).ValueText;
    private static string Quote(string value) => Microsoft.CodeAnalysis.CSharp.SymbolDisplay.FormatLiteral(value, true);

    internal void Validate()
    {
        foreach (var resource in component.Resources)
            if (!resources.TryAdd(Key(resource.Name), resource))
                throw new ParseError($"Duplicate resource '{resource.Name}'.", resource.Offset);
        foreach (var style in component.Styles)
            if (!styles.TryAdd(Key(style.Name), style))
                throw new ParseError($"Duplicate style '{style.Name}'.", style.Offset);
        foreach (var resource in component.Resources) Resolve(Key(resource.Name), resource.Offset, []);
        var depths = new Dictionary<string, int>(StringComparer.Ordinal);
        int Visit(StyleDefinition style, HashSet<string> visiting)
        {
            string key = Key(style.Name);
            if (depths.TryGetValue(key, out int found)) return found;
            if (!visiting.Add(key)) throw new ParseError($"Style inheritance contains a cycle at '{key}'.", style.Offset);
            if (visiting.Count > 16) throw new ParseError("Button style inheritance exceeds 16 layers.", style.Offset);
            int depth = 1;
            if (style.BasedOn is { } parent)
            {
                if (!styles.TryGetValue(Key(parent), out var definition))
                    throw new ParseError($"Style '{parent}' was not found.", style.Offset);
                if (definition.Target != style.Target) throw new ParseError("A style base must target the same control.", style.Offset);
                depth += Visit(definition, visiting);
            }
            if (depth > 16) throw new ParseError("Button style inheritance exceeds 16 layers.", style.Offset);
            visiting.Remove(key);
            depths.Add(key, depth);
            bool generic = style.Target != "Button" || style.Parts.Count != 0 ||
                style.Values.Keys.Concat(style.Rules.SelectMany(r => r.Values.Keys)).Any(p => !Properties.Contains(p)) ||
                (style.BasedOn is { } baseName && genericStyles.Contains(Key(baseName)));
            if (generic) {
                genericStyles.Add(key);
                if (style.Rules.GroupBy(r => (r.Part, r.State)).Any(group => group.Count() > 1))
                    throw new ParseError("Duplicate state block in a generic control style.", style.Offset);
            }
            string valuesTarget = generic ? "generic" : "Button";
            Values(style.Values, valuesTarget, style.Target);
            foreach (var rule in style.Rules) Values(rule.Values, valuesTarget, style.Target, rule.Part);
            foreach (var part in style.Parts) Values(part.Value, valuesTarget, style.Target, part.Key);
            ordered.Add(style);
            return depth;
        }
        foreach (var style in component.Styles) Visit(style, []);
    }

    private string Resolve(string name, int offset, HashSet<string> visiting)
    {
        if (colors.TryGetValue(name, out string? found)) return found;
        if (!resources.TryGetValue(name, out var resource)) throw new ParseError($"Color resource '{name}' was not found.", offset);
        if (!visiting.Add(name)) throw new ParseError($"Color resource aliases contain a cycle at '{name}'.", offset);
        string color = Color(resource.Value, visiting);
        visiting.Remove(name);
        colors.Add(name, color);
        return color;
    }

    private static uint Rgb(ExpressionSyntax syntax, int offset)
    {
        if (syntax is LiteralExpressionSyntax literal && literal.Token.Value is { } value &&
            value is byte or ushort or uint or ulong or sbyte or short or int or long)
        {
            decimal number = Convert.ToDecimal(value, CultureInfo.InvariantCulture);
            if (number >= 0 && number <= 0xffffff) return (uint)number;
        }
        throw new ParseError("A color requires an RGB24 integer literal between 0x000000 and 0xFFFFFF.", offset);
    }

    private string Color(Expression value, HashSet<string>? visiting = null)
    {
        var syntax = SyntaxFactory.ParseExpression(value.Text);
        if (syntax is InvocationExpressionSyntax call && call.Expression is IdentifierNameSyntax function)
        {
            var args = call.ArgumentList.Arguments;
            if (function.Identifier.ValueText == "resource" && args.Count == 1 && args[0].NameColon is null &&
                args[0].RefKindKeyword.RawKind == 0 && args[0].Expression is IdentifierNameSyntax name)
                return Resolve(name.Identifier.ValueText, value.Offset, visiting ?? []);
            if (function.Identifier.ValueText == "theme" && args.Count == 2 &&
                args[0].NameColon?.Name.Identifier.ValueText == "light" && args[1].NameColon?.Name.Identifier.ValueText == "dark" &&
                args.All(a => a.RefKindKeyword.RawKind == 0))
                return $"new global::Xui.ThemeColor({Rgb(args[0].Expression, value.Offset)}u, {Rgb(args[1].Expression, value.Offset)}u)";
        }
        return $"new global::Xui.ThemeColor({Rgb(syntax, value.Offset)}u)";
    }

    private static string Dimension(ExpressionSyntax syntax, int offset)
    {
        if (syntax is LiteralExpressionSyntax literal && literal.Token.Value is { } value &&
            value is byte or ushort or uint or ulong or sbyte or short or int or long or float or double or decimal)
        {
            double number = Convert.ToDouble(value, CultureInfo.InvariantCulture);
            if (double.IsFinite(number) && number >= 0 && number <= 32768)
                return ((float)number).ToString("R", CultureInfo.InvariantCulture) + "f";
        }
        throw new ParseError("A style dimension requires a numeric literal between 0 and 32768 DIPs.", offset);
    }

    internal string Values(Dictionary<string, Expression> values, string target = "Button", string? schemaTarget = null, string part = "root")
    {
        var fields = new List<string>();
        var limits = StyleCatalog.Limits(schemaTarget ?? target, part);
        foreach (var (key, value) in values)
        {
            string compiled;
            if (key is "background" or "foreground" or "borderBrush") compiled = Color(value);
            else
            {
                var syntax = SyntaxFactory.ParseExpression(value.Text);
                if (key is "borderThickness" or "padding")
                {
                    if (syntax is TupleExpressionSyntax tuple)
                    {
                        if (tuple.Arguments.Count != 4 || tuple.Arguments.Any(a => a.NameColon is not null))
                            throw new ParseError("Insets require four values in left, top, right, bottom order.", value.Offset);
                        compiled = "new global::Xui.Insets(" + string.Join(", ", tuple.Arguments.Select(a => Dimension(a.Expression, value.Offset))) + ")";
                    }
                    else compiled = $"new global::Xui.Insets({Dimension(syntax, value.Offset)})";
                }
                else if (key == "fontFamily") {
                    if (syntax is not LiteralExpressionSyntax literal || literal.Token.Value is not string family ||
                        family.Length == 0 || family.Contains('\0') || new System.Text.UTF8Encoding(false, true).GetByteCount(family) > 1024)
                        throw new ParseError("Font family requires a nonempty UTF-8 string literal of at most 1024 bytes.", value.Offset);
                    if (family.Length > limits.FontFamilyUtf16)
                        throw new ParseError("Font family exceeds the part's UTF-16 limit.", value.Offset);
                    compiled = Quote(family);
                }
                else if (key is "fontStyle" or "horizontalAlignment" or "verticalAlignment") {
                    var options = key == "fontStyle" ? new[] { "normal", "italic", "oblique" } : ["start", "center", "end", "stretch"];
                    if (syntax is not IdentifierNameSyntax identifier || !options.Contains(identifier.Identifier.ValueText))
                        throw new ParseError($"Unsupported {key} value.", value.Offset);
                    var name = identifier.Identifier.ValueText;
                    if (key == "fontStyle" && (limits.FontStyles & (1u << Array.IndexOf(options, name))) == 0)
                        throw new ParseError("Font style is not supported on this part.", value.Offset);
                    if ((key == "horizontalAlignment" && (limits.HorizontalAlignments & (1u << Array.IndexOf(options, name))) == 0) ||
                        (key == "verticalAlignment" && (limits.VerticalAlignments & (1u << Array.IndexOf(options, name))) == 0))
                        throw new ParseError("Alignment is not supported on this part.", value.Offset);
                    compiled = $"global::Xui.{(key == "fontStyle" ? "StyleFontStyle" : "StyleAlignment")}.{char.ToUpperInvariant(name[0]) + name[1..]}";
                }
                else if (key == "wrapping") {
                    if (!syntax.IsKind(Microsoft.CodeAnalysis.CSharp.SyntaxKind.TrueLiteralExpression) &&
                        !syntax.IsKind(Microsoft.CodeAnalysis.CSharp.SyntaxKind.FalseLiteralExpression))
                        throw new ParseError("Wrapping requires true or false.", value.Offset);
                    compiled = syntax.ToString();
                }
                else if (key is "fontWeight" or "maximumLines") {
                    if (syntax is not LiteralExpressionSyntax literal || literal.Token.Value is not (byte or ushort or uint or ulong or sbyte or short or int or long))
                        throw new ParseError($"{key} requires an integer literal.", value.Offset);
                    var number = Convert.ToDecimal(literal.Token.Value, CultureInfo.InvariantCulture);
                    if (number < (key == "fontWeight" ? 1 : 0) || number > (key == "fontWeight" ? 999 : 32768))
                        throw new ParseError($"{key} is outside its supported range.", value.Offset);
                    compiled = number.ToString(CultureInfo.InvariantCulture) + "u";
                }
                else {
                    compiled = Dimension(syntax, value.Offset);
                    if (key == "fontSize" && compiled == "0f") throw new ParseError("Font size must be positive.", value.Offset);
                    if (key == "fontSize" && float.Parse(compiled[..^1], CultureInfo.InvariantCulture) > limits.FontSize)
                        throw new ParseError("Font size exceeds the part's limit.", value.Offset);
                    if (key == "rowHeight" && compiled == "0f") throw new ParseError("Row height must be positive.", value.Offset);
                }
            }
            fields.Add(char.ToUpperInvariant(key[0]) + key[1..] + " = " + compiled);
        }
        return $"new global::Xui.{(target == "Button" ? "ButtonStyleValues" : "PartStyleValues")} {{ " + string.Join(", ", fields) + " }";
    }

    internal string Reference(Expression value, string target = "Button")
    {
        if (SyntaxFactory.ParseExpression(value.Text) is not IdentifierNameSyntax name ||
            !styles.TryGetValue(name.Identifier.ValueText, out var style) || style.Target != target)
            throw new ParseError($"Expected a declared {target} style name, not '{value.Text}'.", value.Offset);
        return $"(global::Xui.{(genericStyles.Contains(Key(style.Name)) ? "ControlStyle" : "ButtonStyle")})__xuiGetStyles()[" + Quote(name.Identifier.ValueText) + "]";
    }
    internal string ReferenceTarget(Expression value) {
        if (SyntaxFactory.ParseExpression(value.Text) is IdentifierNameSyntax name && styles.TryGetValue(name.Identifier.ValueText, out var style))
            return style.Target;
        throw new ParseError("Expected a declared style name.", value.Offset);
    }
    internal bool GenericReference(Expression value) => genericStyles.Contains(Key(value.Text));

    private static string Part(string part) => "global::Xui.StylePart." + char.ToUpperInvariant(part[0]) + part[1..];
    private string GenericDefinition(StyleDefinition style, string parent)
    {
        // Legacy Button rules permit repeated states; promotion preserves their field-wise last-write precedence.
        var rules = style.Rules.GroupBy(rule => (rule.Part, rule.State)).Select(group =>
        {
            var values = new Dictionary<string, Expression>(StringComparer.Ordinal);
            foreach (var rule in group)
                foreach (var value in rule.Values) values[value.Key] = value.Value;
            return new StyleRule(group.Key.State, values, group.Key.Part);
        });
        return $"new global::Xui.ControlStyle(global::Xui.StyleTarget.{style.Target}, " +
            "new global::Xui.PartStyle[] { " +
            $"new({Part("root")}, {Values(style.Values, "generic", style.Target)}), " +
            string.Join(", ", style.Parts.Select(p => $"new({Part(p.Key)}, {Values(p.Value, "generic", style.Target, p.Key)})")) +
            " }, new global::Xui.ControlStyleRule[] { " +
            string.Join(", ", rules.Select(r => $"new({Part(r.Part)}, global::Xui.StyleState.{char.ToUpperInvariant(r.State[0]) + r.State[1..]}, {Values(r.Values, "generic", style.Target, r.Part)})")) +
            " }, " + parent + ")";
    }
    internal string Definitions()
    {
        var promoted = new Dictionary<string, string>(StringComparer.Ordinal);
        foreach (var style in ordered.Where(style => genericStyles.Contains(Key(style.Name))))
            for (var parent = style.BasedOn; parent is not null && !genericStyles.Contains(Key(parent)); parent = styles[Key(parent)].BasedOn)
                if (!promoted.ContainsKey(Key(parent)))
                    promoted.Add(Key(parent), "__xuiPromotedStyle" + promoted.Count);

        string GenericBase(StyleDefinition style) => style.BasedOn is not { } parent ? "null" :
            genericStyles.Contains(Key(parent)) ? $"(global::Xui.ControlStyle)__xuiStyles[{Quote(Key(parent))}]" : promoted[Key(parent)];
        var definitions = new List<string>();
        foreach (var style in ordered)
        {
            string key = Key(style.Name);
            if (genericStyles.Contains(key))
                definitions.Add($"__xuiStyles.Add({Quote(key)}, {GenericDefinition(style, GenericBase(style))});");
            else
            {
                definitions.Add($"__xuiStyles.Add({Quote(key)}, new global::Xui.ButtonStyle({Values(style.Values)}, " +
                    "new global::Xui.ButtonStyleRule[] { " +
                    string.Join(", ", style.Rules.Select(rule => $"new(global::Xui.ButtonStyleState.{char.ToUpperInvariant(rule.State[0]) + rule.State[1..]}, {Values(rule.Values)})")) +
                    " }, " + (style.BasedOn is null ? "null" : $"(global::Xui.ButtonStyle)__xuiStyles[{Quote(Key(style.BasedOn))}]") + "));");
                if (promoted.TryGetValue(key, out string? variable))
                    definitions.Add($"var {variable} = {GenericDefinition(style, GenericBase(style))};");
            }
        }
        return string.Join("\n", definitions);
    }
}
