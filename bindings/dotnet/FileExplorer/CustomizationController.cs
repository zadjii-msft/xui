using System.Globalization;
using Xui.FileExplorer.Models;

namespace Xui.FileExplorer;

internal enum SettingKind { Text, Toggle, Number, Slider, Choice, Position }

internal sealed record CustomizationSetting(string Id, string Name, string Help, SettingKind Kind,
    Func<ExplorerCustomization, string> Get, Action<ExplorerCustomization, string> Set,
    double Minimum = 0, double Maximum = 0, string[]? Choices = null,
    Func<ExplorerCustomization, int>? PositionLimit = null);

internal sealed class CustomizationController
{
    private readonly ExplorerApplication app;
    private readonly CustomizationLayout layout;
    private readonly List<CustomizationSetting> settings = [];
    private readonly List<CustomizationSettingRow> rows = [];
    private bool synchronizing;

    public CustomizationController(ExplorerApplication app)
    {
        this.app = app;
        layout = new(app.Window, attach: false);
        AddSettings();
        foreach (var setting in settings)
        {
            var row = new CustomizationSettingRow(app.Window, setting,
                value => Apply(setting, value), () => app.PostCustomization(() => Reset(setting)));
            rows.Add(row);
            layout.Rows.Add(row.Root);
        }
        layout.Search.Changed += _ => Search();
        layout.ResetAll.Click += () => app.PostCustomization(() => Guard(() =>
        {
            app.SetCustomization(new());
            Synchronize(discardDrafts: true);
            ClearErrors();
        }));
        layout.Import.Click += () => app.PostCustomization(Import);
        layout.Export.Click += () => app.PostCustomization(Export);
        layout.Close.Click += Dismiss;
        app.CustomizationChanged += () => Synchronize();
        Synchronize();
        Search();
    }

    public bool IsOpen => layout.Root.IsOpen;
    internal TextInput SearchInput => layout.Search;
    internal string Message => layout.Message.Text;
    internal int ResultCount { get; private set; }
    internal CustomizationSettingRow Row(string id) => rows.Single(row => row.Setting.Id == id);
    internal ScrollView Scroller => layout.Scroller;
    internal Button ResetAllButton => layout.ResetAll;
    public void Dismiss() => layout.Root.Dismiss();
    public void Show()
    {
        app.Palettes.Dismiss();
        Synchronize();
        Search();
        if (!IsOpen) layout.Root.Show(app.Window.TitlebarLeading);
        layout.Search.Focus();
    }

    private void AddSettings()
    {
        void Text(string id, string name, string help, Func<ExplorerCustomization, string> get, Action<ExplorerCustomization, string> set)
            => settings.Add(new(id, name, help, SettingKind.Text, get, set));
        void Flag(string id, string name, string help, Func<ExplorerCustomization, bool> get, Action<ExplorerCustomization, bool> set)
            => settings.Add(new(id, name, help, SettingKind.Toggle, c => get(c).ToString(),
                (c, v) => set(c, bool.Parse(v))));
        void Number(string id, string name, string help, double minimum, double maximum,
            Func<ExplorerCustomization, float> get, Action<ExplorerCustomization, float> set, bool slider = false)
            => settings.Add(new(id, name, help, slider ? SettingKind.Slider : SettingKind.Number,
                c => get(c).ToString(CultureInfo.InvariantCulture),
                (c, v) => set(c, float.Parse(v, CultureInfo.InvariantCulture)), minimum, maximum));
        void Position(string id, string name, string help, string command, Func<ExplorerCustomization, List<string>> list)
            => settings.Add(new(id, name, help, SettingKind.Position,
                c => (list(c).IndexOf(command) + 1).ToString(CultureInfo.InvariantCulture),
                (c, v) => Move(list(c), command, v),
                PositionLimit: c => list(c).Count + (list(c).Contains(command) ? 0 : 1)));
        settings.Add(new("theme", "Appearance: theme", "Windows high contrast takes priority.", SettingKind.Choice,
            c => c.Theme, (c, v) => c.Theme = v, Choices: ["system", "light", "dark"]));
        Text("font", "Appearance: UI font family", "Installed font family. Press Enter or Save to apply.",
            c => c.FontFamily, (c, v) => c.FontFamily = v);
        Number("font-size", "Appearance: UI font size", "Text size in DIPs (9-32).", 9, 32,
            c => c.FontSize, (c, v) => c.FontSize = v);
        Number("density", "Appearance: row spacing", "Row height in DIPs (20-80). Tree rows stay compact.", 20, 80,
            c => c.RowHeight, (c, v) => c.RowHeight = v, slider: true);
        Text("date", "Appearance: date format", "Examples: g, d, yyyy-MM-dd HH:mm. Enter or Save applies.",
            c => c.DateFormat, (c, v) => c.DateFormat = v);
        settings.Add(new("fill", "Appearance: thumbnails", "Fit shows the whole image. Fill crops it to the frame.", SettingKind.Choice,
            c => c.ThumbnailFill ? "fill" : "fit", (c, v) => c.ThumbnailFill = v == "fill", Choices: ["fit", "fill"]));
        Flag("smooth", "Appearance: smooth scrolling", "Animate scrolling when Windows permits motion.", c => c.SmoothScrolling, (c, v) => c.SmoothScrolling = v);
        Flag("animations", "Appearance: animations", "Animate panels and tabs. Reduced motion takes priority.", c => c.Animations, (c, v) => c.Animations = v);
        Flag("single-click", "Interaction: single-click open", "Open items with one click instead of a double-click.", c => c.SingleClick, (c, v) => c.SingleClick = v);
        Flag("toolbar", "Surfaces: show toolbar", "Show the address bar and pane commands.", c => c.ShowToolbar, (c, v) => c.ShowToolbar = v);
        Flag("labels", "Surfaces: toolbar labels", "Show command names instead of icon-only buttons.", c => c.ToolbarLabels, (c, v) => c.ToolbarLabels = v);
        Flag("sidebar", "Surfaces: show sidebar", "Show the navigation pane.", c => c.ShowSidebar, (c, v) => c.ShowSidebar = v);
        Flag("status", "Surfaces: show item status", "Show the file count in each pane.", c => c.ShowStatus, (c, v) => c.ShowStatus = v);
        Flag("home-recent", "Home: recent files", "Show recent files on Home.", c => c.HomeRecentFiles, (c, v) => c.HomeRecentFiles = v);
        Flag("home-pins", "Home: pinned locations", "Show pinned locations on Home.", c => c.HomePinnedLocations, (c, v) => c.HomePinnedLocations = v);
        Flag("home-drives", "Home: drive capacity", "Show storage capacity on Home.", c => c.HomeDriveCapacity, (c, v) => c.HomeDriveCapacity = v);
        foreach (var (id, name) in new[] { ("recents", "Recents"), ("bookmarks", "Bookmarks"),
            ("storage", "Storage"), ("places", "Places"), ("tree", "Current folder") })
            Position("section:" + id, "Sidebar section: " + name, "Show or hide this section. The number sets its order.", id, c => c.SidebarSections);
        foreach (var command in app.Commands.Where(c => c.Shortcut != "Alt+F4"))
        {
            Text("key:" + command.StableId, "Keyboard: " + command.Name,
                "Aliases: ;   Sequence: Ctrl+K, Ctrl+R. Empty removes bindings.",
                c => string.Join("; ", app.Bindings(command, c)),
                (c, v) =>
                {
                    if (v.Equals("default", StringComparison.OrdinalIgnoreCase)) c.Keybindings.Remove(command.StableId);
                    else c.Keybindings[command.StableId] = v.Split(';', StringSplitOptions.TrimEntries | StringSplitOptions.RemoveEmptyEntries);
                });
            Position("toolbar:" + command.StableId, "Toolbar: " + command.Name,
                "Show or hide this command. The number sets its order.", command.StableId, c => c.ToolbarCommands);
            Position("sidebar:" + command.StableId, "Sidebar command: " + command.Name,
                "Show or hide this command. The number sets its order.", command.StableId, c => c.SidebarCommands);
        }
    }

    private static void Move(List<string> list, string id, string value)
    {
        if (!int.TryParse(value, out int position) || position < 0 || position > list.Count + (list.Contains(id) ? 0 : 1))
            throw new InvalidDataException("The command position is no longer available.");
        list.Remove(id);
        if (position > 0) list.Insert(Math.Min(position - 1, list.Count), id);
    }

    private void Search()
    {
        string query = layout.Search.Text.Trim();
        ResultCount = 0;
        foreach (var row in rows)
        {
            var setting = row.Setting;
            bool visible = setting.Name.Contains(query, StringComparison.OrdinalIgnoreCase) ||
                setting.Id.Contains(query, StringComparison.OrdinalIgnoreCase) ||
                setting.Help.Contains(query, StringComparison.OrdinalIgnoreCase);
            row.Root.SetOpen(visible);
            if (visible) ++ResultCount;
        }
        layout.Empty.Visible(ResultCount == 0);
        layout.Scroller.SetOffset(0);
    }

    private void Synchronize(bool discardDrafts = false)
    {
        if (synchronizing) return;
        synchronizing = true;
        try { foreach (var row in rows) row.Synchronize(app.State.Customization, discardDrafts); }
        finally { synchronizing = false; }
    }

    private void Apply(CustomizationSetting setting, string value)
    {
        if (synchronizing) return;
        app.PostCustomization(() => Commit(setting, value));
    }

    private void Commit(CustomizationSetting setting, string value)
    {
        var row = Row(setting.Id);
        Guard(() =>
        {
            var copy = app.State.Customization.Clone();
            setting.Set(copy, value.Trim());
            app.SetCustomization(copy);
            row.Synchronize(app.State.Customization, discardDraft: true);
            row.SetError("");
        }, row, value);
    }

    private void Reset(CustomizationSetting setting) => Guard(() =>
    {
        var copy = app.State.Customization.Clone();
        if (setting.Id.StartsWith("key:", StringComparison.Ordinal)) copy.Keybindings.Remove(setting.Id[4..]);
        else
        {
            string value = setting.Get(new());
            if (setting.Kind == SettingKind.Position)
                value = Math.Min(int.Parse(value, CultureInfo.InvariantCulture), setting.PositionLimit!(copy)).ToString(CultureInfo.InvariantCulture);
            setting.Set(copy, value);
        }
        app.SetCustomization(copy);
        Row(setting.Id).Synchronize(app.State.Customization, discardDraft: true);
        Row(setting.Id).SetError("");
    }, Row(setting.Id));

    private void ClearErrors()
    {
        foreach (var row in rows) row.SetError("");
    }

    private void Import() => Guard(() =>
    {
        string? path = app.Window.ShowOpenFileDialog(new() { Title = "Import Explorer customization", Filters = [new("JSON", "*.json")] });
        if (path is null) return;
        if (new FileInfo(path).Length > 1024 * 1024) throw new InvalidDataException("Customization import is too large.");
        app.SetCustomization(ExplorerCustomization.Parse(File.ReadAllText(path)));
        Synchronize(discardDrafts: true);
        ClearErrors();
    });
    private void Export() => Guard(() =>
    {
        string? path = app.Window.ShowSaveFileDialog(new() { Title = "Export Explorer customization",
            Filters = [new("JSON", "*.json")], DefaultExtension = "json", SuggestedName = "explorer-customization.json" });
        if (path is not null) File.WriteAllText(path, app.State.Customization.ToJson());
    });
    private void Guard(Action action, CustomizationSettingRow? row = null, string? draft = null)
    {
        try { action(); layout.Message.Text = "Preferences saved."; }
        catch (Exception error) when (UiWork.IsExpected(error) || error is XuiException or FormatException)
        {
            row?.Synchronize(app.State.Customization, discardDraft: true);
            if (draft is not null && row?.Text is { } text) text.Text = draft;
            row?.SetError(error.Message);
            layout.Message.Text = $"Preferences were not changed: {error.Message}";
        }
    }
}
