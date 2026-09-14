using Xui;
using System.Runtime.CompilerServices;

internal static class FeatureTests
{
    private static int assertions;
    private static void Expect(bool value) { if (!value) throw new Exception("Feature assertion failed."); ++assertions; }
    private static void Fails(Action action)
    {
        try { action(); } catch (XuiException) { ++assertions; return; }
        throw new Exception("Expected XUI feature failure.");
    }
    private sealed class MillionSource(ulong count = 1000000) : IReadOnlyImmutableSource
    {
        public int Calls, Rows;
        public bool Fail;
        public ulong Count => count;
        public ItemKey Key(ulong index) { ++Calls; if (Fail) throw new Exception("Source callback sentinel"); return new(index + 1, 7); }
        public ulong? Find(ItemKey key) { ++Calls; return key.Version == 7 && key.Id > 0 && key.Id <= count ? key.Id - 1 : null; }
        public ItemContent Item(ulong index, ulong column = 0) { ++Calls; ++Rows; return new($"Row {index}", "日本語 😀"); }
        public bool HasChildren(ItemKey key) => key.Id == 1;
    }
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static WeakReference SourceLifetime()
    {
        using var w = new Window();
        var data = new MillionSource();
        w.ItemsView("Pin").SetSource(w.ImmutableSource(data));
        return new(data);
    }
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static WeakReference AttachedSource(Window window, ItemsView items)
    {
        var data = new MillionSource();
        using (var source = window.ImmutableSource(data)) items.SetSource(source);
        return new(data);
    }
    internal static void Run()
    {
        FluentSetters();
        using (var w = new Window(customTitlebar: true))
        {
            var range = w.RangeInput("Range"); range.Range = new(-10, 10, .5, 2); range.Value = 2.5; Expect(range.Value == 2.5);
            range.Orientation = Axis.Vertical; range.Reversed = true; range.Help("Fine adjustment"); range.TooltipDelay(500);
            int changed = 0; range.Event += _ => ++changed;
            GC.Collect(); GC.WaitForPendingFinalizers(); GC.Collect();
            range.ChangeValue(3); Expect(changed == 1);
            Fails(() => range.Value = double.NaN); Expect(range.Value == 3);
            Task.Run(() => Fails(() => range.Value = 1)).GetAwaiter().GetResult();
            Choice[] choices = [new(1, "First"), new(2, "Second 😀")];
            var radio = w.RadioGroup("Radio"); radio.SetItems(choices, 1); radio.Select(2);
            var combo = w.ComboBox("Combo", true); combo.SetItems(choices, 1); combo.Select(2);
            var number = w.NumericInput("Number"); number.Range = new(0, 20); number.Value = 2; number.Step(true); Expect(number.Value == 3);
            var progress = w.Progress("Progress"); progress.Range = new(0, 100); progress.Value = 40; progress.State = ProgressState.Paused; Expect(progress.State == ProgressState.Paused);
            var expander = w.Expander("More", w.Stack()); expander.Expanded = false; Expect(!expander.Expanded);
            _ = w.Popup("Popup", w.Stack());
            var split = w.SplitButton("Run"); int primary = 0, secondary = 0; split.Primary.Click += () => ++primary;
            split.Secondary.Click += () => ++secondary; split.Primary.Invoke(); split.Secondary.Invoke(); Expect(primary == 1 && secondary == 1);
            split.Primary.Click += () => ++primary; split.Primary.Invoke(); Expect(primary == 3);
            var data = new MillionSource(); var source = w.ImmutableSource(data);
            var items = w.ItemsView("Million rows"); items.SetSource(source); items.SelectAll();
            Expect(items.Selection.StorageTerms == 1 && items.Contains(new(999999, 7)));
            items.Select(new(999999, 7)); Expect(items.Selection.Focused == new ItemKey(999999, 7)); items.Presentation = ItemsPresentation.Tiles;
            items.ItemSize(180, 60); Expect(data.Calls < 100 && data.Rows < 100);
            var tree = w.TreeView("Tree"); tree.SetSource(source); TreeRequest? pending = null;
            tree.OnRequest(r => pending = r); tree.Expand(new(1, 7)); Expect(pending is not null);
            Expect(pending!.Node == new ItemKey(1,7)); pending.Complete(w.ImmutableSource(new MillionSource(0))); pending.Dispose();
            var grid = w.Grid("Grid"); grid.SetTracks([new GridTrack(TrackSizing.Star, 1)], [new GridTrack(TrackSizing.Star, 1)]);
            grid.Add(w.Label("Cell"));
            var wrap = w.Wrap("Wrap"); wrap.ItemWidth = 100; wrap.Add(w.Label("Tile"));
            var adaptive = w.AdaptiveLayout("Adaptive", w.Stack(), w.Stack()); adaptive.Breakpoint = 500; adaptive.NavigationExtent = 180;
            adaptive.NavigationOpen = true; adaptive.CompactNavigation = CompactNavigation.Overlay;
            var command = new Command(1, "Apply", PinLabel: "Pin", ShortcutHint: "Ctrl+K");
            var bar = w.CommandBar("Commands"); bar.SetCommands([command]); bar.Bind(1, 'K', KeyModifiers.Control);
            int actions = 0; bar.Event += e => { if (e.Kind == EventKind.Action) ++actions; }; bar.Invoke(1, true); Expect(actions == 1);
            var surface = w.CommandSurface("Palette"); surface.SetCommands([command]);
            w.Breadcrumb("Path").SetSegments(choices);
            var pane = w.NavigationPane("Navigation"); pane.SetSource(source); pane.Items.Select(new(2, 7));
            var location = w.LocationPicker("Choose"); location.Editor.Text = "Local"; location.Navigation.SetSource(source);
            var view = w.ViewPicker("View", items); view.Size.Value = 64;
            var doc = w.MultilineText("Document"); doc.Text = "A😀Z"; doc.Selection = new(1, 3); Expect(doc.Text == "A😀Z" && doc.Selection.End == 3);
            Fails(() => doc.Selection = new(1, 2)); doc.ReadOnly = true; Expect(doc.ReadOnly);
            var rich = w.RichText("Rich"); rich.SetRuns([new("Bold", Bold: true), new(" plain")]); Expect(rich.Text == "Bold plain");
            var password = w.PasswordInput("Secret"); password.MaximumLength = 32; password.SetPassword("safe".AsSpan());
            password.WithPassword(bytes => Expect(bytes.SequenceEqual("safe"u8)));
            Expect(password.Length == 4);
            var submit = w.Button("Submit secret");
            submit.Click += () => password.WithPassword(bytes => Expect(bytes.SequenceEqual("safe"u8)));
            submit.Invoke();
            var date = w.DateTimePicker("Date", DateTimePresentation.Calendar); date.Value = new DateTime(2028, 2, 29, 12, 34, 56); Expect(date.Value.Day == 29);
            var status = w.InlineStatus("Status"); status.Dismissible = true; status.SetMessage("Done", StatusSeverity.Success); status.Dismiss(); status.Show();
            var color = w.ColorPicker("Color"); color.Value = new(1, 2, 3, 4); Expect(color.Value == new RgbaColor(1, 2, 3, 4));
            var dialog = w.ContentDialog("Dialog", w.Stack()); dialog.SetValidationMessage("Required"); dialog.SetValidationMessage("");
            var canvas = w.VectorCanvas("Scene"); canvas.SetScene([new VectorShape(1, [new(0,0),new(40,0),new(40,40)], "Triangle", true, true)]);
            var map = w.MapView("Map"); map.SetView(new(47, -122), 3); map.SetMarkers([new(1, new(47,-122), "Here")]); Expect(map.View.Zoom == 3);
            using var stale = map.RequestOverlay(); using var current = map.RequestOverlay(); Fails(() => stale.Complete([])); current.Complete([]);
            var media = w.MediaPlayback("Media"); Expect(media.State == HostState.Idle); media.Volume = .25; media.Unload();
            var web = w.WebContent("Web"); Expect(web.State == HostState.Idle); web.SetAllowedOrigins(["https://example.invalid"]);
            using (var evaluation = web.Evaluate("1+1")) Expect(evaluation.TryGetResult()?.IsError == true);
            using (var evaluation = web.Evaluate("1+1")) { web.Stop(); Fails(() => evaluation.TryGetResult()); }
            w.TabStrip("Tabs").SetTabs(choices, 1);
            w.SplitView("Split", w.Stack(), w.Stack()).Ratio = .4;
            var pages = w.PageView("Pages"); pages.Add(w.Stack()); pages.SelectedPage = 0;
            var table = w.DataGrid("Data"); table.SetColumns([new("Name"), new("Value", Numeric: true)]); table.SetSource(source);
            table.SetColumnWidth(1, 160); table.SetColumnOrder([1,0]); table.Select(new(1,7)); table.SelectAll();
            w.HistoryChart("History").Append(5);
            Expect(data.Calls < 1000 && data.Rows < 1000);
            w.Dispose();
            try { range.Value = 2; throw new Exception("Expected disposed element."); } catch (ObjectDisposedException) { ++assertions; }
        }
        using (var w = new Window())
        {
            var source = new MillionSource { Fail = true };
            Fails(() => w.ItemsView("Failure").SetSource(w.ImmutableSource(source)));
            Expect(w.CallbackStatus != 0);
        }
        using (var w = new Window())
        {
            var range = w.RangeInput("Failure"); range.Event += _ => throw new Exception("Range callback sentinel");
            Fails(() => range.ChangeValue(1)); Expect(w.CallbackStatus == 8);
        }
        var weak = SourceLifetime(); GC.Collect(); GC.WaitForPendingFinalizers(); GC.Collect(); Expect(!weak.IsAlive);
        using (var w = new Window())
        {
            var items = w.ItemsView("Source replacement");
            var attached = AttachedSource(w, items); GC.Collect(); GC.WaitForPendingFinalizers(); GC.Collect(); Expect(attached.IsAlive);
            using (var empty = w.ImmutableSource(new MillionSource(0))) items.SetSource(empty);
            GC.Collect(); GC.WaitForPendingFinalizers(); GC.Collect(); Expect(!attached.IsAlive);
        }
        Console.WriteLine($"C# feature assertions: {assertions}; all 35 feature constructors; bounded million-row source.");
    }
    private static void FluentSetters()
    {
        using var w = new Window();
        var map = w.MapView("Offline map")
                        .SetView(new(47.6,-122.3),4)
                        .SetMarkers([new(1,new(47.6,-122.3),"Seattle")])
                        .FixedSize(650,180);
        Expect(map.View == (new GeoPoint(47.6, -122.3), 4));
        Expect(ReferenceEquals(map, map.SetView(new(47, -122), 3)));
        Expect(ReferenceEquals(map, map.SetMarkers([])));
        Expect(ReferenceEquals(map, map.FixedSize(650, 180)));
        Expect(ReferenceEquals(map, map.MinimumSize(10, 10)));
        Expect(ReferenceEquals(map, map.MaximumSize(900, 900)));
        Expect(ReferenceEquals(map, map.PreferredSize(650, 180)));
        Expect(ReferenceEquals(map, map.AutoSize(false)));
        var resized = map.FixedSize(600, 160).Help("Map").TooltipDelay(500).Visible(true)
            .SetName("Renamed map").SetAutomationId("fluent-map").SetEnabled(true)
            .SetView(new(48, -123), 5).Pan(0, 0);
        Expect(ReferenceEquals(map, resized) && resized.View.Zoom == 5);
        Element element = map;
        Control control = map;
        Expect(ReferenceEquals(map, element.FixedSize(650, 180)));
        Expect(ReferenceEquals(map, control.Help("Map")));
        Fails(() => map.SetView(new(48, -123), double.NaN).SetView(new(0, 0), 1));
        Fails(() => map.FixedSize(float.NaN, 180).SetView(new(0, 0), 1));
        Expect(map.View.Zoom == 5);
        try { map.SetMarkers(new MapMarker[257]).SetView(new(0, 0), 1); throw new Exception("Expected marker limit."); }
        catch (ArgumentOutOfRangeException) { ++assertions; }
        Expect(map.View.Zoom == 5);
        Task.Run(() => Fails(() => map.FixedSize(100, 100))).GetAwaiter().GetResult();

        var range = w.RangeInput("Range").FixedSize(120, 24).SetRange(new(0, 100))
            .SetValue(20).SetOrientation(Axis.Horizontal).SetReversed(false);
        Expect(range.Value == 20);
        Expect(ReferenceEquals(range, range.SetRange(new(0, 50))));
        Expect(ReferenceEquals(range, range.SetValue(25)));
        Fails(() => range.SetValue(double.NaN));
        Expect(range.Value == 25);
        var combo = w.ComboBox("Choice").FixedSize(120, 24).SetItems([new(1, "One")], 1);
        Expect(ReferenceEquals(combo, combo.SetItems([new(2, "Two")], 2).Select(2)));
        var grid = w.Grid("Grid").FixedSize(120, 100).SetTracks([new GridTrack()], [new GridTrack()]);
        Expect(ReferenceEquals(grid, grid.Add(w.Label("Cell"))));

        var doc = w.MultilineText("Document").SetText("A😀Z").SetSelection(new(1, 3)).FixedSize(120, 60);
        Expect(doc.Text == "A😀Z" && doc.Selection == new TextSelection(1, 3));
        Expect(ReferenceEquals(doc, doc.SetDocument("Document")));
        Control baseDoc = doc;
        Expect(ReferenceEquals(doc, baseDoc.SetText("Base document")));
        Expect(doc.Text == "Base document");
        var rich = w.RichText("Rich").SetText("Rich document").SetSelection(new(0, 4));
        Expect(rich.Text == "Rich document" && rich.Selection.End == 4);
        Control baseRich = rich;
        Expect(ReferenceEquals(rich, baseRich.SetText("Base rich")));
        Expect(rich.Text == "Base rich");
        Expect(ReferenceEquals(rich, rich.SetRuns([new("Bold", Bold: true)])));
        Expect(rich.Text == "Bold");
        var password = w.PasswordInput("Secret").FixedSize(120, 24).SetMaximumLength(32).SetPassword("secret");
        Expect(ReferenceEquals(password, password.SetPassword("safe")));
        password.WithPassword(bytes => Expect(bytes.SequenceEqual("safe"u8)));
        var color = w.ColorPicker("Color").SetValue(new(1, 2, 3));
        Expect(ReferenceEquals(color, color.SetValue(new(4, 5, 6))) && color.Value == new RgbaColor(4, 5, 6));
        var date = w.DateTimePicker("Date").SetValue(new DateTime(2028, 2, 29));
        Expect(date.Value.Day == 29 && ReferenceEquals(date, date.SetValue(new DateTime(2028, 3, 1))));

        w.Dispose();
        try { map.FixedSize(100, 100); throw new Exception("Expected disposed element."); }
        catch (ObjectDisposedException) { ++assertions; }
        try { range.SetValue(1); throw new Exception("Expected disposed element."); }
        catch (ObjectDisposedException) { ++assertions; }
    }
}
