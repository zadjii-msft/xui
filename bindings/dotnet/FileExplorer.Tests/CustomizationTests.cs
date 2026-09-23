using Xui.FileExplorer.Models;

internal static class CustomizationTests
{
    public static int Run(string fixture)
    {
        int count = 0;
        void Check(bool condition) { ++count; if (!condition) throw new InvalidOperationException($"Customization assertion {count}"); }
        void Invalid(Action action)
        {
            try { action(); }
            catch (InvalidDataException) { ++count; return; }
            throw new InvalidOperationException("Expected invalid customization.");
        }
        var defaults = ExplorerCustomization.Parse("{}");
        Check(defaults.ShowToolbar && defaults.ShowSidebar && defaults.HomeRecentFiles && defaults.HomePinnedLocations && defaults.HomeDriveCapacity);
        Check(defaults.Theme == "system" && defaults.ToolbarCommands.Count == 5);
        var settings = defaults.Clone();
        settings.Keybindings["refresh-folder"] = ["Ctrl+K, Ctrl+R", "Ctrl+Shift+R"];
        settings.SidebarCommands = ["customization", "refresh-folder"];
        settings.ToolbarCommands.Reverse();
        settings.ShowStatus = false;
        settings.FontFamily = "Arial";
        settings.RowHeight = 44;
        settings.DateFormat = "g";
        Check(ExplorerCustomization.Parse(settings.ToJson()).ToJson() == settings.ToJson());
        Check(!ReferenceEquals(settings.Keybindings, settings.Clone().Keybindings));
        Invalid(() => ExplorerCustomization.Parse("{\"Version\":2}"));
        Invalid(() => ExplorerCustomization.Parse("{\"FontSize\":0}"));
        Invalid(() => ExplorerCustomization.Parse("{\"RowHeight\":1000}"));
        Invalid(() => ExplorerCustomization.Parse("{\"Theme\":\"purple\"}"));
        Invalid(() => ExplorerCustomization.Parse("{\"DateFormat\":\"%\"}"));
        Invalid(() => ExplorerCustomization.Parse("{\"ToolbarCommands\":null}"));
        Invalid(() => ExplorerCustomization.Parse("{\"ToolbarCommands\":[\"back\",\"back\"]}"));
        Invalid(() => ExplorerCustomization.Parse("{\"Keybindings\":{\"x\":null}}"));
        Invalid(() => ExplorerCustomization.Parse("{\"UnknownFutureField\":true}"));
        Invalid(() => ExplorerCustomization.Parse("not-json"));
        Invalid(() => ExplorerCustomization.Parse("null"));
        foreach (string invalid in new[] { "Ctrl+Alt+R", "Alt+F4", "Alt+Tab", "Ctrl+Escape", "A", "Shift+B", "Bogus+F3", "Ctrl+F99", "Ctrl+Ctrl+R", "Ctrl+K, Ctrl+R, Ctrl+X, Ctrl+F" })
            Invalid(() => KeyGesture.ParseSequence(invalid));
        Check(KeyGesture.Parse("Control+PgUp") == KeyGesture.Parse("Ctrl+PageUp"));
        Check(KeyGesture.Parse("Shift+Control+R").ToString() == "Ctrl+Shift+R");
        Invalid(() => KeybindingValidation.Validate([("a", ["Ctrl+R"]), ("b", ["Control+R"])]));
        Invalid(() => KeybindingValidation.Validate([("a", ["Ctrl+K"]), ("b", ["Ctrl+K, Ctrl+R"])]));
        Invalid(() => KeybindingValidation.Validate([("a", ["Ctrl+K, Ctrl+R"]), ("b", ["Ctrl+K"])]));
        KeybindingValidation.Validate([("a", ["Ctrl+K, Ctrl+R"]), ("b", ["Ctrl+K, Ctrl+T"])]);
        var tracker = new KeySequenceTracker();
        (string Id, string[] Bindings)[] bindings = [("refresh", ["Ctrl+K, Ctrl+R"]), ("find", ["Ctrl+F"])];
        Check(tracker.Match(KeyGesture.Parse("Ctrl+K"), bindings, true, 10) == (true, null));
        Check(tracker.PendingCount == 1);
        Check(tracker.Match(KeyGesture.Parse("Ctrl+R"), bindings, true, 100) == (true, "refresh"));
        Check(tracker.PendingCount == 0);
        tracker.Match(KeyGesture.Parse("Ctrl+K"), bindings, true, 200);
        Check(tracker.Match(KeyGesture.Parse("Ctrl+R"), bindings, true, 2200) == (false, null));
        tracker.Match(KeyGesture.Parse("Ctrl+K"), bindings, true, 2300);
        Check(tracker.Match(KeyGesture.Parse("Escape"), bindings, true, 2400) == (true, null));
        tracker.Match(KeyGesture.Parse("Ctrl+K"), bindings, true, 2500);
        Check(tracker.Match(KeyGesture.Parse("Z"), bindings, true, 2600) == (false, null));
        Check(tracker.PendingCount == 0);
        Check(tracker.Match(KeyGesture.Parse("Ctrl+K"), bindings, false, 2700) == (false, null));
        tracker.Match(KeyGesture.Parse("Ctrl+K"), bindings, true, 2800);
        Check(tracker.Match(KeyGesture.Parse("Ctrl+R"), bindings, false, 2900) == (false, null));
        Check(tracker.Match(KeyGesture.Parse("Ctrl+F"), bindings, false, 3000) == (true, "find"));
        string path = Path.Combine(fixture, "customization-state.json");
        File.WriteAllText(path, "{\"Bookmarks\":[],\"Recents\":[]}");
        var store = new AppStateStore(path);
        Check(store.Load().Customization.HomeRecentFiles);
        var state = new ExplorerState { Customization = settings };
        state.Bookmarks.Add(fixture);
        store.Save(state);
        Check(store.Load().Customization.ToJson() == settings.ToJson());
        Check(store.Load().Bookmarks.SequenceEqual([fixture]));
        string before = File.ReadAllText(path);
        state.Customization.FontSize = 1;
        Invalid(() => store.Save(state));
        Check(File.ReadAllText(path) == before);
        File.WriteAllText(path, "{\"Bookmarks\":[],\"Recents\":[],\"Customization\":{\"Version\":99}}");
        Invalid(() => store.Load());
        Invalid(() => store.Save(new()));
        Check(File.ReadAllText(path).Contains("99"));
        return count;
    }
}
