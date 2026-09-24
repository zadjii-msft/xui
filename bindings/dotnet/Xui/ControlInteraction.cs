using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Xui;

public readonly record struct ControlInteraction(bool HasFocus, bool IsComposing);

public abstract partial class Control
{
    /// <summary>Whether native self-focus and composition snapshots can be observed without polling.</summary>
    public static bool SupportsInteraction => Native.ControlInteractionAvailable.Value;

    public ControlInteraction GetInteraction()
    {
        Window.Guard();
        ControlInteractionSubscription.RequireSupport();
        var value = new Native.ControlInteractionValue { Size = 16, Version = 0x10000 };
        Window.Check(Native.ControlInteractionGet(Handle, ref value));
        return ControlInteractionSubscription.Decode(value);
    }

    /// <summary>Owns the control's separate interaction observer without replacing Changed or Submitted handlers.</summary>
    /// <remarks>The native callback supplies a read-only snapshot. Queue application mutations with PostUnscoped.
    /// An initial snapshot and subsequent changes are posted, never delivered inline by this method.</remarks>
    public ControlInteractionSubscription ObserveInteraction(Action<ControlInteraction> changed) =>
        ControlInteractionSubscription.Create(this, changed);
}

public sealed unsafe class ControlInteractionSubscription : IDisposable
{
    internal Window Window { get; }
    internal ContentUpdate? Scope { get; }
    internal ulong Handle { get; }
    internal bool AcceptCallbacks => !stopping && !retired && Scope is not { AcceptCallbacks: false };
    private Action<ControlInteraction>? changed;
    private GCHandle callbackRoot;
    private bool stopping;
    private bool retired;

    private ControlInteractionSubscription(Control control, Action<ControlInteraction> changed)
    {
        Window = control.Window;
        Handle = control.Handle;
        Scope = Window.ScopeFor(Handle);
        this.changed = changed;
    }

    internal static void RequireSupport()
    {
        if (!Control.SupportsInteraction)
            throw new NotSupportedException("This native XUI runtime does not support control interaction observations.");
    }

    internal static ControlInteractionSubscription Create(Control control, Action<ControlInteraction> changed)
    {
        ArgumentNullException.ThrowIfNull(changed);
        control.Window.Guard();
        RequireSupport();
        _ = control.GetInteraction();
        control.Window.ReserveControlInteraction(control.Handle);
        var subscription = new ControlInteractionSubscription(control, changed);
        subscription.callbackRoot = GCHandle.Alloc(subscription, GCHandleType.Weak);
        control.Window.RegisterControlInteraction(subscription);
        try
        {
            control.Window.Check(Native.ControlInteractionSubscribe(control.Handle, &Window.ControlInteractionTrampoline,
                GCHandle.ToIntPtr(subscription.callbackRoot)));
            return subscription;
        }
        catch (Exception error)
        {
            try { subscription.Dispose(); }
            catch (Exception cleanup) { throw new AggregateException(error, cleanup); }
            throw;
        }
    }

    internal static ControlInteraction Decode(Native.ControlInteractionValue value)
    {
        if (value.Size != 16 || value.Version != 0x10000 || value.HasFocus > 1 || value.IsComposing > 1)
            throw new InvalidOperationException("The native runtime returned an invalid interaction snapshot.");
        return new(value.HasFocus != 0, value.IsComposing != 0);
    }

    internal void Raise(ControlInteraction value) => changed?.Invoke(value);

    internal void Retire()
    {
        if (retired) return;
        retired = stopping = true;
        changed = null;
        Window.ForgetControlInteraction(Handle);
        if (callbackRoot.IsAllocated) callbackRoot.Free();
    }

    public void Dispose()
    {
        if (retired) return;
        Window.Guard();
        stopping = true;
        changed = null;
        if (Scope is not { Retired: true })
            Window.Check(Native.ControlInteractionSubscribe(Handle, null, 0));
        Retire();
    }
}

public sealed unsafe partial class Window
{
    private readonly Dictionary<ulong, ControlInteractionSubscription> controlInteractions = [];

    internal void ReserveControlInteraction(ulong handle)
    {
        if (controlInteractions.ContainsKey(handle))
            throw new InvalidOperationException("This control already has an owned interaction observer.");
        controlInteractions.EnsureCapacity(checked(controlInteractions.Count + 1));
    }
    internal void RegisterControlInteraction(ControlInteractionSubscription subscription) =>
        controlInteractions.Add(subscription.Handle, subscription);
    internal void ForgetControlInteraction(ulong handle) => controlInteractions.Remove(handle);
    internal IEnumerable<ulong> ControlInteractionHandles(ContentUpdate scope) =>
        controlInteractions.Where(pair => ReferenceEquals(pair.Value.Scope, scope)).Select(pair => pair.Key);

    internal void RetireControlInteractions(ContentUpdate? scope = null, Func<ulong, bool>? predicate = null)
    {
        foreach (var pair in controlInteractions.Where(pair =>
            (scope is null || ReferenceEquals(pair.Value.Scope, scope)) && (predicate is null || predicate(pair.Key))).ToArray())
            pair.Value.Retire();
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    internal static int ControlInteractionTrampoline(nint context, Native.ControlInteractionValue* value)
    {
        ControlInteractionSubscription? subscription = null;
        try
        {
            subscription = GCHandle.FromIntPtr(context).Target as ControlInteractionSubscription;
            if (subscription is null) return 8;
            if (!subscription.AcceptCallbacks) return 0;
            if (value is null) throw new InvalidOperationException("The native interaction callback supplied no snapshot.");
            var window = subscription.Window;
            using var content = window.EnterContent(null);
            ++window.callbacks;
            try { subscription.Raise(ControlInteractionSubscription.Decode(*value)); }
            finally { --window.callbacks; }
            return 0;
        }
        catch (Exception error) { return subscription is null ? 8 : subscription.Window.ContentError(subscription.Scope, error); }
    }
}

internal static unsafe partial class Native
{
    internal static readonly Lazy<bool> ControlInteractionAvailable = new(() =>
        HasExports("xui_control_interaction_get", "xui_control_interaction_subscribe"));

    [StructLayout(LayoutKind.Sequential)]
    internal struct ControlInteractionValue { internal uint Size, Version, HasFocus, IsComposing; }
    [LibraryImport("xui", EntryPoint = "xui_control_interaction_get")]
    internal static partial int ControlInteractionGet(ulong control, ref ControlInteractionValue value);
    [LibraryImport("xui", EntryPoint = "xui_control_interaction_subscribe")]
    internal static partial int ControlInteractionSubscribe(ulong control,
        delegate* unmanaged[Cdecl]<nint, ControlInteractionValue*, int> callback, nint context);
}
