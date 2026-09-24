using Xui;
using Xui.Experimental.Windows;
using P = Xui.Experimental.Portable;

internal static partial class Program
{
    private static void NativeLabelLayoutScenarios()
    {
        using var application = new Application();
        using var window = application.CreateWindow("Native label layout", 480, 640, visualStyle: VisualStyle.WinUI);
        var surface = Surface(window);
        uint baseline = HandleCount(window);
        using var dispatcher = new WindowsDispatcher(window);
        using var host = new P.Host(dispatcher);
        P.Label label;
        P.TextInput input;
        using (var build = host.BeginBuild())
        {
            label = host.Label("A long native label contains words that wrap without truncating the model or its accessible text.");
            label.AutomationId = "layout-label";
            label.SetWidth(180);
            input = host.TextInput("Retained neighbor");
            input.AutomationId = "layout-input";
            input.Text = "A stable editor";
            host.SetContent(host.Stack(P.Axis.Vertical).Spacing(8).Add(label).Add(input));
            build.Complete();
        }
        var backend = new WindowsBackend(surface, dispatcher);
        host.Attach(backend);
        var native = (Label)backend.FindControls("layout-label").Single();
        native.SetTextLayout(new LabelLayout(true, 2));
        application.Show(window);
        var ui = new NativeUi(application);
        var work = Task.Run(() =>
        {
            try
            {
                float originalHeight = ui.Ui(() => native.GetBounds().Height);
                nint editor = ui.Ui(() =>
                {
                    Check(host.TryFocus(input), "The native label fixture editor could not receive focus.");
                    host.SetSelection(input, new(2, 6));
                    label.TextLayout = P.LabelTextLayout.SingleLine(P.TextOverflow.Clip);
                    Check(native.GetTextLayout() == new LabelLayout(false, 1, LabelOverflow.Clip),
                        "Explicit native single-line clipping did not round-trip.");
                    return GetFocus();
                });
                ui.Wait(() => native.GetBounds().Height < originalHeight);
                float singleHeight = ui.Ui(() => native.GetBounds().Height);
                ui.Ui(() => label.TextLayout = P.LabelTextLayout.Wrap(2));
                ui.Wait(() => native.GetBounds().Height > singleHeight);
                float cappedHeight = ui.Ui(() => native.GetBounds().Height);
                ui.Ui(() =>
                {
                    Check(native.GetTextLayout() == new LabelLayout(true, 2), "Native wrapping did not preserve its two-line cap.");
                    label.TextLayout = P.LabelTextLayout.Wrap();
                });
                ui.Wait(() => native.GetBounds().Height > cappedHeight);
                ui.Ui(() =>
                {
                    Check(native.Text == label.Text, "Native wrapping rewrote the accessible/model string.");
                    label.TextLayout = null;
                    Check(native.GetTextLayout() == new LabelLayout(true, 2),
                        "Clearing the portable layout discarded a preexisting native label overlay.");
                    native.SetTextLayout(null);
                    label.TextLayout = P.LabelTextLayout.SingleLine(P.TextOverflow.CharacterEllipsis);
                    string old = label.Text;
                    Throws<ArgumentException>(() => label.Text = "line\nbreak");
                    Check(label.Text == old && native.Text == old, "Rejected single-line text changed native or model content.");
                    label.TextLayout = null;
                    Check(native.GetTextLayout() is null, "A second layout ownership interval restored an obsolete baseline.");
                    Check(GetFocus() == editor && host.GetSelection(input) == new P.TextSelection(2, 6) &&
                        input.Text == "A stable editor", "Native label layout edited or replaced its neighboring editor.");
                    host.Detach();
                    Check(HandleCount(window) == baseline && !IsWindow(editor), "Native label cleanup leaked scoped content.");
                });
            }
            finally { ui.Ui(window.Close); }
        });
        RunNativeWork(application, work);
    }
}
