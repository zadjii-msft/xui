using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Xui;

[Flags]
public enum FileTransferEffect : uint { None = 0, Copy = 1, Move = 2 }
public sealed record FileClipboardContent(string[] Paths, FileTransferEffect Effect);

public sealed unsafe partial class Window
{
    private readonly Dictionary<ulong, FileSubscription> fileSubscriptions = [];
    private sealed class FileSubscription(Window window, ulong handle)
    {
        internal readonly Window Window = window;
        internal readonly ulong Handle = handle;
        internal Func<string[]>? Paths;
        internal Action<FileTransferEffect>? Completed;
        internal Func<ItemKey?, FileTransferEffect, FileTransferEffect>? Query;
        internal Func<ItemKey?, string[], FileTransferEffect, FileTransferEffect>? Drop;
        internal GCHandle Root;
    }
    private FileSubscription FileCallbacks(ulong handle)
    {
        Guard();
        if (fileSubscriptions.TryGetValue(handle, out var current)) return current;
        var subscription = new FileSubscription(this, handle);
        subscription.Root = GCHandle.Alloc(subscription, GCHandleType.Weak);
        try { fileSubscriptions.Add(handle, subscription); }
        catch { subscription.Root.Free(); throw; }
        return subscription;
    }
    internal void FileDrag(ulong handle, Func<string[]> paths, Action<FileTransferEffect> completed)
    {
        ArgumentNullException.ThrowIfNull(paths);
        ArgumentNullException.ThrowIfNull(completed);
        var subscription = FileCallbacks(handle);
        Check(Native.GridFileDragBind(handle, &FileDragTrampoline, GCHandle.ToIntPtr(subscription.Root)));
        subscription.Paths = paths; subscription.Completed = completed;
    }
    internal void FileDrop(ulong handle, Func<ItemKey?, FileTransferEffect, FileTransferEffect> query,
        Func<ItemKey?, string[], FileTransferEffect, FileTransferEffect> drop)
    {
        ArgumentNullException.ThrowIfNull(query);
        ArgumentNullException.ThrowIfNull(drop);
        var subscription = FileCallbacks(handle);
        Check(Native.GridFileDropBind(handle, &FileDropTrampoline, GCHandle.ToIntPtr(subscription.Root)));
        subscription.Query = query; subscription.Drop = drop;
    }
    internal void ClearFileCallbacks(ulong handle)
    {
        Guard();
        Check(Native.GridFileDragBind(handle, null, 0));
        Check(Native.GridFileDropBind(handle, null, 0));
        if (fileSubscriptions.Remove(handle, out var subscription)) subscription.Root.Free();
    }
    internal static Native.Text[] FilePaths(Pins pins, string[] paths, bool allowEmpty = false)
    {
        ArgumentNullException.ThrowIfNull(paths);
        if (paths.Length > 4096 || (!allowEmpty && paths.Length == 0))
            throw new ArgumentOutOfRangeException(nameof(paths), "File transfers require 1 to 4096 paths.");
        var values = new Native.Text[paths.Length];
        long units = 11;
        for (int i = 0; i < paths.Length; ++i)
        {
            var path = paths[i];
            ArgumentNullException.ThrowIfNull(path);
            if (path.Length > 32767) throw new ArgumentException("File paths exceed 32767 UTF-16 units.", nameof(paths));
            units += path.Length + 1;
            if (units * 2 > 16 * 1024 * 1024) throw new ArgumentException("File paths exceed 16 MiB.", nameof(paths));
            values[i] = pins.Text(path);
        }
        return values;
    }
    private static void FileEffect(FileTransferEffect effect)
    {
        if (effect is not (FileTransferEffect.Copy or FileTransferEffect.Move))
            throw new ArgumentOutOfRangeException(nameof(effect), "Use Copy or Move, not both.");
    }
    private static string[] ReadFilePaths(Native.Text* paths, uint count)
    {
        if (count > 4096 || (count != 0 && paths is null)) throw new InvalidOperationException("Invalid native file paths.");
        var result = new string[count];
        for (int i = 0; i < result.Length; ++i)
            result[i] = Encoding.GetString(paths[i].Data, checked((int)paths[i].Length));
        return result;
    }
    /// <summary>Copies absolute file paths to the Windows clipboard. Use Move to cut without deleting source files.</summary>
    public void SetFileClipboard(string[] paths, FileTransferEffect effect)
    {
        Guard(); FileEffect(effect);
        using var pins = new Pins();
        var values = FilePaths(pins, paths);
        fixed (Native.Text* p = values) Check(Native.WindowSetFileClipboard(Handle, p, (uint)values.Length, (uint)effect));
    }
    private sealed class ClipboardReceiver(Window window)
    {
        internal readonly Window Window = window;
        internal FileClipboardContent? Content;
    }
    /// <summary>Reads a snapshot of the file clipboard, or null when it contains no filesystem paths.</summary>
    public FileClipboardContent? GetFileClipboard()
    {
        Guard();
        var receiver = new ClipboardReceiver(this);
        var root = GCHandle.Alloc(receiver);
        try { Check(Native.WindowGetFileClipboard(Handle, &ClipboardTrampoline, GCHandle.ToIntPtr(root))); return receiver.Content; }
        finally { root.Free(); }
    }
    /// <summary>Copies Unicode text without changing native text-editor keyboard behavior.</summary>
    public void SetClipboardText(string text)
    {
        Guard(); using var pins = new Pins(); Check(Native.WindowSetClipboardText(Handle, pins.Text(text)));
    }
    /// <summary>Copies or moves files with Windows Shell conflict and progress dialogs.</summary>
    /// <remarks>Runs synchronously on the UI STA and pumps messages. Returns false for cancellation or skipped work.
    /// Native errors throw. Earlier completed items are not rolled back. Never delete sources after this call.</remarks>
    public bool TransferFiles(string[] paths, string destination, FileTransferEffect effect)
    {
        Guard(); FileEffect(effect);
        using var pins = new Pins();
        var values = FilePaths(pins, paths);
        var target = pins.Text(destination);
        uint completed;
        fixed (Native.Text* p = values) Check(Native.WindowTransferFiles(Handle, p, (uint)values.Length, target, (uint)effect, &completed));
        return completed != 0;
    }
    /// <summary>Pastes clipboard files and sends Shell completion notifications to their original source.</summary>
    /// <returns>Null for no file clipboard, true for complete, or false for cancelled or skipped work.</returns>
    /// <remarks>Uses the same synchronous Shell operation as TransferFiles. Clears a completed cut only if the clipboard did not change.</remarks>
    public bool? PasteFiles(string destination)
    {
        Guard(); using var pins = new Pins(); uint result;
        Check(Native.WindowPasteFiles(Handle, pins.Text(destination), &result));
        return result == 0 ? null : result == 1;
    }
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int ClipboardTrampoline(nint context, Native.Text* paths, uint count, uint effect)
    {
        ClipboardReceiver? receiver = null;
        try
        {
            receiver = GCHandle.FromIntPtr(context).Target as ClipboardReceiver;
            if (receiver is null) return 8;
            receiver.Content = count == 0 ? null : new(ReadFilePaths(paths, count), (FileTransferEffect)effect);
            return 0;
        }
        catch (Exception error) { if (receiver is not null) receiver.Window.callbackError = error; return 8; }
    }
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int FileDragTrampoline(nint context, Native.Event* e)
    {
        FileSubscription? subscription = null;
        try
        {
            subscription = GCHandle.FromIntPtr(context).Target as FileSubscription;
            if (subscription is null) return 8;
            var window = subscription.Window;
            ++window.callbacks;
            try
            {
                if ((EventKind)e->Kind == EventKind.Request)
                {
                    var paths = subscription.Paths?.Invoke() ?? throw new InvalidOperationException("File drag paths cannot be null.");
                    using var pins = new Pins();
                    var values = FilePaths(pins, paths, true);
                    fixed (Native.Text* p = values) window.Check(Native.GridFileDragPaths(subscription.Handle, p, (uint)values.Length));
                }
                else if ((EventKind)e->Kind == EventKind.Action) subscription.Completed?.Invoke((FileTransferEffect)e->Value);
            }
            finally { --window.callbacks; }
            return 0;
        }
        catch (Exception error) { if (subscription is not null) subscription.Window.callbackError = error; return 8; }
    }
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int FileDropTrampoline(nint context, ulong id, ulong version, uint hasKey, Native.Text* paths,
        uint count, uint requested, uint perform, uint* effect)
    {
        FileSubscription? subscription = null;
        try
        {
            subscription = GCHandle.FromIntPtr(context).Target as FileSubscription;
            if (subscription is null) return 8;
            var window = subscription.Window;
            ++window.callbacks;
            try
            {
                ItemKey? key = hasKey == 0 ? null : new ItemKey(id, version);
                *effect = (uint)(perform == 0 ?
                    subscription.Query?.Invoke(key, (FileTransferEffect)requested) ?? FileTransferEffect.None :
                    subscription.Drop?.Invoke(key, ReadFilePaths(paths, count), (FileTransferEffect)requested) ?? FileTransferEffect.None);
            }
            finally { --window.callbacks; }
            return 0;
        }
        catch (Exception error) { if (subscription is not null) subscription.Window.callbackError = error; return 8; }
    }
}
public sealed partial class DataGrid
{
    /// <summary>Starts an OLE file drag after the system pointer threshold. The paths factory snapshots the current selection.</summary>
    /// <remarks>Copy and Move are allowed. Completion reports the target result; it must never delete source files.</remarks>
    public DataGrid OnFileDrag(Func<string[]> paths, Action<FileTransferEffect> completed)
    { Window.FileDrag(Handle, paths, completed); return this; }
    /// <summary>Registers a nonblocking acceptance query and a synchronous file drop callback.</summary>
    /// <remarks>Null keys mean empty grid body, not headers or scrollbars. The query must not perform work.
    /// Return the requested effect from drop only after every transfer completes; return None for cancellation.</remarks>
    public DataGrid OnFileDrop(Func<ItemKey?, FileTransferEffect, FileTransferEffect> query,
        Func<ItemKey?, string[], FileTransferEffect, FileTransferEffect> drop)
    { Window.FileDrop(Handle, query, drop); return this; }
    public void ClearFileTransferCallbacks() => Window.ClearFileCallbacks(Handle);
}
