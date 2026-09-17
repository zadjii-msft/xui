using System.Runtime.InteropServices;
using Xui;

internal static class DocumentEditingTests
{
    [DllImport("user32.dll")]
    private static extern uint GetClipboardSequenceNumber();
    private static void Expect(bool value, string message)
    {
        if (!value) throw new InvalidOperationException(message);
    }
    private static void Fails(Action action, int? status = null)
    {
        try { action(); }
        catch (XuiException e) when (status is null || e.Status == status) { return; }
        throw new InvalidOperationException("Expected document edit rejection.");
    }
    private sealed class Rows : IReadOnlyImmutableSource
    {
        public ulong Count => 2;
        public ItemKey Key(ulong index) => new(index + 1, 7);
        public ulong? Find(ItemKey key) => key.Version == 7 && key.Id is >= 1 and <= 2 ? key.Id - 1 : null;
        public ItemContent Item(ulong index, ulong column = 0) => new($"Row {index}");
    }
    internal static void Run()
    {
        using var window = new Window("Document editing binding tests");
        var document = window.MultilineText("Source").SetDocument("A\U0001f600 old\rZ");
        string original = document.Text;
        Fails(() => document.ReplaceRange(new(4, 7), original, "new"), 7);
        using var source = window.ImmutableSource(new Rows());
        var tree = window.TreeView("Hierarchy").SetSource(source);
        int selections = 0;
        tree.Event += e => { if (e.Kind == EventKind.Selection) ++selections; };
        Expect(ReferenceEquals(tree.Select(new(2, 7)), tree), "Fluent tree selection");
        Expect(tree.Selection.Focused == new ItemKey(2, 7) && tree.Contains(new(2, 7)) && selections == 1,
            "Tree uses native collection selection and emits one event");
        window.SetContent(window.Stack().Add(document).Add(tree));
        int changes = 0;
        document.Event += e =>
        {
            if (e.Kind != EventKind.Change) return;
            ++changes;
            if (changes == 1)
            {
                Expect(document.Text == "A\U0001f600 N\rB\rZ" && document.Selection == new TextSelection(7, 7),
                    "Callback observes resulting text and native selection");
                Fails(() => document.ReplaceRange(new(0, 1), document.Text, "Q"), 7);
            }
        };
        Task.Run(() => Fails(() => document.ReplaceRange(new(4, 7), original, "new"), 4)).GetAwaiter().GetResult();
        bool ran = false;
        Expect(window.Post(() =>
        {
            try
            {
                uint clipboard = GetClipboardSequenceNumber();
                TextSelection result = document.ReplaceRange(new(4, 7), original, "N\r\nB");
                string edited = "A\U0001f600 N\rB\rZ";
                Expect(result == new TextSelection(7, 7) && document.Selection == result && document.Text == edited && changes == 1,
                    "Managed replacement updates native document once");
                Expect(GetClipboardSequenceNumber() == clipboard, "Managed edit preserves clipboard");
                Fails(() => document.ReplaceRange(new(4, 7), original, "stale"), 7);
                Fails(() => document.ReplaceRange(new(2, 3), edited, "split"), 1);
                Fails(() => document.ReplaceRange(new(4, 7), edited, "N\nB"), 1);
                Fails(() => document.ReplaceRange(new(0, ulong.MaxValue), edited, "bad"), 1);
                Expect(document.Text == edited && document.Selection == result && changes == 1, "Rejections preserve text and selection");
                document.ReadOnly = true;
                Fails(() => document.ReplaceRange(new(0, 1), edited, "Q"), 7);
                document.ReadOnly = false;
                document.Command(TextCommand.Undo);
                Expect(document.Text == original && changes == 2, "Managed undo restores exact snapshot");
                document.Command(TextCommand.Redo);
                Expect(document.Text == edited && changes == 3, "Managed redo restores exact edited snapshot");
                document.MaximumLength = (ulong)edited.Length;
                Fails(() => document.ReplaceRange(new(0, 1), edited, "Too long"), 1);
                document.ReplaceRange(new(0, 1), edited, "B");
                Expect(document.Text == "B\U0001f600 N\rB\rZ" && changes == 4, "Consecutive visual edit");
                document.Command(TextCommand.Undo);
                Expect(document.Text == edited && changes == 5, "Consecutive edit is one undo action");
                ran = true;
            }
            finally { window.Close(); }
        }), "Queue native fixture");
        using var finished = new ManualResetEventSlim();
        var watchdog = Task.Run(() => { if (!finished.Wait(TimeSpan.FromSeconds(30))) window.Post(window.Close); });
        try { window.Run(); }
        finally { finished.Set(); watchdog.GetAwaiter().GetResult(); }
        Expect(ran, "Native fixture completed");
        Fails(() => document.ReplaceRange(new(0, 1), original, "Q"));
        Console.WriteLine("C# document replacement and tree selection tests passed");
    }
}
