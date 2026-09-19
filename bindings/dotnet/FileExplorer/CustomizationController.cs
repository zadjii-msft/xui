using System.Globalization;
using Xui.FileExplorer.Models;

namespace Xui.FileExplorer;

internal sealed class CustomizationController
{
    private sealed record Setting(string Id, string Name, string Help,
        Func<ExplorerCustomization, string> Get, Action<ExplorerCustomization, string> Set);
    private readonly ExplorerApplication app;
    private readonly CustomizationLayout layout;
    private readonly List<Setting> settings = [];
    private Setting[] matches = [];
    private Setting? selected;

    public CustomizationController(ExplorerApplication app)
    {
        this.app = app;
        layout = new(app.Window, attach: false);
        layout.Results.ItemSize(800, 40);
        layout.Search.Changed += _ => Search();
        layout.Results.Event += e =>
        {
            if (e.Kind is not (EventKind.Selection or EventKind.Click)) return;
            if (layout.Results.Selection.Focused is { } key && key.Id > 0 && key.Id <= (ulong)matches.Length)
                Select(matches[(int)key.Id - 1]);
        };
        layout.Apply.Click += Apply;
        layout.Value.Submitted += Apply;
        layout.ResetSelected.Click += () => Guard(() =>
        {
            if (selected is null) return;
            var copy = app.State.Customization.Clone();
            if (selected.Id.StartsWith("key:", StringComparison.Ordinal)) copy.Keybindings.Remove(selected.Id[4..]);
            else selected.Set(copy, selected.Get(new()));
            app.SetCustomization(copy);
            Search();
        });
        layout.ResetAll.Click += () => Guard(() => { app.SetCustomization(new()); Search(); });
        layout.Import.Click += Import;
        layout.Export.Click += Export;
        layout.Close.Click += Dismiss;
        AddSettings();
    }

    public bool IsOpen => layout.Root.IsOpen;
    internal TextInput SearchInput => layout.Search;
    internal TextInput ValueInput => layout.Value;
    internal Button ApplyButton => layout.Apply;
    internal string Message => layout.Message.Text;
    internal int ResultCount => matches.Length;
    public void Dismiss() => layout.Root.Dismiss();
    public void Show()
    {
        app.Palettes.Dismiss();
        Search();
        layout.Root.Show(app.Window.TitlebarLeading);
        layout.Search.Focus();
    }

    private void AddSettings()
    {
        void Text(string id, string name, string help, Func<ExplorerCustomization, string> get, Action<ExplorerCustomization, string> set)
            => settings.Add(new(id, name, help, get, set));
        void Flag(string id, string name, Func<ExplorerCustomization, bool> get, Action<ExplorerCustomization, bool> set)
            => Text(id, name, "Enter true or false.", c => get(c).ToString().ToLowerInvariant(),
                (c, v) => set(c, bool.TryParse(v, out bool value) ? value : throw new InvalidDataException("Enter true or false.")));
        void Number(string id, string name, Func<ExplorerCustomization, float> get, Action<ExplorerCustomization, float> set)
            => Text(id, name, "Enter a number in DIPs.", c => get(c).ToString(CultureInfo.InvariantCulture),
                (c, v) => set(c, float.TryParse(v, CultureInfo.InvariantCulture, out float value) ? value
                    : throw new InvalidDataException("Enter a number.")));
        Text("theme", "Appearance: theme", "system, light, or dark. Windows high contrast takes priority.", c => c.Theme, (c, v) => c.Theme = v);
        Number("density", "Appearance: row spacing", c => c.RowHeight, (c, v) => c.RowHeight = v);
        Number("font-size", "Appearance: UI font size", c => c.FontSize, (c, v) => c.FontSize = v);
        Text("font", "Appearance: UI font family", "Enter an installed font family.", c => c.FontFamily, (c, v) => c.FontFamily = v);
        Text("date", "Appearance: date format", "Use a .NET date format, such as g, d, yyyy-MM-dd HH:mm.", c => c.DateFormat, (c, v) => c.DateFormat = v);
        Flag("fill", "Appearance: fill thumbnails (false = fit)", c => c.ThumbnailFill, (c, v) => c.ThumbnailFill = v);
        Flag("smooth", "Appearance: smooth scrolling", c => c.SmoothScrolling, (c, v) => c.SmoothScrolling = v);
        Flag("animations", "Appearance: animations", c => c.Animations, (c, v) => c.Animations = v);
        Flag("single-click", "Interaction: single-click open", c => c.SingleClick, (c, v) => c.SingleClick = v);
        Flag("toolbar", "Surfaces: show toolbar", c => c.ShowToolbar, (c, v) => c.ShowToolbar = v);
        Flag("labels", "Surfaces: toolbar labels", c => c.ToolbarLabels, (c, v) => c.ToolbarLabels = v);
        Flag("sidebar", "Surfaces: show sidebar", c => c.ShowSidebar, (c, v) => c.ShowSidebar = v);
        Flag("status", "Surfaces: show item status", c => c.ShowStatus, (c, v) => c.ShowStatus = v);
        Flag("home-recent", "Home: recent files", c => c.HomeRecentFiles, (c, v) => c.HomeRecentFiles = v);
        Flag("home-pins", "Home: pinned locations", c => c.HomePinnedLocations, (c, v) => c.HomePinnedLocations = v);
        Flag("home-drives", "Home: drive capacity", c => c.HomeDriveCapacity, (c, v) => c.HomeDriveCapacity = v);
        Text("sidebar-sections", "Sidebar: section order and visibility",
            "Comma-separated section IDs: recents, bookmarks, storage, places, tree. Omit a section to hide it.",
            c => string.Join(", ", c.SidebarSections), (c, v) => c.SidebarSections = List(v));
        foreach (var command in app.Commands.Where(c => c.Shortcut != "Alt+F4"))
        {
            Text("key:" + command.StableId, "Keyboard: " + command.Name,
                "Separate aliases with ; and sequence strokes with , (Ctrl+K, Ctrl+R). Empty removes all mappings. default restores defaults.",
                c => string.Join("; ", app.Bindings(command, c)),
                (c, v) =>
                {
                    if (v.Equals("default", StringComparison.OrdinalIgnoreCase)) c.Keybindings.Remove(command.StableId);
                    else c.Keybindings[command.StableId] = v.Split(';', StringSplitOptions.TrimEntries | StringSplitOptions.RemoveEmptyEntries);
                });
            Text("toolbar:" + command.StableId, "Toolbar: " + command.Name, "Enter a 1-based position, or 0 to hide this command.",
                c => Position(c.ToolbarCommands, command.StableId), (c, v) => Move(c.ToolbarCommands, command.StableId, v));
            Text("sidebar:" + command.StableId, "Sidebar command: " + command.Name, "Enter a 1-based position, or 0 to hide this command.",
                c => Position(c.SidebarCommands, command.StableId), (c, v) => Move(c.SidebarCommands, command.StableId, v));
        }
    }

    private static List<string> List(string value) => value.Split(',', StringSplitOptions.TrimEntries | StringSplitOptions.RemoveEmptyEntries).ToList();
    private static string Position(List<string> list, string id) => (list.IndexOf(id) + 1).ToString(CultureInfo.InvariantCulture);
    private static void Move(List<string> list, string id, string value)
    {
        if (!int.TryParse(value, out int position) || position < 0 || position > list.Count + 1)
            throw new InvalidDataException($"Enter a position from 0 through {list.Count + 1}.");
        list.Remove(id);
        if (position > 0) list.Insert(Math.Min(position - 1, list.Count), id);
    }

    private void Search()
    {
        string query = layout.Search.Text.Trim();
        matches = settings.Where(s => s.Name.Contains(query, StringComparison.OrdinalIgnoreCase) ||
            s.Id.Contains(query, StringComparison.OrdinalIgnoreCase)).ToArray();
        using var source = app.Window.ImmutableSource(new SettingRows(matches.Select(s =>
            new ItemContent(s.Name, s.Get(app.State.Customization))).ToArray()));
        layout.Results.SetSource(source);
        if (matches.Length == 0) { selected = null; layout.Help.Text = "No matching settings."; layout.Value.Text = ""; return; }
        int index = Array.FindIndex(matches, s => s.Id == selected?.Id);
        if (index < 0) index = 0;
        layout.Results.Select(new((ulong)index + 1));
        Select(matches[index]);
    }

    private void Select(Setting setting)
    {
        selected = setting;
        layout.Help.Text = setting.Help;
        layout.Value.Text = setting.Get(app.State.Customization);
    }
    private void Apply() => Guard(() =>
    {
        if (selected is null) return;
        var copy = app.State.Customization.Clone();
        selected.Set(copy, layout.Value.Text.Trim());
        app.SetCustomization(copy);
        Search();
    });
    private void Import() => Guard(() =>
    {
        string? path = app.Window.ShowOpenFileDialog(new() { Title = "Import Explorer customization", Filters = [new("JSON", "*.json")] });
        if (path is null) return;
        if (new FileInfo(path).Length > 1024 * 1024) throw new InvalidDataException("Customization import is too large.");
        app.SetCustomization(ExplorerCustomization.Parse(File.ReadAllText(path)));
        Search();
    });
    private void Export() => Guard(() =>
    {
        string? path = app.Window.ShowSaveFileDialog(new() { Title = "Export Explorer customization",
            Filters = [new("JSON", "*.json")], DefaultExtension = "json", SuggestedName = "explorer-customization.json" });
        if (path is not null) File.WriteAllText(path, app.State.Customization.ToJson());
    });
    private void Guard(Action action)
    {
        try { action(); layout.Message.Text = "Preferences saved."; }
        catch (Exception error) when (UiWork.IsExpected(error) || error is XuiException)
        { layout.Message.Text = $"Preferences were not changed: {error.Message}"; }
    }

    private sealed class SettingRows(ItemContent[] rows) : IReadOnlyImmutableSource
    {
        public ulong Count => (ulong)rows.Length;
        public ItemKey Key(ulong index) => new(index + 1);
        public ulong? Find(ItemKey key) => key.Id > 0 && key.Id <= Count ? key.Id - 1 : null;
        public ItemContent Item(ulong index, ulong column = 0) => rows[checked((int)index)];
    }
}
