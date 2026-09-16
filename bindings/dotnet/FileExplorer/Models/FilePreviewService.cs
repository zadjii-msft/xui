using System.Text;

namespace Xui.FileExplorer.Models;

public enum FilePreviewKind { Metadata, Text, Image }

public sealed record FilePreview(FilePreviewKind Kind, string Text, string Message, bool Truncated = false,
    FileEntry? Metadata = null);

public sealed class FilePreviewService
{
    public const int MaximumTextLength = 65_536;
    public const int MaximumEncodedBytes = 262_144;
    private static readonly HashSet<string> Images = new(StringComparer.OrdinalIgnoreCase)
        { ".png", ".jpg", ".jpeg", ".bmp", ".gif", ".tif", ".tiff", ".webp" };
    private static readonly HashSet<string> TextFiles = new(StringComparer.OrdinalIgnoreCase)
    {
        "", ".txt", ".md", ".markdown", ".log", ".csv", ".tsv", ".json", ".xml", ".yaml", ".yml",
        ".toml", ".ini", ".config", ".cs", ".xui", ".cpp", ".c", ".h", ".hpp", ".rs", ".py",
        ".js", ".jsx", ".ts", ".tsx", ".html", ".htm", ".css", ".scss", ".sql", ".sh", ".ps1",
        ".bat", ".cmd", ".sln", ".slnx", ".csproj", ".props", ".targets", ".cmake", ".gitignore"
    };

    public Task<FilePreview> LoadAsync(FileEntry entry, CancellationToken cancellation) => Task.Run(async () =>
    {
        cancellation.ThrowIfCancellationRequested();
        var attributes = File.GetAttributes(entry.FullPath);
        bool directory = (attributes & FileAttributes.Directory) != 0;
        if (directory != entry.IsDirectory)
            throw new IOException("The selected item changed. Refresh the folder and try again.");
        cancellation.ThrowIfCancellationRequested();
        if (directory)
            return new FilePreview(FilePreviewKind.Metadata, "", "", Metadata: entry);
        string extension = Path.GetExtension(entry.Name);
        if (Images.Contains(extension))
            return new FilePreview(FilePreviewKind.Image, "", "");
        if (!TextFiles.Contains(extension))
            return new FilePreview(FilePreviewKind.Metadata, "", "", Metadata: entry);

        await using var stream = new FileStream(entry.FullPath, FileMode.Open, FileAccess.Read, FileShare.Read,
            4096, FileOptions.Asynchronous | FileOptions.SequentialScan);
        byte[] bytes = new byte[MaximumEncodedBytes + 1];
        int count = 0;
        while (count < bytes.Length)
        {
            int read = await stream.ReadAsync(bytes.AsMemory(count), cancellation).ConfigureAwait(false);
            if (read == 0) break;
            count += read;
        }
        cancellation.ThrowIfCancellationRequested();
        bool truncated = count > MaximumEncodedBytes;
        count = Math.Min(count, MaximumEncodedBytes);
        Encoding encoding = new UTF8Encoding(false, true);
        int offset = 0;
        if (count >= 4 && (bytes.AsSpan(0, 4).SequenceEqual(new byte[] { 0xff, 0xfe, 0, 0 })
            || bytes.AsSpan(0, 4).SequenceEqual(new byte[] { 0, 0, 0xfe, 0xff })))
            throw new InvalidDataException("UTF-32 text is not supported. Use UTF-8 or BOM-marked UTF-16.");
        if (count >= 3 && bytes[0] == 0xef && bytes[1] == 0xbb && bytes[2] == 0xbf) offset = 3;
        else if (count >= 2 && bytes[0] == 0xff && bytes[1] == 0xfe)
        {
            encoding = new UnicodeEncoding(false, true, true);
            offset = 2;
        }
        else if (count >= 2 && bytes[0] == 0xfe && bytes[1] == 0xff)
        {
            encoding = new UnicodeEncoding(true, true, true);
            offset = 2;
        }
        char[] characters = new char[encoding.GetMaxCharCount(count - offset)];
        int length;
        try
        {
            length = encoding.GetDecoder().GetChars(bytes, offset, count - offset, characters, 0, flush: !truncated);
        }
        catch (DecoderFallbackException error)
        {
            throw new InvalidDataException("The file is not valid UTF-8 or BOM-marked UTF-16 text.", error);
        }
        truncated |= length > MaximumTextLength;
        length = Math.Min(length, MaximumTextLength);
        if (length > 0 && char.IsHighSurrogate(characters[length - 1])) length--;
        for (int i = 0; i < length; i++)
            if (char.IsControl(characters[i]) && characters[i] is not ('\r' or '\n' or '\t'))
                throw new InvalidDataException("The file contains binary data or unsupported control characters.");
        string text = new string(characters, 0, length).Replace("\r\n", "\r").Replace('\n', '\r');
        cancellation.ThrowIfCancellationRequested();
        return new FilePreview(FilePreviewKind.Text, text,
            truncated ? "Preview truncated. Open the file to see all content."
                : text.Length == 0 ? "Empty text file." : "", truncated);
    }, cancellation);
}
