using Xui.FileExplorer;

internal static class ContextActionTests
{
    internal static int Run()
    {
        int assertions = 0;
        void Check(bool condition, string message)
        {
            ++assertions;
            if (!condition) throw new InvalidOperationException(message);
        }
        Check(Enumerable.Range(1, 10).Select(id => ContextActionCatalog.AppIdentity((ulong)id)).Distinct().Count() == 10,
            "App context identities are explicit and unique.");
        Check(ContextActionCatalog.AppIdentity(11) == "app:properties", "Properties has a reserved stable app identity.");
        Check(Enumerable.Range(20, 10).All(id => ContextActionCatalog.AppIdentity((ulong)id) == $"app:archive-{id}"),
            "Reserved archive command IDs have explicit stable identities.");
        Check(ContextActionCatalog.ShellIdentity("OPEN") == "shell:open", "Canonical verb comparison is case insensitive.");
        Check(ContextActionCatalog.ShellIdentity("") is null, "Missing canonical verbs cannot be pinned.");
        Check(ContextActionCatalog.ShellIdentity("bad\nverb") is null, "Control characters cannot enter saved identities.");
        var pinned = new List<string>();
        Check(!ContextActionCatalog.TogglePin(pinned, null) && pinned.Count == 0, "An ephemeral action cannot be persisted.");
        Check(ContextActionCatalog.TogglePin(pinned, "shell:open") && pinned.SequenceEqual(["shell:open"]),
            "Only the canonical identity is persisted.");
        ContextActionEntry[] entries =
        [
            new("app:copy", "Copy files", true, AppId: 6),
            new("shell:open", "Open fixture", true, ShellId: 42, Version: 100),
            new(null, "Extension action", true, ShellId: 9, Version: 100),
            new("shell:disabled", "Disabled fixture", false, ShellId: 10, Version: 100)
        ];
        Check(ContextActionCatalog.Search(entries, "", pinned)[0].ShellId == 42, "Pinned current commands sort first.");
        Check(ContextActionCatalog.Search(entries, " FIXTURE ", pinned).Length == 2, "Search uses real labels, case-insensitively.");
        Check(ContextActionCatalog.Search(entries, "shell:open", pinned).Single().ShellId == 42, "Canonical verbs are searchable.");
        Check(ContextActionCatalog.Search(entries, "", pinned, true).Length == 1, "Favorites exclude unpinned items.");
        Check(!ContextActionCatalog.Search(entries, "Disabled", pinned).Single().Enabled, "Disabled availability survives search.");
        var refreshed = entries.Select(entry => entry with { ShellId = entry.ShellId + 100, Version = 101 }).ToArray();
        Check(ContextActionCatalog.Search(refreshed, "", pinned, true).Single().ShellId == 142,
            "Favorites resolve to this opening's identity, never a persisted menu ID.");
        var duplicate = ContextActionCatalog.UniqueShellIdentities([entries[1], entries[1] with { ShellId = 43 }]);
        Check(duplicate.All(entry => entry.Identity is null && entry.ShellId != 0),
            "Ambiguous canonical verbs remain executable but cannot be pinned.");
        Check(ContextActionCatalog.TogglePin(pinned, "shell:open") && pinned.Count == 0, "Unpin removes the durable identity.");
        var full = Enumerable.Range(0, 256).Select(i => $"shell:verb{i}").ToList();
        Check(!ContextActionCatalog.TogglePin(full, "shell:extra") && full.Count == 256, "Pin count respects state limits.");
        return assertions;
    }
}
