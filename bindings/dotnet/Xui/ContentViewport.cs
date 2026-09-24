using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Xui;

public readonly record struct ContentViewportSize(float Width, float Height);

public sealed partial class ContentHost
{
    public static bool SupportsViewportObservation => Native.ContentViewportAvailable.Value;

    /// <summary>Observes the inner DIP allocation supplied to the content root, including inset changes.</summary>
    /// <remarks>The initial metadata callback is synchronous. Later callbacks are posted read-only snapshots.
    /// The subscription belongs to this persistent host/window, not one replaceable content arena.</remarks>
    public ContentViewportSubscription ObserveViewport(Action<ContentViewportSize> changed) =>
        ContentViewportSubscription.Create(this, changed);
}

public sealed unsafe class ContentViewportSubscription : IDisposable
{
    internal Window Window { get; }
    internal ulong Handle { get; private set; }
    internal bool AcceptCallbacks => !stopping && !retired;
    private Action<ContentViewportSize>? changed;
    private GCHandle callbackRoot;
    private bool stopping, retired;

    private ContentViewportSubscription(ContentHost host, Action<ContentViewportSize> changed)
    {
        Window = host.OwnerWindow;
        this.changed = changed;
    }

    internal static ContentViewportSubscription Create(ContentHost host, Action<ContentViewportSize> changed)
    {
        ArgumentNullException.ThrowIfNull(changed);
        host.OwnerWindow.Guard();
        if (!ContentHost.SupportsViewportObservation)
            throw new NotSupportedException("The native runtime does not provide owned content viewport observation.");
        host.OwnerWindow.ReserveContentViewport();
        var subscription = new ContentViewportSubscription(host, changed);
        subscription.callbackRoot = GCHandle.Alloc(subscription, GCHandleType.Weak);
        try
        {
            var initial = new Native.ContentViewportValue { Size = 16, Version = 0x10000 };
            int status = Native.ContentViewportSubscribe(host.Handle, &Window.ContentViewportTrampoline,
                GCHandle.ToIntPtr(subscription.callbackRoot), ref initial, out ulong handle);
            subscription.Handle = handle;
            if (handle != 0) subscription.Window.RegisterContentViewport(subscription);
            subscription.Window.Check(status);
            if (handle == 0) throw new InvalidOperationException("The native viewport observer returned an empty subscription.");
            changed(Decode(initial));
            return subscription;
        }
        catch (Exception error)
        {
            try { subscription.Dispose(); }
            catch (Exception cleanup) { throw new AggregateException(error, cleanup); }
            throw;
        }
    }

    internal static ContentViewportSize Decode(Native.ContentViewportValue value)
    {
        if (value.Size != 16 || value.Version != 0x10000 || !float.IsFinite(value.Width) || value.Width < 0 ||
            !float.IsFinite(value.Height) || value.Height < 0)
            throw new InvalidOperationException("The native runtime returned an invalid inner content allocation.");
        return new(value.Width, value.Height);
    }
    internal void Raise(ContentViewportSize value) => changed?.Invoke(value);
    internal void Retire()
    {
        if (retired) return;
        retired = stopping = true;
        changed = null;
        Window.ForgetContentViewport(Handle);
        Handle = 0;
        if (callbackRoot.IsAllocated) callbackRoot.Free();
    }
    public void Dispose()
    {
        if (retired) return;
        Window.Guard();
        stopping = true;
        changed = null;
        if (Handle != 0) Window.Check(Native.ContentViewportRelease(Handle));
        Retire();
    }
}

public sealed unsafe partial class Window
{
    private readonly Dictionary<ulong, ContentViewportSubscription> contentViewports = [];
    internal void ReserveContentViewport() => contentViewports.EnsureCapacity(checked(contentViewports.Count + 1));
    internal void RegisterContentViewport(ContentViewportSubscription subscription) => contentViewports.Add(subscription.Handle, subscription);
    internal void ForgetContentViewport(ulong handle) => contentViewports.Remove(handle);
    internal void RetireContentViewports()
    {
        foreach (var subscription in contentViewports.Values.ToArray()) subscription.Retire();
    }
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    internal static int ContentViewportTrampoline(nint context, Native.ContentViewportValue* value)
    {
        ContentViewportSubscription? subscription = null;
        try
        {
            subscription = GCHandle.FromIntPtr(context).Target as ContentViewportSubscription;
            if (subscription is null) return 8;
            if (!subscription.AcceptCallbacks) return 0;
            if (value is null) throw new InvalidOperationException("The native content viewport callback supplied no snapshot.");
            var window = subscription.Window;
            using var content = window.EnterContent(null);
            ++window.callbacks;
            try { subscription.Raise(ContentViewportSubscription.Decode(*value)); }
            finally { --window.callbacks; }
            return 0;
        }
        catch (Exception error) { return subscription is null ? 8 : subscription.Window.ContentError(null, error); }
    }
}

internal static unsafe partial class Native
{
    internal static readonly Lazy<bool> ContentViewportAvailable = new(() =>
        HasExports("xui_content_viewport_version", "xui_content_viewport_subscribe", "xui_content_viewport_release") &&
        ContentViewportVersion() == 0x10000);
    [StructLayout(LayoutKind.Sequential)]
    internal struct ContentViewportValue { internal uint Size, Version; internal float Width, Height; }
    [LibraryImport("xui", EntryPoint = "xui_content_viewport_version")]
    internal static partial uint ContentViewportVersion();
    [LibraryImport("xui", EntryPoint = "xui_content_viewport_subscribe")]
    internal static partial int ContentViewportSubscribe(ulong host,
        delegate* unmanaged[Cdecl]<nint, ContentViewportValue*, int> callback, nint context,
        ref ContentViewportValue initial, out ulong subscription);
    [LibraryImport("xui", EntryPoint = "xui_content_viewport_release")]
    internal static partial int ContentViewportRelease(ulong subscription);
}
