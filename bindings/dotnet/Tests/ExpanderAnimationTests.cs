using Xui;

internal static class ExpanderAnimationTests
{
    private static int count;
    private static void Expect(bool value) { if (!value) throw new Exception("Expander animation contract failed."); count++; }
    private static void Reject(Action action)
    {
        try { action(); }
        catch (XuiException) { count++; return; }
        throw new Exception("Expected an expander API error.");
    }
    internal static void Run()
    {
        using var window = new Window();
        var input = window.TextInput("Body");
        var expander = window.Expander("Details", input);
        Expect(expander.Expanded && expander.Duration == 0 && expander.Progress == 1 && !expander.Animating);
        expander.Expanded = false;
        Expect(expander.Progress == 0 && !expander.Animating);
        foreach (uint value in new[] { 0u, 180u, 10000u })
        {
            Expect(ReferenceEquals(expander, expander.SetDuration(value)));
            Expect(expander.Duration == value);
        }
        expander.Expanded = true;
        Expect(expander.Animating && expander.Expanded && expander.Progress == 0);
        foreach (uint value in new[] { 10001u, uint.MaxValue })
        {
            Reject(() => expander.Duration = value);
            Expect(expander.Duration == 10000 && expander.Animating);
        }
        expander.Duration = 0;
        Expect(expander.Progress == 1 && !expander.Animating);
        Reject(() => window.Expander("Duplicate", input));
        Task.Run(() =>
        {
            Reject(() => expander.Duration = 0);
            Reject(() => _ = expander.Duration);
            Reject(() => _ = expander.Progress);
            Reject(() => _ = expander.Animating);
        }).GetAwaiter().GetResult();
        using var scopedWindow = new Window();
        var host = scopedWindow.CreateContentHost();
        scopedWindow.SetContent(scopedWindow.Stack().Add(host));
        Expander scoped;
        using (var update = host.BeginUpdate())
        {
            scoped = scopedWindow.Expander("Scoped", scopedWindow.TextInput("Scoped body"));
            scoped.Duration = 180;
            update.Commit(scoped);
        }
        Reject(() => _ = scoped.Duration);
        window.Dispose();
        try { _ = expander.Duration; throw new Exception("Expected disposed expander access to fail."); }
        catch (ObjectDisposedException) { count++; }
        Console.WriteLine($"Expander animation binding assertions: {count} passed.");
    }
}
