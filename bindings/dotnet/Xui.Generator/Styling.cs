using System.Globalization;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.CSharp.Syntax;

namespace Xui.Generator;

internal sealed record ColorResource(string Name, Expression Value, int Offset);
internal sealed record StyleRule(string State, Dictionary<string, Expression> Values);
internal sealed record StyleDefinition(string Name, string? BasedOn, int Offset,
    Dictionary<string, Expression> Values, List<StyleRule> Rules);

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

    private void ReadStyleProperty(Dictionary<string, Expression> values)
    {
        int start = Offset;
        string key = Identifier();
        if (!StyleCompiler.Properties.Contains(key)) throw new ParseError($"Unsupported Button style property '{key}'.", start);
        Expect(":");
        if (!values.TryAdd(key, ReadStyleExpression())) throw new ParseError($"Duplicate style property '{key}'.", start);
        Expect(";");
    }

    private StyleDefinition ParseStyle()
    {
        Take();
        int start = Offset;
        string name = Identifier();
        Expect("for"); Expect("Button");
        string? basedOn = null;
        if (Is("basedOn")) { Take(); basedOn = Identifier(); }
        Expect("{");
        var values = new Dictionary<string, Expression>(StringComparer.Ordinal);
        var rules = new List<StyleRule>();
        while (!Is("}"))
        {
            if (Is("when"))
            {
                Take();
                int stateStart = Offset;
                string state = Take().Text;
                if (!StyleCompiler.States.Contains(state))
                    throw new ParseError($"Unsupported Button style state '{state}'.", stateStart);
                Expect("{");
                var ruleValues = new Dictionary<string, Expression>(StringComparer.Ordinal);
                while (!Is("}")) ReadStyleProperty(ruleValues);
                Expect("}");
                rules.Add(new(state, ruleValues));
                if (rules.Count > 256) throw new ParseError("A Button style supports at most 256 rules.", stateStart);
            }
            else ReadStyleProperty(values);
        }
        Expect("}");
        return new(name, basedOn, start, values, rules);
    }
}

internal sealed class StyleCompiler(Component component)
{
    internal static readonly string[] Properties = ["background", "foreground", "borderBrush", "cornerRadius", "borderThickness", "padding"];
    internal static readonly string[] States = ["focused", "checked", "hovered", "pressed", "disabled"];
    private readonly Dictionary<string, ColorResource> resources = new(StringComparer.Ordinal);
    private readonly Dictionary<string, StyleDefinition> styles = new(StringComparer.Ordinal);
    private readonly Dictionary<string, string> colors = new(StringComparer.Ordinal);
    private readonly List<StyleDefinition> ordered = [];
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
                depth += Visit(definition, visiting);
            }
            if (depth > 16) throw new ParseError("Button style inheritance exceeds 16 layers.", style.Offset);
            visiting.Remove(key);
            depths.Add(key, depth);
            Values(style.Values);
            foreach (var rule in style.Rules) Values(rule.Values);
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

    internal string Values(Dictionary<string, Expression> values)
    {
        var fields = new List<string>();
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
                else compiled = Dimension(syntax, value.Offset);
            }
            fields.Add(char.ToUpperInvariant(key[0]) + key[1..] + " = " + compiled);
        }
        return "new global::Xui.ButtonStyleValues { " + string.Join(", ", fields) + " }";
    }

    internal string Reference(Expression value)
    {
        if (SyntaxFactory.ParseExpression(value.Text) is not IdentifierNameSyntax name ||
            !styles.ContainsKey(name.Identifier.ValueText))
            throw new ParseError($"Expected a declared Button style name, not '{value.Text}'.", value.Offset);
        return "__xuiGetStyles()[" + Quote(name.Identifier.ValueText) + "]";
    }

    internal string Definitions() => string.Join("\n", ordered.Select(style =>
        $"__xuiStyles.Add({Quote(Key(style.Name))}, new global::Xui.ButtonStyle({Values(style.Values)}, " +
        "new global::Xui.ButtonStyleRule[] { " +
        string.Join(", ", style.Rules.Select(rule => $"new(global::Xui.ButtonStyleState.{char.ToUpperInvariant(rule.State[0]) + rule.State[1..]}, {Values(rule.Values)})")) +
        " }, " + (style.BasedOn is null ? "null" : $"__xuiStyles[{Quote(Key(style.BasedOn))}]") + "));"));
}
