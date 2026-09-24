using Xui;
using Xui.Experimental.Windows;
using P = Xui.Experimental.Portable;

internal static partial class Program
{
    private static void NativeSelectionScenarios()
    {
        using var application = new Application();
        using var window = application.CreateWindow("Native choices and ranges", 620, 500, visualStyle: VisualStyle.WinUI);
        var surface = Surface(window);
        uint baseline = HandleCount(window);
        using var dispatcher = new WindowsDispatcher(window);
        using var host = new P.Host(dispatcher);
        P.SingleChoice choice;
        P.RangeInput range;
        int selections = 0, changes = 0, previews = 0, cancels = 0;
        using (var build = host.BeginBuild())
        {
            choice = host.SingleChoice("Native choices");
            choice.AutomationId = "choice-native";
            choice.SetItems([new(1, "First"), new(9007199254740993UL, "Large ID"), new(3, "Disabled", false)]);
            choice.Changed += _ => selections++;
            range = host.RangeInput("Native range");
            range.AutomationId = "range-native";
            range.SetRange(new(0, 100, 5, 20));
            range.Changed += _ => changes++;
            range.Previewed += _ => previews++;
            range.Canceled += _ => cancels++;
            host.SetContent(host.Stack(P.Axis.Vertical).Spacing(20).Add(choice).Add(range));
            build.Complete();
        }
        var backend = new WindowsBackend(surface, dispatcher);
        host.Attach(backend);
        application.Show(window);
        var ui = new NativeUi(application);
        var nativeChoice = (ComboBox)backend.FindControls("choice-native").Single();
        var nativeRange = (RangeInput)backend.FindControls("range-native").Single();
        var work = Task.Run(() =>
        {
            try
            {
                ui.Ui(() =>
                {
                    Check(nativeChoice.Editor is null && nativeChoice.Selected == 1,
                        "SingleChoice did not use actual noneditable native selection.");
                    choice.SetSelected(9007199254740993UL);
                    Check(nativeChoice.Selected == 9007199254740993UL && selections == 0,
                        "Silent native selection lost 64-bit identity or emitted an authored notice.");
                    nativeChoice.Focus();
                    nativeChoice.Select(1);
                    Check(selections == 0, "Native choice events were not deferred outside the native callback.");
                });
                ui.Wait(() => choice.Selected == 1 && selections == 1);
                ui.Ui(() =>
                {
                    choice.SetItems([new(1, "Unavailable", false)]);
                    Check(nativeChoice.Selected is null && choice.Selected is null && selections == 1,
                        "All-disabled native choices did not preserve explicit absence silently.");
                    choice.SetItems([]);
                    Check(nativeChoice.Selected is null, "Empty native choices selected a phantom item.");
                    choice.SetItems([new(7, "Seventh"), new(8, "Eighth")]);
                    Check(nativeChoice.Selected == 7 && choice.Selected == 7, "Native fallback did not select the first enabled item.");
                    Throws<NotSupportedException>(() => choice.SetWidth(100));
                    Check(choice.WidthConstraints is null && host.IsAttached,
                        "An unqualified choice-axis request changed the model or detached it.");
                    range.Value = 20;
                    Check(nativeRange.Value == 20 && changes == 0, "Programmatic range assignment emitted an authored change.");
                    nativeRange.Focus();
                    SendMessage(GetFocus(), 0x0100, 0x27, 1);
                    SendMessage(GetFocus(), 0x0101, 0x27, 1);
                });
                ui.Wait(() => range.Value == 25 && changes == 1);
                ui.Ui(() =>
                {
                    Check(nativeRange.Value == 25, "Native keyboard range commit disagreed with the portable model.");
                    nint slider = GetFocus();
                    Check(GetClientRect(slider, out var bounds), "Native range has no client geometry.");
                    int point = (((bounds.Bottom - bounds.Top) / 2) << 16) | ((bounds.Right - bounds.Left) * 3 / 4);
                    SendMessage(slider, 0x0201, 1, point);
                    SendMessage(slider, 0x0200, 1, point);
                });
                ui.Wait(() => previews > 0 && range.PreviewValue != range.Value);
                ui.Ui(() =>
                {
                    Check(nativeRange.Value == range.Value && range.Value == 25,
                        "Native range preview incorrectly committed its value.");
                    SendMessage(GetFocus(), 0x001f, 0, 0);
                });
                ui.Wait(() => cancels == 1 && range.PreviewValue == range.Value);
                ui.Ui(() =>
                {
                    nint slider = GetFocus();
                    GetClientRect(slider, out var bounds);
                    int point = (((bounds.Bottom - bounds.Top) / 2) << 16) | ((bounds.Right - bounds.Left) / 2);
                    SendMessage(slider, 0x0201, 1, point);
                    SendMessage(slider, 0x0200, 1, point);
                });
                ui.Wait(() => range.PreviewValue != range.Value);
                ui.Ui(() =>
                {
                    int before = cancels;
                    range.Visible = false;
                    range.Visible = true;
                    Check(range.PreviewValue == range.Value && nativeRange.Value == range.Value && cancels == before,
                        "Coalesced programmatic availability changes did not silently clear the native preview.");
                    range.SetRange(new(-10, 30, 0.25, 2));
                    range.SetValue(2.5);
                    Check(nativeRange.Range.Minimum == -10 && nativeRange.Value == 2.5,
                        "Range updates used percentages instead of native range units.");
                    host.Detach();
                    Check(HandleCount(window) == baseline, "Choice/range teardown leaked native content.");
                });
            }
            finally { ui.Ui(window.Close); }
        });
        RunNativeWork(application, work);
    }
}
