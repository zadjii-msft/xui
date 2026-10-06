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
    private Expression ReadStyleExpression(string context)
    {
        int start = position;
        var expression = SyntaxFactory.ParseExpression(text, position, consumeFullText: false);
        if (expression.IsMissing)
            throw new ParseError($"Missing value for {context}; found {Found}. Add a style literal or a supported color expression before ';'.", Offset);
        Check(expression, start, $"style value for {context}");
        position += expression.FullSpan.Length;
        return new(expression.ToString(), start + expression.SpanStart);
    }

    private void ReadStyleProperty(Dictionary<string, Expression> values, string target, string part, bool stateRule = false)
    {
        int start = Offset;
        string key = Identifier($"a style property name on {target} part '{part}'");
        string context = $"'{key}' on {target} part '{part}'{(stateRule ? " in this state rule" : "")}";
        if (!StyleCompiler.AllowedProperties(target, part, stateRule).Contains(key))
            throw new ParseError($"Unsupported style property {context}. Remove or replace this property. " +
                DiagnosticText.Choices(StyleCompiler.AllowedProperties(target, part, stateRule)), start);
        Expect(":", $"after style property {context}");
        if (!values.TryAdd(key, ReadStyleExpression(context)))
            throw new ParseError($"Duplicate style property {context}. Keep one value in this block and remove the repeated entry.", start);
        Expect(";", $"after the value of style property {context}");
    }

    private StyleDefinition ParseStyle()
    {
        Take();
        int start = Offset;
        string name = Identifier("a style name after 'style'");
        Expect("for", $"after style name '{name}'", "Declare a style as 'style Name for Control { ... }'.");
        int targetStart = Offset;
        string target = StyleCompiler.TargetName(Identifier($"a control target after 'for' in style '{name}'"));
        if (target == "Tooltip")
            throw new ParseError($"Style '{name}' targets Tooltip, which the .xui compiler cannot apply. Use the Window API to apply Tooltip styles from C#.", targetStart);
        if (target != "Button" && !StyleCatalog.TargetExists(target))
            throw new ParseError($"Unsupported control target '{target}' in style '{name}'. Use a supported style target, such as Button, Text, or TextInput; see docs/specs/control-styling.md.", targetStart);
        string? basedOn = null;
        if (Is("basedOn")) { Take(); basedOn = Identifier($"a base style name after 'basedOn' in style '{name}'"); }
        Expect("{", $"to begin style '{name}'");
        var values = new Dictionary<string, Expression>(StringComparer.Ordinal);
        var rules = new List<StyleRule>();
        var parts = new Dictionary<string, Dictionary<string, Expression>>(StringComparer.Ordinal);
        void Body(Dictionary<string, Expression> body, string part, bool root)
        {
            while (!Is("}"))
            {
                CheckBlockEnd($"style '{name}', {target} part '{part}'");
                if (Is("part"))
                {
                    int partStart = Offset;
                    if (!root)
                        throw new ParseError($"A part block cannot be nested inside part '{part}' of style '{name}'. Move it directly inside the style's root block.", partStart);
                    Take();
                    string partName = Identifier($"a part name in style '{name}'");
                    if (partName == "root" || StyleCompiler.AllowedProperties(target, partName).Length == 0)
                        throw new ParseError($"Unsupported {target} part '{partName}' in style '{name}'. " +
                            (partName == "root" ? "Put root properties directly inside the style, without a 'part root' block." : "Use a named part supported by this control; see docs/specs/control-styling-inventory.md."), partStart);
                    var partValues = new Dictionary<string, Expression>(StringComparer.Ordinal);
                    if (!parts.TryAdd(partName, partValues))
                        throw new ParseError($"Duplicate part '{partName}' in style '{name}'. Merge its properties into one part block.", partStart);
                    Expect("{", $"to begin part '{partName}' in style '{name}'");
                    Body(partValues, partName, false);
                    CloseBlock($"part '{partName}' in style '{name}'");
                }
                else if (Is("when"))
                {
                    Take();
                    int stateStart = Offset;
                    string stateDescription = Found;
                    string state = Take().Text;
                    if (!StyleCompiler.AllowedStates(target, part).Contains(state))
                        throw new ParseError($"Unsupported state {stateDescription} on {target} part '{part}' in style '{name}'. " +
                            "Remove or replace this when block. " + DiagnosticText.Choices(StyleCompiler.AllowedStates(target, part)), stateStart);
                    if (target != "Button" && rules.Any(r => r.Part == part && r.State == state))
                        throw new ParseError($"Duplicate state '{state}' on part '{part}' in style '{name}'. Merge its properties into one when block.", stateStart);
                    Expect("{", $"to begin the '{state}' state rule in style '{name}'");
                    var ruleValues = new Dictionary<string, Expression>(StringComparer.Ordinal);
                    while (!Is("}"))
                    {
                        CheckBlockEnd($"the '{state}' state rule in style '{name}'");
                        ReadStyleProperty(ruleValues, target, part, true);
                    }
                    CloseBlock($"the '{state}' state rule in style '{name}'");
                    rules.Add(new(state, ruleValues, part));
                    if (rules.Count > 256)
                        throw new ParseError($"Style '{name}' has more than 256 state rules. Remove or combine when blocks to stay within this limit.", stateStart);
                }
                else ReadStyleProperty(body, target, part);
            }
        }
        Body(values, "root", true);
        CloseBlock($"style '{name}'");
        return new(name, basedOn, start, values, rules, target, parts);
    }
}

internal sealed class StyleCompiler(Component component)
{
    internal static readonly string[] Properties = ["background", "foreground", "borderBrush", "cornerRadius", "borderThickness", "padding"];
    internal static readonly string[] States = ["focused", "checked", "hovered", "pressed", "disabled"];
    internal static string TargetName(string target) => target switch {
        "Text" => "Label", "VStack" or "HStack" => "Stack",
        "ToggleSwitch" => "Toggle", "ToggleButton" or "HyperlinkButton" => "Button", "ProgressRing" => "Progress",
        "CheckBox" => "Toggle", "SelectorBar" => "ChoiceList", "InfoBadge" => "InlineStatus",
        "MenuBar" => "CommandBar", _ => target
    };
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
                throw new ParseError($"Duplicate color resource '{resource.Name}'. Remove the repeated declaration or give it a different name; escaped names refer to the same identifier.", resource.Offset);
        foreach (var style in component.Styles)
            if (!styles.TryAdd(Key(style.Name), style))
                throw new ParseError($"Duplicate style '{style.Name}'. Remove the repeated declaration or give it a different name; escaped names refer to the same identifier.", style.Offset);
        foreach (var resource in component.Resources) Resolve(Key(resource.Name), resource.Offset, []);
        var depths = new Dictionary<string, int>(StringComparer.Ordinal);
        int Visit(StyleDefinition style, HashSet<string> visiting)
        {
            string key = Key(style.Name);
            if (depths.TryGetValue(key, out int found)) return found;
            if (!visiting.Add(key))
                throw new ParseError($"Style inheritance contains a cycle at '{key}'. Remove or change a basedOn reference so no style inherits from itself, directly or indirectly.", style.Offset);
            if (visiting.Count > 16)
                throw new ParseError($"Style inheritance through '{key}' exceeds 16 layers. Shorten the basedOn chain or combine base styles.", style.Offset);
            int depth = 1;
            if (style.BasedOn is { } parent)
            {
                if (!styles.TryGetValue(Key(parent), out var definition))
                    throw new ParseError($"Base style '{parent}' for style '{style.Name}' was not found. Declare 'style {parent} for {style.Target} {{ ... }}' in this component or correct the basedOn name.", style.Offset);
                if (definition.Target != style.Target)
                    throw new ParseError($"Style '{style.Name}' targets {style.Target}, but its base '{parent}' targets {definition.Target}. Choose a base style for {style.Target} or change the target; both must match.", style.Offset);
                depth += Visit(definition, visiting);
            }
            if (depth > 16)
                throw new ParseError($"Style '{style.Name}' has an inheritance chain of {depth} layers; at most 16 are supported. Shorten its basedOn chain or combine base styles.", style.Offset);
            visiting.Remove(key);
            depths.Add(key, depth);
            bool generic = style.Target != "Button" || style.Parts.Count != 0 ||
                style.Values.Keys.Concat(style.Rules.SelectMany(r => r.Values.Keys)).Any(p => !Properties.Contains(p)) ||
                (style.BasedOn is { } baseName && genericStyles.Contains(Key(baseName)));
            if (generic) {
                genericStyles.Add(key);
                var duplicate = style.Rules.GroupBy(r => (r.Part, r.State)).FirstOrDefault(group => group.Count() > 1);
                if (duplicate is not null)
                    throw new ParseError($"Duplicate state '{duplicate.Key.State}' on part '{duplicate.Key.Part}' in style '{style.Name}'. Generic control styles require one when block per state and part; merge the repeated blocks.", style.Offset);
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
        if (!resources.TryGetValue(name, out var resource))
            throw new ParseError($"Color resource '{name}' was not found. Declare '{name}: 0xRRGGBB;' inside this component's resources block or correct the resource(...) name.", offset);
        if (!visiting.Add(name))
            throw new ParseError($"Color resource aliases contain a cycle at '{name}'. Replace an alias with a color literal or change its resource(...) reference to break the cycle.", offset);
        string color = Color(resource.Value, $"color resource '{name}'", visiting);
        visiting.Remove(name);
        colors.Add(name, color);
        return color;
    }

    private static uint Rgb(ExpressionSyntax syntax, int offset, string context)
    {
        if (syntax is LiteralExpressionSyntax literal && literal.Token.Value is { } value &&
            value is byte or ushort or uint or ulong or sbyte or short or int or long)
        {
            decimal number = Convert.ToDecimal(value, CultureInfo.InvariantCulture);
            if (number >= 0 && number <= 0xffffff) return (uint)number;
        }
        throw new ParseError($"Invalid color {DiagnosticText.Quote(syntax.ToString())} for {context}. Use an RGB24 integer literal between 0x000000 and 0xFFFFFF, for example 0xB91C1C; alpha bytes, strings, and computed expressions are not supported.", offset);
    }

    private string Color(Expression value, string context, HashSet<string>? visiting = null)
    {
        var syntax = SyntaxFactory.ParseExpression(value.Text);
        if (syntax is InvocationExpressionSyntax call && call.Expression is IdentifierNameSyntax function)
        {
            var args = call.ArgumentList.Arguments;
            if (function.Identifier.ValueText == "resource")
            {
                if (args.Count == 1 && args[0].NameColon is null && args[0].RefKindKeyword.RawKind == 0 &&
                    args[0].Expression is IdentifierNameSyntax name)
                    return Resolve(name.Identifier.ValueText, value.Offset, visiting ?? []);
                throw new ParseError($"Invalid resource reference {DiagnosticText.Quote(value.Text)} for {context}. Use 'resource(ResourceName)' with one unquoted resource name, without named or ref arguments.", value.Offset);
            }
            if (function.Identifier.ValueText == "theme")
            {
                if (args.Count != 2 || args[0].NameColon?.Name.Identifier.ValueText != "light" ||
                    args[1].NameColon?.Name.Identifier.ValueText != "dark" || args.Any(a => a.RefKindKeyword.RawKind != 0))
                    throw new ParseError($"Invalid theme color {DiagnosticText.Quote(value.Text)} for {context}. Use 'theme(light: 0xRRGGBB, dark: 0xRRGGBB)' with light first and dark second.", value.Offset);
                return $"new global::Xui.ThemeColor({Rgb(args[0].Expression, value.Offset + args[0].Expression.SpanStart, $"the light value of {context}")}u, " +
                    $"{Rgb(args[1].Expression, value.Offset + args[1].Expression.SpanStart, $"the dark value of {context}")}u)";
            }
        }
        return $"new global::Xui.ThemeColor({Rgb(syntax, value.Offset, context)}u)";
    }

    private static string Dimension(ExpressionSyntax syntax, int offset, string context)
    {
        if (syntax is LiteralExpressionSyntax literal && literal.Token.Value is { } value &&
            value is byte or ushort or uint or ulong or sbyte or short or int or long or float or double or decimal)
        {
            double number = Convert.ToDouble(value, CultureInfo.InvariantCulture);
            if (double.IsFinite(number) && number >= 0 && number <= 32768)
                return ((float)number).ToString("R", CultureInfo.InvariantCulture) + "f";
        }
        throw new ParseError($"Invalid dimension {DiagnosticText.Quote(syntax.ToString())} for {context}. Use a numeric literal between 0 and 32768 DIPs, for example 8 or 0.5; state and computed expressions are not supported in styles.", offset);
    }

    private static int FontFamilyBytes(string family, string context, int offset)
    {
        try
        {
            return new System.Text.UTF8Encoding(false, true).GetByteCount(family);
        }
        catch (System.Text.EncoderFallbackException)
        {
            throw new ParseError($"Font family for {context} contains an unpaired Unicode surrogate. Replace the invalid Unicode escape or use a valid font name such as '\"Segoe UI\"'.", offset);
        }
    }

    internal string Values(Dictionary<string, Expression> values, string target = "Button", string? schemaTarget = null, string part = "root")
    {
        var fields = new List<string>();
        var limits = StyleCatalog.Limits(schemaTarget ?? target, part);
        foreach (var (key, value) in values)
        {
            string context = $"'{key}' on {schemaTarget ?? target} part '{part}'";
            string compiled;
            if (key is "background" or "foreground" or "borderBrush") compiled = Color(value, context);
            else
            {
                var syntax = SyntaxFactory.ParseExpression(value.Text);
                if (key is "borderThickness" or "padding")
                {
                    if (syntax is TupleExpressionSyntax tuple)
                    {
                        if (tuple.Arguments.Count != 4 || tuple.Arguments.Any(a => a.NameColon is not null))
                            throw new ParseError($"Invalid insets {DiagnosticText.Quote(value.Text)} for {context}. Use one uniform number or four unnamed values in (left, top, right, bottom) order, for example '(4, 8, 4, 8)'.", value.Offset);
                        string[] edges = ["left", "top", "right", "bottom"];
                        compiled = "new global::Xui.Insets(" + string.Join(", ", tuple.Arguments.Select((a, i) =>
                            Dimension(a.Expression, value.Offset + a.Expression.SpanStart, $"the {edges[i]} inset of {context}"))) + ")";
                    }
                    else compiled = $"new global::Xui.Insets({Dimension(syntax, value.Offset, context)})";
                }
                else if (key == "fontFamily") {
                    if (syntax is not LiteralExpressionSyntax literal || literal.Token.Value is not string family ||
                        family.Length == 0 || family.Contains('\0') || FontFamilyBytes(family, context, value.Offset) > 1024)
                        throw new ParseError($"Invalid font family {DiagnosticText.Quote(value.Text)} for {context}. Use a nonempty string literal of at most 1024 UTF-8 bytes, without NUL characters, for example '\"Segoe UI\"'.", value.Offset);
                    if (family.Length > limits.FontFamilyUtf16)
                        throw new ParseError($"Font family for {context} has {family.Length} UTF-16 code units; this part allows at most {limits.FontFamilyUtf16}. Use a shorter font family name.", value.Offset);
                    compiled = Quote(family);
                }
                else if (key is "fontStyle" or "horizontalAlignment" or "verticalAlignment") {
                    var options = key == "fontStyle" ? new[] { "normal", "italic", "oblique" } : ["start", "center", "end", "stretch"];
                    if (syntax is not IdentifierNameSyntax identifier || !options.Contains(identifier.Identifier.ValueText))
                        throw new ParseError($"Unsupported value {DiagnosticText.Quote(value.Text)} for {context}. Use an unquoted style keyword. " + DiagnosticText.Choices(options), value.Offset);
                    var name = identifier.Identifier.ValueText;
                    if (key == "fontStyle" && (limits.FontStyles & (1u << Array.IndexOf(options, name))) == 0)
                        throw new ParseError($"Font style '{name}' is not supported for {context}. " +
                            DiagnosticText.Choices(options.Where((_, i) => (limits.FontStyles & (1u << i)) != 0)), value.Offset);
                    if ((key == "horizontalAlignment" && (limits.HorizontalAlignments & (1u << Array.IndexOf(options, name))) == 0) ||
                        (key == "verticalAlignment" && (limits.VerticalAlignments & (1u << Array.IndexOf(options, name))) == 0))
                        throw new ParseError($"Alignment '{name}' is not supported for {context}. " +
                            DiagnosticText.Choices(options.Where((_, i) => ((key == "horizontalAlignment" ? limits.HorizontalAlignments : limits.VerticalAlignments) & (1u << i)) != 0)), value.Offset);
                    compiled = $"global::Xui.{(key == "fontStyle" ? "StyleFontStyle" : "StyleAlignment")}.{char.ToUpperInvariant(name[0]) + name[1..]}";
                }
                else if (key == "wrapping") {
                    if (!syntax.IsKind(Microsoft.CodeAnalysis.CSharp.SyntaxKind.TrueLiteralExpression) &&
                        !syntax.IsKind(Microsoft.CodeAnalysis.CSharp.SyntaxKind.FalseLiteralExpression))
                        throw new ParseError($"Invalid value {DiagnosticText.Quote(value.Text)} for {context}. Use the boolean literal true or false, not a string or computed expression.", value.Offset);
                    compiled = syntax.ToString();
                }
                else if (key is "fontWeight" or "maximumLines") {
                    if (syntax is not LiteralExpressionSyntax literal || literal.Token.Value is not (byte or ushort or uint or ulong or sbyte or short or int or long))
                        throw new ParseError($"Invalid value {DiagnosticText.Quote(value.Text)} for {context}. Use an integer literal {(key == "fontWeight" ? "between 1 and 999, for example 400" : "between 0 and 32768; 0 means unlimited")}.", value.Offset);
                    var number = Convert.ToDecimal(literal.Token.Value, CultureInfo.InvariantCulture);
                    if (number < (key == "fontWeight" ? 1 : 0) || number > (key == "fontWeight" ? 999 : 32768))
                        throw new ParseError($"Value {DiagnosticText.Quote(value.Text)} for {context} is outside its supported range. Use an integer from {(key == "fontWeight" ? "1 through 999" : "0 through 32768 (0 means unlimited)")}.", value.Offset);
                    compiled = number.ToString(CultureInfo.InvariantCulture) + "u";
                }
                else {
                    compiled = Dimension(syntax, value.Offset, context);
                    if (key == "fontSize" && compiled == "0f")
                        throw new ParseError($"Font size for {context} must be greater than zero. Use a positive numeric literal up to {limits.FontSize} DIPs.", value.Offset);
                    if (key == "fontSize" && float.Parse(compiled[..^1], CultureInfo.InvariantCulture) > limits.FontSize)
                        throw new ParseError($"Font size {DiagnosticText.Quote(value.Text)} for {context} exceeds this part's limit of {limits.FontSize} DIPs. Use a smaller positive font size.", value.Offset);
                    if (key == "rowHeight" && compiled == "0f")
                        throw new ParseError($"Row height for {context} must be greater than zero. Use a positive numeric literal up to 32768 DIPs.", value.Offset);
                }
            }
            fields.Add(char.ToUpperInvariant(key[0]) + key[1..] + " = " + compiled);
        }
        return $"new global::Xui.{(target == "Button" ? "ButtonStyleValues" : "PartStyleValues")} {{ " + string.Join(", ", fields) + " }";
    }

    internal string Reference(Expression value, string target = "Button")
    {
        if (SyntaxFactory.ParseExpression(value.Text) is not IdentifierNameSyntax name)
            throw new ParseError($"Invalid style reference {DiagnosticText.Quote(value.Text)} for {target}. Use 'style: StyleName' with a style declared in this component, not a string or a C# expression.", value.Offset);
        if (!styles.TryGetValue(name.Identifier.ValueText, out var style))
            throw new ParseError($"Style '{name.Identifier.ValueText}' was not found for {target}. Declare 'style {name.Identifier.ValueText} for {target} {{ ... }}' in this component or correct the style name.", value.Offset);
        if (style.Target != target)
            throw new ParseError($"Style '{style.Name}' targets {style.Target}, not {target}. Choose or declare a style targeting {target}.", value.Offset);
        return $"(global::Xui.{(genericStyles.Contains(Key(style.Name)) ? "ControlStyle" : "ButtonStyle")})__xuiGetStyles()[" + Quote(name.Identifier.ValueText) + "]";
    }
    internal string ReferenceTarget(Expression value) {
        if (SyntaxFactory.ParseExpression(value.Text) is IdentifierNameSyntax name && styles.TryGetValue(name.Identifier.ValueText, out var style))
            return style.Target;
        throw new ParseError($"Content requires a declared style name, not {DiagnosticText.Quote(value.Text)}. Declare a style for the embedded control's target and reference it with 'style: StyleName'.", value.Offset);
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
