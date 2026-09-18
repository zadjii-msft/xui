namespace Xui.Designer;

internal enum DesignerCommandId
{
    New = 1, Open, Save, SaveAs, Recovery,
    Undo, Redo, Find, Replace, SelectFromCaret, FocusSource, FocusHierarchy, FocusPalette, FocusProperty,
    Render, LivePreview, PickControls, Fit, Compact, Medium, Wide,
    Output, VisualStyle, Theme, GoToLine, ToggleComment, FocusHierarchySearch, FocusPropertySearch, RevertPropertyDraft,
    RevealPropertySource, DeleteControl, DuplicateControl, MoveControlUp, MoveControlDown,
    WrapVertical, WrapHorizontal, WrapScroll, UnwrapControl,
    SelectParent, SelectFirstChild, SelectPreviousSibling, SelectNextSibling, SelectRoot, ChooseColor,
    FindSelection, FindSelectionNext, FindSelectionPrevious, ExpandHierarchy, CollapseHierarchy, CancelHierarchyExpansion,
    DuplicateSourceLines
}

internal sealed record DesignerCommand(
    DesignerCommandId Id, string Label, Action Execute, string Shortcut = "",
    Func<bool>? CanExecute = null, bool? Checked = null);

internal sealed class DesignerCommandPalette : IDisposable
{
    private readonly Window window;
    private readonly Control anchor;
    private readonly Func<IReadOnlyList<DesignerCommand>> commands;
    private readonly Action<string> report;
    private Dictionary<DesignerCommandId, DesignerCommand> snapshot = [];
    private long generation;
    private bool pending, disposed;

    internal CommandSurface Surface { get; }
    internal bool IsOpen => !disposed && Surface.IsOpen;

    internal DesignerCommandPalette(Window window, Control anchor,
        Func<IReadOnlyList<DesignerCommand>> commands, Action<string> report)
    {
        this.window = window;
        this.anchor = anchor;
        this.commands = commands;
        this.report = report;
        Surface = window.CommandSurface("Designer commands");
        Surface.Editor.SetAutomationId("designer-command-query");
        Surface.CloseButton.SetAutomationId("designer-command-close");
        Surface.OnCommand((id, pin) => Execute((DesignerCommandId)id, pin));
    }

    internal void Show()
    {
        if (disposed) return;
        long request = ++generation;
        Post(() =>
        {
            if (disposed || request != generation) return;
            try
            {
                if (Surface.IsOpen) Surface.Dismiss();
                var entries = commands();
                snapshot = entries.ToDictionary(command => command.Id);
                Surface.SetCommands(entries.Select(command => new Command((ulong)command.Id, command.Label,
                    Enabled: command.CanExecute?.Invoke() ?? true, Checked: command.Checked, ShortcutHint: command.Shortcut)).ToArray());
                Surface.Show(anchor);
            }
            catch (XuiException error) { report($"Could not open Designer commands: {error.Message}"); }
        });
    }

    private void Execute(DesignerCommandId id, bool pin)
    {
        if (disposed) return;
        if (pin || !snapshot.TryGetValue(id, out var command))
        {
            report("The selected Designer command is not available.");
            return;
        }
        if (pending) return;
        pending = true;
        long request = generation;
        Post(() =>
        {
            try
            {
                if (disposed) return;
                if (request != generation)
                {
                    report("Command canceled because the palette reopened.");
                    return;
                }
                if (Surface.IsOpen) Surface.Dismiss();
                if (command.CanExecute is { } available && !available())
                {
                    report("The selected command is no longer available. Open Designer commands again.");
                    return;
                }
                command.Execute();
            }
            catch (XuiException error) { report($"Could not run '{command.Label}': {error.Message}"); }
            finally { pending = false; }
        });
    }

    private void Post(Action action)
    {
        if (!window.Post(action)) throw new InvalidOperationException("The window rejected the Designer command request.");
    }

    public void Dispose()
    {
        if (disposed) return;
        disposed = true;
        generation++;
        snapshot.Clear();
    }
}
