using Xui;

internal static class RevealTests
{
    private static int count;
    private static void Expect(bool value) { if (!value) throw new Exception("Reveal contract failed."); count++; }
    private static void Reject(Action action)
    {
        try { action(); }
        catch (XuiException) { count++; return; }
        throw new Exception("Expected a Reveal API error.");
    }
    internal static void Run()
    {
        using var window = new Window();
        var input = window.TextInput("Find");
        var body = window.Stack().Add(input);
        var reveal = window.Reveal(body);
        Expect(reveal.Text == "Reveal" && !reveal.Open && reveal.Duration == 0 && reveal.Progress == 0 && !reveal.Animating);
        Expect(reveal.Layout == RevealLayout.Fixed && reveal.Direction == RevealDirection.Bottom);
        Expect(ReferenceEquals(reveal, reveal.SetOpen(true)));
        Expect(reveal.Open && reveal.Progress == 1 && !reveal.Animating);
        reveal.Open = false;
        Expect(reveal.Progress == 0 && !reveal.Animating);
        foreach (uint duration in new[] { 0u, 180u, 10000u })
        {
            Expect(ReferenceEquals(reveal, reveal.SetDuration(duration)));
            Expect(reveal.Duration == duration);
        }
        foreach (uint invalid in new[] { 10001u, uint.MaxValue })
        {
            Reject(() => reveal.Duration = invalid);
            Expect(reveal.Duration == 10000);
        }
        foreach (var layout in Enum.GetValues<RevealLayout>())
        {
            Expect(ReferenceEquals(reveal, reveal.SetLayout(layout)));
            Expect(reveal.Layout == layout);
        }
        foreach (var direction in Enum.GetValues<RevealDirection>())
        {
            Expect(ReferenceEquals(reveal, reveal.SetDirection(direction)));
            Expect(reveal.Direction == direction);
        }
        foreach (uint invalid in new[] { 4u, uint.MaxValue })
        {
            Reject(() => reveal.Direction = (RevealDirection)invalid);
            Expect(reveal.Direction == RevealDirection.Right);
        }
        foreach (uint invalid in new[] { 2u, uint.MaxValue })
        {
            Reject(() => reveal.Layout = (RevealLayout)invalid);
            Expect(reveal.Layout == RevealLayout.Expand);
        }
        reveal.Open = true;
        Expect(reveal.Animating && reveal.Progress == 0);
        Reject(() => reveal.Layout = (RevealLayout)2);
        Reject(() => reveal.Direction = (RevealDirection)4);
        Expect(reveal.Animating && reveal.Progress == 0);
        reveal.Layout = RevealLayout.Fixed;
        Expect(reveal.Open && !reveal.Animating && reveal.Progress == 1);
        reveal.Open = false;
        reveal.Direction = RevealDirection.Bottom;
        Expect(!reveal.Open && !reveal.Animating && reveal.Progress == 0);
        Reject(() => window.Reveal(body));
        Reject(() => window.Stack().Add(body));
        using var other = new Window();
        try { other.Reveal(body); throw new Exception("Expected cross-window Reveal rejection."); }
        catch (ArgumentException) { count++; }
        window.SetContent(window.Stack().Add(reveal));
        Task.Run(() =>
        {
            Reject(() => reveal.Open = true);
            Reject(() => reveal.Duration = 180);
            Reject(() => _ = reveal.Open);
            Reject(() => _ = reveal.Duration);
            Reject(() => _ = reveal.Progress);
            Reject(() => _ = reveal.Animating);
            Reject(() => reveal.Layout = RevealLayout.Expand);
            Reject(() => reveal.Direction = RevealDirection.Left);
            Reject(() => _ = reveal.Layout);
            Reject(() => _ = reveal.Direction);
        }).GetAwaiter().GetResult();
        using var scopedWindow = new Window();
        var host = scopedWindow.CreateContentHost();
        var unscoped = scopedWindow.Stack();
        scopedWindow.SetContent(scopedWindow.Stack().Add(host));
        Reveal scoped;
        using (var update = host.BeginUpdate())
        {
            Reject(() => scopedWindow.Reveal(unscoped));
            scoped = scopedWindow.Reveal(scopedWindow.Stack(), "Scoped reveal");
            update.Commit(scoped);
            Expect(!scoped.Open && scoped.Duration == 0);
        }
        Reject(() => _ = scoped.Open);
        window.Dispose();
        try { _ = reveal.Open; throw new Exception("Expected disposed Reveal rejection."); }
        catch (ObjectDisposedException) { count++; }
        Console.WriteLine($"Reveal binding assertions: {count} passed.");
    }
}
