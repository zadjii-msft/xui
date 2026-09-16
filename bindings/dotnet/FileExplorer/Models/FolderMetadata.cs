using System.Diagnostics;
using System.Globalization;

namespace Xui.FileExplorer.Models;

internal sealed record FolderMetadata(string FullPath, string Name, DateTime? CreatedUtc,
    long? Subfolders, long? Files, long? SizeBytes, string? Warning)
{
    public string HelpText()
    {
        string status = Warning is null ? "" : Subfolders.HasValue ? " (partial)" : " (unavailable)";
        return $"{Name}\n{FullPath}\n\nCreated: {CreatedUtc?.ToLocalTime().ToString("g", CultureInfo.CurrentCulture) ?? "Unavailable"}" +
            $"\nSubfolders{status}: {Count(Subfolders)}\nFiles{status}: {Count(Files)}" +
            $"\nSize{status}: {(SizeBytes is { } bytes ? $"{bytes:N0} bytes" : "Unavailable")}" +
            (Warning is null ? "" : $"\n{Warning}") + "\n\nClick to open this folder";
    }

    private static string Count(long? count) => count?.ToString("N0", CultureInfo.CurrentCulture) ?? "Unavailable";
}

internal readonly record struct FolderMetadataEntry(bool IsDirectory, bool IsReparsePoint, DateTime CreatedUtc, long SizeBytes);

internal static class FolderMetadataReader
{
    private const int MaximumEntries = 100_000;

    internal static Task<FolderMetadata> ReadAsync(string path, CancellationToken cancellation)
        => Task.Run(() => Read(path, cancellation, Directory.EnumerateFileSystemEntries, Inspect), cancellation);

    // These adapters also allow deterministic tests for denied access and disappearing entries.
    internal static FolderMetadata Read(string path, CancellationToken cancellation,
        Func<string, IEnumerable<string>> enumerate, Func<string, FolderMetadataEntry> inspect,
        int maximumEntries = MaximumEntries)
    {
        cancellation.ThrowIfCancellationRequested();
        string fullPath = Path.GetFullPath(path);
        string name = Path.GetFileName(Path.TrimEndingDirectorySeparator(fullPath));
        if (string.IsNullOrEmpty(name)) name = fullPath;
        DateTime? created = null;
        long folders = 0, files = 0, bytes = 0;
        int visited = 0;
        bool rootReadable = false;
        string? firstWarning = null;
        var elapsed = Stopwatch.StartNew();
        var pending = new Stack<(string Path, IEnumerator<string> Entries)>();
        try
        {
            var root = inspect(fullPath);
            created = root.CreatedUtc;
            if (!root.IsDirectory) throw new IOException("The path is not a folder.");
            if (root.IsReparsePoint)
                return Result("This folder is a link or junction. Its contents were not scanned.");
            pending.Push((fullPath, enumerate(fullPath).GetEnumerator()));
            while (pending.Count != 0)
            {
                cancellation.ThrowIfCancellationRequested();
                var current = pending.Peek();
                string child;
                try
                {
                    bool next = current.Entries.MoveNext();
                    if (current.Path == fullPath) rootReadable = true;
                    if (!next)
                    {
                        pending.Pop().Entries.Dispose();
                        continue;
                    }
                    child = current.Entries.Current;
                }
                catch (Exception error) when (IsExpected(error))
                {
                    Note(error.Message);
                    pending.Pop().Entries.Dispose();
                    continue;
                }
                if (visited++ >= maximumEntries || elapsed.Elapsed >= TimeSpan.FromSeconds(5))
                {
                    Note("The scan limit was reached. Totals include only entries already read.");
                    break;
                }
                try
                {
                    cancellation.ThrowIfCancellationRequested();
                    var entry = inspect(child);
                    if (entry.IsReparsePoint)
                    {
                        Note("Links and junctions were not scanned.");
                        continue;
                    }
                    if (entry.IsDirectory)
                    {
                        ++folders;
                        pending.Push((child, enumerate(child).GetEnumerator()));
                    }
                    else
                    {
                        bytes = checked(bytes + entry.SizeBytes);
                        ++files;
                    }
                }
                catch (Exception error) when (IsExpected(error))
                {
                    Note(error.Message);
                }
            }
        }
        catch (Exception error) when (IsExpected(error))
        {
            Note(error.Message);
        }
        finally
        {
            while (pending.Count != 0) pending.Pop().Entries.Dispose();
        }
        cancellation.ThrowIfCancellationRequested();
        return Result(firstWarning is not null ? $"Scan incomplete: {firstWarning}" : null);

        void Note(string message) { firstWarning ??= message; }
        FolderMetadata Result(string? warning) => new(fullPath, name, created,
            rootReadable ? folders : null, rootReadable ? files : null, rootReadable ? bytes : null, warning);
    }

    private static FolderMetadataEntry Inspect(string path)
    {
        var attributes = File.GetAttributes(path);
        bool directory = (attributes & FileAttributes.Directory) != 0;
        bool reparse = (attributes & FileAttributes.ReparsePoint) != 0;
        return new(directory, reparse, File.GetCreationTimeUtc(path), directory || reparse ? 0 : new FileInfo(path).Length);
    }

    private static bool IsExpected(Exception error) => error is IOException or UnauthorizedAccessException
        or System.Security.SecurityException or NotSupportedException or ArgumentException or OverflowException;
}
