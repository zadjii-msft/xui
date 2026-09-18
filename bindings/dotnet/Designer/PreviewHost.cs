using System.Reflection;
using System.Runtime.Loader;

namespace Xui.Designer;

internal sealed class PreviewHost : IDisposable
{
    private sealed record Request(long Version, byte[] Assembly);
    private readonly Window window;
    private readonly ContentHost host;
    private readonly Action<long, string, bool> report;
    private readonly object gate = new();
    private Request? pending;
    private Candidate? current;
    private long version;
    private bool posted, stopping;
    private bool highlightRequested, highlightClearPending, highlightClearPosted;

    internal PreviewHost(Window window, Action<long, string, bool> report)
    {
        this.window = window;
        this.report = report;
        host = window.CreateContentHost();
    }

    internal Element View => host;
    internal event Action<PreviewPick>? Picked;
    internal void SetPointerPickMode(bool enabled)
    {
        window.VerifyAccess();
        ObjectDisposedException.ThrowIf(stopping, this);
        host.SetPointerPickMode(enabled);
    }
    internal long? AppliedVersion { get { window.VerifyAccess(); return current?.Version; } }

    internal PreviewHighlightResult TryHighlight(long expectedVersion, int? nodeId)
    {
        window.VerifyAccess();
        lock (gate)
        {
            if (stopping || current is null || current.Version != expectedVersion || version != expectedVersion)
                return PreviewHighlightResult.StaleVersion;
            var result = current.Highlight(nodeId);
            highlightRequested = result == ContentHighlightResult.Applied;
            highlightClearPending = false;
            return result switch
            {
                ContentHighlightResult.Applied => PreviewHighlightResult.Applied,
                ContentHighlightResult.Cleared => PreviewHighlightResult.Cleared,
                ContentHighlightResult.NotVisible => PreviewHighlightResult.NotVisible,
                ContentHighlightResult.OccludedNative => PreviewHighlightResult.OccludedNative,
                ContentHighlightResult.UnsupportedSurface => PreviewHighlightResult.UnsupportedSurface,
                _ => throw new InvalidOperationException("Unknown preview highlight result.")
            };
        }
    }

    internal bool TryReadNodeMap(long expectedVersion, out IReadOnlyList<PreviewNodeSnapshot> nodes)
    {
        window.VerifyAccess();
        nodes = Array.Empty<PreviewNodeSnapshot>();
        if (current is not { } candidate || candidate.Version != expectedVersion) return false;
        var snapshot = new PreviewNodeSnapshot[candidate.NodeCount];
        for (int nodeId = 0; nodeId < snapshot.Length; nodeId++) snapshot[nodeId] = candidate.ReadNode(nodeId);
        nodes = Array.AsReadOnly(snapshot);
        return true;
    }

    internal bool TryReadNode(long expectedVersion, int nodeId, out PreviewNodeSnapshot node)
    {
        window.VerifyAccess();
        node = default;
        if (current is not { } candidate || candidate.Version != expectedVersion) return false;
        node = candidate.ReadNode(nodeId);
        return true;
    }

    internal bool TryReadNodeStyle(long expectedVersion, int nodeId, StylePart part, out PartStyleValues? values)
    {
        window.VerifyAccess();
        values = null;
        if (current is not { } candidate || candidate.Version != expectedVersion) return false;
        values = candidate.ReadNodeStyle(nodeId, part);
        return true;
    }

    internal void Supersede(long value)
    {
        lock (gate)
        {
            if (value < version) return;
            version = value;
            pending = null;
            if (highlightRequested)
            {
                highlightClearPending = true;
                if (!highlightClearPosted)
                {
                    highlightClearPosted = true;
                    if (!window.Post(ClearSupersededHighlight))
                    {
                        highlightClearPosted = highlightClearPending = false;
                        Console.Error.WriteLine("Preview highlight clear was rejected because the designer window is closed.");
                    }
                }
            }
        }
    }

    private void ClearSupersededHighlight()
    {
        window.VerifyAccess();
        lock (gate)
        {
            highlightClearPosted = false;
            if (stopping || !highlightClearPending) return;
            current?.Highlight(null);
            highlightRequested = highlightClearPending = false;
        }
    }

    internal void Publish(long value, byte[] assembly, Theme theme)
    {
        ArgumentNullException.ThrowIfNull(assembly);
        if (!Enum.IsDefined(theme)) throw new ArgumentOutOfRangeException(nameof(theme));
        lock (gate)
        {
            if (stopping || version != value) return;
            pending = new(value, assembly);
            if (posted) return;
            posted = true;
            if (!window.Post(Refresh))
            {
                posted = false;
                pending = null;
                report(value, "Preview update rejected because the designer window is closed.", false);
            }
        }
    }

    private void Refresh()
    {
        Request? request;
        lock (gate)
        {
            posted = false;
            request = pending;
            pending = null;
            if (stopping || request is null || request.Version != version) return;
        }
        Candidate? candidate = null;
        try
        {
            candidate = new Candidate(window, host, request, OnCallbackError, OnPicked);
        }
        catch (Exception error)
        {
            ReportError(request.Version, "Preview construction failed", error);
            return;
        }
        try
        {
            lock (gate)
            {
                if (stopping || request.Version != version) return;
                // Commit creates native peers and completes layout before it returns.
                candidate.Commit();
                var previous = current;
                current = candidate;
                candidate = null;
                highlightRequested = highlightClearPending = false;
                previous?.Dispose();
            }
            report(request.Version, "Preview updated. Component state was reset.", true);
        }
        catch (XuiException error) when (candidate is not null && error.Status is 1 or 7)
        {
            candidate.Dispose();
            candidate = null;
            ReportError(request.Version, "Preview replacement rejected", error);
        }
        finally { candidate?.Dispose(); }
    }

    private void OnCallbackError(Candidate candidate, Exception error)
    {
        if (!ReferenceEquals(current, candidate)) return;
        current = null;
        candidate.Dispose();
        lock (gate) highlightRequested = highlightClearPending = false;
        ReportError(candidate.Version, "Preview callback failed", error);
    }

    private void OnPicked(Candidate candidate, int nodeId)
    {
        window.VerifyAccess();
        lock (gate)
        {
            if (stopping || !ReferenceEquals(current, candidate) || candidate.Version != version) return;
            Picked?.Invoke(new(candidate.Version, nodeId));
        }
    }

    private void ReportError(long value, string message, Exception error)
    {
        var detail = error is TargetInvocationException { InnerException: { } inner } ? inner : error;
        Console.Error.WriteLine($"{message}: {detail}");
        lock (gate)
        {
            if (stopping || value != version) return;
        }
        report(value, $"{message}: {detail.Message}", false);
    }

    public void Dispose()
    {
        window.VerifyAccess();
        lock (gate)
        {
            if (stopping) return;
            stopping = true;
            pending = null;
            highlightRequested = highlightClearPending = false;
        }
        current?.Dispose();
        current = null;
        Picked = null;
    }

    private sealed class Candidate : IDisposable
    {
        private readonly AssemblyLoadContext context = new("XUI embedded preview", isCollectible: true);
        private readonly ContentUpdate update;
        private object? component;
        private Element? root;
        private Func<object, int, Element>? getNode;
        private bool disposed;
        internal long Version { get; }
        internal int NodeCount { get; }

        internal Candidate(Window window, ContentHost host, Request request, Action<Candidate, Exception> failed,
            Action<Candidate, int> picked)
        {
            Version = request.Version;
            update = host.BeginUpdate();
            update.CallbackFailed += error => failed(this, error);
            try
            {
                using var stream = new MemoryStream(request.Assembly, writable: false);
                var assembly = context.LoadFromStream(stream);
                var wrapper = assembly.GetType("Xui.Designer.GeneratedPreview", throwOnError: true)!;
                var build = wrapper.GetMethod("Build", BindingFlags.Public | BindingFlags.Static)
                    ?? throw new MissingMethodException("The preview entry point is missing.");
                var getRoot = wrapper.GetMethod("Root", BindingFlags.Public | BindingFlags.Static)
                    ?? throw new MissingMethodException("The preview root entry point is missing.");
                component = build.Invoke(null, [window])
                    ?? throw new InvalidOperationException("The preview component is missing.");
                root = getRoot.Invoke(null, [component]) as Element
                    ?? throw new InvalidOperationException("The preview root is not an XUI element.");
                var count = wrapper.GetMethod("NodeCount", BindingFlags.Public | BindingFlags.Static)?
                    .CreateDelegate<Func<object, int>>()
                    ?? throw new MissingMethodException("The preview node-count entry point is missing.");
                getNode = wrapper.GetMethod("Node", BindingFlags.Public | BindingFlags.Static)?
                    .CreateDelegate<Func<object, int, Element>>()
                    ?? throw new MissingMethodException("The preview node entry point is missing.");
                NodeCount = count(component);
                if (NodeCount < 1) throw new InvalidOperationException("The preview node map must include its root.");
                var targets = new ContentInspectionTarget[NodeCount];
                for (int nodeId = 0; nodeId < targets.Length; nodeId++)
                    targets[nodeId] = new(nodeId, getNode(component, nodeId));
                update.Picked += nodeId => picked(this, nodeId);
                update.SetInspectionTargets(targets);
                if (update.CallbackError is { } error)
                    throw new InvalidOperationException("Candidate construction raised a callback error.", error);
            }
            catch
            {
                Dispose();
                throw;
            }
        }

        internal void Commit() => update.Commit(root ?? throw new ObjectDisposedException(nameof(Candidate)));
        internal ContentHighlightResult Highlight(int? nodeId) => update.Highlight(nodeId);

        internal PreviewNodeSnapshot ReadNode(int nodeId)
        {
            if ((uint)nodeId >= (uint)NodeCount) throw new ArgumentOutOfRangeException(nameof(nodeId));
            ObjectDisposedException.ThrowIf(disposed, this);
            var element = getNode!(component!, nodeId);
            return new(Version, nodeId, element.GetType().Name, element.GetBounds(), (element as Control)?.Id);
        }

        internal PartStyleValues ReadNodeStyle(int nodeId, StylePart part)
        {
            if ((uint)nodeId >= (uint)NodeCount) throw new ArgumentOutOfRangeException(nameof(nodeId));
            ObjectDisposedException.ThrowIf(disposed, this);
            return getNode!(component!, nodeId).GetControlStyleValues(part, effective: true);
        }

        public void Dispose()
        {
            if (disposed) return;
            update.Dispose();
            disposed = true;
            GC.KeepAlive(component);
            component = null;
            root = null;
            getNode = null;
            context.Unload();
        }
    }
}
