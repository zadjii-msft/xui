using Xui.FileExplorer.Models;

internal static class FolderMetadataTests
{
    internal static async Task<int> Run(string fixture)
    {
        int assertions = 0;
        string root = Path.Combine(fixture, "Hover metadata");
        string nested = Path.Combine(root, "Nested");
        Directory.CreateDirectory(Path.Combine(nested, "Empty"));
        File.WriteAllBytes(Path.Combine(root, "first.txt"), new byte[7]);
        File.WriteAllBytes(Path.Combine(nested, "second.txt"), new byte[11]);
        var metadata = await FolderMetadataReader.ReadAsync(root, CancellationToken.None);
        Check(metadata.Name == "Hover metadata" && metadata.FullPath == root);
        Check(metadata.Subfolders == 2 && metadata.Files == 2 && metadata.SizeBytes == 18);
        Check(metadata.CreatedUtc is { Kind: DateTimeKind.Utc } && metadata.Warning is null);
        Check(metadata.HelpText().Contains("Created:") && metadata.HelpText().Contains("18 bytes"));
        Check(!metadata.HelpText().Contains("new tab", StringComparison.OrdinalIgnoreCase));
        var empty = await FolderMetadataReader.ReadAsync(Path.Combine(nested, "Empty"), CancellationToken.None);
        Check(empty.Subfolders == 0 && empty.Files == 0 && empty.SizeBytes == 0 && empty.Warning is null);
        var absent = await FolderMetadataReader.ReadAsync(Path.Combine(root, "Absent"), CancellationToken.None);
        Check(absent.Subfolders is null && absent.Files is null && absent.SizeBytes is null && absent.Warning is not null);
        Check(absent.HelpText().Contains("(unavailable)") && !absent.HelpText().Contains("Files: 0"));

        var directory = new FolderMetadataEntry(true, false, DateTime.UnixEpoch, 0);
        var file = new FolderMetadataEntry(false, false, DateTime.UnixEpoch, 13);
        string allowed = Path.Combine(root, "Allowed"), denied = Path.Combine(root, "Denied");
        var partial = FolderMetadataReader.Read(root, CancellationToken.None,
            path => path == root ? [allowed, denied] : throw new UnauthorizedAccessException("Access denied to child"),
            path => path == allowed ? file : directory);
        Check(partial.Files == 1 && partial.Subfolders == 1 && partial.SizeBytes == 13);
        Check(partial.Warning?.Contains("Access denied") == true && partial.HelpText().Contains("(partial)"));
        var unreadable = FolderMetadataReader.Read(root, CancellationToken.None,
            _ => throw new UnauthorizedAccessException("Access denied to root"), _ => directory);
        Check(unreadable.Files is null && unreadable.Subfolders is null && unreadable.SizeBytes is null);
        Check(unreadable.Warning?.Contains("Access denied to root") == true);
        var vanished = FolderMetadataReader.Read(root, CancellationToken.None,
            _ => [allowed, denied], path => path == denied ? throw new FileNotFoundException("Entry disappeared") :
                path == root ? directory : file);
        Check(vanished.Files == 1 && vanished.SizeBytes == 13 && vanished.Warning is not null);

        bool enumeratedLink = false;
        var linked = FolderMetadataReader.Read(root, CancellationToken.None,
            path =>
            {
                if (path == root) return [allowed, denied];
                enumeratedLink = true;
                throw new Exception("Junction traversal must not occur.");
            }, path => path == root ? directory : path == allowed ? file : directory with { IsReparsePoint = true });
        Check(!enumeratedLink && linked.Subfolders == 0 && linked.Files == 1 && linked.SizeBytes == 13);
        Check(linked.Warning?.Contains("Links and junctions") == true);
        var linkedRoot = FolderMetadataReader.Read(root, CancellationToken.None,
            _ => throw new Exception("Root junction traversal must not occur."), _ => directory with { IsReparsePoint = true });
        Check(linkedRoot.Files is null && linkedRoot.Warning?.Contains("link or junction") == true);
        var linkedFile = FolderMetadataReader.Read(root, CancellationToken.None,
            _ => [allowed], path => path == root ? directory : file with { IsReparsePoint = true });
        Check(linkedFile.Files == 0 && linkedFile.SizeBytes == 0 && linkedFile.Warning is not null);
        var bounded = FolderMetadataReader.Read(root, CancellationToken.None,
            _ => [allowed, denied], path => path == root ? directory : file, maximumEntries: 1);
        Check(bounded.Files == 1 && bounded.SizeBytes == 13 && bounded.Warning?.Contains("scan limit") == true);

        bool disposed = false;
        var interrupted = FolderMetadataReader.Read(root, CancellationToken.None, _ => Entries(), path => path == root ? directory : file);
        Check(disposed && interrupted.Files == 1 && interrupted.Warning?.Contains("Read interrupted") == true);

        using var cancellation = new CancellationTokenSource();
        cancellation.Cancel();
        try
        {
            await FolderMetadataReader.ReadAsync(root, cancellation.Token);
            throw new Exception("Cancelled metadata scan completed.");
        }
        catch (OperationCanceledException) { Check(true); }
        using var duringRead = new CancellationTokenSource();
        try
        {
            FolderMetadataReader.Read(root, duringRead.Token, _ => [allowed, denied],
                path => { if (path != root) duringRead.Cancel(); return path == root ? directory : file; });
            throw new Exception("Metadata scan ignored cancellation.");
        }
        catch (OperationCanceledException) { Check(true); }
        return assertions;

        void Check(bool value)
        {
            ++assertions;
            if (!value) throw new Exception($"Folder metadata assertion {assertions} failed.");
        }

        IEnumerable<string> Entries()
        {
            try
            {
                yield return allowed;
                throw new IOException("Read interrupted");
            }
            finally { disposed = true; }
        }
    }
}
