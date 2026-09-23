namespace Xui.FileExplorer;

internal static class ContextActionsSmoke
{
    internal static async Task Run(ExplorerApplication app, Func<Action, Task> ui,
        Func<Func<bool>, Task> until, string fixture)
    {
        void Check(bool condition, string message)
        {
            if (!condition) throw new InvalidOperationException(message);
        }
        var pane = app.Left;
        var context = pane.ContextMenu.Customization;
        await ui(() => pane.Navigate(fixture));
        await until(() => !pane.IsLoading && !pane.IsFiltering);
        await ui(() =>
        {
            pane.SelectPath(Path.Combine(fixture, "small.txt"));
            var commands = pane.ContextMenu.GetCommands();
            Check(commands.Any(command => command.Id == ContextActionsController.SearchCommand), "File menus offer context search.");
            var beforeBindings = app.State.Customization.Clone();
            (ulong Id, string StableId)[] hinted =
            [
                (FileContextMenu.Preview, "preview-selected-item"), (FileContextMenu.Copy, "copy-files"),
                (FileContextMenu.Cut, "cut-files"), (FileContextMenu.CopyPaths, "copy-file-paths"),
                (FileContextMenu.Paste, "paste-files-into-this-folder"), (FileContextMenu.Refresh, "refresh-folder")
            ];
            string? originalCopyHint = commands.Single(command => command.Id == FileContextMenu.Copy).ShortcutHint;
            try
            {
                var remapped = beforeBindings.Clone();
                for (int i = 0; i < hinted.Length; ++i)
                    remapped.Keybindings[hinted[i].StableId] = [$"Ctrl+Shift+F{i + 13}"];
                remapped.Keybindings["copy-files"] = ["Ctrl+Shift+F14", "Ctrl+Shift+F19"];
                app.SetCustomization(remapped);
                var updated = pane.ContextMenu.GetCommands();
                foreach (var item in hinted)
                    Check(updated.Single(command => command.Id == item.Id).ShortcutHint ==
                        app.ShortcutHint(app.Commands.Single(command => command.StableId == item.StableId)),
                        $"Context hint follows registered bindings for {item.StableId}.");
                Check(commands.Single(command => command.Id == FileContextMenu.Copy).ShortcutHint == originalCopyHint,
                    "A captured context menu retains its original shortcut hint snapshot.");
                remapped.Keybindings["preview-selected-item"] = [];
                app.SetCustomization(remapped);
                Check(pane.ContextMenu.GetCommands().Single(command => command.Id == FileContextMenu.Preview).ShortcutHint == "",
                    "A removed preview binding leaves no stale Space hint.");
            }
            finally { app.SetCustomization(beforeBindings); }
            pane.ContextMenu.GetCommands();
            pane.ContextMenu.Invoke(ContextActionsController.SearchCommand);
            Check(context.IsOpen && context.Query.Focused, "Context search shows a popup with a native focused text editor.");
            Check(context.SelectedPaths.SequenceEqual([Path.Combine(fixture, "small.txt")]), "Search retains an immutable selected path.");
        });
        await until(() => !context.Loading);
        await ui(() =>
        {
            Check(context.Entries.Any(entry => entry.ShellId != 0), "Search discovers actionable Windows Shell leaves.");
            Check(context.Entries.Where(entry => entry.ShellId != 0).All(entry => !string.IsNullOrWhiteSpace(entry.Label)),
                "Shell search labels come from the native menu.");
            context.SelectResult("app:copy-paths");
            var original = app.State.Customization;
            string[] originalPins = [.. original.PinnedContextActions];
            string[] originalHidden = [.. original.HiddenContextActions];
            void RejectChange()
            {
                app.CustomizationChanged -= RejectChange;
                throw new IOException("Context preference rollback fixture.");
            }
            app.CustomizationChanged += RejectChange;
            try { context.TogglePin(); }
            finally { app.CustomizationChanged -= RejectChange; }
            Check(ReferenceEquals(app.State.Customization, original) &&
                original.PinnedContextActions.SequenceEqual(originalPins),
                "Failed pin changes retain the original customization object and favorites.");
            app.CustomizationChanged += RejectChange;
            try { context.ToggleHidden(); }
            finally { app.CustomizationChanged -= RejectChange; }
            Check(ReferenceEquals(app.State.Customization, original) &&
                original.HiddenContextActions.SequenceEqual(originalHidden),
                "Failed hide changes retain the original customization object and hidden actions.");
            context.TogglePin();
            Check(app.State.Customization.PinnedContextActions.Contains("app:copy-paths"), "Pin persists the explicit app identity.");
            context.ToggleHidden();
            Check(app.State.Customization.HiddenContextActions.Contains("app:copy-paths"), "Hide stores a built-in app identity.");
            context.Dismiss();
            var commands = pane.ContextMenu.GetCommands();
            Check(commands.All(command => command.Id != FileContextMenu.CopyPaths), "Hidden app commands leave the context menu.");
            Check(commands.Any(command => command.Id == ContextActionsController.FavoritesCommand), "Pinned actions remain accessible.");
            pane.ContextMenu.Invoke(ContextActionsController.SearchCommand);
        });
        await until(() => !context.Loading);
        await ui(() =>
        {
            var canonical = context.Entries.FirstOrDefault(entry => entry.ShellId != 0 && entry.Identity is not null);
            if (canonical is not null)
            {
                context.SelectResult(canonical.Identity!);
                context.TogglePin();
                Check(app.State.Customization.PinnedContextActions.Contains(canonical.Identity!), "Shell favorites save canonical verbs.");
                Check(!app.State.Customization.PinnedContextActions.Contains(canonical.ShellId.ToString()), "Ephemeral Shell IDs are never saved.");
                context.TogglePin();
            }
            context.SelectResult("app:copy-paths");
            context.ToggleHidden();
            context.TogglePin();
            pane.SelectPath(Path.Combine(fixture, "large.txt"));
        });
        await Task.Delay(180);
        await ui(() =>
        {
            Check(context.SelectedPaths.SequenceEqual([Path.Combine(fixture, "small.txt")]),
                "A later selection cannot mutate the captured paths.");
            context.Dismiss();
            pane.ContextMenu.GetCommands();
            app.Palettes.ShowCommands();
            pane.ContextMenu.Invoke(FileContextMenu.CopyPaths);
            Check(app.Notification.Text == "File transfer actions are not available while this pane is busy.",
                "A captured enabled transfer action checks current availability before invocation.");
            pane.ContextMenu.GetCommands();
            app.Palettes.Dismiss();
            pane.ContextMenu.Invoke(FileContextMenu.CopyPaths);
            Check(app.Notification.Text == "That command is not available for this item.",
                "An originally disabled app action cannot run from its old context snapshot.");

            var beforeRecovery = app.State.Customization.Clone();
            var recovery = beforeRecovery.Clone();
            recovery.ShowToolbar = false;
            recovery.ShowSidebar = false;
            recovery.Keybindings["commands"] = [];
            recovery.HiddenContextActions.Add(ContextActionsController.CustomizeIdentity);
            try
            {
                app.SetCustomization(recovery);
                var commands = pane.ContextMenu.GetCommands([]);
                Check(commands.Single(command => command.Id == ContextActionsController.CustomizeCommand).Enabled,
                    "Customization recovery cannot be hidden and remains available without selected paths.");
                pane.ContextMenu.Invoke(ContextActionsController.CustomizeCommand);
                Check(app.CustomizationEditor is { IsOpen: true },
                    "The protected context action opens customization with toolbar, sidebar, and palette binding disabled.");
                app.CustomizationEditor!.Dismiss();
            }
            finally { app.SetCustomization(beforeRecovery); }
        });
    }
}
