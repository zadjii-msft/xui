using System.Runtime.InteropServices;
using Xui;

internal static class ExplorerTextTests
{
    [DllImport("user32.dll")]
    private static extern nint GetFocus();
    [DllImport("user32.dll", EntryPoint = "SendMessageW")]
    private static extern nint SendMessage(nint window, uint message, nuint wparam, nint lparam);
    [DllImport("user32.dll", ExactSpelling = true)]
    private static extern bool PostMessageW(nint window, uint message, nuint wparam, nint lparam);

    private static int assertions;
    private static void Expect(bool condition)
    {
        ++assertions;
        if (!condition) throw new InvalidOperationException($"Text input assertion {assertions} failed.");
    }
    private static void Throws<T>(Action action) where T : Exception
    {
        ++assertions;
        try { action(); }
        catch (T) { return; }
        throw new InvalidOperationException($"Expected {typeof(T).Name}.");
    }

    internal static void Run()
    {
        using var window = new Window("Text selection binding tests");
        var input = window.TextInput("Path").SetText("A😀Z");
        Expect(input.Selection == new TextSelection(0, 0));
        Expect(ReferenceEquals(input.SetSelection(new(ulong.MaxValue, ulong.MaxValue)), input));
        Expect(input.Selection == new TextSelection(4, 4));
        input.Selection = new(2, 2);
        Expect(input.Selection == new TextSelection(1, 1));
        input.Selection = new(3, 2);
        Expect(input.Selection == new TextSelection(1, 3));
        Task.Run(() =>
        {
            Throws<XuiException>(() => _ = input.Selection);
            Throws<XuiException>(() => input.Selection = new(0, 0));
        }).GetAwaiter().GetResult();
        Expect(input.Selection == new TextSelection(1, 3));
        window.SetContent(window.Stack().Add(input));
        int changes = 0;
        input.Changed += _ => ++changes;
        bool ran = false;
        Expect(window.Post(() =>
        {
            try
            {
                Expect(input.Selection == new TextSelection(1, 3));
                input.Focus();
                input.Text = @"C:\folder\new path";
                ulong end = (ulong)input.Text.Length;
                input.SetSelection(new(end, end)).Focus();
                Expect(input.Selection == new TextSelection(end, end));
                var edit = GetFocus();
                long actual = (long)SendMessage(edit, 0xB0, 0, 0); // EM_GETSEL
                Expect((actual & 0xffff) == (long)end && ((actual >> 16) & 0xffff) == (long)end);
                Expect(changes == 0);
                SendMessage(edit, 0xB1, 3, 9); // EM_SETSEL
                Expect(input.Selection == new TextSelection(3, 9));
                input.Text = @"C:\folder\child";
                input.SetSelection(new(ulong.MaxValue, ulong.MaxValue));
                SendMessage(edit, 0x102, 0x7f, 1); // Ctrl+Backspace's WM_CHAR
                Expect(input.Text == @"C:\folder\" && changes == 1);
                Expect(SendMessage(edit, 0xC7, 0, 0) != 0); // EM_UNDO
                Expect(input.Text == @"C:\folder\child" && changes == 2);
                ran = true;
            }
            finally { window.Close(); }
        }));
        using var finished = new ManualResetEventSlim();
        var watchdog = Task.Run(() => { if (!finished.Wait(TimeSpan.FromSeconds(15))) window.Post(window.Close); });
        try { window.Run(); }
        finally { finished.Set(); watchdog.GetAwaiter().GetResult(); }
        Expect(ran);
        Throws<XuiException>(() => _ = input.Selection);
        Throws<XuiException>(() => input.Selection = new(0, 0));
        window.Dispose();
        Throws<ObjectDisposedException>(() => _ = input.Selection);
        Throws<ObjectDisposedException>(() => input.Selection = new(0, 0));
        TypingRedirect();
        Console.WriteLine($"C# text selection assertions: {assertions} passed");
    }

    private static void TypingRedirect()
    {
        using var window = new Window("Native typing redirect");
        var source = window.Button("Files");
        var input = window.TextInput("Find").Visible(false);
        window.SetContent(window.Stack().Add(source).Add(input));
        bool navigationSeen = false, typingSeen = false, changed = false;
        window.KeyHandler = key =>
        {
            Expect(key.TargetId == source.Id);
            if (key.VirtualKey == 0x25)
            {
                Expect(!key.IsTextInput);
                navigationSeen = true;
                return false;
            }
            Expect(key.VirtualKey == 0x20 && key.IsTextInput && navigationSeen);
            typingSeen = true;
            input.Visible(true).Focus();
            return false;
        };
        input.Changed += text =>
        {
            Expect(typingSeen && input.Focused && text == " ");
            changed = true;
            window.Close();
        };
        Expect(window.Post(() =>
        {
            source.Focus();
            Expect(PostMessageW(GetFocus(), 0x100, 0x25, 1));
            Expect(PostMessageW(GetFocus(), 0x100, 0x20, 1));
        }));
        using var finished = new ManualResetEventSlim();
        var watchdog = Task.Run(() => { if (!finished.Wait(TimeSpan.FromSeconds(15))) window.Post(window.Close); });
        try { window.Run(); }
        finally { finished.Set(); watchdog.GetAwaiter().GetResult(); }
        Expect(navigationSeen && typingSeen && changed);
    }
}
