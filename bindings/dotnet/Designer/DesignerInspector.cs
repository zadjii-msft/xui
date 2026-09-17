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
    private bool textMode;
    private string textExpression = "";
    private bool dimensionMode;
    private string dimensionExpression = "";
    private bool updatingPalette;
    private ControlTemplate[] matchingTemplates = [];

    internal DesignerInspectorLayout Layout { get; }
    internal MultilineText Value { get; }
    internal string? Argument => argumentIndex >= 0 && argumentIndex < names.Length ? names[argumentIndex] : null;
    internal ControlTemplate? Template { get; private set; } = ControlTemplate.Text;
    internal bool IsTextMode => textMode;
    internal bool IsDimensionMode => dimensionMode;

    internal DesignerInspector(Window window)
    {
        arguments = window.ComboBox("Selected control argument", false).SetAutomationId("designer-arguments");
        palette = window.ComboBox("Control insertion template", false).SetAutomationId("designer-control-palette");
        Value = window.MultilineText("Argument value").SetMaximumLength(65536).SetAutomationId("designer-property-value");
        Value.SetControlStyleValues(StylePart.Text, new PartStyleValues { FontFamily = "Consolas", FontSize = 13 });
        Layout = new DesignerInspectorLayout(window, arguments, Value, palette, attach: false);
        Layout.TextMode.Changed += ChangeTextMode;
        Layout.DimensionMode.Changed += ChangeDimensionMode;
        Layout.PaletteFilter.Event += e => { if (e.Kind == EventKind.Change) FilterPalette(); };
        Layout.ClearPaletteFilter.Click += () =>
        {
            Layout.PaletteFilter.Text = "";
            FilterPalette();
            Layout.PaletteFilter.Focus();
        };
        palette.Event += e =>
        {
            if (updatingPalette || e.Kind != EventKind.Selection) return;
            int index = Array.FindIndex(matchingTemplates, t => (ulong)t + 1 == e.Value);
            Template = index < 0 ? null : matchingTemplates[index];
            UpdatePaletteState();
        };
        arguments.Event += e =>
        {
            if (resetting || e.Kind != EventKind.Selection) return;
            argumentIndex = checked((int)e.Value - 1);
            ShowArgument();
        };
        FilterPalette();
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
        UpdatePaletteState();
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

    internal void FilterPalette()
    {
        matchingTemplates = FindTemplates(Layout.PaletteFilter.Text);
        if (Template is not { } selected || !matchingTemplates.Contains(selected))
            Template = matchingTemplates.Length == 0 ? null : matchingTemplates[0];
        updatingPalette = true;
        try
        {
            palette.SetItems(matchingTemplates.Select(t => new Choice((ulong)t + 1, t.ToString())).ToArray(),
                Template is { } template ? (ulong)template + 1 : null);
        }
        finally { updatingPalette = false; }
        UpdatePaletteState();
    }

    private void UpdatePaletteState()
    {
        Layout.Insert.Enabled = editable && (node?.Kind is "VStack" or "HStack" or "Grid") && Template is not null;
        palette.Enabled = matchingTemplates.Length > 0;
        string count = matchingTemplates.Length == 1 ? "1 control." : $"{matchingTemplates.Length} controls.";
        Layout.PaletteHelp.Text = Template is { } template
            ? $"{count} {DescribeTemplate(template)}"
            : matchingTemplates.Length == 0 ? "No controls match. Change or clear the filter." : "Choose a control from the palette.";
    }

    internal static ControlTemplate[] FindTemplates(string query)
    {
        string[] terms = query.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);
        return Enum.GetValues<ControlTemplate>().Where(template =>
        {
            string text = template + " " + DescribeTemplate(template);
            return terms.All(term => text.Contains(term, StringComparison.OrdinalIgnoreCase));
        }).ToArray();
    }

    private static string DescribeTemplate(ControlTemplate template) => template switch
    {
        ControlTemplate.Text => "Text label for read-only content.",
        ControlTemplate.Button => "Action button with a click handler.",
        ControlTemplate.Toggle => "Boolean choice with a caption.",
        ControlTemplate.TextInput => "Native single-line text field with a caption.",
        ControlTemplate.VStack => "Layout container that arranges controls vertically.",
        ControlTemplate.HStack => "Layout container that arranges controls horizontally.",
        ControlTemplate.Grid => "Layout container with rows and columns.",
        ControlTemplate.ScrollView => "Scrollable layout container with a vertical stack.",
        ControlTemplate.SplitView => "Resizable two-pane layout with a divider.",
        ControlTemplate.DataGrid => "Data table with two example columns.",
        ControlTemplate.NavigationView => "Navigation sidebar with a header.",
        ControlTemplate.RangeInput => "Slider for a numeric value.",
        ControlTemplate.Progress => "Progress bar for a numeric value.",
        ControlTemplate.ToggleSwitch => "On-off switch for a boolean choice.",
        ControlTemplate.ToggleButton => "Button that retains its checked state.",
        ControlTemplate.ProgressRing => "Circular progress indicator for loading.",
        ControlTemplate.CheckBox => "Checkbox with an unchecked initial state.",
        ControlTemplate.HyperlinkButton => "Link-style action button.",
        ControlTemplate.SelectorBar => "Selection bar with two example choices.",
        ControlTemplate.InfoBadge => "Notification badge.",
        ControlTemplate.MenuBar => "Menu with example File and Open commands.",
        _ => throw new ArgumentOutOfRangeException(nameof(template), template, "Unknown control template.")
    };

    internal void ChooseArgument(string name)
    {
        int index = Array.IndexOf(names, name);
        if (index < 0) throw new ArgumentException($"The selected control has no supported argument '{name}'.", nameof(name));
        argumentIndex = index;
        arguments.Select((ulong)index + 1);
        ShowArgument();
    }

    internal void FocusValue()
    {
        if (dimensionMode) Layout.DimensionWidth.Focus();
        else Value.Focus();
    }

    private void ShowArgument()
    {
        var argument = node?.Arguments.FirstOrDefault(a => a.Name == Argument);
        bool expression = argument?.ValueKind == XuiValueKind.Expression;
        bool writable = editable && Argument is not null && !expression;
        textMode = false;
        Layout.TextMode.Checked = false;
        dimensionMode = false;
        Layout.DimensionMode.Checked = false;
        Layout.DimensionsOpen = false;
        Layout.DimensionArgument = Argument is "size" or "preferredSize";
        string? dimensionError = null;
        bool supportsDimensions = Layout.DimensionArgument && argument is not null &&
            DesignerLiteralCodec.TryDecodeDimensions(argument.Value, out _, out _, out dimensionError);
        Layout.DimensionMode.Enabled = writable && supportsDimensions;
        Layout.DimensionWidth.Enabled = Layout.DimensionHeight.Enabled = writable;
        Value.Visible(true);
        string? textError = null;
        bool supportsText = argument?.ValueKind == XuiValueKind.String &&
            DesignerLiteralCodec.TryDecodeText(argument.Value, out _, out textError);
        Layout.TextMode.Enabled = writable && supportsText;
        Layout.ValueLabel.Text = "Literal source value (include quotes for text)";
        Value.Text = argument?.Value ?? "";
        Value.ReadOnly = !writable;
        Layout.Apply.Enabled = writable;
        Layout.Reset.Enabled = writable && argument is { IsPositional: false };
        Layout.ArgumentHelp.Text = !editable ? validationPending
            ? "Read-only while the visual edit is validated."
            : "Read-only until the hierarchy matches valid source."
            : expression ? "Expression: read-only here. Change this argument in the source editor."
            : Argument is null ? "This control has no editable arguments."
            : argument is null ? "Not set. Enter a literal source value; Apply validates its type."
            : $"{argument.ValueKind} literal. Apply validates the complete component.";
        if (writable && textError is not null) Layout.ArgumentHelp.Text += " Text mode unavailable: " + textError;
        if (writable && dimensionError is not null) Layout.ArgumentHelp.Text += " Dimension mode unavailable: " + dimensionError;
        Value.Help(Layout.ArgumentHelp.Text);
        Layout.ArgumentHelp.Visible(!writable || textError is not null || dimensionError is not null);
    }

    internal bool TryReadLiteral(out string value, out string? error)
    {
        value = Value.Text;
        error = null;
        if (dimensionMode)
        {
            try
            {
                value = DesignerLiteralCodec.EncodeDimensions(dimensionExpression, Layout.DimensionWidth.Text, Layout.DimensionHeight.Text);
                return true;
            }
            catch (ArgumentException exception) { error = exception.Message; return false; }
        }
        if (!textMode) return true;
        try { value = DesignerLiteralCodec.EncodeText(textExpression, value); return true; }
        catch (ArgumentException exception) { error = exception.Message; return false; }
    }

    private void ChangeDimensionMode(bool enabled)
    {
        if (enabled == dimensionMode) return;
        if (!editable || !Layout.DimensionArgument || (!dimensionMode && Value.ReadOnly))
        {
            Layout.DimensionMode.Checked = dimensionMode;
            Layout.Feedback.Text = "Dimension mode requires an editable literal size in the current source.";
            return;
        }
        if (enabled)
        {
            string expression = Value.Text;
            if (!DesignerLiteralCodec.TryDecodeDimensions(expression, out string width, out string height, out string? error))
            {
                Layout.DimensionMode.Checked = false;
                Layout.Feedback.Text = error!;
                return;
            }
            dimensionExpression = expression;
            Layout.DimensionWidth.Text = width;
            Layout.DimensionHeight.Text = height;
        }
        else
        {
            if (!TryReadLiteral(out string expression, out string? error))
            {
                Layout.DimensionMode.Checked = true;
                Layout.Feedback.Text = error!;
                return;
            }
            Value.Text = expression;
        }
        dimensionMode = enabled;
        Layout.DimensionsOpen = enabled;
        Value.ReadOnly = enabled;
        Value.Visible(!enabled);
        Layout.ValueLabel.Text = enabled ? "Dimensions (device-independent pixels)" : "Literal source value (include quotes for text)";
        Layout.ArgumentHelp.Text = enabled
            ? "Enter finite, non-negative numeric literals. Apply validates the complete component."
            : "Size tuple. Apply validates the complete component.";
        Layout.ArgumentHelp.Visible(false);
    }

    private void ChangeTextMode(bool enabled)
    {
        if (enabled == textMode) return;
        if (!editable || Value.ReadOnly)
        {
            Layout.TextMode.Checked = textMode;
            Layout.Feedback.Text = "Text mode requires an editable string literal in the current source.";
            return;
        }
        if (enabled)
        {
            string expression = Value.Text;
            if (!DesignerLiteralCodec.TryDecodeText(expression, out string text, out string? error))
            {
                Layout.TextMode.Checked = false;
                Layout.Feedback.Text = error!;
                return;
            }
            textExpression = expression;
            textMode = true;
            Value.Text = text;
        }
        else
        {
            if (!TryReadLiteral(out string expression, out string? error))
            {
                Layout.TextMode.Checked = true;
                Layout.Feedback.Text = error!;
                return;
            }
            textMode = false;
            Value.Text = expression;
        }
        Layout.ValueLabel.Text = textMode ? "Text value (no quotes needed)" : "Literal source value (include quotes for text)";
        Layout.ArgumentHelp.Text = textMode
            ? "Apply encodes quotes and newlines as a C# string literal. Unchanged text preserves the original literal."
            : "String literal. Apply validates the complete component.";
        Value.Help(Layout.ArgumentHelp.Text);
        Layout.ArgumentHelp.Visible(false);
    }
}
