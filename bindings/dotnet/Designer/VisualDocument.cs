using System.Globalization;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.CSharp.Syntax;
using Microsoft.CodeAnalysis.Text;
using Xui.Generator;

namespace Xui.Designer;

public enum ControlTemplate { Text, Button, Toggle, TextInput, VStack, HStack, Grid, ScrollView, SplitView, DataGrid, NavigationView, RangeInput, Progress, ToggleSwitch, ToggleButton, ProgressRing, CheckBox, HyperlinkButton, SelectorBar, InfoBadge, MenuBar }

public readonly record struct GridPlacement(int Row, int Column, int RowSpan = 1, int ColumnSpan = 1);

public sealed record VisualEdit(
    Guid Revision, string ExpectedSource, SourceRange Range, string Replacement, SourceRange Selection)
{
    public string Apply(Guid revision, string source)
    {
        if (revision != Revision || !string.Equals(source, ExpectedSource, StringComparison.Ordinal))
            throw new InvalidOperationException("The document changed. Parse the current editor text before applying this edit.");
        if (Range.Start < 0 || Range.Length < 0 || Range.Start > source.Length - Range.Length)
            throw new InvalidOperationException("The edit range is outside the document.");
        return source.Remove(Range.Start, Range.Length).Insert(Range.Start, Replacement);
    }
}

public sealed record VisualEditResult(VisualEdit? Edit, string? Error)
{
    public bool Success => Edit is not null && Error is null;
}

public sealed class VisualDocument
{
    private readonly Dictionary<int, XuiSourceNode> nodes = [];
    private readonly Dictionary<int, XuiSourceNode> parents = [];
    public Guid Revision { get; } = Guid.NewGuid();
    public XuiSourceDocument Syntax { get; }
    public string Source => Syntax.Source;
    public XuiSourceNode? Root => Syntax.Root;
    public bool Success => Syntax.Success;
    public IReadOnlyList<XuiSourceDiagnostic> Diagnostics => Syntax.Diagnostics;

    private VisualDocument(XuiSourceDocument syntax)
    {
        Syntax = syntax;
        if (Root is not null) Add(Root, null);
        void Add(XuiSourceNode node, XuiSourceNode? parent)
        {
            nodes.Add(node.Id, node);
            if (parent is not null) parents.Add(node.Id, parent);
            foreach (var child in node.Children) Add(child, node);
        }
    }

    public static VisualDocument Parse(string source, CancellationToken cancellation = default) =>
        new(XuiSourceParser.Parse(source, cancellation));

    public XuiSourceNode? FindNode(int caretOffset)
    {
        if (!Success || caretOffset < 0 || caretOffset >= Source.Length) return null;
        return Find(Root!);
        XuiSourceNode? Find(XuiSourceNode node)
        {
            if (!node.Span.Contains(caretOffset)) return null;
            foreach (var child in node.Children)
                if (Find(child) is { } found) return found;
            return node;
        }
    }

    public VisualEditResult SetArgument(Guid revision, int nodeId, string name, string value,
        bool replaceExpression = false, CancellationToken cancellation = default)
    {
        if (Target(revision, nodeId, out var node) is { } error) return Failure(error);
        if (string.IsNullOrWhiteSpace(name) || !node.SupportedArguments.Contains(name, StringComparer.Ordinal))
            return Failure($"'{name}' is not a supported argument on {node.Kind}.");
        if (string.IsNullOrWhiteSpace(value)) return Failure("Supply one C# expression for the argument value.");
        cancellation.ThrowIfCancellationRequested();
        if (value.Length > XuiSourceParser.MaximumSourceLength)
            return Failure($"The argument exceeds the {XuiSourceParser.MaximumSourceLength}-code-unit source limit.");
        value = value.Trim();
        var expression = SyntaxFactory.ParseExpression(value);
        if (expression.ContainsDiagnostics || expression.ToString() != value)
            return Failure("Supply exactly one complete C# expression, without leading or trailing comments.");
        var argument = node.Arguments.FirstOrDefault(a => a.Name == name);
        if (argument?.ValueKind == XuiValueKind.Expression && !replaceExpression)
            return Failure($"'{name}' contains a C# expression. Explicitly approve expression replacement or edit it in source.");
        if (name == "value" && argument is null)
            return Failure("The positional argument is missing. Repair the source before editing.");
        SourceRange range;
        string replacement;
        if (argument is not null)
        {
            range = argument.ValueSpan;
            replacement = value;
        }
        else
        {
            int offset = node.Arguments.Count == 0 ? node.ArgumentsSpan.Start : node.Arguments[^1].ValueSpan.End;
            range = new(offset, 0);
            replacement = (node.Arguments.Count == 0 ? "" : ", ") + name + ": " + value;
        }
        int? grid = name is "row" or "column" or "rowSpan" or "columnSpan"
            ? parents.GetValueOrDefault(node.Id)?.Id
            : node.Kind == "Grid" && name is "rows" or "columns" ? node.Id : null;
        return Propose(revision, range, replacement, node.Span.Start, grid, cancellation);
    }

    public VisualEditResult RemoveArgument(Guid expectedRevision, int nodeId, string argumentName,
        CancellationToken cancellation = default)
    {
        cancellation.ThrowIfCancellationRequested();
        if (Target(expectedRevision, nodeId, out var node) is { } error) return Failure(error);
        var argument = node.Arguments.FirstOrDefault(a => a.Name == argumentName);
        if (argument is null)
            return Failure($"'{argumentName}' is not an authored argument on this node. Select an existing named argument.");
        if (argument.IsPositional)
            return Failure("The positional operand is required. Edit its value in source instead of removing it.");
        foreach (var token in SyntaxFactory.ParseTokens(Slice(argument.Span)))
            foreach (var trivia in token.LeadingTrivia.Concat(token.TrailingTrivia))
                if (!trivia.IsKind(SyntaxKind.WhitespaceTrivia) && !trivia.IsKind(SyntaxKind.EndOfLineTrivia))
                    return Failure("The argument contains comments or directives. Preserve them in source before removing the argument.");

        int index = 0;
        while (node.Arguments[index] != argument) index++;
        bool following = index + 1 < node.Arguments.Count || node.HasTrailingComma;
        SourceRange range = argument.Span;
        string replacement = "";
        if (following || index > 0)
        {
            int start = following ? argument.Span.End : node.Arguments[index - 1].Span.End;
            int end = following
                ? index + 1 < node.Arguments.Count ? node.Arguments[index + 1].Span.Start : node.ArgumentsSpan.End
                : argument.Span.Start;
            var tokens = SyntaxFactory.ParseTokens(Source[start..end]).ToArray();
            var commas = tokens.Where(t => t.IsKind(SyntaxKind.CommaToken)).ToArray();
            if (commas.Length != 1 || tokens.Any(t => !t.IsKind(SyntaxKind.CommaToken) && !t.IsKind(SyntaxKind.EndOfFileToken)) ||
                tokens.SelectMany(t => t.LeadingTrivia.Concat(t.TrailingTrivia)).Any(t => t.IsDirective))
                return Failure("The argument separator is ambiguous. Remove this argument in source.");
            int comma = start + commas[0].SpanStart;
            if (following)
            {
                range = new(argument.Span.Start, comma + 1 - argument.Span.Start);
                replacement = Source[argument.Span.End..comma];
            }
            else
            {
                range = new(comma, argument.Span.End - comma);
                replacement = Source[(comma + 1)..argument.Span.Start];
            }
        }
        int? grid = argumentName is "row" or "column" or "rowSpan" or "columnSpan"
            ? parents.GetValueOrDefault(node.Id)?.Id
            : node.Kind == "Grid" && argumentName is "rows" or "columns" ? node.Id : null;
        return Propose(expectedRevision, range, replacement, node.Span.Start, grid, cancellation);
    }

    public VisualEditResult DeleteNode(Guid revision, int nodeId, CancellationToken cancellation = default)
    {
        if (Target(revision, nodeId, out var node) is { } error) return Failure(error);
        if (VariableParent(node, out var parent) is { } reason) return Failure(reason);
        return Propose(revision, node.Span, "", parent.Span.Start, null, cancellation);
    }

    public VisualEditResult DuplicateNode(Guid revision, int nodeId, GridPlacement? placement = null,
        CancellationToken cancellation = default)
    {
        if (Target(revision, nodeId, out var node) is { } error) return Failure(error);
        if (VariableParent(node, out var parent) is { } reason) return Failure(reason);
        if (Descendants(node).Any(n => n.Kind == "Content"))
            return Failure("This subtree contains Content, which owns an existing element. Insert a new control instead.");
        if (Descendants(node).SelectMany(n => n.Arguments).Any(a => a.Name is "ref" or "id" or "searchId"))
            return Failure("This subtree declares ref, id, or searchId values. Remove or rename these identities in source before duplication.");
        if (parent.Kind == "Grid" && placement is null)
            return Failure("Grid children need distinct placement. Specify a GridPlacement for the duplicate.");
        if (parent.Kind != "Grid" && placement is not null)
            return Failure("GridPlacement requires a Grid parent.");
        string duplicate = Slice(node.Span);
        if (placement is { } cell)
        {
            if (cell.Row < 0 || cell.Column < 0 || cell.RowSpan < 1 || cell.ColumnSpan < 1)
                return Failure("Grid rows and columns must be nonnegative; spans must be positive.");
            var changes = new List<(SourceRange Range, string Text)>();
            var missing = new List<string>();
            foreach (var (name, value) in new[]
            {
                ("row", cell.Row), ("column", cell.Column), ("rowSpan", cell.RowSpan), ("columnSpan", cell.ColumnSpan)
            })
            {
                string literal = value.ToString(CultureInfo.InvariantCulture);
                var argument = node.Arguments.FirstOrDefault(a => a.Name == name);
                if (argument is null) missing.Add(name + ": " + literal);
                else if (argument.ValueKind == XuiValueKind.Expression)
                    return Failure("Grid placement contains a C# expression. Edit placement in source before duplication.");
                else changes.Add((new(argument.ValueSpan.Start - node.Span.Start, argument.ValueSpan.Length), literal));
            }
            if (missing.Count > 0)
            {
                int offset = node.Arguments.Count == 0 ? node.ArgumentsSpan.Start : node.Arguments[^1].ValueSpan.End;
                changes.Add((new(offset - node.Span.Start, 0),
                    (node.Arguments.Count == 0 ? "" : ", ") + string.Join(", ", missing)));
            }
            foreach (var change in changes.OrderByDescending(c => c.Range.Start))
                duplicate = duplicate.Remove(change.Range.Start, change.Range.Length).Insert(change.Range.Start, change.Text);
        }
        string separator = NewLine() + Indent(node.Span.Start);
        string replacement = separator + duplicate;
        return Propose(revision, new(node.Span.End, 0), replacement,
            node.Span.End + separator.Length, parent.Kind == "Grid" ? parent.Id : null, cancellation);
    }

    public VisualEditResult MoveNode(Guid revision, int nodeId, int delta, CancellationToken cancellation = default)
    {
        if (Target(revision, nodeId, out var node) is { } error) return Failure(error);
        if (delta is not (-1 or 1)) return Failure("Move by -1 or 1 to an adjacent sibling.");
        if (!parents.TryGetValue(node.Id, out var parent)) return Failure("The view root cannot be moved.");
        int index = IndexOf(parent, node.Id);
        int destination = index + delta;
        if (destination < 0 || destination >= parent.Children.Count)
            return Failure("There is no sibling in that direction.");
        var other = parent.Children[destination];
        var first = delta < 0 ? other : node;
        var second = delta < 0 ? node : other;
        string gap = Source[first.Span.End..second.Span.Start];
        string replacement = Slice(second.Span) + gap + Slice(first.Span);
        int selection = delta < 0 ? first.Span.Start : first.Span.Start + second.Span.Length + gap.Length;
        return Propose(revision, new(first.Span.Start, second.Span.End - first.Span.Start),
            replacement, selection, null, cancellation);
    }

    public VisualEditResult InsertSibling(Guid revision, int nodeId, bool after, ControlTemplate template,
        GridPlacement? placement = null, CancellationToken cancellation = default)
    {
        cancellation.ThrowIfCancellationRequested();
        if (Target(revision, nodeId, out var node) is { } error) return Failure(error);
        if (VariableParent(node, out var parent) is { } reason) return Failure(reason);
        int index = parent.Children.ToList().FindIndex(child => child.Id == node.Id);
        return InsertControl(revision, parent.Id, index + (after ? 1 : 0), template, placement, cancellation);
    }

    public VisualEditResult InsertControl(Guid revision, int parentId, int index, ControlTemplate template,
        GridPlacement? placement = null, CancellationToken cancellation = default)
    {
        if (Target(revision, parentId, out var parent) is { } error) return Failure(error);
        if (parent.Kind is not ("VStack" or "HStack" or "Grid"))
            return Failure("Insert into a Stack or Grid. ScrollView and Popup require one child; SplitView requires exactly two.");
        if (index < 0 || index > parent.Children.Count)
            return Failure("The insertion index must be between zero and the number of children.");
        if (parent.Kind == "Grid" && placement is null)
            return Failure("Specify a GridPlacement for insertion into a Grid.");
        if (parent.Kind != "Grid" && placement is not null)
            return Failure("GridPlacement requires a Grid parent.");
        if (placement is { } cell && (cell.Row < 0 || cell.Column < 0 || cell.RowSpan < 1 || cell.ColumnSpan < 1))
            return Failure("Grid rows and columns must be nonnegative; spans must be positive.");
        string? control = Template(template, placement);
        if (control is null) return Failure("Select a supported control template.");
        int offset = index < parent.Children.Count ? parent.Children[index].Span.Start : parent.BodySpan!.Value.End;
        string indent = index < parent.Children.Count ? Indent(offset) : Indent(parent.Span.Start) + "    ";
        string prefix = index < parent.Children.Count ? "" : NewLine() + indent;
        string suffix = NewLine() + (index < parent.Children.Count ? indent : Indent(parent.Span.Start));
        return Propose(revision, new(offset, 0), prefix + control + suffix, offset + prefix.Length,
            parent.Kind == "Grid" ? parent.Id : null, cancellation);
    }

    public VisualEditResult WrapNode(Guid revision, int nodeId, ControlTemplate wrapper,
        CancellationToken cancellation = default)
    {
        cancellation.ThrowIfCancellationRequested();
        if (Target(revision, nodeId, out var node) is { } error) return Failure(error);
        if (wrapper is not (ControlTemplate.VStack or ControlTemplate.HStack or ControlTemplate.ScrollView))
            return Failure("Wrap with VStack, HStack, or ScrollView. Edit other wrapper structures in source.");
        var placement = node.Arguments.Where(IsPlacement).ToArray();
        string arguments = string.Join(", ", placement.Select(a => Slice(a.Span)));
        if (wrapper == ControlTemplate.ScrollView)
            arguments = "\"Scroll\"" + (arguments.Length == 0 ? "" : ", " + arguments);
        string subtree = RemoveArguments(node, placement);
        string separator = NewLine() + Indent(node.Span.Start);
        string replacement = wrapper + "(" + arguments + ") {" + separator + subtree + separator + "}";
        return Propose(revision, node.Span, replacement, node.Span.Start, null, cancellation);
    }

    public VisualEditResult UnwrapNode(Guid revision, int nodeId, CancellationToken cancellation = default)
    {
        cancellation.ThrowIfCancellationRequested();
        if (Target(revision, nodeId, out var node) is { } error) return Failure(error);
        if (node.BodySpan is not { } body || node.Children.Count != 1)
            return Failure("Unwrap requires a container with exactly one child. Edit other structures in source.");
        var child = node.Children[0];
        var placement = node.Arguments.Where(IsPlacement).ToArray();
        foreach (var argument in node.Arguments.Where(a => !IsPlacement(a)))
        {
            if (argument.IsPositional && argument.ValueKind == XuiValueKind.String) continue;
            return Failure($"Unwrapping would discard '{argument.Name}' and its authored value or identity. Edit the wrapper in source.");
        }
        if (child.Arguments.Any(IsPlacement))
            return Failure("The child has its own placement arguments. Reconcile inner and outer placement in source before unwrapping.");
        int headerLength = body.Start - node.Span.Start;
        foreach (var token in SyntaxFactory.ParseTokens(Source.Substring(node.Span.Start, headerLength)))
            foreach (var trivia in token.LeadingTrivia.Concat(token.TrailingTrivia))
            {
                if (trivia.IsKind(SyntaxKind.WhitespaceTrivia) || trivia.IsKind(SyntaxKind.EndOfLineTrivia)) continue;
                int offset = node.Span.Start + trivia.SpanStart;
                if (placement.Any(a => a.Span.Contains(offset) && offset + trivia.Span.Length <= a.Span.End)) continue;
                return Failure("The wrapper header contains comments or directives. Preserve them in source before unwrapping.");
            }
        string replacement = Slice(body);
        if (placement.Length != 0)
        {
            int offset = child.Arguments.Count == 0 ? child.ArgumentsSpan.Start : child.Arguments[^1].ValueSpan.End;
            string arguments = (child.Arguments.Count == 0 ? "" : ", ") + string.Join(", ", placement.Select(a => Slice(a.Span)));
            replacement = replacement.Insert(offset - body.Start, arguments);
        }
        int selectionOffset = node.Span.Start + child.Span.Start - body.Start;
        return Propose(revision, node.Span, replacement, selectionOffset, null, cancellation);
    }

    private static bool IsPlacement(XuiSourceArgument argument) =>
        argument.Name is "row" or "column" or "rowSpan" or "columnSpan" or "flex";

    private string RemoveArguments(XuiSourceNode node, IReadOnlyList<XuiSourceArgument> removed)
    {
        if (removed.Count == 0) return Slice(node.Span);
        var ranges = removed.Select(a => a.Span).ToList();
        var retained = node.Arguments.Select((argument, index) => (argument, index))
            .Where(pair => !removed.Contains(pair.argument)).Select(pair => pair.index).ToArray();
        for (int i = 0; i < node.Arguments.Count; i++)
        {
            bool keepComma = retained.Contains(i) && (i != retained[^1] || node.HasTrailingComma);
            if (keepComma) continue;
            int start = node.Arguments[i].ValueSpan.End;
            int end = i + 1 < node.Arguments.Count ? node.Arguments[i + 1].Span.Start : node.ArgumentsSpan.End;
            foreach (var token in SyntaxFactory.ParseTokens(Source[start..end]))
                if (token.IsKind(SyntaxKind.CommaToken))
                    ranges.Add(new(start + token.SpanStart, token.Span.Length));
        }
        string result = Slice(node.Span);
        foreach (var range in ranges.OrderByDescending(r => r.Start))
            result = result.Remove(range.Start - node.Span.Start, range.Length);
        return result;
    }

    private string? Target(Guid revision, int nodeId, out XuiSourceNode node)
    {
        node = null!;
        if (revision != Revision) return "The document revision is stale. Parse the current editor text and select the node again.";
        if (!Success) return "Repair source errors before visual editing: " + string.Join("; ", Diagnostics.Select(d => d.Message));
        if (!nodes.TryGetValue(nodeId, out var found))
            return "The node does not exist in this revision. Select a node from the current hierarchy.";
        node = found;
        return null;
    }

    private string? VariableParent(XuiSourceNode node, out XuiSourceNode parent)
    {
        parent = null!;
        if (!parents.TryGetValue(node.Id, out var found)) return "The view requires one root; its root cannot have siblings, be deleted, or be duplicated.";
        parent = found;
        if (parent.Kind is not ("VStack" or "HStack" or "Grid"))
            return $"{parent.Kind} requires exactly {(parent.Kind == "SplitView" ? "two children" : "one child")}. Edit its child in place.";
        return null;
    }

    private VisualEditResult Propose(Guid revision, SourceRange range, string replacement, int selectionOffset,
        int? gridId, CancellationToken cancellation)
    {
        cancellation.ThrowIfCancellationRequested();
        var originalError = VisualSourceCompilation.Validate(Source, cancellation);
        if (originalError is not null) return Failure("Repair compilation errors before visual editing: " + originalError);
        string candidate = Source.Remove(range.Start, range.Length).Insert(range.Start, replacement);
        var syntax = XuiSourceParser.Parse(candidate, cancellation);
        if (!syntax.Success)
            return Failure("The edit is not valid XUI: " + string.Join("; ", syntax.Diagnostics.Select(d => d.Message)));
        if (gridId is { } id)
        {
            // Property changes and insertions do not change the parent's preorder ID.
            var grid = Descendants(syntax.Root!).FirstOrDefault(n => n.Id == id);
            if (grid is null || grid.Kind != "Grid") return Failure("Placement requires a Grid parent.");
            if (ValidateGrid(grid) is { } placementError) return Failure(placementError);
        }
        var compilationError = VisualSourceCompilation.Validate(candidate, cancellation);
        if (compilationError is not null) return Failure("The edit would not compile: " + compilationError);
        var selected = Descendants(syntax.Root!).FirstOrDefault(n => n.Span.Start == selectionOffset);
        if (selected is null) return Failure("The edit could not preserve a valid node selection.");
        return new(new(revision, Source, range, replacement, selected.Span), null);
    }

    private static string? ValidateGrid(XuiSourceNode grid)
    {
        int? rows = TrackCount(grid, "rows");
        int? columns = TrackCount(grid, "columns");
        if (rows is null || columns is null)
            return "Grid tracks are expressions with unknown lengths. Edit placement in source, or use explicit track array initializers.";
        var cells = new List<(int Row, int Column, int Rows, int Columns)>();
        foreach (var child in grid.Children)
        {
            int? row = Integer(child, "row", 0), column = Integer(child, "column", 0);
            int? rowSpan = Integer(child, "rowSpan", 1), columnSpan = Integer(child, "columnSpan", 1);
            if (row is null || column is null || rowSpan is null || columnSpan is null)
                return "Grid placement contains C# expressions. Use integer literals before changing placement visually.";
            if (row < 0 || column < 0 || rowSpan < 1 || columnSpan < 1 ||
                (long)row + rowSpan > rows || (long)column + columnSpan > columns)
                return "The Grid cell is outside its declared tracks. Add tracks or choose an in-bounds cell.";
            if (cells.Any(c => row < (long)c.Row + c.Rows && (long)row + rowSpan > c.Row &&
                column < (long)c.Column + c.Columns && (long)column + columnSpan > c.Column))
                return "The Grid cell overlaps another child. Choose a distinct cell or edit intentional overlap in source.";
            cells.Add((row.Value, column.Value, rowSpan.Value, columnSpan.Value));
        }
        return null;
    }

    private static int? Integer(XuiSourceNode node, string name, int fallback)
    {
        var argument = node.Arguments.FirstOrDefault(a => a.Name == name);
        if (argument is null) return fallback;
        var syntax = SyntaxFactory.ParseExpression(argument.Value);
        return syntax is LiteralExpressionSyntax literal && literal.Token.Value is int value ? value : null;
    }

    private static int? TrackCount(XuiSourceNode grid, string name)
    {
        var argument = grid.Arguments.FirstOrDefault(a => a.Name == name);
        if (argument is null) return 1;
        return SyntaxFactory.ParseExpression(argument.Value) switch
        {
            ArrayCreationExpressionSyntax { Initializer: { } initializer } => initializer.Expressions.Count,
            ImplicitArrayCreationExpressionSyntax array => array.Initializer.Expressions.Count,
            CollectionExpressionSyntax collection when collection.Elements.All(e => e is ExpressionElementSyntax) =>
                collection.Elements.Count,
            _ => null
        };
    }

    private static string? Template(ControlTemplate template, GridPlacement? placement)
    {
        string place = placement is { } cell
            ? string.Create(CultureInfo.InvariantCulture,
                $", row: {cell.Row}, column: {cell.Column}, rowSpan: {cell.RowSpan}, columnSpan: {cell.ColumnSpan}")
            : "";
        string stackPlace = place.Length == 0 ? "" : place[2..];
        return template switch
        {
            ControlTemplate.Text => $"Text(\"Text\"{place});",
            ControlTemplate.Button => $"Button(\"Button\"{place});",
            ControlTemplate.Toggle => $"Toggle(\"Toggle\"{place});",
            ControlTemplate.ToggleSwitch => $"ToggleSwitch(\"Switch\", checked: false{place});",
            ControlTemplate.ToggleButton => $"ToggleButton(\"Toggle button\", checked: false{place});",
            ControlTemplate.TextInput => $"TextInput(\"Input\"{place});",
            ControlTemplate.VStack => $"VStack({stackPlace}) {{ }}",
            ControlTemplate.HStack => $"HStack({stackPlace}) {{ }}",
            ControlTemplate.Grid => $"Grid(\"Grid\"{place}) {{ }}",
            ControlTemplate.ScrollView => $"ScrollView(\"Scroll\"{place}) {{ VStack() {{ }} }}",
            ControlTemplate.SplitView => $"SplitView(\"Split\"{place}) {{ VStack() {{ }} VStack() {{ }} }}",
            ControlTemplate.DataGrid => $"DataGrid(\"Data\", columns: new global::Xui.GridColumn[] {{ new(\"Name\", 160), new(\"Value\", 160) }}, preferredSize: (360, 200){place});",
            ControlTemplate.NavigationView => $"NavigationView(\"Navigation\", headerVisible: true, preferredSize: (240, 240){place});",
            ControlTemplate.RangeInput => $"RangeInput(\"Value\", currentValue: 50, preferredSize: (320, 42){place});",
            ControlTemplate.Progress => $"Progress(\"Progress\", currentValue: 50, preferredSize: (320, 24){place});",
            ControlTemplate.ProgressRing => $"ProgressRing(\"Loading\", preferredSize: (32, 32){place});",
            ControlTemplate.CheckBox => $"CheckBox(\"Check box\", checkState: global::Xui.CheckState.Unchecked{place});",
            ControlTemplate.HyperlinkButton => $"HyperlinkButton(\"Link\"{place});",
            ControlTemplate.SelectorBar => $"SelectorBar(\"Selector\", items: new global::Xui.Choice[] {{ new(1, \"First\"), new(2, \"Second\") }}, selected: 1{place});",
            ControlTemplate.InfoBadge => $"InfoBadge(\"Notification\"{place});",
            ControlTemplate.MenuBar => $"MenuBar(\"Menu\", commands: new global::Xui.Command[] {{ new(1, \"File\", Kind: global::Xui.CommandKind.Submenu), new(2, \"Open\", Parent: 1) }}{place});",
            _ => null
        };
    }

    private string Slice(SourceRange range) => Source.Substring(range.Start, range.Length);
    private string NewLine()
    {
        int index = Source.IndexOfAny(['\r', '\n']);
        return index < 0 ? "\r" :
            Source[index] == '\r' && index + 1 < Source.Length && Source[index + 1] == '\n' ? "\r\n" : Source[index].ToString();
    }
    private string Indent(int offset)
    {
        int start = offset;
        while (start > 0 && Source[start - 1] is not ('\r' or '\n')) start--;
        int end = start;
        while (end < offset && Source[end] is ' ' or '\t') end++;
        return Source[start..end];
    }
    private static int IndexOf(XuiSourceNode parent, int id)
    {
        for (int i = 0; i < parent.Children.Count; i++)
            if (parent.Children[i].Id == id) return i;
        return -1;
    }
    private static IEnumerable<XuiSourceNode> Descendants(XuiSourceNode node)
    {
        yield return node;
        foreach (var child in node.Children)
            foreach (var descendant in Descendants(child)) yield return descendant;
    }
    private static VisualEditResult Failure(string message) => new(null, message);
}

internal static class VisualSourceCompilation
{
    private static readonly Lazy<MetadataReference[]> References = new(() =>
    {
        string platforms = AppContext.GetData("TRUSTED_PLATFORM_ASSEMBLIES") as string ??
            throw new InvalidOperationException("Source editing requires trusted platform assembly paths.");
        return platforms.Split(Path.PathSeparator, StringSplitOptions.RemoveEmptyEntries)
            .Append(typeof(global::Xui.Window).Assembly.Location).Distinct(StringComparer.OrdinalIgnoreCase)
            .Select(path => MetadataReference.CreateFromFile(path)).ToArray();
    });

    internal static string? Validate(string source, CancellationToken cancellation)
    {
        cancellation.ThrowIfCancellationRequested();
        var input = CSharpCompilation.Create("Xui.Designer.SourceValidation",
            references: References.Value,
            options: new CSharpCompilationOptions(OutputKind.DynamicallyLinkedLibrary,
                nullableContextOptions: NullableContextOptions.Enable));
        GeneratorDriver driver = CSharpGeneratorDriver.Create(
            generators: [new XuiGenerator().AsSourceGenerator()],
            additionalTexts: [new SourceFile(source)],
            parseOptions: CSharpParseOptions.Default.WithLanguageVersion(LanguageVersion.Latest));
        driver.RunGeneratorsAndUpdateCompilation(input, out var generated, out var diagnostics, cancellation);
        using var output = new MemoryStream();
        var emitted = generated.Emit(output, cancellationToken: cancellation);
        var errors = diagnostics.Concat(emitted.Diagnostics).Where(d => d.Severity == DiagnosticSeverity.Error)
            .Select(d => d.ToString()).Distinct(StringComparer.Ordinal).ToArray();
        return errors.Length == 0 ? null : string.Join(Environment.NewLine, errors);
    }

    private sealed class SourceFile(string source) : AdditionalText
    {
        public override string Path => "DesignerSource.xui";
        public override SourceText GetText(CancellationToken cancellationToken = default)
        {
            cancellationToken.ThrowIfCancellationRequested();
            return SourceText.From(source);
        }
    }
}
