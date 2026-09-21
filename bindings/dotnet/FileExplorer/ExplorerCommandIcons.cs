namespace Xui.FileExplorer;

internal static class ExplorerCommandIcons
{
    public static ButtonIcon For(string commandId) => commandId switch
    {
        "back" or "previous-tab" or "tab.move-left" => ButtonIcon.Back,
        "forward" or "next-tab" or "tab.move-right" => ButtonIcon.Forward,
        "up-to-parent-folder" => ButtonIcon.Up,
        "refresh-folder" => ButtonIcon.Refresh,
        "new-tab" or "duplicate-tab" => ButtonIcon.Add,
        "close-tab" or "close-all-tabs" or "close-window" or "clear-folder-filter" => ButtonIcon.Close,
        "duplicate-in-new-pane" or "toggle-split-panes" or "focus-other-pane"
            or "open-selected-item-in-other-pane" => ButtonIcon.Split,
        "duplicate-tab-to-new-window" or "open-selected-folder-in-new-tab"
            or "preview-selected-item" => ButtonIcon.Open,
        "go-to-folder" => ButtonIcon.Folder,
        "use-xl-icons-view" or "use-l-icons-view" or "use-m-icons-view" or "use-list-view"
            or "use-tree-view" or "use-details-view" or "use-columns-view" => ButtonIcon.Library,
        "find-in-this-folder" or "filter-navigation" => ButtonIcon.Search,
        "toggle-navigation-pane" => ButtonIcon.Navigation,
        "add-or-remove-folder-bookmark" => ButtonIcon.Bookmark,
        "toggle-light-dark-theme" => ButtonIcon.Theme,
        "customization" => ButtonIcon.Settings,
        // The native catalog has no clipboard icons. Unknown commands use the same neutral fallback.
        "copy-files" or "cut-files" or "paste-files-into-this-folder" or "copy-file-paths" => ButtonIcon.More,
        _ => ButtonIcon.More
    };
}
