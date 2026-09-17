namespace Xui.Designer;

internal sealed class DesignerDiagnosticNavigator
{
    private readonly MultilineText editor;
    private readonly MultilineText diagnostics;
    private readonly Func<long> currentRevision;
    private readonly Action<string> report;
    private readonly Action navigated;
    private DesignerDiagnostics? snapshot;
    private int selectedIndex = -1;

    internal DesignerDiagnosticsLayout Layout { get; }
    internal Element View => Layout.Root;
    internal bool IsCurrent => snapshot is { } value && value.Revision == currentRevision() &&
        value.Source == editor.Text && value.Text == diagnostics.Text;

    internal DesignerDiagnosticNavigator(Window window, MultilineText editor, MultilineText diagnostics,
        Func<long> currentRevision, Action<string> report, Action navigated)
    {
        this.editor = editor;
        this.diagnostics = diagnostics;
        this.currentRevision = currentRevision;
        this.report = report;
        this.navigated = navigated;
        Layout = new DesignerDiagnosticsLayout(window, diagnostics, attach: false);
        Layout.Previous.Click += () => Move(reverse: true);
        Layout.Next.Click += () => Move();
        Layout.Selected.Click += GoToSelected;
        editor.Event += value => { if (value.Kind == EventKind.Change) RefreshAvailability(); };
        diagnostics.Event += value =>
        {
            if (value.Kind is EventKind.Change or EventKind.Selection) RefreshAvailability();
        };
    }

    internal void Publish(long revision, string source)
    {
        if (revision != currentRevision() || source != editor.Text)
            throw new InvalidOperationException("Cannot publish diagnostics for a stale source snapshot.");
        snapshot = DesignerDiagnostics.Parse(revision, source, diagnostics.Text);
        selectedIndex = -1;
        RefreshAvailability();
    }

    internal void Invalidate()
    {
        snapshot = null;
        selectedIndex = -1;
        RefreshAvailability();
    }

    internal bool HandleKey(UiKeyEvent key)
    {
        if (key.VirtualKey != 0x77 || key.Modifiers is not (KeyModifiers.None or KeyModifiers.Shift)) return false;
        Move(key.Modifiers == KeyModifiers.Shift);
        return true;
    }

    internal void Move(bool reverse = false)
    {
        if (!RequireCurrent()) return;
        Navigate(snapshot!.NextIndex(selectedIndex, reverse));
    }

    internal void GoToSelected()
    {
        if (!RequireCurrent()) return;
        Navigate(snapshot!.FindAtOffset(checked((int)diagnostics.Selection.Start)));
    }

    private bool RequireCurrent()
    {
        if (IsCurrent) return true;
        Invalidate();
        report("Render the current source before navigating diagnostics.");
        return false;
    }

    private void Navigate(int? index)
    {
        if (index is not { } target || snapshot!.Entries[target].SourceSpan is null)
        {
            report("This diagnostic has no location in the current .xui source.");
            return;
        }
        var source = snapshot.GetSourceSelection(target, currentRevision(), editor.Text);
        var message = snapshot.Entries[target].DisplaySpan;
        selectedIndex = target;
        diagnostics.Selection = new((ulong)message.Start, (ulong)message.End);
        editor.Selection = new((ulong)source.Start, (ulong)source.End);
        navigated();
        editor.Focus();
        RefreshAvailability();
    }

    private void RefreshAvailability()
    {
        bool current = IsCurrent;
        int count = current ? snapshot!.Entries.Count(entry => entry.SourceSpan is not null) : 0;
        int? selected = current ? snapshot!.FindAtOffset(checked((int)diagnostics.Selection.Start)) : null;
        Layout.Previous.Enabled = count > 0;
        Layout.Next.Enabled = count > 0;
        Layout.Selected.Enabled = selected is { } index && snapshot!.Entries[index].SourceSpan is not null;
        Layout.Summary.Text = current ? $"{snapshot!.Entries.Count} diagnostics, {count} source locations" : "No current diagnostics";
    }
}
