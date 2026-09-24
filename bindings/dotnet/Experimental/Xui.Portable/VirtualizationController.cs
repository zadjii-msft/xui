using System.Collections.ObjectModel;

namespace Xui.Experimental.Portable;

public enum VirtualizationUpdate { Applied, Deferred, Stale }

public sealed class VirtualizationController<TRow> : IDisposable where TRow : class, IPortableComponent
{
    private readonly Host host;
    private readonly KeyedStack container;
    private readonly Func<Host, string, TRow> create;
    private readonly Action<TRow, string>? update;
    private readonly Dictionary<string, (bool Focused, bool Composing)> interactions = new(StringComparer.Ordinal);
    private Dictionary<string, TRow> mounted = new(StringComparer.Ordinal);
    private string[] keys = [];
    private Dictionary<string, int> indices = new(StringComparer.Ordinal);
    private string[]? pendingKeys;
    private float requestedOffset;
    private float requestedHeight;
    private bool blocked;
    private bool changing;
    private bool disposed;
    private bool preparing;
    private bool viewportPrepared;
    private long viewportEpoch;
    private long completedEpoch;
    private float preparedOffset;
    private float preparedHeight;
    private float? requestedWidth;
    private float? preparedWidth;
    private float offset;
    private float viewportHeight;
    private float? viewportWidth;
    private bool isDeferred;
    private long sourceVersion;
    private long latestSourceVersion;
    private long pendingSourceVersion;
    private long preparingSourceVersion;
    private bool sourceRequiresBatch;
    private string[]? preparedKeys;
    private Dictionary<string, int>? preparedIndices;

    public float RowHeight { get; }
    public int Overscan { get; }
    public float Offset { get { VerifyRead(); return offset; } private set => offset = value; }
    public float ViewportHeight { get { VerifyRead(); return viewportHeight; } private set => viewportHeight = value; }
    public float? ViewportWidth { get { VerifyRead(); return viewportWidth; } private set => viewportWidth = value; }
    public float Extent { get { VerifyRead(); return VirtualizationMath.Extent(keys.Length, RowHeight); } }
    public int Count { get { VerifyRead(); return keys.Length; } }
    public long SourceVersion { get { VerifyRead(); return sourceVersion; } }
    public long RequestedSourceVersion { get { VerifyRead(); return pendingKeys is null ? sourceVersion : pendingSourceVersion; } }
    public int RequestedCount { get { VerifyRead(); return pendingKeys?.Length ?? keys.Length; } }
    public bool IsDeferred { get { VerifyRead(); return isDeferred; } private set => isDeferred = value; }
    public long PreparedEpoch { get { VerifyRead(); return preparing && viewportPrepared ? viewportEpoch : 0; } }
    public IReadOnlyDictionary<string, TRow> Mounted { get { VerifyRead(); return new ReadOnlyDictionary<string, TRow>(mounted); } }
    public event Action<string, TRow>? Retiring;

    public VirtualizationController(Host host, KeyedStack container, float rowHeight, int overscan,
        Func<Host, string, TRow> create, Action<TRow, string>? update = null)
    {
        ArgumentNullException.ThrowIfNull(host);
        ArgumentNullException.ThrowIfNull(container);
        ArgumentNullException.ThrowIfNull(create);
        host.VerifyComponentMutation(container);
        VirtualizationMath.Extent(0, rowHeight);
        ArgumentOutOfRangeException.ThrowIfNegative(overscan);
        if (container.Axis != Axis.Vertical || container.SpacingValue != 0 || container.PaddingValue != 0 ||
            container.Children.Count != 0)
            throw new ArgumentException("Virtualization requires an empty vertical keyed stack without spacing or padding.", nameof(container));
        this.host = host;
        this.container = container;
        this.create = create;
        this.update = update;
        RowHeight = rowHeight;
        Overscan = overscan;
    }

    /// <summary>Immediately reconciles an unleased source. Native viewport leases must use QueueKeys.</summary>
    public VirtualizationUpdate SetKeys(IEnumerable<string> source)
    {
        QueueKeys(source, requiresBatch: false);
        return Refresh();
    }

    /// <summary>Validates an immutable candidate without publishing its count, order, or native extent.</summary>
    public long QueueKeys(IEnumerable<string> source) => QueueKeys(source, requiresBatch: true);

    private long QueueKeys(IEnumerable<string> source, bool requiresBatch)
    {
        Verify();
        ArgumentNullException.ThrowIfNull(source);
        var snapshot = source.ToArray();
        ValidateKeys(snapshot);
        VirtualizationMath.Extent(snapshot.Length, RowHeight);
        long version = checked(latestSourceVersion + 1);
        pendingKeys = snapshot;
        pendingSourceVersion = latestSourceVersion = version;
        sourceRequiresBatch = requiresBatch;
        IsDeferred = true;
        return version;
    }

    public VirtualizationUpdate SetViewport(float offset, float height)
    {
        Verify();
        if (preparing) throw new InvalidOperationException("Complete or reset the prepared viewport before committing direct geometry.");
        VirtualizationMath.ClampOffset(keys.Length, RowHeight, offset, height);
        requestedOffset = offset;
        requestedHeight = height;
        return Refresh();
    }

    public VirtualizationUpdate SetViewport(float offset, float width, float height)
    {
        Verify();
        Values.Length(width);
        VirtualizationMath.ClampOffset(keys.Length, RowHeight, offset, height);
        if (preparing) throw new InvalidOperationException("Complete or reset the prepared viewport before committing direct geometry.");
        requestedWidth = width;
        return SetViewport(offset, height);
    }

    public VirtualizationUpdate PrepareViewport(VirtualViewportRequest request)
    {
        Verify();
        request.Validate();
        if (request.Epoch < viewportEpoch || request.Epoch <= completedEpoch) return VirtualizationUpdate.Stale;
        VerifySourcePreparation(request.Epoch, request.RequestedSourceVersion);
        if (request.RequestedSourceVersion != sourceVersion &&
            (pendingKeys is null || request.RequestedSourceVersion != pendingSourceVersion))
            return VirtualizationUpdate.Stale;
        int requestedCount = request.RequestedSourceVersion == sourceVersion ? keys.Length : pendingKeys!.Length;
        if (request.Requested.Extent != VirtualizationMath.Extent(requestedCount, RowHeight))
            throw new InvalidOperationException("The native request extent does not match its declared source and row pitch.");
        if (request.Requested.Offset != VirtualizationMath.ClampOffset(requestedCount, RowHeight,
            request.Requested.Offset, request.Requested.Height))
            throw new InvalidOperationException("The native request offset is not normalized against its declared extent.");
        preparingSourceVersion = request.RequestedSourceVersion;
        blocked = request.IsBlocked;
        return PrepareViewport(request.Epoch, request.Requested.Offset, request.Requested.Width, request.Requested.Height);
    }

    public VirtualizationUpdate PrepareViewport(long epoch, float offset, float width, float height)
    {
        Verify();
        Values.Length(width);
        ArgumentOutOfRangeException.ThrowIfNegativeOrZero(epoch);
        VirtualizationMath.ClampOffset(keys.Length, RowHeight, offset, height);
        if (epoch < viewportEpoch || epoch <= completedEpoch) return VirtualizationUpdate.Stale;
        VerifySourcePreparation(epoch, preparingSourceVersion);
        requestedWidth = width;
        return PrepareViewport(epoch, offset, height);
    }

    public VirtualizationUpdate PrepareViewport(long epoch, float offset, float height)
    {
        Verify();
        ArgumentOutOfRangeException.ThrowIfNegativeOrZero(epoch);
        VirtualizationMath.ClampOffset(keys.Length, RowHeight, offset, height);
        if (epoch < viewportEpoch || epoch <= completedEpoch) return VirtualizationUpdate.Stale;
        VerifySourcePreparation(epoch, preparingSourceVersion);
        viewportEpoch = epoch;
        requestedOffset = offset;
        requestedHeight = height;
        preparing = true;
        if (preparingSourceVersion == 0) preparingSourceVersion = sourceVersion;
        viewportPrepared = false;
        Refresh();
        return viewportPrepared ? VirtualizationUpdate.Applied : VirtualizationUpdate.Deferred;
    }

    public bool CommitPreparedViewport(long epoch)
    {
        Verify();
        if (!preparing || !viewportPrepared || epoch != viewportEpoch || blocked ||
            interactions.Values.Any(state => state.Composing)) return false;
        if (preparedKeys is not null)
        {
            keys = preparedKeys;
            indices = preparedIndices!;
            sourceVersion = preparingSourceVersion;
            if (pendingSourceVersion == sourceVersion) pendingKeys = null;
        }
        Offset = preparedOffset;
        ViewportHeight = preparedHeight;
        ViewportWidth = preparedWidth;
        completedEpoch = epoch;
        preparing = false;
        viewportPrepared = false;
        preparedKeys = null;
        preparedIndices = null;
        preparingSourceVersion = 0;
        Refresh();
        return true;
    }

    public bool CancelPreparedViewport(long epoch)
    {
        Verify();
        if (!preparing || epoch != viewportEpoch) return false;
        if (viewportPrepared || preparedKeys is not null)
            throw new InvalidOperationException("Viewport staging committed to the model. Detach the failed native attachment instead of claiming rollback.");
        completedEpoch = epoch;
        preparing = false;
        viewportPrepared = false;
        preparingSourceVersion = 0;
        requestedOffset = Offset;
        requestedHeight = ViewportHeight;
        requestedWidth = ViewportWidth;
        return true;
    }

    public void ResetViewportLease()
    {
        Verify();
        if (preparedKeys is not null && host.IsAttached)
            throw new InvalidOperationException("Detach a failed attachment before resetting source staging that committed to the model.");
        preparing = false;
        viewportPrepared = false;
        preparedKeys = null;
        preparedIndices = null;
        preparingSourceVersion = 0;
        viewportEpoch = completedEpoch = 0;
        requestedOffset = Offset;
        requestedHeight = ViewportHeight;
        requestedWidth = ViewportWidth;
        blocked = false;
        interactions.Clear();
        Refresh();
    }

    public void PrepareForViewportAttachment()
    {
        Verify();
        if (host.IsAttached) throw new InvalidOperationException("Detach before clearing a virtual viewport presentation.");
        string[] desired = pendingKeys ?? preparedKeys ?? keys;
        long desiredVersion = pendingKeys is not null ? pendingSourceVersion :
            preparedKeys is not null ? preparingSourceVersion : sourceVersion;
        changing = true;
        try
        {
            foreach (var row in mounted) Retiring?.Invoke(row.Key, row.Value);
            try { container.Reconcile([]); }
            catch (KeyedUpdateException error) when (error.ModelCommitted)
            {
                ClearPresentation();
                throw;
            }
            ClearPresentation();
            container.SetWidth(null);
        }
        finally { changing = false; }

        void ClearPresentation()
        {
            keys = [];
            indices = new(StringComparer.Ordinal);
            mounted = new(StringComparer.Ordinal);
            interactions.Clear();
            pendingKeys = desiredVersion == 0 ? null : desired;
            pendingSourceVersion = desiredVersion;
            sourceVersion = preparingSourceVersion = viewportEpoch = completedEpoch = 0;
            offset = viewportHeight = requestedOffset = requestedHeight = preparedOffset = preparedHeight = 0;
            viewportWidth = requestedWidth = preparedWidth = null;
            preparing = viewportPrepared = blocked = false;
            preparedKeys = null;
            preparedIndices = null;
            sourceRequiresBatch = true;
            IsDeferred = pendingKeys is not null;
        }
    }

    public VirtualizationUpdate SetInteraction(string key, bool focused, bool composing)
    {
        ObserveInteraction(key, new(focused, composing));
        return Refresh();
    }

    public void ObserveInteraction(string key, TextInteraction interaction)
    {
        Verify();
        if (!indices.ContainsKey(key)) throw new ArgumentException("The row is not in the committed source.", nameof(key));
        if (interaction.HasFocus || interaction.IsComposing)
            interactions[key] = (interaction.HasFocus, interaction.IsComposing);
        else interactions.Remove(key);
    }

    public VirtualizationUpdate SetStructuralChangesBlocked(bool value)
    {
        Verify();
        blocked = value;
        return Refresh();
    }

    public float OffsetFor(string key)
    {
        Verify();
        if (!indices.TryGetValue(key, out int index)) throw new ArgumentException("The row is not in the committed source.", nameof(key));
        return VirtualizationMath.Reveal(keys.Length, RowHeight, index, Offset, ViewportHeight);
    }

    public bool TryGetRevealOffset(string key, long version, float offset, float height, out float result)
    {
        Verify();
        ArgumentNullException.ThrowIfNull(key);
        ArgumentOutOfRangeException.ThrowIfNegativeOrZero(version);
        VirtualizationMath.ClampOffset(0, RowHeight, offset, height);
        string[]? source = version == sourceVersion ? keys :
            version == pendingSourceVersion ? pendingKeys : null;
        int index = source is null ? -1 : Array.IndexOf(source, key);
        result = index < 0 ? 0 : VirtualizationMath.Reveal(source!.Length, RowHeight, index, offset, height);
        return index >= 0;
    }

    public VirtualItemInfo GetPreparedItemInfo(string key)
    {
        VerifyRead();
        if (!preparing || !viewportPrepared) throw new InvalidOperationException("Row metadata requires a prepared viewport.");
        if (!mounted.ContainsKey(key)) throw new ArgumentException("The row is not mounted.", nameof(key));
        var source = preparedKeys ?? keys;
        var lookup = preparedIndices ?? indices;
        return new(key, lookup[key], source.Length, preparedKeys is null ? sourceVersion : preparingSourceVersion);
    }

    public VirtualizationUpdate Refresh()
    {
        Verify();
        bool sourcePreparation = preparing && preparingSourceVersion != sourceVersion;
        if (blocked || interactions.Values.Any(state => state.Composing) || (sourcePreparation && interactions.Count != 0))
        {
            viewportPrepared = false;
            IsDeferred = true;
            return VirtualizationUpdate.Deferred;
        }
        bool sourceDeferred = pendingKeys is not null &&
            (interactions.Count != 0 || (!sourcePreparation && (preparing || sourceRequiresBatch)));
        string[] nextKeys = sourcePreparation
            ? preparedKeys ?? (pendingSourceVersion == preparingSourceVersion ? pendingKeys : null) ??
                throw new InvalidOperationException("The source preparation no longer has its validated snapshot.")
            : sourceDeferred ? keys : pendingKeys ?? keys;
        var nextIndices = ReferenceEquals(nextKeys, keys) ? indices : ValidateKeys(nextKeys);
        float nextOffset = VirtualizationMath.ClampOffset(nextKeys.Length, RowHeight, requestedOffset, requestedHeight);
        var range = VirtualizationMath.Visible(nextKeys.Length, RowHeight, nextOffset, requestedHeight, Overscan);
        IEnumerable<int> pins = interactions.Keys.Select(key => nextIndices[key]);
        if (preparing && !sourcePreparation)
        {
            var previous = VirtualizationMath.Visible(keys.Length, RowHeight, Offset, ViewportHeight, Overscan);
            pins = pins.Concat(Enumerable.Range(previous.Start, previous.Count));
        }
        var runs = VirtualizationMath.Runs(nextKeys.Length, range, pins);
        var descriptors = new List<KeyedItem>();
        var nextMounted = new Dictionary<string, TRow>(StringComparer.Ordinal);
        foreach (var run in runs)
        {
            if (run.IsGap)
            {
                float height = VirtualizationMath.Extent(run.Count, RowHeight);
                descriptors.Add(KeyedItem.Create($"gap:{run.Start}",
                    owner => new Gap(owner, height), gap => gap.Root.SetHeight(AxisConstraints.Fixed(height))));
                continue;
            }
            for (int i = run.Start; i < run.Start + run.Count; i++)
            {
                string key = nextKeys[i];
                if (mounted.TryGetValue(key, out var retained)) nextMounted.Add(key, retained);
                descriptors.Add(KeyedItem.Create($"row:{key}",
                    owner =>
                    {
                        var row = create(owner, key) ?? throw new InvalidOperationException("The row factory returned null.");
                        row.Root.SetHeight(AxisConstraints.Fixed(RowHeight));
                        nextMounted.Add(key, row);
                        return row;
                    },
                    row => update?.Invoke(row, key)));
            }
        }
        changing = true;
        try
        {
            if (requestedWidth is float width) container.SetWidth(AxisConstraints.Fixed(width));
            foreach (var entry in mounted)
                if (!nextMounted.ContainsKey(entry.Key)) Retiring?.Invoke(entry.Key, entry.Value);
            try { container.Reconcile(descriptors); }
            catch (KeyedUpdateException error) when (error.ModelCommitted)
            {
                Commit();
                throw;
            }
            Commit();
        }
        finally { changing = false; }
        return sourceDeferred ? VirtualizationUpdate.Deferred : VirtualizationUpdate.Applied;

        void Commit()
        {
            if (sourcePreparation)
            {
                preparedKeys = nextKeys;
                preparedIndices = nextIndices;
            }
            else
            {
                keys = nextKeys;
                indices = nextIndices;
                if (pendingKeys is not null && !sourceDeferred) sourceVersion = pendingSourceVersion;
            }
            mounted = nextMounted;
            if (preparing)
            {
                preparedOffset = nextOffset;
                preparedHeight = requestedHeight;
                preparedWidth = requestedWidth;
                viewportPrepared = true;
            }
            else
            {
                Offset = nextOffset;
                ViewportHeight = requestedHeight;
                ViewportWidth = requestedWidth;
            }
            if (!sourceDeferred && !sourcePreparation) pendingKeys = null;
            IsDeferred = sourceDeferred || sourcePreparation;
        }
    }

    private static Dictionary<string, int> ValidateKeys(string[] snapshot)
    {
        var result = new Dictionary<string, int>(snapshot.Length, StringComparer.Ordinal);
        for (int i = 0; i < snapshot.Length; i++)
        {
            VirtualItemInfo.ValidateKey(snapshot[i]);
            if (!result.TryAdd(snapshot[i], i)) throw new ArgumentException($"Duplicate row key '{snapshot[i]}'.", nameof(snapshot));
        }
        return result;
    }

    private void Verify()
    {
        ObjectDisposedException.ThrowIf(disposed, this);
        host.VerifyComponentMutation(container);
        if (changing) throw new InvalidOperationException("A virtualization callback cannot reenter its controller.");
    }

    private void VerifyRead()
    {
        ObjectDisposedException.ThrowIf(disposed, this);
        host.VerifyComponent(container);
    }

    private void VerifySourcePreparation(long epoch, long version)
    {
        if (preparedKeys is not null && (epoch != viewportEpoch || version != preparingSourceVersion))
            throw new InvalidOperationException("Complete the reserved source update or detach its failed attachment before preparing another epoch.");
    }

    public void Dispose()
    {
        if (disposed) return;
        host.VerifyAccess();
        if (changing) throw new InvalidOperationException("A virtualization callback cannot dispose its controller.");
        disposed = true;
        Retiring = null;
        mounted.Clear();
        interactions.Clear();
        keys = [];
        indices.Clear();
        pendingKeys = null;
        preparedKeys = null;
        preparedIndices = null;
    }

    private sealed class Gap : IPortableComponent
    {
        public Element Root { get; }
        public Gap(Host owner, float height)
        {
            Root = owner.Stack(Axis.Vertical);
            Root.SetHeight(AxisConstraints.Fixed(height));
        }
    }
}
