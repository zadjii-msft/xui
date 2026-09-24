using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;

namespace Xui;

public enum MemoryImageFormat : uint { Png, Jpeg }
public enum MemoryImageStatus : uint { Empty, Loading, Ready, Error, PresentationFailed }
public readonly record struct MemoryImageOptions(long Generation, MemoryImageFormat Format,
    int SourceWidth, int SourceHeight, int OutputWidth, int OutputHeight, int HintWidth, int HintHeight);
public readonly record struct MemoryImageState(long Generation, MemoryImageStatus Status,
    int SourceWidth, int SourceHeight, int PixelWidth, int PixelHeight);
public readonly record struct MemoryImageNotification(MemoryImageState State, string? Error);
public readonly record struct MemoryImageStatistics(ulong EncodedBytes, ulong EncodedPeak, ulong EncodedLimit);

public sealed unsafe partial class Image
{
    public static bool SupportsMemorySource => Native.ImageMemoryAvailable.Value;

    /// <summary>Starts an owned native memory decode; the runtime copies the borrowed bytes before returning.</summary>
    public MemoryImageRequest SetMemorySource(MemoryImageOptions options, ReadOnlySpan<byte> encoded,
        Action<MemoryImageNotification> changed) => MemoryImageRequest.Create(this, options, encoded, changed);

    /// <summary>Clears current presentation and revokes callbacks without joining decoder threads.</summary>
    public void CancelMemorySource()
    {
        Window.Guard();
        MemoryImageRequest.RequireSupport();
        if (Window.FindMemoryImage(Handle) is { } request) request.Dispose();
        else Window.Check(Native.ImageMemoryCancel(Handle));
    }

    public MemoryImageState MemoryState
    {
        get
        {
            Window.Guard();
            MemoryImageRequest.RequireSupport();
            var state = new Native.ImageMemoryStateValue { Size = 40, Version = 0x10000 };
            Window.Check(Native.ImageMemoryGetState(Handle, ref state));
            return MemoryImageRequest.Decode(state);
        }
    }

    public static MemoryImageStatistics GetMemoryStatistics()
    {
        MemoryImageRequest.RequireSupport();
        var value = new Native.ImageMemoryStatisticsValue { Size = 32, Version = 0x10000 };
        Window.CheckStatus(Native.ImageMemoryGetStatistics(ref value), null);
        if (value.Size != 32 || value.Version != 0x10000 || value.Bytes > value.Limit || value.Peak < value.Bytes)
            throw new InvalidOperationException("The native runtime returned invalid Image memory statistics.");
        return new(value.Bytes, value.Peak, value.Limit);
    }
}

/// <summary>Owns native Image callback delivery through Ready and any subsequent presentation failure.</summary>
public sealed unsafe class MemoryImageRequest : IDisposable
{
    private static readonly UTF8Encoding Utf8 = new(false, true);
    internal Window Window { get; }
    internal ContentUpdate? Scope { get; }
    internal ulong ImageHandle { get; }
    internal bool AcceptCallbacks => !stopping && !retired && Scope is not { AcceptCallbacks: false };
    private Action<MemoryImageNotification>? changed;
    private GCHandle root;
    private bool stopping, retired;

    private MemoryImageRequest(Image image, Action<MemoryImageNotification> changed)
    {
        Window = image.Window;
        ImageHandle = image.Handle;
        Scope = Window.ScopeFor(ImageHandle);
        this.changed = changed;
    }

    internal static void RequireSupport()
    {
        if (!Image.SupportsMemorySource)
            throw new NotSupportedException("The native runtime does not provide owned memory Image decoding and presentation.");
    }

    internal static MemoryImageRequest Create(Image image, MemoryImageOptions options, ReadOnlySpan<byte> encoded,
        Action<MemoryImageNotification> changed)
    {
        ArgumentNullException.ThrowIfNull(changed);
        image.Window.Guard();
        RequireSupport();
        ArgumentOutOfRangeException.ThrowIfNegativeOrZero(options.Generation);
        if (!Enum.IsDefined(options.Format) || options.SourceWidth is < 1 or > 16384 ||
            options.SourceHeight is < 1 or > 16384 || (long)options.SourceWidth * options.SourceHeight > 16 * 1024 * 1024 ||
            options.OutputWidth is < 1 or > 1024 || options.OutputHeight is < 1 or > 1024 ||
            options.HintWidth is < 1 or > 1024 || options.HintHeight is < 1 or > 1024 ||
            encoded.IsEmpty || encoded.Length > 32 * 1024 * 1024)
            throw new ArgumentOutOfRangeException(nameof(options), "Image memory input exceeds the bounded native contract.");
        var previous = image.Window.FindMemoryImage(image.Handle);
        image.Window.ReserveMemoryImage();
        var request = new MemoryImageRequest(image, changed);
        request.root = GCHandle.Alloc(request, GCHandleType.Weak);
        image.Window.RegisterMemoryImage(request);
        bool installed = false;
        try
        {
            var value = new Native.ImageMemoryOptionsValue
            {
                Size = 48, Version = 0x10000, Generation = checked((ulong)options.Generation),
                Format = (uint)options.Format, SourceWidth = (uint)options.SourceWidth, SourceHeight = (uint)options.SourceHeight,
                OutputWidth = (uint)options.OutputWidth, OutputHeight = (uint)options.OutputHeight,
                HintWidth = (uint)options.HintWidth, HintHeight = (uint)options.HintHeight
            };
            int status;
            fixed (byte* bytes = encoded)
                status = Native.ImageMemorySet(image.Handle, in value, bytes, checked((uint)encoded.Length),
                    &Window.ImageMemoryTrampoline, GCHandle.ToIntPtr(request.root));
            if (status == 0)
            {
                installed = true;
                previous?.Retire();
            }
            image.Window.Check(status);
            return request;
        }
        catch (Exception error)
        {
            try
            {
                if (installed) request.Dispose();
                else
                {
                    request.Retire();
                    if (previous is not null) image.Window.RegisterMemoryImage(previous);
                }
            }
            catch (Exception cleanup) { throw new AggregateException(error, cleanup); }
            throw;
        }
    }

    internal static MemoryImageState Decode(Native.ImageMemoryStateValue value)
    {
        if (value.Size != 40 || value.Version != 0x10000 || value.Reserved != 0 || value.Generation > long.MaxValue ||
            value.Status > 4 || (value.Status != 0 && value.Generation == 0) ||
            value.SourceWidth > 16384 || value.SourceHeight > 16384 ||
            (ulong)value.SourceWidth * value.SourceHeight > 16 * 1024 * 1024 ||
            value.PixelWidth > 1024 || value.PixelHeight > 1024 ||
            (value.Status == 2 && (value.SourceWidth == 0 || value.SourceHeight == 0 || value.PixelWidth == 0 || value.PixelHeight == 0)))
            throw new InvalidOperationException("The native runtime returned an invalid Image memory state.");
        return new(checked((long)value.Generation), (MemoryImageStatus)value.Status,
            (int)value.SourceWidth, (int)value.SourceHeight, (int)value.PixelWidth, (int)value.PixelHeight);
    }

    internal void Raise(Native.ImageMemoryStateValue state, Native.Text error)
    {
        var decoded = Decode(state);
        if (decoded.Status is not (MemoryImageStatus.Ready or MemoryImageStatus.Error or MemoryImageStatus.PresentationFailed))
            throw new InvalidOperationException("The native Image callback returned a nonterminal notification.");
        if (error.Length > 16384 || (error.Length != 0 && error.Data is null))
            throw new InvalidOperationException("The native Image callback returned an invalid error buffer.");
        string? message = error.Length == 0 ? null : Utf8.GetString(new ReadOnlySpan<byte>(error.Data, checked((int)error.Length)));
        if (message is { } text && (text.Length > 4096 || text.Contains('\0')))
            throw new InvalidOperationException("The native Image error exceeds the supported message contract.");
        if (decoded.Status is MemoryImageStatus.Error or MemoryImageStatus.PresentationFailed && string.IsNullOrWhiteSpace(message))
            throw new InvalidOperationException("The native Image failure did not supply an error message.");
        changed?.Invoke(new(decoded, message));
    }

    internal void Retire()
    {
        if (retired) return;
        retired = stopping = true;
        changed = null;
        Window.ForgetMemoryImage(this);
        if (root.IsAllocated) root.Free();
    }

    public void Dispose()
    {
        if (retired) return;
        Window.Guard();
        stopping = true;
        changed = null;
        if (Scope is not { Retired: true }) Window.Check(Native.ImageMemoryCancel(ImageHandle));
        Retire();
    }
}

public sealed unsafe partial class Window
{
    private readonly Dictionary<ulong, MemoryImageRequest> memoryImages = [];
    internal void ReserveMemoryImage() => memoryImages.EnsureCapacity(checked(memoryImages.Count + 1));
    internal MemoryImageRequest? FindMemoryImage(ulong image) => memoryImages.GetValueOrDefault(image);
    internal void RegisterMemoryImage(MemoryImageRequest request) => memoryImages[request.ImageHandle] = request;
    internal void ForgetMemoryImage(MemoryImageRequest request)
    {
        if (ReferenceEquals(memoryImages.GetValueOrDefault(request.ImageHandle), request))
            memoryImages.Remove(request.ImageHandle);
    }
    internal IEnumerable<ulong> MemoryImageHandles(ContentUpdate scope) =>
        memoryImages.Where(pair => ReferenceEquals(pair.Value.Scope, scope)).Select(pair => pair.Key);
    internal void RetireMemoryImages(ContentUpdate? scope = null, Func<ulong, bool>? predicate = null)
    {
        foreach (var pair in memoryImages.Where(pair =>
            (scope is null || ReferenceEquals(scope, pair.Value.Scope)) && (predicate is null || predicate(pair.Key))).ToArray())
            pair.Value.Retire();
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    internal static int ImageMemoryTrampoline(nint context, Native.ImageMemoryStateValue* state, Native.Text error)
    {
        MemoryImageRequest? request = null;
        try
        {
            request = GCHandle.FromIntPtr(context).Target as MemoryImageRequest;
            if (request is null) return 8;
            if (!request.AcceptCallbacks) return 0;
            if (state is null) throw new InvalidOperationException("The native Image callback supplied no state.");
            var window = request.Window;
            using var content = window.EnterContent(null);
            ++window.callbacks;
            try { request.Raise(*state, error); }
            finally { --window.callbacks; }
            return 0;
        }
        catch (Exception failure) { return request is null ? 8 : request.Window.ContentError(request.Scope, failure); }
    }
}

internal static unsafe partial class Native
{
    internal static readonly Lazy<bool> ImageMemoryAvailable = new(() =>
        HasExports("xui_image_memory_version", "xui_image_memory_set", "xui_image_memory_cancel",
            "xui_image_memory_get_state", "xui_image_memory_get_statistics") && ImageMemoryVersion() == 0x10000);
    [StructLayout(LayoutKind.Sequential)]
    internal struct ImageMemoryOptionsValue
    {
        internal uint Size, Version;
        internal ulong Generation;
        internal uint Format, SourceWidth, SourceHeight, OutputWidth, OutputHeight, HintWidth, HintHeight, Reserved;
    }
    [StructLayout(LayoutKind.Sequential)]
    internal struct ImageMemoryStateValue
    {
        internal uint Size, Version;
        internal ulong Generation;
        internal uint Status, SourceWidth, SourceHeight, PixelWidth, PixelHeight, Reserved;
    }
    [StructLayout(LayoutKind.Sequential)]
    internal struct ImageMemoryStatisticsValue { internal uint Size, Version; internal ulong Bytes, Peak, Limit; }
    [LibraryImport("xui", EntryPoint = "xui_image_memory_version")] internal static partial uint ImageMemoryVersion();
    [LibraryImport("xui", EntryPoint = "xui_image_memory_set")]
    internal static partial int ImageMemorySet(ulong image, in ImageMemoryOptionsValue options, byte* bytes, uint length,
        delegate* unmanaged[Cdecl]<nint, ImageMemoryStateValue*, Text, int> callback, nint context);
    [LibraryImport("xui", EntryPoint = "xui_image_memory_cancel")] internal static partial int ImageMemoryCancel(ulong image);
    [LibraryImport("xui", EntryPoint = "xui_image_memory_get_state")] internal static partial int ImageMemoryGetState(ulong image, ref ImageMemoryStateValue state);
    [LibraryImport("xui", EntryPoint = "xui_image_memory_get_statistics")] internal static partial int ImageMemoryGetStatistics(ref ImageMemoryStatisticsValue statistics);
}
