using Xui;

internal static class NavigationAnimationTests
{
    private static int count;
    private static void Expect(bool value) { if (!value) throw new Exception("Navigation animation contract failed."); count++; }
    private static void Reject(Action action)
    {
        try { action(); }
        catch (XuiException) { count++; return; }
        throw new Exception("Expected a navigation API error.");
    }
    internal static void Run()
    {
        using var window = new Window();
        var view = window.NavigationView("Navigation");
        Expect(view.Duration == 0 && !view.Animating);
        foreach (uint value in new[] { 0u, 180u, 10000u })
        {
            Expect(ReferenceEquals(view, view.SetDuration(value)));
            Expect(view.Duration == value && !view.Animating);
        }
        foreach (uint value in new[] { 10001u, uint.MaxValue })
        {
            Reject(() => view.Duration = value);
            Expect(view.Duration == 10000 && !view.Animating);
        }
        Task.Run(() =>
        {
            Reject(() => view.Duration = 0);
            Reject(() => _ = view.Duration);
            Reject(() => _ = view.Animating);
        }).GetAwaiter().GetResult();
        window.Dispose();
        try { _ = view.Duration; throw new Exception("Expected disposed navigation access to fail."); }
        catch (ObjectDisposedException) { count++; }
        Console.WriteLine($"Navigation animation binding assertions: {count} passed.");
    }
}
