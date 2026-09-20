namespace Xui.FileExplorer;

internal sealed partial class FilePaneView
{
    private ContentHost? customToolbar;
    private readonly List<(Button Button, ExplorerCommand Command)> customToolbarButtons = [];
    internal Button ToolbarButton(string id) => customToolbarButtons.Single(item => item.Command.StableId == id).Button;
    internal void RefreshCommandAvailability()
    {
        foreach (var (button, command) in customToolbarButtons)
            button.SetEnabled(app.CanExecuteInPane(command, this));
    }
    internal void ApplyCommandSurfaces()
    {
        var options = app.State.Customization;
        layout.ToolbarOpen = options.ShowToolbar;
        string[] defaults = ["back", "forward", "up-to-parent-folder", "refresh-folder", "commands"];
        bool standard = options.ToolbarCommands.SequenceEqual(defaults) && !options.ToolbarLabels;
        foreach (var button in new[] { layout.Back, layout.Forward, layout.Up, layout.Refresh, layout.Commands })
            button.Visible(options.ShowToolbar && standard);
        foreach (var (button, id) in new[] { (layout.Back, "back"), (layout.Forward, "forward"),
            (layout.Up, "up-to-parent-folder"), (layout.Refresh, "refresh-folder"), (layout.Commands, "commands") })
        {
            var command = app.Commands.Single(c => c.StableId == id);
            string hint = app.ShortcutHint(command);
            button.Help(hint.Length == 0 ? command.Name : $"{command.Name} ({hint})");
        }
        status.Visible(options.ShowStatus);
        customToolbar ??= CreateToolbar();
        customToolbarButtons.Clear();
        if (!options.ShowToolbar || standard) { customToolbar.Clear(); return; }
        var update = customToolbar.BeginUpdate();
        try
        {
            var row = window.Stack(Axis.Horizontal).Spacing(4);
            foreach (string id in options.ToolbarCommands)
            {
                var command = app.Commands.FirstOrDefault(c => c.StableId == id);
                if (command is null) continue;
                var button = window.Button(command.Name).Help($"{command.Name} ({app.ShortcutHint(command)})");
                button.SetIcon(id switch
                {
                    "back" => ButtonIcon.Back, "forward" => ButtonIcon.Forward,
                    "up-to-parent-folder" => ButtonIcon.Up, "refresh-folder" => ButtonIcon.Refresh,
                    "new-tab" => ButtonIcon.Add,
                    "toggle-light-dark-theme" => ButtonIcon.Theme,
                    "customization" => ButtonIcon.Settings,
                    "toggle-navigation-pane" or "filter-navigation" => ButtonIcon.Navigation,
                    _ => ButtonIcon.More
                });
                button.SetStyle(ExplorerStyles.IconButton);
                button.FixedSize(options.ToolbarLabels ? 156 : 36, 36);
                button.SetEnabled(app.CanExecuteInPane(command, this));
                customToolbarButtons.Add((button, command));
                // A toolbar content scope must not own resources created by application commands.
                button.Click += () => app.PostToolbarCommand(command, this);
                if (options.ToolbarLabels) button.SetIcon(ButtonIcon.None);
                row.Add(button);
            }
            update.Commit(row);
        }
        catch { update.Dispose(); throw; }
    }
    private ContentHost CreateToolbar()
    {
        var host = window.CreateContentHost();
        layout.Toolbar.Add(host);
        return host;
    }
}
