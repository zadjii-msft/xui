using Xui;
using Xui.Experimental.Windows;
using P = Xui.Experimental.Portable;

internal static partial class Program
{
    private static void NativeVisibilitySpacing()
    {
        using var application = new Application();
        using var window = application.CreateWindow("Native visibility spacing", 620, 700, visualStyle: VisualStyle.WinUI);
        var surface = Surface(window);
        uint baseline = HandleCount(window);
        using var dispatcher = new WindowsDispatcher(window);
        using var host = new P.Host(dispatcher);
        P.Label horizontalGap, verticalGap;
        using (var build = host.BeginBuild())
        {
            P.Label Label(string id, float width, float height)
            {
                var label = host.Label(id);
                label.AutomationId = id;
                P.ElementExtensions.FixedSize(label, width, height);
                return label;
            }
            var horizontal = host.Stack(P.Axis.Horizontal).Spacing(10);
            horizontalGap = Label("horizontal-zero", 0, 0);
            horizontal.Add(Label("horizontal-first", 50, 20)).Add(horizontalGap).Add(Label("horizontal-last", 50, 20));
            var vertical = host.Stack(P.Axis.Vertical).Spacing(10);
            verticalGap = Label("vertical-zero", 0, 0);
            vertical.Add(Label("vertical-first", 100, 20)).Add(verticalGap).Add(Label("vertical-last", 100, 20));
            var flex = host.Stack(P.Axis.Horizontal).Spacing(10);
            P.ElementExtensions.FixedSize(flex, 200, 40);
            var hidden = host.Label("hidden flex");
            hidden.AutomationId = "hidden-flex";
            hidden.Visible = false;
            var first = host.Label("flex first"); first.AutomationId = "visible-flex-first";
            var last = host.Label("flex last"); last.AutomationId = "visible-flex-last";
            flex.Add(first, 1).Add(hidden, 98).Add(last, 1);
            host.SetContent(host.Stack(P.Axis.Vertical).Spacing(12).Add(horizontal).Add(vertical).Add(flex));
            build.Complete();
        }
        var backend = new WindowsBackend(surface, dispatcher);
        host.Attach(backend);
        application.Show(window);
        var ui = new NativeUi(application);
        ElementBounds Bounds(string id) => backend.FindControls(id).Single().GetBounds();
        var work = Task.Run(() =>
        {
            try
            {
                uint attached = ui.Ui(() => HandleCount(window));
                ui.Ui(() =>
                {
                    Check(Math.Abs(Bounds("horizontal-last").X - Bounds("horizontal-first").X - 70) < 0.1f &&
                        Math.Abs(Bounds("vertical-last").Y - Bounds("vertical-first").Y - 40) < 0.1f,
                        "A visible zero-size child was incorrectly treated as hidden.");
                    var first = Bounds("visible-flex-first");
                    var last = Bounds("visible-flex-last");
                    Check(Math.Abs(first.Width - 95) < 0.1f && Math.Abs(last.Width - 95) < 0.1f &&
                        Math.Abs(last.X - first.X - 105) < 0.1f,
                        "A hidden child participated in flex weight or spacing allocation.");
                    horizontalGap.Visible = false;
                    verticalGap.Visible = false;
                });
                ui.Wait(() => Math.Abs(Bounds("horizontal-last").X - Bounds("horizontal-first").X - 60) < 0.1f &&
                    Math.Abs(Bounds("vertical-last").Y - Bounds("vertical-first").Y - 30) < 0.1f,
                    () => $"Hidden controls retained spacing: horizontal={Bounds("horizontal-last").X - Bounds("horizontal-first").X}, vertical={Bounds("vertical-last").Y - Bounds("vertical-first").Y}.");
                ui.Ui(() =>
                {
                    Check(Bounds("horizontal-zero").Width == 0 && Bounds("horizontal-zero").Height == 0 &&
                        Bounds("vertical-zero").Width == 0 && Bounds("vertical-zero").Height == 0,
                        "Hidden controls retained arranged geometry.");
                    horizontalGap.Visible = true;
                    verticalGap.Visible = true;
                });
                ui.Wait(() => Math.Abs(Bounds("horizontal-last").X - Bounds("horizontal-first").X - 70) < 0.1f &&
                    Math.Abs(Bounds("vertical-last").Y - Bounds("vertical-first").Y - 40) < 0.1f);
                ui.Ui(() =>
                {
                    Check(HandleCount(window) == attached, "Visibility layout replaced native model handles.");
                    host.Detach();
                    Check(HandleCount(window) == baseline, "Visibility fixture leaked native content.");
                });
            }
            finally { ui.Ui(window.Close); }
        });
        try { application.Run(); }
        finally { work.WaitAsync(Timeout).GetAwaiter().GetResult(); }
    }
}
