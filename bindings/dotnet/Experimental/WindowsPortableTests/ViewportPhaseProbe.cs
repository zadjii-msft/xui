using System.Diagnostics;
using P = Xui.Experimental.Portable;

internal sealed class ViewportPhaseProbe(P.IBackend backend) : P.IBackendTreePreflight
{
    private readonly Dictionary<string, (int Count, double Milliseconds)> timings = [];

    private T Measure<T>(string name, Func<T> action)
    {
        long started = Stopwatch.GetTimestamp();
        try { return action(); }
        finally
        {
            var previous = timings.GetValueOrDefault(name);
            timings[name] = (previous.Count + 1, previous.Milliseconds + Stopwatch.GetElapsedTime(started).TotalMilliseconds);
        }
    }
    private void Measure(string name, Action action) => Measure(name, () => { action(); return true; });
    internal void Clear() => timings.Clear();
    internal string Report() => string.Join("; ", timings.OrderByDescending(pair => pair.Value.Milliseconds)
        .Select(pair => $"{pair.Key}={pair.Value.Milliseconds:F2}ms/{pair.Value.Count}"));

    public P.IElementPeer Create(P.Element element, P.IControlEvents events) =>
        new Peer(this, Measure("Create", () => backend.Create(element, events)));
    public void Mount(P.IElementPeer root) => Measure("Mount", () => backend.Mount(((Peer)root).Native));
    public void Dispose() => Measure("Backend.Dispose", backend.Dispose);
    public void ValidateTree(P.Element root) => ((P.IBackendTreePreflight)backend).ValidateTree(root);
    public void ValidateInsertion(P.Element parent, P.Element candidateRoot) =>
        ((P.IBackendTreePreflight)backend).ValidateInsertion(parent, candidateRoot);

    private sealed class Peer(ViewportPhaseProbe probe, P.IElementPeer native) :
        P.IMutationPreflightPeer, P.IConstrainedElementPeer, P.ITextSelectionPeer, P.IVirtualViewportPeer, P.IVirtualItemPeer
    {
        internal P.IElementPeer Native => native;
        public void AddChild(P.IElementPeer child) => probe.Measure("AddChild", () => native.AddChild(((Peer)child).Native));
        public void Update(P.ElementProperty property) => probe.Measure("Update." + property, () => native.Update(property));
        public void Dispose() => probe.Measure("Peer.Dispose", native.Dispose);
        public void ValidateMutation() => probe.Measure("ValidateMutation", ((P.IMutationPreflightPeer)native).ValidateMutation);
        public void InsertChild(int index, P.IElementPeer child) =>
            probe.Measure("Insert", () => ((P.IMutableElementPeer)native).InsertChild(index, ((Peer)child).Native));
        public void RemoveChild(P.IElementPeer child) =>
            probe.Measure("Remove", () => ((P.IMutableElementPeer)native).RemoveChild(((Peer)child).Native));
        public void ValidateMove(P.IElementPeer child, int index) =>
            probe.Measure("ValidateMove", () => ((P.IMutableElementPeer)native).ValidateMove(((Peer)child).Native, index));
        public void MoveChild(P.IElementPeer child, int index) =>
            probe.Measure("Move", () => ((P.IMutableElementPeer)native).MoveChild(((Peer)child).Native, index));
        public bool TryFocus() => probe.Measure("Focus", ((P.IFocusableElementPeer)native).TryFocus);
        public bool HasFocus => probe.Measure("HasFocus", () => ((P.IFocusableElementPeer)native).HasFocus);
        public P.TextSelection Selection
        {
            get => probe.Measure("GetSelection", () => ((P.ITextSelectionPeer)native).Selection);
            set => probe.Measure("SetSelection", () => ((P.ITextSelectionPeer)native).Selection = value);
        }
        public void SetVirtualItemInfo(P.VirtualItemInfo info) =>
            probe.Measure("SetItem", () => ((P.IVirtualItemPeer)native).SetVirtualItemInfo(info));
        public P.IVirtualViewportLease BeginVirtualViewport(int count, float pitch, long version, Action<P.VirtualViewportRequest> requested) =>
            new Lease(probe, probe.Measure("BeginLease",
                () => ((P.IVirtualViewportPeer)native).BeginVirtualViewport(count, pitch, version, requested)));
    }

    private sealed class Lease(ViewportPhaseProbe probe, P.IVirtualViewportLease native) : P.ISettledVirtualViewportLease
    {
        public void SetExtent(int count, long version) => probe.Measure("SetExtent", () => native.SetExtent(count, version));
        public void RequestOffset(float offset) => probe.Measure("RequestOffset", () => native.RequestOffset(offset));
        public P.VirtualViewportUpdateResult TryBeginUpdate(long epoch) => probe.Measure("BeginUpdate", () => native.TryBeginUpdate(epoch));
        public P.VirtualViewportCommitResult TryCommit(long epoch) => probe.Measure("Commit", () => native.TryCommit(epoch));
        public void Cancel(long epoch) => probe.Measure("Cancel", () => native.Cancel(epoch));
        public void FlushCommitted(long epoch) =>
            probe.Measure("FlushCommitted", () => ((P.ISettledVirtualViewportLease)native).FlushCommitted(epoch));
        public void Dispose() => probe.Measure("Lease.Dispose", native.Dispose);
    }
}
