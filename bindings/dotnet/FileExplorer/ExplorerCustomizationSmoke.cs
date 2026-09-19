using Xui.FileExplorer.Models;
using System.Runtime.InteropServices;

namespace Xui.FileExplorer;

internal static class ExplorerCustomizationSmoke
{
    [DllImport("user32.dll", ExactSpelling = true)]
    private static extern nint GetFocus();
    [DllImport("user32.dll", EntryPoint = "SendMessageW", ExactSpelling = true, CharSet = CharSet.Unicode)]
    private static extern nint SendMessageTextW(nint window, uint message, nuint wparam, string text);

    internal static async Task Run(ExplorerApplication app, Func<Action, Task> ui, Func<Func<bool>, Task> until, string fixture)
    {
        static void Check(bool value, string message) { if (!value) throw new InvalidOperationException(message); }
        await ui(() => app.Left.Navigate(fixture));
        await until(() => !app.Left.IsLoading && !app.Left.IsFiltering);
        await ui(() =>
        {
            var original = app.State.Customization.Clone();
            var changed = original.Clone();
            changed.Keybindings["new-tab"] = ["Ctrl+K, Ctrl+T", "Ctrl+Shift+F12"];
            app.SetCustomization(changed);
            app.Left.Focus();
            int tabs = app.Left.Model.Tabs.Count;
            Check(app.Window.KeyHandler!(new(0x4b, KeyModifiers.Control, app.Left.Grid.Id)), "Sequence prefix was not handled.");
            Check(app.Left.Model.Tabs.Count == tabs, "Sequence prefix executed a command.");
            Check(app.Window.KeyHandler!(new(0x54, KeyModifiers.Control, app.Left.Grid.Id)), "Sequence did not execute.");
            Check(app.Left.Model.Tabs.Count == tabs + 1, "Sequence did not create a tab.");
            Check(app.ShortcutHint(app.Commands.Single(c => c.StableId == "new-tab")).Contains("Ctrl+K, Ctrl+T"),
                "Palette hints must show remapped sequences.");
            bool executed = false;
            app.ExecuteCommand(new("Unavailable", "", () => executed = true, () => false));
            Check(!executed, "Disabled command was executed.");
            var conflicting = changed.Clone();
            conflicting.Keybindings["close-tab"] = ["Ctrl+K"];
            try { app.SetCustomization(conflicting); throw new InvalidOperationException("Conflicting shortcut accepted."); }
            catch (InvalidDataException) { }
            Check(app.State.Customization.ToJson() == changed.ToJson(), "Failed import changed preferences.");
            app.Left.ShowFind();
            foreach (var key in new uint[] { 0x43, 0x56, 0x58, 0x41, 0x5a, 0x59, 0x4b })
                Check(!app.Window.KeyHandler!(new(key, KeyModifiers.Control, app.Left.FindInput.Id)), "Native editor shortcut was stolen.");
            Check(!app.Window.KeyHandler!(new(0x41, KeyModifiers.Control | KeyModifiers.Alt, app.Left.FindInput.Id)),
                "AltGr text was stolen.");
            app.SetCustomization(original);
        });
        await until(() => !app.Left.IsLoading && !app.Left.IsFiltering);
        await ui(() =>
        {
            app.Left.HideFind();
            app.Left.Focus();
            Check(!app.Window.KeyHandler!(new(0x1b, KeyModifiers.None, app.Left.Grid.Id)),
                "Escape without Find must remain available to native menus.");
            Check(!app.Window.KeyHandler!(new(0x41, KeyModifiers.None, app.Left.Grid.Id) { IsTextInput = true }),
                "Text-producing input must continue to the native editor.");
            Check(app.Left.Model.Active.FindOpen, "Native type-to-Find was not opened.");
            app.Left.HideFind();
            var options = app.State.Customization.Clone();
            options.ToolbarLabels = true;
            options.ToolbarCommands = ["customization", "new-tab", "back"];
            options.SidebarCommands = ["customization"];
            options.SidebarSections = ["bookmarks", "recents"];
            options.Animations = false;
            options.RowHeight = 42;
            options.FontSize = 16;
            options.Theme = "light";
            options.SingleClick = true;
            app.SetCustomization(options);
            app.ToggleSplit();
        });
        await until(() => !app.Right.IsLoading && !app.Right.IsFiltering);
        await ui(() =>
        {
            Check(app.Left.Items.SingleClickActivation && app.Right.Items.SingleClickActivation,
                "Preferences did not reach both panes.");
            Check(app.Left.FindReveal.Duration == 0 && app.Right.FindReveal.Duration == 0,
                "Animation preference did not reach both panes.");
            app.ShowCustomization();
            var editor = app.CustomizationEditor!;
            editor.SearchInput.Focus(selectAll: true);
            SendMessageTextW(GetFocus(), 0x000c, 0, "Keyboard: New tab");
            Check(editor.ResultCount == 1, "The settings search did not filter keyboard commands.");
            editor.ValueInput.Text = "Ctrl+Shift+F12";
            editor.ApplyButton.Invoke();
            Check(app.State.Customization.Keybindings["new-tab"].SequenceEqual(["Ctrl+Shift+F12"]),
                "The keybinding editor did not apply its native input.");
            editor.ValueInput.Text = "Ctrl+W";
            editor.ApplyButton.Invoke();
            Check(editor.Message.Contains("conflict", StringComparison.OrdinalIgnoreCase),
                "The editor did not show the shortcut conflict.");
            Check(app.State.Customization.Keybindings["new-tab"].SequenceEqual(["Ctrl+Shift+F12"]),
                "The editor changed settings after a conflict.");
            Check(app.Window.KeyHandler!(new(0x1b, KeyModifiers.None, 0)), "Customization editor did not dismiss.");
            app.SetCustomization(new());
        });
    }
}
