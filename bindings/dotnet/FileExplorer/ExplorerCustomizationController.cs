using Xui.FileExplorer.Models;

namespace Xui.FileExplorer;

internal sealed partial class ExplorerApplication
{
    private readonly List<FilePaneView> customizablePanes = [];
    private readonly KeySequenceTracker keySequence = new();
    private CustomizationController? customizationEditor;
    private bool ignoreSavedBindings;
    public event Action? CustomizationChanged;
    internal CustomizationController? CustomizationEditor => customizationEditor;

    public void RegisterCustomizablePane(FilePaneView pane)
    {
        customizablePanes.Add(pane);
        pane.ApplyCustomization();
        if (Commands is not null) pane.ApplyCommandSurfaces();
    }

    public void UnregisterCustomizablePane(FilePaneView pane) => customizablePanes.Remove(pane);

    public string ShortcutHint(ExplorerCommand command) => command.Shortcut == "Alt+F4"
        ? "Alt+F4" : string.Join("; ", Bindings(command));

    internal string[] Bindings(ExplorerCommand command, ExplorerCustomization? options = null)
    {
        options ??= ignoreSavedBindings ? new() : State.Customization;
        if (options.Keybindings.TryGetValue(command.StableId, out var mapped)) return mapped;
        if (command.Shortcut == "Alt+F4") return [];
        var defaults = command.Shortcut.Length == 0 ? new List<string>() : [command.Shortcut];
        if (command.Aliases is not null) defaults.AddRange(command.Aliases);
        switch (command.StableId)
        {
            case "go-to-folder": defaults.AddRange(["Ctrl+G", "Alt+D"]); break;
            case "close-tab": defaults.Add("Ctrl+F4"); break;
            case "copy-files": defaults.Add("Ctrl+Insert"); break;
            case "paste-files-into-this-folder": defaults.Add("Shift+Insert"); break;
        }
        return defaults.ToArray();
    }

    public void ExecuteCommand(ExplorerCommand command)
    {
        if (!command.Enabled) { Report($"'{command.Name}' is not available now."); return; }
        command.Execute();
    }

    internal void PostToolbarCommand(ExplorerCommand command, FilePaneView pane)
    {
        if (!Application.Post(() =>
        {
            if (CloseRequested || IsDisposed) return;
            if (!customizablePanes.Contains(pane) || pane.Model.Tabs.Count == 0)
            {
                Report("The command's pane is no longer available.");
                return;
            }
            Activate(pane);
            ExecuteCommand(command);
        }))
            throw new InvalidOperationException("The application rejected a toolbar command.");
    }

    internal bool CanExecuteInPane(ExplorerCommand command, FilePaneView pane)
    {
        var previous = active;
        active = pane;
        try { return command.Enabled; }
        finally { active = previous; }
    }

    internal void RefreshCommandAvailability()
    {
        foreach (var pane in customizablePanes.Where(pane => pane.Model.Tabs.Count != 0))
            pane.RefreshCommandAvailability();
    }

    internal void ValidateCustomization(ExplorerCustomization options)
    {
        options.Validate();
        if (Commands.Select(c => c.StableId).Distinct(StringComparer.Ordinal).Count() != Commands.Count)
            throw new InvalidDataException("Command identities must be unique.");
        // Unknown identities remain stored so another extension/version can register them later.
        KeybindingValidation.Validate(Commands.Select(command => (command.StableId, (IEnumerable<string>)Bindings(command, options)))
            .Concat(options.Keybindings.Where(pair => !Commands.Any(c => c.StableId == pair.Key))
                .Select(pair => (pair.Key, (IEnumerable<string>)pair.Value))));
    }

    public void SaveCustomization() => SetCustomization(State.Customization.Clone());

    internal void PostCustomization(Action update)
    {
        // Replacing command surfaces is not permitted inside a native control callback.
        if (!Application.Post(() =>
        {
            if (!CloseRequested && !IsDisposed) update();
        }))
            throw new InvalidOperationException("The application rejected a settings update.");
    }

    public void SetCustomization(ExplorerCustomization options)
    {
        options = options.Clone();
        ValidateCustomization(options);
        if (!stateWritable && !smoke)
            throw new InvalidDataException("Saved state is unavailable. Repair state.json before changing preferences.");
        var previous = State.Customization;
        State.Customization = options;
        try
        {
            ApplyCustomization();
            if (!smoke) store.Save(State);
        }
        catch (Exception error)
        {
            State.Customization = previous;
            try { ApplyCustomization(); }
            catch (Exception rollbackError)
            {
                throw new AggregateException("Customization failed and the previous presentation could not be restored.", error, rollbackError);
            }
            throw;
        }
    }

    private void ApplyCustomization()
    {
        keySequence.Reset();
        ExplorerPresentation.Apply(Window, State.Customization);
        foreach (var pane in customizablePanes.Where(pane => pane.Model.Tabs.Count != 0))
        {
            pane.ApplyCustomization();
            pane.ApplyCommandSurfaces();
        }
        Sidebar.ApplyCustomization();
        CustomizationChanged?.Invoke();
    }

    internal void ShowCustomization()
    {
        customizationEditor!.Show();
    }

    private bool HandleCustomizationKey(UiKeyEvent key)
    {
        if (customizationEditor?.IsOpen == true)
        {
            if (key.VirtualKey == 0x1b) customizationEditor.Dismiss();
            return key.VirtualKey == 0x1b;
        }
        bool fileFocus = Active.FilesFocused;
        var stroke = new KeyGesture(key.VirtualKey, key.Modifiers.HasFlag(KeyModifiers.Control),
            key.Modifiers.HasFlag(KeyModifiers.Shift), key.Modifiers.HasFlag(KeyModifiers.Alt));
        if (stroke.Control && stroke.Alt) { keySequence.Reset(); return false; }
        var available = Commands.Where(command =>
        {
            bool fileCommand = command.StableId.StartsWith("archive.", StringComparison.Ordinal) ||
                command.StableId.StartsWith("file.", StringComparison.Ordinal) ||
                command.StableId is "copy-files" or "cut-files" or "delete-files" or "copy-file-paths" or
                    "paste-files-into-this-folder" or "properties" or "preview-selected-item" or
                    "open-selected-folder-in-new-tab" or "open-selected-item-in-other-pane" or "tab.open-selected-folders";
            return command.Enabled && (fileFocus || (!fileCommand && !State.Customization.Keybindings.ContainsKey(command.StableId)));
        });
        var result = keySequence.Match(stroke, available.Select(c => (c.StableId, Bindings(c))),
            fileFocus, Environment.TickCount64);
        if (result.Command is { } id) ExecuteCommand(Commands.Single(c => c.StableId == id));
        return result.Handled;
    }
}
