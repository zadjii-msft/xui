using Xui.Generator;

namespace Xui.Designer;

internal enum DesignerSelectionTarget { Parent, FirstChild, PreviousSibling, NextSibling, Root }

internal sealed class DesignerHierarchy
{
    private readonly Window window;
    private Dictionary<int, XuiSourceNode> nodes = [];
    private Dictionary<int, int> parents = [];
    private ulong generation;
    private bool selecting;
    private Guid revision;
    private bool searchCurrent;
    private XuiSourceNode[] matches = [];

    internal TreeView Tree { get; }
    internal DesignerHierarchyLayout Layout { get; }
    internal event Action<XuiSourceNode>? Selected;
    internal XuiSourceNode? Selection { get; private set; }

    internal DesignerHierarchy(Window window)
    {
        this.window = window;
        Tree = window.TreeView("Source control hierarchy").SetAutomationId("designer-hierarchy")
            .Help("Expand a control to see its children.");
        Tree.SetControlStyleValues(StylePart.Root, new PartStyleValues { RowHeight = 28, Indentation = 16 });
        Tree.SetControlStyleValues(StylePart.Row, new PartStyleValues { Padding = new Insets(4, 2, 4, 2) });
        Layout = new DesignerHierarchyLayout(window, Tree, attach: false);
        Layout.Query.Changed += _ => RefreshSearch();
        Layout.ClearSearch.Click += ClearSearch;
        Layout.NextMatch.Click += () => MoveSearch();
        Layout.PreviousMatch.Click += () => MoveSearch(reverse: true);
        using (var source = window.ImmutableSource(new Rows([], 0, nodes))) Tree.SetSource(source);
        Tree.OnRequest(request =>
        {
            using (request)
            {
                var key = request.Node;
                if (key.Version != generation || !nodes.TryGetValue(checked((int)key.Id - 1), out var node))
                    return;
                using var children = window.ImmutableSource(new Rows(node.Children, generation, nodes));
                request.Complete(children);
            }
        });
        Tree.Event += e =>
        {
            if (selecting || e.Kind != EventKind.Selection) return;
            if (Tree.Selection.Focused is { } key && key.Version == generation &&
                nodes.TryGetValue(checked((int)key.Id - 1), out var node))
            {
                Selection = node;
                UpdateSearchState();
                Selected?.Invoke(node);
            }
        };
    }

    internal void SetDocument(VisualDocument document)
    {
        if (revision == document.Revision) return;
        revision = document.Revision;
        generation = checked(generation + 1);
        nodes = [];
        parents = [];
        Selection = null;
        if (document.Root is { } root) Add(root, null);
        using var source = window.ImmutableSource(new Rows(document.Root is { } value ? [value] : [], generation, nodes));
        selecting = true;
        try { Tree.SetSource(source); }
        finally { selecting = false; }
        if (document.Root is { Children.Count: > 0 } expanded) Tree.Expand(Key(expanded));
        RefreshSearch();
    }

    internal void SetSearchCurrent(bool current)
    {
        searchCurrent = current;
        UpdateSearchState();
    }

    internal void RefreshSearch()
    {
        string[] terms = Layout.Query.Text.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);
        matches = terms.Length == 0 ? [] : nodes.Values.Where(node =>
        {
            string text = node.Kind + " " + string.Join(" ", node.Arguments.Select(argument => $"{argument.Name} {argument.Value}"));
            return terms.All(term => text.Contains(term, StringComparison.OrdinalIgnoreCase));
        }).OrderBy(node => node.Span.Start).ToArray();
        UpdateSearchState();
    }

    private void UpdateSearchState()
    {
        Layout.NextMatch.Enabled = Layout.PreviousMatch.Enabled = searchCurrent && matches.Length > 0;
        int selected = Array.FindIndex(matches, node => ReferenceEquals(node, Selection));
        Layout.SearchStatus.Text = !searchCurrent ? "Search waits for current source."
            : string.IsNullOrWhiteSpace(Layout.Query.Text) ? "Enter a query."
            : matches.Length == 0 ? "No controls match."
            : selected >= 0 ? $"Match {selected + 1} of {matches.Length}."
            : matches.Length == 1 ? "1 matching control." : $"{matches.Length} matching controls.";
    }

    internal void MoveSearch(bool reverse = false)
    {
        RefreshSearch();
        if (!searchCurrent || matches.Length == 0) return;
        int selected = Array.FindIndex(matches, node => ReferenceEquals(node, Selection));
        int next = selected < 0 ? reverse ? matches.Length - 1 : 0
            : (selected + (reverse ? matches.Length - 1 : 1)) % matches.Length;
        var node = matches[next];
        Select(node);
        Selected?.Invoke(node);
    }

    internal bool HandleSearchKey(UiKeyEvent key)
    {
        if (!Layout.Query.Focused) return false;
        if (key.VirtualKey == 0x0D && key.Modifiers is KeyModifiers.None or KeyModifiers.Shift)
        {
            MoveSearch(key.Modifiers == KeyModifiers.Shift);
            return true;
        }
        if (key.VirtualKey == 0x1B && key.Modifiers == KeyModifiers.None)
        {
            ClearSearch();
            return true;
        }
        return false;
    }

    private void ClearSearch()
    {
        Layout.Query.Text = "";
        RefreshSearch();
        Layout.Query.Focus();
    }

    private void Add(XuiSourceNode node, XuiSourceNode? parent)
    {
        nodes.Add(node.Id, node);
        if (parent is not null) parents.Add(node.Id, parent.Id);
        foreach (var child in node.Children) Add(child, node);
    }

    internal XuiSourceNode? Parent(XuiSourceNode node) =>
        parents.TryGetValue(node.Id, out var id) ? nodes[id] : null;

    internal XuiSourceNode? SelectionTarget(DesignerSelectionTarget target)
    {
        if (Selection is not { } selected) return null;
        var parent = Parent(selected);
        switch (target)
        {
            case DesignerSelectionTarget.Parent: return parent;
            case DesignerSelectionTarget.FirstChild: return selected.Children.FirstOrDefault();
            case DesignerSelectionTarget.Root:
                if (parent is null) return null;
                while (Parent(parent) is { } ancestor) parent = ancestor;
                return parent;
            case DesignerSelectionTarget.PreviousSibling:
            case DesignerSelectionTarget.NextSibling:
                if (parent is null) return null;
                for (int index = 0; index < parent.Children.Count; index++)
                {
                    if (!ReferenceEquals(parent.Children[index], selected)) continue;
                    int next = index + (target == DesignerSelectionTarget.PreviousSibling ? -1 : 1);
                    return next >= 0 && next < parent.Children.Count ? parent.Children[next] : null;
                }
                return null;
            default: throw new ArgumentOutOfRangeException(nameof(target));
        }
    }

    private void RevealAncestors(XuiSourceNode node)
    {
        if (!nodes.TryGetValue(node.Id, out var known) || !ReferenceEquals(node, known))
            throw new InvalidOperationException("The selected node does not belong to this hierarchy revision.");
        var ancestors = new Stack<XuiSourceNode>();
        for (var parent = Parent(node); parent is not null; parent = Parent(parent)) ancestors.Push(parent);
        foreach (var parent in ancestors) Tree.Expand(Key(parent));
    }

    internal void ExpandBranch(XuiSourceNode node)
    {
        RevealAncestors(node);
        Tree.Expand(Key(node));
    }

    internal void Select(XuiSourceNode node)
    {
        selecting = true;
        try
        {
            RevealAncestors(node);
            Tree.Select(Key(node));
            if (Tree.Selection.Focused != Key(node))
                throw new InvalidOperationException("The native hierarchy could not reveal the selected control.");
            Selection = node;
            UpdateSearchState();
        }
        finally { selecting = false; }
    }

    internal ItemKey Key(XuiSourceNode node) => new(checked((ulong)node.Id + 1), generation);

    private sealed class Rows(IReadOnlyList<XuiSourceNode> nodes, ulong generation,
        IReadOnlyDictionary<int, XuiSourceNode> hierarchy) : IReadOnlyImmutableSource
    {
        public ulong Count => (ulong)nodes.Count;
        public ItemKey Key(ulong index) => new(checked((ulong)nodes[checked((int)index)].Id + 1), generation);
        public ulong? Find(ItemKey key)
        {
            if (key.Version != generation) return null;
            for (int i = 0; i < nodes.Count; i++)
                if (checked((ulong)nodes[i].Id + 1) == key.Id) return (ulong)i;
            return null;
        }
        public ItemContent Item(ulong index, ulong column = 0)
        {
            var node = nodes[checked((int)index)];
            var label = node.Arguments.FirstOrDefault(a => a.IsPositional)?.Value;
            if (label is { Length: > 50 })
            {
                int end = char.IsHighSurrogate(label[46]) ? 46 : 47;
                label = label[..end] + "...";
            }
            return new(label is null ? node.Kind : $"{node.Kind} {label}");
        }
        public bool HasChildren(ItemKey key) => key.Version == generation && key.Id is > 0 and <= int.MaxValue &&
            hierarchy.TryGetValue((int)key.Id - 1, out var node) && node.Children.Count > 0;
    }
}
