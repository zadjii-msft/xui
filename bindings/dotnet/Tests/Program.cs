using Xui;
using System.Runtime.CompilerServices;

internal static class Tests
{
    [STAThread]
    private static int Main(string[] args)
    {
        try
        {
            if (args is not ["--text-only"]) { Run(); FeatureTests.Run(); }
            ExplorerTextTests.Run();
            return 0;
        }
        catch (Exception error) { Console.Error.WriteLine(error); return 1; }
    }
    private static int count;
    private static void Assert(bool condition) { if (!condition) throw new Exception("Assertion failed."); ++count; }
    private static void Throws<T>(Action body) where T : Exception
    {
        try { body(); } catch (T) { ++count; return; }
        throw new Exception($"Expected {typeof(T).Name}.");
    }
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static WeakReference DisposedSubscription()
    {
        var w = new Window();
        var button = w.Button("Release");
        button.Click += w.Close;
        w.Dispose();
        return new WeakReference(w);
    }
    internal static void Run()
    {
        using (var w = new Window())
        {
            Assert(w.Style == VisualStyle.Classic);
            Assert(ReferenceEquals(w, w.SetVisualStyle(VisualStyle.WinUI)));
            Assert(w.Style == VisualStyle.WinUI);
            w.SetTheme(Theme.Light);
            Assert(w.Style == VisualStyle.WinUI);
            w.SetTheme(Theme.HighContrast);
            Assert(w.Style == VisualStyle.WinUI);
            Throws<XuiException>(() => w.SetVisualStyle((VisualStyle)2));
            Assert(w.Style == VisualStyle.WinUI);
            Task.Run(() =>
            {
                Throws<XuiException>(() => w.SetVisualStyle(VisualStyle.Classic));
                Throws<XuiException>(() => _ = w.Style);
            }).GetAwaiter().GetResult();
            w.SetVisualStyle(VisualStyle.Classic);
            Assert(w.Style == VisualStyle.Classic);
            var label = w.Label("日本語 😀");
            Assert(label.Text == "日本語 😀");
            Assert(ReferenceEquals(label, label.SetText("日本語 😀").SetName("Label").SetEnabled(true).SetAutomationId("label")));
            var root = w.Stack().Padding(8).Spacing(4).FixedSize(300, 300);
            Assert(ReferenceEquals(root, root.Add(label)));
            Assert(ReferenceEquals(w, w.SetContent(root).SetTheme(Theme.Light)));
            Assert(ReferenceEquals(w, w.Update(new Property(label, PropertyKind.Text, "日本語 😀"))));
            var toggle = w.Toggle("Toggle").SetChecked(true).FixedSize(100, 24);
            Assert(ReferenceEquals(toggle, toggle.SetChecked(false)));
            var scroll = w.ScrollView(w.Stack(), "Scroll").SetOffset(0);
            Assert(ReferenceEquals(scroll, scroll.SetOffset(0)));
            var shortcuts = w.ItemsView("Commands");
            Assert(ReferenceEquals(shortcuts, shortcuts.SetTrailingShortcutBadges(true).SetTrailingShortcutBadges(false)));
            var popup = w.Popup("Palette", w.Stack());
            Assert(ReferenceEquals(popup, popup.SetWindowBackground(true).SetWindowBackground(false)));
            Task.Run(() =>
            {
                Throws<XuiException>(() => shortcuts.SetTrailingShortcutBadges(true));
                Throws<XuiException>(() => popup.SetWindowBackground(true));
            }).GetAwaiter().GetResult();
            var image = w.Image("Image").FixedSize(100, 100).Source("");
            Assert(ReferenceEquals(image, image.Source("")));
            Throws<ArgumentException>(() => label.Text = "\0");
            Throws<ArgumentException>(() => label.SetText("\0"));
            Throws<System.Text.EncoderFallbackException>(() => label.Text = "\ud800");
            Throws<XuiException>(() => w.Update(new(label, PropertyKind.Text, "changed"),
                new(label, PropertyKind.Checked, Integer: 1)));
            Assert(label.Text == "日本語 😀");
            Task.Run(() => Throws<XuiException>(() => label.Text = "Wrong thread")).GetAwaiter().GetResult();
            using var other = new Window();
            Throws<ArgumentException>(() => other.Stack().Add(label));
            var button = w.Button("Button");
            int calls = 0;
            Action<UiEvent>? action = null;
            action = _ => { calls++; button.Event -= action; Throws<XuiException>(w.Dispose); };
            button.Event += action;
            GC.Collect(); GC.WaitForPendingFinalizers(); GC.Collect();
            button.Invoke(); button.Invoke(); Assert(calls == 1);
            var list = w.FileList("Files");
            Assert(ReferenceEquals(list, list.SetItems([new(0, "zero 😀", ""), new(8, "eight", "")])));
            Assert(ReferenceEquals(list, list.Select(0))); Assert(list.State == (2u, (ulong?)0));
            Assert(ReferenceEquals(list, list.Filter("eight"))); Assert(list.State == (1u, (ulong?)0));
            list.Select(null); Assert(list.State.SelectedId is null);
            Throws<XuiException>(() => list.Select(99));
            w.Dispose();
            Throws<ObjectDisposedException>(() => _ = label.Text);
            Throws<ObjectDisposedException>(() => shortcuts.SetTrailingShortcutBadges(true));
            Throws<ObjectDisposedException>(() => popup.SetWindowBackground(true));
            Throws<ObjectDisposedException>(() => w.SetVisualStyle(VisualStyle.Classic));
            Throws<ObjectDisposedException>(() => _ = w.Style);
        }
        Throws<ArgumentOutOfRangeException>(() => new Window(visualStyle: (VisualStyle)2));
        using (var styled = new Window(customTitlebar: true, visualStyle: VisualStyle.WinUI))
            Assert(styled.Style == VisualStyle.WinUI);
        using (var w = new Window())
        {
            var button = w.Button("Fail");
            button.Event += _ => throw new InvalidOperationException("callback sentinel");
            try { button.Invoke(); throw new Exception("Expected callback failure."); }
            catch (XuiException e) { Assert(e.Status == 8 && e.InnerException?.Message == "callback sentinel"); }
        }
        using (var w = new Window())
        {
            var button = w.Button("Reentry");
            button.Event += _ => button.Invoke();
            Throws<XuiException>(button.Invoke);
        }
        var weak = DisposedSubscription();
        GC.Collect(); GC.WaitForPendingFinalizers(); GC.Collect();
        Assert(!weak.IsAlive);
        Console.WriteLine($"C# wrapper assertions: {count} passed; architecture: {System.Runtime.InteropServices.RuntimeInformation.ProcessArchitecture}");
    }
}
