using System.Text.Json;
using System.Text.Json.Serialization;

namespace Xui.FileExplorer.Models;

public sealed class ExplorerState
{
    public const int RecentLimit = 32;
    public List<string> Bookmarks { get; set; } = [];
    public List<string> Recents { get; set; } = [];

    public void AddRecent(string path)
    {
        ValidatePath(path);
        Recents.RemoveAll(item => StringComparer.OrdinalIgnoreCase.Equals(item, path));
        Recents.Insert(0, path);
        if (Recents.Count > RecentLimit)
            Recents.RemoveRange(RecentLimit, Recents.Count - RecentLimit);
    }

    public void ToggleBookmark(string path)
    {
        ValidatePath(path);
        if (Bookmarks.RemoveAll(item => StringComparer.OrdinalIgnoreCase.Equals(item, path)) == 0)
            Bookmarks.Add(path);
    }

    internal static void ValidatePath(string path)
    {
        if (string.IsNullOrWhiteSpace(path) || path.Length > 32767 || path.Contains('\0'))
            throw new ArgumentException("A saved path must contain 1 to 32767 characters and no null characters.", nameof(path));
    }
}

[JsonSourceGenerationOptions(WriteIndented = true)]
[JsonSerializable(typeof(ExplorerState))]
internal partial class ExplorerStateJsonContext : JsonSerializerContext;

public sealed class AppStateStore
{
    private const long MaximumFileSize = 4 * 1024 * 1024;
    private readonly string statePath;
    private bool corruptState;

    public AppStateStore(string? path = null)
    {
        statePath = System.IO.Path.GetFullPath(path ?? System.IO.Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "Xui", "FileExplorer", "state.json"));
    }

    public ExplorerState Load()
    {
        try
        {
            using var stream = new FileStream(statePath, FileMode.Open, FileAccess.Read, FileShare.Read);
            if (stream.Length > MaximumFileSize)
                throw new InvalidDataException("The state file is too large.");
            var state = JsonSerializer.Deserialize(stream, ExplorerStateJsonContext.Default.ExplorerState)
                ?? throw new InvalidDataException("The state file contains null.");
            Validate(state);
            corruptState = false;
            return state;
        }
        catch (FileNotFoundException)
        {
            corruptState = false;
            return new ExplorerState();
        }
        catch (DirectoryNotFoundException)
        {
            corruptState = false;
            return new ExplorerState();
        }
        catch (Exception error) when (error is JsonException or InvalidDataException or ArgumentException)
        {
            corruptState = true;
            throw new InvalidDataException($"Cannot load explorer state from '{statePath}': {error.Message}", error);
        }
    }

    public void Save(ExplorerState state)
    {
        if (corruptState)
            throw new InvalidDataException($"Cannot overwrite corrupt explorer state '{statePath}'. Remove or repair it, then load it again.");
        Validate(state);
        var bytes = JsonSerializer.SerializeToUtf8Bytes(state, ExplorerStateJsonContext.Default.ExplorerState);
        if (bytes.Length > MaximumFileSize)
            throw new InvalidDataException("The explorer state is too large.");
        var directory = System.IO.Path.GetDirectoryName(statePath)!;
        Directory.CreateDirectory(directory);
        var stagingPath = System.IO.Path.Combine(directory, $".{System.IO.Path.GetFileName(statePath)}.{Guid.NewGuid():N}.tmp");
        try
        {
            using (var stream = new FileStream(stagingPath, FileMode.CreateNew, FileAccess.Write, FileShare.None))
            {
                stream.Write(bytes);
                stream.Flush(flushToDisk: true);
            }
            if (File.Exists(statePath))
                File.Replace(stagingPath, statePath, null);
            else
                File.Move(stagingPath, statePath);
        }
        finally
        {
            if (File.Exists(stagingPath))
                File.Delete(stagingPath);
        }
    }

    private static void Validate(ExplorerState state)
    {
        ArgumentNullException.ThrowIfNull(state);
        if (state.Bookmarks is null || state.Recents is null)
            throw new InvalidDataException("Bookmarks and recents must be lists.");
        if (state.Bookmarks.Count > 4096 || state.Recents.Count > ExplorerState.RecentLimit)
            throw new InvalidDataException("The explorer state contains too many saved paths.");
        foreach (var path in state.Bookmarks.Concat(state.Recents))
            ExplorerState.ValidatePath(path);
    }
}
