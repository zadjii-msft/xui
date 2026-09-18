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
        Require(split.Layout == (Axis.Horizontal, 300f), "Default split layout remains compatible");
        Require(ReferenceEquals(split.SetLayout(Axis.Vertical, 48), split), "Fluent layout setter preserves identity");
        Require(split.Layout == (Axis.Vertical, 48f), "Managed layout round-trips");
        Throws<XuiException>(() => split.SetLayout((Axis)2, 48));
        Throws<XuiException>(() => split.SetLayout(Axis.Horizontal, float.NaN));
        Throws<XuiException>(() => split.SetLayout(Axis.Horizontal, 0));
        Require(split.Layout == (Axis.Vertical, 48f), "Rejected layouts preserve state");
        var changes = 0;
        split.Event += e => { if (e.Kind == EventKind.Change) ++changes; };
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
            Throws<XuiException>(() => _ = split.Layout);
            Throws<XuiException>(() => split.SetLayout(Axis.Horizontal, 48));
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
            Require(changes == 1, "Changed ratio posts one owner-thread event");
            split.FirstVisible = true;
            Require(split.FirstVisible && Math.Abs(split.Ratio - .4) < .0001, "Live restore preserves ratio");
            window.Close();
        }), "Layout assertion post accepted");
        window.Run();
        window.Dispose();
        Throws<ObjectDisposedException>(() => _ = split.FirstVisible);
        Throws<ObjectDisposedException>(() => split.FirstVisible = false);
        Throws<ObjectDisposedException>(() => _ = split.Layout);
        Throws<ObjectDisposedException>(() => split.SetLayout(Axis.Horizontal, 48));
        Console.WriteLine("Split first-pane visibility, ratio, narrow layout and lifetime guards passed");
    }
}
