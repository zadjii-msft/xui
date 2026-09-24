using PortableDemo;
using Xui;
using Xui.Experimental.Windows;
using P = Xui.Experimental.Portable;

internal static partial class Program
{
    private static void NativeSettingsScenarios()
    {
        using var application = new Application();
        using var window = application.CreateWindow("Portable native scenarios", 620, 960, visualStyle: VisualStyle.WinUI);
        var surface = Surface(window);
        uint baseline = HandleCount(window);
        using var dispatcher = new WindowsDispatcher(window);
        using var host = new P.Host(dispatcher);
        var settings = new SettingsShowcase(host);
        var backend = new WindowsBackend(surface, dispatcher);
        host.Attach(backend);
        application.Show(window);
        var driver = new Driver(application, window, host, () => backend);
        var work = Task.Run(() =>
        {
            try
            {
                Interlocked.Add(ref assertions, SettingsScenarioRunner.Run(Resource("SettingsScenarios.json"), driver));
                driver.Click("reset-settings");
                driver.Ui(() =>
                {
                    Check(!host.TryFocus(settings.ChangesLabel) && !host.HasFocus(settings.ChangesLabel) &&
                        !host.TryFocus(settings.WorkProgress), "Nonfocusable portable controls accepted native focus.");
                    settings.Draft = "A\ud83d\ude00Z";
                    Check(host.TryFocus(settings.DraftInput) && host.HasFocus(settings.DraftInput),
                        "The portable focus API did not focus its native Edit.");
                    nint editor = GetFocus();
                    host.SetSelection(settings.DraftInput, new(2, 2));
                    Check(host.GetSelection(settings.DraftInput) == new P.TextSelection(1, 1),
                        "A caret split a native UTF-16 surrogate pair.");
                    host.SetSelection(settings.DraftInput, new(3, 2));
                    Check(host.GetSelection(settings.DraftInput) == new P.TextSelection(1, 3),
                        "Portable native selection did not order and expand a surrogate-spanning range.");
                    var input = driver.Input("settings-draft");
                    input.Selection = new(0, 4);
                    Check(host.GetSelection(settings.DraftInput) == new P.TextSelection(0, 4),
                        "The portable selection getter returned cached state instead of the actual native range.");
                    SendText(GetFocus(), 0x000c, 0, "longer native text");
                    Check(settings.Draft == "A\ud83d\ude00Z", "The queued native event was delivered synchronously.");
                    host.SetSelection(settings.DraftInput, new(int.MaxValue, int.MaxValue));
                    Check(host.GetSelection(settings.DraftInput) == new P.TextSelection(18, 18),
                        "Selection clamped against stale portable text rather than current native text.");
                    settings.Draft = "cancel queued native edit";
                    settings.DraftInput.Enabled = false;
                    Check(!host.TryFocus(settings.DraftInput), "A disabled portable input accepted focus.");
                    settings.DraftInput.Enabled = true;
                    settings.DraftInput.Visible = false;
                    Check(!host.TryFocus(settings.DraftInput), "A hidden portable input accepted focus.");
                    settings.DraftInput.Visible = true;
                    Check(host.HasFocus(settings.DraftInput) == (GetFocus() == editor),
                        "The portable focus getter did not report actual native focus after coalesced availability changes.");
                    Check(host.TryFocus(settings.DraftInput) && host.HasFocus(settings.DraftInput),
                        $"A restored portable input could not regain native focus: native focused={input.Focused}, HWND={GetFocus()}, class={ClassName(GetFocus())}.");
                    Throws<ArgumentOutOfRangeException>(() => host.SetSelection(settings.DraftInput, new(-1, 0)));
                });
                Throws<InvalidOperationException>(() => host.GetSelection(settings.DraftInput));
                driver.Ui(() =>
                {
                    settings.Draft = "Retained native draft";
                    var input = driver.Input("settings-draft");
                    input.Focus();
                    input.Selection = new(2, 5);
                    nint edit = GetFocus();
                    settings.Notifications = false;
                    settings.Sync = P.CheckState.Checked;
                    settings.Completed = 75;
                    settings.Units = 2.75;
                    Check(GetFocus() == edit && input.Selection == new TextSelection(2, 5) && settings.Changes == 0,
                        "Programmatic settings disturbed the editor or echoed native user events.");
                    ((Toggle)driver.Native("notifications")).Invoke();
                    settings.Notifications = true;
                    settings.Sync = P.CheckState.Indeterminate;
                    ((CheckBox)driver.Native("sync-policy")).Invoke();
                    settings.CycleMixed = false;
                });
                driver.Ui(() =>
                {
                    Check(settings.Changes == 0 && settings.Notifications &&
                        ((CheckBox)driver.Native("sync-policy")).State == CheckState.Indeterminate,
                        "Delayed native choices overwrote newer programmatic values or mixed-state policy.");
                    var input = driver.Input("settings-draft");
                    input.Focus();
                    SendText(GetFocus(), 0x000c, 0, "superseded by availability change");
                    settings.Content.Enabled = false;
                    settings.Content.Enabled = true;
                });
                driver.Ui(() =>
                {
                    Check(settings.Draft == "Retained native draft" &&
                        driver.Input("settings-draft").Text == settings.Draft && settings.Changes == 0,
                        "Invalidated native edits diverged from the retained model.");
                    host.Detach();
                    Throws<InvalidOperationException>(() => host.GetSelection(settings.DraftInput));
                    Throws<InvalidOperationException>(() => host.TryFocus(settings.DraftInput));
                    Check(HandleCount(window) == baseline, "Settings controls leaked their native attachment.");
                });
            }
            finally { driver.Ui(window.Close); }
        });
        try { application.Run(); }
        finally { work.WaitAsync(Timeout).GetAwaiter().GetResult(); }
    }
}
