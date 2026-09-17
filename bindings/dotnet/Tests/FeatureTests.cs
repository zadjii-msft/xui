using Xui;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

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
    internal static void NavigationStyleBridges()
    {
        using var w = new Window(customTitlebar: true);
        var navigation = w.NavigationView("Navigation");
        var pane = w.NavigationPane("Query");
        (Element Child, StyleTarget Target)[] children = [
            (w.Titlebar, StyleTarget.TitleBar), (w.TitlebarTitle, StyleTarget.Label),
            (w.TitlebarMinimize, StyleTarget.Button), (w.TitlebarMaximize, StyleTarget.Button),
            (w.TitlebarClose, StyleTarget.Button), (navigation.Search, StyleTarget.TextInput),
            (navigation.ToggleButton, StyleTarget.Button), (navigation.Items, StyleTarget.NavigationList),
            (navigation.HeaderItems, StyleTarget.NavigationList), (navigation.FooterItems, StyleTarget.NavigationList),
            (navigation.Title, StyleTarget.Label), (navigation.EmptyMessage, StyleTarget.Label),
            (pane.Group, StyleTarget.Expander), (pane.Progress, StyleTarget.Progress)
        ];
        foreach (var (child, target) in children)
        {
            child.SetControlStyle(new ControlStyle(target, [new(StylePart.Root, new() { Background = new ThemeColor(0x123456) })]));
            child.SetControlStyleValues(StylePart.Root, new() { Background = new ThemeColor(0x654321) });
            child.SetControlStyle(null);
            Expect(child.GetControlStyleValues(StylePart.Root, true).Background == new ThemeColor(0x654321));
            child.SetControlStyleValues(StylePart.Root, new());
        }
        Expect(ReferenceEquals(w.Titlebar, w.Titlebar) && ReferenceEquals(navigation.Items, navigation.Items));
        int captions = 0;
        Action captionClick = () => ++captions;
        w.TitlebarMinimize.Click += captionClick;
        w.TitlebarMinimize.Click += captionClick;
        w.TitlebarMinimize.Invoke(); Expect(captions == 2);
        w.TitlebarMinimize.Click -= captionClick; w.TitlebarMinimize.Click -= captionClick;
        w.TitlebarMinimize.Invoke(); Expect(captions == 2);
        Expect(ReferenceEquals(pane.Group, pane.Group) && ReferenceEquals(pane.Progress, pane.Progress));
        int toggles = 0;
        Action toggleClick = () => ++toggles;
        navigation.ToggleButton.Click += toggleClick;
        navigation.ToggleButton.Click += toggleClick;
        var expanded = navigation.Expanded;
        navigation.ToggleButton.Invoke();
        Expect(navigation.Expanded != expanded && toggles == 2);
        navigation.ToggleButton.Click -= toggleClick;
        navigation.ToggleButton.Click -= toggleClick;
        navigation.ToggleButton.Invoke();
        Expect(navigation.Expanded == expanded && toggles == 2);

        var breadcrumb = w.Breadcrumb("Path").SetSegments([new(10, "Root", Version: 7), new(20, "Leaf", Version: 9)]);
        var segment = breadcrumb.SegmentButton(new(20, 9));
        Expect(ReferenceEquals(segment, breadcrumb.SegmentButton(new(20, 9))));
        ulong navigated = 0; int segments = 0;
        breadcrumb.Event += e => navigated = e.Value;
        Action segmentClick = () => ++segments;
        segment.Click += segmentClick;
        segment.Invoke(); Expect(navigated == 20 && segments == 1);
        breadcrumb.SetSegments([new(20, "Renamed leaf", Version: 9), new(10, "Root", Version: 7)]);
        segment.Invoke(); Expect(navigated == 20 && segments == 2);
        Expect(ReferenceEquals(segment, breadcrumb.SegmentButton(new(20, 9))));
        segment.Click -= segmentClick;
        navigated = 0; segment.Invoke(); Expect(navigated == 20 && segments == 2);
        Fails(() => breadcrumb.SegmentButton(new(20, 8)));
        _ = breadcrumb.OverflowButton;
        segment.Click += segmentClick;
        breadcrumb.SetSegments([new(20, "Replacement", Version: 10)]);
        navigated = 0;
        Fails(segment.Invoke); Expect(navigated == 0 && segments == 2);
        segment.Click -= segmentClick; segment.Click += segmentClick;
        Fails(segment.Invoke); Expect(navigated == 0 && segments == 2);
        Fails(() => breadcrumb.SegmentButton(new(20, 9)));
        Expect(!ReferenceEquals(segment, breadcrumb.SegmentButton(new(20, 10))));

        var bar = w.CommandBar("Commands").SetCommands([new(42, "Checked", Checked: true)]);
        var button = bar.CommandButton(42);
        _ = bar.OverflowButton;
        button.SetControlStyleValues(StylePart.Root, new() { Background = new ThemeColor(0x123456) });
        int actions = 0, clicks = 0;
        bar.Event += e => { Expect(e.Value == 42); ++actions; };
        Action click = () => ++clicks;
        button.Click += click;
        foreach (var state in new bool?[] { true, null, false, true })
        {
            bar.SetCommands([new(42, "Refreshed", Checked: state)]);
            var previousActions = actions; var previousClicks = clicks;
            button.Invoke();
            Expect(actions == previousActions + 1 && clicks == previousClicks + 1 && button.IsChecked() == (state == true));
            Expect(ReferenceEquals(button, bar.CommandButton(42)));
            button.Click -= click;
            button.Invoke(); Expect(actions == previousActions + 2 && clicks == previousClicks + 1);
            button.Click += click;
            Expect(button.GetControlStyleValues(StylePart.Root, true).Background == new ThemeColor(0x123456));
        }
        button.Click += click;
        var before = clicks;
        bar.SetCommands([new(42, "Momentary")]); button.Invoke(); Expect(clicks == before + 2);
        button.Click -= click; button.Click -= click;
        foreach (var state in new bool?[] { true, null, false })
        {
            bar.SetCommands([new(42, "Unsubscribed", Checked: state)]);
            var previousActions = actions; var previousClicks = clicks;
            button.Invoke();
            Expect(actions == previousActions + 1 && clicks == previousClicks && button.IsChecked() == (state == true));
            Expect(ReferenceEquals(button, bar.CommandButton(42)));
            Expect(button.GetControlStyleValues(StylePart.Root, true).Background == new ThemeColor(0x123456));
        }
        Fails(() => bar.CommandButton(99));
        Task.Run(() => Fails(() => bar.CommandButton(42))).GetAwaiter().GetResult();
        w.SetVisualStyle(VisualStyle.WinUI);
        Expect(ReferenceEquals(button, bar.CommandButton(42)));
        button.Click += click;
        var retiredActions = actions; var retiredClicks = clicks;
        bar.SetCommands([]);
        Fails(button.Invoke); Expect(actions == retiredActions && clicks == retiredClicks);
        bar.SetCommands([new(42, "Disabled replacement", Enabled: false)]);
        var replacement = bar.CommandButton(42);
        Expect(!ReferenceEquals(button, replacement));
        int replacementClicks = 0;
        replacement.Click += () => ++replacementClicks;
        button.Click -= click; button.Click += click;
        Fails(button.Invoke); Fails(replacement.Invoke);
        Expect(actions == retiredActions && clicks == retiredClicks && replacementClicks == 0);
        bar.SetCommands([new(42, "Enabled replacement")]);
        Fails(button.Invoke); replacement.Invoke();
        Expect(actions == retiredActions + 1 && clicks == retiredClicks && replacementClicks == 1);
    }
    private static void MillerContracts()
    {
        using var window = new Window();
        using var other = new Window();
        var columns = window.MillerColumns("Folders");
        var reserved = columns.Column(31);
        reserved.FocusEntered += () => { };
        var data = new MillionSource();
        using var source = window.ImmutableSource(data);
        using var foreign = other.ImmutableSource(data);
        Expect(columns.ColumnCount == 0);
        columns.SetColumns([new("Root", source, new(1, 7)), new("Child", source)]);
        columns.ActiveColumn = 1;
        columns.ColumnWidth = 320;
        Expect(columns.ColumnCount == 2 && columns.ActiveColumn == 1 && columns.ColumnWidth == 320);
        Expect(columns.HorizontalOffset == 0 && columns.MaximumHorizontalOffset == 0);
        Expect(ReferenceEquals(columns, columns.SetHorizontalOffset(0)));
        Fails(() => columns.HorizontalOffset = -1);
        Fails(() => columns.HorizontalOffset = 1);
        Fails(() => columns.HorizontalOffset = double.NaN);
        Fails(() => columns.HorizontalOffset = double.PositiveInfinity);
        Fails(() => columns.ActiveColumn = 2);
        Fails(() => columns.ColumnWidth = double.NaN);
        Fails(() => columns.SetColumns([new("Invalid", source, new(1, 8))]));
        bool differentWindow = false;
        try { columns.SetColumns([new("Foreign", foreign)]); }
        catch (ArgumentException) { differentWindow = true; }
        Expect(differentWindow && columns.ColumnCount == 2);
        var child = columns.Column(0);
        Expect(ReferenceEquals(child, columns.Column(0)));
        MillerItemEvent? selected = null;
        int childSelections = 0;
        child.Event += e => { if (e.Kind == EventKind.Selection) ++childSelections; };
        child.FocusEntered += () => { };
        columns.SelectionChanged += e => selected = e;
        child.Select(new(42, 7));
        Expect(selected == new MillerItemEvent(0, new(42, 7)) && childSelections == 1);
        Expect(columns.ActiveColumn == 0 && child.Selection.Focused == new ItemKey(42, 7));
        Expect(data.Calls < 200 && data.Rows < 100);
        child.OnContextMenu(() => [new(1, "Inspect")], _ => { });
        child.ClearContextMenu();
        Task.Run(() => Fails(() => columns.ActiveColumn = 0)).GetAwaiter().GetResult();
        columns.SetColumns([]);
        Expect(columns.ColumnCount == 0);
        Expect(ReferenceEquals(reserved, columns.Column(31)));
        using var failedWindow = new Window();
        var failing = failedWindow.MillerColumns("Callback failure");
        using var failedSource = failedWindow.ImmutableSource(new MillionSource(2));
        failing.SetColumns([new("Root", failedSource)]);
        failing.SelectionChanged += _ => throw new InvalidOperationException("Miller callback sentinel");
        Fails(() => failing.Column(0).Select(new(1, 7)));
        Expect(failedWindow.CallbackStatus != 0);
    }

    internal static void Run()
    {
        NavigationStyleBridges();
        MillerContracts();
        VisualTests.Run();
        ExplorerPrimitives();
        FluentSetters();
        using (var w = new Window(customTitlebar: true))
        {
            var tabs = w.TitlebarTabs;
            Expect(!tabs.NewTabButtonVisible);
            Expect(ReferenceEquals(tabs.SetNewTabButtonVisible(true), tabs) && tabs.NewTabButtonVisible);
            tabs.NewTabButtonVisible = false;
            Expect(!tabs.NewTabButtonVisible);
            var tabColors = new TabColors(0x123456, 0, 0xffffff, 0x234567, 0xeeeeee, 0x345678, 0x456789);
            Expect(ReferenceEquals(tabs.SetColors(tabColors), tabs) && tabs.Colors == tabColors);
            bool invalidColors = false;
            try { tabs.Colors = tabColors with { Border = 0xff123456 }; }
            catch (ArgumentOutOfRangeException) { invalidColors = true; }
            Expect(invalidColors && tabs.Colors == tabColors);
            tabs.Colors = new(SelectedBackground: 0);
            Expect(tabs.Colors == new TabColors(SelectedBackground: 0));
            tabs.Colors = default;
            Expect(tabs.Colors == default);
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
            Expect(w.ComboBox("Noneditable").Editor is null && combo.Editor is not null);
            Expect(ReferenceEquals(combo.Editor, combo.Editor) && ReferenceEquals(combo.Choices, combo.Choices));
            Expect(ReferenceEquals(number.Editor, number.Editor) && ReferenceEquals(number.IncreaseButton, number.IncreaseButton));
            Expect(ReferenceEquals(color.Channel(0), color.Channel(0)) && ReferenceEquals(color.SwatchButton(2), color.SwatchButton(2)));
            Fails(() => color.SwatchButton(5));
            int channelChanges = 0, stepClicks = 0;
            var red = color.Channel(0);
            red.OnChange(_ => ++channelChanges);
            red.OnChange(_ => ++channelChanges);
            red.IncreaseButton.Click += () => ++stepClicks;
            red.IncreaseButton.Click += () => ++stepClicks;
            red.ChangeValue(21);
            Expect(color.Value == new RgbaColor(21, 2, 3, 4) && channelChanges == 2);
            red.IncreaseButton.Invoke();
            Expect(color.Value == new RgbaColor(22, 2, 3, 4) && channelChanges == 4 && stepClicks == 2);
            int swatchClicks = 0;
            color.SwatchButton(2).Click += () => ++swatchClicks;
            color.SwatchButton(2).Click += () => ++swatchClicks;
            color.SwatchButton(2).Invoke();
            Expect(color.Value == new RgbaColor(220, 45, 45) && swatchClicks == 2);
            var dialog = w.ContentDialog("Dialog", w.Stack()); dialog.SetValidationMessage("Required"); dialog.SetValidationMessage("");
            Expect(ReferenceEquals(dialog.Title, dialog.Title) && ReferenceEquals(dialog.Body, dialog.Body));
            Expect(ReferenceEquals(surface.Menu, surface.Menu) && ReferenceEquals(location.Toolbar, location.Toolbar));
            Element[] retained = [dialog.Title, dialog.Validation, dialog.Body, dialog.Footer,
                surface.Editor, surface.Title, surface.Status, surface.CloseButton, surface.Content, surface.Results, surface.Menu,
                location.Content, location.Footer, location.Toolbar, view.Content, pane.Status, pane.Content,
                combo.Editor!, combo.Popup, combo.Choices, number.Editor, number.DecreaseButton, number.IncreaseButton,
                status.ActionButton, status.DismissButton, color.Channel(0), color.Channel(1), color.Channel(2), color.Channel(3)];
            foreach (var child in retained)
            {
                child.SetControlStyleValues(StylePart.Root, new() { Background = new ThemeColor(0x123456) });
                child.SetControlStyle(null);
                Expect(child.GetControlStyleValues(StylePart.Root, true).Background == new ThemeColor(0x123456));
                child.SetControlStyleValues(StylePart.Root, new());
                Expect(child.GetControlStyleValues(StylePart.Root).Background is null);
            }
            int closeClicks = 0;
            surface.CloseButton.Click += () => ++closeClicks;
            surface.CloseButton.Invoke();
            surface.CloseButton.Click += () => ++closeClicks;
            surface.CloseButton.Invoke();
            Expect(closeClicks == 3);
            var canvas = w.VectorCanvas("Scene"); canvas.SetScene([new VectorShape(1, [new(0,0),new(40,0),new(40,40)], "Triangle", true, true)]);
            var map = w.MapView("Map"); map.SetView(new(47, -122), 3); map.SetMarkers([new(1, new(47,-122), "Here")]); Expect(map.View.Zoom == 3);
            using var stale = map.RequestOverlay(); using var current = map.RequestOverlay(); Fails(() => stale.Complete([])); current.Complete([]);
            var media = w.MediaPlayback("Media"); Expect(media.State == HostState.Idle); media.Volume = .25; media.Unload();
            var web = w.WebContent("Web"); Expect(web.State == HostState.Idle); web.SetAllowedOrigins(["https://example.invalid"]);
            using (var evaluation = web.Evaluate("1+1")) Expect(evaluation.TryGetResult()?.IsError == true);
            using (var evaluation = web.Evaluate("1+1")) { web.Stop(); Fails(() => evaluation.TryGetResult()); }
            w.TabStrip("Tabs").SetTabs(choices, 1);
            var visualTabs = w.TabStrip("Visual tabs");
            int tabSelections = 0;
            visualTabs.Event += e => { if (e.Kind == EventKind.Selection) ++tabSelections; };
            Expect(ReferenceEquals(visualTabs, visualTabs.SetTabItems([
                new(71, "日本語", ButtonIcon.Folder, @"C:\"),
                new(72, "Text only")], 72)));
            Expect(tabSelections == 0);
            Fails(() => visualTabs.SetTabItems([new(71, "Invalid", (ButtonIcon)999)], 71));
            Fails(() => visualTabs.SetTabItems([new(71, "Duplicate"), new(71, "Duplicate")], 71));
            Fails(() => visualTabs.SetTabItems([new(71, "Missing selection")], 99));
            Fails(() => visualTabs.SetTabItems([new(71, "Too long", ButtonIcon.Folder, new string('x', 32768))], 71));
            visualTabs.Select(71);
            Expect(tabSelections == 1);
            Task.Run(() => Fails(() => visualTabs.SetTabItems([]))).GetAwaiter().GetResult();
            visualTabs.SetTabs([new(71, "Legacy choice")], 71);
            visualTabs.SetTabItems([]);
            var splitView = w.SplitView("Split", w.Stack(), w.Stack());
            splitView.Ratio = .4;
            Expect(Math.Abs(splitView.Ratio - .4) < .0001);
            Expect(splitView.SecondVisible);
            splitView.SetSecondVisible(false);
            Expect(!splitView.SecondVisible);
            var pages = w.PageView("Pages"); pages.Add(w.Stack()); pages.SelectedPage = 0;
            var table = w.DataGrid("Data"); table.SetColumns([new("Name"), new("Value", Numeric: true)]); table.SetSource(source);
            table.SetColumnWidth(1, 160); table.SetColumnOrder([1,0]); table.Select(new(1,7)); table.SelectAll();
            w.HistoryChart("History").Append(5);
            Expect(data.Calls < 1000 && data.Rows < 1000);
            w.Dispose();
            try { range.Value = 2; throw new Exception("Expected disposed element."); } catch (ObjectDisposedException) { ++assertions; }
        }
        FeatureLifetimes();
    }
        [DllImport("user32.dll")] private static extern nint GetFocus();
        [DllImport("user32.dll")] private static extern nint SendMessageW(nint window, uint message, nuint wparam, nint lparam);
        [DllImport("user32.dll", EntryPoint = "PostMessageW")] private static extern bool PostMessage(nint window, uint message, nuint key, nint data);
        private static void ExplorerPrimitives()
        {
            using (var w = new Window(customTitlebar: true))
            {
                w.SetTitle("Explorer title");
                Expect(ReferenceEquals(w.TitlebarTabs, w.TitlebarTabs));
                Expect(!w.TitlebarSecondaryTabs.Visible);
                w.TitlebarSecondaryTabs.Visible = true;
                w.TitlebarTabs.SetTabs([new(1, "Left")], 1);
                w.TitlebarSecondaryTabs.SetTabs([new(2, "Right")], 2);
                int clicks = 0; w.TitlebarLeading.Click += () => ++clicks; w.TitlebarLeading.Invoke(); Expect(clicks == 1);
                Expect(w.TitlebarLeading.Icon == ButtonIcon.Navigation);
                var iconButton = w.Button("Back").SetIcon(ButtonIcon.Back);
                Expect(iconButton.Icon == ButtonIcon.Back);
                Fails(() => iconButton.SetIcon((ButtonIcon)99));
                Expect(iconButton.Icon == ButtonIcon.Back);
                Expect((uint)ButtonIcon.Library == 18 && (uint)ButtonIcon.History == 19 &&
                    (uint)ButtonIcon.Bookmark == 20 && (uint)ButtonIcon.Drive == 21 &&
                    (uint)ButtonIcon.Save == 22 && (uint)ButtonIcon.SaveAs == 23 &&
                    (uint)ButtonIcon.Undo == 24 && (uint)ButtonIcon.Redo == 25 &&
                    (uint)ButtonIcon.ChevronUp == 26 && (uint)ButtonIcon.ChevronDown == 27);
                foreach (var icon in new[] { ButtonIcon.History, ButtonIcon.Bookmark, ButtonIcon.Drive,
                    ButtonIcon.Save, ButtonIcon.SaveAs, ButtonIcon.Undo, ButtonIcon.Redo,
                    ButtonIcon.ChevronUp, ButtonIcon.ChevronDown })
                {
                    iconButton.SetIcon(icon);
                    Expect(iconButton.Icon == icon);
                    w.NavigationView($"Icon {icon}").SetItems([new(1, "Section", Selectable: false, Icon: icon)]);
                    w.TabStrip($"Tab {icon}").SetTabItems([new(1, "Document", icon)], 1);
                }
                Fails(() => iconButton.SetIcon((ButtonIcon)28));
                Expect(iconButton.Icon == ButtonIcon.ChevronDown);
                var navigation = w.NavigationView("Navigation");
                navigation.SetItems([new(1, "Group", Selectable: false), new(2, "Home", 1)]);
                ulong selected = 0; navigation.Event += e => { if (e.Kind == EventKind.Selection) selected = e.Value; };
                navigation.Select(2); Expect(selected == 2);
                navigation.SetExpanded(false); Expect(!navigation.Expanded);
                Expect(ReferenceEquals(navigation.Search, navigation.Search));
                Fails(() => navigation.SetItems([new(1, "Bad parent", 2)]));
                int executed = 0;
                Expect(Task.Run(() => w.Post(() => ++executed)).GetAwaiter().GetResult());
                w.Close(); Expect(executed == 0 && !w.Post(() => ++executed));
            }
            using (var w = new Window("Posted callbacks"))
            {
                var anchor = w.TextInput("Anchor");
                var editor = w.TextInput("Popup editor");
                var items = w.ItemsView("Suggestions");
                using var source = w.ImmutableSource(new MillionSource(3));
                items.SetSource(source);
                var grid = w.DataGrid("Files");
                grid.SetColumns([new("Name")]).SetSource(source);
                grid.Select(new(1, 7));
                grid.Navigate(GridNavigation.Next);
                Expect(grid.Selection.Focused == new ItemKey(2, 7));
                grid.Navigate(GridNavigation.Last);
                Expect(grid.Selection.Focused == new ItemKey(3, 7));
                grid.Navigate(GridNavigation.First);
                Expect(grid.Selection.Focused == new ItemKey(1, 7));
                Fails(() => grid.Navigate((GridNavigation)6));
                Fails(() => grid.Navigate(GridNavigation.Next, KeyModifiers.Alt));
                source.Dispose();
                var popup = w.Popup("Suggestions", w.Stack().Add(editor).Add(items, 1));
                w.SetContent(w.Stack().Add(anchor).Add(grid, 1));
                int keys = 0, entered = 0, dismissals = 0, legacy = 0, submits = 0;
                int gridEntered = 0, gridClicks = 0, navigations = 0;
                w.NavigationHandler = e =>
                {
                    Expect(e.TargetId == anchor.Id && e.Position is not null);
                    Expect(e.Direction == (navigations == 0 ? NavigationDirection.Back : NavigationDirection.Forward));
                    ++navigations;
                    return true;
                };
                grid.FocusEntered += () => ++gridEntered;
                grid.Event += e => { if (e.Kind == EventKind.Click && e.Value == 1) ++gridClicks; };
                editor.Submitted += () => ++submits;
                editor.FocusEntered += () => ++entered;
                popup.Event += e => { if (e.Kind == EventKind.Dismiss) ++dismissals; };
                w.Key += _ => ++legacy;
                w.KeyHandler = e =>
                {
                    if (e.VirtualKey == 0x7A) { w.Close(); return true; }
                    if (e.VirtualKey == 0x7B)
                    {
                        grid.Focus();
                        Expect(grid.Focused);
                        Expect(PostMessage(GetFocus(), 0x100, 0x0D, 0));
                        Expect(PostMessage(GetFocus(), 0x100, 0x7A, 0));
                        return true;
                    }
                    if (e.TargetId == grid.Id) return false;
                    if (e.VirtualKey == 0x24) return false;
                    if (e.VirtualKey == 0x1B) { Expect(popup.IsOpen); ++keys; return false; }
                    Expect(e.TargetId == editor.Id && editor.Focused && popup.IsOpen);
                    ++keys;
                    if (e.VirtualKey == 0x28) items.Step(1);
                    if (e.VirtualKey == 0x26) items.Step(-1);
                    return true;
                };
                using var finished = new ManualResetEventSlim();
                var watchdog = Task.Run(() => { if (!finished.Wait(TimeSpan.FromSeconds(15))) w.Post(w.Close); });
                try
                {
                    Expect(Task.Run(() => w.Post(() =>
                    {
                        anchor.Focus();
                        nint nativeAnchor = GetFocus();
                        Expect(SendMessageW(nativeAnchor, 0x20B, (nuint)((1 << 16) | 0x20), 0) == 1);
                        Expect(navigations == 0);
                        Expect(SendMessageW(nativeAnchor, 0x20C, 1 << 16, 0) == 1);
                        Expect(navigations == 1);
                        Expect(SendMessageW(nativeAnchor, 0x319, (nuint)nativeAnchor, 2 << 16) == 1);
                        Expect(navigations == 2);
                        w.NavigationHandler = null;
                        popup.Show(anchor);
                        Expect(popup.IsOpen && editor.Focused);
                        nint edit = GetFocus();
                        Expect(PostMessage(edit, 0x100, 0x28, 0));
                        Expect(PostMessage(edit, 0x100, 0x26, 0));
                        Expect(PostMessage(edit, 0x100, 0x09, 0));
                        Expect(PostMessage(edit, 0x100, 0x0D, 0));
                        Expect(PostMessage(edit, 0x102, 'q', 0));
                        Expect(PostMessage(edit, 0x100, 0x24, 0));
                        Expect(PostMessage(edit, 0x100, 0x1B, 0));
                        Expect(PostMessage(edit, 0x100, 0x7B, 0));
                    })).GetAwaiter().GetResult());
                    w.Run();
                }
                finally { finished.Set(); watchdog.GetAwaiter().GetResult(); }
                Expect(keys == 5 && entered == 1 && dismissals == 1 && legacy == 3 && submits == 0);
                Expect(gridEntered == 1 && gridClicks == 1);
                Expect(editor.Text == "q" && !popup.IsOpen && !w.Post(() => { }));
                Expect(navigations == 2 && w.NavigationHandler is null);
            }
            using (var w = new Window("Split events", 900, 400))
            {
                var first = w.TextInput("First pane");
                var second = w.TextInput("Second pane");
                var split = w.SplitView("Responsive panes", first, second);
                int transitions = 0;
                split.Event += e =>
                {
                    if (e.Kind != EventKind.View) return;
                    ++transitions;
                    Expect(split.Expanded == (e.Value != 0));
                    if (split.Expanded)
                    {
                        second.Focus();
                        split.MaximumSize(609, 400);
                    }
                    else
                    {
                        Expect(split.SecondVisible);
                        first.Focus();
                        Expect(first.Focused && !second.Focused);
                        w.Close();
                    }
                };
                w.SetContent(w.Stack().Add(split, 1));
                using var finished = new ManualResetEventSlim();
                var watchdog = Task.Run(() => { if (!finished.Wait(TimeSpan.FromSeconds(15))) w.Post(w.Close); });
                try { w.Run(); }
                finally { finished.Set(); watchdog.GetAwaiter().GetResult(); }
                Expect(transitions == 2);
            }
            using (var w = new Window("Post failure"))
            {
                w.SetContent(w.Stack().Add(w.Label("Failure test")));
                Expect(w.Post(() => throw new InvalidOperationException("posted sentinel")));
                try { w.Run(); throw new Exception("Expected posted failure."); }
                catch (XuiException error) { Expect(error.Status == 8 && error.InnerException?.Message == "posted sentinel"); }
                Expect(w.CallbackStatus == 8 && !w.Post(() => { }));
            }
            using (var w = new Window("Navigation failure"))
            {
                var input = w.TextInput("Navigation target");
                w.SetContent(w.Stack().Add(input));
                w.NavigationHandler = _ => throw new InvalidOperationException("navigation sentinel");
                Expect(w.Post(() =>
                {
                    input.Focus();
                    SendMessageW(GetFocus(), 0x319, (nuint)GetFocus(), 1 << 16);
                }));
                try { w.Run(); throw new Exception("Expected navigation failure."); }
                catch (XuiException error) { Expect(error.Status == 8 && error.InnerException?.Message == "navigation sentinel"); }
                Expect(w.CallbackStatus == 8);
            }
        }
    private static void FeatureLifetimes()
    {
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
        Console.WriteLine($"C# feature assertions: {assertions}; feature constructors including Miller columns; bounded million-row source.");
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
