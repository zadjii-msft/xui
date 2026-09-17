using System.Collections.ObjectModel;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

namespace Xui.Designer;

internal sealed record DesignerRecovery(Guid Id, string SourcePath, string? OriginalPath, DateTimeOffset UpdatedAt,
    bool Legacy, string? Error);

internal sealed class DesignerDocumentStore
{
    internal const int MaximumSourceLength = 65536;
    private const int MaximumMetadataBytes = 262144;
    private static readonly UTF8Encoding Utf8 = new(false, true);
    private readonly string recoveryDirectory;
    private string? savedSource;
    private string? savedFileHash;

    internal DesignerDocumentStore(string recoveryDirectory, string source)
    {
        this.recoveryDirectory = Path.GetFullPath(recoveryDirectory);
        Source = Validate(source);
        savedSource = Normalize(Source);
    }

    internal Guid RecoveryId { get; } = Guid.NewGuid();
    internal string Source { get; private set; }
    internal string? FilePath { get; private set; }
    internal bool IsUntitled => FilePath is null;
    internal bool IsDirty => savedSource is null || Normalize(Source) != savedSource;
    internal string RecoveryPath => DraftPath(RecoveryId);

    internal void UpdateSource(string source) => Source = Validate(source);

    internal void New(string source, bool discardChanges = false)
    {
        RequireReplaceable(discardChanges);
        var next = Validate(source);
        DeleteRecovery(RecoveryId);
        Source = next;
        savedSource = Normalize(next);
        savedFileHash = null;
        FilePath = null;
    }

    internal void Open(string path, bool discardChanges = false)
    {
        RequireReplaceable(discardChanges);
        var fullPath = SourcePath(path);
        var snapshot = ReadSource(fullPath);
        DeleteRecovery(RecoveryId);
        Source = snapshot.Source;
        savedSource = Normalize(Source);
        savedFileHash = snapshot.Hash;
        FilePath = fullPath;
    }

    internal void Save(string? path = null)
    {
        var destination = SourcePath(path ?? FilePath ?? throw new InvalidOperationException("Choose a .xui path before saving."));
        var source = Normalize(Source);
        var bytes = Utf8.GetBytes(source);
        bool exists = File.Exists(destination);
        if (exists)
        {
            if (!StringComparer.OrdinalIgnoreCase.Equals(destination, FilePath) || savedFileHash is null)
                throw new IOException("The destination already exists. Choose a new path to keep both files.");
            if (!StringComparer.Ordinal.Equals(ReadSource(destination).Hash, savedFileHash))
                throw new IOException("The file changed on disk. Save to a different path to keep both versions.");
        }
        WriteAtomic(destination, bytes, exists);
        FilePath = destination;
        savedSource = source;
        savedFileHash = Convert.ToHexString(SHA256.HashData(bytes));
        try { DeleteRecovery(RecoveryId); }
        catch (Exception failure) when (ExpectedFileError(failure))
        {
            throw new IOException("The file was saved, but its recovery draft could not be removed.", failure);
        }
    }

    internal void PersistRecovery()
    {
        if (!IsDirty)
        {
            DeleteRecovery(RecoveryId);
            return;
        }
        Directory.CreateDirectory(recoveryDirectory);
        var bytes = Utf8.GetBytes(Normalize(Source));
        WriteAtomic(DraftPath(RecoveryId), bytes, overwrite: true);
        var metadata = new RecoveryMetadata(1, FilePath, savedFileHash, Convert.ToHexString(SHA256.HashData(bytes)));
        WriteAtomic(MetadataPath(RecoveryId), JsonSerializer.SerializeToUtf8Bytes(metadata), overwrite: true);
    }

    internal ReadOnlyCollection<DesignerRecovery> ListRecovery()
    {
        if (!Directory.Exists(recoveryDirectory)) return Array.AsReadOnly<DesignerRecovery>([]);
        var result = new List<DesignerRecovery>();
        foreach (var path in Directory.EnumerateFiles(recoveryDirectory, "*.xui"))
        {
            if (!Guid.TryParseExact(Path.GetFileNameWithoutExtension(path), "N", out var id) || id == RecoveryId) continue;
            string? original = null, error = null;
            bool legacy = !File.Exists(MetadataPath(id));
            try
            {
                var snapshot = ReadSource(path);
                original = ReadMetadata(id, snapshot.Hash)?.OriginalPath;
            }
            catch (Exception failure) when (ExpectedFileError(failure))
            {
                error = failure.Message;
            }
            result.Add(new(id, path, original, File.GetLastWriteTimeUtc(path), legacy, error));
        }
        return result.OrderByDescending(entry => entry.UpdatedAt).ThenBy(entry => entry.Id).ToList().AsReadOnly();
    }

    internal void Recover(Guid id, bool discardChanges = false)
    {
        RequireReplaceable(discardChanges);
        if (id == RecoveryId) throw new InvalidOperationException("This document already owns that recovery draft.");
        var snapshot = ReadSource(DraftPath(id));
        var metadata = ReadMetadata(id, snapshot.Hash);
        DeleteRecovery(RecoveryId);
        Source = snapshot.Source;
        savedSource = null;
        FilePath = metadata?.OriginalPath;
        savedFileHash = metadata?.SavedFileHash;
    }

    internal void DeleteRecovery(Guid id)
    {
        DeleteIfPresent(DraftPath(id));
        DeleteIfPresent(MetadataPath(id));
    }

    private static void DeleteIfPresent(string path)
    {
        try { File.Delete(path); }
        catch (DirectoryNotFoundException) { } // An absent recovery directory already has no draft to remove.
    }

    private void RequireReplaceable(bool discardChanges)
    {
        if (IsDirty && !discardChanges)
            throw new InvalidOperationException("Save or explicitly discard the current edits before replacing this document.");
    }

    private string DraftPath(Guid id) => Path.Combine(recoveryDirectory, id.ToString("N") + ".xui");
    private string MetadataPath(Guid id) => Path.Combine(recoveryDirectory, id.ToString("N") + ".json");

    private RecoveryMetadata? ReadMetadata(Guid id, string sourceHash)
    {
        var path = MetadataPath(id);
        if (!File.Exists(path)) return null;
        using var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read);
        if (stream.Length > MaximumMetadataBytes) throw new InvalidDataException("Recovery metadata exceeds its size limit.");
        var metadata = JsonSerializer.Deserialize<RecoveryMetadata>(stream)
            ?? throw new InvalidDataException("Recovery metadata is empty.");
        if (metadata.Version != 1) throw new InvalidDataException("This recovery metadata version is not supported.");
        if (!StringComparer.Ordinal.Equals(metadata.SourceHash, sourceHash))
            throw new InvalidDataException("Recovery source and metadata do not match. Open the .xui draft as a new document.");
        if (metadata.OriginalPath is { } original)
        {
            if (!Path.IsPathFullyQualified(original) || SourcePath(original) != original ||
                metadata.SavedFileHash is not { Length: 64 } hash || hash.Any(character => !Uri.IsHexDigit(character)))
                throw new InvalidDataException("Recovery metadata has an invalid original file identity.");
        }
        else if (metadata.SavedFileHash is not null)
            throw new InvalidDataException("Recovery metadata has a file hash without an original path.");
        return metadata;
    }

    private static (string Source, string Hash) ReadSource(string path)
    {
        const int maximumBytes = MaximumSourceLength * 4 + 3;
        using var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read);
        if (stream.Length > maximumBytes) throw new InvalidDataException("Source exceeds 65,536 UTF-16 code units.");
        using var buffer = new MemoryStream();
        var bytes = new byte[8192];
        int read;
        while ((read = stream.Read(bytes)) != 0)
        {
            if (buffer.Length + read > maximumBytes) throw new InvalidDataException("Source exceeds 65,536 UTF-16 code units.");
            buffer.Write(bytes, 0, read);
        }
        var content = buffer.ToArray();
        int preamble = content.AsSpan().StartsWith("\uFEFF"u8) ? 3 : 0;
        var text = Validate(Utf8.GetString(content, preamble, content.Length - preamble));
        return (text, Convert.ToHexString(SHA256.HashData(content)));
    }

    private static void WriteAtomic(string path, byte[] bytes, bool overwrite)
    {
        var temporary = path + "." + Guid.NewGuid().ToString("N") + ".tmp";
        try
        {
            using (var output = new FileStream(temporary, FileMode.CreateNew, FileAccess.Write, FileShare.None))
            {
                output.Write(bytes);
                output.Flush(flushToDisk: true);
            }
            if (overwrite && File.Exists(path)) File.Replace(temporary, path, destinationBackupFileName: null);
            else File.Move(temporary, path, overwrite: false);
        }
        finally { DeleteIfPresent(temporary); }
    }

    private static string SourcePath(string path)
    {
        if (string.IsNullOrWhiteSpace(path)) throw new ArgumentException("Choose a .xui file path.", nameof(path));
        var fullPath = Path.GetFullPath(path);
        if (!Path.GetExtension(fullPath).Equals(".xui", StringComparison.OrdinalIgnoreCase))
            throw new ArgumentException("Choose a file with the .xui extension.", nameof(path));
        return fullPath;
    }

    private static string Validate(string source)
    {
        ArgumentNullException.ThrowIfNull(source);
        if (source.Length > MaximumSourceLength) throw new InvalidDataException("Source exceeds 65,536 UTF-16 code units.");
        if (source.Contains('\0')) throw new InvalidDataException("Source contains a NUL character.");
        _ = Utf8.GetByteCount(source);
        return source;
    }

    private static string Normalize(string source) => source.Replace("\r\n", "\n", StringComparison.Ordinal).Replace('\r', '\n');
    private static bool ExpectedFileError(Exception error) => error is IOException or InvalidDataException or
        UnauthorizedAccessException or ArgumentException or NotSupportedException or System.Security.SecurityException or JsonException;
    private sealed record RecoveryMetadata(int Version, string? OriginalPath, string? SavedFileHash, string SourceHash);
}
