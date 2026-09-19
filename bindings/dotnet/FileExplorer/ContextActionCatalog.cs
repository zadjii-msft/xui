namespace Xui.FileExplorer;

internal sealed record ContextActionEntry(string? Identity, string Label, bool Enabled,
    ulong AppId = 0, ulong ShellId = 0, ulong Version = 0);

internal static class ContextActionCatalog
{
    internal static string AppIdentity(ulong id) => id switch
    {
        1 => "app:open", 2 => "app:new-tab", 3 => "app:other-pane", 4 => "app:bookmark",
        5 => "app:refresh", 6 => "app:copy", 7 => "app:cut", 8 => "app:paste",
        9 => "app:copy-paths", 10 => "app:preview", 11 => "app:properties",
        20 => "app:archive-20", 21 => "app:archive-21", 22 => "app:archive-22",
        23 => "app:archive-23", 24 => "app:archive-24", 25 => "app:archive-25",
        26 => "app:archive-26", 27 => "app:archive-27", 28 => "app:archive-28",
        29 => "app:archive-29",
        _ => throw new ArgumentOutOfRangeException(nameof(id))
    };

    internal static string? ShellIdentity(string verb) =>
        string.IsNullOrWhiteSpace(verb) || verb.Length > 250 || verb.Any(char.IsControl)
            ? null : "shell:" + verb.ToLowerInvariant();

    internal static ContextActionEntry[] Search(IEnumerable<ContextActionEntry> entries, string query,
        IReadOnlyCollection<string> pinned, bool favoritesOnly = false) =>
        entries.Where(entry => (!favoritesOnly || entry.Identity is { } id && pinned.Contains(id)) &&
            (entry.Label.Contains(query.Trim(), StringComparison.OrdinalIgnoreCase) ||
             entry.Identity?.Contains(query.Trim(), StringComparison.OrdinalIgnoreCase) == true))
        .OrderByDescending(entry => entry.Identity is { } id && pinned.Contains(id)).ToArray();

    internal static ContextActionEntry[] UniqueShellIdentities(IEnumerable<ContextActionEntry> entries)
    {
        var values = entries.ToArray();
        var duplicates = values.Where(entry => entry.Identity is not null).GroupBy(entry => entry.Identity)
            .Where(group => group.Count() > 1).Select(group => group.Key).ToHashSet();
        return values.Select(entry => duplicates.Contains(entry.Identity) ? entry with { Identity = null } : entry).ToArray();
    }

    internal static bool TogglePin(List<string> pinned, string? identity)
    {
        if (identity is null) return false;
        if (!pinned.Remove(identity))
        {
            if (pinned.Count >= 256) return false;
            pinned.Add(identity);
        }
        return true;
    }
}
