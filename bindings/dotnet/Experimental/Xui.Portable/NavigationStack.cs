using System.Collections.ObjectModel;

namespace Xui.Experimental.Portable;

/// <summary>An application-owned navigation history with explicit ownership of page resources.</summary>
/// <remarks>
/// Page identities are independent of routes and automation IDs. State is application-owned;
/// native controls, hosts, and platform contexts must not be stored in persistent snapshots.
/// Dispose this stack before its UI host. A failed cleanup never resurrects a retired entry.
/// </remarks>
public sealed class NavigationStack<TState> : IDisposable
{
    private readonly Host host;
    private readonly int thread = Environment.CurrentManagedThreadId;
    private readonly List<Entry> entries = [];
    private readonly ReadOnlyCollection<Entry> view;
    private bool disposed;
    private bool transitioning;
    private Action? changed;

    public NavigationStack(Host host)
    {
        ArgumentNullException.ThrowIfNull(host);
        host.VerifyMutation();
        this.host = host;
        view = entries.AsReadOnly();
    }

    public IReadOnlyList<Entry> Entries { get { VerifyAccess(); return view; } }
    public Entry? Current { get { VerifyAccess(); return entries.Count == 0 ? null : entries[^1]; } }
    public bool CanGoBack { get { VerifyAccess(); return entries.Count > 1; } }
    public event Action Changed
    {
        add { VerifyMutation(); changed += value; }
        remove { VerifyThread(); changed -= value; }
    }

    public Entry Push(string route, TState state, IDisposable? resources = null)
    {
        VerifyMutation();
        var entry = Create(route, state, resources);
        Change(() => entries.Add(entry), []);
        return entry;
    }

    public Entry Replace(string route, TState state, IDisposable? resources = null)
    {
        VerifyMutation();
        var entry = Create(route, state, resources);
        Entry[] retired = entries.Count == 0 ? [] : [entries[^1]];
        Change(() =>
        {
            if (entries.Count != 0) entries.RemoveAt(entries.Count - 1);
            entries.Add(entry);
        }, retired);
        return entry;
    }

    public bool Back()
    {
        VerifyMutation();
        if (entries.Count <= 1) return false;
        var retired = entries[^1];
        Change(() => entries.RemoveAt(entries.Count - 1), [retired]);
        return true;
    }

    public Entry Reset(string route, TState state, IDisposable? resources = null)
    {
        VerifyMutation();
        var entry = Create(route, state, resources);
        var retired = entries.AsEnumerable().Reverse().ToArray();
        Change(() => { entries.Clear(); entries.Add(entry); }, retired);
        return entry;
    }

    private Entry Create(string route, TState state, IDisposable? resources)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(route);
        Values.Text(route);
        if (resources is not null && entries.Any(entry => ReferenceEquals(entry.Resources, resources)))
            throw new InvalidOperationException("Navigation entries must not share owned resources.");
        return new Entry(route, state, resources);
    }

    private void Change(Action commit, IReadOnlyList<Entry> retired)
    {
        transitioning = true;
        var failures = new List<Exception>();
        try
        {
            commit();
            foreach (var entry in retired)
            {
                try { entry.Retire(); }
                catch (Exception error) { failures.Add(error); }
            }
            try { changed?.Invoke(); }
            catch (Exception error) { failures.Add(error); }
        }
        finally { transitioning = false; }
        if (failures.Count != 0) throw new AggregateException("Navigation changed, but cleanup or notification failed.", failures);
    }

    private void VerifyAccess()
    {
        host.VerifyAccess();
        ObjectDisposedException.ThrowIf(disposed, this);
    }

    private void VerifyThread()
    {
        if (Environment.CurrentManagedThreadId != thread)
            throw new InvalidOperationException("Navigation requires its creating UI thread.");
    }

    private void VerifyMutation()
    {
        VerifyAccess();
        host.VerifyMutation();
        if (transitioning) throw new InvalidOperationException("Navigation cannot change during cleanup or notification.");
    }

    public void Dispose()
    {
        VerifyThread();
        if (disposed) return;
        if (transitioning) throw new InvalidOperationException("Navigation cannot be disposed during a transition.");
        disposed = true;
        var retired = entries.AsEnumerable().Reverse().ToArray();
        entries.Clear();
        changed = null;
        var failures = new List<Exception>();
        foreach (var entry in retired)
        {
            try { entry.Retire(); }
            catch (Exception error) { failures.Add(error); }
        }
        if (failures.Count != 0) throw new AggregateException("Navigation cleanup failed.", failures);
    }

    public sealed class Entry
    {
        private readonly CancellationTokenSource lifetime = new();
        internal IDisposable? Resources { get; private set; }
        public Guid Id { get; } = Guid.NewGuid();
        public string Route { get; }
        public TState State { get; }
        public CancellationToken Lifetime { get; }
        public bool IsRetired { get; private set; }

        internal Entry(string route, TState state, IDisposable? resources)
        {
            Route = route;
            State = state;
            Resources = resources;
            Lifetime = lifetime.Token;
        }

        internal void Retire()
        {
            if (IsRetired) return;
            IsRetired = true;
            var resources = Resources;
            Resources = null;
            var failures = new List<Exception>();
            try { lifetime.Cancel(); }
            catch (Exception error) { failures.Add(error); }
            try { resources?.Dispose(); }
            catch (Exception error) { failures.Add(error); }
            lifetime.Dispose();
            if (failures.Count != 0) throw new AggregateException("Page resources failed to retire.", failures);
        }
    }
}
