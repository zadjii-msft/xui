using System.Runtime.InteropServices;

namespace Xui;

/// <summary>A stable layout element whose content changes through an explicit UI-thread ownership scope.</summary>
public sealed class ContentHost : Element
{
    internal ContentUpdate? Current;
    internal ContentHost(Window window, ulong handle) : base(window, handle) { }

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
}

/// <summary>Owns one candidate and, after commit, its active content and callback registrations.</summary>
/// <remarks>Dispose before commit rolls back construction. Dispose after commit clears this content if still current.
/// Authored code is trusted. Arbitrary side effects, hangs, and native failures are not isolated.</remarks>
public sealed class ContentUpdate : IDisposable
{
    internal readonly ContentHost Host;
    internal readonly ulong Handle;
    internal bool Retired { get; private set; }
    internal bool Committed { get; private set; }
    internal Exception? Failure { get; private set; }
    internal bool AcceptCallbacks => !Retired && Failure is null;
    private readonly object postedGate = new();
    private readonly HashSet<Window.PostedAction> posts = [];

    /// <summary>Opts into containment of managed event exceptions for this content.
    /// Delivery occurs outside the failed callback. Further callbacks stop until content is replaced or cleared.</summary>
    public event Action<Exception>? CallbackFailed;
    public Exception? CallbackError { get { Host.Window.VerifyAccess(); return Failure; } }

    internal ContentUpdate(ContentHost host, ulong handle) { Host = host; Handle = handle; }

    /// <summary>Queues work owned by this content. Returns false after retirement or a managed callback error.</summary>
    public bool Post(Action action) => Host.Window.PostContent(action, this);

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
        Host.Window.EndContentBuild(this);
        Host.Window.RetireContent(this);
        lock (postedGate)
        {
            foreach (var post in posts) post.Action = null;
            posts.Clear();
        }
        CallbackFailed = null;
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
    internal void RetireContent(ContentUpdate scope)
    {
        foreach (var item in subscriptions.Where(p => ReferenceEquals(p.Value.Scope, scope)).ToArray())
        { subscriptions.Remove(item.Key); item.Value.Free(); }
        foreach (var item in menuSubscriptions.Where(p => ReferenceEquals(p.Value.Scope, scope)).ToArray())
        { menuSubscriptions.Remove(item.Key); item.Value.Free(); }
        foreach (var item in fileSubscriptions.Where(p => ReferenceEquals(p.Value.Scope, scope)).ToArray())
        { fileSubscriptions.Remove(item.Key); item.Value.Root.Free(); }
        foreach (var item in millerSubscriptions.Where(p => ReferenceEquals(p.Value.Scope, scope)).ToArray())
        { millerSubscriptions.Remove(item.Key); item.Value.Free(); }
        contentScopes.Remove(scope.Handle);
    }
}

internal static partial class Native
{
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
}
