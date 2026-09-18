using Xui;

internal static class TabAnimationTests
{
    private static int count;
    private static void Expect(bool value) { if (!value) throw new Exception("Tab animation binding contract failed."); count++; }
    private static void Reject(Action action)
    {
        try { action(); }
        catch (XuiException) { count++; return; }
        throw new Exception("Expected a tab duration API error.");
    }
    internal static void Run()
    {
        using var window = new Window(customTitlebar: true);
        var tabs = window.TabStrip("Animated tabs");
        Expect(tabs.Duration == 0 && window.TitlebarTabs.Duration == 0);
        var button = tabs.NewTabButton;
        tabs.NewTabButtonVisible = true;
        foreach (uint value in new[] { 0u, 180u, 10000u })
        {
            Expect(ReferenceEquals(tabs, tabs.SetDuration(value)));
            Expect(tabs.Duration == value);
        }
        foreach (uint value in new[] { 10001u, uint.MaxValue })
        {
            Reject(() => tabs.Duration = value);
            Expect(tabs.Duration == 10000);
        }
        tabs.Duration = 180;
        int selected = 0, created = 0;
        tabs.Event += e =>
        {
            if (e.Kind == EventKind.Selection) selected++;
            if (e.Kind == EventKind.Action) created++;
        };
        tabs.SetTabItems([new(11, "First")], 11);
        tabs.SetTabItems([new(11, "First"), new(22, "Inserted")], 22);
        Expect(selected == 0);
        tabs.Select(22);
        Expect(selected == 0);
        tabs.Select(11);
        Expect(selected == 1);
        tabs.SetTabItems([new(22, "Inserted"), new(11, "First")], 11);
        tabs.SetTabItems([new(11, "First"), new(22, "Inserted")], 11);
        Expect(selected == 1);
        tabs.SetTabItems([new(22, "Moved"), new(11, "First")], 22);
        tabs.SetTabItems([new(11, "Remaining")], 11);
        Expect(ReferenceEquals(tabs.NewTabButton, button));
        button.Invoke();
        Expect(created == 1);
        Reject(() => tabs.SetTabItems([new(11, "Duplicate"), new(11, "Duplicate")], 11));
        Expect(tabs.Duration == 180);
        Task.Run(() =>
        {
            Reject(() => tabs.Duration = 0);
            Reject(() => _ = tabs.Duration);
        }).GetAwaiter().GetResult();
        window.TitlebarTabs.Duration = 180;
        Expect(window.TitlebarTabs.Duration == 180 && window.TitlebarSecondaryTabs.Duration == 0);
        window.Dispose();
        try { _ = tabs.Duration; throw new Exception("Expected disposed tab access to fail."); }
        catch (ObjectDisposedException) { count++; }
        Console.WriteLine($"Tab animation binding assertions: {count} passed.");
    }
}
