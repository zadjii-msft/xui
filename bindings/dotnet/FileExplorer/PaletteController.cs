using Xui.FileExplorer.Models;

namespace Xui.FileExplorer;

internal sealed class PaletteController
{
    private readonly ExplorerApplication app;
    private readonly Popup popup;
    private readonly TextInput editor;
    private readonly ItemsView results;
    private readonly Label message;
    private readonly ScrollView statusHost;
    private readonly List<string> history = [];
    private readonly Dictionary<string, ulong> identities = new(StringComparer.OrdinalIgnoreCase);
    private CancellationTokenSource suggestions = new();
    private DirectorySnapshot? snapshot;
    private FilePaneView? pane;
    private FileRows? files;
    private IReadOnlyList<ExplorerCommand> commands = [];
    private IReadOnlyList<FileEntry> entries = [];
    private bool navigationMode, updating, pending;
    private int selected = -1, historyIndex;
    private ulong nextId = 1;

    public PaletteController(ExplorerApplication app)
    {
        this.app = app;
        var window = app.Window;
        editor = window.TextInput("Go to folder").SetAutomationId("palette-query")
            .SetCaptionVisible(false).PreferredSize(740, 44);
        results = window.ItemsView("Palette results").SetAutomationId("palette-results");
        message = window.Label("").SetAutomationId("palette-status");
        statusHost = window.ScrollView(window.Stack().Padding(4).Add(message), "Palette status")
            .PreferredSize(740, 0).Visible(false);
        var layout = window.Grid("Palette content")
            .SetTracks([new(TrackSizing.Fixed, 44), new(TrackSizing.Fixed, 8),
                new(TrackSizing.Star), new(TrackSizing.Automatic)], [new(TrackSizing.Star)])
            .Add(editor).Add(results, row: 2).Add(statusHost, row: 3);
        var content = window.Stack().Padding(12).Add(layout, 1);
        popup = window.Popup("Explorer palette", content).SetAutomationId("explorer-palette")
            .PreferredSize(760, 420).SetPlacement(PopupPlacement.Center).SetWindowBackground(true);
        popup.Event += e => { if (e.Kind == EventKind.Dismiss) suggestions.Cancel(); };
        editor.Changed += _ => { if (!updating) Query(); };
        editor.Submitted += () => Accept(false);
        results.Event += e =>
        {
            if (e.Kind == EventKind.Selection)
            {
                var key = results.Selection.Focused;
                selected = key is null ? -1 : navigationMode
                    ? files?.Find(key.Value) is { } index ? checked((int)index) : -1
                    : checked((int)key.Value.Id - 1);
            }
            else if (e.Kind == EventKind.Click) Accept(false);
        };
    }

    public bool IsOpen => popup.IsOpen;
    public bool Pending => pending;
    public int ResultCount => navigationMode ? entries.Count : commands.Count;
    public string QueryText => editor.Text;
    public TextSelection QuerySelection => editor.Selection;
    public ElementBounds Bounds => popup.GetBounds();
    public float StatusHeight => statusHost.GetBounds().Height;
    public void EditQuery(string text) => SetQuery(text, remember: false);

    public void ShowNavigation(FilePaneView target)
    {
        if (IsOpen) Dismiss();
        pane = target;
        navigationMode = true;
        var tab = target.Model.Active;
        snapshot = tab.TryGetHistory(0, out _) ? new(tab.Path, tab.Entries) : null;
        identities.Clear();
        entries = [];
        files = null;
        selected = -1;
        SetSource(new FileRows([], Identify, suggestions: true));
        results.SetTrailingShortcutBadges(false).ItemSize(180, 32);
        editor.SetName("Go to folder").SetPlaceholder("Type a folder path");
        history.Clear();
        historyIndex = -1;
        SetQuery(target.Model.Active.Path, remember: true);
        Show(target.Address);
    }

    public void ShowCommands()
    {
        if (IsOpen) Dismiss();
        pane = app.Active;
        navigationMode = false;
        results.SetTrailingShortcutBadges(true).ItemSize(180, 40);
        editor.SetName("Search commands").SetPlaceholder("Search commands");
        SetQuery("", remember: false);
        Show(app.Active.Address);
    }

    private void Show(Control anchor)
    {
        popup.Show(anchor);
        editor.Focus(selectAll: true);
    }

    public void Dismiss()
    {
        suggestions.Cancel();
        pending = false;
        popup.Dismiss();
    }

    private ulong Identify(string path)
    {
        if (!identities.TryGetValue(path, out ulong id)) identities.Add(path, id = nextId++);
        return id;
    }

    private void SetQuery(string text, bool remember)
    {
        if (remember)
        {
            if (historyIndex + 1 < history.Count) history.RemoveRange(historyIndex + 1, history.Count - historyIndex - 1);
            if (history.Count == 0 || !string.Equals(history[^1], text, StringComparison.OrdinalIgnoreCase))
                history.Add(text);
            historyIndex = history.Count - 1;
        }
        updating = true;
        try { editor.Text = text; }
        finally { updating = false; }
        Query();
    }

    private void ShowStatus(string text)
    {
        message.Text = text;
        statusHost.PreferredSize(740, text.Length == 0 ? 0 : 32).Visible(text.Length != 0);
    }

    private void Query()
    {
        suggestions.Cancel();
        suggestions.Dispose();
        suggestions = new();
        if (!navigationMode)
        {
            pending = false;
            snapshot = null;
            entries = [];
            files = null;
            identities.Clear();
            results.Enabled = true;
            string query = editor.Text.Trim();
            commands = app.Commands.Where(c => c.Name.Contains(query, StringComparison.OrdinalIgnoreCase)
                || c.Shortcut.Contains(query, StringComparison.OrdinalIgnoreCase)).ToArray();
            SetSource(new CommandRows(commands));
            SelectFirst();
            ShowStatus(commands.Count == 0 ? "No matching commands." : "");
            return;
        }

        string text = editor.Text;
        string basePath = pane!.Model.Active.Path;
        try
        {
            if (snapshot is not null && FileSystemService.SuggestFromSnapshot(snapshot, text, basePath) is { } matches)
            {
                ShowMatches(matches);
                return;
            }
        }
        catch (Exception error) when (UiWork.IsExpected(error))
        {
            ShowError(error);
            return;
        }

        pending = true;
        selected = -1;
        // Keep the last complete view until its replacement is ready, but do not activate stale rows.
        results.Enabled = false;
        ShowStatus("Loading...");
        app.Work.Start(token => app.Files.SuggestAsync(text, basePath, token),
            suggestions.Token, ShowMatches, ShowError);
    }

    private void ShowMatches(NavigationSuggestions matches)
    {
        pending = false;
        if (!string.Equals(snapshot?.Path, matches.Directory, StringComparison.OrdinalIgnoreCase)) identities.Clear();
        snapshot = matches.Snapshot;
        entries = matches.Entries;
        files = new(entries, Identify, suggestions: true);
        results.Enabled = true;
        SetSource(files);
        SelectFirst();
        ShowStatus(entries.Count == 0 ? "No matches. Enter opens the typed folder." : "");
    }

    private void ShowError(Exception error)
    {
        pending = false;
        selected = -1;
        entries = [];
        files = new(entries, Identify, suggestions: true);
        results.Enabled = true;
        SetSource(files);
        ShowStatus($"Cannot suggest this path: {error.Message}");
    }

    private void SetSource(IReadOnlyImmutableSource rows)
    {
        using var source = app.Window.ImmutableSource(rows);
        results.SetSource(source);
    }

    private void SelectFirst()
    {
        selected = navigationMode ? (entries.Count == 0 ? -1 : 0) : commands.ToList().FindIndex(c => c.Enabled);
        SelectCurrent();
    }

    private void SelectCurrent()
    {
        if (selected >= 0) results.Select(navigationMode ? files!.Key((ulong)selected) : new((ulong)selected + 1));
    }

    private void Step(int delta)
    {
        int count = ResultCount;
        if (pending || count == 0) return;
        int candidate = selected;
        for (int i = 0; i < count; i++)
        {
            candidate = (candidate + delta + count) % count;
            if (navigationMode || commands[candidate].Enabled)
            {
                selected = candidate;
                SelectCurrent();
                break;
            }
        }
    }

    public void CompletePath()
    {
        if (!navigationMode || pending || selected < 0) return;
        var entry = entries[selected];
        SetQuery(entry.IsDirectory ? Path.TrimEndingDirectorySeparator(entry.FullPath) + Path.DirectorySeparatorChar : entry.FullPath, remember: true);
        FocusQueryEnd();
    }

    private void FocusQueryEnd()
    {
        ulong end = (ulong)editor.Text.Length;
        editor.SetSelection(new(end, end)).Focus();
    }

    public void Accept(bool otherPane)
    {
        if (pending) return;
        if (!navigationMode)
        {
            if (selected < 0 || !commands[selected].Enabled) return;
            var command = commands[selected];
            Dismiss();
            command.Execute();
            return;
        }

        var target = otherPane ? app.OtherPane(pane!, show: true) : pane!;
        if (target is null) return;
        FileEntry? entry = selected >= 0 && selected < entries.Count ? entries[selected] : null;
        string path = editor.Text;
        Dismiss();
        if (entry is null) target.Navigate(path);
        else app.Open(entry, target);
        target.Focus();
    }

    private void MoveHistory(int delta)
    {
        int next = historyIndex + delta;
        if (!navigationMode || next < 0 || next >= history.Count) return;
        historyIndex = next;
        SetQuery(history[next], remember: false);
        FocusQueryEnd();
    }

    private void ParentQuery()
    {
        if (!navigationMode || pane is null) return;
        try
        {
            string current = FileSystemService.ResolvePath(editor.Text, pane.Model.Active.Path);
            if (Directory.GetParent(current) is { } parent) SetQuery(parent.FullName, remember: true);
        }
        catch (Exception error) when (UiWork.IsExpected(error)) { ShowStatus(error.Message); }
        FocusQueryEnd();
    }

    public bool HandleKey(uint key, KeyModifiers modifiers)
    {
        if (!IsOpen) return false;
        if (key == 0x1b) { Dismiss(); return true; }
        if (modifiers is KeyModifiers.None or KeyModifiers.Control)
        {
            switch (key)
            {
                case 0x26: Step(-1); return true;
                case 0x28: Step(1); return true;
                case 0x0d: Accept(modifiers == KeyModifiers.Control); return true;
                case 0x09 when navigationMode: CompletePath(); return true;
            }
        }
        if (modifiers == KeyModifiers.Alt && navigationMode)
        {
            switch (key)
            {
                case 0x25: MoveHistory(-1); return true;
                case 0x27: MoveHistory(1); return true;
                case 0x26: ParentQuery(); return true;
            }
        }
        return false;
    }
}
