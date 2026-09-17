using System.Globalization;
using Microsoft.CodeAnalysis.Text;

namespace Xui.Designer;

internal sealed class DesignerGoTo : IDisposable
{
    private readonly Window window;
    private readonly MultilineText editor;
    private readonly Func<long> version;
    private readonly Action navigated;
    private readonly Action<string> report;
    private string source = "";
    private long generation;
    private long openingVersion;
    private bool disposed;

    internal DesignerGoToLayout Layout { get; }
    internal ContentDialog View { get; }
    internal bool IsPending { get; private set; }
    internal string? ValidationError { get; private set; }

    internal DesignerGoTo(Window window, MultilineText editor, Func<long> version, Action navigated, Action<string> report)
    {
        this.window = window;
        this.editor = editor;
        this.version = version;
        this.navigated = navigated;
        this.report = report;
        Layout = new DesignerGoToLayout(window, attach: false);
        View = window.ContentDialog("Go to source location", Layout.Root);
        View.Primary.Text = "Go to";
        View.Primary.SetAutomationId("designer-go-to-confirm");
        View.CancelButton.SetAutomationId("designer-go-to-cancel");
        View.OnResult(Complete);
        Layout.Line.Changed += _ => Refresh();
        Layout.Column.Changed += _ => Refresh();
        editor.Event += value =>
        {
            if (disposed || value.Kind != EventKind.Change) return;
            Refresh();
        };
    }

    internal void Show(Control anchor)
    {
        if (disposed) return;
        long request = ++generation;
        Post(() =>
        {
            if (disposed || request != generation) return;
            if (IsPending) { Layout.Line.Focus(selectAll: true); return; }
            try
            {
                source = editor.Text;
                openingVersion = version();
                var text = SourceText.From(source);
                int offset = checked((int)editor.Selection.Start);
                var line = text.Lines.GetLineFromPosition(offset);
                Layout.Line.Text = (line.LineNumber + 1).ToString(CultureInfo.InvariantCulture);
                Layout.Column.Text = (offset - line.Start + 1).ToString(CultureInfo.InvariantCulture);
                Layout.Summary.Text = text.Lines.Count == 1 ? "The source has 1 line." : $"The source has {text.Lines.Count} lines.";
                IsPending = true;
                Refresh();
                View.Show(anchor);
                Layout.Line.Focus(selectAll: true);
            }
            catch (XuiException error)
            {
                IsPending = false;
                report($"Could not open Go to source location: {error.Message}");
            }
        });
    }

    internal void Refresh()
    {
        if (disposed || !IsPending) return;
        ReadTarget(out _, out var error);
        ValidationError = error;
        View.Primary.Enabled = error is null;
        View.SetValidationMessage(error ?? "");
    }

    private bool ReadTarget(out TextSpan location, out string? error)
    {
        location = default;
        if (openingVersion != version() || source != editor.Text)
        {
            error = "The source changed. Close this dialog and reopen Go to source location.";
            return false;
        }
        return TryLocate(source, Layout.Line.Text, Layout.Column.Text, out location, out error);
    }

    internal static bool TryLocate(string source, string rowText, string columnText, out TextSpan location, out string? error)
    {
        location = default;
        if (!int.TryParse(rowText, NumberStyles.None, CultureInfo.InvariantCulture, out int row) || row <= 0)
        {
            error = "Enter a positive whole line number.";
            return false;
        }
        if (!int.TryParse(columnText, NumberStyles.None, CultureInfo.InvariantCulture, out int column) || column <= 0)
        {
            error = "Enter a positive whole column number.";
            return false;
        }
        var text = SourceText.From(source);
        if (row > text.Lines.Count)
        {
            error = $"The source has {text.Lines.Count} lines. Choose a line within that range.";
            return false;
        }
        var line = text.Lines[row - 1];
        if (column > line.Span.Length + 1)
        {
            error = $"Line {row} accepts columns 1 through {line.Span.Length + 1}.";
            return false;
        }
        if (DesignerDiagnostics.Locate(text, row, column) is not { } target)
        {
            error = "The column splits a Unicode character. Choose the preceding or following column.";
            return false;
        }
        location = new(target.Start, 0);
        error = null;
        return true;
    }

    private void Complete(bool accepted)
    {
        if (disposed || !IsPending) return;
        IsPending = false;
        long request = ++generation;
        if (!accepted) return;
        string expected = source;
        long expectedVersion = openingVersion;
        bool valid = ReadTarget(out var target, out var error);
        Post(() =>
        {
            if (disposed || request != generation) return;
            if (!valid) { report(error!); return; }
            if (version() != expectedVersion || editor.Text != expected)
            {
                report("The source changed before navigation. No selection was moved.");
                return;
            }
            try
            {
                editor.Selection = new((ulong)target.Start, (ulong)target.Start);
                navigated();
                editor.Focus();
            }
            catch (XuiException failure) { report($"Could not select the source location: {failure.Message}"); }
        });
    }

    private void Post(Action action)
    {
        if (!window.Post(action)) throw new InvalidOperationException("The window rejected the source navigation request.");
    }

    public void Dispose()
    {
        if (disposed) return;
        disposed = true;
        generation++;
        IsPending = false;
    }
}
