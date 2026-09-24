using System.Runtime.InteropServices;
using System.Runtime.CompilerServices;

namespace Xui;

/// <summary>An inspection key and a borrowed element from one content candidate.</summary>
public readonly record struct ContentInspectionTarget(int NodeId, Element Element);

/// <summary>The current-layout result of a renderer-only selection outline.</summary>
public enum ContentHighlightResult { Applied, Cleared, NotVisible, OccludedNative, UnsupportedSurface }

/// <summary>A stable layout element whose content changes through an explicit UI-thread ownership scope.</summary>
public sealed partial class ContentHost : Element
{
    internal ContentUpdate? Current;
    internal ContentHost(Window window, ulong handle) : base(window, handle) { }

    /// <summary>The borrowed window that owns this host and its content scopes.</summary>
    public Window OwnerWindow => Window;

    public ContentUpdate BeginUpdate()
    {
        Window.Guard();
        Window.Check(Native.ContentBegin(Handle, out var scope));
        try
        {
            var update = new ContentUpdate(this, scope);
            Window.RegisterContent(update);
            return update;
        }
        catch
        {
            Window.Check(Native.ContentRelease(scope));
            throw;
        }
    }

    public void Clear()
    {
        Window.Guard();
        Window.Check(Native.ContentClear(Handle));
        Current?.Retire();
        Current = null;
    }

    /// <summary>Consumes primary pointer gestures for inspection. Keyboard and UIA remain interactive.</summary>
    public void SetPointerPickMode(bool enabled)
    {
        Window.Guard();
        Window.Check(Native.ContentPointerPicking(Handle, enabled ? 1u : 0u));
    }

    /// <summary>Finds a registered node at window-client coordinates in device-independent pixels.</summary>
    public bool TryHitTest(float x, float y, out int nodeId)
    {
        Window.Guard();
        Window.Check(Native.ContentHitTest(Handle, x, y, out var key, out var found));
        nodeId = found != 0 ? checked((int)key) : -1;
        return found != 0;
    }
}

/// <summary>Owns one candidate and, after commit, its active content and callback registrations.</summary>
/// <remarks>Dispose before commit rolls back construction. Dispose after commit clears this content if still current.
/// Authored code is trusted. Arbitrary side effects, hangs, and native failures are not isolated.</remarks>
public sealed partial class ContentUpdate : IDisposable
{
    internal readonly ContentHost Host;
    internal readonly ulong Handle;
    internal bool Retired { get; private set; }
    internal bool Committed { get; private set; }
    internal Exception? Failure { get; private set; }
    internal bool AcceptCallbacks => !Retired && Failure is null;
    private readonly object postedGate = new();
    private readonly HashSet<Window.PostedAction> posts = [];
    private GCHandle pickRoot;

    /// <summary>Reports a registered node after a primary pointer gesture. Lifetime belongs to this scope.</summary>
    /// <remarks>Inspection observers run in window context so they can queue a replacement.</remarks>
    public event Action<int>? Picked;

    /// <summary>Opts into containment of managed event exceptions for this content.
    /// Delivery occurs outside the failed callback. Further callbacks stop until content is replaced or cleared.</summary>
    public event Action<Exception>? CallbackFailed;
    public Exception? CallbackError { get { Host.Window.VerifyAccess(); return Failure; } }

    internal ContentUpdate(ContentHost host, ulong handle) { Host = host; Handle = handle; }

    /// <summary>Queues work owned by this content. Returns false after retirement or a managed callback error.</summary>
    public bool Post(Action action) => Host.Window.PostContent(action, this);

    /// <summary>Registers dense, unique node IDs before commit. The scope does not retain managed elements.</summary>
    public unsafe void SetInspectionTargets(IReadOnlyList<ContentInspectionTarget> targets)
    {
        ArgumentNullException.ThrowIfNull(targets);
        Host.Window.Guard();
        ObjectDisposedException.ThrowIf(Retired, this);
        if (Committed) throw new InvalidOperationException("Register inspection targets before committing content.");
        if (targets.Count is < 1 or > 65536) throw new ArgumentOutOfRangeException(nameof(targets));
        var native = new Native.ContentInspectionTarget[targets.Count];
        for (int i = 0; i < native.Length; i++)
        {
            var target = targets[i];
            ArgumentNullException.ThrowIfNull(target.Element);
            if (target.NodeId < 0) throw new ArgumentOutOfRangeException(nameof(targets));
            target.Element.BelongsTo(Host.Window);
            native[i] = new() { Key = (uint)target.NodeId, Element = target.Element.Handle };
        }
        bool allocated = !pickRoot.IsAllocated;
        if (allocated) pickRoot = GCHandle.Alloc(this, GCHandleType.Weak);
        try
        {
            fixed (Native.ContentInspectionTarget* span = native)
                Host.Window.Check(Native.ContentInspectionTargets(Handle, span, (uint)native.Length,
                    &Window.ContentPickTrampoline, GCHandle.ToIntPtr(pickRoot)));
        }
        catch
        {
            if (allocated) pickRoot.Free();
            throw;
        }
    }

    internal void RaisePick(int nodeId) => Picked?.Invoke(nodeId);

    /// <summary>Outlines a registered node without changing native regions or input. Null clears the outline.</summary>
    /// <remarks>Applied describes the current layout. Later unsafe or hidden geometry hides the outline.</remarks>
    public ContentHighlightResult Highlight(int? nodeId)
    {
        Host.Window.Guard();
        ObjectDisposedException.ThrowIf(Retired, this);
        if (!Committed) throw new InvalidOperationException("Commit content before highlighting a node.");
        if (nodeId < 0) throw new ArgumentOutOfRangeException(nameof(nodeId));
        Host.Window.Check(Native.ContentHighlight(Handle, (uint)nodeId.GetValueOrDefault(),
            nodeId.HasValue ? 0u : 1u, out var result));
        if (!Enum.IsDefined((ContentHighlightResult)result)) throw new InvalidOperationException("Native highlight returned an invalid result.");
        return (ContentHighlightResult)result;
    }

    public void Commit(Element root)
    {
        ArgumentNullException.ThrowIfNull(root);
        Host.Window.Guard();
        ObjectDisposedException.ThrowIf(Retired, this);
        if (Committed) throw new InvalidOperationException("The content update is already committed.");
        if (Failure is not null) throw new InvalidOperationException("Candidate construction raised a callback error.", Failure);
        root.BelongsTo(Host.Window);
        Host.Window.Check(Native.ContentCommit(Handle, root.Handle));
        Committed = true;
        Host.Window.EndContentBuild(this);
        var previous = Host.Current;
        Host.Current = this;
        previous?.Retire();
        if (Failure is { } error) ReportFailure(error);
    }

    internal bool Fail(Exception error)
    {
        if (CallbackFailed is null) return false;
        if (!AcceptCallbacks) return true;
        Failure = error;
        if (Committed) ReportFailure(error);
        return true;
    }

    private void ReportFailure(Exception error)
    {
        if (!Host.Window.PostUnscoped(() =>
        {
            if (!Retired) CallbackFailed?.Invoke(error);
        }))
            Console.Error.WriteLine($"Content callback failed after window close: {error}");
    }

    internal bool Track(Window.PostedAction post)
    {
        lock (postedGate)
        {
            if (!AcceptCallbacks) return false;
            if (posts.Count >= 256) throw new InvalidOperationException("A content scope supports at most 256 queued callbacks.");
            posts.Add(post);
            return true;
        }
    }
    internal void Untrack(Window.PostedAction post) { lock (postedGate) posts.Remove(post); }

    internal void Retire()
    {
        if (Retired) return;
        Retired = true;
        append = null;
        Host.Window.EndContentBuild(this);
        Host.Window.RetireContent(this);
        lock (postedGate)
        {
            foreach (var post in posts) post.Action = null;
            posts.Clear();
        }
        CallbackFailed = null;
        Picked = null;
        if (pickRoot.IsAllocated) pickRoot.Free();
        Failure = null;
    }

    public void Dispose()
    {
        if (Retired) return;
        if (Host.Window.Handle != 0)
        {
            Host.Window.Guard();
            Host.Window.Check(Native.ContentRelease(Handle));
        }
        if (ReferenceEquals(Host.Current, this)) Host.Current = null;
        Retire();
    }
}

public sealed unsafe partial class Window
{
    private readonly Dictionary<ulong, ContentUpdate> contentScopes = [];
    private readonly AsyncLocal<ContentUpdate?> contentContext = new();

    public ContentHost CreateContentHost()
    {
        Guard();
        Check(Native.ContentHostCreate(Handle, out var host));
        return new(this, host);
    }

    internal void RegisterContent(ContentUpdate scope)
    {
        contentScopes.Add(scope.Handle, scope);
        contentContext.Value = scope;
    }
    internal void BeginContentBuild(ContentUpdate scope) => contentContext.Value = scope;
    internal void EndContentBuild(ContentUpdate scope)
    {
        if (ReferenceEquals(contentContext.Value, scope)) contentContext.Value = null;
    }
    internal ContentUpdate? ScopeFor(ulong handle)
    {
        Check(Native.ContentOwner(handle, out var scope));
        return scope == 0 ? null : contentScopes.GetValueOrDefault(scope)
            ?? throw new InvalidOperationException("The content scope has no managed owner.");
    }
    internal void GuardWindowCallback()
    {
        Guard();
        if (contentContext.Value is not null) throw new InvalidOperationException("Content scopes cannot replace window callbacks.");
    }
    internal readonly struct ContentCall : IDisposable
    {
        private readonly Window window;
        private readonly ContentUpdate? previous;
        private readonly ulong previousNative;
        internal ContentCall(Window window, ContentUpdate? scope)
        {
            this.window = window;
            previous = window.contentContext.Value;
            window.Check(Native.ContentContext(window.Handle, scope?.Handle ?? 0, out previousNative));
            window.contentContext.Value = scope;
        }
        public void Dispose()
        {
            window.contentContext.Value = previous;
            window.Check(Native.ContentContext(window.Handle, previousNative, out _));
        }
    }
    internal ContentCall EnterContent(ContentUpdate? scope) => new(this, scope);
    internal bool PostContent(Action action, ContentUpdate scope) => PostCore(action, scope);
    internal int ContentError(ContentUpdate? scope, Exception error)
    {
        if (scope?.Fail(error) == true) return 0;
        callbackError = error;
        return 8;
    }
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    internal static int ContentPickTrampoline(nint context, Native.Event* value)
    {
        ContentUpdate? scope = null;
        try
        {
            scope = GCHandle.FromIntPtr(context).Target as ContentUpdate;
            if (scope is null) return 8;
            if (!scope.AcceptCallbacks) return 0;
            var window = scope.Host.Window;
            using var content = window.EnterContent(null);
            ++window.callbacks;
            try { scope.RaisePick(checked((int)value->Value)); }
            finally { --window.callbacks; }
            return 0;
        }
        catch (Exception error)
        {
            return scope is null ? 8 : scope.Host.Window.ContentError(scope, error);
        }
    }
    internal void RetireContent(ContentUpdate scope)
    {
        RetireContentCallbacks(scope, _ => true);
        contentScopes.Remove(scope.Handle);
    }
    internal HashSet<ulong> ContentCallbackHandles(ContentUpdate scope) =>
        subscriptions.Where(p => ReferenceEquals(p.Value.Scope, scope)).Select(p => p.Key)
        .Concat(menuSubscriptions.Where(p => ReferenceEquals(p.Value.Scope, scope)).Select(p => p.Key))
        .Concat(fileSubscriptions.Where(p => ReferenceEquals(p.Value.Scope, scope)).Select(p => p.Key))
        .Concat(millerSubscriptions.Where(p => ReferenceEquals(p.Value.Scope, scope)).Select(p => p.Key))
        .Concat(VirtualViewportHandles(scope)).Concat(ControlInteractionHandles(scope)).Concat(MemoryImageHandles(scope)).ToHashSet();
    internal void RetireContentCallbacks(ContentUpdate scope, Func<ulong, bool> predicate)
    {
        RetireVirtualViewports(scope, predicate);
        RetireControlInteractions(scope, predicate);
        RetireMemoryImages(scope, predicate);
        foreach (var item in subscriptions.Where(p => ReferenceEquals(p.Value.Scope, scope) && predicate(p.Key)).ToArray())
        { subscriptions.Remove(item.Key); item.Value.Free(); }
        foreach (var item in menuSubscriptions.Where(p => ReferenceEquals(p.Value.Scope, scope) && predicate(p.Key)).ToArray())
        { menuSubscriptions.Remove(item.Key); item.Value.Free(); }
        foreach (var item in fileSubscriptions.Where(p => ReferenceEquals(p.Value.Scope, scope) && predicate(p.Key)).ToArray())
        { fileSubscriptions.Remove(item.Key); item.Value.Root.Free(); }
        foreach (var item in millerSubscriptions.Where(p => ReferenceEquals(p.Value.Scope, scope) && predicate(p.Key)).ToArray())
        { millerSubscriptions.Remove(item.Key); item.Value.Free(); }
    }
}

internal static unsafe partial class Native
{
    [StructLayout(LayoutKind.Sequential)]
    internal struct ContentInspectionTarget { internal uint Key, Reserved; internal ulong Element; }
    [LibraryImport("xui", EntryPoint = "xui_content_host_create")]
    internal static partial int ContentHostCreate(ulong window, out ulong host);
    [LibraryImport("xui", EntryPoint = "xui_content_begin")]
    internal static partial int ContentBegin(ulong host, out ulong scope);
    [LibraryImport("xui", EntryPoint = "xui_content_commit")]
    internal static partial int ContentCommit(ulong scope, ulong root);
    [LibraryImport("xui", EntryPoint = "xui_content_release")]
    internal static partial int ContentRelease(ulong scope);
    [LibraryImport("xui", EntryPoint = "xui_content_clear")]
    internal static partial int ContentClear(ulong host);
    [LibraryImport("xui", EntryPoint = "xui_content_context")]
    internal static partial int ContentContext(ulong window, ulong scope, out ulong previous);
    [LibraryImport("xui", EntryPoint = "xui_content_owner")]
    internal static partial int ContentOwner(ulong target, out ulong scope);
    [LibraryImport("xui", EntryPoint = "xui_content_handle_count")]
    internal static partial int ContentHandleCount(ulong window, out uint count);
    [LibraryImport("xui", EntryPoint = "xui_content_inspection_targets")]
    internal static partial int ContentInspectionTargets(ulong scope, ContentInspectionTarget* targets, uint count,
        delegate* unmanaged[Cdecl]<nint, Event*, int> callback, nint context);
    [LibraryImport("xui", EntryPoint = "xui_content_pointer_picking")]
    internal static partial int ContentPointerPicking(ulong host, uint enabled);
    [LibraryImport("xui", EntryPoint = "xui_content_hit_test")]
    internal static partial int ContentHitTest(ulong host, float x, float y, out uint key, out uint found);
    [LibraryImport("xui", EntryPoint = "xui_content_highlight")]
    internal static partial int ContentHighlight(ulong scope, uint key, uint clear, out uint result);
}
