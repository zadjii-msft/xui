using Xui.FileExplorer.Models;

namespace Xui.FileExplorer;

internal sealed class ContextActionsController
{
    internal const ulong SearchCommand = 1000, FavoritesCommand = 1001, CustomizeCommand = 1002;
    internal const string CustomizeIdentity = "app:customize-explorer";
    private readonly ExplorerApplication app;
    private readonly FilePaneView pane;
    private readonly Popup popup;
    private readonly TextInput query;
    private readonly ItemsView results;
    private readonly Label status;
    private readonly Button run, pin, hide, windows;
    private ShellActionSession? shell;
    private IReadOnlyList<string> paths = Array.AsReadOnly(Array.Empty<string>());
    private ContextActionEntry[] apps = [], entries = [], filtered = [];
    private Action<ulong>? invokeApp;
    private Func<bool> current = () => false;
    private volatile int generation;
    private bool favoritesOnly, invoking, loading;
    private string? pendingVerb;
    internal bool Executing => pendingVerb is not null || invoking;
    private string notice = "";
    internal bool IsOpen => popup.IsOpen;
    internal bool Loading => loading;
    internal IReadOnlyList<ContextActionEntry> Entries => entries;
    internal IReadOnlyList<string> SelectedPaths => paths;
    internal TextInput Query => query;
    internal void Dismiss() => popup.Dismiss();
    internal void SelectResult(string text)
    {
        query.Text = text;
        Filter();
        if (filtered.Length > 0) results.Select(new(1));
    }

    internal ContextActionsController(ExplorerApplication app, FilePaneView pane)
    {
        this.app = app;
        this.pane = pane;
        var window = app.Window;
        query = window.TextInput("Search context actions").SetAutomationId("context-actions-query")
            .SetCaptionVisible(false).SetPlaceholder("Search app actions and Windows Shell commands").PreferredSize(740, 40);
        results = window.ItemsView("Context actions").SetAutomationId("context-actions-results").ItemSize(180, 38);
        status = window.Label("").SetAutomationId("context-actions-status");
        run = window.Button("Run").SetAutomationId("context-actions-run");
        pin = window.Button("Pin").SetAutomationId("context-actions-pin");
        hide = window.Button("Hide app action").SetAutomationId("context-actions-hide");
        windows = window.Button("Show Windows menu...").SetAutomationId("context-actions-windows");
        var close = window.Button("Close");
        var buttons = window.Stack(Axis.Horizontal).Spacing(8).Add(run).Add(pin).Add(hide).Add(windows).Add(close);
        var body = window.Stack().Padding(12).Spacing(8).Add(query).Add(results, 1).Add(status).Add(buttons);
        popup = window.Popup("Search and customize context actions", body).SetPlacement(PopupPlacement.Center)
            .SetWindowBackground(true).PreferredSize(790, 470).SetAutomationId("context-actions");
        query.Changed += _ => Filter();
        query.Submitted += Run;
        results.Event += e => { if (e.Kind == EventKind.Selection) UpdateButtons(); };
        run.Click += Run;
        pin.Click += TogglePin;
        hide.Click += ToggleHidden;
        windows.Click += WindowsMenu;
        close.Click += () => popup.Dismiss();
        popup.Event += e => { if (e.Kind == EventKind.Dismiss && !invoking) Cancel(); };
        window.Closed += _ => Cancel();
    }

    internal Command[] Capture(Command[] commands, string[] selection, Func<bool> isCurrent, Action<ulong> invoke)
    {
        if (popup.IsOpen) popup.Dismiss();
        Cancel();
        paths = Array.AsReadOnly((string[])selection.Clone());
        apps = commands.Select(command => new ContextActionEntry(ContextActionCatalog.AppIdentity(command.Id),
            command.Label, command.Enabled, AppId: command.Id)).ToArray();
        current = isCurrent;
        invokeApp = invoke;
        var preferences = app.State.Customization;
        var visible = commands.Where(command => !preferences.HiddenContextActions.Contains(ContextActionCatalog.AppIdentity(command.Id)))
            .OrderByDescending(command => preferences.PinnedContextActions.Contains(ContextActionCatalog.AppIdentity(command.Id))).ToList();
        visible.Add(new(SearchCommand, "Search / customize context actions..."));
        if (preferences.PinnedContextActions.Count != 0)
            visible.Add(new(FavoritesCommand, "Pinned context actions..."));
        visible.Add(new(CustomizeCommand, "Customize Explorer..."));
        return visible.ToArray();
    }

    internal bool Invoke(ulong id)
    {
        if (id == CustomizeCommand)
        {
            if (popup.IsOpen) popup.Dismiss();
            Cancel();
            app.ShowCustomization();
            return true;
        }
        if (id is not (SearchCommand or FavoritesCommand)) return false;
        Show(id == FavoritesCommand);
        return true;
    }

    private bool Current() => !app.CloseRequested && current();

    internal void InvokeCanonical(string verb)
    {
        if (!Current()) { app.Report("The context selection changed. Open its menu again."); return; }
        if (paths.Count is < 1 or > 256)
        {
            app.Report("Windows Shell actions support 1 to 256 selected items.");
            return;
        }
        Cancel();
        pendingVerb = verb;
        loading = true;
        try
        {
            shell = new(app.Window, paths, Current);
            Poll(++generation);
        }
        catch (Exception error) when (Expected(error)) { Fail(error); }
    }

    private void Show(bool onlyFavorites)
    {
        if (!Current()) { app.Report("The context selection changed. Open its menu again."); return; }
        Cancel();
        favoritesOnly = onlyFavorites;
        entries = apps;
        notice = "";
        loading = paths.Count is > 0 and <= 256;
        query.Text = "";
        Filter();
        popup.Show(pane.Address);
        query.Focus();
        if (!loading)
        {
            notice = paths.Count > 256 ? "Shell search supports up to 256 selected items." : "Select a file or folder to search Windows actions.";
            Filter();
            return;
        }
        try
        {
            shell = new(app.Window, paths, Current);
            Poll(++generation);
        }
        catch (Exception error) when (Expected(error)) { Fail(error); }
    }

    private async void Poll(int request)
    {
        // The timer posts only metadata reads. COM ownership stays on the native STA.
        while (request == generation)
        {
            await Task.Delay(60).ConfigureAwait(false);
            if (!app.Window.Post(() => Tick(request))) return;
        }
    }

    private void Tick(int request)
    {
        if (request != generation || shell is null || shell.IsDisposed) return;
        if (!invoking && !Current())
        {
            bool direct = pendingVerb is not null;
            Cancel();
            notice = "The context selection changed. Open its menu again.";
            if (direct) app.Report(notice);
            else Filter();
            return;
        }
        try
        {
            shell.Read();
            if (shell.Finished)
            {
                bool completed = invoking;
                Cancel();
                if (completed) { pane.RequestDirectoryRefresh(); app.Report("The Windows command session ended."); }
                else { notice = "The Shell session ended. Open the context menu again."; Filter(); }
                return;
            }
            if (!loading || !shell.Ready) return;
            loading = false;
            if (pendingVerb is { } verb)
            {
                var matches = shell.Actions.Where(action =>
                    string.Equals(action.CanonicalVerb, verb, StringComparison.OrdinalIgnoreCase)).ToArray();
                if (matches.Length != 1 || !matches[0].Enabled)
                {
                    Fail(new InvalidOperationException($"Windows does not provide a unique enabled '{verb}' action for this selection."));
                    return;
                }
                shell.Invoke(matches[0].Key);
                pendingVerb = null;
                invoking = true;
                return;
            }
            var discovered = ContextActionCatalog.UniqueShellIdentities(shell.Actions.Select(action =>
                new ContextActionEntry(ContextActionCatalog.ShellIdentity(action.CanonicalVerb),
                    MenuLabel(action.Label), action.Enabled, ShellId: action.Key.Id, Version: action.Key.Version)));
            entries = [.. apps, .. discovered];
            Filter();
        }
        catch (Exception error) when (Expected(error)) { Fail(error); }
    }

    private static string MenuLabel(string label) => label.Replace("&&", "\u0001", StringComparison.Ordinal)
        .Replace("&", "", StringComparison.Ordinal).Replace('\u0001', '&');

    private ContextActionEntry? Selected => results.Selection.Focused is { } key && key.Id > 0 && key.Id <= (ulong)filtered.Length
        ? filtered[checked((int)key.Id - 1)] : null;

    private void Filter()
    {
        var previous = Selected;
        var pinned = app.State.Customization.PinnedContextActions;
        var unavailable = pinned.Where(identity => !entries.Any(entry => entry.Identity == identity))
            .Select(identity => new ContextActionEntry(identity, $"Unavailable for this selection: {identity}", false));
        filtered = ContextActionCatalog.Search(entries.Concat(unavailable), query.Text, pinned, favoritesOnly);
        using var source = app.Window.ImmutableSource(new Rows(filtered, app.State.Customization.HiddenContextActions,
            app.State.Customization.PinnedContextActions));
        results.SetSource(source);
        int index = previous is null ? -1 : Array.IndexOf(filtered, previous);
        if (filtered.Length > 0) results.Select(new((ulong)Math.Max(0, index) + 1));
        status.Text = notice.Length != 0 ? notice : loading ? "Loading Windows Shell actions..."
            : filtered.Length == 0 ? "No matching actions. The Windows menu remains available."
            : "Run uses the captured selection. Dynamic and owner-drawn commands stay in the Windows menu.";
        UpdateButtons();
    }

    private void UpdateButtons()
    {
        var entry = Selected;
        run.Enabled = !invoking && entry?.Enabled == true && (entry.AppId != 0 || shell is { Ready: true }) && Current();
        pin.Enabled = !invoking && entry is not null;
        pin.Text = entry?.Identity is null ? "Pin unsupported (no stable verb)"
            : app.State.Customization.PinnedContextActions.Contains(entry.Identity) ? "Unpin" : "Pin";
        hide.Enabled = !invoking && entry?.AppId > 0;
        hide.Text = entry?.Identity is { } identity && app.State.Customization.HiddenContextActions.Contains(identity)
            ? "Show app action" : "Hide app action";
        windows.Enabled = !invoking && paths.Count is > 0 and <= 256 && Current();
    }

    internal void TogglePin()
    {
        if (Selected is not { } entry) return;
        if (entry.Identity is null)
        {
            notice = "Pinning is unsupported: this Shell entry has no unique canonical verb. Its temporary command ID is never saved.";
            Filter();
            return;
        }
        UpdatePreferences(copy => ContextActionCatalog.TogglePin(copy.PinnedContextActions, entry.Identity));
    }

    internal void ToggleHidden()
    {
        if (Selected is not { AppId: > 0, Identity: { } identity }) return;
        UpdatePreferences(copy =>
        {
            if (!copy.HiddenContextActions.Remove(identity)) copy.HiddenContextActions.Add(identity);
            return true;
        });
    }

    private void UpdatePreferences(Func<ExplorerCustomization, bool> update)
    {
        try
        {
            var copy = app.State.Customization.Clone();
            if (!update(copy)) notice = "You can pin at most 256 context actions.";
            else { app.SetCustomization(copy); notice = ""; }
        }
        catch (Exception error) when (UiWork.IsExpected(error) || Expected(error))
        {
            notice = $"Context preferences were not saved: {error.Message}";
            app.Report(notice);
        }
        Filter();
    }

    internal void Run()
    {
        if (Selected is not { Enabled: true } entry || invoking) return;
        if (!Current()) { Fail(new InvalidOperationException("The context selection changed.")); return; }
        if (paths.Any(path => !File.Exists(path) && !Directory.Exists(path)))
        {
            Fail(new InvalidOperationException("A selected path is no longer available."));
            return;
        }
        if (entry.AppId is FileContextMenu.Copy or FileContextMenu.Cut or FileContextMenu.CopyPaths or FileContextMenu.Paste &&
            !app.Transfers.CanTransfer(pane))
        {
            notice = "File transfer actions are not available while this pane is busy.";
            Filter();
            return;
        }
        if (entry.AppId != 0)
        {
            var invoke = invokeApp;
            popup.Dismiss();
            invoke?.Invoke(entry.AppId);
        }
        else if (entry.ShellId != 0 && shell is not null)
        {
            try { shell.Invoke(new(entry.ShellId, entry.Version)); invoking = true; popup.Dismiss(); }
            catch (Exception error) when (Expected(error)) { Fail(error); }
        }
        else Fail(new InvalidOperationException("The Shell snapshot is no longer available."));
    }

    private void WindowsMenu()
    {
        if (!Current()) { Fail(new InvalidOperationException("The context selection changed.")); return; }
        try
        {
            shell ??= new(app.Window, paths, Current);
            shell.ShowWindowsMenu();
            invoking = true;
            popup.Dismiss();
            Poll(++generation);
        }
        catch (Exception error) when (Expected(error)) { Fail(error); }
    }

    private void Fail(Exception error)
    {
        Cancel();
        notice = $"Cannot use this context action: {error.Message} Use the full Windows menu.";
        app.Report(notice);
        if (popup.IsOpen) Filter();
    }

    private static bool Expected(Exception error) => error is XuiException or InvalidOperationException or ArgumentException;
    private void Cancel()
    {
        ++generation;
        shell?.Dispose();
        shell = null;
        pendingVerb = null;
        invoking = loading = false;
    }

    private sealed class Rows(ContextActionEntry[] values, List<string> hidden, List<string> pinned) : IReadOnlyImmutableSource
    {
        private readonly ItemContent[] items = values.Select(entry => MakeItem(entry, hidden, pinned)).ToArray();
        public ulong Count => (ulong)items.Length;
        public ItemKey Key(ulong index) => new(index + 1);
        public ulong? Find(ItemKey key) => key.Id > 0 && key.Id <= Count ? key.Id - 1 : null;
        public ItemContent Item(ulong index, ulong column = 0) => items[checked((int)index)];
        private static ItemContent MakeItem(ContextActionEntry entry, List<string> hidden, List<string> pinned)
        {
            string detail = entry.Identity?.StartsWith("app:", StringComparison.Ordinal) == true ? "App action" : "Windows Shell";
            if (entry.Identity is null) detail += " · Pin unsupported";
            if (entry.Identity is { } identity)
            {
                if (pinned.Contains(identity)) detail += " · Pinned";
                if (hidden.Contains(identity)) detail += " · Hidden from menu";
            }
            if (!entry.Enabled) detail += " · Unavailable";
            return new(entry.Label, detail);
        }
    }
}
