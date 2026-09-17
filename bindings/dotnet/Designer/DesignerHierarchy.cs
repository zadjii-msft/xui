using Xui.Generator;

namespace Xui.Designer;

internal sealed class DesignerHierarchy
{
    private readonly Window window;
    private Dictionary<int, XuiSourceNode> nodes = [];
    private Dictionary<int, int> parents = [];
    private ulong generation;
    private bool selecting;
    private Guid revision;

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
    }

    private void Add(XuiSourceNode node, XuiSourceNode? parent)
    {
        nodes.Add(node.Id, node);
        if (parent is not null) parents.Add(node.Id, parent.Id);
        foreach (var child in node.Children) Add(child, node);
    }

    internal XuiSourceNode? Parent(XuiSourceNode node) =>
        parents.TryGetValue(node.Id, out var id) ? nodes[id] : null;

    internal void Select(XuiSourceNode node)
    {
        if (!nodes.TryGetValue(node.Id, out var known) || !ReferenceEquals(node, known))
            throw new InvalidOperationException("The selected node does not belong to this hierarchy revision.");
        var ancestors = new Stack<XuiSourceNode>();
        for (var parent = Parent(node); parent is not null; parent = Parent(parent)) ancestors.Push(parent);
        selecting = true;
        try
        {
            foreach (var parent in ancestors) Tree.Expand(Key(parent));
            Tree.Select(Key(node));
            if (Tree.Selection.Focused != Key(node))
                throw new InvalidOperationException("The native hierarchy could not reveal the selected control.");
            Selection = node;
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
