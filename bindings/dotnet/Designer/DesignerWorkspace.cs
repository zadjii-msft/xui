using System.Globalization;
using System.Threading.Channels;
using Xui.Generator;

namespace Xui.Designer;

internal sealed class DesignerWorkspace : IDisposable
{
    private readonly Window window;
    private readonly MultilineText editor;
    private readonly Action<string> report;
    private readonly CancellationTokenSource lifetime = new();
    private readonly Channel<(long Version, string Source)> snapshots = Channel.CreateBounded<(long, string)>(
        new BoundedChannelOptions(1) { FullMode = BoundedChannelFullMode.DropOldest, SingleReader = true });
    private readonly Task parser;
    private readonly object publicationGate = new();
    private (long Version, VisualDocument Document)? pendingDocument;
    private bool publicationScheduled;
    private CancellationTokenSource? editCancellation;
    private Task? editing;
    private long version;
    private string? observedSource;
    private bool disposed, current, busy;

    internal DesignerHierarchy Hierarchy { get; }
    internal DesignerInspector Inspector { get; }
    internal VisualDocument? Document { get; private set; }
    internal bool IsCurrent => current;
    internal bool IsBusy => busy;
    internal bool CanRevertPropertyDraft => !disposed && !busy && current && Document?.Source == editor.Text &&
        Hierarchy.Selection is not null && Inspector.CanRevertDraft;
    internal event Action? Changed;
    internal event Action? SelectionChanged;

    internal DesignerWorkspace(Window window, MultilineText editor, Action<string> report)
    {
        this.window = window;
        this.editor = editor;
        this.report = report;
        Hierarchy = new(window);
        Inspector = new(window);
        Hierarchy.Selected += node =>
        {
            if (!current || Document is null) return;
            SelectNode(node, revealSource: true, selectHierarchy: false);
        };
        Hierarchy.Layout.FromCaret.Click += SelectFromCaret;
        Inspector.Layout.Apply.Click += ApplyProperty;
        Inspector.Layout.Reset.Click += ResetProperty;
        Inspector.Layout.RevertDraft.Click += RevertPropertyDraft;
        Inspector.Layout.Delete.Click += () => Edit((document, node, token) => document.DeleteNode(document.Revision, node.Id, token));
        Inspector.Layout.Duplicate.Click += Duplicate;
        Inspector.Layout.Up.Click += () => Move(-1);
        Inspector.Layout.Down.Click += () => Move(1);
        Inspector.Layout.Insert.Click += Insert;
        Inspector.Layout.InsertBefore.Click += () => InsertSibling(after: false);
        Inspector.Layout.InsertAfter.Click += () => InsertSibling(after: true);
        Inspector.Layout.FindCell.Click += FindEmptyGridCell;
        Inspector.Layout.WrapVertical.Click += () => Wrap(ControlTemplate.VStack);
        Inspector.Layout.WrapHorizontal.Click += () => Wrap(ControlTemplate.HStack);
        Inspector.Layout.WrapScroll.Click += () => Wrap(ControlTemplate.ScrollView);
        Inspector.Layout.Unwrap.Click += Unwrap;
        parser = Task.Run(ParseSnapshots);
    }

    internal void SourceChanged()
    {
        if (disposed) return;
        string source = editor.Text;
        if (observedSource == source) return;
        observedSource = source;
        editCancellation?.Cancel();
        current = false;
        version++;
        Hierarchy.Tree.Enabled = false;
        Hierarchy.SetSearchCurrent(false);
        Hierarchy.Layout.FromCaret.Enabled = false;
        Hierarchy.Layout.Status.Text = Document is null ? "Reading source..." : "Stale hierarchy - read-only. Reading source...";
        Hierarchy.Layout.Status.Visible(true);
        Inspector.Show(Hierarchy.Selection, Hierarchy.Selection is { } selected ? Hierarchy.Parent(selected) : null, false);
        Inspector.Layout.Feedback.Text = "Visual edits wait for a matching source hierarchy.";
        if (!snapshots.Writer.TryWrite((version, source))) throw new InvalidOperationException("The hierarchy queue is closed.");
    }

    private async Task ParseSnapshots()
    {
        try
        {
            await foreach (var snapshot in snapshots.Reader.ReadAllAsync(lifetime.Token))
            {
                var document = VisualDocument.Parse(snapshot.Source, lifetime.Token);
                bool post;
                lock (publicationGate)
                {
                    pendingDocument = (snapshot.Version, document);
                    post = !publicationScheduled;
                    publicationScheduled = true;
                }
                if (post && !window.Post(PublishDocument)) return;
            }
        }
        catch (OperationCanceledException) when (lifetime.IsCancellationRequested) { }
        catch (Exception error)
        {
            Console.Error.WriteLine(error);
            window.Post(() =>
            {
                if (disposed) return;
                Hierarchy.Layout.Status.Text = "Hierarchy failed - read-only.";
                report($"Hierarchy worker failed: {error.Message}");
            });
        }
    }

    private void PublishDocument()
    {
        (long Version, VisualDocument Document)? snapshot;
        lock (publicationGate)
        {
            snapshot = pendingDocument;
            pendingDocument = null;
            publicationScheduled = false;
        }
        if (disposed || snapshot is not { } ready || ready.Version != version || ready.Document.Source != editor.Text) return;
        var document = ready.Document;
        if (!document.Success)
        {
            Hierarchy.Layout.Status.Text = "Invalid source - stale hierarchy is read-only.";
            Inspector.Layout.Feedback.Text = document.Diagnostics.FirstOrDefault()?.Message ?? "Source is not a valid component.";
            Changed?.Invoke();
            return;
        }
        Document = document;
        current = true;
        Hierarchy.Tree.Enabled = true;
        Hierarchy.SetDocument(document);
        Hierarchy.SetSearchCurrent(true);
        Hierarchy.Layout.FromCaret.Enabled = true;
        Hierarchy.Layout.Status.Visible(false);
        var selected = document.FindNode(checked((int)editor.Selection.Start)) ?? document.Root;
        if (selected is not null) SelectNode(selected, revealSource: false);
        else Inspector.Show(null, null, false);
        Inspector.Layout.Feedback.Text = busy ? "Validating visual edit..." : "Ready. Visual changes must compile before they are applied.";
        Changed?.Invoke();
    }

    internal void SelectFromCaret()
    {
        if (!current || Document is null)
        {
            Inspector.Layout.Feedback.Text = "Cannot select from a stale hierarchy. Correct the source first.";
            return;
        }
        var node = Document.FindNode(checked((int)editor.Selection.Start));
        if (node is null)
        {
            Inspector.Layout.Feedback.Text = "The caret is outside the component's visual controls.";
            return;
        }
        SelectNode(node, revealSource: false);
        Inspector.Layout.Feedback.Text = $"Selected {node.Kind} from the source caret.";
    }

    private void SelectNode(XuiSourceNode node, bool revealSource, bool selectHierarchy = true)
    {
        if (selectHierarchy) Hierarchy.Select(node);
        Inspector.Show(node, Hierarchy.Parent(node), current && !busy, validating: busy);
        if (revealSource) editor.Selection = new((ulong)node.Span.Start, (ulong)node.Span.End);
        SelectionChanged?.Invoke();
    }

    internal bool SelectFromPreview(string expectedSource, int nodeId)
    {
        if (!current || Document is not { } document || document.Source != expectedSource || editor.Text != expectedSource)
        {
            Inspector.Layout.Feedback.Text = "The preview does not match the current source hierarchy.";
            return false;
        }
        var node = Find(document.Root);
        if (node is null)
        {
            Inspector.Layout.Feedback.Text = "The preview control has no matching authored source node.";
            return false;
        }
        SelectNode(node, revealSource: true);
        editor.Focus();
        Inspector.Layout.Feedback.Text = $"Selected {node.Kind} from the preview.";
        return true;

        XuiSourceNode? Find(XuiSourceNode? candidate)
        {
            if (candidate is null || candidate.Id == nodeId) return candidate;
            foreach (var child in candidate.Children)
                if (Find(child) is { } match) return match;
            return null;
        }
    }

    internal void ApplyProperty()
    {
        string? name = Inspector.Argument;
        if (name is null) { Inspector.Layout.Feedback.Text = "Select an argument first."; return; }
        if (!Inspector.TryReadLiteral(out string value, out string? error))
        {
            Inspector.Layout.Feedback.Text = error!;
            return;
        }
        if (!busy && current && Document?.Source == editor.Text &&
            Hierarchy.Selection?.Arguments.FirstOrDefault(argument => argument.Name == name)?.Value == value)
        {
            Inspector.Layout.Feedback.Text = "The value is unchanged. No source edit was applied.";
            return;
        }
        Edit((document, node, token) =>
        {
            var result = document.SetArgument(document.Revision, node.Id, name, value, cancellation: token);
            if (result.Edit is not { } edit) return result;
            var candidate = VisualDocument.Parse(edit.Apply(document.Revision, document.Source), token);
            var changed = candidate.FindNode(edit.Selection.Start)?.Arguments.FirstOrDefault(a => a.Name == name);
            return changed?.ValueKind == XuiValueKind.Expression
                ? new(null, "Only literal values can be entered here. Add C# expressions in the source editor.")
                : result;
        });
    }

    internal void Move(int delta) => Edit((document, node, token) => document.MoveNode(document.Revision, node.Id, delta, token));

    internal void RevertPropertyDraft()
    {
        if (busy)
        {
            Inspector.Layout.Feedback.Text = "Wait for the current visual edit before reverting a property draft.";
            return;
        }
        if (!current || Document is null || Document.Source != editor.Text || Hierarchy.Selection is null)
        {
            Inspector.Layout.Feedback.Text = "Cannot revert a draft from a stale hierarchy. Select a property in the current source.";
            return;
        }
        Inspector.RevertDraft();
    }

    internal void ResetProperty()
    {
        string? name = Inspector.Argument;
        var argument = Hierarchy.Selection?.Arguments.FirstOrDefault(value => value.Name == name);
        if (argument is null) { Inspector.Layout.Feedback.Text = "This argument is not set in source."; return; }
        if (argument.IsPositional)
        {
            Inspector.Layout.Feedback.Text = "Positional arguments cannot be reset. Edit the value instead.";
            return;
        }
        if (argument.ValueKind == XuiValueKind.Expression)
        {
            Inspector.Layout.Feedback.Text = "Expressions are read-only here. Remove this argument in the source editor.";
            return;
        }
        Edit((document, node, token) => document.RemoveArgument(document.Revision, node.Id, argument.Name, token));
    }
    internal void Wrap(ControlTemplate wrapper) =>
        Edit((document, node, token) => document.WrapNode(document.Revision, node.Id, wrapper, token));
    internal void Unwrap() => Edit((document, node, token) => document.UnwrapNode(document.Revision, node.Id, token));

    private void Insert()
    {
        if (Inspector.Template is not { } template)
        {
            Inspector.Layout.Feedback.Text = "Choose a control from the palette before insertion.";
            return;
        }
        GridPlacement? placement = null;
        if (Hierarchy.Selection?.Kind == "Grid")
        {
            placement = ReadGridPlacement();
            if (placement is null) return;
        }
        Edit((document, node, token) => document.InsertControl(document.Revision, node.Id, node.Children.Count, template, placement, token));
    }

    internal void InsertSibling(bool after)
    {
        if (Inspector.Template is not { } template)
        {
            Inspector.Layout.Feedback.Text = "Choose a control from the palette before insertion.";
            return;
        }
        GridPlacement? placement = null;
        if (Hierarchy.Selection is { } selected && Hierarchy.Parent(selected)?.Kind == "Grid")
        {
            placement = ReadGridPlacement();
            if (placement is null) return;
        }
        Edit((document, node, token) => document.InsertSibling(document.Revision, node.Id, after, template, placement, token));
    }

    internal void FindEmptyGridCell()
    {
        if (busy) { Inspector.Layout.Feedback.Text = "Wait for the current visual edit before choosing a Grid cell."; return; }
        if (!current || Document is not { } document || Hierarchy.Selection is not { } selected || document.Source != editor.Text)
        {
            Inspector.Layout.Feedback.Text = "Cannot choose a cell from a stale hierarchy. Select a control in the current source.";
            return;
        }
        var grid = selected.Kind == "Grid" ? selected : Hierarchy.Parent(selected);
        if (grid?.Kind != "Grid")
        {
            Inspector.Layout.Feedback.Text = "Select a Grid or one of its direct children.";
            return;
        }
        if (!document.TryFindEmptyGridCell(document.Revision, grid.Id, out var placement, out var error, lifetime.Token))
        {
            Inspector.Layout.Feedback.Text = error!;
            return;
        }
        Inspector.Layout.Row.Text = placement.Row.ToString(CultureInfo.InvariantCulture);
        Inspector.Layout.Column.Text = placement.Column.ToString(CultureInfo.InvariantCulture);
        string target = selected.Kind == "Grid" ? "selected" : "parent";
        Inspector.Layout.Feedback.Text = $"Empty cell in the {target} Grid: row {placement.Row}, column {placement.Column}. Insert or duplicate to apply.";
    }

    private GridPlacement? ReadGridPlacement()
    {
        if (int.TryParse(Inspector.Layout.Row.Text, NumberStyles.None, CultureInfo.InvariantCulture, out int row) &&
            int.TryParse(Inspector.Layout.Column.Text, NumberStyles.None, CultureInfo.InvariantCulture, out int column))
            return new(row, column);
        Inspector.Layout.Feedback.Text = "Enter non-negative whole numbers for the grid row and column.";
        return null;
    }

    private void Duplicate()
    {
        GridPlacement? placement = null;
        if (Hierarchy.Selection is { } selected && Hierarchy.Parent(selected)?.Kind == "Grid")
        {
            placement = ReadGridPlacement();
            if (placement is null) return;
        }
        Edit((document, node, token) => document.DuplicateNode(document.Revision, node.Id, placement, token));
    }

    internal bool HandleHierarchyKey(UiKeyEvent key)
    {
        if (!Hierarchy.Tree.Focused) return false;
        if (key.Modifiers == KeyModifiers.Control && key.VirtualKey == 'D') { Duplicate(); return true; }
        if (key.Modifiers == KeyModifiers.Control && key.VirtualKey == 'G') { Wrap(ControlTemplate.VStack); return true; }
        if (key.Modifiers == (KeyModifiers.Control | KeyModifiers.Shift) && key.VirtualKey == 'G') { Unwrap(); return true; }
        if (key.Modifiers == KeyModifiers.None && key.VirtualKey == 0x2E)
        { Edit((document, node, token) => document.DeleteNode(document.Revision, node.Id, token)); return true; }
        if (key.Modifiers == KeyModifiers.Alt && key.VirtualKey is 0x26 or 0x28)
        { Move(key.VirtualKey == 0x26 ? -1 : 1); return true; }
        return false;
    }

    private void Edit(Func<VisualDocument, XuiSourceNode, CancellationToken, VisualEditResult> operation)
    {
        if (busy) { Inspector.Layout.Feedback.Text = "A visual edit is already being validated."; return; }
        if (!current || Document is not { } document || Hierarchy.Selection is not { } node || document.Source != editor.Text)
        {
            Inspector.Layout.Feedback.Text = "Cannot edit a stale hierarchy. Select a control in the current source.";
            return;
        }
        busy = true;
        var cancellation = CancellationTokenSource.CreateLinkedTokenSource(lifetime.Token);
        editCancellation = cancellation;
        Inspector.Show(node, Hierarchy.Parent(node), false, validating: true);
        Inspector.Layout.Feedback.Text = "Validating visual edit... Source editing remains available.";
        editing = Task.Run(() =>
        {
            VisualEditResult? result = null;
            Exception? failure = null;
            try { result = operation(document, node, cancellation.Token); }
            catch (OperationCanceledException) when (cancellation.IsCancellationRequested) { }
            catch (Exception error) { failure = error; }
            window.Post(() =>
            {
                if (disposed) return;
                try
                {
                    if (cancellation.IsCancellationRequested || !current || Document?.Revision != document.Revision || editor.Text != document.Source)
                    {
                        Inspector.Layout.Feedback.Text = "Visual edit cancelled because the source changed. No edit was applied.";
                        return;
                    }
                    if (failure is not null)
                    {
                        report($"Visual edit failed: {failure.Message}");
                        Inspector.Layout.Feedback.Text = $"Visual edit failed: {failure.Message}";
                        return;
                    }
                    if (result?.Edit is not { } edit)
                    {
                        Inspector.Layout.Feedback.Text = result?.Error ?? "Visual edit did not produce a source change.";
                        return;
                    }
                    editor.ReplaceRange(new((ulong)edit.Range.Start, (ulong)edit.Range.End), edit.ExpectedSource, edit.Replacement);
                    editor.Selection = new((ulong)edit.Selection.Start, (ulong)edit.Selection.End);
                    editor.Focus();
                    Inspector.Layout.Feedback.Text = "Visual edit applied. Use Undo to restore the previous source.";
                }
                catch (XuiException error)
                {
                    Inspector.Layout.Feedback.Text = $"Native editor rejected the visual edit: {error.Message}";
                    report(Inspector.Layout.Feedback.Text);
                }
                finally
                {
                    busy = false;
                    editCancellation = null;
                    cancellation.Dispose();
                    Inspector.Show(Hierarchy.Selection, Hierarchy.Selection is { } selected ? Hierarchy.Parent(selected) : null, current);
                    Changed?.Invoke();
                }
            });
        });
    }

    public void Dispose()
    {
        if (disposed) return;
        disposed = true;
        lifetime.Cancel();
        snapshots.Writer.TryComplete();
        parser.GetAwaiter().GetResult();
        editing?.GetAwaiter().GetResult();
        lock (publicationGate) pendingDocument = null;
        editCancellation?.Dispose();
        lifetime.Dispose();
    }
}
