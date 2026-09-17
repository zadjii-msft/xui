using System.Globalization;
using Xui.FileExplorer.Models;

namespace Xui.FileExplorer;

internal sealed class FileRows : IReadOnlyImmutableSource
{
    private readonly IReadOnlyList<FileEntry> entries;
    private readonly Dictionary<ulong, int> indices = [];
    private readonly ItemKey[] keys;
    private readonly bool suggestions;

    public FileRows(IReadOnlyList<FileEntry> entries, Func<string, ulong> identify, bool suggestions = false)
    {
        this.entries = entries;
        this.suggestions = suggestions;
        keys = new ItemKey[entries.Count];
        for (int i = 0; i < entries.Count; i++)
        {
            keys[i] = new(identify(entries[i].FullPath));
            indices.Add(keys[i].Id, i);
        }
    }

    public ulong Count => (ulong)entries.Count;
    public bool HasChildren(ItemKey key) => Entry(key.Id)?.IsDirectory == true;
    public ItemKey Key(ulong index) => keys[checked((int)index)];
    public ulong? Find(ItemKey key) => indices.TryGetValue(key.Id, out int index) ? (ulong)index : null;
    public FileEntry? Entry(ulong id) => indices.TryGetValue(id, out int index) ? entries[index] : null;
    public FileEntry EntryAt(ulong index) => entries[checked((int)index)];
    public ItemKey? KeyForPath(string? path)
    {
        if (path is null) return null;
        for (int i = 0; i < entries.Count; i++)
            if (string.Equals(entries[i].FullPath, path, StringComparison.OrdinalIgnoreCase)) return keys[i];
        return null;
    }

    public ItemContent Item(ulong index, ulong column = 0)
    {
        var entry = entries[checked((int)index)];
        if (suggestions) return new(entry.IsDirectory ? entry.Name + Path.DirectorySeparatorChar : entry.Name,
            entry.IsDirectory ? "Folder" : entry.Kind, Icon: entry.IsDirectory ? ButtonIcon.Folder : ButtonIcon.Library,
            ImagePath: entry.FullPath);
        return column switch
        {
            0 => new(entry.Name, Icon: entry.IsDirectory ? ButtonIcon.Folder : ButtonIcon.Library, ImagePath: entry.FullPath),
            1 => new(entry.ModifiedUtc.ToLocalTime().ToString("yyyy-MM-dd HH:mm", CultureInfo.CurrentCulture)),
            2 => new(entry.Kind),
            3 => new(entry.IsDirectory ? "" : FormatSize(entry.Size)),
            _ => new("")
        };
    }

    internal static string FormatSize(long size) => size switch
    {
        < 1024 => $"{size:N0} B",
        < 1024 * 1024 => $"{size / 1024.0:N1} KB",
        < 1024L * 1024 * 1024 => $"{size / (1024.0 * 1024):N1} MB",
        _ => $"{size / (1024.0 * 1024 * 1024):N1} GB"
    };
}
