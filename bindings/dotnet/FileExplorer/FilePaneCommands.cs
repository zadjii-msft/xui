namespace Xui.FileExplorer;

internal sealed partial class FilePaneView
{
    private ContentHost? customToolbar;
    private readonly List<(Button Button, ExplorerCommand Command)> customToolbarButtons = [];
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
                    "new-tab" => ButtonIcon.Add, _ => ButtonIcon.More
                });
                button.SetStyle(ExplorerStyles.IconButton);
                button.FixedSize(options.ToolbarLabels ? 156 : 36, 36);
                button.SetEnabled(app.CanExecuteInPane(command, this));
                customToolbarButtons.Add((button, command));
                button.Click += () => { Activate(); app.ExecuteCommand(command); };
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
