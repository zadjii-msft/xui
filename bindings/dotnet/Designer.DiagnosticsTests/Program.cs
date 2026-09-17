using System.Runtime.InteropServices;
using Microsoft.CodeAnalysis.Text;
using Xui.Designer;

int assertions = 0;
void Require(bool condition, string message)
{
    if (!condition) throw new InvalidOperationException(message);
    assertions++;
}
void Reject<T>(Action action, string message) where T : Exception
{
    try { action(); }
    catch (T) { assertions++; return; }
    throw new InvalidOperationException(message);
}

try
{
    NativeLibrary.SetDllImportResolver(typeof(Xui.Window).Assembly, (_, _, _) =>
        throw new InvalidOperationException("Diagnostic tests must not load native XUI."));
    foreach (string newline in new[] { "\r", "\r\n", "\n", "\u0085", "\u2028", "\u2029" })
    {
        string source = "first" + newline + "A\U0001F680B" + newline;
        string text = "Preview.xui(2,2): error CS0103: Unknown value" + newline +
            "error XUITOOL001: No source location" + newline +
            "Preview.xui(1,6): warning CS0168: End of line" + newline +
            "Generated.g.cs(1,1): error CS1002: Generated source" + newline +
            "Preview.xui(3,1): info XUIINFO001: End of source";
        var diagnostics = DesignerDiagnostics.Parse(42, source, text);
        Require(diagnostics.Entries.Count == 5, "Every diagnostic line is retained.");
        Require(diagnostics.Entries[0].Code == "CS0103" && diagnostics.Entries[0].Severity == "error", "Severity and diagnostic code are available.");
        Require(diagnostics.GetSourceSelection(0, 42, source) == new TextSpan(5 + newline.Length + 1, 2), "Offsets retain exact line endings and select complete surrogate pairs.");
        Require(diagnostics.GetSourceSelection(2, 42, source) == new TextSpan(5, 0), "A diagnostic at the end of a line produces a caret.");
        Require(diagnostics.GetSourceSelection(4, 42, source) == new TextSpan(source.Length, 0), "An end-of-source diagnostic produces a caret.");
        Require(diagnostics.Entries[3].Line == 1 && diagnostics.Entries[3].SourceSpan is null, "Generated file locations do not point into authored source.");
        Require(diagnostics.NextIndex() == 0 && diagnostics.NextIndex(0) == 2 && diagnostics.NextIndex(2) == 4 &&
            diagnostics.NextIndex(4) == 0, "Next wraps and skips diagnostics without authored locations.");
        Require(diagnostics.NextIndex(reverse: true) == 4 && diagnostics.NextIndex(0, reverse: true) == 4 &&
            diagnostics.NextIndex(4, reverse: true) == 2, "Previous wraps and skips diagnostics without authored locations.");
        for (int index = 0; index < diagnostics.Entries.Count; index++)
        {
            var entry = diagnostics.Entries[index];
            Require(diagnostics.FindAtOffset(entry.DisplaySpan.Start) == index, "The diagnostics caret resolves to its current entry.");
            Require(text.Substring(entry.DisplaySpan.Start, entry.DisplaySpan.Length) == entry.Text, "Display spans refer to exact native diagnostics text.");
        }
        Reject<InvalidOperationException>(() => diagnostics.GetSourceSelection(0, 43, source), "A newer revision must reject old diagnostics even with identical text.");
        Reject<InvalidOperationException>(() => diagnostics.GetSourceSelection(0, 42, source + "changed"), "Changed source must reject old diagnostics.");
        Reject<InvalidOperationException>(() => diagnostics.GetSourceSelection(1, 42, source), "An unmapped diagnostic must not fabricate a source selection.");
        Reject<ArgumentOutOfRangeException>(() => diagnostics.GetSourceSelection(-1, 42, source), "Invalid diagnostic indexes must fail.");
        Require(diagnostics.FindAtOffset(-1) is null && diagnostics.FindAtOffset(text.Length + 1) is null, "Invalid display offsets do not identify diagnostics.");
    }

    foreach (var location in new[] { "(0,1)", "(1,0)", "(9,1)", "(1,99)", "(1,3)", "(99999999999999,1)", "(1,99999999999999)" })
    {
        var diagnostics = DesignerDiagnostics.Parse(1, "A\U0001F680B", $"Preview.xui{location}: error CS0001: Invalid position");
        Require(diagnostics.Entries.Count == 1 && diagnostics.Entries[0].SourceSpan is null, "Invalid coordinates and split-surrogate positions are not navigable.");
        Require(diagnostics.NextIndex() is null && diagnostics.NextIndex(reverse: true) is null, "Unmapped diagnostics never cause a navigation loop.");
    }

    var empty = DesignerDiagnostics.Parse(1, "", "");
    Require(empty.Entries.Count == 0 && empty.NextIndex() is null && empty.FindAtOffset(0) is null, "An empty set has no navigation target.");
    var prose = DesignerDiagnostics.Parse(1, "source", "\r\nPreview construction failed:\r\n  authored exception\r\n");
    Require(prose.Entries.Count == 2 && prose.Entries.All(entry => entry.SourceSpan is null), "Runtime errors remain visible without invented source coordinates.");
    Reject<ArgumentOutOfRangeException>(() => prose.NextIndex(2), "Navigation rejects an invalid current index.");
    Reject<ArgumentOutOfRangeException>(() => prose.NextIndex(-2), "Navigation rejects invalid negative indexes.");

    foreach (string newline in new[] { "\r", "\r\n", "\n" })
    {
        string source = string.Join(newline, "component Broken {", "    view {", "        Text(MissingValue);", "    }", "}");
        var compilation = PreviewCompiler.Compile(source);
        Require(!compilation.Success, "The compiler rejects an unknown authored expression.");
        var diagnostics = DesignerDiagnostics.Parse(7, source, compilation.Diagnostics);
        int? index = diagnostics.NextIndex();
        Require(index is not null, "Actual compiler diagnostics expose an authored location.");
        var selection = diagnostics.GetSourceSelection(index!.Value, 7, source);
        Require(selection.Start >= 0 && selection.End <= source.Length, "An actual compiler location selects within the exact authored source.");
        Require(diagnostics.Entries[index.Value].Line == 3 &&
            selection.Start == SourceText.From(source).Lines[2].Start + diagnostics.Entries[index.Value].Column - 1,
            $"Navigation honors the compiler's authored line and column. Actual: {compilation.Diagnostics}; selection={selection}.");
    }
    Console.WriteLine($"Designer diagnostics assertions: {assertions} passed.");
}
catch (Exception error)
{
    Console.Error.WriteLine(error);
    Environment.ExitCode = 1;
}
