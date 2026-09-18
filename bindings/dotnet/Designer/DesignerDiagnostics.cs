using System.Collections.ObjectModel;
using System.Globalization;
using System.Text.RegularExpressions;
using Microsoft.CodeAnalysis.Text;

namespace Xui.Designer;

internal sealed record DesignerDiagnostic(
    TextSpan DisplaySpan, string Text, string? Severity, string? Code,
    int? Line, int? Column, TextSpan? SourceSpan);

internal sealed partial class DesignerDiagnostics
{
    internal long Revision { get; }
    internal string Source { get; }
    internal string Text { get; }
    internal ReadOnlyCollection<DesignerDiagnostic> Entries { get; }

    private DesignerDiagnostics(long revision, string source, string text, List<DesignerDiagnostic> entries)
    {
        Revision = revision;
        Source = source;
        Text = text;
        Entries = entries.AsReadOnly();
    }

    internal static DesignerDiagnostics Parse(long revision, string source, string diagnostics)
    {
        ArgumentNullException.ThrowIfNull(source);
        ArgumentNullException.ThrowIfNull(diagnostics);
        var sourceText = SourceText.From(source);
        var entries = new List<DesignerDiagnostic>();
        foreach (var line in SourceText.From(diagnostics).Lines)
        {
            string text = line.ToString();
            if (string.IsNullOrWhiteSpace(text)) continue;
            var match = DiagnosticLine().Match(text);
            string? severity = null, code = null;
            int? row = null, column = null;
            TextSpan? span = null;
            if (match.Success)
            {
                severity = match.Groups["severity"].Value;
                code = match.Groups["code"].Value;
                if (PositiveNumber(match.Groups["line"].Value) is { } parsedRow &&
                    PositiveNumber(match.Groups["column"].Value) is { } parsedColumn)
                {
                    row = parsedRow;
                    column = parsedColumn;
                    if (match.Groups["path"].Value == "Preview.xui")
                        span = Locate(sourceText, parsedRow, parsedColumn);
                }
            }
            entries.Add(new(line.Span, text, severity, code, row, column, span));
        }
        return new(revision, source, diagnostics, entries);
    }

    internal int? FindAtOffset(int offset)
    {
        if (offset < 0 || offset > Text.Length) return null;
        for (int index = 0; index < Entries.Count; index++)
        {
            var span = Entries[index].DisplaySpan;
            if (offset >= span.Start && offset <= span.End) return index;
        }
        return null;
    }

    internal int? NextIndex(int currentIndex = -1, bool reverse = false)
    {
        if (currentIndex < -1 || currentIndex >= Entries.Count)
            throw new ArgumentOutOfRangeException(nameof(currentIndex));
        for (int step = 1; step <= Entries.Count; step++)
        {
            int origin = currentIndex < 0 ? (reverse ? 0 : -1) : currentIndex;
            int index = (origin + (reverse ? -step : step) + Entries.Count) % Entries.Count;
            if (Entries[index].SourceSpan is not null) return index;
        }
        return null;
    }

    internal TextSpan GetSourceSelection(int index, long revision, string source)
    {
        if (revision != Revision || !string.Equals(source, Source, StringComparison.Ordinal))
            throw new InvalidOperationException("The source changed. Render the current source before navigating diagnostics.");
        if ((uint)index >= (uint)Entries.Count) throw new ArgumentOutOfRangeException(nameof(index));
        return Entries[index].SourceSpan ??
            throw new InvalidOperationException("This diagnostic has no location in the current .xui source.");
    }

    private static int? PositiveNumber(string value) =>
        int.TryParse(value, NumberStyles.None, CultureInfo.InvariantCulture, out int result) && result > 0 ? result : null;

    internal static TextSpan? Locate(SourceText source, int row, int column)
    {
        if (row <= 0 || column <= 0 || row > source.Lines.Count) return null;
        var line = source.Lines[row - 1];
        if (column > line.Span.Length + 1) return null;
        int offset = line.Start + column - 1;
        int length = offset == line.End ? 0 : 1;
        if (length > 0 && char.IsLowSurrogate(source[offset]) &&
            offset > line.Start && char.IsHighSurrogate(source[offset - 1])) return null;
        if (length > 0 && char.IsHighSurrogate(source[offset]) &&
            offset + 1 < line.End && char.IsLowSurrogate(source[offset + 1])) length = 2;
        return new(offset, length);
    }

    [GeneratedRegex(@"^(?:(?<path>.+)\((?<line>[0-9]+),(?<column>[0-9]+)\): )?(?<severity>error|warning|info) (?<code>[A-Za-z][A-Za-z0-9]*): (?<message>.*)$",
        RegexOptions.CultureInvariant | RegexOptions.NonBacktracking)]
    private static partial Regex DiagnosticLine();
}
