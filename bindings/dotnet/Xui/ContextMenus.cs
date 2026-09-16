namespace Xui;

public enum ShellMenuPresentation { Windows, Xui }

public sealed partial class TabStrip
{
    /// <summary>Builds a native menu for the right-clicked tab, or the selected tab for keyboard requests.</summary>
    public TabStrip OnContextMenu(Func<ulong, Command[]> items, Action<ulong> invoked)
    {
        ArgumentNullException.ThrowIfNull(items);
        ArgumentNullException.ThrowIfNull(invoked);
        Window.SetMenuSubscription(Handle, e =>
        {
            if (e.Kind == EventKind.Request)
                Features.Commands(this, items(e.Value) ??
                    throw new InvalidOperationException("Context menu items cannot be null."), contextMenu: true);
            else if (e.Kind == EventKind.Action) invoked(e.Value);
        });
        return this;
    }

    public void ClearContextMenu() => Window.SetMenuSubscription(Handle, null);
}

public sealed unsafe partial class DataGrid
{
    /// <summary>Builds a native row context menu on the UI thread after pointer selection updates.</summary>
    /// <remarks>Supports flat actions and separators. Empty space clears selection. Stale item actions are cancelled.</remarks>
    public DataGrid OnContextMenu(Func<Command[]> items, Action<ulong> invoked)
        => BindContextMenu(items, invoked, null);

    /// <summary>Merges real Shell verbs and app commands in one native menu.</summary>
    /// <remarks>Factories run in order after row selection. Empty paths use the app-only menu.
    /// Omit a duplicate app Open command when paths are supplied. Shell Open keeps its system behavior.</remarks>
    public DataGrid OnContextMenu(Func<Command[]> items, Action<ulong> invoked, Func<string[]> shellPaths)
    {
        ArgumentNullException.ThrowIfNull(shellPaths);
        return BindContextMenu(items, invoked, shellPaths);
    }
    /// <summary>Displays real Shell commands in XUI's styled native context menu or the Windows Shell menu.</summary>
    /// <remarks>XUI menus always include a Windows menu fallback. Native-only entries use that fallback.
    /// Labeled bitmap-backed leaves display their text and invoke the original Shell verb.
    /// Native Shell submenus, unreadable entries, and owner-drawn commands use the Windows fallback.
    /// Empty paths use the existing app-only menu. XUI menus accept at most 4092 app entries.
    /// XUI menus show app commands immediately, then add Shell commands after asynchronous discovery.
    /// A dedicated STA owns the Shell handlers. App factories and callbacks run on the UI thread.
    /// Each opening uses fresh handlers for its exact selection.
    /// Closed owners and stale selections cancel the menu without waiting for discovery.</remarks>
    public DataGrid OnContextMenu(Func<Command[]> items, Action<ulong> invoked, Func<string[]> shellPaths,
        ShellMenuPresentation presentation)
    {
        ArgumentNullException.ThrowIfNull(shellPaths);
        if (presentation is not (ShellMenuPresentation.Windows or ShellMenuPresentation.Xui))
            throw new ArgumentOutOfRangeException(nameof(presentation));
        return BindContextMenu(items, invoked, shellPaths, presentation);
    }
    private DataGrid BindContextMenu(Func<Command[]> items, Action<ulong> invoked, Func<string[]>? shellPaths,
        ShellMenuPresentation presentation = ShellMenuPresentation.Windows)
    {
        CollectionContextMenus.Bind(this, items, invoked, shellPaths, presentation);
        return this;
    }
    public void ClearContextMenu() => Window.SetMenuSubscription(Handle, null);
}

public static unsafe class CollectionContextMenus
{
    public static ItemsView OnContextMenu(this ItemsView control, Func<Command[]> items, Action<ulong> invoked,
        Func<string[]>? shellPaths = null, ShellMenuPresentation presentation = ShellMenuPresentation.Windows)
    {
        Bind(control, items, invoked, shellPaths, presentation);
        return control;
    }
    public static void ClearContextMenu(this ItemsView control) => control.Window.SetMenuSubscription(control.Handle, null);

    internal static void Bind(Control control, Func<Command[]> items, Action<ulong> invoked, Func<string[]>? shellPaths,
        ShellMenuPresentation presentation)
    {
        ArgumentNullException.ThrowIfNull(items);
        ArgumentNullException.ThrowIfNull(invoked);
        if (presentation is not (ShellMenuPresentation.Windows or ShellMenuPresentation.Xui))
            throw new ArgumentOutOfRangeException(nameof(presentation));
        var window = control.Window;
        window.SetMenuSubscription(control.Handle, e =>
        {
            if (e.Kind == EventKind.Request)
            {
                var commands = items() ?? throw new InvalidOperationException("Context menu items cannot be null.");
                var paths = shellPaths?.Invoke() ?? (shellPaths is null ? [] :
                    throw new InvalidOperationException("Shell paths cannot be null."));
                if (paths.Length > 256) throw new ArgumentOutOfRangeException(nameof(shellPaths));
                Features.Commands(control, commands, contextMenu: true);
                if (shellPaths is not null)
                {
                    using var pins = new Window.Pins();
                    var values = new Native.Text[paths.Length];
                    for (int i = 0; i < paths.Length; ++i) values[i] = pins.Text(paths[i]);
                    fixed (Native.Text* p = values) window.Check(Native.ContextMenuShellPaths(control.Handle, p, (uint)values.Length));
                    window.Check(Native.ContextMenuPresentation(control.Handle, (uint)presentation));
                }
            }
            else if (e.Kind == EventKind.Action) invoked(e.Value);
        });
    }
}
