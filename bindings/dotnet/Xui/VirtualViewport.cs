using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Xui;

public readonly record struct VirtualViewportRect(float Offset, float Width, float Height, float Extent);
public readonly record struct VirtualViewportRequest(long Epoch, long CommittedSourceVersion, long RequestedSourceVersion,
    VirtualViewportRect Committed, VirtualViewportRect Requested, bool IsBlocked);
public enum VirtualViewportUpdateResult { Ready, Superseded, Blocked }
public enum VirtualViewportCommitResult { Committed, Superseded, Blocked }

public sealed partial class ScrollView
{
    /// <summary>Whether the loaded runtime supports the versioned, explicitly leased virtual viewport protocol.</summary>
    public static bool SupportsVirtualViewport => Native.VirtualViewportAvailable.Value;

    /// <summary>Enables a fixed-pitch viewport whose logical publication is controlled by its lease.</summary>
    /// <remarks>Callbacks are posted snapshots, never synchronous lease-operation callbacks.
    /// Native paint and accessibility may observe valid intermediate staging states.</remarks>
    public VirtualViewportLease BeginVirtualViewport(int itemCount, float rowHeight, long sourceVersion,
        Action<VirtualViewportRequest> requested) =>
        VirtualViewportLease.Create(this, itemCount, rowHeight, sourceVersion, requested);
}

/// <summary>Owns one native viewport subscription and its logical publication epochs.</summary>
public sealed unsafe class VirtualViewportLease : IDisposable
{
    internal Window Window { get; }
    internal ContentUpdate? Scope { get; }
    internal ulong Handle { get; private set; }
    internal bool AcceptCallbacks => !stopping && !retired && Scope is not { AcceptCallbacks: false };
    private readonly float rowHeight;
    private Action<VirtualViewportRequest>? requested;
    private GCHandle callbackRoot;
    private bool stopping;
    private bool retired;
    private long readyEpoch;
    private long committedEpoch;

    private VirtualViewportLease(ScrollView scroll, float rowHeight, Action<VirtualViewportRequest> requested)
    {
        Window = scroll.Window;
        Scope = Window.ScopeFor(scroll.Handle);
        this.rowHeight = rowHeight;
        this.requested = requested;
    }

    internal static VirtualViewportLease Create(ScrollView scroll, int itemCount, float rowHeight, long sourceVersion,
        Action<VirtualViewportRequest> requested)
    {
        ArgumentNullException.ThrowIfNull(requested);
        scroll.Window.Guard();
        if (!ScrollView.SupportsVirtualViewport)
            throw new NotSupportedException("This native XUI runtime does not support leased virtual viewports.");
        ValidateExtent(itemCount, rowHeight, sourceVersion);
        scroll.Window.ReserveVirtualViewport();
        var lease = new VirtualViewportLease(scroll, rowHeight, requested);
        lease.callbackRoot = GCHandle.Alloc(lease, GCHandleType.Weak);
        try
        {
            var options = new Native.VirtualViewportOptions
            {
                Size = 24, Version = 0x10000, ItemCount = checked((uint)itemCount),
                RowHeight = rowHeight, SourceVersion = checked((ulong)sourceVersion)
            };
            int status = Native.VirtualViewportBegin(scroll.Handle, in options, &Window.VirtualViewportTrampoline,
                GCHandle.ToIntPtr(lease.callbackRoot), out ulong handle);
            lease.Handle = handle;
            if (handle != 0) scroll.Window.RegisterVirtualViewport(lease);
            scroll.Window.Check(status);
            if (handle == 0) throw new InvalidOperationException("The native runtime returned an empty viewport lease.");
            return lease;
        }
        catch (Exception error)
        {
            try { lease.Dispose(); }
            catch (Exception cleanup) { throw new AggregateException(error, cleanup); }
            throw;
        }
    }

    private static void ValidateExtent(int count, float pitch, long sourceVersion)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(count);
        ArgumentOutOfRangeException.ThrowIfNegativeOrZero(sourceVersion);
        if (!float.IsFinite(pitch) || pitch <= 0)
            throw new ArgumentOutOfRangeException(nameof(pitch), "The row pitch must be finite and positive.");
        if (count * (double)pitch > float.MaxValue)
            throw new ArgumentOutOfRangeException(nameof(count), "The logical extent must fit finite native geometry.");
    }

    private void Guard()
    {
        Window.Guard();
        ObjectDisposedException.ThrowIf(stopping || retired || Scope is { Retired: true }, this);
    }

    public void SetExtent(int itemCount, long sourceVersion)
    {
        Guard();
        ValidateExtent(itemCount, rowHeight, sourceVersion);
        Window.Check(Native.VirtualViewportSetExtent(Handle, checked((uint)itemCount), checked((ulong)sourceVersion)));
    }

    public void RequestOffset(float offset)
    {
        Guard();
        if (!float.IsFinite(offset) || offset < 0)
            throw new ArgumentOutOfRangeException(nameof(offset), "The requested offset must be finite and nonnegative.");
        Window.Check(Native.VirtualViewportRequestOffset(Handle, offset));
    }

    /// <summary>Registers a realized row root during its reserved viewport update.</summary>
    /// <remarks>The opaque key is copied as ordinal UTF-16, not parsed as an automation identifier.
    /// The native runtime verifies the row's ownership, placement, and commit coverage.</remarks>
    public void SetItemInfo(Stack row, string key, int index, int count, long sourceVersion)
    {
        ArgumentNullException.ThrowIfNull(row);
        ArgumentNullException.ThrowIfNull(key);
        Guard();
        if (readyEpoch == 0) throw new InvalidOperationException("Virtual row metadata requires a reserved viewport update.");
        if (key.Length is < 1 or > 4096 || key.Contains('\0'))
            throw new ArgumentException("A virtual row key requires 1-4096 UTF-16 units without NUL.", nameof(key));
        ArgumentOutOfRangeException.ThrowIfNegativeOrZero(count);
        if (index < 0 || index >= count) throw new ArgumentOutOfRangeException(nameof(index));
        ArgumentOutOfRangeException.ThrowIfNegativeOrZero(sourceVersion);
        row.BelongsTo(Window);
        fixed (char* text = key)
        {
            var value = new Native.VirtualItemInfoValue
            {
                Size = 40, Version = 0x10000, Index = checked((uint)index), Count = checked((uint)count),
                SourceVersion = checked((ulong)sourceVersion), Key = text, KeyLength = checked((uint)key.Length)
            };
            Window.Check(Native.VirtualViewportSetItem(Handle, row.Handle, in value));
        }
    }

    public VirtualViewportUpdateResult TryBeginUpdate(long expectedEpoch)
    {
        Guard();
        ArgumentOutOfRangeException.ThrowIfNegativeOrZero(expectedEpoch);
        Window.Check(Native.VirtualViewportTryBeginUpdate(Handle, checked((ulong)expectedEpoch), out uint result));
        if (result > 2) throw new InvalidOperationException("The native runtime returned an invalid viewport update result.");
        if (result == 0) readyEpoch = expectedEpoch;
        return (VirtualViewportUpdateResult)result;
    }

    public VirtualViewportCommitResult TryCommit(long expectedEpoch)
    {
        Guard();
        ArgumentOutOfRangeException.ThrowIfNegativeOrZero(expectedEpoch);
        Window.Check(Native.VirtualViewportTryCommit(Handle, checked((ulong)expectedEpoch), out uint result));
        if (result > 2 || (readyEpoch == expectedEpoch && result != 0))
            throw new InvalidOperationException("A reserved native viewport did not commit; detach the failed attachment.");
        if (result == 0 && readyEpoch == expectedEpoch)
        {
            readyEpoch = 0;
            committedEpoch = expectedEpoch;
        }
        return (VirtualViewportCommitResult)result;
    }

    /// <summary>Settles pending native geometry and row-provider updates after committed row pruning.</summary>
    /// <remarks>This is not a vsync, animation, or operating-system accessibility snapshot barrier.</remarks>
    public void FlushCommitted(long expectedEpoch)
    {
        Guard();
        ArgumentOutOfRangeException.ThrowIfNegativeOrZero(expectedEpoch);
        if (readyEpoch != 0 || committedEpoch != expectedEpoch)
            throw new InvalidOperationException("Flush requires the current committed viewport epoch without active preparation.");
        Window.Check(Native.VirtualViewportFlushCommitted(Handle, checked((ulong)expectedEpoch)));
    }

    /// <summary>Abandons preparation before row model commit. It does not undo staged row changes.</summary>
    public void Cancel(long expectedEpoch)
    {
        Guard();
        ArgumentOutOfRangeException.ThrowIfNegativeOrZero(expectedEpoch);
        Window.Check(Native.VirtualViewportCancel(Handle, checked((ulong)expectedEpoch)));
        if (readyEpoch == expectedEpoch) readyEpoch = 0;
    }

    /// <summary>Reads the last logical publication and current requested geometry, not an OS-tree snapshot.</summary>
    public VirtualViewportRequest GetRequest()
    {
        Guard();
        var value = new Native.VirtualViewportRequestValue { Size = 72, Version = 0x10000 };
        Window.Check(Native.VirtualViewportGetRequest(Handle, ref value));
        return Decode(value);
    }

    internal static VirtualViewportRequest Decode(Native.VirtualViewportRequestValue value)
    {
        if (value.Size != 72 || value.Version != 0x10000 || value.Reserved != 0 || value.Blocked > 1 ||
            value.Epoch is 0 or > long.MaxValue || value.RequestedSourceVersion is 0 or > long.MaxValue ||
            value.CommittedSourceVersion > long.MaxValue || value.RequestedSourceVersion < value.CommittedSourceVersion)
            throw new InvalidOperationException("The native runtime returned an invalid viewport request header.");
        static VirtualViewportRect Rect(Native.VirtualViewportRectValue rect)
        {
            if (!float.IsFinite(rect.Offset) || rect.Offset < 0 || !float.IsFinite(rect.Width) || rect.Width < 0 ||
                !float.IsFinite(rect.Height) || rect.Height < 0 || !float.IsFinite(rect.Extent) || rect.Extent < 0)
                throw new InvalidOperationException("The native runtime returned invalid viewport geometry.");
            return new(rect.Offset, rect.Width, rect.Height, rect.Extent);
        }
        return new(checked((long)value.Epoch), checked((long)value.CommittedSourceVersion),
            checked((long)value.RequestedSourceVersion), Rect(value.Committed), Rect(value.Requested), value.Blocked != 0);
    }

    internal void Raise(VirtualViewportRequest value) => requested?.Invoke(value);

    internal void Retire()
    {
        if (retired) return;
        retired = stopping = true;
        requested = null;
        readyEpoch = 0;
        committedEpoch = 0;
        Window.ForgetVirtualViewport(Handle);
        Handle = 0;
        if (callbackRoot.IsAllocated) callbackRoot.Free();
    }

    public void Dispose()
    {
        if (retired) return;
        Window.Guard();
        stopping = true;
        requested = null;
        if (Handle != 0 && Scope is not { Retired: true })
            Window.Check(Native.VirtualViewportRelease(Handle));
        Retire();
    }
}

public sealed unsafe partial class Window
{
    private readonly Dictionary<ulong, VirtualViewportLease> virtualViewports = [];

    internal void ReserveVirtualViewport() => virtualViewports.EnsureCapacity(checked(virtualViewports.Count + 1));
    internal void RegisterVirtualViewport(VirtualViewportLease lease) => virtualViewports.Add(lease.Handle, lease);
    internal void ForgetVirtualViewport(ulong handle) => virtualViewports.Remove(handle);
    internal IEnumerable<ulong> VirtualViewportHandles(ContentUpdate scope) =>
        virtualViewports.Where(pair => ReferenceEquals(pair.Value.Scope, scope)).Select(pair => pair.Key);

    internal void RetireVirtualViewports(ContentUpdate? scope = null, Func<ulong, bool>? predicate = null)
    {
        foreach (var pair in virtualViewports.Where(pair =>
            (scope is null || ReferenceEquals(pair.Value.Scope, scope)) && (predicate is null || predicate(pair.Key))).ToArray())
            pair.Value.Retire();
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    internal static int VirtualViewportTrampoline(nint context, Native.VirtualViewportRequestValue* value)
    {
        VirtualViewportLease? lease = null;
        try
        {
            lease = GCHandle.FromIntPtr(context).Target as VirtualViewportLease;
            if (lease is null) return 8;
            if (!lease.AcceptCallbacks) return 0;
            if (value is null) throw new InvalidOperationException("The native viewport callback supplied no request.");
            var window = lease.Window;
            using var content = window.EnterContent(null);
            ++window.callbacks;
            try { lease.Raise(VirtualViewportLease.Decode(*value)); }
            finally { --window.callbacks; }
            return 0;
        }
        catch (Exception error) { return lease is null ? 8 : lease.Window.ContentError(lease.Scope, error); }
    }
}

internal static unsafe partial class Native
{
    internal static readonly Lazy<bool> VirtualViewportAvailable = new(() =>
        HasExports("xui_virtual_viewport_version", "xui_virtual_viewport_begin", "xui_virtual_viewport_set_extent",
            "xui_virtual_viewport_request_offset", "xui_virtual_viewport_try_begin_update", "xui_virtual_viewport_try_commit",
            "xui_virtual_viewport_cancel", "xui_virtual_viewport_release", "xui_virtual_viewport_get_request",
            "xui_virtual_viewport_set_item", "xui_virtual_viewport_flush_committed") &&
        VirtualViewportVersion() == 0x10000);

    [StructLayout(LayoutKind.Sequential)]
    internal struct VirtualViewportOptions
    {
        internal uint Size, Version, ItemCount;
        internal float RowHeight;
        internal ulong SourceVersion;
    }
    [StructLayout(LayoutKind.Sequential)]
    internal struct VirtualViewportRectValue { internal float Offset, Width, Height, Extent; }
    [StructLayout(LayoutKind.Sequential)]
    internal struct VirtualViewportRequestValue
    {
        internal uint Size, Version;
        internal ulong Epoch, CommittedSourceVersion, RequestedSourceVersion;
        internal VirtualViewportRectValue Committed, Requested;
        internal uint Blocked, Reserved;
    }
    [StructLayout(LayoutKind.Sequential)]
    internal struct VirtualItemInfoValue
    {
        internal uint Size, Version, Index, Count;
        internal ulong SourceVersion;
        internal char* Key;
        internal uint KeyLength, Reserved;
    }
    [LibraryImport("xui", EntryPoint = "xui_virtual_viewport_version")]
    internal static partial uint VirtualViewportVersion();
    [LibraryImport("xui", EntryPoint = "xui_virtual_viewport_begin")]
    internal static partial int VirtualViewportBegin(ulong scroll, in VirtualViewportOptions options,
        delegate* unmanaged[Cdecl]<nint, VirtualViewportRequestValue*, int> callback, nint context, out ulong lease);
    [LibraryImport("xui", EntryPoint = "xui_virtual_viewport_set_extent")]
    internal static partial int VirtualViewportSetExtent(ulong lease, uint itemCount, ulong sourceVersion);
    [LibraryImport("xui", EntryPoint = "xui_virtual_viewport_request_offset")]
    internal static partial int VirtualViewportRequestOffset(ulong lease, float offset);
    [LibraryImport("xui", EntryPoint = "xui_virtual_viewport_try_begin_update")]
    internal static partial int VirtualViewportTryBeginUpdate(ulong lease, ulong epoch, out uint result);
    [LibraryImport("xui", EntryPoint = "xui_virtual_viewport_try_commit")]
    internal static partial int VirtualViewportTryCommit(ulong lease, ulong epoch, out uint result);
    [LibraryImport("xui", EntryPoint = "xui_virtual_viewport_cancel")]
    internal static partial int VirtualViewportCancel(ulong lease, ulong epoch);
    [LibraryImport("xui", EntryPoint = "xui_virtual_viewport_release")]
    internal static partial int VirtualViewportRelease(ulong lease);
    [LibraryImport("xui", EntryPoint = "xui_virtual_viewport_get_request")]
    internal static partial int VirtualViewportGetRequest(ulong lease, ref VirtualViewportRequestValue request);
    [LibraryImport("xui", EntryPoint = "xui_virtual_viewport_set_item")]
    internal static partial int VirtualViewportSetItem(ulong lease, ulong row, in VirtualItemInfoValue info);
    [LibraryImport("xui", EntryPoint = "xui_virtual_viewport_flush_committed")]
    internal static partial int VirtualViewportFlushCommitted(ulong lease, ulong expectedEpoch);
}
