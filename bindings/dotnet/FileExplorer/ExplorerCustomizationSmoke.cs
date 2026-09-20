using Xui.FileExplorer.Models;
using System.Runtime.InteropServices;

namespace Xui.FileExplorer;

internal static class ExplorerCustomizationSmoke
{
    [DllImport("user32.dll", ExactSpelling = true)]
    private static extern nint GetFocus();
    [DllImport("user32.dll", EntryPoint = "SendMessageW", ExactSpelling = true, CharSet = CharSet.Unicode)]
    private static extern nint SendMessageTextW(nint window, uint message, nuint wparam, string text);
    [DllImport("user32.dll", EntryPoint = "PostMessageW", ExactSpelling = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool PostMessageKeyW(nint window, uint message, nuint wparam, nint lparam);

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
        var editor = app.CustomizationEditor!;
        var shortcut = editor.Row("key:new-tab");
        var font = editor.Row("font");
        var date = editor.Row("date");
        var section = editor.Row("section:tree");
        var toolbar = editor.Row("toolbar:new-tab");
        string previousDate = "";
        nint textFocus = 0;
        async Task Change(Action action, Action check)
        {
            await ui(action);
            await ui(check);
        }
        await ui(() =>
        {
            Check(app.Left.Items.SingleClickActivation && app.Right.Items.SingleClickActivation,
                "Preferences did not reach both panes.");
            Check(app.Left.FindReveal.Duration == 0 && app.Right.FindReveal.Duration == 0,
                "Animation preference did not reach both panes.");
            app.ShowCustomization();
            editor.SearchInput.Focus(selectAll: true);
            SendMessageTextW(GetFocus(), 0x000c, 0, "Keyboard: New tab");
            Check(editor.ResultCount == 1, "The settings search did not filter keyboard commands.");
        });
        await Change(() =>
        {
            shortcut.Text!.Text = "Ctrl+Shift+F12";
            shortcut.Save!.Invoke();
        }, () =>
        {
            Check(app.State.Customization.Keybindings["new-tab"].SequenceEqual(["Ctrl+Shift+F12"]),
                "The keybinding editor did not apply its native input.");
        });
        await Change(() =>
        {
            shortcut.Text!.Text = "Ctrl+W";
            shortcut.Save!.Invoke();
        }, () =>
        {
            Check(editor.Message.Contains("conflict", StringComparison.OrdinalIgnoreCase),
                "The editor did not show the shortcut conflict.");
            Check(app.State.Customization.Keybindings["new-tab"].SequenceEqual(["Ctrl+Shift+F12"]),
                "The editor changed settings after a conflict.");
            Check(shortcut.Text!.Text == "Ctrl+W" && shortcut.Error.Length > 0,
                "A rejected shortcut must retain its draft and show a row error.");
            shortcut.Reset.Invoke();
            Check(!app.State.Customization.Keybindings.ContainsKey("new-tab") && shortcut.Error.Length == 0,
                "The inline shortcut reset did not restore defaults.");
            editor.SearchInput.Focus(selectAll: true);
            SendMessageTextW(GetFocus(), 0x000c, 0, "Appearance:");
            Check(editor.ResultCount == 8, "Appearance search did not show the inline settings.");
            font.Text!.Text = "Consolas";
        });
        await Change(() =>
        {
            editor.Row("animations").Toggle!.Invoke();
        }, () =>
        {
            Check(app.State.Customization.Animations && font.Text!.Text == "Consolas" &&
                app.State.Customization.FontFamily != "Consolas", "A toggle discarded or saved an unrelated font draft.");
        });
        await Change(() =>
        {
            editor.Row("font-size").Number!.ChangeValue(18);
        }, () =>
        {
            Check(app.State.Customization.FontSize == 18, "The font stepper did not save.");
        });
        await Change(() =>
        {
            editor.Row("density").Slider!.ChangeValue(38);
        }, () =>
        {
            Check(app.State.Customization.RowHeight == 38, "The spacing slider did not save.");
        });
        await Change(() =>
        {
            var theme = editor.Row("theme").Choice!;
            theme.Popup.Show(theme);
            Check(theme.Popup.IsOpen && editor.IsOpen, "Opening an inline dropdown closed the settings editor.");
            theme.Select(3);
            theme.Popup.Dismiss();
            Check(editor.IsOpen, "Closing an inline dropdown closed the settings editor.");
        }, () =>
        {
            Check(app.State.Customization.Theme == "dark", "The theme dropdown did not save.");
        });
        await Change(() =>
        {
            editor.Row("fill").Choice!.Select(2);
        }, () =>
        {
            Check(app.State.Customization.ThumbnailFill, "The thumbnail dropdown did not save.");
        });
        await until(() => app.CustomizationEditor!.Row("font").Text!.GetBounds().Height > 20);
        await ui(() =>
        {
            Check(font.Text!.Text == "Consolas", "The retained font draft changed during layout.");
            font.Text.Focus(selectAll: true);
            Check(font.Text.Focused, "The visible inline font field did not receive native focus.");
            textFocus = GetFocus();
            Check(!app.Window.KeyHandler!(new(0x41, KeyModifiers.Control, font.Text.Id)) &&
                !app.Window.KeyHandler!(new(0x41, KeyModifiers.Control | KeyModifiers.Alt, font.Text.Id)),
                "Inline text fields intercepted native editing or AltGr.");
            editor.Row("smooth").Toggle!.Invoke();
        });
        await ui(() =>
        {
            Check(GetFocus() == textFocus && font.Text!.Text == "Consolas",
                "An unrelated settings update changed native text focus or draft.");
            Check(PostMessageKeyW(GetFocus(), 0x0100, 0x0d, 1), "Could not submit the native font editor.");
        });
        await until(() => app.State.Customization.FontFamily == "Consolas");
        await Change(() =>
        {
            date.Text!.Text = "%";
            previousDate = app.State.Customization.DateFormat;
            date.Save!.Invoke();
        }, () =>
        {
            Check(date.Error.Length > 0 && date.Text!.Text == "%" && app.State.Customization.DateFormat == previousDate,
                "Invalid date formats must retain the draft without changing settings.");
            editor.SearchInput.Focus(selectAll: true);
            SendMessageTextW(GetFocus(), 0x000c, 0, "no-such-setting");
            Check(editor.ResultCount == 0 && !date.Root.Open, "Empty search did not hide all settings.");
            SendMessageTextW(GetFocus(), 0x000c, 0, "date");
            Check(date.Root.Open && date.Text!.Text == "%", "Search discarded an invalid draft.");
            date.Reset.Invoke();
            Check(date.Text!.Text == new ExplorerCustomization().DateFormat && date.Error.Length == 0,
                "Per-setting reset did not clear the invalid draft.");
            section.Reset.Invoke();
            Check(app.State.Customization.SidebarSections.SequenceEqual(["bookmarks", "recents", "tree"]),
                "Resetting a section must work when earlier default sections are hidden.");
        });
        await Change(() => section.Number!.ChangeValue(1), () =>
        {
            Check(app.State.Customization.SidebarSections[0] == "tree", "The inline section order did not save.");
        });
        await Change(() => section.Toggle!.Invoke(), () =>
        {
            Check(!app.State.Customization.SidebarSections.Contains("tree"), "The section visibility switch did not save.");
        });
        await Change(() => section.Toggle!.Invoke(), () =>
        {
            Check(app.State.Customization.SidebarSections[^1] == "tree", "The section visibility switch did not restore the section.");
        });
        await Change(() => toolbar.Number!.ChangeValue(1), () =>
        {
            Check(app.State.Customization.ToolbarCommands[0] == "new-tab" &&
                editor.Row("toolbar:customization").Number!.Value == 2, "Command position rows did not stay synchronized.");
        });
        await Change(() => toolbar.Toggle!.Invoke(), () =>
        {
            Check(!app.State.Customization.ToolbarCommands.Contains("new-tab"), "The toolbar visibility switch did not save.");
            editor.Dismiss();
            app.ShowCustomization();
            Check(editor.Row("font-size").Number!.Value == 18 && editor.Row("theme").Choice is not null,
                "Reopening settings lost saved inline values.");
            editor.SearchInput.Focus(selectAll: true);
            SendMessageTextW(GetFocus(), 0x000c, 0, "font");
        });
        await until(() => app.CustomizationEditor!.Row("font-size").Number!.Editor.GetBounds().Height > 20);
        await ui(() =>
        {
            var number = editor.Row("font-size");
            var fieldBounds = font.Text!.GetBounds();
            var buttonBounds = font.Save!.GetBounds();
            Check(fieldBounds.Width > 100 && fieldBounds.X + fieldBounds.Width <= buttonBounds.X,
                "The inline text field overlaps its Save button.");
            Check(number.Number!.IncreaseButton.GetBounds().Height >= 24 &&
                number.Number.DecreaseButton.GetBounds().Height >= 24,
                "The inline number stepper has clipped spin buttons.");
            Check(number.Root.GetBounds().Y >= font.Root.GetBounds().Y + font.Root.GetBounds().Height,
                "Inline settings rows overlap.");
            number.Number.ChangeValue(32);
        });
        await until(() => app.CustomizationEditor!.Row("font-size").Number!.Editor.GetBounds().Height >= 40);
        await ui(() =>
        {
            Check(editor.Row("font-size").Number!.IncreaseButton.GetBounds().Height >= 32,
                "The number stepper did not grow for the largest supported font.");
            Check(app.Window.KeyHandler!(new(0x1b, KeyModifiers.None, 0)), "Customization editor did not dismiss.");
            app.SetCustomization(new());
        });
    }
}
