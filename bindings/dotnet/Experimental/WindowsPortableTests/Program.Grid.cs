using Xui;
using Xui.Experimental.Windows;
using P = Xui.Experimental.Portable;

internal static partial class Program
{
    private static void SharedGridSizingScenarios()
    {
        using var application = new Application();
        using var window = application.CreateWindow("Portable native scenarios", 620, 960, visualStyle: VisualStyle.WinUI);
        var surface = Surface(window);
        uint baseline = HandleCount(window);
        using var dispatcher = new WindowsDispatcher(window);
        using var host = new P.Host(dispatcher);
        var sample = new PortableLayout.GridSizingShowcase(host);
        var backend = new WindowsBackend(surface, dispatcher);
        host.Attach(backend);
        application.Show(window);
        var driver = new Driver(application, window, host, () => backend);
        var work = Task.Run(() =>
        {
            try
            {
                driver.Ui(() =>
                {
                    var sidebar = driver.Native("grid-sidebar").GetBounds();
                    var input = driver.Input("grid-input").GetBounds();
                    Check(Math.Abs(sidebar.Width - 60) < 0.1f && Math.Abs(input.X - sidebar.X - 60) < 0.1f &&
                        sidebar.Height >= input.Height,
                        "The shared Grid's fixed sidebar, column span, or row span was not arranged correctly.");
                    var first = driver.Native("grid-natural-first").GetBounds();
                    var second = driver.Native("grid-natural-second").GetBounds();
                    Check(Math.Abs(first.Height - 40) < 0.1f && Math.Abs(second.Height - 80) < 0.1f &&
                        Math.Abs(second.Y - first.Y - 40) < 0.1f,
                        $"Star rows in an unbounded scroll did not retain natural slots: first={first}, second={second}.");
                    Check(!((ScrollView)driver.Native("unbounded-grid-scroll")).FillViewport,
                        "The shared natural Grid did not opt out of viewport filling.");
                    var fixedFirst = driver.Native("fixed-scope-first").GetBounds();
                    var fixedSecond = driver.Native("fixed-scope-second").GetBounds();
                    Check(Math.Abs(fixedFirst.Height - 48) < 0.1f && Math.Abs(fixedSecond.Height - 144) < 0.1f &&
                        Math.Abs(fixedSecond.Y - fixedFirst.Y - 56) < 0.1f,
                        "An all-fixed Grid cell did not establish a bounded flex allocation.");
                    var mixedFirst = driver.Native("mixed-scope-first").GetBounds();
                    var mixedSecond = driver.Native("mixed-scope-second").GetBounds();
                    Check(Math.Abs(mixedFirst.Height - 32) < 0.1f && Math.Abs(mixedSecond.Height - 48) < 0.1f &&
                        Math.Abs(mixedSecond.Y - mixedFirst.Y - 40) < 0.1f,
                        "A mixed fixed/automatic Grid span did not retain its unbounded child layout context.");
                });
                driver.Change("grid-input", "Retained grid draft");
                driver.Click("grid-compact");
                driver.Wait(() => Math.Abs(driver.Input("grid-input").GetBounds().Width - 260) < 0.1f);
                driver.Ui(() =>
                {
                    Check(driver.Native("grid-summary").Text == "Draft: Retained grid draft" &&
                        Math.Abs(driver.Native("grid-sidebar").GetBounds().Width - 40) < 0.1f,
                        "State-driven Grid tracks or text bindings were not updated.");
                });
                uint withoutNote = driver.Ui(() => HandleCount(window));
                nint edit = driver.Ui(() =>
                {
                    Check(host.TryFocus(sample.Input), "The shared Grid input could not receive portable focus.");
                    host.SetSelection(sample.Input, new(2, 6));
                    return GetFocus();
                });
                for (int i = 0; i < 6; i++)
                {
                    driver.Ui(() =>
                    {
                        sample.Notes = [P.KeyedItem.Create("note", h => new PortableMutation.MutationBanner(h, "grid-note"))];
                        Check(backend.FindControls("grid-note").Count == 1 && GetFocus() == edit &&
                            host.GetSelection(sample.Input) == new P.TextSelection(2, 6),
                            "Adding owned content inside a Grid replaced its surviving editor.");
                        sample.Notes = [];
                        Check(backend.FindControls("grid-note").Count == 0 && HandleCount(window) == withoutNote,
                            "Removing owned Grid content leaked native handles.");
                    });
                }
                driver.Ui(() =>
                {
                    Check(sample.Draft == "Retained grid draft" && host.HasFocus(sample.Input),
                        "Grid row changes lost retained application text or focus.");
                    host.Detach();
                    Check(HandleCount(window) == baseline && !IsWindow(edit), "Shared Grid teardown leaked native content.");
                });
            }
            finally { driver.Ui(window.Close); }
        });
        try { application.Run(); }
        finally { work.WaitAsync(Timeout).GetAwaiter().GetResult(); }
    }

    private static void PortableGridScenarios()
    {
        using var application = new Application();
        using var window = application.CreateWindow("Portable static Grid", 620, 960, visualStyle: VisualStyle.WinUI);
        var surface = Surface(window);
        uint baseline = HandleCount(window);
        using var dispatcher = new WindowsDispatcher(window);
        using var host = new P.Host(dispatcher);
        P.Grid grid;
        P.TextInput input;
        using (var build = host.BeginBuild())
        {
            var root = host.Stack(P.Axis.Vertical);
            grid = host.Grid("Native grid contract");
            grid.SetWidth(420);
            grid.SetTracks([new(P.TrackSizing.Automatic), new(P.TrackSizing.Automatic)],
                [new(P.TrackSizing.Fixed, 100), new(P.TrackSizing.Star)]);
            var label = host.Label("Fixed column");
            label.AutomationId = "grid-fixed";
            input = host.TextInput("Retained grid editor");
            input.AutomationId = "grid-input";
            input.Text = "Grid input identity";
            input.SetHeight(P.AxisConstraints.Auto);
            var spanning = host.Button("Spanning second row");
            spanning.AutomationId = "grid-span";
            grid.Add(label).Add(input, column: 1).Add(spanning, row: 1, columnSpan: 2);
            root.Add(grid);
            host.SetContent(root);
            build.Complete();
        }
        var backend = new WindowsBackend(surface, dispatcher);
        Check(backend.SupportsGrid == Grid.SupportsPortableLayout, "The adapter advertised an unqualified native Grid version.");
        if (!backend.SupportsGrid)
        {
            Throws<NotSupportedException>(() => host.Attach(backend));
            Check(!host.IsAttached && HandleCount(window) == baseline && grid.Children.Count == 3,
                "Unsupported Grid creation changed the retained tree or leaked native handles.");
            return;
        }
        host.Attach(backend);
        application.Show(window);
        var ui = new NativeUi(application);
        var work = Task.Run(() =>
        {
            try
            {
                nint edit = 0;
                float inputX = 0;
                int changes = 0;
                var nativeInput = ui.Ui(() => (TextInput)backend.FindControls("grid-input").Single());
                ui.Ui(() =>
                {
                    var first = backend.FindControls("grid-fixed").Single().GetBounds();
                    var field = nativeInput.GetBounds();
                    var spanning = backend.FindControls("grid-span").Single().GetBounds();
                    Check(Math.Abs(first.Width - 100) < 0.1f && Math.Abs(field.Width - 320) < 0.1f &&
                        Math.Abs(field.X - first.X - 100) < 0.1f && Math.Abs(spanning.Width - 420) < 0.1f &&
                        spanning.Y >= field.Y + field.Height - 0.1f,
                        "Native Grid fixed/star columns or two-column span have incorrect geometry.");
                    nativeInput.Focus();
                    nativeInput.Selection = new(2, 6);
                    edit = GetFocus();
                    inputX = field.X;
                    input.Changed += _ => changes++;
                    grid.SetTracks(grid.Rows.ToArray(), [new(P.TrackSizing.Fixed, 140), new(P.TrackSizing.Star)]);
                });
                ui.Wait(() => Math.Abs(nativeInput.GetBounds().X - inputX - 40) < 0.1f);
                ui.Ui(() =>
                {
                    Check(Math.Abs(nativeInput.GetBounds().Width - 280) < 0.1f &&
                        GetFocus() == edit && nativeInput.Selection == new TextSelection(2, 6) && changes == 0,
                        "Changing native Grid tracks recreated or edited the input.");
                    var rows = grid.Rows.ToArray();
                    var columns = grid.Columns.ToArray();
                    Throws<ArgumentException>(() => grid.SetTracks([new(P.TrackSizing.Automatic)], columns));
                    Check(grid.Rows.SequenceEqual(rows) && grid.Columns.SequenceEqual(columns) && host.IsAttached,
                        "Invalid track replacement changed native or retained Grid state.");
                    grid.SetWidth(300);
                });
                ui.Wait(() => Math.Abs(nativeInput.GetBounds().Width - 160) < 0.1f);
                ui.Ui(() =>
                {
                    Check(GetFocus() == edit && nativeInput.Selection == new TextSelection(2, 6),
                        "Grid axis constraints lost native editing identity.");
                    host.Detach();
                    Check(HandleCount(window) == baseline && !IsWindow(edit), "Static Grid detach leaked its arena.");
                    backend = new WindowsBackend(surface, dispatcher);
                    host.Attach(backend);
                    Check(((TextInput)backend.FindControls("grid-input").Single()).Text == "Grid input identity",
                        "Reattaching a Grid lost its retained text.");
                    host.Detach();
                    Check(HandleCount(window) == baseline, "Repeated Grid attachment leaked native handles.");
                });
            }
            finally { ui.Ui(window.Close); }
        });
        try { application.Run(); }
        finally { work.WaitAsync(Timeout).GetAwaiter().GetResult(); }
    }
}
