using PortableDemo;
using Xui;
using Xui.Experimental.Windows;
using P = Xui.Experimental.Portable;

internal static partial class Program
{
    private static void NativeResponsiveScenarios()
    {
        Check(ContentHost.SupportsViewportObservation, "The native content viewport observer is required.");
        using var application = new Application();
        using var window = application.CreateWindow("Owned responsive allocation", 620, 960, visualStyle: VisualStyle.WinUI);
        var surface = window.CreateContentHost();
        surface.SetControlStyleValues(StylePart.Root, new PartStyleValues { Padding = new Insets(10, 20, 30, 40) });
        var allocation = window.Stack().Add(surface, 1);
        window.SetContent(allocation);
        using var dispatcher = new WindowsDispatcher(window);
        using var host = new P.Host(dispatcher);
        var app = new Greeting(host);
        var backend = new WindowsBackend(surface, dispatcher);
        host.Attach(backend);
        int notices = 0;
        P.Size latest = default;
        using var observation = host.ObserveViewport(size => { notices++; latest = size; });
        Check(notices == 0, "Host viewport metadata escaped synchronously into authored code.");
        application.Show(window);
        var ui = new NativeUi(application);
        var work = Task.Run(() =>
        {
            try
            {
                ui.Wait(() => notices != 0 && latest.Width > 0 && latest.Height > 0);
                nint editor = ui.Ui(() =>
                {
                    var bounds = surface.GetBounds();
                    Check(Math.Abs(latest.Width - bounds.Width + 40) < 0.1f &&
                        Math.Abs(latest.Height - bounds.Height + 60) < 0.1f,
                        "Viewport metadata reported outer host bounds instead of the root's inset allocation.");
                    app.Entry = "Retained responsive input";
                    Check(host.TryFocus(app.Input), "Responsive fixture input did not receive native focus.");
                    host.SetSelection(app.Input, new(2, 6));
                    return GetFocus();
                });
                ui.Ui(() => surface.SetControlStyleValues(StylePart.Root,
                    new PartStyleValues { Padding = new Insets(20, 30, 40, 50) }));
                ui.Wait(() => Math.Abs(latest.Width - surface.GetBounds().Width + 60) < 0.1f &&
                    Math.Abs(latest.Height - surface.GetBounds().Height + 80) < 0.1f);
                ui.Ui(() => surface.FixedSize(300, 400));
                ui.Wait(() => Math.Abs(latest.Width - 240) < 0.1f && Math.Abs(latest.Height - 320) < 0.1f);
                ui.Ui(() =>
                {
                    Check(GetFocus() == editor && host.GetSelection(app.Input) == new P.TextSelection(2, 6),
                        "An internal borrowed-host resize replaced or edited its native input.");
                    surface.FixedSize(310, 410);
                    surface.FixedSize(320, 420);
                });
                ui.Wait(() => Math.Abs(latest.Width - 260) < 0.1f && Math.Abs(latest.Height - 340) < 0.1f);
                int before = ui.Ui(() => notices);
                ui.Ui(() =>
                {
                    observation.Dispose();
                    surface.FixedSize(280, 380);
                });
                ui.Wait(() => Math.Abs(surface.GetBounds().Width - 280) < 0.1f);
                ui.Ui(() =>
                {
                    Check(notices == before, "A disposed viewport subscription received later allocation changes.");
                    host.Detach();
                    using var raw = surface.ObserveViewport(_ => { });
                    using var update = surface.BeginUpdate();
                    update.Commit(window.Stack().Add(window.Label("New content arena")));
                    Check(NativeObserverCount(window, "contentViewports") == 1,
                        "Replacing content retired the persistent host's independent viewport observation.");
                    update.Dispose();
                });
            }
            finally { ui.Ui(window.Close); }
        });
        RunNativeWork(application, work);
        Check(NativeObserverCount(window, "contentViewports") == 0, "Viewport subscriptions leaked after their owners disposed.");
    }
}
