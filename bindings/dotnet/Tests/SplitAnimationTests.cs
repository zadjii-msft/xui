using Xui;

internal static class SplitAnimationTests
{
    internal static void Run()
    {
        int count = 0;
        void Expect(bool value) { if (!value) throw new Exception("Split animation contract failed."); count++; }
        using var window = new Window();
        var first = window.TextInput("First");
        var second = window.TextInput("Second");
        var split = window.SplitView("Panes", first, second);
        Expect(split.TransitionDuration == 0 && split.Progress == 1 && !split.Animating);
        split.SecondVisible = false;
        Expect(split.Progress == 0 && !split.Animating);
        Expect(ReferenceEquals(split, split.SetTransitionDuration(180)));
        split.SecondVisible = true;
        Expect(split.Animating && split.Progress == 0);
        foreach (uint invalid in new[] { 10001u, uint.MaxValue })
        {
            try { split.TransitionDuration = invalid; throw new Exception("Expected invalid duration rejection."); }
            catch (XuiException) { count++; }
            Expect(split.TransitionDuration == 180 && split.Animating);
        }
        split.TransitionDuration = 0;
        Expect(!split.Animating && split.Progress == 1);
        split.SecondVisible = false;
        Expect(!split.Animating && split.Progress == 0);
        split.TransitionDuration = 10000;
        Expect(split.TransitionDuration == 10000);
        Console.WriteLine($"Split animation bindings passed: {count} assertions.");
    }
}
