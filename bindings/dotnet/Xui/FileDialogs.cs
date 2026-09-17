using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Xui;

public sealed record FileDialogFilter(string Name, string Pattern);
public sealed record FileDialogOptions
{
    public string Title { get; init; } = "";
    public IReadOnlyList<FileDialogFilter> Filters { get; init; } = Array.Empty<FileDialogFilter>();
    public string DefaultExtension { get; init; } = "";
    public string SuggestedName { get; init; } = "";
    public string InitialDirectory { get; init; } = "";
}
public sealed unsafe partial class Window
{
    private bool fileDialogActive;
    private sealed class FileDialogResult(Window window)
    {
        internal readonly Window Window = window;
        internal bool Delivered;
        internal string? Path;
    }
    public string? ShowOpenFileDialog(FileDialogOptions options) => ShowFileDialog(options, false);
    public string? ShowSaveFileDialog(FileDialogOptions options) => ShowFileDialog(options, true);
    private string? ShowFileDialog(FileDialogOptions options, bool save)
    {
        Guard();
        if (contentContext.Value is not null) throw new InvalidOperationException("Content scopes cannot open window dialogs.");
        if (fileDialogActive) throw new InvalidOperationException("A window file dialog call is already active.");
        ArgumentNullException.ThrowIfNull(options);
        ArgumentNullException.ThrowIfNull(options.Filters);
        if (options.Filters.Count > 32) throw new ArgumentOutOfRangeException(nameof(options), "At most 32 file filters are supported.");
        using var pins = new Pins();
        var filters = new Native.FileDialogFilter[options.Filters.Count];
        for (int i = 0; i < filters.Length; ++i)
        {
            var filter = options.Filters[i];
            ArgumentNullException.ThrowIfNull(filter);
            filters[i] = new() { Name = pins.Text(filter.Name), Pattern = pins.Text(filter.Pattern) };
        }
        var native = new Native.FileDialogOptions
        {
            Size = (uint)sizeof(Native.FileDialogOptions), Version = 0x10000,
            Title = pins.Text(options.Title), DefaultExtension = pins.Text(options.DefaultExtension),
            SuggestedName = pins.Text(options.SuggestedName), InitialDirectory = pins.Text(options.InitialDirectory),
            FilterCount = (uint)filters.Length
        };
        var result = new FileDialogResult(this);
        var root = GCHandle.Alloc(result);
        fileDialogActive = true;
        ++callbacks;
        try
        {
            fixed (Native.FileDialogFilter* p = filters)
            {
                native.Filters = p;
                Check(save ? Native.WindowSaveFileDialog(Handle, &native, &FileDialogTrampoline, GCHandle.ToIntPtr(root)) :
                    Native.WindowOpenFileDialog(Handle, &native, &FileDialogTrampoline, GCHandle.ToIntPtr(root)));
            }
            if (!result.Delivered) throw new InvalidOperationException("Native file dialog did not deliver a result.");
            return result.Path;
        }
        finally { --callbacks; fileDialogActive = false; root.Free(); }
    }
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int FileDialogTrampoline(nint context, uint accepted, Native.Text path)
    {
        FileDialogResult? result = null;
        try
        {
            result = GCHandle.FromIntPtr(context).Target as FileDialogResult;
            if (result is null) return 8;
            if (result.Delivered || accepted > 1 || path.Reserved != 0 ||
                (accepted == 0 && path.Length != 0) || (accepted == 1 && (path.Length == 0 || path.Data is null)))
                throw new InvalidOperationException("Native file dialog returned an invalid result.");
            result.Path = accepted == 0 ? null : Encoding.GetString(path.Data, checked((int)path.Length));
            result.Delivered = true;
            return 0;
        }
        catch (Exception error) { if (result is not null) result.Window.callbackError = error; return 8; }
    }
}
