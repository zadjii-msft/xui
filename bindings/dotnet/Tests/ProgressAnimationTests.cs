using Xui;

internal static class ProgressAnimationTests
{
    private static int count;
    private static void Expect(bool value) { if (!value) throw new Exception("Progress animation contract failed."); count++; }
    private static void Reject(Action action)
    {
        try { action(); }
        catch (XuiException) { count++; return; }
        throw new Exception("Expected a progress API error.");
    }
    internal static void Run()
    {
        using var window = new Window();
        var progress = window.Progress("Work");
        Expect(progress.Duration == 0 && progress.PresentedValue == 0 && !progress.Animating);
        progress.Value = 25;
        Expect(progress.PresentedValue == 25 && !progress.Animating);
        foreach (uint value in new[] { 0u, 180u, 10000u })
        {
            Expect(ReferenceEquals(progress, progress.SetDuration(value)));
            Expect(progress.Duration == value);
        }
        progress.Value = 75;
        Expect(progress.Value == 75 && progress.PresentedValue == 25 && progress.Animating);
        foreach (uint value in new[] { 10001u, uint.MaxValue })
        {
            Reject(() => progress.Duration = value);
            Expect(progress.Duration == 10000 && progress.Animating);
        }
        foreach (double value in new[] { double.NaN, double.PositiveInfinity, -1, 101 })
        {
            Reject(() => progress.Value = value);
            Expect(progress.Value == 75 && progress.PresentedValue == 25 && progress.Animating);
        }
        progress.Value = 50;
        Expect(progress.Value == 50 && progress.PresentedValue == 25 && progress.Animating);
        progress.Duration = 0;
        Expect(progress.PresentedValue == 50 && !progress.Animating);
        progress.Duration = 180;
        progress.Value = 100;
        progress.State = ProgressState.Indeterminate;
        Expect(progress.PresentedValue == 100 && !progress.Animating);
        progress.Value = 40;
        Expect(progress.PresentedValue == 40 && !progress.Animating);
        progress.State = ProgressState.Determinate;
        progress.Value = 80;
        progress.Range = new NumericRange(0, 50);
        Expect(progress.Value == 50 && progress.PresentedValue == 50 && !progress.Animating);
        Task.Run(() =>
        {
            Reject(() => progress.Duration = 0);
            Reject(() => _ = progress.Duration);
            Reject(() => _ = progress.PresentedValue);
            Reject(() => _ = progress.Animating);
        }).GetAwaiter().GetResult();
        using var scopedWindow = new Window();
        var host = scopedWindow.CreateContentHost();
        scopedWindow.SetContent(scopedWindow.Stack().Add(host));
        Progress scoped;
        using (var update = host.BeginUpdate())
        {
            scoped = scopedWindow.Progress("Scoped");
            scoped.Duration = 180;
            update.Commit(scoped);
        }
        Reject(() => _ = scoped.Duration);
        window.Dispose();
        try { _ = progress.Duration; throw new Exception("Expected disposed progress access to fail."); }
        catch (ObjectDisposedException) { count++; }
        Console.WriteLine($"Progress animation binding assertions: {count} passed.");
    }
}
