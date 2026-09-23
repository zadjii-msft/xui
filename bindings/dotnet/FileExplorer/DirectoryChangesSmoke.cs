using System.Runtime.InteropServices;
using Xui.FileExplorer.Models;

namespace Xui.FileExplorer;

internal static class DirectoryChangesSmoke
{
    [DllImport("user32.dll", ExactSpelling = true)]
    private static extern nint GetFocus();
    [DllImport("user32.dll", ExactSpelling = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool PostMessageW(nint window, uint message, nuint wparam, nint lparam);

    internal static async Task Run(ExplorerApplication app, Func<Action, Task> ui,
        Func<Func<bool>, Task> wait, string fixture)
    {
        async Task until(Func<bool> predicate)
        {
            try { await wait(predicate); }
            catch (TimeoutException)
            {
                await ui(() =>
                {
                    foreach (var view in new[] { app.Left, app.Right })
                        Console.Error.WriteLine($"Directory smoke: {view.Model.Active.Path}, {view.Model.Active.ViewMode}, " +
                            $"loading={view.IsLoading}, filtering={view.IsFiltering}, rows={string.Join(", ", view.DisplayedPaths())}, error={view.Error}");
                    Console.Error.WriteLine($"Busy={app.Transfers.Busy}, notification={app.Notification.Text}");
                });
                throw;
            }
        }
        static void Check(bool condition, string message)
        {
            if (!condition) throw new InvalidOperationException(message);
        }
        var pane = app.Left;
        string root = Path.Combine(fixture, "changes");
        Directory.CreateDirectory(root);
        await ui(() => pane.Navigate(root));
        await until(() => pane.HasCurrentRows);
        await ui(app.ToggleSplit);
        await until(() => app.SecondPaneVisible && app.Right.HasCurrentRows);

        foreach (var mode in Enum.GetValues<ExplorerViewMode>())
        {
            await ui(() => pane.SetViewMode(mode));
            await until(() => pane.HasCurrentRows);
            string path = Path.Combine(root, $"delete-{mode}.txt");
            File.WriteAllText(path, "disposable deletion fixture");
            await until(() => pane.HasCurrentRows && pane.DisplayedPaths().Contains(path) &&
                app.Right.HasCurrentRows && app.Right.DisplayedPaths().Contains(path));
            await ui(() =>
            {
                pane.SelectPath(path);
                pane.ShowFind();
                Check(!app.Window.KeyHandler!(new(0x2e, KeyModifiers.None, pane.FindInput.Id)),
                    "Delete in the native Find editor was intercepted.");
                Check(File.Exists(path), "Editing Find deleted a selected file.");
                pane.HideFind();
            });
            await until(() => pane.HasCurrentRows);
            await ui(() =>
            {
                pane.SelectPath(path);
                pane.Focus();
                Check(pane.FilesFocused, "The file view did not receive focus.");
                Check(PostMessageW(GetFocus(), 0x100, 0x2e, 1), "Could not post Delete to the native file view.");
            });
            await until(() => !File.Exists(path) && !app.Transfers.Busy &&
                pane.HasCurrentRows && !pane.DisplayedPaths().Contains(path) &&
                app.Right.HasCurrentRows && !app.Right.DisplayedPaths().Contains(path));
            Console.WriteLine($"Delete and both-pane refresh passed: {mode}.");
        }

        await ui(() => pane.SetViewMode(ExplorerViewMode.Columns));
        await until(() => pane.HasCurrentRows);
        string folder = Path.Combine(root, "shell-folder");
        Directory.CreateDirectory(folder);
        File.WriteAllText(Path.Combine(folder, "nested.txt"), "nested deletion fixture");
        await until(() => pane.HasCurrentRows && pane.DisplayedPaths().Contains(folder));
        await ui(() => pane.SelectColumnPath(0, folder));
        await until(() => pane.HasCurrentRows && pane.Model.Active.Columns.Count == 2);
        var retainedColumns = pane.Model.Active.Columns.ToArray();
        string additionalChild = Path.Combine(folder, "additional.txt");
        File.WriteAllText(additionalChild, "column refresh fixture");
        await until(() => pane.HasCurrentRows && pane.DisplayedPaths(1).Contains(additionalChild));
        await ui(() => Check(pane.Model.Active.Columns.SequenceEqual(retainedColumns),
            "Automatic refresh replaced the surviving Columns path."));
        await ui(() =>
        {
            pane.ContextMenu.GetCommands();
            pane.ContextMenu.Invoke(ContextActionsController.SearchCommand);
        });
        var context = pane.ContextMenu.Customization;
        await until(() => !context.Loading);
        await ui(() =>
        {
            Check(context.Entries.Any(entry => entry.Identity == "shell:delete" && entry.Enabled),
                "Windows did not expose Delete for the selected folder.");
            context.SelectResult("shell:delete");
            context.Run();
        });
        await until(() => !Directory.Exists(folder) && !app.Transfers.Busy && pane.HasCurrentRows &&
            pane.Model.Active.Columns.Count == 1 && !pane.DisplayedPaths().Contains(folder) &&
            app.Right.HasCurrentRows && !app.Right.DisplayedPaths().Contains(folder));
        await ui(() => Check(pane.Model.Active.Path == root, "The surviving column did not become the current folder."));

        string external = Path.Combine(root, "external.txt");
        string renamed = Path.Combine(root, "renamed.txt");
        File.WriteAllText(external, "external");
        await until(() => pane.HasCurrentRows && pane.DisplayedPaths().Contains(external));
        File.Move(external, renamed);
        await until(() => pane.HasCurrentRows && pane.DisplayedPaths().Contains(renamed) && !pane.DisplayedPaths().Contains(external));
        File.Delete(renamed);
        await until(() => pane.HasCurrentRows && !pane.DisplayedPaths().Contains(renamed));

        await ui(() => pane.SetViewMode(ExplorerViewMode.Details));
        string first = Path.Combine(root, "first.txt"), second = Path.Combine(root, "second.txt");
        File.WriteAllText(first, "first selection");
        File.WriteAllText(second, "second selection");
        await until(() => pane.HasCurrentRows && pane.DisplayedPaths().Count == 2);
        await ui(() =>
        {
            pane.Grid.SelectAll();
            Check(pane.SelectedEntries.Length == 2, "The multi-selection fixture did not select both files.");
        });
        File.AppendAllText(first, " changed");
        await until(() => pane.HasCurrentRows && pane.Model.Active.Entries.Single(entry => entry.FullPath == first).Size > 15);
        await ui(() =>
        {
            Check(pane.SelectedEntries.Length == 2, "Automatic refresh discarded a surviving multi-selection.");
            pane.Focus();
            Check(app.Window.KeyHandler!(new(0x2e, KeyModifiers.None, pane.Grid.Id)), "Multi-selection Delete was not handled.");
        });
        await until(() => !File.Exists(first) && !File.Exists(second) && !app.Transfers.Busy && pane.HasCurrentRows &&
            pane.DisplayedPaths().Count == 0);

        File.WriteAllText(first, "cancel deletion");
        File.WriteAllText(second, "retain selection");
        await until(() => pane.HasCurrentRows && pane.DisplayedPaths().Count == 2);
        await ui(() =>
        {
            pane.SelectPath(first);
            pane.Focus();
            Check(app.Window.KeyHandler!(new(0x2e, KeyModifiers.None, pane.Grid.Id)), "Pending Delete was not handled.");
            pane.SelectPath(second);
        });
        await until(() => !app.Transfers.Busy);
        await ui(() => Check(File.Exists(first) && File.Exists(second), "A stale Delete command changed the captured selection."));

        await ui(() =>
        {
            app.ToggleSplit();
            pane.Navigate(Path.Combine(fixture, "alpha"));
        });
        await until(() => pane.HasCurrentRows && !app.SecondPaneVisible);
        File.WriteAllText(Path.Combine(root, "hidden.txt"), "hidden pane fixture");
        await Task.Delay(400);
        await ui(() =>
        {
            Check(pane.Model.Active.Path == Path.Combine(fixture, "alpha"), "An obsolete watch navigated the active pane.");
            Check(!app.Right.IsLoading && !app.Right.IsFiltering, "A hidden pane kept refreshing.");
            app.ToggleSplit();
        });
        await until(() => app.Right.HasCurrentRows && app.Right.DisplayedPaths().Contains(Path.Combine(root, "hidden.txt")));
    }
}
