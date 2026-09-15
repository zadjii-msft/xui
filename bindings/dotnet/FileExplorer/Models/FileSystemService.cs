namespace Xui.FileExplorer.Models;

public sealed record FileEntry(string FullPath, string Name, bool IsDirectory, long Size, DateTime ModifiedUtc)
{
    public string Kind => IsDirectory ? "Folder" :
        System.IO.Path.GetExtension(Name) is { Length: > 0 } extension
            ? $"{extension[1..].ToUpperInvariant()} file"
            : "File";
}

public sealed record DirectorySnapshot(string Path, IReadOnlyList<FileEntry> Entries);

public sealed record NavigationSuggestions(DirectorySnapshot Snapshot, IReadOnlyList<FileEntry> Entries)
{
    public string Directory => Snapshot.Path;
}

public sealed class FileSystemService
{
    public static string ResolvePath(string path, string basePath)
        => ResolvePath(path, basePath, out _);

    private static string ResolvePath(string path, string basePath, out bool directoryQuery)
    {
        ArgumentNullException.ThrowIfNull(path);
        ArgumentException.ThrowIfNullOrWhiteSpace(basePath);
        if (path.Contains('\0') || basePath.Contains('\0'))
            throw new ArgumentException("A directory path must not contain null characters.");
        path = path.Trim();
        if (path.Length >= 2 && ((path[0] == '"' && path[^1] == '"') ||
                               (path[0] == '\'' && path[^1] == '\'')))
        {
            path = path[1..^1];
        }
        path = Environment.ExpandEnvironmentVariables(path);
        if (path == "~" || path.StartsWith(@"~\", StringComparison.Ordinal) ||
            path.StartsWith("~/", StringComparison.Ordinal))
        {
            path = System.IO.Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.UserProfile),
                path.Length > 1 ? path[2..] : "");
        }
        directoryQuery = path.Length == 0 || System.IO.Path.EndsInDirectorySeparator(path);
        return System.IO.Path.TrimEndingDirectorySeparator(System.IO.Path.GetFullPath(
            path.Length == 0 ? "." : path, System.IO.Path.GetFullPath(basePath)));
    }

    public Task<DirectorySnapshot> ReadDirectoryAsync(string path, string basePath, CancellationToken cancellationToken)
        => Task.Run(() =>
        {
            cancellationToken.ThrowIfCancellationRequested();
            var resolved = ResolvePath(path, basePath);
            var entries = ReadEntries(resolved, cancellationToken);
            cancellationToken.ThrowIfCancellationRequested();
            return new DirectorySnapshot(resolved, entries);
        }, cancellationToken);

    public Task<NavigationSuggestions> SuggestAsync(string query, string basePath, CancellationToken cancellationToken)
        => Task.Run(() =>
        {
            cancellationToken.ThrowIfCancellationRequested();
            var (directory, term) = SuggestionTarget(query, basePath);
            var snapshot = new DirectorySnapshot(directory, ReadEntries(directory, cancellationToken));
            var suggestions = FilterSuggestions(snapshot, term);
            cancellationToken.ThrowIfCancellationRequested();
            return suggestions;
        }, cancellationToken);

    public static NavigationSuggestions? SuggestFromSnapshot(DirectorySnapshot snapshot, string query, string basePath)
    {
        var (directory, term) = SuggestionTarget(query, basePath);
        if (!StringComparer.OrdinalIgnoreCase.Equals(directory, snapshot.Path))
            return null;
        return FilterSuggestions(snapshot, term);
    }

    private static (string Directory, string Term) SuggestionTarget(string query, string basePath)
    {
        var resolved = ResolvePath(query, basePath, out bool directoryQuery);
        if (directoryQuery) return (resolved, "");
        var parent = System.IO.Path.GetDirectoryName(resolved);
        return parent is null ? (resolved, resolved) : (parent, System.IO.Path.GetFileName(resolved));
    }

    private static NavigationSuggestions FilterSuggestions(DirectorySnapshot snapshot, string term) => new(snapshot,
        snapshot.Entries
            .Where(entry => entry.Name.Contains(term, StringComparison.OrdinalIgnoreCase))
            .OrderBy(entry => entry.Name.StartsWith(term, StringComparison.OrdinalIgnoreCase) ? 0 : 1)
            .ThenBy(entry => entry.IsDirectory ? 0 : 1)
            .ThenBy(entry => entry.Name, StringComparer.OrdinalIgnoreCase)
            .ThenBy(entry => entry.FullPath, StringComparer.Ordinal)
            .ToArray());

    public static IReadOnlyList<FileEntry> FilterAndSort(
        IReadOnlyList<FileEntry> entries, string filter, int column, bool descending)
    {
        ArgumentNullException.ThrowIfNull(entries);
        ArgumentNullException.ThrowIfNull(filter);
        if (column is < 0 or > 3)
            throw new ArgumentOutOfRangeException(nameof(column));
        var result = entries.Where(entry => entry.Name.Contains(filter, StringComparison.OrdinalIgnoreCase)).ToArray();
        Array.Sort(result, (left, right) =>
        {
            var folders = right.IsDirectory.CompareTo(left.IsDirectory);
            if (folders != 0)
                return folders;
            var comparison = column switch
            {
                1 => left.ModifiedUtc.CompareTo(right.ModifiedUtc),
                2 => StringComparer.OrdinalIgnoreCase.Compare(left.Kind, right.Kind),
                3 => left.Size.CompareTo(right.Size),
                _ => StringComparer.OrdinalIgnoreCase.Compare(left.Name, right.Name)
            };
            if (comparison != 0)
                return descending ? -Math.Sign(comparison) : comparison;
            comparison = StringComparer.OrdinalIgnoreCase.Compare(left.Name, right.Name);
            return comparison != 0 ? comparison : StringComparer.Ordinal.Compare(left.FullPath, right.FullPath);
        });
        return result;
    }

    private static IReadOnlyList<FileEntry> ReadEntries(string path, CancellationToken cancellationToken)
    {
        var result = new List<FileEntry>();
        // Do not skip inaccessible entries: callers must see incomplete-read failures.
        foreach (var info in new DirectoryInfo(path).EnumerateFileSystemInfos("*", new EnumerationOptions
                 {
                     RecurseSubdirectories = false,
                     IgnoreInaccessible = false,
                     AttributesToSkip = 0,
                     ReturnSpecialDirectories = false
                 }))
        {
            cancellationToken.ThrowIfCancellationRequested();
            info.Refresh();
            if (!info.Exists)
                throw new IOException($"Cannot read directory entry '{info.FullName}': it no longer exists or cannot be accessed.");
            var isDirectory = (info.Attributes & FileAttributes.Directory) != 0;
            result.Add(new FileEntry(info.FullName, info.Name, isDirectory,
                isDirectory ? 0 : ((FileInfo)info).Length, info.LastWriteTimeUtc));
        }
        cancellationToken.ThrowIfCancellationRequested();
        return FilterAndSort(result, "", 0, false);
    }
}
