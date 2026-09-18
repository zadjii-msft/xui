using Xui;

internal static class SplitVisibilityTests
{
    private static void Require(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
    }
    private static void Throws<T>(Action action) where T : Exception
    {
        try { action(); }
        catch (T) { return; }
        throw new InvalidOperationException($"Expected {typeof(T).Name}");
    }
    internal static void Run()
    {
        using var window = new Window("Secondary-only split", 400, 300);
        window.SetShowActivated(false);
        var first = window.Stack();
        var second = window.Stack();
        var split = window.SplitView("Panes", first, second);
        split.Ratio = .4;
        Require(split.FirstVisible && split.SecondVisible, "Both panes default to visible");
        Require(ReferenceEquals(split.SetFirstVisible(false), split), "Fluent setter preserves split identity");
        Require(!split.FirstVisible && split.SecondVisible && split.Expanded, "Secondary-only layout remains expanded");
        split.SetSecondVisible(false);
        Require(!split.FirstVisible && !split.Expanded, "First visibility does not override hidden second pane");
        split.SetSecondVisible(true);
        split.FirstVisible = true;
        Require(split.FirstVisible && Math.Abs(split.Ratio - .4) < .0001, "Restore preserves ratio");
        Task.Run(() =>
        {
            Throws<XuiException>(() => _ = split.FirstVisible);
            Throws<XuiException>(() => split.FirstVisible = false);
        }).GetAwaiter().GetResult();
        Require(split.FirstVisible, "Rejected thread access preserves visibility");
        split.FirstVisible = false;
        window.SetContent(window.Stack().Add(split, 1));
        Require(window.Post(() =>
        {
            var area = split.GetBounds();
            var hidden = first.GetBounds();
            var visible = second.GetBounds();
            Require(area.Width > 0 && area.Width < 600 && area.Height > 0, "Fixture has a narrow client area");
            Require(hidden.Width == 0 && hidden.Height == 0, "Hidden first pane has zero extent");
            Require(visible == area && split.Expanded, "Secondary fills the area without a divider gap");
            split.FirstVisible = true;
            Require(split.FirstVisible && Math.Abs(split.Ratio - .4) < .0001, "Live restore preserves ratio");
            window.Close();
        }), "Layout assertion post accepted");
        window.Run();
        window.Dispose();
        Throws<ObjectDisposedException>(() => _ = split.FirstVisible);
        Throws<ObjectDisposedException>(() => split.FirstVisible = false);
        Console.WriteLine("Split first-pane visibility, ratio, narrow layout and lifetime guards passed");
    }
}
