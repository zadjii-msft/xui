using PortableDemo;
using Xui;
using Xui.Experimental.Windows;
using P = Xui.Experimental.Portable;

internal static partial class Program
{
    private static void NativeThemeScenarios()
    {
        Check(Window.SupportsSemanticTheme, "The semantic window theme contract is required.");
        using var application = new Application();
        using var window = application.CreateWindow("Explicit theme authority", 620, 700,
            theme: Theme.System, visualStyle: VisualStyle.WinUI);
        var surface = window.CreateContentHost();
        var secondSurface = window.CreateContentHost();
        window.SetContent(window.Stack().Add(surface, 1).Add(secondSurface, 1));
        var original = new WindowThemeSettings(Theme.System, new ThemeColor(0x203040, 0xddeeff),
            new ThemeColor(0xf4f5f6, 0x182028), new ThemeColor(0x006699, 0x88ccff));
        window.SetThemeSettings(original);
        Check(window.GetThemeSettings() == original, "The initial requested System palette did not round-trip.");
        using var dispatcher = new WindowsDispatcher(window);
        using var host = new P.Host(dispatcher);
        _ = new Greeting(host);
        var borrowed = new WindowsBackend(surface, dispatcher);
        host.Attach(borrowed);
        Throws<NotSupportedException>(() => host.Theme = P.ThemeSettings.System);
        Check(host.Theme is null && host.IsAttached && window.GetThemeSettings() == original,
            "A borrowed surface mutated the host model or window-wide theme.");
        host.Detach();

        var backend = new WindowsBackend(surface, dispatcher, WindowsThemeAuthority.ExclusiveWindow);
        host.Attach(backend);
        application.Show(window);
        var ui = new NativeUi(application);
        var work = Task.Run(() =>
        {
            try
            {
                ui.Ui(() =>
                {
                    var resources = new P.ThemeResources(new(0x001122, 0xf1f2f3),
                        new(0xffffff, 0x111820), new(0x005588, 0x66bbff));
                    host.Theme = new(P.ThemeMode.Light, resources);
                    using (var competitor = new P.Host(dispatcher))
                    {
                        _ = new Greeting(competitor);
                        competitor.Attach(new WindowsBackend(secondSurface, dispatcher, WindowsThemeAuthority.ExclusiveWindow));
                        Throws<InvalidOperationException>(() => competitor.Theme = P.ThemeSettings.System);
                        Check(competitor.Theme is null && competitor.IsAttached,
                            "A second content host acquired theme authority from the same native window.");
                    }
                    Check(window.GetThemeSettings() == new WindowThemeSettings(Theme.Light,
                        new(0x001122, 0xf1f2f3), new(0xffffff, 0x111820), new(0x005588, 0x66bbff)),
                        "Exclusive theme authority did not apply exact semantic light/dark pairs.");
                    host.Theme = new(P.ThemeMode.Dark, resources);
                    Check(window.GetThemeSettings().Mode == Theme.Dark, "Explicit Dark mode was not applied.");
                    host.Theme = P.ThemeSettings.System;
                    Check(window.GetThemeSettings() == new WindowThemeSettings(Theme.System),
                        "Explicit System did not retain requested OS tracking or clear semantic overrides.");
                    host.Theme = null;
                    Check(window.GetThemeSettings() == original, "Null theme did not restore the exact borrowed baseline.");
                    host.Theme = new(P.ThemeMode.Dark);
                    host.Detach();
                    Check(window.GetThemeSettings() == original, "Live detach did not restore the window's original theme authority.");

                    var secondBaseline = new WindowThemeSettings(Theme.Light, Accent: new ThemeColor(0, 0));
                    window.SetThemeSettings(secondBaseline);
                    backend = new WindowsBackend(surface, dispatcher, WindowsThemeAuthority.ExclusiveWindow);
                    host.Attach(backend);
                    Check(window.GetThemeSettings().Mode == Theme.Dark, "Reattachment did not reapply retained host theme state.");
                    host.Theme = null;
                    Check(window.GetThemeSettings() == secondBaseline,
                        "A new attachment reused an obsolete baseline or conflated RGB black with an unset color.");
                    host.Theme = new(P.ThemeMode.Light);
                    host.Detach();
                    Check(window.GetThemeSettings() == secondBaseline, "The replacement theme baseline was not restored.");
                });
            }
            finally { ui.Ui(window.Close); }
        });
        RunNativeWork(application, work);
        Check(window.GetThemeSettings() == new WindowThemeSettings(Theme.Light, Accent: new ThemeColor(0, 0)),
            "Closed-window theme readback lost the requested palette.");
    }

    private static void SharedPresentationScenarios()
    {
        using var application = new Application();
        using var window = application.CreateWindow("Portable native scenarios", 620, 960, visualStyle: VisualStyle.WinUI);
        var surface = Surface(window);
        var baseline = window.GetThemeSettings();
        using var dispatcher = new WindowsDispatcher(window);
        using var host = new P.Host(dispatcher);
        var sample = PresentationWorkbench.Create(host);
        var backend = new WindowsBackend(surface, dispatcher, WindowsThemeAuthority.ExclusiveWindow);
        host.Attach(backend);
        application.Show(window);
        var ui = new Driver(application, window, host, () => backend);
        var work = Task.Run(() =>
        {
            try
            {
                ui.Change("presentation-draft", "Retained shared presentation draft");
                nint editor = ui.Ui(() =>
                {
                    Check(host.TryFocus(sample.DraftInput), "The shared presentation editor did not receive focus.");
                    host.SetSelection(sample.DraftInput, new(2, 8));
                    return GetFocus();
                });
                ui.Click("presentation-larger");
                ui.Wait(() => Math.Abs(Math.Abs(ReadNativeFont(editor).Height) - 28 * GetDpiForWindow(editor) / 96f) <= 1);
                ui.Click("presentation-bold");
                ui.Wait(() => ReadNativeFont(editor).Weight == 700);
                foreach (string id in new[] { "presentation-light", "presentation-colors", "presentation-dark", "presentation-system" })
                {
                    ui.Click(id);
                    ui.Ui(() => Check(IsWindow(editor) && sample.Draft == "Retained shared presentation draft",
                        "A shared theme operation replaced the editor or changed its text."));
                }
                ui.Ui(() => Check(window.GetThemeSettings() == new WindowThemeSettings(Theme.System,
                    new(0x1a1a1a, 0xffffff), new(0xffffff, 0x202020), new(0x005fb8, 0x60cdff)),
                    "The shared workbench did not publish exact semantic resources with requested System tracking."));
                ui.Click("presentation-inherit");
                ui.Ui(() =>
                {
                    Check(host.Theme is null && window.GetThemeSettings() == baseline,
                        "The shared inherited action did not restore exact window appearance.");
                    Check(host.TryFocus(sample.DraftInput) && host.GetSelection(sample.DraftInput) == new P.TextSelection(2, 8),
                        "Shared appearance changes lost the native edit range.");
                    host.Detach();
                });
            }
            finally { ui.Ui(window.Close); }
        });
        RunNativeWork(application, work);
    }
}
