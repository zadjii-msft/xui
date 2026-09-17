using Xui;
using Xui.Designer;

internal static class Program
{
    private static int assertions;
    private static void Require(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
        assertions++;
    }

    [STAThread]
    private static int Main()
    {
        try
        {
            Run();
            Console.WriteLine($"Designer diagnostic navigation UI assertions: {assertions} passed.");
            return 0;
        }
        catch (Exception error) { Console.Error.WriteLine(error); return 1; }
    }

    private static void Run()
    {
        using var window = new Window("Designer diagnostic navigation UI smoke", 900, 600);
        window.SetShowActivated(false);
        var editor = window.MultilineText("Source").SetDocument("Alpha\rA\U0001F680B\r");
        var diagnostics = window.MultilineText("Diagnostics").SetReadOnly(true);
        long revision = 1;
        int navigations = 0;
        var errors = new List<string>();
        var navigator = new DesignerDiagnosticNavigator(window, editor, diagnostics,
            () => revision, errors.Add, () => navigations++);
        window.SetContent(window.Stack().Padding(12).Spacing(8).Add(editor, 1).Add(navigator.View, 1));
        Exception? failure = null;
        bool completed = false;
        if (!window.Post(() =>
        {
            try
            {
                string source = editor.Text;
                diagnostics.Text = "Preview.xui(1,2): error CS0001: First\r\n" +
                    "Generated.g.cs(1,1): error CS0002: Generated\r\n" +
                    "Preview.xui(2,2): warning CS0003: Unicode";
                navigator.Publish(revision, source);
                Require(navigator.IsCurrent, "Published diagnostics match exact native source and diagnostics text.");
                navigator.Layout.Next.Invoke();
                Require(editor.Selection == new TextSelection(1, 2), "The Next button selects the exact source scalar.");
                Require(editor.Focused && navigations == 1, "Navigation focuses source and notifies source-selection synchronization.");
                Require(diagnostics.Selection.Start == 0 && diagnostics.Selection.End > 0, "Navigation selects the corresponding diagnostic message.");
                navigator.Layout.Next.Invoke();
                Require(editor.Selection == new TextSelection(7, 9), "Next skips generated locations and preserves a Unicode pair.");
                navigator.Layout.Previous.Invoke();
                Require(editor.Selection == new TextSelection(1, 2), "Previous returns to the preceding authored location.");
                Require(navigator.HandleKey(new(0x77, KeyModifiers.Shift, 0)) && editor.Selection == new TextSelection(7, 9),
                    "Shift+F8 wraps to the previous source diagnostic.");
                Require(navigator.HandleKey(new(0x77, KeyModifiers.None, 0)) && editor.Selection == new TextSelection(1, 2),
                    "F8 advances through source diagnostics.");
                Require(!navigator.HandleKey(new(0x77, KeyModifiers.Control, 0)), "Unregistered modifier combinations remain available to the application.");
                string nativeDiagnostics = diagnostics.Text;
                int unicodeLine = nativeDiagnostics.IndexOf("Preview.xui(2", StringComparison.Ordinal);
                diagnostics.Selection = new((ulong)unicodeLine, (ulong)unicodeLine);
                navigator.Layout.Selected.Invoke();
                Require(editor.Selection == new TextSelection(7, 9), "The diagnostics caret selects its own authored location.");
                int generatedLine = nativeDiagnostics.IndexOf("Generated", StringComparison.Ordinal);
                diagnostics.Selection = new((ulong)generatedLine, (ulong)generatedLine);
                navigator.GoToSelected();
                Require(errors.Count == 1 && editor.Selection == new TextSelection(7, 9), "An unmapped diagnostic reports an error without moving source selection.");
                revision++;
                navigator.Move();
                Require(!navigator.IsCurrent && errors.Count == 2, "A newer source revision invalidates navigation.");
                Require(editor.Text == source, "Diagnostics navigation never edits source text.");
                diagnostics.Text = "Preview.xui(1,2): error CS0001: Current";
                navigator.Publish(revision, source);
                diagnostics.Text = "A runtime error without a source location";
                navigator.Move();
                Require(!navigator.IsCurrent && errors.Count == 3, "Replaced diagnostic text cannot reuse previous message offsets.");
                diagnostics.Text = "";
                navigator.Publish(revision, source);
                navigator.Move();
                Require(errors.Count == 4, "An empty set reports that no source location is available.");
                navigator.Invalidate();
                bool rejected = false;
                try { navigator.Publish(revision - 1, source); }
                catch (InvalidOperationException) { rejected = true; }
                Require(rejected, "Publication rejects an old source revision.");
                completed = true;
            }
            catch (Exception error) { failure = error; }
            finally { window.Close(); }
        })) throw new InvalidOperationException("The navigation test window rejected its action.");
        window.Run();
        if (failure is not null) throw new InvalidOperationException("Diagnostic navigation UI smoke failed.", failure);
        Require(completed, "The navigation fixture completed every native action.");
    }
}
