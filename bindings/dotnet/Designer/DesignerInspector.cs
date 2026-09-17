using Xui.Generator;

namespace Xui.Designer;

internal sealed class DesignerInspector
{
    private readonly ComboBox arguments;
    private readonly ComboBox palette;
    private XuiSourceNode? node;
    private string[] names = [];
    private int argumentIndex;
    private bool resetting, editable, validationPending;

    internal DesignerInspectorLayout Layout { get; }
    internal MultilineText Value { get; }
    internal string? Argument => argumentIndex >= 0 && argumentIndex < names.Length ? names[argumentIndex] : null;
    internal ControlTemplate Template { get; private set; } = ControlTemplate.Text;

    internal DesignerInspector(Window window)
    {
        arguments = window.ComboBox("Selected control argument", false).SetAutomationId("designer-arguments");
        palette = window.ComboBox("Control insertion template", false).SetAutomationId("designer-control-palette");
        Value = window.MultilineText("Argument literal source").SetMaximumLength(65536).SetAutomationId("designer-property-value");
        Value.SetControlStyleValues(StylePart.Text, new PartStyleValues { FontFamily = "Consolas", FontSize = 13 });
        Layout = new DesignerInspectorLayout(window, arguments, Value, palette, attach: false);
        palette.SetItems(Enum.GetValues<ControlTemplate>().Select(t => new Choice((ulong)t + 1, t.ToString())).ToArray(), 1);
        palette.Event += e => { if (e.Kind == EventKind.Selection) Template = (ControlTemplate)(e.Value - 1); };
        arguments.Event += e =>
        {
            if (resetting || e.Kind != EventKind.Selection) return;
            argumentIndex = checked((int)e.Value - 1);
            ShowArgument();
        };
        Show(null, null, false);
    }

    internal void Show(XuiSourceNode? selected, XuiSourceNode? parent, bool canEdit, bool validating = false)
    {
        string? preferred = ReferenceEquals(node, selected) ? Argument : selected?.Arguments.FirstOrDefault(a => a.IsPositional)?.Name;
        node = selected;
        editable = canEdit;
        validationPending = validating;
        Layout.Selected.Text = selected is null ? "Select a control" : $"{selected.Kind} at UTF-16 {selected.Span.Start}..{selected.Span.End}";
        names = selected is null ? [] : selected.Arguments.Select(a => a.Name)
            .Concat(selected.SupportedArguments).Distinct(StringComparer.Ordinal).Order(StringComparer.Ordinal).ToArray();
        preferred ??= selected?.Arguments.FirstOrDefault()?.Name;
        argumentIndex = preferred is null ? -1 : Array.IndexOf(names, preferred);
        if (argumentIndex < 0 && names.Length > 0) argumentIndex = 0;
        resetting = true;
        try { arguments.SetItems(names.Select((name, i) => new Choice((ulong)i + 1, name)).ToArray(),
            argumentIndex >= 0 ? (ulong)argumentIndex + 1 : null); }
        finally { resetting = false; }
        arguments.Enabled = selected is not null && canEdit;
        bool siblings = parent?.Kind is "VStack" or "HStack" or "Grid";
        bool movable = siblings || parent?.Kind == "SplitView";
        int index = selected is null || parent is null ? -1 : parent.Children.ToList().FindIndex(n => n.Id == selected.Id);
        Layout.Delete.Enabled = canEdit && siblings;
        Layout.Duplicate.Enabled = canEdit && siblings;
        Layout.Up.Enabled = canEdit && movable && index > 0;
        Layout.Down.Enabled = canEdit && movable && index >= 0 && index + 1 < parent!.Children.Count;
        Layout.WrapVertical.Enabled = canEdit && selected is not null;
        Layout.WrapHorizontal.Enabled = canEdit && selected is not null;
        Layout.WrapScroll.Enabled = canEdit && selected is not null;
        Layout.Unwrap.Enabled = canEdit && selected?.BodySpan is not null && selected.Children.Count == 1;
        bool insert = canEdit && selected?.Kind is "VStack" or "HStack" or "Grid";
        Layout.Insert.Enabled = insert;
        palette.Enabled = insert;
        bool grid = selected?.Kind == "Grid" || parent?.Kind == "Grid";
        Layout.Row.Enabled = canEdit && grid;
        Layout.Column.Enabled = canEdit && grid;
        Layout.StructureHelp.Text = selected is null ? "Select a control to change its structure."
            : parent is null ? "The root cannot move, duplicate, or delete. Insert children into a stack or grid."
            : !siblings && !movable ? "This container requires its children. Edit its structure in source."
            : parent.Kind == "SplitView" ? "SplitView keeps both panes. Move swaps their order."
            : parent.Kind == "Grid" ? "Grid duplicates need an empty cell. Placement and overlap checks run before Apply."
            : "Move changes sibling order. Duplicate rejects shared IDs and Content references.";
        ShowArgument();
    }

    internal void ChooseArgument(string name)
    {
        int index = Array.IndexOf(names, name);
        if (index < 0) throw new ArgumentException($"The selected control has no supported argument '{name}'.", nameof(name));
        argumentIndex = index;
        arguments.Select((ulong)index + 1);
        ShowArgument();
    }

    private void ShowArgument()
    {
        var argument = node?.Arguments.FirstOrDefault(a => a.Name == Argument);
        bool expression = argument?.ValueKind == XuiValueKind.Expression;
        bool writable = editable && Argument is not null && !expression;
        Value.Text = argument?.Value ?? "";
        Value.ReadOnly = !writable;
        Layout.Apply.Enabled = writable;
        Layout.ArgumentHelp.Text = !editable ? validationPending
            ? "Read-only while the visual edit is validated."
            : "Read-only until the hierarchy matches valid source."
            : expression ? "Expression: read-only here. Change this argument in the source editor."
            : Argument is null ? "This control has no editable arguments."
            : argument is null ? "Not set. Enter a literal source value; Apply validates its type."
            : $"{argument.ValueKind} literal. Apply validates the complete component.";
    }
}
